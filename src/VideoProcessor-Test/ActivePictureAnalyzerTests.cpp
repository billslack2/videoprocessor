#include "pch.h"
#include "CppUnitTest.h"

#include <ActivePictureAnalyzer.h>
#include <vector>
#include <algorithm>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(ActivePictureAnalyzerTests)
	{
	public:
		TEST_METHOD(UnavailableP010FrameProducesOneDeterministicObservation)
		{
			ActivePictureAnalyzer analyzer;
			const ActivePictureAnalyzerResult first = analyzer.Analyze({
				{}, 1, 60.0 });
			Assert::IsFalse(first.analyzed);

			P010PlaneView missingFrame;
			missingFrame.width = 1920;
			missingFrame.height = 1080;
			const ActivePictureAnalyzerResult unavailable = analyzer.Analyze({
				missingFrame, 6, 60.0 });
			Assert::IsTrue(unavailable.analyzed);
			Assert::IsFalse(unavailable.evidence.available);
			Assert::IsTrue(unavailable.evidence.reason ==
				"P010 sample pointer is unavailable");
			Assert::IsFalse(unavailable.decision.stable);
		}

		TEST_METHOD(ResetRestartsTheSparseAnalysisSchedule)
		{
			ActivePictureAnalyzer analyzer;
			P010PlaneView missingFrame;
			missingFrame.width = 1920;
			missingFrame.height = 1080;
			Assert::IsTrue(analyzer.Analyze({ missingFrame, 1, 60.0 }).analyzed);
			Assert::IsFalse(analyzer.Analyze({ missingFrame, 2, 60.0 }).analyzed);
			analyzer.Reset();
			Assert::IsTrue(analyzer.Analyze({ missingFrame, 1, 60.0 }).analyzed);
		}
        TEST_METHOD(ConvertedP010AnalyzerUsesCommonFactsAndDoesNotCacheMutableSample)
        {
            constexpr int width=640,height=360,top=46,bottom=314;
            std::vector<uint16_t> pixels(width*height*3/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+width*height,uint16_t(64<<6));
            std::fill(pixels.begin()+width*top,pixels.begin()+width*bottom,uint16_t(300<<6));
            P010PlaneView frame{reinterpret_cast<const uint8_t*>(pixels.data()),pixels.size()*sizeof(uint16_t),
                width,height,width*sizeof(uint16_t),width*sizeof(uint16_t)};
            ActivePictureAnalyzer analyzer;
            ActivePictureAnalyzerResult result;
            for(uint64_t sequence=1;sequence<=30;++sequence) {
                const auto observed=analyzer.Analyze({frame,sequence,24.0,23});
                if(observed.analyzed)result=observed;
            }
            Assert::IsTrue(result.evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured);
            Assert::IsTrue(result.evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(top,result.evidence.physicalEdges.estimate.top);
            Assert::AreEqual(bottom,result.evidence.physicalEdges.estimate.bottom);
            Assert::AreEqual(uint64_t(23),result.evidence.physicalEdges.sourceGeneration);
            Assert::IsTrue(result.evidence.candidateVerification.vertical==ActivePictureCandidateVerificationState::VERIFIED);
            Assert::IsTrue(result.decision.stable);
            Assert::AreEqual(result.evidence.trustedBounds.top,result.decision.stableBounds.top);
            Assert::AreEqual(result.evidence.trustedBounds.bottom,result.decision.stableBounds.bottom);
            Assert::IsTrue(result.evidence.currentSourceIdentityAvailable);
            Assert::AreEqual(uint64_t(23),result.evidence.currentSourceIdentity.transportGeneration);
            // DirectShow may relocate subtitles into this sample after analysis.
            // Reusing its address must never reuse the old pixel facts.
            std::fill(pixels.begin(),pixels.begin()+width*height,uint16_t(300<<6));
            const auto changed=analyzer.Analyze({frame,1000,24.0,23});
            Assert::IsTrue(changed.analyzed && changed.evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(0,changed.evidence.physicalEdges.estimate.top);
            Assert::AreEqual(height,changed.evidence.physicalEdges.estimate.bottom);
            Assert::AreEqual(uint64_t(1000),changed.evidence.currentSourceIdentity.acceptedSequence);
        }

        TEST_METHOD(ConvertedP010AnalyzerNewGenerationCannotPublishOldOrIncompleteGeometry)
        {
            constexpr int width=640,height=360;
            std::vector<uint16_t> pixels(width*height*3/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+width*height,uint16_t(300<<6));
            P010PlaneView frame{reinterpret_cast<const uint8_t*>(pixels.data()),pixels.size()*sizeof(uint16_t),
                width,height,width*sizeof(uint16_t),width*sizeof(uint16_t)};
            ActivePictureAnalyzer analyzer;
            bool acquired=false;
            for(uint64_t sequence=1;sequence<=30;++sequence) {
                const auto result=analyzer.Analyze({frame,sequence,24.0,31});
                acquired=acquired || result.decision.stable;
            }
            Assert::IsTrue(acquired);
            frame.dataBytes=1;
            const auto incomplete=analyzer.Analyze({frame,1,24.0,32});
            Assert::IsTrue(incomplete.analyzed);
            Assert::IsFalse(incomplete.evidence.available);
            Assert::IsFalse(incomplete.evidence.physicalEdges.estimateAvailable);
            Assert::IsTrue(incomplete.evidence.detector==ActivePictureDetectorKind::SourceSpatialUnavailable);
            Assert::IsFalse(incomplete.decision.stable);
            Assert::IsFalse(incomplete.decision.publish);
            Assert::AreEqual(uint64_t(32),incomplete.evidence.currentSourceIdentity.transportGeneration);
        }
	};
}
