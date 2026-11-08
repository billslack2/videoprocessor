// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: the wizard's step machine (design "Wizard",
// "Safety"). One session = one run:
//
//   Idle -> OpeningMeter -> [MeterCalibration] -> Placement -> [WarmUp]
//        -> Measuring -> Fitting -> [Verifying] -> Review -> Applied | Discarded
//   any step -> Cancelled / Failed (patterns off, meter closed)
//
// The UI (Config, over RPC) sends commands - Start, Continue (the user did
// what the step asked: sensor calibrated, meter placed), Skip (warm-up),
// Cancel, Apply, Discard - and reads Status snapshots. A worker thread calls
// Step() until Finished(); every Step does one bounded unit of work (open the
// meter, one patch, one aim reading, a second of warm-up), so a command is
// seen within one reading.
//
// Patterns go through IPatternSink (the VP renderer: exact codes, the
// sigma-delta and dither bypassed for the patch only). The output format
// (8-bit, 8-in-10, 10-bit; limited / full; SDR / PQ) is read from the sink
// when measuring starts and every code is chosen in it (PatternCodes.h).
// Nothing assumes a refresh rate or a resolution.

#include "CalibrationConfig.h"
#include "CalibrationRun.h"
#include "DisplayModel.h"
#include "Meter.h"
#include "MeterCatalog.h"
#include "../pattern_lib/PatternCodes.h"
#include "../pattern_lib/PatternPlan.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Calibration
{
	class IPatternSink
	{
	public:
		virtual ~IPatternSink() = default;
		virtual bool GetOutput(PatternOutputInfo& info) = 0;
		// True only while a real renderer is attached (it may still be starting, so GetOutput can
		// report unavailable for a moment). A sink with no renderer behind it never waits.
		virtual bool RendererAttached() { return false; }
		// request.active false = back to video. False with error when refused.
		virtual bool Show(const PatternRequest& request, std::string& error) = 0;
	};

	class ISessionClock
	{
	public:
		virtual ~ISessionClock() = default;
		virtual double Now() = 0;               // seconds, monotonic
		virtual void Sleep(double seconds) = 0;
		virtual RunTimestamp LocalTime() = 0;
	};

	// The real clock (steady clock, Sleep, local time).
	std::unique_ptr<ISessionClock> CreateSystemClock();

	enum class SessionStep
	{
		Idle,
		OpeningMeter,
		MeterCalibration,
		Placement,
		WarmUp,
		Measuring,
		Fitting,
		Verifying,
		Review,
		Applied,
		Discarded,
		Cancelled,
		Failed,
		// Phase 2 (appended: the text names, not the numbers, cross the RPC).
		UserAction,   // waiting for the user to do what the instruction says (room lights), then Continue
		Done,         // a verify-only run finished (nothing to apply)
	};

	const char* ToString(SessionStep step);
	bool IsFinal(SessionStep step);

	struct SessionStatus
	{
		SessionStep step = SessionStep::Idle;
		std::string kind = "calibration"; // calibration, led, ambient, hdr_verify
		std::string instruction;          // what the user should do now
		std::string message;              // the last event / error
		int patchIndex = 0, patchCount = 0;
		int verifyIndex = 0, verifyCount = 0;
		double secondsRemaining = 0.0;    // estimate for the current phase
		double warmupRemaining = 0.0;
		bool haveReading = false;
		Vec3 lastXyz = { 0, 0, 0 };
		std::string format;               // the output format patches are in
		std::string meter;
		std::string runFolder;
		std::string review;               // the summary shown before Apply
		std::string diff;                 // the configuration edits Apply makes
		std::string backup;               // after Apply: the backups made
		double verifyMean = -1.0, verifyMax = -1.0;
		bool verifyPass = false;
		int measured = 0, skipped = 0, floorLimited = 0;
		double driftPercent = 0.0;
	};

	// Text form for the RPC ("key: value" lines, multi-line values escaped).
	std::string FormatStatus(const SessionStatus& status);
	bool ParseStatus(const std::string& text, SessionStatus& status);

	struct SessionEnvironment
	{
		std::string vpRoot;               // the folder of VideoProcessor.cfg
		std::string configPath;
		std::string activeColorSection;   // the live Color / Output profile ([vprenderer.color.<name>])
		std::string displayName;          // settings display_name, else the monitor's name
		double displayLatencySeconds = 0.0;
	};

	// What the controller runs: the LUT calibration (CalibrationSession) or a
	// phase 2 run (CalibrationRunBase.h). Commands from any thread; Step()
	// on the controller's worker until Finished().
	class ICalibrationRun
	{
	public:
		virtual ~ICalibrationRun() = default;
		virtual void Start() = 0;
		virtual void Continue() = 0;
		virtual void Skip() = 0;
		virtual void Cancel() = 0;
		virtual void Apply() = 0;
		virtual void Discard() = 0;
		virtual void Touch() = 0;
		virtual bool Step() = 0;
		virtual bool Finished() const = 0;
		virtual SessionStatus Status() const = 0;
		virtual double SecondsSinceTouch() = 0;
		// Arguments of a command ("key: value" lines), before it is sent: the
		// ambient check's lux for Continue. Default: ignored.
		virtual void SetArguments(const std::string& arguments) { (void)arguments; }
	};

	class CalibrationSession final : public ICalibrationRun
	{
	public:
		CalibrationSession(const CalibrationSettings& settings, const SessionEnvironment& environment,
			std::unique_ptr<IMeter> meter, double meterFloorNits, IPatternSink& patterns, ISessionClock& clock);
		~CalibrationSession() override;

		// Commands (any thread).
		void Start() override;
		void Continue() override;
		void Skip() override;
		void Cancel() override;
		void Apply() override;
		void Discard() override;
		// The UI is alive (the design's "Config silent for 60 s" safety).
		void Touch() override;

		// The worker: one unit of work. False once finished.
		bool Step() override;
		bool Finished() const override;
		SessionStatus Status() const override;
		// The client last touched the session this long ago.
		double SecondsSinceTouch() override;

		// Results (after Fitting).
		const DisplayModel& Model() const { return model; }
		const Lut3D& Lut(int slot) const { return luts[slot]; }
		const VerifyReport& Verify() const { return verify; }
		const std::vector<JournalEntry>& Journal() const { return journal; }
		const RunFolder& Folder() const { return folder; }

		// The plan (tests; built at Start).
		const std::vector<Patch>& Plan() const { return plan; }

	private:
		enum class Command { None, Continue, Skip, Cancel, Apply, Discard };

		Command TakeCommand();
		bool CancelPending();
		void SetStep(SessionStep step, const std::string& instruction);
		void SetMessage(const std::string& message);
		void Fail(const std::string& message);
		void EndPatterns();
		void Finish(SessionStep step, const std::string& message);

		bool ShowPatch(const PatchCodes& codes, double expectedY, std::string& error);
		// Reads one patch with the near-black strategy; false when the session
		// must stop or wait (the step was changed).
		bool MeasurePatch(const Patch& patch, const PatchCodes& codes, double expectedY, JournalEntry& entry);
		void DoOpenMeter();
		void DoPlacement();
		void DoWarmUp();
		void DoMeasure();
		void DoFit();
		void DoVerify();
		void DoReview();
		void DoApply();
		void DoDiscard();
		void WriteJournal(const JournalEntry& entry);
		void WriteIndex(const char* status);
		double ExpectedY(const Vec3& device) const;
		std::vector<Measurement> DriftCorrected() const;
		std::string FormatName() const;

		CalibrationSettings settings;
		SessionEnvironment environment;
		std::unique_ptr<IMeter> meter;
		double meterFloor;
		IPatternSink& patterns;
		ISessionClock& clock;

		mutable std::mutex mutex;
		SessionStatus status;
		Command pending = Command::None;
		double lastTouch = 0.0;

		// Worker state.
		SessionStep resumeStep = SessionStep::Placement;   // after a meter calibration
		PatternOutputInfo format;
		std::vector<Patch> plan;
		size_t planIndex = 0;
		std::vector<Patch> verifyPlan;
		size_t verifyIndex = 0;
		std::vector<Measurement> measurements;
		std::vector<int> measurementPlanIndex;
		std::vector<std::pair<int, double>> driftReferences;   // plan index, white Y
		std::vector<VerifyEntry> verifyEntries;
		std::vector<JournalEntry> journal;
		double whiteY = 0.0;
		double previousY = 0.0;
		double warmupStart = -1.0;
		double phaseStart = 0.0;
		uint64_t generation = 0;
		int retries = 0;
		bool reopened = false;
		bool patternsOn = false;
		bool placementPatchShown = false;
		bool indexWritten = false;
		RunTimestamp stamp;
		RunFolder folder;
		DisplayModel model;
		CalibrationTarget targets[3];
		Lut3D luts[3];
		bool slotWritten[3] = { false, false, false };
		VerifyReport verify;
		bool verified = false;
		int verifySlot = 0;
	};

	// The plan the settings ask for (PatternPlan's BuildPlan).
	PlanKind ToPlanKind(PlanChoice choice);
	// The session's patch order: a white drift reference first (the reference
	// for everything after it), the greyscale from white down (BTB last), then
	// primaries / secondaries and the cube, with a drift reference every
	// driftInterval patches and at the end.
	std::vector<Patch> BuildSessionPlan(PlanChoice choice, int driftInterval);
}
