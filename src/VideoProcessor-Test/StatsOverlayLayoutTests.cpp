#include "pch.h"
#include "CppUnitTest.h"
#include "../VideoProcessor-GUI/StatsOverlayWindow.h"
#include <algorithm>
#include <HdrOsdFormatting.h>
#include <numeric>
#include <type_traits>

static_assert(std::is_standard_layout<RendererOutputPresentationStats>::value &&
    std::is_trivially_copyable<RendererOutputPresentationStats>::value,
    "Output presentation telemetry must remain a shared value snapshot.");

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
TEST_CLASS(StatsOverlayLayoutTests)
{
    struct Graphics
    {
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = CreateFontW(StatsOverlayLayout::LineHeight, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        HGDIOBJ old = nullptr;
        Graphics() { if (dc && font) old = SelectObject(dc, font); }
        ~Graphics() { if (old) SelectObject(dc, old); if (font) DeleteObject(font); if (dc) DeleteDC(dc); }
    };
    static StatsData Example(bool alpha)
    {
        StatsData stats; stats.isAlphaRenderer = alpha; stats.rendererName = L"Alpha";
        stats.resolution = L"4096 x 2160"; stats.aspectRatio = L"2.350"; stats.aspectName = L"Scope";
        stats.refreshRate = 23.976; stats.displayRefreshRate = 59.940;
        stats.eotf = L"PQ (wire SDR)"; stats.colorspace = L"BT.2020 (wire Rec.709)";
        stats.dtmProfile = L"Cinema"; stats.hdr10 = L"CLL 10000 / FALL 10000 | M 0.005-10000";
        stats.measuredHdr = L"peak 10000 / avg 10000 | L1 10000 / 10000";
        stats.method = L"Rational-Rational"; stats.videoConversion = L"V210 -> P010";
        stats.pixelFormat = L"V210"; stats.viewport = L"cinema (2.35:1)"; stats.surfaceMode = L"Fullscreen";
        stats.outputMode = L"Target SDR Rec.709 | Req A/A/A/A -> Flip/F/sRGB/709"
            L" | SIGNAL: native | SWAPCHAIN: R10G10B10A2 | PRESENTER: D3D11"
            L" | CONTRACT: SDR Full | DISPLAY: HDR off | GAMMA: 2.2 | STATUS: Active"
            L" | WIRE: 3840x2160 | DRAWN: 3840x2160 | DESKTOP: 1920x1080";
        stats.displayLut = L"Active: calibration.cube (65^3)";
        stats.activeShaderRule = L"Default"; stats.activeShaders = { L"Sharpen", L"Deband" };
        return stats;
    }
    static const StatsOverlayLayout::Row& Find(const std::vector<StatsOverlayLayout::Row>& rows, const wchar_t* label)
    {
        const auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.label == label; });
        Assert::IsTrue(at != rows.end()); return *at;
    }
    static void VerifyNative(const StatsData& stats)
    {
        Graphics graphics; Assert::IsTrue(graphics.dc && graphics.font);
        const auto rows = StatsOverlayLayout::Arrange(graphics.dc, StatsOverlayWindow::BuildStatsRows(stats));
        const int expected = StatsOverlayLayout::Padding * 2 + static_cast<int>(rows.size()) *
            StatsOverlayLayout::LineHeight + std::accumulate(rows.begin(), rows.end(), 0,
                [](int sum, const auto& row) { return sum + row.gapBefore; });
        StatsOverlayWindow overlay; overlay.UpdateStats(stats);
        std::vector<uint8_t> pixels; int width = 0, height = 0, stride = 0;
        Assert::IsTrue(overlay.RenderBgra(pixels, width, height, stride));
        Assert::AreEqual(StatsOverlayLayout::Width, width); Assert::AreEqual(expected, height);
        Assert::AreEqual(width * 4, stride); Assert::AreEqual(size_t(stride) * height, pixels.size());
        // Every real row draws ink inside its slot; no row is allocated solely
        // because an optional detail marker was present without a value.
        int y = StatsOverlayLayout::Padding;
        for (const auto& row : rows)
        {
            y += row.gapBefore;
            bool hasInk = false;
            for (int scan = y; scan < y + StatsOverlayLayout::LineHeight; ++scan)
                for (int x = StatsOverlayLayout::Padding; x < width - StatsOverlayLayout::Padding; ++x)
                {
                    const size_t offset = size_t(scan) * stride + x * 4;
                    hasInk |= pixels[offset] != 43 || pixels[offset + 1] != 43 || pixels[offset + 2] != 43;
                }
            Assert::IsTrue(hasInk, L"Native row was blank or clipped vertically");
            y += StatsOverlayLayout::LineHeight;
        }
        Assert::AreEqual(height - StatsOverlayLayout::Padding, y);
        // Both outer margins and all final padding remain background, even
        // with arbitrarily long user names and high-valued diagnostics.
        for (int scan = 0; scan < height; ++scan)
            for (int x = width - StatsOverlayLayout::Padding; x < width; ++x)
                Assert::AreEqual(uint8_t(43), pixels[size_t(scan) * stride + x * 4]);
        for (int scan = y; scan < height; ++scan)
            for (int x = 0; x < width; ++x)
                Assert::AreEqual(uint8_t(43), pixels[size_t(scan) * stride + x * 4]);
    }
public:
    TEST_METHOD(BothBackendsDeriveNativeHeightFromExactRowsAndConditionalSections)
    {
        for (const bool alpha : {false, true})
        {
            auto stats = Example(alpha); VerifyNative(stats);
            stats.measuredRefreshRate = 23.9761; stats.hasPPMCorrection = true; stats.ppmCorrection = 7;
            stats.hasConversionData = true; stats.currentConversionTimeUs = 1300.0;
            stats.avgConversionTime10s = 1200.0; stats.maxConversionTime10s = 1800.0;
            stats.outputSweep = L"HDR 1000 nits"; stats.renderLoadKnown = true;
            stats.renderLoadGpuValid = true; stats.renderLoadFramePeriodMs = 16.68;
            stats.renderLoadFramePeriodFromRenderCadence = true;
            VerifyNative(stats);
        }
    }
    TEST_METHOD(MissingAndEmptyOutputDetailsDoNotAllocatePhantomRows)
    {
        for (const bool alpha : {false, true})
        {
            StatsData stats; stats.isAlphaRenderer = alpha;
            const auto baseline = StatsOverlayWindow::BuildStatsRows(stats); VerifyNative(stats);
            stats.outputMode = L"Target SDR Rec.709 | Req A/A/A/A -> Flip/F/sRGB/709"
                L" | GAMMA:  | DISPLAY:  | STATUS:  | SIGNAL:  | SWAPCHAIN:  | PRESENTER:  | CONTRACT: ";
            const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::AreEqual(baseline.size() + size_t(2), rows.size()); VerifyNative(stats);
        }
    }
    TEST_METHOD(RasterAndShaderContinuationRowsUseExplicitSharedValueColumn)
    {
        const auto rows = StatsOverlayWindow::BuildStatsRows(Example(true));
        size_t continuationCount = 0;
        for (const auto& row : rows) if (row.label.empty())
        {
            ++continuationCount;
            Assert::IsFalse(row.value.empty());
            Assert::IsTrue(row.value.front() == L'[' || row.value.front() == L'-');
            Assert::AreEqual(0, row.gapBefore);
        }
        Assert::AreEqual(size_t(4), continuationCount);
        Graphics graphics; Assert::IsTrue(graphics.dc && graphics.font);
        SIZE columns{}; const std::wstring spaces(StatsOverlayLayout::LabelColumns, L' ');
        Assert::IsTrue(GetTextExtentPoint32W(graphics.dc, spaces.c_str(), int(spaces.size()), &columns) != FALSE);
        Assert::AreEqual(StatsOverlayLayout::Padding + int(columns.cx), StatsOverlayLayout::ValueX(graphics.dc));
    }
    TEST_METHOD(LongProfileShaderLutAndStatusNamesAreMeasuredAndEllipsized)
    {
        auto stats = Example(true); const std::wstring longName(250, L'W');
        stats.dtmProfile = longName.c_str(); stats.activeShaderRule = longName.c_str();
        stats.activeShaders = {CString(longName.c_str())}; stats.displayLut = longName.c_str();
        stats.outputSweep = longName.c_str();
        const CString status = L"STATUS: " + CString(longName.c_str());
        stats.outputMode.Replace(L"STATUS: Active", status);
        VerifyNative(stats);
        Graphics graphics; Assert::IsTrue(graphics.dc && graphics.font);
        const int available = StatsOverlayLayout::Width - StatsOverlayLayout::Padding - StatsOverlayLayout::ValueX(graphics.dc);
        const auto fitted = StatsOverlayLayout::FitText(graphics.dc, longName, available);
        Assert::IsTrue(fitted.size() < longName.size()); Assert::IsTrue(fitted.substr(fitted.size() - 3) == L"...");
        SIZE extent{}; Assert::IsTrue(GetTextExtentPoint32W(graphics.dc, fitted.c_str(), int(fitted.size()), &extent) != FALSE);
        Assert::IsTrue(extent.cx <= available);
        Assert::AreEqual(L"Cinema", StatsOverlayLayout::FitText(graphics.dc, L"Cinema", available).c_str());
        Assert::IsTrue(StatsOverlayLayout::FitText(graphics.dc, longName, 1).empty());
        Assert::IsTrue(StatsOverlayLayout::FitText(graphics.dc, longName, 0).empty());
    }
    TEST_METHOD(ProductionHdrAndGpuValuesFitOrWrapWithoutLosingDiagnosticNumbers)
    {
        Graphics graphics; Assert::IsTrue(graphics.dc && graphics.font);
        for (const bool dolby : {false,true})
            for (const double value : {10000.0, 0.001, 0.00001})
            {
                auto stats = Example(true);
                RendererHdrStatus hdr; hdr.hdr = true; hdr.dolbyVision = dolby;
                hdr.maxCll = value; hdr.maxFall = value; hdr.masteringMin = 0.00001;
                hdr.masteringMax = 0.00002; hdr.peakDetectionEnabled = true; hdr.detectedValid = true;
                hdr.detectedPeakNits = value; hdr.detectedLinearAvgNits = value;
                hdr.sceneValid = true; hdr.scenePeakNits = value; hdr.sceneAvgNits = value;
                stats.hdr10 = HdrOsdFormatting::Static(hdr); stats.measuredHdr = HdrOsdFormatting::Measured(hdr);
                stats.renderLoadKnown = true; stats.renderLoadGpuValid = true; stats.cpuUsageKnown = true;
                stats.cpuUsageAvgPercent = stats.cpuUsagePeakPercent = 100.0;
                stats.renderLoadGpuPercentValid = true;
                stats.renderLoadGpuAvgPercent = stats.renderLoadGpuPercent = 1000.0;
                const auto raw = StatsOverlayWindow::BuildStatsRows(stats);
                const auto arranged = StatsOverlayLayout::Arrange(graphics.dc, raw);
                const int available = StatsOverlayLayout::Width - StatsOverlayLayout::Padding - StatsOverlayLayout::ValueX(graphics.dc);
                for (const wchar_t* label : {L"HDR10 (nits):", L"Measured (nits):", L"10s avg/max:"})
                {
                    const auto first = std::find_if(arranged.begin(), arranged.end(),
                        [&](const auto& row) { return row.label == label; });
                    Assert::IsTrue(first != arranged.end());
                    std::wstring restored;
                    for (auto at = first; at != arranged.end() && (at == first || at->label.empty()); ++at)
                    {
                        if (!restored.empty()) restored += L" "; restored += at->value;
                        Assert::AreEqual(at->value.c_str(), StatsOverlayLayout::FitText(graphics.dc, at->value, available).c_str());
                    }
                    Assert::AreEqual(Find(raw, label).value.c_str(), restored.c_str());
                }
                VerifyNative(stats);
            }
    }
    TEST_METHOD(SectionGapsFollowExistingSectionsWithoutDoubleAlphaGap)
    {
        for (const bool alpha : {false, true})
        {
            StatsData stats; stats.isAlphaRenderer = alpha;
            const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::AreEqual(StatsOverlayLayout::SectionGap, Find(rows, L"Delay:").gapBefore);
            Assert::AreEqual(StatsOverlayLayout::SectionGap, Find(rows, alpha ? L"Queue:" : L"Queue R/C/T:").gapBefore);
            const int gaps = std::accumulate(rows.begin(), rows.end(), 0, [](int sum, const auto& row) { return sum + row.gapBefore; });
            Assert::AreEqual((alpha ? 2 : 3) * StatsOverlayLayout::SectionGap, gaps);
        }
    }
    TEST_METHOD(ProcessingRowsKeepTwoSecondCpuCountsSeparateFromAsyncGpuCoverage)
    {
        auto stats=Example(true);stats.renderLoadKnown=true;stats.renderLoadGpuValid=true;
        stats.renderLoadGpuAvgMs=5;stats.renderLoadGpuPeakMs=9;stats.renderLoadGpuFrames=50;stats.renderLoadFrames=120;
        auto& timing=stats.processingPerformance;timing.available=true;timing.windowMilliseconds=2000;
        timing.preparationCalls=48;timing.preparationCpu.average=2;timing.preparationCpu.peak=10;
        timing.sourceDraws=48;timing.sourceDrawCpu.average=12;timing.sourceDrawCpu.peak=20;
        timing.reusedDraws=72;timing.reusedDrawCpu.average=0.5;timing.reusedDrawCpu.peak=1;
        const auto rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"GPU frame:").value.find(L"50/120 async (10 s)")!=std::wstring::npos);
        Assert::IsTrue(Find(rows,L"Prep CPU/wait:").value.find(L"n=48 (2.0 s)")!=std::wstring::npos);
        Assert::IsTrue(Find(rows,L"New CPU/wait:").value.find(L"12.00 / 20.00")!=std::wstring::npos);
        Assert::IsTrue(Find(rows,L"Reuse CPU/wait:").value.find(L"n=72 (2.0 s)")!=std::wstring::npos);
        VerifyNative(stats);
    }
    TEST_METHOD(ProcessingRowsDistinguishUnavailableNoCallsAndMeasuredZero)
    {
        auto stats=Example(true);stats.renderLoadKnown=true;
        auto rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Prep CPU/wait:").value==L"measuring...");
        stats.processingPerformance.available=true;stats.processingPerformance.windowMilliseconds=2000;
        stats.processingPerformance.sourceDraws=1;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Prep CPU/wait:").value==L"no calls in window");
        Assert::IsTrue(Find(rows,L"New CPU/wait:").value.find(L"0.00 / 0.00")!=std::wstring::npos);
        VerifyNative(stats);
    }
    TEST_METHOD(DisplayObservationsDistinguishUnavailableWarmupAndMeasuredZero)
    {
        for (const bool alpha : {false, true})
        {
            auto stats = Example(alpha);
            auto rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::IsTrue(Find(rows,L"Display D/R:").value == L"unavailable (not measured)");
            Assert::IsTrue(Find(rows,L"Display holds:").value == L"unavailable");
            auto& output = stats.outputPresentation;
            output.supported = true;
            output.state = RendererOutputPresentationState::Warming;
            rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::IsTrue(Find(rows,L"Display holds:").value == L"unavailable (warming)");
            output.state = RendererOutputPresentationState::Observing;
            // A contradictory ready state with no verified interval still must
            // not print a clean zero. GPU timing need not be available at all.
            rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::IsTrue(Find(rows,L"Display holds:").value.find(L"0 events") == std::wstring::npos);
            output.verifiedIntervals = 12;
            rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::IsFalse(stats.renderLoadKnown);
            Assert::IsTrue(Find(rows,L"Display holds:").value == L"0 events / 0 excess refreshes (observed)");
            Assert::IsTrue(Find(rows,L"Display evidence:").value == L"12 intervals / 0 gaps; partial; observing");
            Assert::IsTrue(Find(rows,L"Display D/R:").value == L"unavailable (not measured)");
            VerifyNative(stats);
        }
    }
    TEST_METHOD(DisplayObservationFailuresNeverTurnMissingEvidenceIntoCleanZero)
    {
        auto stats = Example(true);
        auto& output = stats.outputPresentation;
        output.supported = true;
        for (const auto reason : {RendererOutputPresentationReason::NoSwapchain,
            RendererOutputPresentationReason::Unsupported,
            RendererOutputPresentationReason::StatisticsFailed,
            RendererOutputPresentationReason::Disjoint,
            RendererOutputPresentationReason::Stale,
            RendererOutputPresentationReason::ObservationGap})
        {
            output.reason = reason;
            output.state = RendererOutputPresentationState::Limited;
            const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
            const auto& holds = Find(rows,L"Display holds:").value;
            Assert::IsTrue(holds.find(L"unavailable (") == 0);
            Assert::IsTrue(holds.find(L"0 events") == std::wstring::npos);
            VerifyNative(stats);
        }
        output.verifiedIntervals = 25;
        output.reason = RendererOutputPresentationReason::Unsupported;
        for (const bool supported : {true,false})
        {
            output.supported = supported;
            const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
            Assert::IsTrue(Find(rows,L"Display holds:").value == L"unavailable (unsupported)");
            Assert::IsTrue(Find(rows,L"Display evidence:").value == (supported ? L"unsupported" : L"unavailable"));
        }
    }
    TEST_METHOD(RecordedDisplayHoldsSurviveUnavailableAndResetObservationStates)
    {
        auto stats = Example(true);
        auto& output = stats.outputPresentation;
        output.observedLongHolds = 3; output.excessRefreshes = 4;
        output.verifiedIntervals = 20; output.observationGaps = 2;
        for (const auto state : {RendererOutputPresentationState::Unavailable,
            RendererOutputPresentationState::Warming,RendererOutputPresentationState::Limited})
            for (const auto reason : {RendererOutputPresentationReason::None,
                RendererOutputPresentationReason::NoSwapchain,
                RendererOutputPresentationReason::Unsupported,
                RendererOutputPresentationReason::StatisticsFailed,
                RendererOutputPresentationReason::Disjoint,
                RendererOutputPresentationReason::Stale,
                RendererOutputPresentationReason::ObservationGap})
                for (const bool supported : {false,true})
                {
                    output.supported = supported; output.state = state; output.reason = reason;
                    const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
                    Assert::IsTrue(Find(rows,L"Display holds:").value == L"3 events / 4 excess refreshes (recorded)");
                    Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"20 intervals / 2 gaps; partial; ") == 0);
                    Assert::IsTrue(Find(rows,L"Display D/R:").value == L"unavailable (not measured)");
                }
        output.supported = false; output.state = RendererOutputPresentationState::Unavailable;
        output.reason = RendererOutputPresentationReason::Unsupported;
        VerifyNative(stats);
        // Resetting only the current observation baseline cannot erase the
        // session's incidents even before a new interval has been verified.
        output.verifiedIntervals = 0;
        const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display holds:").value == L"3 events / 4 excess refreshes (recorded)");
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"unsupported") != std::wstring::npos);
    }
    TEST_METHOD(DisplayHoldTotalsKeepPartialCoverageAndLargeNumbersVisible)
    {
        auto stats = Example(true);
        auto& output = stats.outputPresentation;
        output.supported = true;
        output.state = RendererOutputPresentationState::Limited;
        output.reason = RendererOutputPresentationReason::ObservationGap;
        output.observedLongHolds = UINT64_MAX;
        output.excessRefreshes = UINT64_MAX;
        output.verifiedIntervals = UINT64_MAX;
        output.observationGaps = 7;
        const auto raw = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(raw,L"Display holds:").value.find(L"18446744073709551615 events") == 0);
        Assert::IsTrue(Find(raw,L"Display evidence:").value.find(L"7 gaps; partial; observation gap") != std::wstring::npos);
        Graphics graphics; Assert::IsTrue(graphics.dc && graphics.font);
        const auto arranged = StatsOverlayLayout::Arrange(graphics.dc,raw);
        const int available = StatsOverlayLayout::Width - StatsOverlayLayout::Padding - StatsOverlayLayout::ValueX(graphics.dc);
        for (const wchar_t* label : {L"Display holds:",L"Display evidence:"})
        {
            auto at = std::find_if(arranged.begin(),arranged.end(),[&](const auto& row){return row.label==label;});
            Assert::IsTrue(at != arranged.end());
            std::wstring restored;
            for (bool first=true; at!=arranged.end() && (first || at->label.empty()); ++at,first=false)
            {
                if (!restored.empty()) restored += L" ";
                restored += at->value;
                Assert::AreEqual(at->value.c_str(),StatsOverlayLayout::FitText(graphics.dc,at->value,available).c_str());
            }
            Assert::AreEqual(Find(raw,label).value.c_str(),restored.c_str());
        }
        VerifyNative(stats);
    }
    TEST_METHOD(PlaybackCounterResetHidesOldReportsUntilRendererAcknowledges)
    {
        auto stats = Example(true);
        stats.playbackCounterResetSerial = 2;
        auto& output = stats.outputPresentation;
        output.supported = true;
        output.appliedResetSerial = 1;
        output.state = RendererOutputPresentationState::Observing;
        output.verifiedIntervals = 20;
        output.observedLongHolds = 8;
        output.excessRefreshes = 12;
        auto rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display holds:").value == L"reset pending");
        Assert::IsTrue(Find(rows,L"Display evidence:").value == L"reset pending");
        VerifyNative(stats);
        output = {};
        output.appliedResetSerial = 2;
        output.reason = RendererOutputPresentationReason::Unsupported;
        rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display holds:").value == L"unavailable (unsupported)");
        Assert::IsTrue(Find(rows,L"Display D/R:").value == L"unavailable (not measured)");
        output.supported = true;
        output.state = RendererOutputPresentationState::Observing;
        output.reason = RendererOutputPresentationReason::None;
        output.verifiedIntervals = 1;
        rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display holds:").value == L"0 events / 0 excess refreshes (observed)");
    }
    TEST_METHOD(CaptureQueueLossAndIntentionalCorrectionsAreNotDisplayDropCounts)
    {
        auto stats = Example(true);
        stats.capturedDroppedFrames = 2; stats.queueDroppedFrames = 3;
        stats.sceneDetectCorrectionDrops = 4; stats.sceneDetectCorrectionRepeats = 5;
        stats.cadenceIntervalEstimate = L"repeat every 100 s";
        const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Capture/queue:").value == L"2 / 3 drops");
        Assert::IsTrue(Find(rows,L"Intentional D/R:").value == L"4 / 5");
        Assert::IsTrue(Find(rows,L"Cadence drift:").value == L"estimate: repeat every 100 s");
        Assert::IsTrue(Find(rows,L"Display D/R:").value == L"unavailable (not measured)");
        for (const auto& row : rows)
        {
            Assert::IsFalse(row.label == L"Dropped:" || row.label == L"D/R estimate:" || row.label == L"- Action D/R:");
            Assert::IsTrue(row.label.size() < size_t(StatsOverlayLayout::LabelColumns));
        }
        VerifyNative(stats);
    }
    TEST_METHOD(PassiveDisplayOutcomesAreObservedAndRepeatCountsRemainLowerBounds)
    {
        auto stats=Example(true); auto& p=stats.outputPresentation.passive;
        p.supported=true; p.running=true; p.fresh=true; p.matchedOutcomes=120;
        p.confirmedDrops=3; p.unexpectedDrops=2; p.holdIncidents=1; p.excessRefreshes=3; p.coverageGaps=1;
        auto rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"2 / >=3 (observed)");
        Assert::IsTrue(Find(rows,L"Display glitches:").value==L">=1 excessive holds");
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"partial")!=std::wstring::npos);
        VerifyNative(stats);
        p.running=false; p.fresh=false;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"2 / >=3 (recorded)");
        stats.playbackCounterResetSerial=5; p.appliedResetSerial=4;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"reset pending");
        Assert::IsTrue(Find(rows,L"Display glitches:").value==L"reset pending");
    }
    TEST_METHOD(PassiveDisplayEvidenceDistinguishesHealthyCollectorFromMissingTimestamps)
    {
        auto stats=Example(true); auto& p=stats.outputPresentation.passive;
        p.supported=true; p.running=true; p.verifiedIntervals=100;
        p.matchedOutcomes=33364; p.coverageGaps=1842;
        p.holdIncidents=1; p.excessRefreshes=1; p.eventAgeMilliseconds=3000;
        auto rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"0 / >=1 (recorded)");
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"no recent display timestamps")!=std::wstring::npos);
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"unavailable")==std::wstring::npos);
        VerifyNative(stats);
        p.eventAgeMilliseconds=50;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"cadence evidence gap")!=std::wstring::npos);
        p.running=false; p.errorCode=ERROR_BROKEN_PIPE;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"observer unavailable")!=std::wstring::npos);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"0 / >=1 (recorded)");
        p.running=true; p.errorCode=0; p.fresh=true;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"0 / >=1 (observed)");
        Assert::IsTrue(Find(rows,L"Display evidence:").value.find(L"observing")!=std::wstring::npos);
    }
    TEST_METHOD(PassiveDisplayUnavailableNeverLooksLikeMeasuredCleanZero)
    {
        auto stats=Example(true); auto& p=stats.outputPresentation.passive;
        p.supported=true; p.errorCode=ERROR_ACCESS_DENIED;
        auto rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"unavailable (access denied)");
        p.errorCode=0; p.running=true;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"unavailable (measuring)");
        p.fresh=true; p.verifiedIntervals=1;
        rows=StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows,L"Display D/R:").value==L"0 / >=0 (observed)");
        VerifyNative(stats);
    }
    TEST_METHOD(WarmupAndUnavailableStatusesRemainVisibleAndDoNotFabricateNumbers)
    {
        auto stats = Example(true); stats.renderLoadKnown = true;
        stats.displayRefreshRate = 0.0; stats.displayRefreshRateStatus = L"quarantined";
        stats.presentationTimingStatus = L"warming"; stats.sceneDetectMode = L"Repeat/Drop";
        stats.sceneTimingStatus = L"warming"; stats.renderLoadSettling = true;
        const auto rows = StatsOverlayWindow::BuildStatsRows(stats);
        Assert::IsTrue(Find(rows, L"- Display Rate:").value.find(L"quarantined") != std::wstring::npos);
        Assert::IsTrue(Find(rows, L"Delay:").value == L"---");
        Assert::IsTrue(Find(rows, L"GPU frame:").value == L"settling..." ||
            Find(rows, L"GPU frame:").value == L"measuring...");
        VerifyNative(stats);
    }
};
}
