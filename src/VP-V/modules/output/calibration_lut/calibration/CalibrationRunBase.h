// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration phase 2:
// the plumbing the phase 2 runs share - the Dynamic Drive (LED)
// characterisation (LedRun.h), the ambient-light check (AmbientCheck.h) and
// the HDR (PQ) verify (HdrVerify.h). Same wizard steps as the LUT session
// (CalibrationSession.h) up to placement:
//
//   Idle -> OpeningMeter -> [MeterCalibration] -> Placement -> [WarmUp]
//        -> (the run's own steps: Measuring / UserAction / Fitting)
//        -> Review -> Applied | Discarded        (a suggestion to apply)
//        -> Done                                 (verify only)
//   any step -> Cancelled / Failed
//
// Finishing always calls OnFinishing() first (the LED run restores full light
// and the CNTR default there, before the patterns end and VP's own Dynamic
// Drive takes the projector back), then returns the renderer to video and
// closes the meter.
//
// Apply follows phase 1's rules: the edits are previewed as a diff, then a
// timestamped byte backup beside the configuration and a copy in the run's
// before\ folder are made before any byte changes; only the named keys
// change (comments, unknown keys and order kept); the candidate must pass
// VP's validation, else nothing is written and the review says why.

#include "CalibrationSession.h"

#include <functional>
#include <string>
#include <vector>

namespace ConfigEditorCore
{
	struct ConfigDocument;
}

namespace Calibration
{
	// <vp>\luts\calibration\<display>\<timestamp>-<suffix>\ (raw.jsonl, the
	// run's results, report, before\). Phase 1's index.json lists it.
	RunFolder BuildPhase2RunFolder(const std::string& vpRoot, const std::string& displayName,
		const RunTimestamp& stamp, const std::string& suffix);

	// The candidate check before any write (default: VP's own parser and
	// schemas, ConfigEditorCore::ValidateCandidate). Tests substitute one to
	// model a build that knows a key this one does not.
	using CandidateValidator = std::function<bool(const ConfigEditorCore::ConfigDocument& document, std::string& error)>;
	CandidateValidator DefaultCandidateValidator();

	// The diff the edits would make, validated, without writing anything.
	// diff.ok false with error when the configuration cannot take them.
	ConfigDiff PreviewKeyEdits(const std::string& configPath, const std::vector<ConfigEdit>& edits,
		const CandidateValidator& validate, std::string& error);
	// Backups first, then the key-only edits, validation and SaveSafely.
	ApplyOutcome ApplyKeyEdits(const std::string& configPath, const std::vector<ConfigEdit>& edits,
		const RunFolder& folder, const RunTimestamp& stamp, const CandidateValidator& validate);

	// "key: value" argument lines (the RPC's command arguments).
	bool ArgumentValue(const std::string& arguments, const std::string& key, std::string& value);

	class MeterRun : public ICalibrationRun
	{
	public:
		MeterRun(const char* kind, const CalibrationSettings& settings, const SessionEnvironment& environment,
			std::unique_ptr<IMeter> meter, double meterFloorNits, IPatternSink& patterns, ISessionClock& clock);
		// Derived classes call Shutdown() in their own destructor (it calls the
		// virtual OnFinishing while the derived object still exists).
		~MeterRun() override;

		void Start() override;
		void Continue() override;
		void Skip() override;
		void Cancel() override;
		void Apply() override;
		void Discard() override;
		void Touch() override;
		bool Step() override;
		bool Finished() const override;
		SessionStatus Status() const override;
		double SecondsSinceTouch() override;
		void SetArguments(const std::string& arguments) override;

		const RunFolder& Folder() const { return folder; }
		const PatternOutputInfo& Format() const { return format; }

	protected:
		enum class Command { None, Continue, Skip, Cancel, Apply, Discard };
		enum class ReadOutcome { Ok, Unreadable, Stop };

		// ------------------------------------------------ for the runs
		virtual const char* FolderSuffix() const = 0;
		// Start: build the plan (status.patchCount); false with error refuses.
		virtual bool OnStart(std::string& error) { (void)error; return true; }
		// The first patch is on the screen (the renderer has paused video and
		// stopped VP's Dynamic Drive): the LED run opens the projector here.
		virtual bool OnPatternsStarted(std::string& error) { (void)error; return true; }
		// After placement (and the warm-up when the run wants one).
		virtual bool WantsWarmUp() const { return false; }
		virtual void BeginWork() = 0;
		// Every step that is not the shared open / calibrate / place / warm-up.
		virtual void StepWork(SessionStep step, Command command) = 0;
		// Before the patterns end on any finish (cancel, failure, apply, ...).
		virtual void OnFinishing(SessionStep step) { (void)step; }
		// Review: Apply / Discard.
		virtual void DoApply();
		virtual void DoDiscard();
		virtual std::string PlacementInstruction() const;

		// ------------------------------------------------- helpers
		Command TakeCommand();
		bool CancelPending();
		void SetStep(SessionStep step, const std::string& instruction);
		void SetMessage(const std::string& message);
		void SetProgress(int index, int count);
		void Fail(const std::string& message);
		void Finish(SessionStep step, const std::string& message);
		void Shutdown();
		void EndPatterns();
		// Shows a patch and waits for its presentation, then the settle time
		// (settleSeconds < 0: the display kind's, as phase 1).
		bool ShowPatch(const PatchCodes& codes, double expectedY, std::string& error, double settleSeconds = -1.0);
		PatchCodes Signal(double r, double g, double b) const;
		// Reads one patch with phase 1's near-black strategy.
		ReadOutcome MeasureCodes(double& elapsed, Vec3& xyz, int& reads, bool& floorLimited, std::string& note);
		// One reading, time-stamped at its end (the LED sweep's samples).
		ReadOutcome ReadOnce(Vec3& xyz, double& atSeconds, std::string& note);
		// Sleeps in short slices; false when a cancel arrived.
		bool Wait(double seconds);
		void WriteRaw(const std::string& jsonLine);
		void WriteIndex(const char* state);
		std::string FormatName() const;
		void SetReview(const std::string& review, const std::vector<ConfigEdit>& edits, const std::string& applyTarget);

		CalibrationSettings settings;
		SessionEnvironment environment;
		std::unique_ptr<IMeter> meter;
		double meterFloor;
		IPatternSink& patterns;
		ISessionClock& clock;
		CandidateValidator validator;

		mutable std::mutex mutex;
		SessionStatus status;
		Command pending = Command::None;
		std::string arguments;
		double lastTouch = 0.0;

		PatternOutputInfo format;
		RunTimestamp stamp;
		RunFolder folder;
		uint64_t generation = 0;
		bool patternsOn = false;
		bool patternsStartedHook = false;
		bool placementShown = false;
		bool reopened = false;
		bool indexWritten = false;
		bool shutDown = false;
		double whiteY = 0.0;
		double previousY = 0.0;
		double warmupStart = -1.0;
		double phaseStart = 0.0;
		SessionStep resumeStep = SessionStep::Placement;
		std::vector<ConfigEdit> pendingEdits;

	private:
		void DoOpenMeter();
		void DoPlacement();
		void DoWarmUp();
		bool HandleMeterAction(const MeterResult& r);
	};

	// JSON helpers for the runs' result files.
	std::string JsonNumber(double value, int decimals = 6);
	std::string JsonText(const std::string& text);
	std::string FixedText(double value, int decimals);
}
