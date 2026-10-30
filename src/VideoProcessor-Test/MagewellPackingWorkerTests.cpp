#include "pch.h"
#include "CppUnitTest.h"
#include <magewell/MagewellPackingWorker.h>
#include <future>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(MagewellPackingWorkerTests) {
public:
    TEST_METHOD(WorkerIsBoundedAndWaitMakesWritesVisible)
    {
        MagewellPackingWorker worker;
        std::promise<void> entered, release; auto gate=release.get_future().share();
        int output=0;
        worker.Submit([&]{entered.set_value();gate.wait();output=73;return true;});
        entered.get_future().wait();
        bool rejected=false;try{worker.Submit([]{return true;});}catch(const std::logic_error&){rejected=true;}
        release.set_value();const auto result=worker.Wait();
        Assert::IsTrue(rejected);Assert::IsTrue(result.success);Assert::AreEqual(73,output);
        worker.Submit([]{return false;});Assert::IsFalse(worker.Wait().success);
    }
    TEST_METHOD(WorkerPropagatesExceptionAndCanAcceptAnotherFrame)
    {
        MagewellPackingWorker worker;
        worker.Submit([]()->bool{throw std::runtime_error("packing failure");});
        Assert::ExpectException<std::runtime_error>([&]{worker.Wait();});
        worker.Submit([]{return true;});Assert::IsTrue(worker.Wait().success);
    }
    TEST_METHOD(DestructorDrainsBufferReadsBeforeReturning)
    {
        std::promise<void> entered,release;auto gate=release.get_future().share();
        std::atomic_bool finished=false;
        auto worker=std::make_unique<MagewellPackingWorker>();
        worker->Submit([&]{entered.set_value();gate.wait();finished=true;return true;});
        entered.get_future().wait();
        auto destroy=std::async(std::launch::async,[w=std::move(worker)]()mutable{w.reset();});
        const bool blocked=destroy.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
        release.set_value();destroy.get();Assert::IsTrue(blocked);Assert::IsTrue(finished.load());
    }
}; }
