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
	};
}
