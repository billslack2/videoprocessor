#include "pch.h"
#include "CppUnitTest.h"

#include <DirectShowSegmentTransition.h>

#include <stdexcept>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowSegmentTransitionTests)
	{
	public:
		TEST_METHOD(OrdersFlushStateAndSegmentOnSuccess)
		{
			DirectShowSegmentTransition transition;
			std::vector<int> order;
			const DirectShowSegmentTransitionResult result = transition.Execute(
				[&]() { order.push_back(1); return S_OK; },
				[&]() { order.push_back(2); },
				[&]() { order.push_back(3); return S_OK; },
				[&]() { order.push_back(4); return S_OK; });

			Assert::IsTrue(result.began);
			Assert::AreEqual<HRESULT>(S_OK, result.beginFlushResult);
			Assert::AreEqual<HRESULT>(S_OK, result.endFlushResult);
			Assert::AreEqual<HRESULT>(S_OK, result.newSegmentResult);
			Assert::AreEqual<size_t>(4, order.size());
			for (size_t index = 0; index < order.size(); ++index)
				Assert::AreEqual(static_cast<int>(index + 1), order[index]);
		}

		TEST_METHOD(BeginFlushFailureSkipsStateAndSegment)
		{
			DirectShowSegmentTransition transition;
			int callbacksAfterBegin = 0;
			const DirectShowSegmentTransitionResult result = transition.Execute(
				[]() { return E_FAIL; },
				[&]() { ++callbacksAfterBegin; },
				[&]() { ++callbacksAfterBegin; return S_OK; },
				[&]() { ++callbacksAfterBegin; return S_OK; });

			Assert::IsFalse(result.began);
			Assert::AreEqual<HRESULT>(E_FAIL, result.beginFlushResult);
			Assert::AreEqual(0, callbacksAfterBegin);
		}

		TEST_METHOD(EndFlushFailureSkipsNewSegment)
		{
			DirectShowSegmentTransition transition;
			int newSegmentCalls = 0;
			const DirectShowSegmentTransitionResult result = transition.Execute(
				[]() { return S_OK; },
				[]() {},
				[]() { return E_FAIL; },
				[&]() { ++newSegmentCalls; return S_OK; });

			Assert::IsTrue(result.began);
			Assert::AreEqual<HRESULT>(E_FAIL, result.endFlushResult);
			Assert::AreEqual(0, newSegmentCalls);
		}

		TEST_METHOD(ExceptionStillEndsTheFlushAndSkipsNewSegment)
		{
			DirectShowSegmentTransition transition;
			std::vector<int> order;
			Assert::ExpectException<std::runtime_error>([&]()
				{
					transition.Execute(
						[&]() { order.push_back(1); return S_OK; },
						[&]() { order.push_back(2); throw std::runtime_error("test"); },
						[&]() { order.push_back(3); return S_OK; },
						[&]() { order.push_back(4); return S_OK; });
				});
			Assert::AreEqual<size_t>(3, order.size());
			Assert::AreEqual(1, order[0]);
			Assert::AreEqual(2, order[1]);
			Assert::AreEqual(3, order[2]);
		}
	};
}
