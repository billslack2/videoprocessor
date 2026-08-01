#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/DirectShowPresentationLeadPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowPresentationLeadPolicyTests)
	{
	public:
		TEST_METHOD(OmittedSettingPreservesLegacyModeBehavior)
		{
			Assert::AreEqual<int64_t>(1800000,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					false, 0, 417083, true));
			Assert::AreEqual<int64_t>(0,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					false, 0, 417083, false));
		}

		TEST_METHOD(ExplicitFramesScaleWithTheActiveSourceRate)
		{
			Assert::AreEqual<int64_t>(417083,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					true, 1, 417083, true));
			Assert::AreEqual<int64_t>(333666,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					true, 2, 166833, true));
		}

		TEST_METHOD(ExplicitZeroIsLiteralAndExcessIsBounded)
		{
			Assert::AreEqual<int64_t>(0,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					true, 0, 166833, true));
			Assert::AreEqual<int64_t>(16 * 166833LL,
				DirectShowPresentationLeadPolicy::Resolve100ns(
					true, 99, 166833, true));
		}
	};
}
