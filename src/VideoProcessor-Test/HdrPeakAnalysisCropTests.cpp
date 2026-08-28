#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/HdrPeakAnalysisCrop.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(HdrPeakAnalysisCropTests)
	{
	public:
		TEST_METHOD(RestrictsFullRasterToCentralTrustedPictureBand)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				0, 140, 1920, 940, 1920, 1080, 7, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 7, trusted,
					pl_rect2df{ 0.0f, 0.0f, 1920.0f, 1080.0f });

			Assert::IsTrue(decision.AppliesRestriction());
			Assert::AreEqual(240.0f / 1080.0f,
				decision.normalizedCrop.y0, 0.000001f);
			Assert::AreEqual(840.0f / 1080.0f,
				decision.normalizedCrop.y1, 0.000001f);
			Assert::AreEqual(480.0 / 1080.0,
				decision.excludedFraction, 0.000001);
		}

		TEST_METHOD(NormalizesTrustedPictureWithinSubtitleExpandedPresentation)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				0, 140, 1920, 940, 1920, 1080, 11, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 11, trusted,
					pl_rect2df{ 0.0f, 100.0f, 1920.0f, 1000.0f });

			Assert::IsTrue(decision.AppliesRestriction());
			Assert::AreEqual(140.0f / 900.0f,
				decision.normalizedCrop.y0, 0.000001f);
			Assert::AreEqual(740.0f / 900.0f,
				decision.normalizedCrop.y1, 0.000001f);
			Assert::AreEqual(300.0 / 900.0,
				decision.excludedFraction, 0.000001);
			Assert::AreEqual(240.0f, decision.trustedIntersection.y0, 0.000001f);
			Assert::AreEqual(840.0f, decision.trustedIntersection.y1, 0.000001f);
		}

		TEST_METHOD(ExcludesPillarboxBarsBeforeUsingFullActivePictureWidth)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				200, 100, 1720, 980, 1920, 1080, 12, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 12, trusted,
					pl_rect2df{ 0.0f, 0.0f, 1920.0f, 1080.0f });

			Assert::IsTrue(decision.AppliesRestriction());
			Assert::AreEqual(200.0f / 1920.0f,
				decision.normalizedCrop.x0, 0.000001f);
			Assert::AreEqual(1720.0f / 1920.0f,
				decision.normalizedCrop.x1, 0.000001f);
			Assert::AreEqual(210.0f / 1080.0f,
				decision.normalizedCrop.y0, 0.000001f);
			Assert::AreEqual(870.0f / 1080.0f,
				decision.normalizedCrop.y1, 0.000001f);
		}

		TEST_METHOD(IntersectsCentralBandWithFinalCihZoomOrNlsSourceCrop)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				200, 100, 1720, 980, 1920, 1080, 13, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 13, trusted,
					pl_rect2df{ 400.0f, 100.0f, 1500.0f, 900.0f });

			Assert::IsTrue(decision.AppliesRestriction());
			Assert::AreEqual(0.0f, decision.normalizedCrop.x0, 0.000001f);
			Assert::AreEqual(1.0f, decision.normalizedCrop.x1, 0.000001f);
			Assert::AreEqual(110.0f / 800.0f,
				decision.normalizedCrop.y0, 0.000001f);
			Assert::AreEqual(770.0f / 800.0f,
				decision.normalizedCrop.y1, 0.000001f);
			Assert::AreEqual(400.0f, decision.trustedIntersection.x0, 0.000001f);
			Assert::AreEqual(1500.0f, decision.trustedIntersection.x1, 0.000001f);
		}

		TEST_METHOD(PresentationAlreadyInsideCentralBandNeedsNoShaderCrop)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				0, 140, 1920, 940, 1920, 1080, 4, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 4, trusted,
					pl_rect2df{ 100.0f, 300.0f, 1820.0f, 800.0f });

			Assert::IsFalse(decision.AppliesRestriction());
			Assert::AreEqual(
				static_cast<int>(HdrPeakAnalysisCrop::Outcome::FULL_PRESENTATION),
				static_cast<int>(decision.outcome));
			Assert::AreEqual(0.0, decision.excludedFraction, 0.000001);
		}

		TEST_METHOD(ConfiguredHeightChangesCentralBand)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				0, 100, 1920, 900, 1920, 1080, 14, true
			};
			const HdrPeakAnalysisCrop::Decision decision =
				HdrPeakAnalysisCrop::Resolve(true, true, 14, trusted,
					pl_rect2df{ 0.0f, 0.0f, 1920.0f, 1080.0f }, 50.0);

			Assert::IsTrue(decision.AppliesRestriction());
			Assert::AreEqual(300.0f, decision.trustedIntersection.y0, 0.000001f);
			Assert::AreEqual(700.0f, decision.trustedIntersection.y1, 0.000001f);
		}

		TEST_METHOD(FailsOpenForStaleGeometryOrInactivePeakDetection)
		{
			HdrPeakAnalysisCrop::TrustedPicture trusted = {
				0, 140, 1920, 940, 1920, 1080, 8, true
			};
			const pl_rect2df presentation = {
				0.0f, 0.0f, 1920.0f, 1080.0f
			};

			Assert::IsFalse(HdrPeakAnalysisCrop::Resolve(
				true, true, 9, trusted, presentation).AppliesRestriction());
			Assert::IsFalse(HdrPeakAnalysisCrop::Resolve(
				true, false, 8, trusted, presentation).AppliesRestriction());
			Assert::IsFalse(HdrPeakAnalysisCrop::Resolve(
				false, true, 8, trusted, presentation).AppliesRestriction());
		}
	};
}
