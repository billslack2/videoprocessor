#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        ActivePictureBounds Bounds(int left, int top, int right, int bottom,
            int width=3840, int height=2160)
        {
            return {left,top,right,bottom,width,height,
                static_cast<double>(right-left)/(bottom-top),ActivePictureBounds::BarAxes::NONE};
        }

        PresentationEnvelopeCompositionInput RecordedEnvelope()
        {
            PresentationEnvelopeCompositionInput input;
            input.trustedPicture=Bounds(0,276,3840,1884);
            input.trustedPicture.trustedBarAxes=ActivePictureBounds::BarAxes::TOP_BOTTOM;
            input.detectorContent.bounds=Bounds(0,54,3840,2106);
            input.detectorContent.expandTop=input.detectorContent.expandBottom=true;
            input.denseContent.bounds=Bounds(0,68,3840,2092);
            input.denseContent.expandTop=input.denseContent.expandBottom=true;
            input.verticalPadding=34;
            return input;
        }

        void AssertBounds(const PresentationEnvelopeGeometryDecision& actual,
            int left, int top, int right, int bottom)
        {
            Assert::IsTrue(actual.valid);
            Assert::AreEqual(left,actual.bounds.left);
            Assert::AreEqual(top,actual.bounds.top);
            Assert::AreEqual(right,actual.bounds.right);
            Assert::AreEqual(bottom,actual.bounds.bottom);
        }
    }

    TEST_CLASS(PresentationEnvelopeCompositionTests)
    {
    public:
        TEST_METHOD(RecordedDetectorAndDenseEdgesAreCompletedSeparatelyBeforeUnion)
        {
            const auto input=RecordedEnvelope();
            const auto result=BuildComposedPresentationEnvelope(input);
            // Dense clearance is 68-34 / 2092+34. The detector's 54/2106
            // already fits inside it and must not receive another 34 pixels.
            AssertBounds(result,0,34,3840,2126);
            Assert::IsTrue(result.expanded);
            Assert::AreEqual(276,input.trustedPicture.top);
            Assert::AreEqual(1884,input.trustedPicture.bottom);
        }

        TEST_METHOD(DetectorOnlyUsesExactOutwardAlignedEdgesWithoutDenseClearance)
        {
            auto input=RecordedEnvelope();
            input.denseContent={};
            for (int padding : {0,10,20,34,500})
            {
                input.horizontalPadding=input.verticalPadding=padding;
                AssertBounds(BuildComposedPresentationEnvelope(input),0,54,3840,2106);
            }
            input.detectorContent.bounds=Bounds(0,55,3840,2105);
            AssertBounds(BuildComposedPresentationEnvelope(input),0,54,3840,2106);
            // Even a two-pixel outward observation gets no unrelated margin.
            input.detectorContent.bounds=Bounds(0,274,3840,1886);
            AssertBounds(BuildComposedPresentationEnvelope(input),0,274,3840,1886);
        }

        TEST_METHOD(DenseOnlyPreservesExistingPaddingAndAlignmentAcrossResolutions)
        {
            for (int scale : {1,2,4})
            {
                PresentationEnvelopeCompositionInput input;
                input.trustedPicture=Bounds(100*scale,100*scale,860*scale,440*scale,960*scale,540*scale);
                input.denseContent.bounds=Bounds(80*scale,60*scale,880*scale,480*scale,960*scale,540*scale);
                input.denseContent.expandLeft=input.denseContent.expandTop=true;
                input.denseContent.expandRight=input.denseContent.expandBottom=true;
                // Preserve the renderer's existing source minimum plus configured
                // padding. Composition must not add another minimum.
                for (int padding : {0,10,20,500})
                {
                    input.horizontalPadding=std::max(8,960*scale/160)+padding;
                    input.verticalPadding=std::max(8,540*scale/90)+padding;
                    PresentationEnvelopeGeometryInput legacy;
                    legacy.trustedPicture=input.trustedPicture;
                    legacy.observedContent=input.denseContent.bounds;
                    legacy.observedContentAvailable=true;
                    legacy.expandLeft=legacy.expandTop=legacy.expandRight=legacy.expandBottom=true;
                    legacy.horizontalPadding=input.horizontalPadding;
                    legacy.verticalPadding=input.verticalPadding;
                    const auto expected=BuildPresentationEnvelope(legacy);
                    const auto actual=BuildComposedPresentationEnvelope(input);
                    AssertBounds(actual,expected.bounds.left,expected.bounds.top,
                        expected.bounds.right,expected.bounds.bottom);
                    Assert::AreEqual(expected.expanded,actual.expanded);
                }
            }
            auto input=RecordedEnvelope(); input.detectorContent={};
            input.verticalPadding=20;
            AssertBounds(BuildComposedPresentationEnvelope(input),0,48,3840,2112);
            input.verticalPadding=500;
            AssertBounds(BuildComposedPresentationEnvelope(input),0,0,3840,2160);
        }

        TEST_METHOD(HorizontalFitDoesNotLeakUnselectedVerticalEdgesDuringTranslation)
        {
            PresentationEnvelopeCompositionInput input;
            input.trustedPicture=Bounds(200,276,3640,1884);
            input.detectorContent.bounds=Bounds(100,0,3740,2160);
            input.detectorContent.expandLeft=input.detectorContent.expandRight=true;
            input.denseContent.bounds=Bounds(140,20,3700,2140);
            input.denseContent.expandLeft=input.denseContent.expandRight=true;
            input.horizontalPadding=20; input.verticalPadding=500;
            VerticalBarPresentationResolutionInput vertical;
            vertical.detailedAction=VerticalBarPresentationAction::TRANSLATE;
            vertical.translationPixels=178;
            vertical.authoritativeTop=276; vertical.authoritativeBottom=1884;
            vertical.rasterHeight=2160;
            const auto routing=ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(vertical));
            Assert::IsTrue(routing.translationActive);
            Assert::IsFalse(routing.fitActive);
            input.detectorContent.expandTop=input.detectorContent.expandBottom=routing.fitActive;
            input.denseContent.expandTop=input.denseContent.expandBottom=routing.fitActive;
            AssertBounds(BuildComposedPresentationEnvelope(input),100,276,3740,1884);
        }

        TEST_METHOD(ActiveMalformedComponentIsRejectedAndInactiveMalformedComponentIsIgnored)
        {
            for (bool detector : {false,true})
            {
                for (int fault=0;fault<6;++fault)
                {
                    auto input=RecordedEnvelope();
                    auto& component=detector ? input.detectorContent : input.denseContent;
                    switch (fault)
                    {
                    case 0: component.bounds.rasterWidth=1920; break;
                    case 1: component.bounds.rasterHeight=1080; break;
                    case 2: component.bounds.left=-2; break;
                    case 3: component.bounds.bottom=2162; break;
                    case 4: component.bounds.top=component.bounds.bottom; break;
                    case 5: component.bounds.right=component.bounds.left; break;
                    }
                    Assert::IsFalse(BuildComposedPresentationEnvelope(input).valid,
                        L"An active component must not hide its invalid bounds behind the trusted rectangle.");
                    component.expandLeft=component.expandTop=component.expandRight=component.expandBottom=false;
                    const auto ignored=BuildComposedPresentationEnvelope(input);
                    if (detector) AssertBounds(ignored,0,34,3840,2126);
                    else AssertBounds(ignored,0,54,3840,2106);
                }
            }
            auto input=RecordedEnvelope();
            input.trustedPicture.top=277;
            Assert::IsFalse(BuildComposedPresentationEnvelope(input).valid);
            input=RecordedEnvelope(); input.trustedPicture.rasterWidth=0;
            Assert::IsFalse(BuildComposedPresentationEnvelope(input).valid);
        }

        TEST_METHOD(SelectedEdgesAlignOutwardAndDensePaddingClampsToRaster)
        {
            PresentationEnvelopeCompositionInput input;
            input.trustedPicture=Bounds(100,100,860,440,960,540);
            input.detectorContent.bounds=Bounds(51,71,909,469,960,540);
            input.detectorContent.expandLeft=input.detectorContent.expandTop=true;
            input.detectorContent.expandRight=input.detectorContent.expandBottom=true;
            input.horizontalPadding=input.verticalPadding=500;
            AssertBounds(BuildComposedPresentationEnvelope(input),50,70,910,470);
            input.denseContent=input.detectorContent;
            input.denseContent.bounds=Bounds(1,1,959,539,960,540);
            input.horizontalPadding=input.verticalPadding=10;
            AssertBounds(BuildComposedPresentationEnvelope(input),0,0,960,540);
        }

        TEST_METHOD(HeldDenseEnvelopeSurvivesSparseScansWhileCurrentDetectorGrowthIsImmediate)
        {
            auto input=RecordedEnvelope();
            PresentationEnvelopeInput lifetime;
            lifetime.envelopeAvailable=lifetime.effectiveGeometryAvailable=lifetime.baseMatchesEffectiveGeometry=true;
            lifetime.evidenceSourceGeneration=lifetime.frameSourceGeneration=7;
            lifetime.holdMs=2000;
            const int coarseTop[]={54,68,80,52,60,20,54,70,84};
            const int coarseBottom[]={2106,2092,2080,2108,2100,2140,2106,2090,2076};
            const int expectedTop[]={34,34,34,34,34,20,34,34,34};
            const int expectedBottom[]={2126,2126,2126,2126,2126,2140,2126,2126,2126};
            for (int frame=0;frame<9;++frame)
            {
                lifetime.currentSourceSequence=100+frame;
                lifetime.currentTick=1000+frame*42;
                const bool scanned=frame%3==0;
                if (scanned)
                {
                    lifetime.detectedSourceSequence=lifetime.currentSourceSequence;
                    lifetime.lastDetectionTick=lifetime.currentTick;
                }
                const auto live=EvaluatePresentationEnvelope(lifetime);
                Assert::IsTrue(live.active);
                Assert::AreEqual(!scanned,live.held);
                input.denseContent.expandTop=input.denseContent.expandBottom=live.active;
                input.detectorContent.bounds=Bounds(0,coarseTop[frame],3840,coarseBottom[frame]);
                AssertBounds(BuildComposedPresentationEnvelope(input),0,expectedTop[frame],3840,expectedBottom[frame]);
            }
        }

        TEST_METHOD(ComposedEnvelopeReachesFinalAdmissionWithoutChangingTrustedScope)
        {
            const auto composition=RecordedEnvelope();
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometry=composition.trustedPicture;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
            crop.frameSourceSequence=100; crop.rasterWidth=3840; crop.rasterHeight=2160;
            const auto trusted=AdmitCropPresentation({},crop,Evaluate(crop),9).state;
            const auto envelope=BuildComposedPresentationEnvelope(composition);
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=envelope.valid && envelope.expanded;
            crop.outwardExpansion=envelope.bounds; crop.outwardExpansionSourceGeneration=7;
            const auto shown=AdmitCropPresentation(trusted,crop,Evaluate(crop),9);
            Assert::IsFalse(shown.blocked);
            Assert::IsTrue(shown.presentation.applyCrop && shown.presentation.outwardExpanded);
            Assert::AreEqual(34,shown.presentation.sourceBounds.top);
            Assert::AreEqual(2126,shown.presentation.sourceBounds.bottom);
            Assert::AreEqual(276,shown.state.trustedCrop.top);
            Assert::AreEqual(1884,shown.state.trustedCrop.bottom);
        }

        TEST_METHOD(EmptyAndInwardComponentsDoNotChangeTrustedPicture)
        {
            auto input=RecordedEnvelope();
            input.detectorContent={}; input.denseContent={};
            input.horizontalPadding=input.verticalPadding=500;
            auto actual=BuildComposedPresentationEnvelope(input);
            AssertBounds(actual,0,276,3840,1884);
            Assert::IsFalse(actual.expanded);
            input.detectorContent.bounds=input.denseContent.bounds=Bounds(100,300,3700,1800);
            input.detectorContent.expandLeft=input.detectorContent.expandTop=true;
            input.detectorContent.expandRight=input.detectorContent.expandBottom=true;
            input.denseContent.expandLeft=input.denseContent.expandTop=true;
            input.denseContent.expandRight=input.denseContent.expandBottom=true;
            actual=BuildComposedPresentationEnvelope(input);
            AssertBounds(actual,0,276,3840,1884);
            Assert::IsFalse(actual.expanded,L"Padding may not turn inward content into an outward expansion.");
        }

        TEST_METHOD(ActiveDenseComponentRejectsNegativePadding)
        {
            for (bool horizontal : {false,true})
            {
                auto input=RecordedEnvelope();
                if (horizontal) input.horizontalPadding=-1;
                else input.verticalPadding=-1;
                Assert::IsFalse(BuildComposedPresentationEnvelope(input).valid);
            }
        }

        TEST_METHOD(EveryEdgeMaskPreservesTrustedRectangleAndOnlyItsSelectedContent)
        {
            // Detector extends farther horizontally; padded dense content
            // extends farther vertically. Independent numeric targets exercise
            // both union winners and all 256 pairs of routed edge selections.
            for (unsigned detectorMask=0;detectorMask<16;++detectorMask)
            for (unsigned denseMask=0;denseMask<16;++denseMask)
            {
                PresentationEnvelopeCompositionInput input;
                input.trustedPicture=Bounds(100,100,860,440,960,540);
                input.detectorContent.bounds=Bounds(51,71,909,469,960,540);
                input.denseContent.bounds=Bounds(80,60,880,480,960,540);
                auto setMask=[](PresentationEnvelopeContent& content,unsigned mask)
                {
                    content.expandLeft=(mask&1)!=0; content.expandTop=(mask&2)!=0;
                    content.expandRight=(mask&4)!=0; content.expandBottom=(mask&8)!=0;
                };
                setMask(input.detectorContent,detectorMask); setMask(input.denseContent,denseMask);
                input.horizontalPadding=input.verticalPadding=20;
                const auto actual=BuildComposedPresentationEnvelope(input);
                const int left=(detectorMask&1) ? 50 : (denseMask&1) ? 60 : 100;
                const int top=(denseMask&2) ? 40 : (detectorMask&2) ? 70 : 100;
                const int right=(detectorMask&4) ? 910 : (denseMask&4) ? 900 : 860;
                const int bottom=(denseMask&8) ? 500 : (detectorMask&8) ? 470 : 440;
                AssertBounds(actual,left,top,right,bottom);
                Assert::IsTrue(actual.bounds.left<=100 && actual.bounds.top<=100 &&
                    actual.bounds.right>=860 && actual.bounds.bottom>=440);
                Assert::AreEqual(0,(actual.bounds.left|actual.bounds.top|
                    actual.bounds.right|actual.bounds.bottom)&1);
                Assert::AreEqual((detectorMask|denseMask)!=0,actual.expanded);
            }
        }

        TEST_METHOD(HeldDetectorAccumulationDoesNotShrinkUntilItsLifetimeIsInvalidated)
        {
            auto input=RecordedEnvelope();
            auto heldDetector=input.trustedPicture;
            PresentationEnvelopeInput lifetime;
            lifetime.envelopeAvailable=lifetime.effectiveGeometryAvailable=lifetime.baseMatchesEffectiveGeometry=true;
            lifetime.evidenceSourceGeneration=lifetime.frameSourceGeneration=7;
            lifetime.holdMs=2000;
            const int observedTop[]={54,68,80,52,60,20,54,70,84};
            const int observedBottom[]={2106,2092,2080,2108,2100,2140,2106,2090,2076};
            for (int frame=0;frame<9;++frame)
            {
                // Model the existing renderer's retained widest detector
                // envelope, not a fresh narrower rectangle on every frame.
                heldDetector.top=std::min(heldDetector.top,observedTop[frame]);
                heldDetector.bottom=std::max(heldDetector.bottom,observedBottom[frame]);
                lifetime.detectedSourceSequence=lifetime.currentSourceSequence=100+frame;
                lifetime.lastDetectionTick=lifetime.currentTick=1000+frame*42;
                const auto active=EvaluatePresentationEnvelope(lifetime);
                input.detectorContent.bounds=heldDetector;
                input.detectorContent.expandTop=input.detectorContent.expandBottom=active.active;
                AssertBounds(BuildComposedPresentationEnvelope(input),0,frame<5 ? 34 : 20,
                    3840,frame<5 ? 2126 : 2140);
            }
            for (int fault=0;fault<3;++fault)
            {
                auto invalid=lifetime;
                invalid.currentSourceSequence=109; invalid.currentTick=1378;
                if (fault==0) invalid.currentTick=3337; // One tick beyond the retained hold.
                if (fault==1) invalid.frameSourceGeneration=8;
                if (fault==2) invalid.baseMatchesEffectiveGeometry=false;
                const auto expired=EvaluatePresentationEnvelope(invalid);
                Assert::IsFalse(expired.active);
                auto after=input;
                after.detectorContent.expandTop=after.detectorContent.expandBottom=expired.active;
                // Current dense evidence remains valid and keeps its own margin.
                AssertBounds(BuildComposedPresentationEnvelope(after),0,34,3840,2126);
            }
        }

        TEST_METHOD(ProvisionalInspectionHandsOffToComposedFitWithoutAcquiringNewCrop)
        {
            const auto composition=RecordedEnvelope();
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometry=composition.trustedPicture;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
            crop.frameSourceSequence=99; crop.rasterWidth=3840; crop.rasterHeight=2160;
            auto admitted=AdmitCropPresentation({},crop,Evaluate(crop),9).state;
            crop.latestObservationSupportsCrop=false;
            crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameSourceSequence=100;
            crop.currentVisibleBoundsAvailable=true;
            crop.currentVisibleBounds=composition.detectorContent.bounds;
            crop.currentVisibleBase=crop.geometry;
            crop.currentVisibleSourceGeneration=7; crop.currentVisibleSourceSequence=100;
            const auto unresolved=Evaluate(crop);
            Assert::IsFalse(unresolved.applyCrop);
            VerticalInspectionBridgeInput bridge;
            bridge.candidate=true;
            bridge.retentionRequested=unresolved.withdrawalCause==WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
            bridge.sourceGeneration=7; bridge.sourceSequence=100; bridge.presentationEpoch=9;
            bridge.trustedBase=crop.geometry;
            const auto inspecting=UpdateVerticalInspectionBridge(bridge);
            Assert::IsTrue(inspecting.retain);
            crop.verticalInspectionPending=inspecting.retain;
            crop.verticalInspectionSourceGeneration=7; crop.verticalInspectionSourceSequence=100;
            const auto retained=AdmitCropPresentation(admitted,crop,Evaluate(crop),9);
            Assert::IsFalse(retained.blocked);
            Assert::IsTrue(retained.presentation.owner==DecisionOwner::VERTICAL_INSPECTION);
            Assert::AreEqual(276,retained.presentation.sourceBounds.top);

            const auto envelope=BuildComposedPresentationEnvelope(composition);
            VerticalInspectionFitResolutionInput fit;
            fit.confirmedDenseFit=fit.denseAnalysisCurrent=true;
            fit.outwardExpansionAvailable=envelope.valid && envelope.expanded;
            fit.trustedBase=crop.geometry; fit.outwardExpansion=envelope.bounds;
            fit.outwardExpansionSourceGeneration=fit.frameSourceGeneration=7;
            bridge.previous=inspecting.state;
            bridge.sourceSequence=101; bridge.denseAnalysisCompleted=true;
            bridge.confirmedVerticalFitResolved=CanResolveVerticalInspectionWithConfirmedFit(fit);
            Assert::IsTrue(bridge.confirmedVerticalFitResolved);
            const auto resolved=UpdateVerticalInspectionBridge(bridge);
            Assert::IsFalse(resolved.state.active);
            Assert::IsFalse(resolved.retain);
            crop.frameSourceSequence=crop.currentVisibleSourceSequence=101;
            crop.verticalInspectionPending=resolved.retain;
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=envelope.valid && envelope.expanded;
            crop.outwardExpansion=envelope.bounds; crop.outwardExpansionSourceGeneration=7;
            const auto shown=AdmitCropPresentation(retained.state,crop,Evaluate(crop),9);
            Assert::IsFalse(crop.latestObservationSupportsCrop);
            Assert::IsFalse(shown.blocked);
            Assert::IsTrue(shown.presentation.owner==DecisionOwner::OUTWARD_FIT);
            Assert::AreEqual(34,shown.presentation.sourceBounds.top);
            Assert::AreEqual(2126,shown.presentation.sourceBounds.bottom);
            Assert::AreEqual(276,shown.state.trustedCrop.top);
            Assert::AreEqual(1884,shown.state.trustedCrop.bottom);
            const auto unacquired=AdmitCropPresentation({},crop,Evaluate(crop),9);
            Assert::IsTrue(unacquired.blocked,L"The same provisional FIT cannot acquire an unpresented crop.");
        }
    };
}
