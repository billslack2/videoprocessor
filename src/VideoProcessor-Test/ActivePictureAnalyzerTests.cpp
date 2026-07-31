#include "pch.h"
#include "CppUnitTest.h"

#include <ActivePictureAnalyzer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(ActivePictureAnalyzerTests)
	{
	public:
		TEST_METHOD(UnavailableP010FrameProducesOneDeterministicObservation)
		{
			ActivePictureAnalyzer analyzer;
			const ActivePictureAnalyzerResult first = analyzer.Analyze({
				{}, 1, 60.0 });
			Assert::IsFalse(first.analyzed);

			P010PlaneView missingFrame;
			missingFrame.width = 1920;
			missingFrame.height = 1080;
			const ActivePictureAnalyzerResult unavailable = analyzer.Analyze({
				missingFrame, 6, 60.0 });
			Assert::IsTrue(unavailable.analyzed);
			Assert::IsFalse(unavailable.evidence.available);
			Assert::IsTrue(unavailable.evidence.reason ==
				"P010 sample pointer is unavailable");
			Assert::IsFalse(unavailable.decision.stable);
		}

		TEST_METHOD(ResetRestartsTheSparseAnalysisSchedule)
		{
			ActivePictureAnalyzer analyzer;
			P010PlaneView missingFrame;
			missingFrame.width = 1920;
			missingFrame.height = 1080;
			Assert::IsTrue(analyzer.Analyze({ missingFrame, 1, 60.0 }).analyzed);
			Assert::IsFalse(analyzer.Analyze({ missingFrame, 2, 60.0 }).analyzed);
			analyzer.Reset();
			Assert::IsTrue(analyzer.Analyze({ missingFrame, 1, 60.0 }).analyzed);
		}
	};
}
