#include "pch.h"
#include "CppUnitTest.h"

#include <DirectShowFrameDeliverer.h>

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowFrameDelivererTests)
	{
	public:
		TEST_METHOD(PairsAttachDeliverAndCompleteInOrder)
		{
			DirectShowFrameDeliverer deliverer;
			std::vector<int> order;
			IMediaSample* const sample = reinterpret_cast<IMediaSample*>(0x1);
			uint64_t clockValues[] = { 1000, 2760 };
			size_t clockIndex = 0;
			const DirectShowDeliveryTicket ticket = deliverer.Begin(sample,
				[&](IMediaSample* attached)
				{
					Assert::IsTrue(attached == sample);
					order.push_back(1);
					return 42ULL;
				},
				[&]() { return clockValues[clockIndex++]; });

			const DirectShowDeliveryResult result = deliverer.Complete(ticket,
				[&](IMediaSample* delivered)
				{
					Assert::IsTrue(delivered == sample);
					order.push_back(2);
					return S_OK;
				},
				[&](uint64_t generation, HRESULT completed)
				{
					Assert::AreEqual<uint64_t>(42, generation);
					Assert::AreEqual<HRESULT>(S_OK, completed);
					order.push_back(3);
				},
				[&]() { return clockValues[clockIndex++]; });

			Assert::AreEqual<HRESULT>(S_OK, result.result);
			Assert::AreEqual<uint32_t>(176, result.durationUs);
			Assert::AreEqual<size_t>(3, order.size());
			Assert::AreEqual(1, order[0]);
			Assert::AreEqual(2, order[1]);
			Assert::AreEqual(3, order[2]);
		}

		TEST_METHOD(CompletesFailedDeliveryWithTheOriginalGeneration)
		{
			DirectShowFrameDeliverer deliverer;
			const DirectShowDeliveryTicket ticket = deliverer.Begin(
				reinterpret_cast<IMediaSample*>(0x1),
				[](IMediaSample*) { return 9ULL; },
				[]() { return 0ULL; });
			bool completed = false;
			const DirectShowDeliveryResult result = deliverer.Complete(ticket,
				[](IMediaSample*) { return E_FAIL; },
				[&](uint64_t generation, HRESULT completedResult)
				{
					completed = true;
					Assert::AreEqual<uint64_t>(9, generation);
					Assert::AreEqual<HRESULT>(E_FAIL, completedResult);
				},
				[]() { return 0ULL; });
			Assert::IsTrue(completed);
			Assert::AreEqual<HRESULT>(E_FAIL, result.result);
		}

		TEST_METHOD(PreparesDiscontinuityAndLateBoundStopWithoutDelivery)
		{
			DirectShowFrameDeliverer deliverer;
			IMediaSample* const sample = reinterpret_cast<IMediaSample*>(0x1);
			int discontinuityCalls = 0;
			int setTimeCalls = 0;
			const DirectShowSamplePreparationResult result = deliverer.Prepare({
				sample, true, true, 1000, 100,
				[&](IMediaSample* prepared, BOOL discontinuity)
				{
					Assert::IsTrue(prepared == sample);
					Assert::IsTrue(discontinuity == TRUE);
					++discontinuityCalls;
					return S_OK;
				},
				[&](IMediaSample* prepared, REFERENCE_TIME* start, REFERENCE_TIME* stop)
				{
					Assert::IsTrue(prepared == sample);
					*start = 5000;
					*stop = 6000;
					return S_OK;
				},
				[&](IMediaSample* prepared, REFERENCE_TIME* start, REFERENCE_TIME* stop)
				{
					Assert::IsTrue(prepared == sample);
					Assert::AreEqual<REFERENCE_TIME>(5000, *start);
					Assert::AreEqual<REFERENCE_TIME>(6200, *stop);
					++setTimeCalls;
					return S_OK;
				},
				[](REFERENCE_TIME current, REFERENCE_TIME theoretical, REFERENCE_TIME tolerance)
				{
					Assert::AreEqual<REFERENCE_TIME>(5000, current);
					Assert::AreEqual<REFERENCE_TIME>(6000, theoretical);
					Assert::AreEqual<REFERENCE_TIME>(100, tolerance);
					return static_cast<REFERENCE_TIME>(6200);
				} });

			Assert::AreEqual(1, discontinuityCalls);
			Assert::AreEqual(1, setTimeCalls);
			Assert::IsTrue(result.lateBoundStopApplied);
			Assert::AreEqual<REFERENCE_TIME>(6200, result.matchedNextStart);
		}

		TEST_METHOD(ClearsDiscontinuityWhenAllocatorSampleIsReused)
		{
			DirectShowFrameDeliverer deliverer;
			IMediaSample* const reusedSample =
				reinterpret_cast<IMediaSample*>(0x1);
			BOOL allocatorFlag = FALSE;
			int normalizationCalls = 0;
			auto normalize = [&](IMediaSample* prepared, BOOL discontinuity)
				{
					Assert::IsTrue(prepared == reusedSample);
					allocatorFlag = discontinuity;
					++normalizationCalls;
					return S_OK;
				};

			const DirectShowSamplePreparationResult marked = deliverer.Prepare({
				reusedSample, true, false, 0, 0, normalize, {}, {}, {} });
			Assert::AreEqual<HRESULT>(S_OK, marked.discontinuityResult);
			Assert::IsTrue(allocatorFlag == TRUE);

			const DirectShowSamplePreparationResult continuous = deliverer.Prepare({
				reusedSample, false, false, 0, 0, normalize, {}, {}, {} });
			Assert::AreEqual<HRESULT>(S_OK, continuous.discontinuityResult);
			Assert::IsTrue(allocatorFlag == FALSE);
			Assert::AreEqual(2, normalizationCalls);
		}
	};
}
