#pragma once

// Compare two transition models on the same moving source pixels. Both
// apertures last long enough to pass normal adjacent-frame confirmation.
static bool RunAspectOscillationSequence(double fps) {
    constexpr int width = 640, height = 360;
    constexpr int scopeTop = 46, openTop = 12;
    const int shotFrames = static_cast<int>(std::ceil(fps / 3));
    const int holdFrames = static_cast<int>(std::ceil(2 * fps));
    auto pixels = std::make_shared<std::vector<uint16_t>>(width * height * 3 / 2, 512 << 6);
    AnalysisLumaSource source;
    source.width = width;
    source.height = height;
    source.rowBytes = source.chromaRowBytes = width * 2;
    source.range = AnalysisLumaRange::Limited;
    source.generation = 1;
    source.data = reinterpret_cast<const uint8_t*>(pixels->data());
    source.dataBytes = pixels->size() * sizeof(uint16_t);
    source.backingOwner = pixels;

    ActivePictureTransitionModel guarded, disabled;
    guarded.SetOscillationHoldMilliseconds(2000);
    disabled.SetOscillationHoldMilliseconds(0);
    int guardedChanges = 0, disabledChanges = 0, recoveredAt = -1;
    int previousGuarded = -1, previousDisabled = -1;
    bool rawCorrect = true, safeDuringReversals = true, firstCutUnchanged = true;
    const int sustainedStart = shotFrames * 8;
    for (int frame = 0; frame <= sustainedStart + holdFrames + shotFrames; ++frame) {
        const int phase = frame / shotFrames;
        const int top = frame >= sustainedStart || phase % 2 == 0 ? scopeTop : openTop;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int luma = y < top || y >= height - top ? 64 : 220 + (x + y + frame) % 113;
                (*pixels)[y * width + x] = static_cast<uint16_t>(luma << 6);
            }
        }
        const auto measurement = ExperimentalAspect::Measure(width, height, 64,
            [&](int x, int y, ExperimentalAspect::Pixel& pixel) {
                AnalysisLumaSample value;
                if (!source.Sample(x, y, value)) return false;
                pixel = {value.luma, value.chromaU, value.chromaV};
                return true;
            }, [] { return true; });
        const auto evidence = ExperimentalAspect::EvidenceFromMeasurement(source, measurement);
        rawCorrect &= evidence.available && evidence.trustedBounds.top == top &&
            evidence.trustedBounds.bottom == height - top;
        const auto observed = MakeActivePictureObservation(evidence, frame + 1, fps);
        const auto protectedDecision = guarded.Observe(observed);
        const auto ordinaryDecision = disabled.Observe(observed);
        // On publication, stableBounds is the old reference (empty on startup).
        // RenderLocked installs bounds on that source and retains stableBounds
        // only on later non-publishing observations.
        const auto selectedBounds = [](const ActivePictureTransitionDecision& decision)
            -> const ActivePictureBounds* {
            if (!decision.stable || decision.clearTransition) return nullptr;
            return decision.publish ? &decision.bounds : &decision.stableBounds;
        };
        const auto* protectedBounds = selectedBounds(protectedDecision);
        const auto* ordinaryBounds = selectedBounds(ordinaryDecision);
        if (phase < 2) {
            firstCutUnchanged &= protectedDecision.publish == ordinaryDecision.publish &&
                ((protectedBounds == nullptr && ordinaryBounds == nullptr) ||
                 (protectedBounds && ordinaryBounds && protectedBounds->SameRectangleAndAxes(*ordinaryBounds)));
        }
        if (phase >= 2 && frame < sustainedStart)
            safeDuringReversals &= protectedBounds && protectedBounds->top == openTop &&
                protectedBounds->bottom == height - openTop;
        if (protectedBounds) {
            const int current = protectedBounds->top;
            if (previousGuarded >= 0 && previousGuarded != current) ++guardedChanges;
            previousGuarded = current;
            if (frame >= sustainedStart && current == scopeTop && recoveredAt < 0)
                recoveredAt = frame - sustainedStart;
        }
        if (ordinaryBounds) {
            const int current = ordinaryBounds->top;
            if (previousDisabled >= 0 && previousDisabled != current) ++disabledChanges;
            previousDisabled = current;
        }
    }
    std::printf("%.3f Hz: raw_correct=%d first_cut_unchanged=%d guarded_changes=%d "
        "disabled_changes=%d sustained_return_frame=%d\n",
        fps, rawCorrect, firstCutUnchanged, guardedChanges, disabledChanges, recoveredAt);
    return rawCorrect && firstCutUnchanged && safeDuringReversals && guardedChanges == 2 &&
        disabledChanges == 8 && recoveredAt == holdFrames;
}
