#include "pch.h"
#include "CppUnitTest.h"

#include <PanelSubtitleActivePictureAuthority.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(PanelSubtitleActivePictureAuthorityTests)
	{
	public:
		static PanelSubtitleActivePictureAuthorityObservation Crop(
			int top = 132, int bottom = 948, uint64_t pipeline = 1,
			uint64_t reset = 1, uint64_t mode = 1)
		{
			return { 0, top, 1920, bottom, 1920, 1080, true, false,
				pipeline, reset, mode };
		}

		static PanelSubtitleActivePictureAuthorityObservation FullRaster(
			uint64_t pipeline = 1, uint64_t reset = 1, uint64_t mode = 1)
		{
			return { 0, 0, 1920, 1080, 1920, 1080, true, false,
				pipeline, reset, mode };
		}

		static PanelSubtitleActivePictureAuthorityObservation TrustedFullRaster()
		{
			auto value = FullRaster();
			value.fullRasterTrusted = true;
			return value;
		}

		TEST_METHOD(RetainsCroppedAuthorityThroughAmbiguousFullRasterFrames)
		{
			PanelSubtitleActivePictureAuthority authority;
			const auto established = authority.Observe(Crop());
			const auto fullRaster = authority.Observe(FullRaster());
			const auto unstable = authority.Observe({ 0, 0, 0, 0, 1920, 1080,
				false, false, 1, 1, 1 });

			Assert::IsTrue(established.available);
			Assert::IsTrue(fullRaster.available);
			Assert::IsTrue(unstable.available);
			Assert::AreEqual(132, fullRaster.top);
			Assert::AreEqual(948, unstable.bottom);
			Assert::AreEqual(established.generation, fullRaster.generation);
			Assert::AreEqual(established.generation, unstable.generation);
		}

		TEST_METHOD(SustainedTrustedFullRasterReleasesTheHeldCrop)
		{
			PanelSubtitleActivePictureAuthority authority;
			const auto established = authority.Observe(Crop());
			for (uint32_t count = 1;
				count < PanelSubtitleActivePictureAuthority::FULL_RASTER_CONFIRMATIONS;
				++count)
			{
				const auto held = authority.Observe(TrustedFullRaster());
				Assert::IsTrue(held.available);
			}
			const auto released = authority.Observe(TrustedFullRaster());

			Assert::IsFalse(released.available);
			Assert::IsTrue(released.generation != established.generation);
		}

		TEST_METHOD(RequiresSustainedStrongContradictionBeforeSwitchingGeometry)
		{
			PanelSubtitleActivePictureAuthority authority;
			const auto established = authority.Observe(Crop());
			const auto first = authority.Observe(Crop(216, 864));
			const auto second = authority.Observe(Crop(216, 864));
			const auto switched = authority.Observe(Crop(216, 864));

			Assert::AreEqual(132, first.top);
			Assert::AreEqual(132, second.top);
			Assert::AreEqual(2u, second.pendingContradictions);
			Assert::AreEqual(216, switched.top);
			Assert::IsTrue(switched.generation != established.generation);
		}

		TEST_METHOD(PipelineRasterAndResetChangesInvalidateImmediately)
		{
			PanelSubtitleActivePictureAuthority authority;
			const auto established = authority.Observe(Crop());
			const auto pipelineReset = authority.Observe(FullRaster(2));
			const auto afterPipeline = authority.Observe(Crop(132, 948, 2));
			const auto detectorReset = authority.Observe(FullRaster(2, 2));
			const auto afterRaster = authority.Observe({ 0, 132, 1280, 588, 1280,
				720, true, false, 2, 2, 1 });

			Assert::IsFalse(pipelineReset.available);
			Assert::IsTrue(afterPipeline.available);
			Assert::IsTrue(afterPipeline.generation != established.generation);
			Assert::IsFalse(detectorReset.available);
			Assert::IsTrue(afterRaster.available);
			Assert::AreEqual(1280, afterRaster.rasterWidth);
		}
	};
}
