#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/video_renderers/DirectShowGraphExecutor.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowGraphRetirementTests)
	{
	public:
		TEST_METHOD(IncompleteRetirementKeepsOwnerApartmentForRetry)
		{
			DirectShowGraphExecutor executor;
			const DWORD ownerThreadId = executor.OwnerThreadId();
			DWORD firstCleanupThreadId = 0;
			DWORD secondCleanupThreadId = 0;

			Assert::IsFalse(executor.QuiesceAndInvokeCleanup([&]()
				{
					firstCleanupThreadId = GetCurrentThreadId();
					return false;
				}));
			Assert::AreEqual(ownerThreadId, firstCleanupThreadId);
			Assert::IsFalse(executor.Post([]() {}));

			Assert::IsTrue(executor.QuiesceAndInvokeCleanup([&]()
				{
					secondCleanupThreadId = GetCurrentThreadId();
					return true;
				}));
			Assert::AreEqual(ownerThreadId, secondCleanupThreadId);
			executor.Shutdown();
		}
	};
}
