#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/ProcessingPerformanceWindow.h>
#include <limits>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(ProcessingPerformanceWindowTests) {
public:
    TEST_METHOD(SourcePreparationAndRepeatedDrawsKeepIndependentDenominators) {
        ProcessingPerformanceWindow meter;const ProcessingPerformanceWindow::Context context{1,2,3};
        // Two seconds of 24 Hz sources on a 60 Hz output. Only real preparation
        // calls enter that mean; 72 cheap repeats cannot dilute their cost.
        for(uint64_t output=0;output<120;++output) {
            const bool fresh=output%5==0 || output%5==2;
            const uint64_t tick=output*2000/120;
            if(fresh)meter.RecordPreparation(context,tick,output==0?10.0:2.0);
            meter.RecordDraw(context,tick,!fresh,fresh?12.0:0.5);
        }
        Assert::IsFalse(meter.Snapshot(1999).available);
        meter.PublishIfDue(2000);const auto value=meter.Snapshot(2000);
        Assert::IsTrue(value.available);Assert::AreEqual(uint64_t(2000),value.windowMilliseconds);
        Assert::AreEqual(uint64_t(48),value.preparationCalls);
        Assert::AreEqual(uint64_t(48),value.sourceDraws);Assert::AreEqual(uint64_t(72),value.reusedDraws);
        Assert::AreEqual(104.0/48.0,value.preparationCpu.average,0.000001);
        Assert::AreEqual(10.0,value.preparationCpu.peak);Assert::AreEqual(12.0,value.sourceDrawCpu.average);
        Assert::AreEqual(0.5,value.reusedDrawCpu.average);Assert::AreEqual(0.5,value.reusedDrawCpu.peak);
    }
    TEST_METHOD(ContextChangesAndAgeNeverExposePreviousModeCosts) {
        for(int changed=0;changed<3;++changed) {
            ProcessingPerformanceWindow meter;ProcessingPerformanceWindow::Context context{1,2,3};
            meter.RecordPreparation(context,100,90);meter.RecordDraw(context,2100,false,20);
            Assert::IsTrue(meter.Snapshot(2100).available);
            if(changed==0)++context.generation;if(changed==1)++context.processingEpoch;if(changed==2)++context.mode;
            meter.RecordDraw(context,2101,true,1);
            Assert::IsFalse(meter.Snapshot(2101).available);
            meter.PublishIfDue(4101);const auto next=meter.Snapshot(4101);
            Assert::IsTrue(next.available);Assert::AreEqual(uint64_t(0),next.preparationCalls);
            Assert::AreEqual(uint64_t(0),next.sourceDraws);Assert::AreEqual(uint64_t(1),next.reusedDraws);
            Assert::AreEqual(1.0,next.reusedDrawCpu.average);
            Assert::IsTrue(meter.Snapshot(8101).available);Assert::IsFalse(meter.Snapshot(8102).available);
            Assert::IsFalse(meter.Snapshot(4100).available);
        }
    }
    TEST_METHOD(PreparationWithoutSuccessfulSubmissionAndZeroCostStayTruthful) {
        ProcessingPerformanceWindow meter;const ProcessingPerformanceWindow::Context context{1,1,1};
        meter.RecordPreparation(context,0,0);meter.RecordPreparation(context,2000,4);
        const auto value=meter.Snapshot(2000);
        Assert::IsTrue(value.available);Assert::AreEqual(uint64_t(2),value.preparationCalls);
        Assert::AreEqual(2.0,value.preparationCpu.average);Assert::AreEqual(4.0,value.preparationCpu.peak);
        Assert::AreEqual(uint64_t(0),value.sourceDraws);Assert::AreEqual(uint64_t(0),value.reusedDraws);
    }
    TEST_METHOD(PublicationWindowsDoNotCarryPriorPeaksOrInvalidDurations) {
        ProcessingPerformanceWindow meter;const ProcessingPerformanceWindow::Context context{1,1,1};
        meter.RecordDraw(context,0,false,100);meter.PublishIfDue(2000);
        meter.RecordDraw(context,2001,false,3);meter.RecordDraw(context,2002,true,0);
        meter.RecordPreparation(context,2003,std::numeric_limits<double>::quiet_NaN());
        meter.RecordPreparation(context,2004,std::numeric_limits<double>::infinity());
        meter.RecordPreparation(context,2005,-1);
        meter.PublishIfDue(4000);const auto value=meter.Snapshot(4000);
        Assert::AreEqual(uint64_t(0),value.preparationCalls);Assert::AreEqual(uint64_t(1),value.sourceDraws);
        Assert::AreEqual(3.0,value.sourceDrawCpu.peak);Assert::AreEqual(0.0,value.reusedDrawCpu.average);
        Assert::AreEqual(uint64_t(1),value.reusedDraws);
        meter.RecordDraw(context,3,false,8);Assert::IsFalse(meter.Snapshot(4000).available);
    }
};
}
