#include "pch.h"
#include "CppUnitTest.h"

#include <DisplayRefreshRateWindow.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DisplayRefreshRateWindowTests)
	{
	public:
		TEST_METHOD(RecentSamplesReplaceAnOldRateInsteadOfDilutingIt)
		{
			DisplayRefreshRateWindow window(1000, 3000);
			window.Add(1000, 1000, 1); // 1 Hz initial observation.
			window.Add(1500, 500, 1);
			window.Add(2000, 500, 1);
			window.Add(2500, 500, 1);
			window.Add(3000, 500, 1);
			window.Add(3500, 500, 1);
			window.Add(4000, 500, 1);
			window.Add(4500, 500, 1);

			const DisplayRefreshRateWindowSnapshot snapshot = window.Snapshot();

			Assert::AreEqual(2.0, snapshot.refreshRateHz, 0.000001);
			Assert::AreEqual(2.0, snapshot.rawWaitRateHz, 0.000001);
			Assert::AreEqual<uint64_t>(7, snapshot.rawWaitIntervals);
		}

		TEST_METHOD(OldRawGapOutlierAgesOutOfCurrentDiagnostics)
		{
			DisplayRefreshRateWindow window(1000, 2000);
			window.Add(1000, 1000, 1);
			window.Add(1500, 500, 1);
			window.Add(2000, 500, 1);
			window.Add(2500, 500, 1);
			window.Add(3000, 500, 1);
			window.Add(3500, 500, 1);

			const DisplayRefreshRateWindowSnapshot snapshot = window.Snapshot();

			Assert::AreEqual<int64_t>(500, snapshot.minimumWaitIntervalQpc);
			Assert::AreEqual<int64_t>(500, snapshot.maximumWaitIntervalQpc);
			Assert::AreEqual<uint64_t>(5, snapshot.rawWaitIntervals);
		}
	};
}
