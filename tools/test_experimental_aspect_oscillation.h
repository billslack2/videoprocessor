#pragma once

namespace OscillationPixelTests {

constexpr int Width = 640, Height = 360;
constexpr int ScopeTop = 46, OpenTop = 12;

struct Applied {
    int guardedTop, ordinaryTop;
    bool sameDecision;
};

struct Stream {
    double fps;
    uint64_t sequence;
    std::vector<uint16_t> pixels;
    AnalysisLumaSource source;
    ActivePictureTransitionModel guarded, ordinary;
    bool valid = true;
    int previousGuarded = -1, previousOrdinary = -1;
    int guardedChanges = 0, ordinaryChanges = 0;

    Stream(double rate, uint64_t firstSequence) :
        fps(rate), sequence(firstSequence), pixels(Width * Height * 3 / 2, 512 << 6) {
        source.width = Width;
        source.height = Height;
        source.rowBytes = source.chromaRowBytes = Width * 2;
        source.range = AnalysisLumaRange::Limited;
        source.generation = 1;
        source.data = reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes = pixels.size() * sizeof(uint16_t);
        guarded.SetOscillationHoldMilliseconds(2000);
        ordinary.SetOscillationHoldMilliseconds(0);
    }

    int AppliedTop(const ActivePictureTransitionDecision& decision, int& previous, int& changes) {
        if (!decision.stable || decision.clearTransition) return -1;
        // RenderLocked installs bounds on publication; stableBounds is the old
        // reference on that source and is used only on non-publishing sources.
        const auto& b = decision.publish ? decision.bounds : decision.stableBounds;
        valid &= b.left == 0 && b.right == Width && b.bottom == Height - b.top &&
            b.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM;
        if (previous >= 0 && previous != b.top) ++changes;
        previous = b.top;
        return b.top;
    }

    Applied Step(int top, bool lowerCaption = false) {
        for (int y = 0; y < Height; ++y) {
            for (int x = 0; x < Width; ++x) {
                int luma = y < top || y >= Height - top ? 64 : 220 + int((x + y + sequence) % 113);
                if (lowerCaption && y >= 323 && y < 338 && x >= 258 && x < 382 && x % 12 < 5)
                    luma = 800;
                pixels[y * Width + x] = static_cast<uint16_t>(luma << 6);
            }
        }
        const auto measurement = ExperimentalAspect::Measure(Width, Height, 64,
            [&](int x, int y, ExperimentalAspect::Pixel& pixel) {
                AnalysisLumaSample sample;
                if (!source.Sample(x, y, sample)) return false;
                pixel = {sample.luma, sample.chromaU, sample.chromaV};
                return true;
            }, [] { return true; });
        const auto evidence = ExperimentalAspect::EvidenceFromMeasurement(source, measurement);
        valid &= evidence.available && evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            evidence.trustedBounds.left == 0 && evidence.trustedBounds.right == Width &&
            evidence.trustedBounds.top == top && evidence.trustedBounds.bottom == Height - top;
        const auto observed = MakeActivePictureObservation(evidence, sequence++, fps);
        const auto protectedDecision = guarded.Observe(observed);
        const auto ordinaryDecision = ordinary.Observe(observed);
        const int protectedTop = AppliedTop(protectedDecision, previousGuarded, guardedChanges);
        const int ordinaryTop = AppliedTop(ordinaryDecision, previousOrdinary, ordinaryChanges);
        return {protectedTop, ordinaryTop, protectedTop == ordinaryTop &&
            protectedDecision.publish == ordinaryDecision.publish &&
            protectedDecision.clearTransition == ordinaryDecision.clearTransition};
    }
};

static bool IsolatedReturns(double fps) {
    // The 23.976 Hz trial opened at S3042, returned at S3071, opened again
    // at S3297 and returned at S3329. Keep their elapsed times at 59.94 Hz.
    const auto atRate = [fps](int filmFrames) {
        return static_cast<int>(std::lround(filmFrames * fps / (24000.0 / 1001)));
    };
    const int preRoll = atRate(16);
    const std::array<int, 4> onsets{0, atRate(29), atRate(255), atRate(287)};
    const std::array<int, 4> aperture{OpenTop, ScopeTop, OpenTop, ScopeTop};
    std::array<int, 4> latency{-1, -1, -1, -1};
    Stream stream(fps, 3042 - preRoll);
    bool matched = true, acquired = true;
    int shot = -1;
    for (int i = -preRoll; i <= onsets.back() + atRate(60); ++i) {
        if (shot + 1 < int(onsets.size()) && i == onsets[shot + 1]) ++shot;
        const int top = shot < 0 ? ScopeTop : aperture[shot];
        const bool caption = (i >= onsets[1] - atRate(3) && i <= onsets[1] + atRate(6)) ||
            (i >= onsets[3] - atRate(3) && i <= onsets[3] + atRate(2));
        const auto selected = stream.Step(top, caption);
        matched &= selected.sameDecision;
        if (i >= -1) acquired &= selected.guardedTop >= 0 && selected.ordinaryTop >= 0;
        if (shot >= 0 && latency[shot] < 0 && selected.guardedTop == top) latency[shot] = i - onsets[shot];
    }
    bool timely = true;
    for (int frames : latency) timely &= frames >= 0 && frames <= 2;
    std::printf("%.3f Hz isolated source cuts: correct=%d baseline_parity=%d changes=%d/%d "
        "cut_latency_frames=%d,%d,%d,%d\n", fps, stream.valid, matched,
        stream.guardedChanges, stream.ordinaryChanges, latency[0], latency[1], latency[2], latency[3]);
    return stream.valid && acquired && matched && timely && stream.guardedChanges == 4 && stream.ordinaryChanges == 4;
}

static bool SustainedAlternation(double fps) {
    const int shotFrames = static_cast<int>(std::ceil(fps / 3));
    const int holdFrames = static_cast<int>(std::ceil(2 * fps));
    const int sustainedStart = shotFrames * 8;
    Stream stream(fps, 1);
    bool initialParity = true, staysOpen = true, settled = true;
    int recoveredAt = -1;
    for (int frame = 0; frame <= sustainedStart + holdFrames + shotFrames; ++frame) {
        const int phase = frame / shotFrames;
        const int top = frame >= sustainedStart || phase % 2 == 0 ? ScopeTop : OpenTop;
        const auto selected = stream.Step(top);
        // Scope, open, scope is a single round trip. A further outward cut can
        // still reveal picture. Subsequent inward cuts need sustained support.
        if (phase < 4) initialParity &= selected.sameDecision;
        if (phase >= 4 && frame < sustainedStart + holdFrames) staysOpen &= selected.guardedTop == OpenTop;
        if (frame >= sustainedStart && selected.guardedTop == ScopeTop && recoveredAt < 0)
            recoveredAt = frame - sustainedStart;
        if (recoveredAt >= 0) settled &= selected.guardedTop == ScopeTop;
    }
    std::printf("%.3f Hz repeated source cuts: correct=%d initial_parity=%d protects_picture=%d "
        "changes=%d/%d sustained_return_frame=%d\n", fps, stream.valid, initialParity,
        staysOpen, stream.guardedChanges, stream.ordinaryChanges, recoveredAt);
    return stream.valid && initialParity && staysOpen && settled && stream.guardedChanges == 4 &&
        stream.ordinaryChanges == 8 && recoveredAt == holdFrames;
}

} // namespace OscillationPixelTests

static bool RunAspectOscillationSequence(double fps) {
    const bool isolated = OscillationPixelTests::IsolatedReturns(fps);
    const bool sustained = OscillationPixelTests::SustainedAlternation(fps);
    return isolated && sustained;
}
