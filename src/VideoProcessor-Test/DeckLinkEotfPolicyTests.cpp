#include "pch.h"
#include "CppUnitTest.h"

#include <blackmagic_decklink/DeckLinkEotfPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DeckLinkEotfPolicyTests)
	{
	public:
		TEST_METHOD(AcceptsDefinedValuesAndChangesStateOnlyWhenNeeded)
		{
			BlackMagicDeckLinkEotfPolicy::State state;
			for (int64_t value = 0; value <= 3; ++value)
			{
				const auto accepted =
					BlackMagicDeckLinkEotfPolicy::Observe(state, value);
				Assert::AreEqual(value, accepted.next.acceptedValue);
				Assert::IsTrue(accepted.videoStateChanged);
				Assert::IsFalse(accepted.logIgnoredValue);

				const auto repeated =
					BlackMagicDeckLinkEotfPolicy::Observe(accepted.next, value);
				Assert::AreEqual(value, repeated.next.acceptedValue);
				Assert::IsFalse(repeated.videoStateChanged);
				Assert::IsFalse(repeated.logIgnoredValue);
				state = repeated.next;
			}
		}

		TEST_METHOD(ReservedValuesRetainTheLastAcceptedState)
		{
			auto state = BlackMagicDeckLinkEotfPolicy::Observe(
				BlackMagicDeckLinkEotfPolicy::State{}, 2).next;

			for (int64_t value = 4; value <= 7; ++value)
			{
				const auto ignored =
					BlackMagicDeckLinkEotfPolicy::Observe(state, value);
				Assert::AreEqual<int64_t>(2, ignored.next.acceptedValue);
				Assert::IsFalse(ignored.videoStateChanged);
				Assert::IsTrue(ignored.logIgnoredValue);

				const auto repeated =
					BlackMagicDeckLinkEotfPolicy::Observe(
						ignored.next, value);
				Assert::IsFalse(repeated.logIgnoredValue);
				state = repeated.next;
			}

			const auto recovered =
				BlackMagicDeckLinkEotfPolicy::Observe(state, 3);
			Assert::AreEqual<int64_t>(3, recovered.next.acceptedValue);
			Assert::IsTrue(recovered.videoStateChanged);
			Assert::AreEqual(
				BlackMagicDeckLinkEotfPolicy::INVALID_VALUE,
				recovered.next.lastIgnoredValue);
			Assert::IsTrue(BlackMagicDeckLinkEotfPolicy::Observe(
				recovered.next, 4).logIgnoredValue);
		}

		TEST_METHOD(ReservedValueAtStartupLeavesEotfInvalid)
		{
			const auto ignored =
				BlackMagicDeckLinkEotfPolicy::Observe(
					BlackMagicDeckLinkEotfPolicy::State{}, 4);
			Assert::AreEqual(
				BlackMagicDeckLinkEotfPolicy::INVALID_VALUE,
				ignored.next.acceptedValue);
			Assert::IsFalse(ignored.videoStateChanged);
			Assert::IsTrue(ignored.logIgnoredValue);
		}
	};
}
