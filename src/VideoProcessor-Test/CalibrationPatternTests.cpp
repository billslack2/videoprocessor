#include "pch.h"

#include <CalibrationPatterns.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(CalibrationPatternTests)
	{
		static std::uint8_t Channel(const CalibrationPatternFrame& frame,
			unsigned int x, unsigned int y, unsigned int channel)
		{
			return frame.bgra[(static_cast<size_t>(y) * frame.width + x) * 4 + channel];
		}

	public:
		TEST_METHOD(FullAndStudioEndpointsAreExact)
		{
			Assert::AreEqual(static_cast<unsigned char>(0),
				CalibrationPatterns::EncodeGray(0.0, CalibrationSignalRange::FULL));
			Assert::AreEqual(static_cast<unsigned char>(255),
				CalibrationPatterns::EncodeGray(1.0, CalibrationSignalRange::FULL));
			Assert::AreEqual(static_cast<unsigned char>(16),
				CalibrationPatterns::EncodeGray(0.0, CalibrationSignalRange::STUDIO));
			Assert::AreEqual(static_cast<unsigned char>(235),
				CalibrationPatterns::EncodeGray(1.0, CalibrationSignalRange::STUDIO));
		}

		TEST_METHOD(FieldsUseOpaqueBgraAtRequestedRange)
		{
			const auto full = CalibrationPatterns::Generate(
				CalibrationPattern::GRAY_FIELD, CalibrationSignalRange::FULL, 64, 64);
			const auto studio = CalibrationPatterns::Generate(
				CalibrationPattern::GRAY_FIELD, CalibrationSignalRange::STUDIO, 64, 64);
			Assert::AreEqual(static_cast<unsigned char>(128), Channel(full, 20, 30, 0));
			Assert::AreEqual(static_cast<unsigned char>(128), Channel(full, 20, 30, 1));
			Assert::AreEqual(static_cast<unsigned char>(128), Channel(full, 20, 30, 2));
			Assert::AreEqual(static_cast<unsigned char>(255), Channel(full, 20, 30, 3));
			Assert::AreEqual(static_cast<unsigned char>(126), Channel(studio, 20, 30, 0));
		}

		TEST_METHOD(TenPercentWindowHasReferenceWhiteCenterAndBlackCorner)
		{
			const auto frame = CalibrationPatterns::Generate(
				CalibrationPattern::TEN_PERCENT_WHITE_WINDOW,
				CalibrationSignalRange::STUDIO, 100, 100);
			Assert::AreEqual(static_cast<unsigned char>(16), Channel(frame, 0, 0, 0));
			Assert::AreEqual(static_cast<unsigned char>(235), Channel(frame, 50, 50, 0));
		}

		TEST_METHOD(GrayscaleStepsStartAndEndAtRangeEndpoints)
		{
			const auto frame = CalibrationPatterns::Generate(
				CalibrationPattern::GRAYSCALE_STEPS,
				CalibrationSignalRange::STUDIO, 110, 64);
			Assert::AreEqual(static_cast<unsigned char>(16), Channel(frame, 0, 32, 0));
			Assert::AreEqual(static_cast<unsigned char>(235), Channel(frame, 109, 32, 0));
		}

		TEST_METHOD(CinemaGeometryUsesRequestedAspectAndColor)
		{
			const auto academy = CalibrationPatterns::GenerateCinemaGeometry(
				1.33, 20, 40, 60, 160, 90);
			Assert::AreEqual(static_cast<unsigned char>(0), Channel(academy, 0, 0, 0));
			Assert::AreEqual(static_cast<unsigned char>(60), Channel(academy, 20, 0, 0));
			Assert::AreEqual(static_cast<unsigned char>(40), Channel(academy, 20, 0, 1));
			Assert::AreEqual(static_cast<unsigned char>(20), Channel(academy, 20, 0, 2));

			const auto scope = CalibrationPatterns::GenerateCinemaGeometry(
				2.39, 30, 50, 70, 160, 90);
			Assert::AreEqual(static_cast<unsigned char>(0), Channel(scope, 0, 0, 0));
			Assert::AreEqual(static_cast<unsigned char>(70), Channel(scope, 0, 11, 0));
		}
	};
}
