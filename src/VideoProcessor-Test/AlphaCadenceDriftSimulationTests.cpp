#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaCadenceCorrectionPolicy.h>
#include <vprenderer/AlphaQueuePolicy.h>

#include "AlphaCadenceDriftSceneEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <random>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// A model of the matched-refresh render loop, one display refresh per
// iteration, driving the real AlphaCadenceCorrectionPolicy and the real
// AlphaQueuePolicy functions for a whole film while the capture clock runs a
// few ppm off the display clock. Each iteration: admit the source frames that
// have arrived; the post-render backlog check for the previous present
// (ShouldRecoverBacklog, flush to target, policy reset); dequeue (CanDequeue);
// evaluate the cadence policy on the post-pop depth; a drop skips the flagged
// frame and pops again at once; present for one display period.
//
// Not modelled: GPU timing, DXGI statistics (presentation debt is held at 2,
// as logged at 1:1, and evidence is unavailable for 0.25 s after a reset), the
// real scene detector (cuts come from a logged session or a fixed interval),
// present_every_vsync, motion blending.

namespace Tests
{
	namespace
	{
		enum class CutPattern
		{
			Logged,
			Every24Seconds,
			Every120Seconds,
			None
		};

		struct DriftScenario
		{
			double displayHz = 23.975223;
			double ppm = 14.6;
			size_t desired = 4;
			double minutes = 0.0; // 0 = the length of the logged session
			CutPattern cuts = CutPattern::Logged;
			double stallAtMinutes = -1.0;
			bool surplusAtStart = false;
		};

		struct DriftResult
		{
			uint64_t presents = 0;
			uint64_t correctorDropsAtCut = 0;
			uint64_t correctorDropsByDeadline = 0;
			uint64_t correctorRepeats = 0;
			uint64_t staleAgeRecoveries = 0;
			uint64_t renderStallRecoveries = 0;
			uint64_t backstopFramesDropped = 0;
			uint64_t unplannedRepeats = 0;
			size_t maxPostPopDepth = 0;
			double minDelayMs = 1.0e9;
			double maxDelayMs = 0.0;
			double meanDelayMs = 0.0;
			double surplusSeconds = 0.0;
			double longestSurplusSeconds = 0.0;
			double maxCutGapSeconds = 0.0;
			double resyncBoundSeconds = 0.0;
			double correctorReadySeconds = 0.0;
			double firstStaleAgeSeconds = -1.0;
			double lastStaleAgeSeconds = -1.0;
		};

		const wchar_t* CutName(CutPattern cuts)
		{
			switch (cuts)
			{
			case CutPattern::Logged: return L"logged";
			case CutPattern::Every24Seconds: return L"every 24 s";
			case CutPattern::Every120Seconds: return L"every 120 s";
			default: return L"none";
			}
		}

		DriftResult RunDrift(const DriftScenario& scenario)
		{
			using namespace AlphaQueuePolicy;
			const auto& logged = AlphaCadenceDriftSceneEvents::SEQUENCES;
			const size_t loggedCount = sizeof(logged) / sizeof(logged[0]);

			const double displayHz = scenario.displayHz;
			const double captureHz = displayHz * (1.0 + scenario.ppm / 1000000.0);
			const double displayPeriod = 1.0 / displayHz;
			const double sourcePeriod = 1.0 / captureHz;
			const size_t desired = NormalizeDesiredDepth(scenario.desired);
			const double seconds = scenario.minutes > 0.0
				? scenario.minutes * 60.0
				: static_cast<double>(logged[loggedCount - 1]) / captureHz;
			const uint64_t cutEvery =
				scenario.cuts == CutPattern::Every24Seconds
					? static_cast<uint64_t>(std::llround(24.0 * captureHz))
					: (scenario.cuts == CutPattern::Every120Seconds
						? static_cast<uint64_t>(std::llround(120.0 * captureHz))
						: 0);

			DriftResult result;
			if (scenario.cuts == CutPattern::Logged)
			{
				uint64_t gap = 0;
				for (size_t index = 1; index < loggedCount; ++index)
					gap = std::max(gap, logged[index] - logged[index - 1]);
				result.maxCutGapSeconds = static_cast<double>(gap) / captureHz;
			}
			else if (cutEvery != 0)
				result.maxCutGapSeconds = static_cast<double>(cutEvery) / captureHz;
			else
				result.maxCutGapSeconds = seconds;
			// Evidence returns 0.25 s after a reset; the policy then needs its
			// 600 rate samples, 120 evaluations to confirm a surplus and, after
			// a drop, 120 evaluations of cooldown.
			result.resyncBoundSeconds =
				0.25 + (600.0 + 120.0 + 120.0) / displayHz + 1.0;
			// The earliest the policy can act from a fresh start: evidence after
			// 0.25 s, then 600 rate samples and 120 evaluations of surplus.
			result.correctorReadySeconds = 0.25 + (600.0 + 120.0) / displayHz;

			std::mt19937 random(20261109u);
			std::uniform_real_distribution<double> jitter(0.0, 0.003);

			struct Frame
			{
				uint64_t sequence;
				double arrival;
			};
			std::deque<Frame> queue;
			uint64_t nextSequence = 1;
			double nextArrival = jitter(random);
			auto admit = [&](double now)
			{
				while (nextArrival <= now)
				{
					queue.push_back({ nextSequence, nextArrival });
					++nextSequence;
					nextArrival = static_cast<double>(nextSequence - 1) *
						sourcePeriod + jitter(random);
				}
			};

			AlphaCadenceCorrectionPolicy policy;
			uint64_t generation = 1;
			double evidenceStableAt = 0.25;
			uint32_t presentId = 0;
			uint64_t sceneEventId = 0;
			size_t loggedIndex = 0;
			bool prefillPending = true;
			const size_t prefillDepth =
				scenario.surplusAtStart ? desired + 1 : desired;
			bool stallDone = scenario.stallAtMinutes < 0.0;
			bool repeatPending = false;
			bool havePresented = false;
			size_t lastPostPopDepth = 0;
			double renderCycleMs = displayPeriod * 1000.0;
			double surplusStretch = 0.0;
			double delaySum = 0.0;
			uint64_t delaySamples = 0;
			double now = 0.0;

			while (now < seconds)
			{
				admit(now);
				if (havePresented)
				{
					const double oldestMs = queue.empty() ? 0.0 :
						(now - queue.front().arrival) * 1000.0;
					if (ShouldRecoverBacklog(queue.size(), desired, oldestMs,
						renderCycleMs, captureHz))
					{
						if (renderCycleMs >= RenderStallThresholdMs(captureHz))
							++result.renderStallRecoveries;
						else
						{
							++result.staleAgeRecoveries;
							if (result.firstStaleAgeSeconds < 0.0)
								result.firstStaleAgeSeconds = now;
							result.lastStaleAgeSeconds = now;
						}
						while (queue.size() > desired)
						{
							queue.pop_front();
							++result.backstopFramesDropped;
						}
						policy.Reset(++generation);
						evidenceStableAt = now + 0.25;
						repeatPending = false;
					}
				}

				double presentSeconds = displayPeriod;
				if (!stallDone && now >= scenario.stallAtMinutes * 60.0)
				{
					stallDone = true;
					presentSeconds = 2.0 * displayPeriod;
				}

				if (repeatPending)
				{
					repeatPending = false;
				}
				else
				{
					for (;;)
					{
						const bool canDequeue = prefillPending
							? queue.size() >= prefillDepth &&
								CanDequeue(queue.size(), desired, true)
							: CanDequeue(queue.size(), desired, false);
						if (!canDequeue)
						{
							if (!prefillPending)
								++result.unplannedRepeats;
							break;
						}
						prefillPending = false;
						const Frame frame = queue.front();
						queue.pop_front();
						const size_t postPopDepth = queue.size();

						bool cut = false;
						if (scenario.cuts == CutPattern::Logged)
						{
							while (loggedIndex < loggedCount &&
								logged[loggedIndex] < frame.sequence)
								++loggedIndex;
							cut = loggedIndex < loggedCount &&
								logged[loggedIndex] == frame.sequence;
						}
						else if (cutEvery != 0)
							cut = frame.sequence % cutEvery == 0;

						const bool stable = now >= evidenceStableAt;
						AlphaCadenceCorrectionInput input;
						input.enabled = true;
						input.generation = generation;
						input.presentationEvidence = stable
							? AlphaPresentationEvidence::Stable
							: AlphaPresentationEvidence::Unavailable;
						input.captureRateHz = captureHz;
						input.displayRateHz = displayHz;
						input.queueDepth = postPopDepth;
						input.desiredQueueDepth = desired;
						input.oldestQueuedAgeMs = queue.empty() ? 0.0 :
							(now - queue.front().arrival) * 1000.0;
						input.presentationDebt = stable ? 2 : 0;
						input.lastPresentId = presentId;
						input.safeSceneBoundary = cut;
						input.sceneEventId = cut ? ++sceneEventId : 0;
						input.sourceSequence = frame.sequence;
						const auto decision = policy.Evaluate(input);

						if (decision.action == AlphaCadenceAction::Drop)
						{
							if (decision.deadlineFallback)
								++result.correctorDropsByDeadline;
							else
								++result.correctorDropsAtCut;
							continue;
						}
						if (decision.action == AlphaCadenceAction::Repeat)
						{
							++result.correctorRepeats;
							repeatPending = true;
						}

						lastPostPopDepth = postPopDepth;
						result.maxPostPopDepth =
							std::max(result.maxPostPopDepth, postPopDepth);
						const double delayMs = (now - frame.arrival) * 1000.0;
						result.minDelayMs = std::min(result.minDelayMs, delayMs);
						result.maxDelayMs = std::max(result.maxDelayMs, delayMs);
						delaySum += delayMs;
						++delaySamples;
						break;
					}
				}

				if (!prefillPending)
				{
					havePresented = true;
					++presentId;
					++result.presents;
					if (lastPostPopDepth >= desired)
					{
						surplusStretch += presentSeconds;
						result.surplusSeconds += presentSeconds;
						result.longestSurplusSeconds =
							std::max(result.longestSurplusSeconds, surplusStretch);
					}
					else
						surplusStretch = 0.0;
				}
				renderCycleMs = presentSeconds * 1000.0;
				now += presentSeconds;
			}
			result.meanDelayMs = delaySamples != 0
				? delaySum / static_cast<double>(delaySamples) : 0.0;
			return result;
		}

		void Report(const DriftScenario& scenario, const DriftResult& result)
		{
			wchar_t line[768];
			swprintf_s(line,
				L"display %.6f Hz, %+.1f ppm, target %zu, cuts %s%s%s: "
				L"presents %llu; corrector drops %llu at cuts, %llu by deadline, "
				L"repeats %llu; backstop %llu stale-age, %llu render-stall, "
				L"%llu frames; unplanned repeats %llu; max post-pop depth %zu; "
				L"delay %.1f to %.1f ms, mean %.1f; surplus %.0f s, longest %.1f s "
				L"(bound %.1f s); stale-age backstop first %.1f s, last %.1f s "
				L"(-1 = none; corrector ready %.1f s)\n",
				scenario.displayHz, scenario.ppm, scenario.desired,
				CutName(scenario.cuts),
				scenario.stallAtMinutes >= 0.0 ? L", stall" : L"",
				scenario.surplusAtStart ? L", surplus at start" : L"",
				static_cast<unsigned long long>(result.presents),
				static_cast<unsigned long long>(result.correctorDropsAtCut),
				static_cast<unsigned long long>(result.correctorDropsByDeadline),
				static_cast<unsigned long long>(result.correctorRepeats),
				static_cast<unsigned long long>(result.staleAgeRecoveries),
				static_cast<unsigned long long>(result.renderStallRecoveries),
				static_cast<unsigned long long>(result.backstopFramesDropped),
				static_cast<unsigned long long>(result.unplannedRepeats),
				result.maxPostPopDepth,
				result.minDelayMs, result.maxDelayMs, result.meanDelayMs,
				result.surplusSeconds, result.longestSurplusSeconds,
				result.maxCutGapSeconds + result.resyncBoundSeconds,
				result.firstStaleAgeSeconds, result.lastStaleAgeSeconds,
				result.correctorReadySeconds);
			Logger::WriteMessage(line);
		}

		// Every scenario is run and reported before any assertion, so a
		// failing build still prints its whole table.
		struct Expectations
		{
			std::wstring failures;

			void Expect(bool ok, const DriftScenario& scenario,
				const wchar_t* what)
			{
				if (ok)
					return;
				wchar_t line[256];
				swprintf_s(line,
					L"[display %.6f Hz, %+.1f ppm, target %zu, cuts %s%s%s] %s\n",
					scenario.displayHz, scenario.ppm, scenario.desired,
					CutName(scenario.cuts),
					scenario.stallAtMinutes >= 0.0 ? L", stall" : L"",
					scenario.surplusAtStart ? L", surplus at start" : L"",
					what);
				failures += line;
			}

			void AssertNone() const
			{
				if (!failures.empty())
					Assert::Fail(failures.c_str());
			}
		};

		// With scene cuts and a source faster than the display: the corrector
		// removes each surplus source at a cut, the backstop never fires on
		// age, drift alone never queues two surplus sources, and a surplus waits
		// at most one cut interval plus the re-synchronisation time. A surplus
		// queued at start sits at the backstop's age limit before the policy
		// can act; one stale-age recovery is allowed then, and none after.
		void ExpectCorrectedAtCuts(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result)
		{
			expectations.Expect(result.correctorDropsAtCut >= 1, scenario,
				L"the corrector never dropped at a cut");
			if (scenario.surplusAtStart)
				expectations.Expect(result.staleAgeRecoveries <= 1 &&
					result.lastStaleAgeSeconds < result.correctorReadySeconds,
					scenario,
					L"the backstop fired on queue age after the corrector was ready");
			else
				expectations.Expect(result.staleAgeRecoveries == 0, scenario,
					L"the backstop fired on queue age");
			// A stall or a surplus queued at start adds one source; landing on
			// top of a drift surplus it makes two (at 60 Hz neither a missed
			// refresh nor two surplus sources reaches the backstop's limits).
			const bool injected =
				scenario.stallAtMinutes >= 0.0 || scenario.surplusAtStart;
			expectations.Expect(result.maxPostPopDepth <=
				scenario.desired + (injected ? 1 : 0), scenario,
				injected ? L"the queue held three surplus sources"
					: L"the queue held two surplus sources");
			expectations.Expect(result.longestSurplusSeconds <=
				result.maxCutGapSeconds + result.resyncBoundSeconds, scenario,
				L"a surplus outlasted one cut interval plus re-synchronisation");
		}
	}

	TEST_CLASS(AlphaCadenceDriftSimulationTests)
	{
	public:
		TEST_METHOD(LoggedFilmKeepsRestDelayAtEveryTarget)
		{
			Expectations expectations;
			for (size_t desired = 1; desired <= 5; ++desired)
			{
				DriftScenario scenario;
				scenario.desired = desired;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectCorrectedAtCuts(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		TEST_METHOD(CommonRatesAndDriftsAtTargetFour)
		{
			const double rates[] =
			{
				24000.0 / 1001.0, 24.0, 25.0, 50.0, 60000.0 / 1001.0, 60.0
			};
			const double drifts[] = { 14.6, 50.0, 150.0, -14.6 };
			Expectations expectations;
			for (const double rate : rates)
			{
				for (const double ppm : drifts)
				{
					DriftScenario scenario;
					scenario.displayHz = rate;
					scenario.ppm = ppm;
					scenario.minutes = 100.0;
					scenario.cuts = CutPattern::Every24Seconds;
					const auto result = RunDrift(scenario);
					Report(scenario, result);
					if (ppm > 0.0)
					{
						ExpectCorrectedAtCuts(expectations, scenario, result);
						continue;
					}
					// A slower source drains at the dequeue wait; nothing
					// for the corrector or the backstop to do.
					expectations.Expect(result.correctorDropsAtCut +
						result.correctorDropsByDeadline +
						result.correctorRepeats == 0, scenario,
						L"the corrector acted on a slower source");
					expectations.Expect(result.staleAgeRecoveries == 0,
						scenario, L"the backstop fired on queue age");
					expectations.Expect(
						result.maxPostPopDepth <= scenario.desired, scenario,
						L"the queue held two surplus sources");
				}
			}
			expectations.AssertNone();
		}

		TEST_METHOD(CutSpacingSetsHowLongASurplusWaits)
		{
			const CutPattern patterns[] =
			{
				CutPattern::Logged, CutPattern::Every24Seconds,
				CutPattern::Every120Seconds
			};
			Expectations expectations;
			for (const CutPattern cuts : patterns)
			{
				DriftScenario scenario;
				scenario.cuts = cuts;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectCorrectedAtCuts(expectations, scenario, result);
			}

			// No cut at all: the corrector has nowhere to hide a drop and the
			// backstop catches the drift, as before.
			DriftScenario noCuts;
			noCuts.cuts = CutPattern::None;
			noCuts.minutes = 150.0;
			const auto result = RunDrift(noCuts);
			Report(noCuts, result);
			expectations.Expect(result.correctorDropsAtCut == 0, noCuts,
				L"the corrector dropped at a cut that does not exist");
			expectations.Expect(result.staleAgeRecoveries >= 1, noCuts,
				L"the backstop never caught the drift");
			expectations.AssertNone();
		}

		TEST_METHOD(ResynchronisesAfterStallAndAfterSurplusAtStart)
		{
			const double rates[] = { 23.975223, 60000.0 / 1001.0 };
			Expectations expectations;
			for (const double rate : rates)
			{
				const CutPattern cuts = rate < 30.0
					? CutPattern::Logged : CutPattern::Every24Seconds;

				DriftScenario stall;
				stall.displayHz = rate;
				stall.cuts = cuts;
				stall.minutes = rate < 30.0 ? 0.0 : 100.0;
				stall.stallAtMinutes = 20.0;
				const auto stallResult = RunDrift(stall);
				Report(stall, stallResult);
				ExpectCorrectedAtCuts(expectations, stall, stallResult);

				DriftScenario surplus;
				surplus.displayHz = rate;
				surplus.cuts = cuts;
				surplus.minutes = rate < 30.0 ? 0.0 : 100.0;
				surplus.surplusAtStart = true;
				const auto surplusResult = RunDrift(surplus);
				Report(surplus, surplusResult);
				ExpectCorrectedAtCuts(expectations, surplus, surplusResult);
			}
			expectations.AssertNone();
		}
	};
}
