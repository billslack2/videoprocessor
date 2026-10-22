#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class DisplayRefreshRateDecision
{
	Accepted,
	Warming,
	Quarantined,
	Unavailable
};

enum class DisplayRefreshRateReason
{
	Accepted,
	NoSamples,
	InsufficientSamples,
	Stabilizing,
	NonFiniteCandidate,
	StaleCandidate,
	InvalidRawCadence,
	InvalidIntervalRange,
	RawCadenceExceedsCandidate,
	HarmonicMismatch,
	NominalMismatch,
	UnexplainedCompensation
};

struct DisplayRefreshRateInput
{
	double candidateRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	double nominalRateHz = 0.0;
	double minimumWaitIntervalMs = 0.0;
	double maximumWaitIntervalMs = 0.0;
	uint64_t compensatedIntervals = 0;
	uint64_t rawWaitIntervals = 0;
	// Short current-rate evidence used to begin the one deterministic
	// reset/prefill sequence. It deliberately precedes long phase confidence.
	double startupObservationSeconds = 0.0;
	// Duration of the current, post-transition observation window. This is
	// distinct from the longer phase-correction stability requirement.
	double readinessObservationSeconds = 0.0;
	bool fresh = false;
	bool stable = false;
};

struct DisplayRefreshRateResult
{
	DisplayRefreshRateDecision decision =
		DisplayRefreshRateDecision::Unavailable;
	DisplayRefreshRateReason reason =
		DisplayRefreshRateReason::NoSamples;
	double selectedRateHz = 0.0;
	double startupRateHz = 0.0;
	bool startupValidated = false;
	// The candidate passed all freshness, raw-cadence, interval-range, harmonic,
	// and nominal-family checks. It may still be in the longer stabilization
	// period required for phase-sensitive correction. Output readiness can use
	// this bounded, already-validated observation without treating it as HDMI
	// lock proof.
	double readinessRateHz = 0.0;
	bool readinessValidated = false;
	bool shouldRecalculate = false;
};

struct DisplayRefreshRational
{
	uint32_t numerator = 0;
	uint32_t denominator = 0;
};

enum class DisplayRefreshModeSelectionPath
{
	None,
	ExactOrClose,
	ClosestInRange
};

struct DisplayRefreshModeSelection
{
	DisplayRefreshModeSelectionPath path =
		DisplayRefreshModeSelectionPath::None;
	DisplayRefreshRational selected{};
	double requestedRateHz = 0.0;
	double selectedRateHz = 0.0;
	double differenceHz = 0.0;
	bool doubledRate = false;
};

// A complete permitted output mode. Unlike DisplayMode (which describes a
// captured source), this is the progressive raster and exact refresh timing
// requested from the Windows display path.
struct DisplayOutputMode
{
	uint32_t width = 0;
	uint32_t height = 0;
	DisplayRefreshRational refreshRate{};
};

struct DisplayOutputModeChoice
{
	DisplayOutputMode mode{};
	DisplayRefreshModeSelectionPath ratePath =
		DisplayRefreshModeSelectionPath::None;
	bool doubledRate = false;
    bool configuredFallback = false;
    bool exactCadenceRatio = false;
};

// Accepted forms are WIDTHxHEIGHTpRATE and the documented 16:9 shorthand
// HEIGHTpRATE. Decimal 23.976/29.97/59.94 and integer 23/29/59 spellings map
// to the broadcast rationals 24000/1001, 30000/1001, and 60000/1001.
bool ParseDisplayOutputMode(const std::string& text,
	DisplayOutputMode& mode, std::string& error);
bool ParseDisplayOutputModeList(const std::string& text,
	std::vector<DisplayOutputMode>& modes, std::string& error);
std::string FormatDisplayOutputMode(const DisplayOutputMode& mode);
// Compact UI labels never replace the exact timing stored in configuration.
std::string FormatDisplayOutputModeLabel(const DisplayOutputMode& mode);
std::vector<DisplayOutputMode> DisplayOutputModePickerChoices(
    const std::vector<DisplayOutputMode>& available, uint32_t preferredWidth,
    uint32_t preferredHeight, bool recommendedOnly);
bool DisplayOutputModesExactlyEqual(const DisplayOutputMode& first,
	const DisplayOutputMode& second);

// The preferred refresh family follows VP-0167 native-first/two-pass
// ranking. Within one eligible rational rate, configured mode order decides
// which complete raster is attempted first. Exact integer cadence multiples
// and divisors follow (multiples first, then smaller factors); configured order
// breaks ties. Other available allowed modes follow in configured order, so
// one allowed mode is fixed output for any valid source cadence.
// Unavailable modes are ignored.
std::vector<DisplayOutputModeChoice> RankDisplayOutputModesForInput(
	const DisplayRefreshRational& input, bool interlaced,
	const std::vector<DisplayOutputMode>& configured,
	const std::vector<DisplayOutputMode>& available);
// Platform-independent read-back contract used by the Windows mode transaction.
enum class DisplayOutputScan { Unknown, Progressive, InterlacedUpper, InterlacedLower };
struct DisplayOutputModeState
{
    uint32_t sourceWidth = 0, sourceHeight = 0;
    uint32_t targetWidth = 0, targetHeight = 0;
    DisplayRefreshRational refreshRate{};
    DisplayOutputScan scan = DisplayOutputScan::Progressive;
    bool targetKnown = false;
};
bool DisplayOutputModeMatches(const DisplayOutputModeState& expected,
    const DisplayOutputModeState& observed);
bool DisplayOutputModeChangesRaster(const DisplayOutputModeState& before,
    const DisplayOutputModeState& desired);
bool DisplayOutputModePrefersGdi(const DisplayOutputModeState& before,
    const DisplayOutputModeState& desired);
class DisplayOutputModeVerifier
{
public:
    explicit DisplayOutputModeVerifier(const DisplayOutputModeState& expected) : expected_(expected) {}
    bool Observe(bool querySucceeded, const DisplayOutputModeState& observed);
private:
    DisplayOutputModeState expected_;
    unsigned int consecutive_ = 0;
};

enum class RefreshRateSwitchMode
{
	Never,
	FullscreenOnly,
	Always
};

double DisplayRefreshRateHz(const DisplayRefreshRational& rate);

// Select a display mode in two intentional passes. The first pass accepts an
// exactly equivalent rational or tight driver rounding. Only if that fails is
// the closest cadence-family candidate accepted within the bounded fallback
// window; this must not turn a 24 Hz source into an unrelated 50/60 Hz mode.
DisplayRefreshModeSelection SelectDisplayRefreshMode(
	const DisplayRefreshRational& requested,
	const std::vector<DisplayRefreshRational>& candidates);

// Rank native-rate candidates before doubled-rate fallbacks for progressive
// 25/29.97/30 input (the existing >24.1 and <31 Hz family). Interlaced input
// keeps field-rate output only. Each group retains the normal rational/tolerance
// ranking; returning the complete list lets switch failures try the next mode.
std::vector<DisplayRefreshModeSelection> RankDisplayRefreshModesForInput(
	const DisplayRefreshRational& input, bool interlaced,
	const std::vector<DisplayRefreshRational>& candidates);

bool DisplayRefreshRatesExactlyEqual(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

// Windows and GPU drivers can report a refresh rate that differs from the
// requested rational by a few millihertz after a display-topology rebuild.
// This comparison is deliberately for restore acknowledgement only; mode
// selection continues to require exact rational equality.
bool DisplayRefreshRatesEquivalentForRestore(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

// Refresh switching is display-global. Only a top-level presentation surface
// may own that transition; an embedded preview must not change desktop timing
// while a fullscreen renderer is being retired or replaced.
bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow,
	RefreshRateSwitchMode mode);

// Compatibility overload for existing callers/tests: the former checked
// setting maps to the new Fullscreen Only policy.
bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow);

class DisplayRefreshRestoreVerifier
{
public:
	explicit DisplayRefreshRestoreVerifier(
		DisplayRefreshRational expected,
		unsigned int requiredConsecutiveMatches = 2)
		: m_expected(expected),
		  m_requiredConsecutiveMatches(
			requiredConsecutiveMatches == 0 ? 1 : requiredConsecutiveMatches)
	{
	}

	bool Observe(bool querySucceeded, DisplayRefreshRational observed);
	unsigned int ConsecutiveMatches() const
	{
		return m_consecutiveMatches;
	}

private:
	DisplayRefreshRational m_expected;
	unsigned int m_requiredConsecutiveMatches = 2;
	unsigned int m_consecutiveMatches = 0;
};

DisplayRefreshRateResult EvaluateDisplayRefreshRate(
	const DisplayRefreshRateInput& input);
const char* ToString(DisplayRefreshRateDecision decision);
const char* ToString(DisplayRefreshRateReason reason);
