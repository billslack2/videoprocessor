#include <iostream>
#include <stdexcept>
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace AlphaSourceCrop;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static Input cropInput() {
    Input in;
    in.automaticCropEnabled = true;
    in.sharedGeometryAvailable = true;
    in.latestObservationSupportsCrop = true;
    in.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
    in.geometry = {0,40,3840,2116,3840,2160,3840.0/2076.0,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    in.geometrySourceGeneration = in.frameSourceGeneration = 7;
    in.rasterWidth = 3840;
    in.rasterHeight = 2160;
    return in;
}

static NearBlackPresentationEpisodeInput episodeInput() {
    NearBlackPresentationEpisodeInput in;
    in.measurementCurrent = in.nearBlackEvaluated = true;
    in.globalNearBlack = true;
    in.trustedCropAvailable = true;
    in.trustedCrop = cropInput().geometry;
    in.presentationEpoch = 41;
    in.sourceGeneration = 7;
    in.sourceSequence = 1000;
    in.framesPerSecond = 23.976;
    return in;
}

static NearBlackPresentationEpisodeDecision next(
    NearBlackPresentationEpisodeInput& in,
    const NearBlackPresentationEpisodeDecision& prev) {
    in.previous = prev.state;
    ++in.sourceSequence;
    in.retentionSourceSequence = in.sourceSequence;
    in.reacquiredSourceSequence = in.sourceSequence;
    return EvaluateNearBlackPresentationEpisode(in);
}

static void safeRecovery(NearBlackPresentationEpisodeInput& in) {
    in.globalNearBlack = false;
    in.boundedVisibleContentOutsideCrop = false;
    in.currentObservationAvailable = true;
    in.currentObservation = in.trustedCrop;
    in.retentionEvaluated = in.retentionSafe = true;
    in.retentionBounds = in.trustedCrop;
    in.retentionSourceGeneration = in.sourceGeneration;
    in.knownTrustedGeometryReacquired = true;
    in.reacquiredTrustedGeometry = in.trustedCrop;
    in.reacquiredTrustedClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
    in.reacquiredSourceGeneration = in.sourceGeneration;
    in.reacquiredPresentationEpoch = in.presentationEpoch;
    in.reacquisitionIsCurrentAssociation = true;
}

int main() {
    try {
        auto crop = cropInput();
        crop.latestObservationSupportsCrop = false;
        crop.latestObservationIsProvisional = true;
        crop.frameLocalPresentationRetentionEvaluated = true;
        int transitions = 0;
        bool previous = false;
        for (int frame = 0; frame < 24; ++frame) {
            crop.frameSourceSequence = frame + 1;
            crop.frameLocalPresentationRetentionSafe = (frame % 2 == 0);
            const auto out = Evaluate(crop);
            if (frame && previous != out.applyCrop) ++transitions;
            previous = out.applyCrop;
        }
        require(transitions == 23, "alternating safety did not reproduce");
        std::cout << "alternating frame-local safety: " << transitions
                  << " changes across 24 synthetic frames, no scene cuts\n";
        crop.ambiguityHoldActive = crop.sceneVerificationHoldActive = true;
        crop.frameLocalPresentationRetentionSafe = false;
        require(!Evaluate(crop).applyCrop, "timers unexpectedly masked safety");
        std::cout << "active ambiguity and scene holds: unsafe sample still releases crop\n";

        auto in = episodeInput();
        auto out = EvaluateNearBlackPresentationEpisode(in);
        require(out.state.mode == NearBlackPresentationMode::RETAIN_CROP, "entry");
        in.globalNearBlack = false;
        in.boundedVisibleContentOutsideCrop = true;
        out = next(in, out);
        require(out.changedToFullRaster, "one-frame latch");
        std::cout << "one non-near-black outward sample: full raster immediately\n";
        safeRecovery(in);
        int recovery = 0;
        do { out = next(in, out); ++recovery; } while (!out.ended && recovery < 50);
        require(out.releasedToTrustedCrop && recovery == 7, "exact recovery");
        std::cout << "exact safe entry geometry: restored after " << recovery
                  << " distinct samples at 23.976 fps\n";

        in = episodeInput();
        out = EvaluateNearBlackPresentationEpisode(in);
        in.globalNearBlack = false;
        in.boundedVisibleContentOutsideCrop = true;
        in.retentionEvaluated = true;
        in.retentionBounds = in.trustedCrop;
        in.retentionSourceGeneration = in.sourceGeneration;
        int outward = 0;
        do { out = next(in, out); ++outward; }
        while (!out.state.confirmedNonNearBlackContent && outward < 50);
        require(outward == 10, "outward count");
        safeRecovery(in);
        for (int frame=0; frame<1440; ++frame) {
            out = next(in, out);
            require(out.state.mode == NearBlackPresentationMode::FULL_RASTER,
                "sticky outward mode unexpectedly released");
        }
        std::cout << "10 outward samples followed by 1440 exact safe samples: "
                     "full raster remains latched\n";

        in = episodeInput();
        out = EvaluateNearBlackPresentationEpisode(in);
        in.boundedVisibleContentOutsideCrop = true;
        out = next(in, out);
        safeRecovery(in);
        in.currentObservation.top += 4;
        for (int frame=0; frame<1440; ++frame) {
            out = next(in, out);
            require(out.state.mode == NearBlackPresentationMode::FULL_RASTER,
                "inset observation unexpectedly released");
            require(out.state.revalidationSamples == 0, "inset observation counted");
        }
        std::cout << "one outward sample then 1440 safe samples with top=44 versus saved top=40: "
                     "no recovery samples count\n";

        in.currentObservation = in.trustedCrop;
        for (int frame=0; frame<7; ++frame) out = next(in, out);
        require(out.releasedToTrustedCrop, "exact observation after inset gap");
        std::cout << "same case returns to exact top=40: recovery succeeds in 7 samples\n";

        crop = cropInput();
        for (int frame=0; frame<720; ++frame)
            require(Evaluate(crop).applyCrop, "stable crop control");
        crop.fullRasterPresentationAuthoritative = true;
        require(!Evaluate(crop).applyCrop, "full raster control");
        in = episodeInput();
        out = EvaluateNearBlackPresentationEpisode(in);
        in.fullRasterAuthorityAvailable = true;
        in.globalNearBlack = false;
        out = next(in, out);
        require(out.ended && out.state.mode == NearBlackPresentationMode::INACTIVE,
            "full-raster episode exit control");
        std::cout << "controls: stable bars retained; trusted full raster accepted immediately\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
