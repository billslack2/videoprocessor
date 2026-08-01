#include "pch.h"
#include "CppUnitTest.h"

#include <PanelSubtitleDiagnostic.h>

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(PanelSubtitleDiagnosticTests)
	{
	public:
		static constexpr int Width = 64;
		static constexpr int Height = 48;

		static PanelSubtitleResult StableTopBarResult()
		{
			PanelSubtitleResult result;
			result.state = PanelSubtitleState::Stable;
			result.rasterWidth = Width;
			result.rasterHeight = Height;
			result.lineCount = 1;
			result.lines[0].glyphBounds = { 18, 3, 24, 7 };
			result.lines[0].captureBounds = { 12, 0, 40, 10 };
			result.lines[0].location = PanelSubtitleLocation::TopBar;
			result.lines[0].backingLuma = 64;
			auto mask = std::make_shared<std::vector<uint8_t>>(
				Width * Height, 0);
			for (int y = 3; y < 7; ++y)
				for (int x = 18; x < 24; ++x)
					(*mask)[y * Width + x] = 255;
			result.softGlyphMask = mask;
			return result;
		}

		static PanelSubtitleDiagnosticSurface Surface(
			std::vector<uint16_t>& luma, std::vector<uint16_t>& chroma)
		{
			return { luma.data(), chroma.data(), Width, Height,
				Width * sizeof(uint16_t), Width * sizeof(uint16_t) };
		}

		TEST_METHOD(CandidateAndOffModesNeverMutateTheFrame)
		{
			PanelSubtitleResult result = StableTopBarResult();
			std::vector<uint16_t> luma(Width * Height, 600 << 6);
			std::vector<uint16_t> chroma(Width * Height / 2, 512 << 6);
			const auto originalLuma = luma;
			const auto originalChroma = chroma;
			Assert::IsFalse(PanelSubtitleDiagnostic::Apply(result,
				Surface(luma, chroma)));
			Assert::IsTrue(luma == originalLuma);
			Assert::IsTrue(chroma == originalChroma);

			result.state = PanelSubtitleState::Candidate;
			Assert::IsFalse(PanelSubtitleDiagnostic::Apply(result,
				Surface(luma, chroma), PanelSubtitleTestMode::Move, 8, 40));
			Assert::IsTrue(luma == originalLuma);
			Assert::IsTrue(chroma == originalChroma);
		}

		TEST_METHOD(HighlightMarksEveryStableLineWithoutMovingIt)
		{
			PanelSubtitleResult result = StableTopBarResult();
			std::vector<uint16_t> luma(Width * Height, 64 << 6);
			std::vector<uint16_t> chroma(Width * Height / 2, 512 << 6);
			Assert::IsTrue(PanelSubtitleDiagnostic::Apply(result,
				Surface(luma, chroma), PanelSubtitleTestMode::Highlight, 8, 40));
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[4 * Width + 20]));
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[0 * Width + 12]));
		}

		TEST_METHOD(MoveErasesOnlyTheMaskAndCopiesGlyphsInsidePicture)
		{
			PanelSubtitleResult result = StableTopBarResult();
			std::vector<uint16_t> luma(Width * Height, 64 << 6);
			std::vector<uint16_t> chroma(Width * Height / 2, 512 << 6);
			for (int y = 3; y < 7; ++y)
				for (int x = 18; x < 24; ++x)
					luma[y * Width + x] = 900 << 6;
			const int sourceChromaIndex = (3 / 2) * Width + (18 / 2) * 2;
			chroma[sourceChromaIndex] = 600 << 6;
			chroma[sourceChromaIndex + 1] = 450 << 6;
			const uint16_t untouched = luma[15 * Width + 4];

			Assert::IsTrue(PanelSubtitleDiagnostic::Apply(result,
				Surface(luma, chroma), PanelSubtitleTestMode::Move, 8, 40));
			// Margin 12 produces destination y=20; the even shift preserves P010
			// chroma phase. Source y=3 therefore appears at y=23.
			Assert::AreEqual(static_cast<unsigned int>(64 << 6),
				static_cast<unsigned int>(luma[4 * Width + 20]));
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[24 * Width + 20]));
			Assert::AreEqual(static_cast<unsigned int>(untouched),
				static_cast<unsigned int>(luma[15 * Width + 4]));
			Assert::AreEqual(static_cast<unsigned int>(512 << 6),
				static_cast<unsigned int>(chroma[sourceChromaIndex]));
			const int destinationChromaIndex = (23 / 2) * Width + (18 / 2) * 2;
			Assert::AreEqual(static_cast<unsigned int>(600 << 6),
				static_cast<unsigned int>(chroma[destinationChromaIndex]));
			Assert::AreEqual(static_cast<unsigned int>(450 << 6),
				static_cast<unsigned int>(chroma[destinationChromaIndex + 1]));
		}

		TEST_METHOD(MovePreservesBottomMultilineReadingOrder)
		{
			PanelSubtitleResult result;
			result.state = PanelSubtitleState::Stable;
			result.rasterWidth = Width;
			result.rasterHeight = Height;
			result.lineCount = 2;
			result.lines[0].glyphBounds = { 18, 41, 24, 43 };
			result.lines[0].captureBounds = { 12, 40, 40, 44 };
			result.lines[0].location = PanelSubtitleLocation::BottomBar;
			result.lines[0].backingLuma = 64;
			result.lines[1].glyphBounds = { 18, 45, 24, 47 };
			result.lines[1].captureBounds = { 12, 44, 40, 48 };
			result.lines[1].location = PanelSubtitleLocation::BottomBar;
			result.lines[1].backingLuma = 64;
			auto mask = std::make_shared<std::vector<uint8_t>>(Width * Height, 0);
			for (int y = 41; y < 43; ++y)
				for (int x = 18; x < 24; ++x)
					(*mask)[y * Width + x] = 255;
			for (int y = 45; y < 47; ++y)
				for (int x = 18; x < 24; ++x)
					(*mask)[y * Width + x] = 255;
			result.softGlyphMask = mask;

			std::vector<uint16_t> luma(Width * Height, 64 << 6);
			std::vector<uint16_t> chroma(Width * Height / 2, 512 << 6);
			for (int y = 41; y < 43; ++y)
				for (int x = 18; x < 24; ++x)
					luma[y * Width + x] = 800 << 6;
			for (int y = 45; y < 47; ++y)
				for (int x = 18; x < 24; ++x)
					luma[y * Width + x] = 900 << 6;

			Assert::IsTrue(PanelSubtitleDiagnostic::Apply(result,
				Surface(luma, chroma), PanelSubtitleTestMode::Move, 8, 40));
			// Upper source line remains above the lower source line after packing
			// both upward from the bottom active-picture edge.
			Assert::AreEqual(static_cast<unsigned int>(800 << 6),
				static_cast<unsigned int>(luma[15 * Width + 20]));
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[25 * Width + 20]));
		}
	};
}
