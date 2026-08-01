#include "pch.h"
#include "CppUnitTest.h"

#include <LiveOutputTrace.h>

#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(LiveOutputTraceTests)
	{
	public:
		TEST_METHOD(RetainsTheNewestBoundedTraceInSequenceOrder)
		{
			LiveOutputTrace trace;
			for (size_t index = 0; index < LiveOutputTrace::CAPACITY + 3; ++index)
			{
				LiveOutputTraceRecord record;
				record.frameNumber = index;
				trace.Record(record);
			}

			const auto snapshot = trace.Snapshot();
			Assert::AreEqual<size_t>(LiveOutputTrace::CAPACITY, snapshot.size());
			Assert::AreEqual<uint64_t>(4, snapshot.front().sequence);
			Assert::AreEqual<uint64_t>(3, snapshot.front().frameNumber);
			Assert::AreEqual<uint64_t>(
				LiveOutputTrace::CAPACITY + 3,
				snapshot.back().sequence);
		}

		TEST_METHOD(ComparisonAllowsOnlyDeclaredPresentationTolerance)
		{
			LiveOutputTraceRecord expected;
			expected.sequence = 1;
			expected.kind = LiveOutputTraceKind::DeliveryCompleted;
			expected.frameNumber = 42;
			expected.pipelineEpoch = 7;
			expected.presentationStart = 1000;
			expected.presentationStop = 2000;

			LiveOutputTraceRecord actual = expected;
			actual.presentationStart += 4;
			actual.presentationStop -= 4;

			Assert::IsFalse(LiveOutputTrace::Compare({ expected }, { actual }, 3).equivalent);
			Assert::IsTrue(LiveOutputTrace::Compare({ expected }, { actual }, 4).equivalent);
			actual.captureArrivalTick = 123456;
			Assert::IsTrue(LiveOutputTrace::Compare({ expected }, { actual }, 4).equivalent);

			actual.convertedQueueDepth = 2;
			Assert::IsFalse(LiveOutputTrace::Compare({ expected }, { actual }, 4).equivalent);
		}

		TEST_METHOD(CsvContainsOnlyVpObservableFields)
		{
			LiveOutputTraceRecord record;
			record.sequence = 1;
			record.kind = LiveOutputTraceKind::DeliveryCompleted;
			record.frameNumber = 100;
			record.pipelineEpoch = 3;
			record.captureArrivalTick = 456;
			record.rawQueueDepth = 2;
			record.convertedQueueDepth = 4;
			record.totalQueueDepth = 6;
			record.queueCapacity = 32;

			std::ostringstream stream;
			LiveOutputTrace::WriteCsv(stream, { record });
			const std::string csv = stream.str();

			Assert::IsTrue(csv.find("raw_queue,converted_queue,total_queue,queue_capacity") != std::string::npos);
			Assert::IsTrue(csv.find("capture_arrival_tick") != std::string::npos);
			Assert::IsTrue(csv.find("vp_internal_us") != std::string::npos);
			Assert::IsTrue(csv.find("latency_display_ready") != std::string::npos);
			Assert::IsTrue(csv.find("madvr") == std::string::npos);
			Assert::IsTrue(csv.find("1,3") != std::string::npos);
		}
	};
}
