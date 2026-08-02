#include "pch.h"
#include "CppUnitTest.h"

#include <ConsecutiveFrameDurationHistory.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(ConsecutiveFrameDurationHistoryTests)
	{
	public:
		TEST_METHOD(AveragesOnlyConsecutive5994Frames)
		{
			ConsecutiveFrameDurationHistory history;
			Assert::IsFalse(history.Observe(100, 1000000, 166833));
			Assert::IsTrue(history.Observe(101, 1166833, 166833));
			Assert::IsTrue(history.Observe(102, 1333666, 166833));
			Assert::AreEqual<size_t>(2, history.Count());
			Assert::AreEqual<int64_t>(166833, history.AverageOr(1));
		}

		TEST_METHOD(ForwardGapDoesNotContaminateTheAverage)
		{
			ConsecutiveFrameDurationHistory history;
			history.Observe(100, 1000000, 166833);
			history.Observe(101, 1166833, 166833);
			Assert::IsFalse(history.Observe(111, 2835163, 166833));
			Assert::AreEqual<size_t>(1, history.Count());
			Assert::AreEqual<int64_t>(166833, history.AverageOr(1));
			Assert::IsTrue(history.Observe(112, 3001996, 166833));
			Assert::AreEqual<int64_t>(166833, history.AverageOr(1));
		}

		TEST_METHOD(CounterRollbackRebaselinesWithoutAddingADuration)
		{
			ConsecutiveFrameDurationHistory history;
			history.Observe(100, 1000000, 417083);
			history.Observe(101, 1417083, 417083);
			Assert::IsFalse(history.Observe(1, 0, 417083));
			Assert::IsTrue(history.Observe(2, 417083, 417083));
			Assert::AreEqual<int64_t>(417083, history.AverageOr(1));
		}

		TEST_METHOD(InvalidDurationIsRejectedAndRebaselined)
		{
			ConsecutiveFrameDurationHistory history;
			history.Observe(1, 1000000, 166833);
			Assert::IsFalse(history.Observe(2, 1000001, 166833));
			Assert::AreEqual<size_t>(0, history.Count());
			Assert::IsTrue(history.Observe(3, 1166834, 166833));
			Assert::AreEqual<int64_t>(166833, history.AverageOr(1));
		}

		TEST_METHOD(RejectsDoubleDurationGlitchesAt5994And23976)
		{
			ConsecutiveFrameDurationHistory history;
			history.Observe(1, 1000000, 166833);
			Assert::IsFalse(history.Observe(2, 1333666, 166833));
			Assert::AreEqual<size_t>(0, history.Count());
			Assert::IsTrue(history.Observe(3, 1500499, 166833));

			history.Reset();
			history.Observe(1, 1000000, 417083);
			Assert::IsFalse(history.Observe(2, 1834166, 417083));
			Assert::AreEqual<size_t>(0, history.Count());
			Assert::IsTrue(history.Observe(3, 2251249, 417083));
		}
	};
}
