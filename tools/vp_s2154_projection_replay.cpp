#include "SubtitleBoxLookahead.h"
#include <SubtitleCutPaste.h>
#include <SubtitleMeasurementWorker.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/ExperimentalAspectActive.h>
#include <vprenderer/SubtitleAspectRetention.h>
using namespace AlphaSourceCrop;
std::vector<uint8_t> read(const char *p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void rect(const char *name, const SubtitleBoxRect &r) {
    std::cout << name << "=" << r.left << "," << r.top << "-" << r.right << "," << r.bottom << " ";
}
void bounds(const char *name, const ActivePictureBounds &r) {
    std::cout << name << "=" << r.left << "," << r.top << "-" << r.right << "," << r.bottom << " ";
}

int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf);
    SetEnvironmentVariableA("VP_AR_EXPERIMENT", "active");
    if (argc < 4)
        return 2;
    if (std::string(argv[2]) != "raised" && std::string(argv[2]) != "raised-clear-ui" &&
        std::string(argv[2]) != "raised-clear-ui-with-mark")
        return 2;
    const bool requireScope = argc > 4 && std::string(argv[4]) == "--require-scope";
    auto b = read(argv[1]);
    const std::string variant = argv[2];
    const bool actual2173 = std::string(argv[3]) == "2173";
    if (!actual2173 && std::string(argv[3]) != "2154")
        return 2;
    if (b.size() != 24883200)
        return 3;
    const auto *original = reinterpret_cast<const uint16_t *>(b.data());
    if ((original[size_t(2049) * 3840 + 1431] >> 6) != (actual2173 ? 156 : 150)) {
        std::cerr << "unexpected capture; expected S2154/S2173 footer pixel\n";
        return 2;
    }
    if (variant == "raised-clear-ui" || variant == "raised-clear-ui-with-mark") {
        auto *p = reinterpret_cast<uint16_t *>(b.data());
        for (int y = 0; y < 2160; ++y)
            if (y < 260 || y >= 1898)
                for (int x = 0; x < 3840; ++x)
                    if (!(x >= 1100 && x < 2750 && y >= 1980 && y < 2080))
                        p[size_t(y) * 3840 + x] = uint16_t(119 << 6);
    }
    if (variant == "raised-clear-ui-with-mark") {
        auto *p = reinterpret_cast<uint16_t *>(b.data());
        for (int y = 1980; y < 1988; ++y)
            for (int x = 25; x < 33; ++x)
                p[size_t(y) * 3840 + x] = uint16_t(700 << 6);
    }

    AnalysisLumaSource s{};
    s.data = b.data();
    s.dataBytes = b.size();
    s.width = 3840;
    s.height = 2160;
    s.rowBytes = s.chromaRowBytes = 7680;
    s.format = AnalysisLumaFormat::P010;
    s.encoding = VideoFrameEncoding::P010;
    s.colorspace = ColorSpace::REC_709;
    s.range = AnalysisLumaRange::Limited;
    s.generation = 1;
    ActivePictureFrameIdentity id{};
    id.transportGeneration = 1;
    id.acceptedSequence = actual2173 ? 2173 : 2154;
    id.sourceFrameNumber = id.acceptedSequence + 51;
    id.captureTimestamp = actual2173 ? 373434716 : 370076433;
    id.sourceFormatGeneration = 6417685342305713284ull;
    id.viewportGeneration = 3;
    id.rendererGeneration = 1;
    id.analysisDomainKey = 17712632399396405480ull;
    ActivePictureBounds base{0,    260,  3840,          1898,
                             3840, 2160, 3840.0 / 1638, ActivePictureBounds::BarAxes::TOP_BOTTOM};
    const uint64_t continuity = actual2173 ? 8 : 6;
    SubtitlePictureAuthority shared{};
    shared.required = true;
    shared.identity = id;
    shared.bounds = base;
    auto physical = ExperimentalAspect::MeasureActiveTrial(s, id);
    SubtitleBoxObservation key{};
    key.identity = id;
    key.nearBarDistance = 20;
    key.policyGeneration = 2;
    key.continuityGeneration = continuity;
    key.sharedPicture = shared;
    SubtitleMeasurementSampler sampler;
    auto obs = sampler.Measure(s, key, {}, 59.94, physical, true, &id);
    std::cout << "physical=" << int(physical.classification)
              << " strong=" << physical.strongCurrentBarMeasurement << " variant=" << variant
              << " source=" << argv[3] << " observed=" << obs.text.detected
              << " reason=" << obs.glyphLocalProofReason << " bar=" << obs.barEvidence.available
              << " ";
    rect("raw", obs.text.bounds);
    rect("line", obs.text.lineBounds[0]);
    std::cout << "\n";
    if (!obs.text.detected) {
        std::cout << "NO_CUE: safe fallback required; scope reconstruction NOT proven\n";
        return requireScope ? 1 : 0;
    }
    auto accepted = obs.text;
    accepted.cue = 1;
    SubtitleBoxPreview preview{};
    preview.available = true;
    preview.current = obs;
    preview.text = accepted;
    preview.policyGeneration = 2;
    preview.continuityGeneration = continuity;
    SubtitleCutPastePresentation placement;
    SubtitleBoxPadding padding{60, 40, 21};
    auto geom = placement.Consume(accepted, preview, padding, 20);
    geom = FitSubtitleToVisiblePicture(geom, {0, 260, 3840, 1898}, 20);
    geom = ApplySubtitleTextReduction(geom, 30, padding, true, false);
    std::cout << "geometry valid=" << geom.valid << " sourcePrepared=SIMULATED_NO_GPU ";
    rect("source", geom.source);
    rect("content", geom.content);
    rect("destination", geom.destination);
    rect("cleanup", geom.generatedCleanup);
    rect("pictureCapture", geom.pictureCapture);
    std::cout << " picture=" << geom.pictureTop << ".." << geom.pictureBottom << "\n";
    const bool assumed = true;
    const bool rendered = true; // Model successful preparation; no GPU render is claimed.
    auto legacy = EvaluateActivePicturePresentationRetention(s, base);
    auto global = EvaluateActivePictureGlobalNearBlack(s);
    auto darkness = EvaluateActivePictureArDarkness(s, physical, global);
    auto raw = EvaluateActivePicturePresentationRetention(s, base, physical, global, darkness);
    std::cout << "raw unseededClass=" << int(legacy.activePicture.classification)
              << " seededClass=" << int(raw.activePicture.classification)
              << " seededSafe=" << raw.excludedBandsPixelSafe << "\n";
    QualifiedSubtitlePresentationInput q{};
    q.mode = SubtitlePreviewMode::GeneratedGrayBackground;
    q.currentIdentity = id;
    q.observation = &obs;
    q.accepted = &accepted;
    q.composition = &geom;
    q.physicalBase = base;
    q.currentDisplayPicture = shared;
    q.policyGeneration = 2;
    q.continuityGeneration = continuity;
    q.sourcePrepared = rendered;
    auto legacyStrict = EvaluateQualifiedSubtitlePresentationEvidence(s, legacy, q);
    auto legacyPrepared = EvaluatePreparedSubtitlePresentationEvidence(s, legacy, q);
    std::cout << "unseeded strict=" << legacyStrict.qualified << ":" << legacyStrict.reason
              << " prepared=" << legacyPrepared.qualified << ":" << legacyPrepared.reason
              << " sourcePreparedAssumed=" << assumed << "\n";
    ActivePictureBounds erase{geom.content.left,
                              geom.content.top,
                              geom.content.right,
                              geom.content.bottom,
                              3840,
                              2160,
                              0,
                              ActivePictureBounds::BarAxes::NONE};
    auto residual =
        EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(s, base, raw, erase, 0);
    std::cout << "residual safe=" << residual.excludedBandsPixelSafe
              << " topXY=" << residual.visibleTop.nativePixelX << ","
              << residual.visibleTop.nativePixelY
              << " bottomXY=" << residual.visibleBottom.nativePixelX << ","
              << residual.visibleBottom.nativePixelY
              << " bottomFirst=" << residual.visibleBottom.firstX << ","
              << residual.visibleBottom.firstY << " luma=" << residual.visibleBottom.firstLuma
              << " cutoff=" << residual.visibleBottom.lumaCutoff
              << " threshold=" << raw.presentationBlackThreshold << "\n";
    auto strict = EvaluateQualifiedSubtitlePresentationEvidence(s, raw, q);
    auto prepared = EvaluatePreparedSubtitlePresentationEvidence(s, raw, q);
    std::cout << "strict=" << strict.qualified << ":" << strict.reason
              << " prepared=" << prepared.qualified << ":" << prepared.reason
              << " rawSafe=" << raw.excludedBandsPixelSafe
              << " rawVisible=" << raw.outwardVisibleBoundsAvailable << " ";
    bounds("rawBounds", raw.outwardVisibleBounds);
    std::cout << "\n";
    const auto &proof = strict.qualified ? strict : prepared;
    PresentationEnvelopeExtentState history{};
    auto view = ProjectQualifiedSubtitlePresentationEvidence(
        proof, history, raw.outwardVisibleBounds, base, id, raw);
    std::cout << "projection=" << view.qualified
              << " safe=" << view.retention.excludedBandsPixelSafe << " ";
    bounds("view", view.currentBounds);
    std::cout << "\n";
    if ((variant == "raised" || variant == "raised-clear-ui-with-mark") && view.qualified) {
        std::cerr << "ERROR: exact source contains unowned UI yet projection qualified\n";
        return 3;
    }
    if (variant == "raised-clear-ui-with-mark" &&
        (!view.currentBounds.rasterWidth || view.currentBounds.left > 25 ||
         view.currentBounds.right < 33 || view.currentBounds.top > 1980 ||
         view.currentBounds.bottom < 1988)) {
        std::cerr << "ERROR: selected conservative extent excludes independent mark\n";
        return 3;
    }
    bool scopeReady = false;
    if (view.qualified && view.retention.excludedBandsPixelSafe) {
        Input crop{};
        crop.automaticCropEnabled = crop.sharedGeometryAvailable =
            crop.latestObservationSupportsCrop = true;
        crop.geometry = base;
        crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
        crop.geometrySourceGeneration = crop.frameSourceGeneration = 1;
        crop.frameSourceSequence = id.acceptedSequence;
        crop.rasterWidth = 3840;
        crop.rasterHeight = 2160;
        crop.frameLocalPresentationRetentionEvaluated = true;
        crop.frameLocalPresentationRetentionSafe = true;
        crop.currentVisibleBoundsAvailable = view.retention.outwardVisibleBoundsAvailable;
        crop.currentVisibleBounds = view.currentBounds;
        crop.currentVisibleBase = base;
        crop.currentVisibleSourceGeneration = 1;
        crop.currentVisibleSourceSequence = id.acceptedSequence;
        auto admitted = AdmitCropPresentation({}, crop, Evaluate(crop), 3);
        FinalAspectLimitFillInput fill{};
        fill.crop = crop;
        fill.presentation = admitted.presentation;
        fill.configuredScreenActive = true;
        fill.configured.screenAspect = 2.35;
        fill.configured.cropNarrowerContentToFillScreen = fill.configured.narrowerLimitConfigured =
            true;
        fill.configured.narrowerAspectLimit = 2.32;
        fill.configured.cropWiderContentToFillScreen = fill.configured.widerLimitConfigured = true;
        fill.configured.widerAspectLimit = 2.42;
        auto final = EvaluateFinalAspectLimitFill(fill);
        const auto selected =
            final.applied ? final.sourceBounds : admitted.presentation.sourceBounds;
        scopeReady =
            !admitted.blocked && admitted.presentation.applyCrop && geom.content.bottom > 2049 &&
            selected.left == 0 && selected.right == 3840 && selected.top > 0 &&
            selected.bottom < 2160 && selected.left <= geom.destination.left &&
            selected.top <= geom.destination.top && selected.right >= geom.destination.right &&
            selected.bottom >= geom.destination.bottom;
        std::cout << "modeled final eligible admitted=" << !admitted.blocked
                  << " fill=" << final.applied << " ";
        bounds("source", final.applied ? final.sourceBounds : admitted.presentation.sourceBounds);
        std::cout << "\n";
    } else
        std::cout << "unowned pixels: retain current raw evidence; any proposed crop must contain "
                     "them or fail open\n";
    if (requireScope && !scopeReady) {
        std::cerr << "ACCEPTANCE_RED: scope-safe real glyph relocation not established\n";
        return 1;
    }
    return 0;
}
