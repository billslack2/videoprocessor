#include "pch.h"
#include "CppUnitTest.h"

#include <RationalLiveOutputSequencer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
RationalLiveOutputTimestampInput Input(uint64_t epoch = 9)
{
	RationalLiveOutputTimestampInput input;
	input.epoch = epoch;
	input.ppmCorrection = -17;
	input.pipelineOffset = 0;
	input.presentationLead = 1800000;
	return input;
}
}

namespace Tests
{
	TEST_CLASS(RationalLiveOutputSequencerTests)
	{
	public:
		TEST_METHOD(CommitsAContinuous5994OutputTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input());
			Assert::IsTrue(first.valid);
			Assert::AreEqual<uint64_t>(0, first.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(1800000, first.start);
			Assert::IsTrue(first.discontinuity);
			Assert::IsTrue(sequencer.Commit(first));

			const RationalLiveOutputTimestampDecision second =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(1, second.outputSequence);
			Assert::IsTrue(second.start > first.start);
			Assert::IsTrue(second.stop > second.start);
			Assert::IsTrue(second.start >= first.stop);
			Assert::IsFalse(second.discontinuity);
		}

		TEST_METHOD(SkippedLivePicturesDoNotSkipPresentationTime)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			RationalLiveOutputTimestampDecision previous = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(previous));
			for (uint32_t i = 0; i < 4; ++i)
			{
				const RationalLiveOutputTimestampDecision next =
					sequencer.Preview(Input());
				Assert::IsTrue(next.start >= previous.stop);
				Assert::IsTrue(sequencer.Commit(next));
				previous = next;
			}

			// Ten captured pictures may be discarded before the next delivery. The
			// delivery-owned sequence has no capture number to turn that into a
			// timestamp hole.
			const RationalLiveOutputTimestampDecision afterDiscard =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(5, afterDiscard.outputSequence);
			Assert::IsTrue(afterDiscard.start >= previous.stop);
			Assert::IsFalse(afterDiscard.discontinuity);
		}

		TEST_METHOD(FailedDeliveryDoesNotAdvanceTheSequence)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision attempted =
				sequencer.Preview(Input());
			const RationalLiveOutputTimestampDecision retry =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(attempted.outputSequence, retry.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::IsTrue(sequencer.Commit(retry));
		}

		TEST_METHOD(NewEpochStartsANewDirectShowSegmentSequence)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input(9));
			Assert::IsTrue(sequencer.Commit(first));
			const RationalLiveOutputTimestampDecision next =
				sequencer.Preview(Input(10));
			Assert::AreEqual<uint64_t>(0, next.outputSequence);
			Assert::IsTrue(next.discontinuity);
		}
	};
}
