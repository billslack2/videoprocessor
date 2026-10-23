#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/OptionalAnalysisWorker.h>
#include <chrono>
#include <future>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        template<typename Predicate>
        bool Await(Predicate predicate)
        {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (!predicate() && std::chrono::steady_clock::now()<deadline)
                std::this_thread::yield();
            return predicate();
        }

        template<size_t Capacity>
        struct HeldWorker
        {
            using Worker=OptionalAnalysisWorker<int,Capacity>;
            using Slot=typename Worker::Slot;
            Worker worker;
            std::promise<void> entered,release;
            std::future<void> entry=entered.get_future();
            std::shared_future<void> gate=release.get_future().share();
            std::shared_ptr<Slot> active=std::make_shared<Slot>();
            std::vector<int> order;
            bool opened=false;

            ~HeldWorker() { Open();worker.Stop(); }
            bool Start()
            {
                return worker.Submit(active,1,1,1,[this] {
                    entered.set_value();gate.wait();order.push_back(1);return 1;
                });
            }
            bool Entered() { return entry.wait_for(std::chrono::seconds(5))==std::future_status::ready; }
            void Open() { if (!opened) { opened=true;release.set_value(); } }
            bool Submit(const std::shared_ptr<Slot>& slot,uint64_t generation,
                uint64_t sequence,uint64_t minimum,int value)
            {
                return worker.Submit(slot,generation,sequence,minimum,[this,value] {
                    order.push_back(value);return value;
                });
            }
            bool Ready(const std::shared_ptr<Slot>& slot)
            {
                return Await([&] { return Worker::Ready(slot)!=nullptr; });
            }
        };

        void ExpectOrder(const std::vector<int>& expected,const std::vector<int>& actual)
        {
            Assert::AreEqual(expected.size(),actual.size());
            for (size_t i=0;i<expected.size();++i) Assert::AreEqual(expected[i],actual[i]);
        }
    }

    TEST_CLASS(OptionalAnalysisWorkerTests)
    {
    public:
        TEST_METHOD(ReversedArrivalsRunNearestQueuedSourceFirstWhileActiveJobContinues)
        {
            HeldWorker<3> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto farSlot=std::make_shared<HeldWorker<3>::Slot>();
            auto nearSlot=std::make_shared<HeldWorker<3>::Slot>();
            auto middle=std::make_shared<HeldWorker<3>::Slot>();
            Assert::IsTrue(held.Submit(farSlot,1,30,1,30));
            Assert::IsTrue(held.Submit(nearSlot,1,10,1,10));
            Assert::IsTrue(held.Submit(middle,1,20,1,20));
            // These admissions completed before release of the deliberately
            // blocked active job; none waited for measurement completion.
            Assert::IsTrue(held.active->pending.load());
            Assert::AreEqual(uint64_t(3),held.worker.Snapshot().queued);
            held.Open();Assert::IsTrue(held.Ready(farSlot));held.worker.Stop();
            ExpectOrder({1,10,20,30},held.order);
        }

        TEST_METHOD(EqualSequenceJobsKeepStableArrivalOrder)
        {
            HeldWorker<3> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto first=std::make_shared<HeldWorker<3>::Slot>();
            auto second=std::make_shared<HeldWorker<3>::Slot>();
            auto earlier=std::make_shared<HeldWorker<3>::Slot>();
            Assert::IsTrue(held.Submit(first,1,10,1,101));
            Assert::IsTrue(held.Submit(second,1,10,1,102));
            Assert::IsTrue(held.Submit(earlier,1,8,1,8));
            held.Open();Assert::IsTrue(held.Ready(second));held.worker.Stop();
            ExpectOrder({1,8,101,102},held.order);
        }

        TEST_METHOD(NearRequestOnFullQueueEvictsOnlyFurthestPendingSource)
        {
            HeldWorker<2> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto middle=std::make_shared<HeldWorker<2>::Slot>();
            auto farSlot=std::make_shared<HeldWorker<2>::Slot>();
            auto nearSlot=std::make_shared<HeldWorker<2>::Slot>();
            Assert::IsTrue(held.Submit(middle,1,20,1,20));
            Assert::IsTrue(held.Submit(farSlot,1,30,1,30));
            Assert::IsTrue(held.Submit(nearSlot,1,10,1,10));
            Assert::IsFalse(farSlot->pending.load());
            Assert::IsTrue(held.active->pending.load());
            Assert::IsTrue(middle->pending.load());Assert::IsTrue(nearSlot->pending.load());
            Assert::IsTrue((HeldWorker<2>::Worker::Ready(farSlot)==nullptr));
            const auto admitted=held.worker.Snapshot();
            Assert::AreEqual(uint64_t(1),admitted.cancelled);
            Assert::AreEqual(uint64_t(0),admitted.full);
            Assert::AreEqual(uint64_t(2),admitted.queued);
            Assert::AreEqual(uint64_t(2),admitted.queueHighWater);
            held.Open();Assert::IsTrue(held.Ready(middle));held.worker.Stop();
            ExpectOrder({1,10,20},held.order);
            Assert::AreEqual(uint64_t(3),held.worker.Snapshot().completed);
        }

        TEST_METHOD(FarOrEqualRequestOnFullQueueCannotEvictNearerWork)
        {
            HeldWorker<2> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto nearSlot=std::make_shared<HeldWorker<2>::Slot>();
            auto farSlot=std::make_shared<HeldWorker<2>::Slot>();
            auto rejected=std::make_shared<HeldWorker<2>::Slot>();
            Assert::IsTrue(held.Submit(nearSlot,1,10,1,10));
            Assert::IsTrue(held.Submit(farSlot,1,20,1,20));
            Assert::IsFalse(held.Submit(rejected,1,30,1,30));
            Assert::IsFalse(rejected->pending.load());
            Assert::IsFalse(held.Submit(rejected,1,20,1,21));
            Assert::IsFalse(rejected->pending.load());
            const auto metrics=held.worker.Snapshot();
            Assert::AreEqual(uint64_t(2),metrics.full);
            Assert::AreEqual(uint64_t(0),metrics.cancelled);
            held.Open();Assert::IsTrue(held.Ready(farSlot));held.worker.Stop();
            ExpectOrder({1,10,20},held.order);
        }

        TEST_METHOD(EvictedSlotCanBeAdmittedAgainWithoutStuckPendingFlag)
        {
            HeldWorker<1> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto farSlot=std::make_shared<HeldWorker<1>::Slot>();
            auto nearSlot=std::make_shared<HeldWorker<1>::Slot>();
            Assert::IsTrue(held.Submit(farSlot,1,30,1,30));
            Assert::IsTrue(held.Submit(nearSlot,1,20,1,20));
            Assert::IsFalse(farSlot->pending.load());
            Assert::IsTrue(held.Submit(farSlot,1,10,1,10));
            Assert::IsFalse(nearSlot->pending.load());
            Assert::AreEqual(uint64_t(2),held.worker.Snapshot().cancelled);
            held.Open();Assert::IsTrue(held.Ready(farSlot));held.worker.Stop();
            ExpectOrder({1,10},held.order);
        }

        TEST_METHOD(NewGenerationCancelsQueuedOldGenerationButNotActiveMeasurement)
        {
            HeldWorker<3> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto oldA=std::make_shared<HeldWorker<3>::Slot>();
            auto oldB=std::make_shared<HeldWorker<3>::Slot>();
            auto newFar=std::make_shared<HeldWorker<3>::Slot>();
            auto newNear=std::make_shared<HeldWorker<3>::Slot>();
            Assert::IsTrue(held.Submit(oldA,1,10,1,10));
            Assert::IsTrue(held.Submit(oldB,1,20,1,20));
            Assert::IsTrue(held.Submit(newFar,2,100,100,100));
            Assert::IsFalse(oldA->pending.load());Assert::IsFalse(oldB->pending.load());
            Assert::IsTrue(held.active->pending.load());
            Assert::IsTrue(held.Submit(newNear,2,90,90,90));
            Assert::AreEqual(uint64_t(2),held.worker.Snapshot().cancelled);
            held.Open();Assert::IsTrue(held.Ready(newFar));held.worker.Stop();
            ExpectOrder({1,90,100},held.order);
            Assert::IsTrue((HeldWorker<3>::Worker::Ready(oldA)==nullptr));
            Assert::IsTrue((HeldWorker<3>::Worker::Ready(oldB)==nullptr));
        }

        TEST_METHOD(AdvancingMinimumRetiresObsoleteSourcesBeforePriorityAdmission)
        {
            HeldWorker<3> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto a=std::make_shared<HeldWorker<3>::Slot>();
            auto b=std::make_shared<HeldWorker<3>::Slot>();
            auto c=std::make_shared<HeldWorker<3>::Slot>();
            auto nearSlot=std::make_shared<HeldWorker<3>::Slot>();
            Assert::IsTrue(held.Submit(a,1,10,1,10));
            Assert::IsTrue(held.Submit(b,1,20,1,20));
            Assert::IsTrue(held.Submit(c,1,30,1,30));
            Assert::IsTrue(held.Submit(nearSlot,1,25,25,25));
            Assert::IsFalse(a->pending.load());Assert::IsFalse(b->pending.load());
            Assert::AreEqual(uint64_t(2),held.worker.Snapshot().cancelled);
            held.Open();Assert::IsTrue(held.Ready(c));held.worker.Stop();
            ExpectOrder({1,25,30},held.order);
        }

        TEST_METHOD(ShutdownClearsPendingQueueAndWaitsOnlyForReleasedActiveJob)
        {
            HeldWorker<2> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto queued=std::make_shared<HeldWorker<2>::Slot>();
            Assert::IsTrue(held.Submit(queued,1,10,1,10));
            std::promise<void> stopped;
            auto completion=stopped.get_future();
            std::thread stopper([&] { held.worker.Stop();stopped.set_value(); });
            // Collect observations before asserting so failure cannot strand a
            // blocked fake job or the lifecycle join thread.
            const bool cleared=Await([&] { return !queued->pending.load(); });
            const bool stoppedBeforeRelease=completion.wait_for(std::chrono::seconds(0))==std::future_status::ready;
            held.Open();stopper.join();
            Assert::IsTrue(cleared);Assert::IsFalse(stoppedBeforeRelease);
            Assert::IsFalse(queued->pending.load());
            Assert::IsTrue((HeldWorker<2>::Worker::Ready(queued)==nullptr));
            ExpectOrder({1},held.order);
            const auto metrics=held.worker.Snapshot();
            Assert::AreEqual(uint64_t(1),metrics.cancelled);
            Assert::AreEqual(uint64_t(0),metrics.queued);
            Assert::AreEqual(uint64_t(1),metrics.completed);
        }
    };
}
