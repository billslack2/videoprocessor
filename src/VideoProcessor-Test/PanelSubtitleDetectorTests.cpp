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
				Fill(frame, 58 + glyphOffset, 78, 102 + glyphOffset, 85, 760);
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

		static PanelSubtitleInput ActiveInput(const std::vector<uint16_t>& frame,
			uint64_t sequence, int top = 20, int bottom = 80)
		{
			PanelSubtitleInput input = Input(frame, sequence);
			input.searchTop = 0;
			input.searchBottom = Height;
			input.activePictureTop = top;
			input.activePictureBottom = bottom;
			input.trustedActivePictureGeneration = input.generation.activePicture;
			input.activePictureStable = true;
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
				// The highlighted capture area is deterministic glyph-derived
				// geometry, not a claim about the opaque panel's original edges.
				AssertSameRectangle({ 16, 46, 140, 97 }, stable.panelBounds);
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
			Assert::AreEqual(static_cast<size_t>(2), stable.lineCount);
			Assert::IsTrue(stable.lines[0].glyphBounds.top <= 68);
			Assert::IsTrue(stable.lines[0].glyphBounds.bottom >= 75);
			Assert::IsTrue(stable.lines[1].glyphBounds.top <= 78);
			Assert::IsTrue(stable.lines[1].glyphBounds.bottom >= 85);
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

		TEST_METHOD(ChangedCueAtIdenticalBoundsCannotReuseTheOldLock)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> first = Frame();
			DrawPanelWithGlyphs(first, 160);
			detector.Analyze(Input(first, 1));
			const PanelSubtitleResult locked = detector.Analyze(Input(first, 2));

			std::vector<uint16_t> changed = Frame();
			DrawPanelWithGlyphs(changed, 160);
			Fill(changed, 48, 68, 72, 75, 160);
			Fill(changed, 78, 68, 108, 75, 160);
			for (int y = 68; y < 75; ++y)
				for (int x = 48; x < 108; ++x)
					if (((x - 48) % 4 < 2 || x >= 106) &&
						(x < 72 || x >= 78))
						changed[y * Width + x] = static_cast<uint16_t>(820 << 6);

			const PanelSubtitleResult candidate = detector.Analyze(Input(changed, 3));
			const PanelSubtitleResult relocked = detector.Analyze(Input(changed, 4));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
				static_cast<int>(candidate.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(relocked.state));
			AssertSameRectangle(locked.glyphBounds, relocked.glyphBounds);
			Assert::AreNotEqual(locked.fingerprint, relocked.fingerprint);
		}

		TEST_METHOD(IdleAcquisitionUsesCadenceButConfirmsOnTheNextFrame)
		{
			PanelSubtitleDetector detector;
			const std::vector<uint16_t> empty = Frame();
			for (uint64_t sequence = 1; sequence <= 60; ++sequence)
				Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
					static_cast<int>(detector.Analyze(Input(empty, sequence)).state));
			Assert::IsTrue(detector.AcquisitionScanCount() <= 21);

			std::vector<uint16_t> cue = Frame();
			DrawPanelWithGlyphs(cue, 160);
			const PanelSubtitleResult candidate = detector.Analyze(Input(cue, 61));
			const PanelSubtitleResult stable = detector.Analyze(Input(cue, 62));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
				static_cast<int>(candidate.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(stable.state));
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
				Width * sizeof(uint16_t), Width * sizeof(uint16_t) },
				PanelSubtitleTestMode::Highlight));
			// Magenta deterministic capture-outline at (16,46).
			Assert::AreEqual(static_cast<unsigned int>(900 << 6),
				static_cast<unsigned int>(luma[46 * Width + 16]));
			Assert::AreEqual(static_cast<unsigned int>(896 << 6),
				static_cast<unsigned int>(chroma[(46 / 2) * Width + (16 / 2) * 2]));
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

		TEST_METHOD(FindsBoundaryAndBarGlyphsWithoutAcceptingPictureOnlyContent)
		{
			auto detectTwice = [](const std::vector<uint16_t>& frame) {
				PanelSubtitleDetector detector;
				detector.Analyze(ActiveInput(frame, 1));
				return detector.Analyze(ActiveInput(frame, 2));
			};

			std::vector<uint16_t> topBoundary = Frame();
			Fill(topBoundary, 35, 12, 125, 29, 80);
			Fill(topBoundary, 50, 16, 110, 24, 820);
			const PanelSubtitleResult top = detectTwice(topBoundary);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable), static_cast<int>(top.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleLocation::TopBoundary),
				static_cast<int>(top.lines[0].location));

			std::vector<uint16_t> topBar = Frame();
			Fill(topBar, 35, 1, 125, 17, 80);
			Fill(topBar, 50, 5, 110, 13, 820);
			const PanelSubtitleResult bar = detectTwice(topBar);
			Assert::AreEqual(static_cast<int>(PanelSubtitleLocation::TopBar),
				static_cast<int>(bar.lines[0].location));

			PanelSubtitleDetector detector;
			std::vector<uint16_t> pictureOnly = Frame();
			Fill(pictureOnly, 35, 35, 125, 53, 80);
			Fill(pictureOnly, 50, 40, 110, 48, 820);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(detector.Analyze(ActiveInput(pictureOnly, 1)).state));

			// The same bright boundary shape, but strongly colored in the optional
			// P010 UV plane, must not become a subtitle seed.
			std::vector<uint16_t> chroma(Width * Height / 2,
				static_cast<uint16_t>(512 << 6));
			for (int y = 16 / 2; y < 24 / 2; ++y)
				for (int x = 50 / 2; x < 110 / 2; ++x)
					chroma[y * Width + x * 2] = static_cast<uint16_t>(800 << 6);
			PanelSubtitleInput colored = ActiveInput(topBoundary, 3);
			colored.p010Chroma = chroma.data();
			colored.chromaStrideBytes = Width * sizeof(uint16_t);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(detector.Analyze(colored).state));
		}

		TEST_METHOD(AcceptsOneSidedEncodedBars)
		{
			std::vector<uint16_t> bottomOnly = Frame();
			Fill(bottomOnly, 35, 82, 125, 99, 80);
			Fill(bottomOnly, 50, 86, 110, 94, 820);
			PanelSubtitleDetector bottomDetector;
			bottomDetector.Analyze(ActiveInput(bottomOnly, 1, 0, 80));
			const auto bottom =
				bottomDetector.Analyze(ActiveInput(bottomOnly, 2, 0, 80));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(bottom.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleLocation::BottomBar),
				static_cast<int>(bottom.lines[0].location));

			std::vector<uint16_t> topOnly = Frame();
			Fill(topOnly, 35, 1, 125, 18, 80);
			Fill(topOnly, 50, 5, 110, 13, 820);
			PanelSubtitleDetector topDetector;
			topDetector.Analyze(ActiveInput(topOnly, 1, 20, Height));
			const auto top =
				topDetector.Analyze(ActiveInput(topOnly, 2, 20, Height));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(top.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleLocation::TopBar),
				static_cast<int>(top.lines[0].location));
		}

		TEST_METHOD(DetectsAndHighlightsUhdSubtitleWhollyInsideScopeBar)
		{
			constexpr int UhdWidth = 3840;
			constexpr int UhdHeight = 2160;
			std::vector<uint16_t> luma(UhdWidth * UhdHeight,
				static_cast<uint16_t>(600 << 6));
			std::vector<uint16_t> chroma(UhdWidth * UhdHeight / 2,
				static_cast<uint16_t>(512 << 6));
			auto fill = [&](int left, int top, int right, int bottom,
				uint16_t code)
			{
				for (int y = top; y < bottom; ++y)
					for (int x = left; x < right; ++x)
						luma[static_cast<size_t>(y) * UhdWidth + x] =
							static_cast<uint16_t>(code << 6);
			};
			fill(0, 0, UhdWidth, 276, 64);
			fill(0, 1884, UhdWidth, UhdHeight, 64);
			fill(1040, 1988, 1320, 2038, 900);
			fill(1370, 1988, 1800, 2038, 900);

			PanelSubtitleInput input;
			input.p010Luma = luma.data();
			input.p010Chroma = chroma.data();
			input.width = UhdWidth;
			input.height = UhdHeight;
			input.strideBytes = UhdWidth * sizeof(uint16_t);
			input.chromaStrideBytes = UhdWidth * sizeof(uint16_t);
			input.activePictureTop = 276;
			input.activePictureBottom = 1884;
			input.generation = { 1, 2, 3 };
			input.trustedActivePictureGeneration = 2;
			input.activePictureStable = true;
			input.enabled = true;
			input.sourceSequence = 1;

			PanelSubtitleDetector detector;
			const auto candidate = detector.Analyze(input);
			input.sourceSequence = 2;
			const auto stable = detector.Analyze(input);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Candidate),
				static_cast<int>(candidate.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable),
				static_cast<int>(stable.state));
			Assert::AreEqual(static_cast<int>(PanelSubtitleLocation::BottomBar),
				static_cast<int>(stable.lines[0].location));
			Assert::IsTrue(PanelSubtitleDiagnostic::Apply(stable, {
				luma.data(), chroma.data(), UhdWidth, UhdHeight,
				UhdWidth * sizeof(uint16_t), UhdWidth * sizeof(uint16_t) },
				PanelSubtitleTestMode::Highlight, 276, 1884));
		}

		TEST_METHOD(RejectsSeparatedMenuHighlightsAndKeepsFrozenCaptureBox)
		{
			PanelSubtitleDetector detector;
			std::vector<uint16_t> menu = Frame();
			Fill(menu, 10, 1, 150, 17, 80);
			Fill(menu, 18, 5, 48, 13, 820);
			Fill(menu, 108, 5, 138, 13, 820);
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Unavailable),
				static_cast<int>(detector.Analyze(ActiveInput(menu, 1)).state));
			detector.Reset();

			std::vector<uint16_t> first = Frame();
			Fill(first, 35, 12, 125, 29, 80);
			Fill(first, 50, 16, 110, 24, 820);
			detector.Analyze(ActiveInput(first, 2));
			const PanelSubtitleResult stable = detector.Analyze(ActiveInput(first, 3));
			const PanelSubtitleRect frozen = stable.lines[0].captureBounds;
			std::vector<uint16_t> shifted = Frame();
			Fill(shifted, 36, 12, 126, 29, 80);
			Fill(shifted, 51, 16, 111, 24, 820);
			const PanelSubtitleResult retained = detector.Analyze(ActiveInput(shifted, 4));
			Assert::AreEqual(static_cast<int>(PanelSubtitleState::Stable), static_cast<int>(retained.state));
			AssertSameRectangle(frozen, retained.lines[0].captureBounds);
		}
	};
}
