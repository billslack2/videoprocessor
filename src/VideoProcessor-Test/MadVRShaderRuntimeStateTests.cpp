#include "pch.h"

#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(MadVRShaderRuntimeStateTests)
	{
	public:
		TEST_METHOD(ScopeContentUsesLinearPassthrough)
		{
			const MadVRNlsMappingDecision decision =
				EvaluateMadVRNlsMapping(true, 2.35, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SCOPE_PASSTHROUGH),
				static_cast<int>(decision.mode));
			Assert::AreEqual(1.0, decision.stretchRatio, 0.000001);
		}

		TEST_METHOD(ImaxContentUsesNonlinearHorizontalMapping)
		{
			const MadVRNlsMappingDecision decision =
				EvaluateMadVRNlsMapping(true, 1.90, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(decision.mode));
			Assert::IsFalse(decision.verticalWarp);
			Assert::AreEqual(2.35 / 1.90, decision.stretchRatio, 0.000001);
		}

		TEST_METHOD(WiderContentUsesNonlinearVerticalMapping)
		{
			const MadVRNlsMappingDecision decision =
				EvaluateMadVRNlsMapping(true, 2.55, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(decision.mode));
			Assert::IsTrue(decision.verticalWarp);
			Assert::AreEqual(2.55 / 2.35, decision.stretchRatio, 0.000001);
		}

		TEST_METHOD(UnstableOrUnsafeGeometryWaits)
		{
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(EvaluateMadVRNlsMapping(
					false, 0.0, 2.35, 5.0, 1.0, false).mode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(EvaluateMadVRNlsMapping(
					true, 4.0, 2.35, 5.0, 1.0, false).mode));
		}

		TEST_METHOD(ScreenAndContentMatrixUsesExpectedMappings)
		{
			struct MatrixCase
			{
				double source;
				double target;
				MadVRNlsMappingMode mode;
				bool verticalWarp;
				double stretchRatio;
			};
			const MatrixCase cases[] = {
				{ 4.0 / 3.0, 16.0 / 9.0, MadVRNlsMappingMode::ACTIVE,
					false, 4.0 / 3.0 },
				{ 16.0 / 9.0, 16.0 / 9.0,
					MadVRNlsMappingMode::SCOPE_PASSTHROUGH, false, 1.0 },
				{ 2.35, 16.0 / 9.0, MadVRNlsMappingMode::ACTIVE,
					true, 2.35 / (16.0 / 9.0) },
				{ 16.0 / 9.0, 2.35, MadVRNlsMappingMode::ACTIVE,
					false, 2.35 / (16.0 / 9.0) },
				{ 1.90, 2.35, MadVRNlsMappingMode::ACTIVE,
					false, 2.35 / 1.90 },
				{ 2.35, 2.35, MadVRNlsMappingMode::SCOPE_PASSTHROUGH,
					false, 1.0 }
			};

			for (const MatrixCase& testCase : cases)
			{
				const MadVRNlsMappingDecision decision =
					EvaluateMadVRNlsMapping(true, testCase.source,
						testCase.target, 5.0, 1.0, false);
				Assert::AreEqual(static_cast<int>(testCase.mode),
					static_cast<int>(decision.mode));
				Assert::AreEqual(testCase.verticalWarp, decision.verticalWarp);
				Assert::AreEqual(testCase.stretchRatio,
					decision.stretchRatio, 0.000001);
			}
		}

		TEST_METHOD(ScreenProfilesResolveStableOutputContracts)
		{
			unsigned long aspectX = 0;
			unsigned long aspectY = 0;
			Assert::IsTrue(ResolveMadVRNlsOutputAspect(
				16.0 / 9.0, aspectX, aspectY));
			Assert::AreEqual(16ul, aspectX);
			Assert::AreEqual(9ul, aspectY);

			Assert::IsTrue(ResolveMadVRNlsOutputAspect(
				2.35, aspectX, aspectY));
			Assert::AreEqual(235ul, aspectX);
			Assert::AreEqual(100ul, aspectY);
		}

		TEST_METHOD(RestartOnlyWhenEffectiveScreenContractChanges)
		{
			const double nativeAspect = 16.0 / 9.0;
			Assert::IsFalse(MadVROutputAspectRequiresRestart(
				0, 0, 16, 9, nativeAspect));
			Assert::IsFalse(MadVROutputAspectRequiresRestart(
				16, 9, 0, 0, nativeAspect));
			Assert::IsTrue(MadVROutputAspectRequiresRestart(
				0, 0, 235, 100, nativeAspect));
			Assert::IsTrue(MadVROutputAspectRequiresRestart(
				235, 100, 16, 9, nativeAspect));
			Assert::IsFalse(MadVROutputAspectRequiresRestart(
				235, 100, 235, 100, nativeAspect));
		}

		TEST_METHOD(RendererReplacementPreservesRequestButRejectsOldGeometry)
		{
			MadVRShaderRuntimeState state;
			state.SetNlsTargetAspect(2.35);
			const uint64_t firstRenderer = state.BeginRendererGeneration();
			state.SetRuleSelection("nls", "nls",
				MadVRNlsMappingMode::ACTIVE);
			MadVRActivePictureGeometry geometry{
				1.90, 0.0, 0.08, 1.0, 0.92, 7, firstRenderer, true };
			Assert::IsTrue(state.SetActiveGeometry(geometry));

			const uint64_t secondRenderer = state.BeginRendererGeneration();
			const MadVRShaderRuntimeSnapshot restored = state.GetSnapshot();
			Assert::AreEqual("nls", restored.requestedRule.c_str());
			Assert::AreEqual("nls", restored.effectiveRule.c_str());
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(restored.nlsMode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(restored.lastSafeNlsMode));
			Assert::AreEqual(2.35, restored.nlsTargetAspect, 0.000001);
			Assert::IsFalse(restored.activeGeometry.stable);

			Assert::IsFalse(state.SetActiveGeometry(geometry));
			geometry.rendererGeneration = secondRenderer;
			geometry.generation = 1;
			Assert::IsTrue(state.SetActiveGeometry(geometry));
		}

		TEST_METHOD(ManualOffClearsTheArmedNlsState)
		{
			MadVRShaderRuntimeState state;
			const uint64_t renderer = state.BeginRendererGeneration();
			state.SetRuleSelection("nls", "nls",
				MadVRNlsMappingMode::SCOPE_PASSTHROUGH);
			Assert::IsTrue(state.SetActiveGeometry({
				2.35, 0.0, 0.1, 1.0, 0.9, 2, renderer, true }));

			state.SetRuleSelection("nls_off", "nls_off",
				MadVRNlsMappingMode::OFF);
			const MadVRShaderRuntimeSnapshot snapshot = state.GetSnapshot();
			Assert::AreEqual("nls_off", snapshot.requestedRule.c_str());
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::OFF),
				static_cast<int>(snapshot.nlsMode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::OFF),
				static_cast<int>(snapshot.lastSafeNlsMode));
			Assert::IsFalse(snapshot.activeGeometry.stable);
		}
	};
}
