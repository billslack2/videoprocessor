#include "pch.h"
#include "CppUnitTest.h"
#include <CaptureCallbackBoundary.h>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(CaptureResyncHardeningTests)
	{
	public:
		TEST_METHOD(PreservesCallbackResults)
		{
			int reports = 0;
			for (HRESULT expected : { S_OK, S_FALSE, E_INVALIDARG })
				Assert::AreEqual<HRESULT>(expected, CaptureCallbackBoundary(
					[&]() { return expected; }, [&](const char*) { ++reports; }));
			Assert::AreEqual(0, reports);
		}

		TEST_METHOD(ContainsFrameFailureAndAllowsNextCallback)
		{
			std::string diagnostic;
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw std::runtime_error("frame bytes unavailable"); },
				[&](const char* error) { diagnostic = error; }));
			Assert::IsTrue(diagnostic == "frame bytes unavailable");
			Assert::AreEqual<HRESULT>(S_OK, CaptureCallbackBoundary(
				[]() { return S_OK; }, [](const char*) {}));
		}

		TEST_METHOD(ContainsUnknownExceptionAndFailingReporter)
		{
			bool reported = false;
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw 42; },
				[&](const char*) { reported = true; throw std::bad_alloc(); }));
			Assert::IsTrue(reported);
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw std::bad_alloc(); },
				[](const char*) { throw std::runtime_error("logger failed"); }));
		}

		TEST_METHOD(ExhaustedAllocatorReturnsAndRecoversAfterSampleRelease)
		{
			HRESULT result = S_OK;
			CComPtr<IMemAllocator> allocator = new CMemAllocator(NAME("resync test"), nullptr, &result);
			Assert::AreEqual<HRESULT>(S_OK, result);
			ALLOCATOR_PROPERTIES requested{ 1, 256, 1, 0 }, actual{};
			Assert::AreEqual<HRESULT>(S_OK, allocator->SetProperties(&requested, &actual));
			Assert::AreEqual<HRESULT>(S_OK, allocator->Commit());
			CComPtr<IMediaSample> held, next;
			Assert::AreEqual<HRESULT>(S_OK, allocator->GetBuffer(&held, nullptr, nullptr, AM_GBF_NOWAIT));
			Assert::AreEqual<HRESULT>(VFW_E_TIMEOUT, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
			Assert::IsTrue(next == nullptr);
			held.Release();
			Assert::AreEqual<HRESULT>(S_OK, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
			next.Release();
			Assert::AreEqual<HRESULT>(S_OK, allocator->Decommit());
			Assert::AreEqual<HRESULT>(VFW_E_NOT_COMMITTED, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
		}
	};
}
