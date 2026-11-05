#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/OptionalAnalysisWorker.h>
#include <chrono>
#include <future>
#include <stdexcept>
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

        struct JobBarrier
        {
            std::promise<void> entered,release;
            std::future<void> entry=entered.get_future();
            std::shared_future<void> gate=release.get_future().share();
            bool opened=false;
            ~JobBarrier(){Open();}
            void Wait(){entered.set_value();gate.wait();}
            bool Entered(){return entry.wait_for(std::chrono::seconds(5))==std::future_status::ready;}
            void Open(){if(!opened){opened=true;release.set_value();}}
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
        TEST_METHOD(DependentAttemptObservesPublicationForBothRegistrationOrders)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            for(bool lateRegistration:{false,true}) {
                std::shared_ptr<const int> published;
                std::atomic<unsigned> offers{0};std::atomic<bool> observed{false};
                HeldWorker<2> held;
                auto signal=std::make_shared<AnalysisCompletionSignal>();held.active->completion=signal;
                const auto revision=signal->Snapshot();
                const auto offer=[&] {
                    const auto value=std::atomic_load(&published);
                    observed.store(value && *value==42 && held.active->attemptFinished.load() && !held.active->pending.load());
                    ++offers;return true;
                };
                Assert::IsTrue(held.worker.Submit(held.active,1,1,1,[&] {
                    held.entered.set_value();held.gate.wait();return 42;
                },[&](std::shared_ptr<const int> value){std::atomic_store(&published,std::move(value));}));
                Assert::IsTrue(held.Entered());
                if(!lateRegistration)Worker::AfterAttempt(held.active,offer);
                Assert::AreEqual(0u,offers.load());
                held.Open();Assert::IsTrue(Await([&]{return signal->Snapshot()!=revision;}));
                if(lateRegistration)Worker::AfterAttempt(held.active,offer);
                Assert::IsTrue(Await([&]{return offers.load()==1;}));
                held.worker.Stop();
                Assert::IsTrue(observed.load(),L"Completed native publication must precede the dependent offer.");
                for(unsigned poll=0;poll<20;++poll)Worker::RetryAfterAttempt(held.active);
                Assert::AreEqual(1u,offers.load());
            }
        }
        TEST_METHOD(DependentAttemptNegativeAndExceptionNeverCreateUsableEvidence)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            for(bool throws:{false,true}) {
                std::atomic<unsigned> offers{0};std::atomic<bool> usable{false},terminal{false};
                HeldWorker<2> held;
                Worker::AfterAttempt(held.active,[&] {
                    const auto result=Worker::Ready(held.active);
                    usable.store(result && *result>0);
                    terminal.store(held.active->attemptFinished.load()&&!held.active->pending.load());
                    ++offers;return true;
                });
                Assert::IsTrue(held.worker.Submit(held.active,1,1,1,[&]()->int {
                    held.entered.set_value();held.gate.wait();
                    if(throws)throw std::runtime_error("optional native attempt failed");
                    return 0; // A completed negative, explicitly not a proof.
                }));
                Assert::IsTrue(held.Entered());held.Open();
                Assert::IsTrue(Await([&]{return offers.load()==1;}));held.worker.Stop();
                Assert::IsTrue(terminal.load());Assert::IsFalse(usable.load());
                Assert::AreEqual(uint64_t(throws?1:0),held.worker.Snapshot().failed);
                Assert::AreEqual(!throws,Worker::Ready(held.active)!=nullptr);
            }
        }
        TEST_METHOD(DependentAttemptQueuedCancellationNeverInvokesCancelledOffer)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            std::atomic<unsigned> calls{0};HeldWorker<2> held;
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
            auto stale=std::make_shared<Worker::Slot>(),current=std::make_shared<Worker::Slot>();
            auto owner=std::make_shared<int>(17);std::weak_ptr<int> weak=owner;
            Worker::AfterAttempt(stale,[owner,&calls]{++calls;return true;});owner.reset();
            Assert::IsTrue(held.Submit(stale,1,10,1,10));
            Assert::IsTrue(held.Submit(current,2,20,20,20));
            Assert::IsFalse(stale->pending.load());Assert::IsFalse(stale->attemptFinished.load());
            Worker::RetryAfterAttempt(stale);Assert::AreEqual(0u,calls.load());
            Assert::IsFalse(weak.expired());stale.reset();Assert::IsTrue(weak.expired());
            held.Open();Assert::IsTrue(held.Ready(current));held.worker.Stop();
            Assert::AreEqual(0u,calls.load());
            Assert::AreEqual(uint64_t(1),held.worker.Snapshot().cancelled);
        }
        TEST_METHOD(DependentAttemptSaturatedQueueRetriesOnceWithoutDuplicateAdmission)
        {
            using Worker=OptionalAnalysisWorker<int,1>;
            std::atomic<unsigned> offers{0},admissions{0};
            HeldWorker<1> raw;JobBarrier secondRaw;HeldWorker<1> native;
            Assert::IsTrue(raw.Start());Assert::IsTrue(raw.Entered());
            auto occupying=std::make_shared<Worker::Slot>(),target=std::make_shared<Worker::Slot>();
            Assert::IsTrue(raw.worker.Submit(occupying,1,20,1,[&]{secondRaw.Wait();return 20;}));
            auto signal=std::make_shared<AnalysisCompletionSignal>();native.active->completion=signal;
            const auto revision=signal->Snapshot();
            Worker::AfterAttempt(native.active,[&] {
                ++offers;
                const bool admitted=raw.worker.Submit(target,1,30,1,[]{return 30;});
                if(admitted)++admissions;
                return admitted;
            });
            Assert::IsTrue(native.Start());Assert::IsTrue(native.Entered());native.Open();
            Assert::IsTrue(Await([&]{return signal->Snapshot()!=revision;}));
            Assert::AreEqual(1u,offers.load());Assert::AreEqual(0u,admissions.load());
            Assert::IsFalse(target->pending.load());Assert::IsTrue(Worker::Ready(target)==nullptr);
            raw.Open();Assert::IsTrue(secondRaw.Entered()); // Raw mutex is free; queue is empty.
            Worker::RetryAfterAttempt(native.active);
            Assert::AreEqual(2u,offers.load());Assert::AreEqual(1u,admissions.load());
            for(unsigned poll=0;poll<20;++poll)Worker::RetryAfterAttempt(native.active);
            Assert::AreEqual(2u,offers.load());
            secondRaw.Open();Assert::IsTrue(raw.Ready(target));raw.worker.Stop();native.worker.Stop();
            Assert::AreEqual(30,*Worker::Ready(target));
            Assert::AreEqual(uint64_t(3),raw.worker.Snapshot().submitted);
        }
        TEST_METHOD(DependentAttemptReadmissionSuppressesOldTerminalRetryWhilePending)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            std::atomic<unsigned> offers{0},replacement{0};HeldWorker<2> held;JobBarrier second;
            auto signal=std::make_shared<AnalysisCompletionSignal>();held.active->completion=signal;
            const auto revision=signal->Snapshot();
            Worker::AfterAttempt(held.active,[&]{return ++offers>1;});
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());held.Open();
            Assert::IsTrue(Await([&]{return signal->Snapshot()!=revision;}));
            Assert::AreEqual(1u,offers.load());
            Assert::IsTrue(held.worker.Submit(held.active,1,1,1,[&]{second.Wait();return 2;}));
            Assert::IsTrue(second.Entered());Assert::IsFalse(held.active->attemptFinished.load());
            Worker::AfterAttempt(held.active,[&]{++replacement;return true;});
            for(unsigned poll=0;poll<20;++poll)Worker::RetryAfterAttempt(held.active);
            Assert::AreEqual(1u,offers.load());Assert::AreEqual(0u,replacement.load());
            second.Open();Assert::IsTrue(Await([&]{return offers.load()==2;}));held.worker.Stop();
            Assert::AreEqual(2,*Worker::Ready(held.active));
            Worker::RetryAfterAttempt(held.active);Assert::AreEqual(2u,offers.load());
        }
        TEST_METHOD(DependentAttemptRetiredSourceContextReleasesOwnerWithoutScheduling)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            for(unsigned fault=0;fault<3;++fault) {
                std::atomic<uint64_t> currentSource{41},currentContext{9};
                std::atomic<unsigned> callbacks{0},scheduled{0};
                auto lease=std::make_shared<int>(1);std::weak_ptr<int> weakLease=lease;
                auto owner=std::make_shared<int>(42);std::weak_ptr<int> weakOwner=owner;
                HeldWorker<2> held;
                Worker::AfterAttempt(held.active,[owned=owner,weakLease,&currentSource,&currentContext,&callbacks,&scheduled] {
                    ++callbacks;
                    // Caller-owned immutable identity/lifetime guard; a terminal
                    // attempt itself supplies no source/context authority.
                    if(weakLease.expired() || currentSource.load()!=41 || currentContext.load()!=9)return true;
                    if(*owned==42)++scheduled;
                    return true;
                });owner.reset();
                Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());
                Assert::IsFalse(weakOwner.expired());
                if(fault==0)lease.reset();
                if(fault==1)currentSource.store(42);
                if(fault==2)currentContext.store(10);
                held.Open();Assert::IsTrue(Await([&]{return callbacks.load()==1;}));held.worker.Stop();
                Assert::AreEqual(0u,scheduled.load());Assert::IsTrue(weakOwner.expired());
                Worker::RetryAfterAttempt(held.active);Assert::AreEqual(1u,callbacks.load());
            }
        }

        TEST_METHOD(DependentAttemptConcurrentRetriesAdmitOnlyOnce)
        {
            using Worker=OptionalAnalysisWorker<int,2>;
            std::atomic<unsigned> offers{0};std::atomic<bool> accept{false};
            HeldWorker<2> held;
            auto signal=std::make_shared<AnalysisCompletionSignal>();held.active->completion=signal;
            const auto revision=signal->Snapshot();
            Worker::AfterAttempt(held.active,[&]{++offers;return accept.load();});
            Assert::IsTrue(held.Start());Assert::IsTrue(held.Entered());held.Open();
            Assert::IsTrue(Await([&]{return signal->Snapshot()!=revision;}));
            Assert::AreEqual(1u,offers.load());accept.store(true);
            std::promise<void> release;const auto gate=release.get_future().share();
            std::vector<std::thread> retries;
            for(unsigned thread=0;thread<8;++thread)
                retries.emplace_back([&]{gate.wait();for(unsigned poll=0;poll<100;++poll)Worker::RetryAfterAttempt(held.active);});
            release.set_value();for(auto& thread:retries)thread.join();held.worker.Stop();
            Assert::AreEqual(2u,offers.load(),L"Only one retry may consume a successful dependent offer.");
        }

        TEST_METHOD(CompletionSignalKeepsEarlyNotificationAndAbsoluteExpiry)
        {
            AnalysisCompletionSignal signal;
            const auto before=signal.Snapshot();
            signal.Notify();
            // Already-published work is observed even with an expired deadline.
            const auto expired=std::chrono::steady_clock::now();
            Assert::IsTrue(signal.WaitUntil(before,expired));
            const auto current=signal.Snapshot();
            Assert::IsFalse(signal.WaitUntil(current,expired));
            Assert::IsFalse(signal.WaitUntil(current,expired));
        }
        TEST_METHOD(WorkerCompletionWakesOnlyAfterImmutablePublication)
        {
            HeldWorker<2> held;
            auto signal=std::make_shared<AnalysisCompletionSignal>();
            held.active->completion=signal;
            const auto revision=signal->Snapshot();
            Assert::IsTrue(held.Start());
            Assert::IsTrue(held.Entered());
            auto waiter=std::async(std::launch::async,[signal,revision] {
                return signal->WaitUntil(revision,std::chrono::steady_clock::now()+std::chrono::seconds(5));
            });
            held.Open();
            Assert::IsTrue(waiter.get());
            const auto ready=held.worker.Ready(held.active);
            Assert::IsNotNull(ready.get());
            Assert::AreEqual(1,*ready);
            Assert::IsFalse(held.active->pending.load());
        }
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
