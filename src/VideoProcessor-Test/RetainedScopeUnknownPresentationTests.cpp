#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <deque>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        ActivePictureBounds IncidentScope()
        {
            return {0,278,3840,1880,3840,2160,3840.0/1602.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
        }
        ActivePictureBounds VisibleBounds(int top,int bottom)
        {
            return {0,top,3840,bottom,3840,2160,3840.0/(bottom-top),
                ActivePictureBounds::BarAxes::NONE};
        }
        void AssertBounds(const ActivePictureBounds& expected,const ActivePictureBounds& actual)
        {
            Assert::AreEqual(expected.left,actual.left);
            Assert::AreEqual(expected.top,actual.top);
            Assert::AreEqual(expected.right,actual.right);
            Assert::AreEqual(expected.bottom,actual.bottom);
            Assert::AreEqual(expected.rasterWidth,actual.rasterWidth);
            Assert::AreEqual(expected.rasterHeight,actual.rasterHeight);
        }
        PresentationRecoveryInput UnknownIncident(uint64_t sequence=9426,bool nearBlack=false)
        {
            PresentationRecoveryInput input;
            auto& crop=input.crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.geometry=IncidentScope();
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=2;
            crop.frameSourceSequence=sequence;crop.framePresentationEpoch=9;
            crop.rasterWidth=3840;crop.rasterHeight=2160;
            crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=true;
            crop.currentVisibleBoundsAvailable=true;
            crop.currentVisibleBase=crop.geometry;
            crop.currentVisibleBounds=VisibleBounds(0,2160);
            crop.currentVisibleSourceGeneration=2;crop.currentVisibleSourceSequence=sequence;
            crop.verticalTranslationBase=crop.geometry;
            crop.verticalTranslationSourceGeneration=2;
            crop.nearBlackEpisodeRetainCrop=nearBlack;
            input.previousAdmission.available=true;
            input.previousAdmission.trustedCrop=crop.geometry;
            input.previousAdmission.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            input.previousAdmission.sourceGeneration=2;input.previousAdmission.presentationEpoch=9;
            input.submittedScopeAnchor.available=true;
            input.submittedScopeAnchor.crop=crop.geometry;
            input.submittedScopeAnchor.sourceGeneration=2;
            input.submittedScopeAnchor.presentationEpoch=9;
            input.submittedScopeAnchor.lastSubmittedSourceSequence=sequence-1;
            input.submittedScopeAnchor.lastSubmittedPresentationSourceSequence=sequence-1;
            input.measurementCurrent=input.retentionEvaluated=input.nearBlackEvaluated=true;
            input.observationAvailable=true;
            input.observationClassification=ActivePictureClassification::PROVISIONAL;
            input.observation={1138,280,3594,2160,3840,2160,2456.0/1880.0,
                ActivePictureBounds::BarAxes::NONE};
            input.retentionBounds=crop.geometry;input.retentionSourceGeneration=2;
            input.retentionSourceSequence=sequence;
            input.globalNearBlack=nearBlack;input.presentationEpoch=9;input.framesPerSecond=23.976;
            input.candidate.sourceBounds=VisibleBounds(0,2160);
            input.candidate.reason="unresolved observation has no independent presentation owner";
            return input;
        }
        ConfirmedFitOwnershipInput CaptionOwner(const PresentationRecoveryInput& input,
            const ActivePictureBounds& approved,const ActivePictureBounds& evidence)
        {
            ConfirmedFitOwnershipInput owner;
            owner.origin=ConfirmedFitOwnershipOrigin::CURRENT_CAPTION;
            owner.current=owner.confirmed=true;
            owner.nativeBase=input.crop.geometry;owner.approvedBounds=approved;
            owner.evidenceBounds=evidence;
            owner.sourceGeneration=input.crop.frameSourceGeneration;
            owner.sourceSequence=input.crop.frameSourceSequence;
            owner.presentationEpoch=input.presentationEpoch;
            return owner;
        }
        void SetFitCandidate(PresentationRecoveryInput& input,const ActivePictureBounds& bounds)
        {
            input.crop.outwardPresentationActive=input.crop.outwardExpansionAvailable=true;
            input.crop.outwardExpansion=bounds;
            input.crop.outwardExpansionSourceGeneration=input.crop.frameSourceGeneration;
            auto owner=input.crop;
            owner.nearBlackEpisodeRetainCrop=owner.nearBlackEpisodeFullRaster=false;
            input.candidate=Evaluate(owner);
            Assert::IsTrue(input.candidate.applyCrop && input.candidate.owner==DecisionOwner::OUTWARD_FIT);
            AssertBounds(bounds,input.candidate.sourceBounds);
        }
        SubmittedNativeScopeAnchor Submit(const PresentationRecoveryInput& input,
            const Decision& selected,CropPresentationAdmissionState& admission,
            const AspectLimitFillDecision* fill=nullptr,bool submitted=true)
        {
            auto admitted=AdmitCropPresentation(admission,input.crop,selected,input.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            admission=admitted.state;
            if(fill && fill->applied)
            {
                admission.optionalFillApplied=true;
                admission.optionalFillRetained=fill->retainedSubmittedFill;
                admission.optionalFillSourceBounds=fill->sourceBounds;
                admission.optionalFillSourceSequence=input.crop.frameSourceSequence;
                admission.optionalFillScreenAspect=2.35;
            }
            SubmittedNativeScopeAnchorUpdateInput update;
            update.previous=input.submittedScopeAnchor;update.selectedAdmission=admission;
            update.nativeCropAvailable=admission.available;update.nativeCrop=admission.trustedCrop;
            update.submittedBounds=fill && fill->applied?fill->sourceBounds:selected.sourceBounds;
            update.authorityOrigin=input.crop.authorityOrigin;update.selectedOwner=selected.owner;
            update.sourceGeneration=input.crop.frameSourceGeneration;
            update.sourceSequence=input.crop.frameSourceSequence;
            update.presentationEpoch=input.presentationEpoch;update.frameSubmitted=submitted;
            return CommitSubmittedNativeScopeAnchor(update);
        }
        FinalAspectLimitFillInput ScreenFill(const PresentationRecoveryInput& input,
            const UncertainPicturePresentationDecision& resolved)
        {
            FinalAspectLimitFillInput fill;
            fill.crop=input.crop;fill.presentation=resolved.presentation;
            fill.evidence=input;fill.uncertainPicture=resolved;
            fill.configuredScreenActive=true;fill.configured.screenAspect=2.35;
            fill.configured.cropNarrowerContentToFillScreen=fill.configured.narrowerLimitConfigured=true;
            fill.configured.narrowerAspectLimit=2.32;
            fill.configured.cropWiderContentToFillScreen=fill.configured.widerLimitConfigured=true;
            fill.configured.widerAspectLimit=2.42;
            fill.currentConfirmedPresentationConflict=false;
            return fill;
        }
        NearBlackPresentationEpisodeState RetainedEpisode(const PresentationRecoveryInput& input)
        {
            NearBlackPresentationEpisodeState episode;
            episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            episode.sourceGeneration=input.crop.frameSourceGeneration;
            episode.presentationEpoch=input.presentationEpoch;
            episode.startedSourceSequence=input.crop.frameSourceSequence-6;
            episode.entryTrustedCropAvailable=true;
            episode.entryTrustedCrop=input.crop.geometry;
            episode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            return episode;
        }
        Decision ResolveIncident(PresentationRecoveryInput input)
        {
            auto candidate=input.candidate;
            if(input.crop.nearBlackEpisodeRetainCrop)
                candidate=EvaluateNearBlackBoundedPresentation(RetainedEpisode(input),input).presentation;
            return ResolveUncertainPicturePresentation(input,candidate).presentation;
        }
        void MakeNativeCurrent(PresentationRecoveryInput& input,const ActivePictureBounds& bounds)
        {
            auto& crop=input.crop;
            crop.geometry=bounds;crop.latestObservationSupportsCrop=true;
            crop.latestObservationIsProvisional=crop.latestObservationIsUnavailable=false;
            crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.currentVisibleBase=bounds;crop.currentVisibleBounds=bounds;
            input.retentionBounds=bounds;input.observation=bounds;
            input.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
        }
        struct EnvelopePacket
        {
            Decision presentation;
            RetainedSubmittedEnvelope retained;
            uint64_t sequence=0;
        };
        EnvelopePacket FreshEnvelope(uint64_t sequence,const ActivePictureBounds& bounds,
            DecisionOwner owner=DecisionOwner::OUTWARD_FIT)
        {
            EnvelopePacket packet;
            packet.sequence=sequence;packet.presentation.sourceBounds=bounds;
            packet.presentation.applyCrop=true;packet.presentation.owner=owner;
            return packet;
        }
        SubmittedNativeScopeAnchorUpdateInput EnvelopeSelection(
            const SubmittedNativeScopeAnchor& anchor,const EnvelopePacket& packet)
        {
            SubmittedNativeScopeAnchorUpdateInput selection;
            selection.previous=anchor;selection.nativeCropAvailable=true;
            selection.nativeCrop=IncidentScope();selection.submittedBounds=packet.presentation.sourceBounds;
            selection.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            selection.selectedOwner=packet.presentation.owner;
            selection.sourceGeneration=2;selection.sourceSequence=packet.sequence;selection.presentationEpoch=9;
            selection.selectedAdmission.available=true;selection.selectedAdmission.trustedCrop=IncidentScope();
            selection.selectedAdmission.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            selection.selectedAdmission.sourceGeneration=2;selection.selectedAdmission.presentationEpoch=9;
            return selection;
        }
        SubmittedNativeScopeAnchor SubmitEnvelope(const SubmittedNativeScopeAnchor& anchor,
            const EnvelopePacket& packet,bool succeeded=true)
        {
            auto selection=EnvelopeSelection(anchor,packet);selection.frameSubmitted=succeeded;
            return CommitSubmittedNativeScopeAnchor(selection);
        }
        SubmittedNativeScopeAnchor EnvelopeAnchor(const ActivePictureBounds& envelope)
        {
            const auto native=SubmitEnvelope({},FreshEnvelope(226,IncidentScope(),DecisionOwner::TRUSTED_CROP));
            return SubmitEnvelope(native,FreshEnvelope(227,envelope,DecisionOwner::TRANSLATION_CONFIRMATION));
        }
        EnvelopePacket PrepareEnvelope(const SubmittedNativeScopeAnchor& anchor,uint64_t sequence)
        {
            auto input=UnknownIncident(sequence);
            input.submittedScopeAnchor=anchor;
            input.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.candidate.owner=DecisionOwner::OUTWARD_FIT;
            const auto resolved=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsTrue(resolved.retained && resolved.expanded && resolved.retainedEnvelope.available);
            EnvelopePacket packet;
            packet.sequence=sequence;packet.presentation=resolved.presentation;
            packet.retained=resolved.retainedEnvelope;return packet;
        }
        EnvelopePacket SelectEnvelope(const SubmittedNativeScopeAnchor& anchor,EnvelopePacket packet)
        {
            const auto selected=RevalidateRetainedSubmittedEnvelopeSelection(
                packet.retained,EnvelopeSelection(anchor,packet));
            if(selected.expanded)packet.presentation.sourceBounds=selected.sourceBounds;
            return packet;
        }
    }

    TEST_CLASS(RetainedScopeUnknownPresentationTests)
    {
    public:
        TEST_METHOD(DelayedRetainedEnvelopesConvergeWithoutAlternatingOrRenewingNativeAuthority)
        {
            const auto larger=VisibleBounds(66,2132),smaller=VisibleBounds(148,2048);
            for(int lag:{0,1,3})
            {
                auto anchor=EnvelopeAnchor(larger);
                std::deque<EnvelopePacket> queue;
                for(int i=0;i<lag;++i)
                    queue.push_back(FreshEnvelope(231-lag+i,smaller,DecisionOwner::FIT_CONFIRMATION));
                for(uint64_t sequence=231;sequence<251;++sequence)
                {
                    queue.push_back(PrepareEnvelope(anchor,sequence));
                    const auto selected=SelectEnvelope(anchor,queue.front());queue.pop_front();
                    anchor=SubmitEnvelope(anchor,selected);
                    Assert::AreEqual(uint64_t(226),anchor.lastSubmittedSourceSequence,
                        L"Retained presentation cannot renew native picture authority.");
                    AssertBounds(selected.presentation.sourceBounds,anchor.submittedEnvelope);
                    if(sequence>=uint64_t(231+2*lag+2))
                        AssertBounds(larger,anchor.submittedEnvelope);
                }
            }
        }

        TEST_METHOD(DelayedRetainedEnvelopeNeverContractsAndFreshAuthorityDrainsTheQueue)
        {
            const auto larger=VisibleBounds(66,2132),smaller=VisibleBounds(148,2048);
            for(int lag:{0,1,3})for(bool native:{false,true})
            {
                auto anchor=EnvelopeAnchor(larger);
                std::deque<EnvelopePacket> queue;
                for(int i=0;i<lag;++i)queue.push_back(PrepareEnvelope(anchor,228+i));
                for(uint64_t sequence=uint64_t(228+lag);sequence<uint64_t(234+2*lag);++sequence)
                {
                    const auto bounds=native?IncidentScope():smaller;
                    auto input=UnknownIncident(sequence);input.submittedScopeAnchor=anchor;
                    input.candidate=FreshEnvelope(sequence,bounds,
                        native?DecisionOwner::TRUSTED_CROP:DecisionOwner::OUTWARD_FIT).presentation;
                    if(native)MakeNativeCurrent(input,IncidentScope());
                    else input.confirmedFitOwnership=QualifyConfirmedFitOwnership(CaptionOwner(input,bounds,bounds));
                    const auto resolved=ResolveUncertainPicturePresentation(input,input.candidate);
                    Assert::IsFalse(resolved.retainedEnvelope.available);
                    queue.push_back(FreshEnvelope(sequence,resolved.presentation.sourceBounds,resolved.presentation.owner));
                    const auto selected=SelectEnvelope(anchor,queue.front());queue.pop_front();
                    anchor=SubmitEnvelope(anchor,selected);
                    if(sequence>=uint64_t(228+2*lag))
                    {
                        if(native)Assert::IsFalse(anchor.submittedEnvelopeAvailable);
                        else AssertBounds(smaller,anchor.submittedEnvelope);
                    }
                }
            }
            // A queued UNKNOWN cannot borrow another source's smaller/native
            // presentation as evidence that its own outer pixels disappeared.
            auto anchor=EnvelopeAnchor(larger);
            auto pending=PrepareEnvelope(anchor,231);
            anchor=SubmitEnvelope(anchor,FreshEnvelope(229,smaller));
            AssertBounds(larger,SelectEnvelope(anchor,pending).presentation.sourceBounds);
            anchor=SubmitEnvelope(anchor,FreshEnvelope(230,IncidentScope(),DecisionOwner::TRUSTED_CROP));
            AssertBounds(larger,SelectEnvelope(anchor,pending).presentation.sourceBounds);
        }

        TEST_METHOD(RetainedEnvelopeSelectionRequiresExactCopyContextAndContainingActualSubmission)
        {
            const auto larger=VisibleBounds(66,2132),smaller=VisibleBounds(148,2048);
            const auto original=EnvelopeAnchor(smaller);
            const auto prepared=PrepareEnvelope(original,230);
            const auto latest=SubmitEnvelope(original,FreshEnvelope(229,larger));
            for(int fault=0;fault<21;++fault)
            {
                auto retained=prepared.retained;auto selection=EnvelopeSelection(latest,prepared);
                switch(fault)
                {
                case 1:retained.available=false;break;
                case 2:++retained.sourceGeneration;break;
                case 3:--retained.sourceSequence;break;
                case 4:++retained.presentationEpoch;break;
                case 5:retained.nativeCrop.top-=2;break;
                case 6:selection.submittedBounds.top-=2;break;
                case 7:selection.previous.submittedEnvelopeSourceSequence=retained.envelopeSubmittedSequence;break;
                case 8:++selection.previous.sourceGeneration;break;
                case 9:selection.selectedOwner=DecisionOwner::TRUSTED_CROP;break;
                case 10:++selection.previous.lastSubmittedSourceSequence;break;
                case 11:selection.previous.submittedEnvelope=VisibleBounds(160,2000);break;
                case 12:selection.previous.submittedEnvelope=VisibleBounds(60,2000);break;
                case 13:selection.selectedAdmission.optionalFillApplied=true;break;
                case 14:selection.fullRasterAuthority=true;break;
                case 15:selection.nativeCropAvailable=false;break;
                case 16:selection.previous.available=false;break;
                case 17:selection.previous.submittedEnvelopeSourceSequence=231;break;
                case 18:selection.previous.submittedEnvelope.left=2;break;
                case 19:++selection.previous.lastSubmittedPresentationSourceSequence;break;
                case 20:selection.previous.submittedOptionalFillAvailable=true;break;
                }
                const auto result=RevalidateRetainedSubmittedEnvelopeSelection(retained,selection);
                Assert::AreEqual(fault==0,result.expanded);
                AssertBounds(fault==0?larger:selection.submittedBounds,result.sourceBounds);
            }
            const auto selected=SelectEnvelope(latest,prepared);
            Assert::AreEqual(prepared.sequence,selected.sequence);
            const auto failed=SubmitEnvelope(latest,selected,false);
            Assert::AreEqual(latest.submittedEnvelopeSourceSequence,failed.submittedEnvelopeSourceSequence);
            AssertBounds(latest.submittedEnvelope,failed.submittedEnvelope);
            const auto submitted=SubmitEnvelope(latest,selected);
            AssertBounds(larger,submitted.submittedEnvelope);
            const auto repeated=SelectEnvelope(submitted,prepared);
            AssertBounds(larger,repeated.presentation.sourceBounds);
            Assert::AreEqual(uint64_t(226),SubmitEnvelope(submitted,repeated).lastSubmittedSourceSequence);
        }

        TEST_METHOD(CurrentSamplingExtentDoesNotCreateAStoredEnvelopeCopyReceipt)
        {
            auto input=UnknownIncident();
            input.crop.currentVisibleBoundsAvailable=false;input.excludedBandsPixelSafe=true;
            input.observation=VisibleBounds(274,1884);
            const auto sampled=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsTrue(sampled.retained && sampled.currentPixelExtent && sampled.expanded);
            Assert::IsFalse(sampled.retainedEnvelope.available);
            auto owned=UnknownIncident();const auto bounds=VisibleBounds(148,2048);
            SetFitCandidate(owned,bounds);owned.confirmedFitOwnership=QualifyConfirmedFitOwnership(CaptionOwner(owned,bounds,bounds));
            Assert::IsFalse(ResolveUncertainPicturePresentation(owned,owned.candidate).retainedEnvelope.available);
        }

        TEST_METHOD(LoggedPartialAndFullExtentsCannotOriginateAnUnknownFallbackEnvelope)
        {
            // 9425 exposed top=0..1992; 9426 and 9455 exposed the whole raster.
            // In all three cases the same native Scope remained established.
            for(bool nearBlack:{false,true})
            for(bool pendingFit:{false,true})
            for(int extent:{0,1,2})
            for(int evidence:{0,1,2})
            {
                auto input=UnknownIncident(extent==2?9455:9425+extent,nearBlack);
                input.crop.currentVisibleBounds=extent==0?VisibleBounds(0,1992):
                    extent==1?VisibleBounds(0,2160):VisibleBounds(0,2160);
                input.crop.verticalFitConfirmationPending=pendingFit;
                if(evidence==1)
                {
                    input.crop.latestObservationIsUnavailable=true;
                    input.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                    input.observationAvailable=false;
                    input.observationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                if(evidence==2)
                {
                    --input.crop.currentVisibleSourceSequence;
                    input.measurementCurrent=false;
                }
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop,
                    L"Unknown or stale visible pixels cannot turn an established native Scope into full raster.");
                AssertBounds(IncidentScope(),output.sourceBounds);
                Assert::IsFalse(output.outwardExpanded,
                    L"A partial near-full envelope is also an AR jump, even when it is not exactly full raster.");
                const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,output,input.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                AssertBounds(IncidentScope(),admitted.presentation.sourceBounds);
                AssertBounds(IncidentScope(),admitted.state.trustedCrop);
                Assert::IsFalse(admitted.state.outwardPresentationAvailable,
                    L"Unconfirmed visible extent may not become a persistent outward FIT owner.");
            }
        }

        TEST_METHOD(UnknownFrameCanRetainAnOwnedSubmittedEnvelopeWithoutGrowingIt)
        {
            for(bool nearBlack:{false,true})
            {
                auto input=UnknownIncident(9455,nearBlack);
                const auto owned=VisibleBounds(278,1992);
                auto& anchor=input.submittedScopeAnchor;
                anchor.lastSubmittedSourceSequence=9453;
                anchor.lastSubmittedPresentationSourceSequence=9454;
                anchor.submittedEnvelopeAvailable=true;anchor.submittedEnvelope=owned;
                anchor.submittedEnvelopeSourceSequence=9454;
                input.previousAdmission.outwardPresentationAvailable=true;
                input.previousAdmission.outwardPresentation=owned;
                input.previousAdmission.presentationSourceSequence=9454;
                // A new raw full extent must not grow the existing owned envelope.
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsTrue(output.retained && output.expanded && output.presentation.applyCrop);
                AssertBounds(owned,output.presentation.sourceBounds);
                AssertBounds(owned,ResolveIncident(input).sourceBounds);
                AssertBounds(IncidentScope(),output.nativeCrop);
                Assert::IsFalse(output.currentPixelExtent,
                    L"Retention of an already submitted envelope is not a new current-pixel expansion certificate.");
                Assert::AreEqual(uint64_t(9453),anchor.lastSubmittedSourceSequence,
                    L"Presentation retention cannot renew the native AR acquisition watermark.");
                Assert::AreEqual(uint64_t(9454),anchor.submittedEnvelopeSourceSequence);
            }
        }

        TEST_METHOD(PendingFitOrAConfirmationFlagCannotPromoteADifferentUnownedCandidate)
        {
            auto input=UnknownIncident();
            input.crop.verticalFitConfirmationPending=true;
            input.confirmedPresentationResolved=true;
            input.candidate.applyCrop=true;input.candidate.outwardExpanded=true;
            input.candidate.owner=DecisionOwner::OUTWARD_FIT;
            input.candidate.sourceBounds=VisibleBounds(200,1996);
            Decision different;
            different.sourceBounds=VisibleBounds(0,2160);
            different.reason="the actually admitted candidate has no matching confirmed owner";
            const auto output=ResolveUncertainPicturePresentation(input,different);
            Assert::IsTrue(output.retained && output.presentation.applyCrop);
            AssertBounds(IncidentScope(),output.presentation.sourceBounds);
        }

        TEST_METHOD(UnconfirmedVerticalFitCannotBypassScopeRetentionViaTheCroppedFastPath)
        {
            for(int extent:{0,1,2,3})
            {
                auto input=UnknownIncident();
                const auto proposed=extent==0?VisibleBounds(60,2160):
                    extent==1?VisibleBounds(0,1992):
                    extent==2?VisibleBounds(200,1996):VisibleBounds(0,2160);
                input.crop.outwardPresentationActive=input.crop.outwardExpansionAvailable=true;
                input.crop.outwardExpansion=proposed;
                input.crop.outwardExpansionSourceGeneration=input.crop.frameSourceGeneration;
                input.candidate=Evaluate(input.crop);
                Assert::IsTrue(input.candidate.applyCrop && input.candidate.owner==DecisionOwner::OUTWARD_FIT);
                Assert::IsFalse(input.confirmedPresentationResolved);
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsTrue(output.retained && output.presentation.applyCrop);
                AssertBounds(IncidentScope(),output.presentation.sourceBounds);
                Assert::IsFalse(output.presentation.outwardExpanded,
                    L"An unconfirmed label and a valid partial crop cannot promote unknown bar occupancy into a new vertical picture shape.");
            }
        }

        TEST_METHOD(CurrentPixelBoundedSideFitKeepsItsIndependentHorizontalOwner)
        {
            auto input=UnknownIncident();
            const ActivePictureBounds window{100,278,3740,1880,3840,2160,3640.0/1602.0,
                ActivePictureBounds::BarAxes::BOTH};
            auto& crop=input.crop;
            crop.geometry=window;crop.currentVisibleBase=window;
            crop.currentVisibleBounds=VisibleBounds(278,1880);
            crop.barCropRefinementHorizontalConflict=true;
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
            crop.outwardExpansion=crop.currentVisibleBounds;
            crop.outwardExpansionSourceGeneration=crop.frameSourceGeneration;
            input.retentionBounds=window;input.previousAdmission.trustedCrop=window;
            input.submittedScopeAnchor.crop=window;
            input.candidate=Evaluate(crop);
            Assert::IsTrue(input.candidate.applyCrop && input.candidate.horizontalExpansionPixelBounded,
                L"The positive fixture uses the existing current-source horizontal pixel certificate.");
            Assert::IsTrue(input.candidate.owner==DecisionOwner::OUTWARD_FIT);
            Assert::IsFalse(input.confirmedPresentationResolved);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            Assert::IsTrue(output.presentation.owner==DecisionOwner::OUTWARD_FIT);
            AssertBounds(crop.currentVisibleBounds,output.presentation.sourceBounds);
        }
        TEST_METHOD(CurrentConfirmedCaptionFitKeepsItsActualPartialOrFullPresentation)
        {
            for(bool nearBlack:{false,true})
            for(bool full:{false,true})
            {
                auto input=UnknownIncident(9455,nearBlack);
                input.crop.outwardPresentationActive=input.crop.outwardExpansionAvailable=true;
                input.crop.outwardExpansion=full?VisibleBounds(0,2160):VisibleBounds(200,1996);
                input.crop.outwardExpansionSourceGeneration=input.crop.frameSourceGeneration;
                // The confirmed dense caption owner is an independent accepted
                // presentation. It is not inferred from currentVisibleBounds.
                auto evaluated=input.crop;
                evaluated.nearBlackEpisodeRetainCrop=false;
                input.candidate=Evaluate(evaluated);
                Assert::IsTrue(input.candidate.applyCrop);
                Assert::IsTrue(input.candidate.owner==DecisionOwner::OUTWARD_FIT);
                AssertBounds(input.crop.outwardExpansion,input.candidate.sourceBounds);
                input.confirmedPresentationResolved=true;
                // The fixture explicitly originates from accepted current caption
                // backing, independently of the provisional visible scan.
                const auto owner=CaptionOwner(input,input.crop.outwardExpansion,
                    input.crop.outwardExpansion);
                input.confirmedFitOwnership=QualifyConfirmedFitOwnership(owner);
                Assert::IsTrue(input.confirmedFitOwnership.available);
                const auto output=ResolveIncident(input);
                AssertBounds(input.candidate.sourceBounds,output.sourceBounds);
                Assert::IsTrue(output.owner==DecisionOwner::OUTWARD_FIT);
                const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,output,input.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                AssertBounds(input.candidate.sourceBounds,admitted.presentation.sourceBounds);
            }
        }

        TEST_METHOD(DenseFitResolutionWithoutAcceptedCaptionOrUiOwnershipCannotFlashFullRaster)
        {
            for(bool nearBlack:{false,true})for(bool full:{false,true})
            {
                auto input=UnknownIncident(205,nearBlack);
                SetFitCandidate(input,full?VisibleBounds(0,2160):VisibleBounds(180,2032));
                input.confirmedPresentationResolved=true; // Dense FIT reached 2/2.
                Assert::IsFalse(input.confirmedFitOwnership.available);
                AssertBounds(IncidentScope(),ResolveIncident(input).sourceBounds);
            }
        }
        TEST_METHOD(ConfirmedFitCapsuleRequiresIndependentCurrentBoundedEvidence)
        {
            auto input=UnknownIncident(205);
            const auto approved=VisibleBounds(278,2032);
            const ActivePictureBounds caption{1200,1900,2640,2024,3840,2160};
            const auto valid=CaptionOwner(input,approved,caption);
            Assert::IsTrue(QualifyConfirmedFitOwnership(valid).available);
            for(int fault=0;fault<13;++fault)
            {
                auto owner=valid;
                if(fault==0)owner.current=false;
                if(fault==1)owner.confirmed=false;
                if(fault==2)owner.origin=ConfirmedFitOwnershipOrigin::NONE;
                if(fault==3)owner.sourceGeneration=0;
                if(fault==4)owner.sourceSequence=0;
                if(fault==5)owner.nativeBase.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
                if(fault==6)owner.approvedBounds=owner.nativeBase;
                if(fault==7)owner.approvedBounds.top=280;
                if(fault==8)owner.approvedBounds.bottom=2031;
                if(fault==9)owner.evidenceBounds.bottom=2034;
                if(fault==10)owner.evidenceBounds.left=-2;
                if(fault==11)owner.approvedBounds.right=3838;
                if(fault==12)owner.evidenceBounds={1200,1800,2640,1870,3840,2160};
                Assert::IsFalse(QualifyConfirmedFitOwnership(owner).available,
                    L"Unknown, unconfirmed, unbounded or unpaired input cannot originate FIT ownership.");
            }
        }
        TEST_METHOD(StaleOrOversizedFitCannotBorrowAQualifiedCurrentCaptionCapsule)
        {
            for(bool nearBlack:{false,true})for(int fault=0;fault<12;++fault)
            {
                auto input=UnknownIncident(205,nearBlack);
                // The unavailable/stale native-contract cases isolate the
                // full-envelope exemption; partial non-full fast paths retain
                // their separate existing geometry contract.
                const auto approved=fault>=10?VisibleBounds(0,2160):VisibleBounds(278,2032);
                const ActivePictureBounds caption{1200,1900,2640,2024,3840,2160};
                SetFitCandidate(input,approved);
                input.confirmedPresentationResolved=true;
                input.confirmedFitOwnership=QualifyConfirmedFitOwnership(CaptionOwner(input,approved,caption));
                Assert::IsTrue(input.confirmedFitOwnership.available);
                if(fault==0)--input.confirmedFitOwnership.sourceSequence;
                if(fault==1)++input.confirmedFitOwnership.sourceGeneration;
                if(fault==2)++input.confirmedFitOwnership.presentationEpoch;
                if(fault==3)input.confirmedFitOwnership.nativeBase.top-=2;
                if(fault==4)input.confirmedFitOwnership.nativeBase.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
                if(fault==5)input.confirmedFitOwnership.approvedBounds.bottom+=2;
                if(fault==6)input.candidate.sourceBounds=VisibleBounds(0,2160);
                if(fault==7)input.confirmedFitOwnership.origin=ConfirmedFitOwnershipOrigin::NONE;
                if(fault==8)++input.crop.framePresentationEpoch;
                if(fault==9)input.confirmedFitOwnership.available=false;
                if(fault==10)input.crop.sharedGeometryAvailable=false;
                if(fault==11)--input.crop.geometrySourceGeneration;
                AssertBounds(IncidentScope(),ResolveIncident(input).sourceBounds);
            }
            auto input=UnknownIncident(205);
            const auto approved=VisibleBounds(278,2032);
            SetFitCandidate(input,approved);
            input.confirmedFitOwnership=QualifyConfirmedFitOwnership(CaptionOwner(input,approved,approved));
            auto different=input.candidate;different.sourceBounds=VisibleBounds(0,2160);
            AssertBounds(IncidentScope(),ResolveUncertainPicturePresentation(input,different).presentation.sourceBounds);
        }
        TEST_METHOD(MixedUiAndPictureEdgesCannotMasqueradeAsConfirmedOverlayOwnership)
        {
            auto input=UnknownIncident(205);
            const auto approved=VisibleBounds(180,2032);
            auto owner=CaptionOwner(input,approved,VisibleBounds(184,2026));
            owner.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
            owner.upperContent=owner.lowerContent=true;
            owner.upperOverlayLike=true;owner.lowerOverlayLike=false; // Logged 205.
            Assert::IsFalse(QualifyConfirmedFitOwnership(owner).available);
            owner.lowerOverlayLike=true;
            Assert::IsTrue(QualifyConfirmedFitOwnership(owner).available,
                L"Current UI/icon evidence is valid without requiring subtitle glyphs.");
            for(int fault=0;fault<5;++fault)
            {
                auto invalid=owner;
                if(fault==0)invalid.upperContent=false;
                if(fault==1)invalid.lowerContent=false;
                if(fault==2)invalid.upperOverlayLike=false;
                if(fault==3)invalid.evidenceBounds.top=278;
                if(fault==4)invalid.evidenceBounds.bottom=1880;
                Assert::IsFalse(QualifyConfirmedFitOwnership(invalid).available);
            }
        }
        TEST_METHOD(ExactCurrentAcceptedCaptionDoesNotWaitForTheCoarseArMeasurementStride)
        {
            for(bool nearBlack:{false,true})for(bool skipped:{false,true})
            {
                auto input=UnknownIncident(205,nearBlack);
                const auto approved=VisibleBounds(278,2032);
                const ActivePictureBounds multilineCaption{1100,1870,2740,2024,3840,2160};
                SetFitCandidate(input,approved);
                input.confirmedPresentationResolved=true;
                input.confirmedFitOwnership=QualifyConfirmedFitOwnership(
                    CaptionOwner(input,approved,multilineCaption));
                Assert::IsTrue(input.confirmedFitOwnership.available);
                if(skipped)
                {
                    input.measurementCurrent=input.retentionEvaluated=input.observationAvailable=false;
                    input.retentionSourceSequence=0;
                    input.crop.latestObservationIsUnavailable=true;
                    input.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                const auto result=ResolveIncident(input);
                AssertBounds(approved,result.sourceBounds);
                Assert::IsTrue(result.owner==DecisionOwner::OUTWARD_FIT,
                    L"An independently current accepted caption must not be delayed by the AR analysis cadence.");
            }
        }
        TEST_METHOD(CaptionOwnedPaddingPermitsSmallerLegacyFitWithoutDelayingAcceptedCue)
        {
            for(int bottom:{2024,2032,2056})
            {
                auto input=UnknownIncident(205);
                const ActivePictureBounds caption{1200,1900,2640,2024,3840,2160};
                const auto approved=VisibleBounds(278,bottom);
                SetFitCandidate(input,approved);
                input.confirmedFitOwnership=QualifyCaptionFitWithinPadding(
                    CaptionOwner(input,approved,caption),32,32);
                Assert::IsTrue(input.confirmedFitOwnership.available,
                    L"A current accepted cue may keep the existing smaller dense clearance inside its independently bounded padding.");
                input.measurementCurrent=input.retentionEvaluated=input.observationAvailable=false;
                const auto resolved=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(resolved.retained);
                AssertBounds(approved,resolved.presentation.sourceBounds);
                Assert::IsTrue(resolved.presentation.owner==DecisionOwner::OUTWARD_FIT);
            }
        }
        TEST_METHOD(CaptionOwnedPaddingCannotCertifyUnrelatedDenseOrDetectorExtent)
        {
            for(int fault=0;fault<8;++fault)
            {
                auto input=UnknownIncident(205);
                const ActivePictureBounds caption{1200,1900,2640,2024,3840,2160};
                auto approved=VisibleBounds(278,2032);
                if(fault==0)approved=VisibleBounds(180,2032); // Unrelated opposite bar.
                if(fault==1)approved=VisibleBounds(278,2058); // Beyond cue-owned padding.
                if(fault==2)approved=VisibleBounds(0,2160); // Detector/dense full raster.
                SetFitCandidate(input,approved);
                auto owner=CaptionOwner(input,approved,caption);
                if(fault==3)owner.evidenceBounds={1200,1800,2640,1870,3840,2160};
                if(fault==4)owner.current=false;
                if(fault==5)owner.confirmed=false;
                if(fault==6)owner.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
                input.confirmedFitOwnership=QualifyCaptionFitWithinPadding(owner,32,fault==7?-1:32);
                Assert::IsFalse(input.confirmedFitOwnership.available,
                    L"The presence of a caption cannot authorize independent raw/dense geometry outside that caption's own padded area.");
                input.confirmedPresentationResolved=true;
                AssertBounds(IncidentScope(),ResolveIncident(input).sourceBounds);
            }
        }
        TEST_METHOD(OffMeasuredBarContentReachesSubmittedScopeFitWithoutGlyphOrUiShape)
        {
            for(bool nearBlack:{false,true})for(bool top:{false,true})
            {
                auto input=UnknownIncident(200,nearBlack);
                auto native=IncidentScope();native.top=384;native.bottom=1776;
                native.aspectRatio=3840.0/1392.0;
                input.crop.geometry=input.crop.verticalTranslationBase=native;
                input.crop.currentVisibleBase=input.retentionBounds=native;
                input.previousAdmission.trustedCrop=input.submittedScopeAnchor.crop=native;
                const auto occupied=VisibleBounds(top?92:384,top?1776:2068);
                PresentationEnvelopeGeometryInput envelope;envelope.trustedPicture=native;
                envelope.observedContent=occupied;envelope.observedContentAvailable=true;
                envelope.expandTop=top;envelope.expandBottom=!top;envelope.verticalPadding=24;
                const auto approved=BuildPresentationEnvelope(envelope);
                Assert::IsTrue(approved.valid && approved.expanded);
                Assert::IsTrue(approved.bounds.top>0 && approved.bounds.bottom<2160);
                SetFitCandidate(input,approved.bounds);
                input.crop.currentVisibleBounds=approved.bounds;
                auto proof=CaptionOwner(input,approved.bounds,occupied);
                proof.origin=ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT;
                proof.upperContent=top;proof.lowerContent=!top;proof.verticalPadding=24;
                // Both shape flags remain false, exactly as the failed 190-row trace.
                input.confirmedFitOwnership=QualifyConfirmedFitOwnership(proof);
                Assert::IsTrue(input.confirmedFitOwnership.available);
                const auto resolved=ResolveUncertainPicturePresentation(input,input.candidate);
                AssertBounds(approved.bounds,resolved.presentation.sourceBounds);
                const auto fill=EvaluateFinalAspectLimitFill(ScreenFill(input,resolved));
                Assert::IsFalse(fill.applied,L"Aspect-limit fill must not crop measured bar content");
                auto admission=input.previousAdmission;
                const auto anchor=Submit(input,resolved.presentation,admission);
                Assert::IsTrue(anchor.submittedEnvelopeAvailable);
                AssertBounds(approved.bounds,anchor.submittedEnvelope);
                for(int fault=0;fault<7;++fault)
                {
                    auto invalid=proof;
                    if(fault==0)invalid.current=false;
                    if(fault==1)invalid.confirmed=false;
                    if(fault==2)invalid.sourceSequence=0;
                    if(fault==3)invalid.nativeBase.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
                    if(fault==4)invalid.upperContent=invalid.lowerContent=false;
                    if(fault==5)invalid.approvedBounds=VisibleBounds(0,2160);
                    if(fault==6)invalid.evidenceBounds=VisibleBounds(384,1776);
                    Assert::IsFalse(QualifyConfirmedFitOwnership(invalid).available);
                }
            }
        }
        TEST_METHOD(SelectedUiFitSurvivesHeldSkippedAndRepeatedAnalysisWithoutGrowing)
        {
            auto input=UnknownIncident(200);
            const auto approved=VisibleBounds(180,2032);
            SetFitCandidate(input,approved);
            input.crop.currentVisibleBounds=approved;
            auto owner=CaptionOwner(input,approved,VisibleBounds(184,2026));
            owner.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
            owner.upperContent=owner.lowerContent=owner.upperOverlayLike=owner.lowerOverlayLike=true;
            input.confirmedFitOwnership=QualifyConfirmedFitOwnership(owner);
            Assert::IsTrue(input.confirmedFitOwnership.available);
            auto admission=input.previousAdmission;
            auto anchor=Submit(input,ResolveIncident(input),admission);
            Assert::IsTrue(anchor.submittedEnvelopeAvailable);
            AssertBounds(approved,anchor.submittedEnvelope);
            Assert::AreEqual(uint64_t(199),anchor.lastSubmittedSourceSequence);
            for(uint64_t sequence:{201,202,202,203,204})
            {
                auto held=UnknownIncident(sequence,sequence%2==0);
                held.previousAdmission=admission;held.submittedScopeAnchor=anchor;
                held.confirmedPresentationResolved=true; // Held cold/UI result, not fresh pixels.
                held.measurementCurrent=held.retentionEvaluated=held.observationAvailable=false;
                held.crop.latestObservationIsUnavailable=true;
                held.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                SetFitCandidate(held,VisibleBounds(0,2160)); // New raw occupancy must not grow it.
                Assert::IsFalse(held.confirmedFitOwnership.available);
                const auto result=ResolveIncident(held);
                AssertBounds(approved,result.sourceBounds);
                anchor=Submit(held,result,admission);
                AssertBounds(approved,anchor.submittedEnvelope);
                Assert::AreEqual(uint64_t(199),anchor.lastSubmittedSourceSequence,
                    L"Held/repeated UI presentation may not renew native AR authority.");
                Assert::AreEqual(sequence,anchor.lastSubmittedPresentationSourceSequence);
            }
        }
        TEST_METHOD(Startup205Through210CannotAlternateDenseFullFitAndSubmittedNativeFill)
        {
            auto seed=UnknownIncident(204);
            MakeNativeCurrent(seed,IncidentScope());
            seed.crop.frameLocalPresentationRetentionSafe=seed.excludedBandsPixelSafe=true;
            seed.crop.currentVisibleBoundsAvailable=false;
            seed.previousAdmission={};seed.submittedScopeAnchor={};
            seed.candidate=Evaluate(seed.crop);
            UncertainPicturePresentationDecision native;native.presentation=seed.candidate;
            const auto firstFill=EvaluateFinalAspectLimitFill(ScreenFill(seed,native));
            Assert::IsTrue(firstFill.applied && !firstFill.retainedSubmittedFill);
            CropPresentationAdmissionState admission;
            auto anchor=Submit(seed,seed.candidate,admission,&firstFill);
            Assert::IsTrue(anchor.available && anchor.submittedOptionalFillAvailable);
            Assert::AreEqual(uint64_t(204),anchor.lastSubmittedSourceSequence);
            for(uint64_t sequence=205;sequence<=210;++sequence)
            {
                auto frame=UnknownIncident(sequence);
                frame.previousAdmission=admission;frame.submittedScopeAnchor=anchor;
                if(sequence%2)
                {
                    SetFitCandidate(frame,VisibleBounds(0,2160));
                    frame.confirmedPresentationResolved=true; // Dense 2/2, no accepted cue.
                }
                else
                {
                    frame.measurementCurrent=frame.retentionEvaluated=frame.observationAvailable=false;
                    frame.crop.latestObservationIsUnavailable=true;
                    frame.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                const auto resolved=ResolveUncertainPicturePresentation(frame,frame.candidate);
                Assert::IsTrue(resolved.retained && !resolved.expanded);
                AssertBounds(IncidentScope(),resolved.presentation.sourceBounds);
                const auto fill=EvaluateFinalAspectLimitFill(ScreenFill(frame,resolved));
                Assert::IsTrue(fill.applied && fill.retainedSubmittedFill);
                AssertBounds(firstFill.sourceBounds,fill.sourceBounds);
                anchor=Submit(frame,resolved.presentation,admission,&fill);
                Assert::IsFalse(anchor.submittedEnvelopeAvailable,
                    L"A raw dense FIT must never contaminate selected output continuity with a full envelope.");
                Assert::AreEqual(uint64_t(204),anchor.lastSubmittedSourceSequence);
                Assert::AreEqual(sequence,anchor.lastSubmittedPresentationSourceSequence);
                Assert::AreEqual(sequence,anchor.submittedOptionalFillSourceSequence);
            }
        }
        TEST_METHOD(CurrentConfirmedTranslationKeepsItsDimensionsAndDisplacement)
        {
            auto input=UnknownIncident();
            input.crop.verticalTranslationActive=true;
            input.crop.verticalTranslationPixels=112;
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.applyCrop && input.candidate.verticallyTranslated);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
            input.confirmedPresentationResolved=true;
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
            Assert::AreEqual(112,output.presentation.verticalTranslationPixels);
        }

        TEST_METHOD(LegacyTranslationFlagAloneCannotAuthorizeAnUnknownNearBlackEnvelope)
        {
            // Defensive pure-policy contract: the renderer normally suppresses
            // this flag during retained near-black episodes. The flag itself
            // must nevertheless never count as an actually accepted owner.
            for(int fault:{0,1,2,3,4})
            {
                auto input=UnknownIncident(9425,true);
                input.crop.currentVisibleBounds=VisibleBounds(0,1992);
                input.crop.verticalTranslationActive=true;
                input.crop.verticalTranslationPixels=112;
                if(fault!=0)
                {
                    input.candidate=Evaluate(input.crop);
                    Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
                    if(fault==1)input.candidate.sourceBounds=VisibleBounds(0,1992);
                    if(fault==2)++input.crop.verticalTranslationSourceGeneration;
                    if(fault==3)input.crop.verticalTranslationBase.top-=2;
                    if(fault==4)++input.candidate.sourceBounds.top;
                }
                Assert::IsFalse(input.confirmedPresentationResolved);
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop);
                AssertBounds(IncidentScope(),output.sourceBounds);
                Assert::IsFalse(output.outwardExpanded || output.verticallyTranslated);
            }
        }

        TEST_METHOD(ActuallyAcceptedTranslationSurvivesNearBlackMeasurementGaps)
        {
            for(bool missingMeasurement:{false,true})
            {
                auto input=UnknownIncident(9455,true);
                input.crop.verticalTranslationActive=true;
                input.crop.verticalTranslationPixels=112;
                input.candidate=Evaluate(input.crop);
                Assert::IsTrue(input.candidate.applyCrop && input.candidate.verticallyTranslated);
                Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
                // No new dense confirmation: the actual same-base translation
                // already owns this source presentation through its hold.
                Assert::IsFalse(input.confirmedPresentationResolved);
                if(missingMeasurement)
                {
                    input.measurementCurrent=input.retentionEvaluated=false;
                    input.observationAvailable=false;
                    input.crop.latestObservationIsUnavailable=true;
                    input.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                const auto output=ResolveIncident(input);
                AssertBounds(input.candidate.sourceBounds,output.sourceBounds);
                Assert::IsTrue(output.owner==DecisionOwner::VERTICAL_TRANSLATION);
                Assert::IsTrue(output.applyCrop && output.verticallyTranslated);
                Assert::AreEqual(112,output.verticalTranslationPixels);
            }
        }
        TEST_METHOD(RealFullRasterAuthorityAndPublishedNativeCropStillApplyImmediately)
        {
            for(bool committed:{false,true})
            {
                auto input=UnknownIncident();
                if(committed)
                {
                    input.crop.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
                    input.crop.geometry=VisibleBounds(0,2160);
                }
                else input.crop.fullRasterPresentationAuthoritative=true;
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(VisibleBounds(0,2160),output.presentation.sourceBounds);
                Assert::IsFalse(output.presentation.applyCrop);
            }
            auto input=UnknownIncident();
            const ActivePictureBounds expanded{0,96,3840,2064,3840,2160,3840.0/1968.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            MakeNativeCurrent(input,expanded);
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::TRUSTED_CROP);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            AssertBounds(expanded,output.presentation.sourceBounds);
            const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,
                output.presentation,input.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            AssertBounds(expanded,admitted.presentation.sourceBounds);
        }

        TEST_METHOD(CurrentPictureHandoffAndMovingHoldAreNotConvertedToFallbackOwners)
        {
            for(bool moving:{false,true})
            {
                auto input=UnknownIncident();
                auto& crop=input.crop;
                if(moving)
                {
                    crop.movingPictureTransition=true;
                    crop.movingPictureHold.base=crop.geometry;
                    crop.movingPictureHold.sourceGeneration=crop.frameSourceGeneration;
                    crop.movingPictureHold.sourceSequence=crop.frameSourceSequence;
                    crop.movingPictureHold.presentationEpoch=input.presentationEpoch;
                }
                else
                {
                    crop.latestObservationIsProvisional=false;
                    crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.pictureTransitionHandoff.active=true;
                    crop.pictureTransitionHandoff.trustedBase=crop.geometry;
                    crop.pictureTransitionHandoff.sourceGeneration=crop.frameSourceGeneration;
                    crop.pictureTransitionHandoff.sourceSequence=crop.frameSourceSequence;
                    crop.pictureTransitionHandoff.presentationEpoch=input.presentationEpoch;
                }
                input.candidate=Evaluate(crop);
                Assert::IsTrue(input.candidate.applyCrop);
                Assert::IsTrue(input.candidate.owner==(moving?DecisionOwner::MOVING_PICTURE_HOLD:
                    DecisionOwner::PICTURE_CONFIRMATION));
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
                Assert::IsTrue(output.presentation.owner==input.candidate.owner);
            }
        }

        TEST_METHOD(IndependentOneScanStepSamplingGuardStillPreservesMeasuredBoundaryRows)
        {
            auto input=UnknownIncident();
            input.crop.currentVisibleBoundsAvailable=false;
            input.excludedBandsPixelSafe=true;
            input.observation=VisibleBounds(274,1884);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsTrue(output.retained && output.currentPixelExtent && output.expanded);
            AssertBounds(VisibleBounds(274,1884),output.presentation.sourceBounds);
            AssertBounds(IncidentScope(),output.nativeCrop);
        }

        TEST_METHOD(NearBlackPipelinePreservesOnlyExactCurrentIndependentTinySamplingGuard)
        {
            for(int fault:{0,1,2,3,4,5,6})
            {
                auto input=UnknownIncident(9426,true);
                input.crop.currentVisibleBoundsAvailable=false;
                input.excludedBandsPixelSafe=true;
                input.observation=VisibleBounds(274,1884);
                // This exception is independent exact-source strip proof, not
                // a generic visible extent or a confirmed caption/FIT owner.
                Assert::IsFalse(input.confirmedPresentationResolved);
                if(fault==1)input.measurementCurrent=false;
                if(fault==2)--input.retentionSourceSequence;
                if(fault==3)++input.retentionSourceGeneration;
                if(fault==4)input.retentionBounds.top-=2;
                if(fault==5)input.excludedBandsPixelSafe=false;
                if(fault==6)input.observation.top=270;
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop);
                AssertBounds(fault==0?VisibleBounds(274,1884):IncidentScope(),output.sourceBounds);
                if(fault!=0)Assert::IsFalse(output.outwardExpanded,
                    L"A stale, mismatched, unsafe or larger proposal cannot borrow the independent one-step sampling exception.");
            }
        }
        TEST_METHOD(NewCropAcquisitionStillRejectsUnownedCurrentVisiblePixels)
        {
            for(bool confirmedNative:{false,true})
            {
                auto input=UnknownIncident();
                input.previousAdmission={};input.submittedScopeAnchor={};
                input.crop.latestObservationSupportsCrop=confirmedNative;
                input.candidate={};input.candidate.applyCrop=true;
                input.candidate.sourceBounds=IncidentScope();
                input.candidate.owner=confirmedNative?DecisionOwner::TRUSTED_CROP:
                    DecisionOwner::NEAR_BLACK_EPISODE;
                const auto retained=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(retained.retained);
                const auto admitted=AdmitCropPresentation({},input.crop,
                    retained.presentation,input.presentationEpoch);
                Assert::IsTrue(admitted.blocked,
                    L"The continuity fix applies only to an actually submitted native crop; visible pixels still veto new acquisition.");
                Assert::IsFalse(admitted.presentation.applyCrop);
                AssertBounds(VisibleBounds(0,2160),admitted.presentation.sourceBounds);
            }
        }

        TEST_METHOD(SourceViewportAndOrderingChangesCannotBorrowASubmittedScopeOrEnvelope)
        {
            for(int changed:{0,1,2})
            {
                auto input=UnknownIncident();
                input.submittedScopeAnchor.submittedEnvelopeAvailable=true;
                input.submittedScopeAnchor.submittedEnvelope=VisibleBounds(278,1992);
                input.submittedScopeAnchor.submittedEnvelopeSourceSequence=input.crop.frameSourceSequence-1;
                if(changed==0)++input.crop.frameSourceGeneration;
                if(changed==1)++input.presentationEpoch;
                if(changed==2)input.submittedScopeAnchor.lastSubmittedPresentationSourceSequence=
                    input.crop.frameSourceSequence+1;
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
            }
        }
    };
}