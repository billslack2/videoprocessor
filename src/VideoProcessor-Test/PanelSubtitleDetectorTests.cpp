#include "pch.h"
#include "CppUnitTest.h"

#include <PanelSubtitleDetector.h>
#include <PanelSubtitleDiagnostic.h>

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(PanelSubtitleDetectorTests)
	{
	public:
		static constexpr int Width = 160;
		static constexpr int Height = 100;

		static std::vector<uint16_t> Frame(uint16_t background = 600)
		{
			return std::vector<uint16_t>(Width * Height,
				static_cast<uint16_t>(background << 6));
		}

		static void Fill(std::vector<uint16_t>& frame, int left, int top,
			int right, int bottom, uint16_t luma)
		{
			for (int y = top; y < bottom; ++y)
				for (int x = left; x < right; ++x)
					frame[y * Width + x] = static_cast<uint16_t>(luma << 6);
		}

		static void DrawPanelWithGlyphs(std::vector<uint16_t>& frame,
			uint16_t panelLuma, bool twoLines = false, bool outline = false,
			int glyphOffset = 0)
		{
			Fill(frame, 32, 62, 128, 88, panelLuma);
			if (outline)
			{
				Fill(frame, 54 + glyphOffset, 68, 106 + glyphOffset, 76, 820);
				Fill(frame, 57 + glyphOffset, 70, 103 + glyphOffset, 74, panelLuma);
			}
			else
			{
				Fill(frame, 48 + glyphOffset, 68, 72 + glyphOffset, 75, 820);
				Fill(frame, 78 + glyphOffset, 68, 108 + glyphOffset, 75, 780);
			}
			if (twoLines)
				Fill(frame, 58 + glyphOffset, 78, 102 + glyphOffset, 84, 760);
		}

		static PanelSubtitleInput Input(const std::vector<uint16_t>& frame,
			uint64_t sequence, uint64_t pipeline = 1)
		{
			PanelSubtitleInput input;
			input.p010Luma = frame.data();
			input.width = Width;
			input.height = Height;
			input.strideBytes = Width * sizeof(uint16_t);
			input.searchTop = 50;
			input.searchBottom = 96;
			input.sourceSequence = sequence;
			input.generation = { pipeline, 7, 11 };
			input.enabled = true;
			return input;
		}

		static void AssertSameRectangle(const PanelSubtitleRect& expected,
			const PanelSubtitleRect& actual)
		{
			Assert::AreEqual(expected.left, actual.left);
			Assert::AreEqual(expected.top, actual.top);
			Assert::AreEqual(expected.right, actual.right);
			Assert::AreEqual(expected.bottom, actual.bottom);
		}

		TEST_METHOD(FindsAndLocksBlackCharcoalAndGrayPanels)
		{
			const uint16_t panelColors[] = { 64, 160, 260 };
			for (size_t index = 0; index < _countof(panelColors); ++index)
			{
				PanelSubtitleDetector detector;
				std::vector<uint16_t> frame = Frame();
				DrawPanelWithGlyphs(frame, panelColors[index]);
				const PanelSubtitleResult candidate = detector.Analyze(Input(frame, 1));
				const PanelSubtitleResult stable = detector.Analyze(Input(frame, 2));

				Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
					static_cast<int>(candidate.state));
				Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
					static_cast<int>(stable.state));
				Assert::AreEqual(static_cast<int>(panelColors[index]),
					static_cast<int>(stable.panelLuma));
				AssertSameRectangle({ 32, 62, 128, 88 }, stable.panelBounds);
				Assert::IsTrue(stable.glyphBounds.left >= 48);
				Assert::IsTrue(stable.glyphBounds.right <= 108);
				Assert::IsTrue(stable.softGlyphMask && !stable.softGlyphMask->empty());
			}
		}

		TEST_METHOD(FindsOutlinedAndTwoLineGlyphGeometry)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> frame = Frame();
			DrawPanelWithGlyphs(frame, 160, true, true);
			detector.Analyze(Input(frame, 1));
			const PanelSubtitleResult stable = detector.Analyze(Input(frame, 2));

			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(stable.state));
			Assert::IsTrue(stable.glyphBounds.top <= 68);
			Assert::IsTrue(stable.glyphBounds.bottom >= 84);
			Assert::IsTrue(stable.glyphBounds.left <= 54);
			Assert::IsTrue(stable.glyphBounds.right >= 106);
		}

		TEST_METHOD(RejectsPanelOutsideConfiguredSubtitleBand)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> frame = Frame();
			Fill(frame, 32, 12, 128, 38, 160);
			Fill(frame, 50, 18, 110, 26, 820);
			const PanelSubtitleResult result = detector.Analyze(Input(frame, 1));

			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(result.state));
		}

		TEST_METHOD(RejectsLowContrastAndNonUniformDarkContent)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> lowContrast = Frame();
			DrawPanelWithGlyphs(lowContrast, 160);
			Fill(lowContrast, 48, 68, 108, 75, 180);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(detector.Analyze(Input(lowContrast, 1)).state));

			detector.Reset();
			std::vector<uint16_t> nonUniform = Frame();
			Fill(nonUniform, 32, 62, 128, 88, 40);
			for (int y = 62; y < 88; ++y)
				for (int x = 32; x < 128; ++x)
					if ((x + y) % 2 == 0)
						nonUniform[y * Width + x] = static_cast<uint16_t>(260 << 6);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(detector.Analyze(Input(nonUniform, 1)).state));
		}

		TEST_METHOD(StableCueNeverChangesItsGeometryRegardlessOfDuration)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> frame = Frame();
			DrawPanelWithGlyphs(frame, 160, true);
			detector.Analyze(Input(frame, 1));
			const PanelSubtitleResult locked = detector.Analyze(Input(frame, 2));
			// Production has no duration threshold: the cue remains locked until
			// its content, panel geometry, or generation actually changes. Exercise
			// a long 40-second cue at 60 fps as a regression sample.
			constexpr uint64_t LongCueFinalSequence = 2'402;
			for (uint64_t sequence = 3; sequence <= LongCueFinalSequence; ++sequence)
			{
				const PanelSubtitleResult current = detector.Analyze(Input(frame, sequence));
				Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
					static_cast<int>(current.state));
				AssertSameRectangle(locked.panelBounds, current.panelBounds);
				AssertSameRectangle(locked.glyphBounds, current.glyphBounds);
				Assert::AreEqual(static_cast<unsigned int>(locked.panelLuma),
					static_cast<unsigned int>(current.panelLuma));
				Assert::AreEqual(locked.fingerprint, current.fingerprint);
			}
		}

		TEST_METHOD(ChangedCueReleasesBeforeItRelocks)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> first = Frame();
			DrawPanelWithGlyphs(first, 160);
			detector.Analyze(Input(first, 1));
			const PanelSubtitleResult locked = detector.Analyze(Input(first, 2));

			std::vector<uint16_t> changed = Frame();
			DrawPanelWithGlyphs(changed, 160, false, false, 10);
			const PanelSubtitleResult candidate = detector.Analyze(Input(changed, 3));
			const PanelSubtitleResult relocked = detector.Analyze(Input(changed, 4));

			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
				static_cast<int>(candidate.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(relocked.state));
			Assert::IsTrue(relocked.glyphBounds.left > locked.glyphBounds.left);
		}

		TEST_METHOD(GenerationMismatchCannotApplyAStableCue)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> frame = Frame();
			DrawPanelWithGlyphs(frame, 160);
			detector.Analyze(Input(frame, 1, 1));
			detector.Analyze(Input(frame, 2, 1));
			const PanelSubtitleResult afterGenerationChange = detector.Analyze(Input(frame, 3, 2));

			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
				static_cast<int>(afterGenerationChange.state));
			Assert::AreEqual(static_cast<uint64_t>(2),
				afterGenerationChange.generation.pipeline);
		}

		TEST_METHOD(DiagnosticMarksPanelBoundsAndGlyphMask)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> luma = Frame();
			std::vector<uint16_t> chroma(Width * Height / 2,
				static_cast<uint16_t>(512 << 6));
			DrawPanelWithGlyphs(luma, 160);
			detector.Analyze(Input(luma, 1));
			const PanelSubtitleResult stable = detector.Analyze(Input(luma, 2));

			Assert::IsTrue(PanelSubtitleDiagnostic::Apply(stable, {
				luma.data(), chroma.data(), Width, Height,
				Width * sizeof(uint16_t), Width * sizeof(uint16_t) }));
			// Magenta panel outline at (32,62).
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[62 * Width + 32]));
			Assert::AreEqual(static_cast<unsigned int>(896 << 6),
				static_cast<unsigned int>(chroma[(62 / 2) * Width + (32 / 2) * 2]));
			// A glyph pixel receives a non-neutral diagnostic chroma marker,
			// independent of its original subtitle luma. A glyph-box edge can
			// deliberately supersede cyan with yellow in its 2x2 chroma cell.
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[70 * Width + 60]));
			Assert::AreNotEqual(static_cast<unsigned int>(512 << 6),
				static_cast<unsigned int>(chroma[(70 / 2) * Width + (60 / 2) * 2]));
			Assert::AreNotEqual(static_cast<unsigned int>(512 << 6),
				static_cast<unsigned int>(chroma[(70 / 2) * Width + (60 / 2) * 2 + 1]));
		}
	};
}
