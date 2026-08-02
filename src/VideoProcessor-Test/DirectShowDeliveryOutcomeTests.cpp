#include "pch.h"
#include "CppUnitTest.h"

#include <DirectShowDeliveryOutcome.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowDeliveryOutcomeTests)
	{
	public:
		TEST_METHOD(ClassifiesTheExistingDeliveryLatencyBands)
		{
			DirectShowDeliveryOutcomeClassifier classifier;
			Assert::AreEqual(
				static_cast<int>(DirectShowDeliveryLatencyClass::Instant),
				static_cast<int>(classifier.Classify({ S_OK, 1999, 5000 }).latencyClass));
			Assert::AreEqual(
				static_cast<int>(DirectShowDeliveryLatencyClass::Normal),
				static_cast<int>(classifier.Classify({ S_OK, 2000, 5000 }).latencyClass));
			Assert::AreEqual(
				static_cast<int>(DirectShowDeliveryLatencyClass::Normal),
				static_cast<int>(classifier.Classify({ S_OK, 5000, 5000 }).latencyClass));
			Assert::AreEqual(
				static_cast<int>(DirectShowDeliveryLatencyClass::Slow),
				static_cast<int>(classifier.Classify({ S_OK, 5001, 5000 }).latencyClass));
		}

		TEST_METHOD(PreservesFailureAndSuccessCounterSemantics)
		{
			DirectShowDeliveryOutcomeClassifier classifier;
			const DirectShowDeliveryOutcome failed =
				classifier.Classify({ E_FAIL, 0, 5000 });
			Assert::IsTrue(failed.deliveryFailed);
			Assert::IsFalse(failed.deliverySucceeded);
			Assert::IsTrue(failed.countDroppedFrame);
			Assert::IsTrue(failed.incrementRecentFailures);
			Assert::IsFalse(failed.clearRecentFailures);

			const DirectShowDeliveryOutcome successful =
				classifier.Classify({ S_OK, 0, 5000 });
			Assert::IsFalse(successful.deliveryFailed);
			Assert::IsTrue(successful.deliverySucceeded);
			Assert::IsFalse(successful.countDroppedFrame);
			Assert::IsFalse(successful.incrementRecentFailures);
			Assert::IsTrue(successful.clearRecentFailures);

			const DirectShowDeliveryOutcome nonSuccess =
				classifier.Classify({ S_FALSE, 0, 5000 });
			Assert::IsFalse(nonSuccess.deliveryFailed);
			Assert::IsFalse(nonSuccess.deliverySucceeded);
			Assert::IsTrue(nonSuccess.deliveryRejected);
			Assert::IsFalse(nonSuccess.incrementRecentFailures);
			Assert::IsFalse(nonSuccess.clearRecentFailures);
		}
	};
}
