#include <iostream>
#include <vprenderer/AlphaSourceCropPolicy.h>
using namespace AlphaSourceCrop;
int main() {
 const ActivePictureBounds scope{188,364,3652,1796,3840,2160,3464.0/1432.0,ActivePictureBounds::BarAxes::BOTH};
 const ActivePictureBounds pillar{188,0,3648,2160,3840,2160,3460.0/2160.0,ActivePictureBounds::BarAxes::LEFT_RIGHT};
 auto contains=pillar; contains.right=3652;
 ActivePicturePresentationRetentionEvidence evidence;
 // No broad opposing picture evidence: only localized bottom-overlay content.
 evidence.analysisValid=evidence.presentationValid=true;
 auto mixed=ConfirmOutwardPictureTransition({},scope,pillar,evidence,2,90015);
 auto exact=ConfirmOutwardPictureTransition({},scope,contains,evidence,2,90015);
 std::cout << "outward guard mixed: transition=" << mixed.outwardTransition << " authoritative=" << mixed.authoritative << '\n';
 std::cout << "outward guard containing: transition=" << exact.outwardTransition << " authoritative=" << exact.authoritative << '\n';
 VerticalBarPresentationState presentation;
 std::cout << "before subtitle translation active: defer=" << ShouldDeferVerticalGeometryTransition(scope,pillar,ActivePictureClassification::BAR_CROP_TRUSTED,presentation,false,2,2) << '\n';
 presentation.action=VerticalBarPresentationAction::TRANSLATE;
 std::cout << "after subtitle translation active: defer=" << ShouldDeferVerticalGeometryTransition(scope,pillar,ActivePictureClassification::BAR_CROP_TRUSTED,presentation,false,2,2) << '\n';
 ActivePictureTransitionModel model;
 ActivePictureTransitionDecision d;
 for (uint64_t i=1;i<=4;++i) d=model.Observe({pillar,i,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
 for (uint64_t i=5;i<=110;++i) d=model.Observe({scope,i,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
 std::cout << "seeded stable top=" << d.stableBounds.top << " bounds.top=" << d.bounds.top << '\n';
 auto provisional=pillar; provisional.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
 for (uint64_t i=111;i<=112;++i) {
  d=model.Observe({provisional,i,true,ActivePictureClassification::PROVISIONAL,24});
  std::cout << "provisional recurrence frame=" << i << " publish=" << d.publish << " reacquired=" << d.knownTrustedGeometryReacquired << " top=" << d.bounds.top << " reason=" << d.reason << '\n';
 }
}
