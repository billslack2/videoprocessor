#include "pch.h"

#include <microsoft_directshow/MadVRShaderLoader.h>
#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(MadVRShaderRuntimeStateTests)
	{
	public:
		TEST_METHOD(WaitingCannotExposeNlsOutputContract)
		{
			MadVRShaderRuntimeSnapshot snapshot;
			snapshot.rendererGeneration = 4;
			snapshot.nlsMode = MadVRNlsMappingMode::WAITING;
			snapshot.activeGeometry = {
				1.90, 0.0, 0.05, 1.0, 0.95, 3, 4, true };
			Assert::IsFalse(
				MadVRNlsOutputContractIsPrepared(snapshot));

			snapshot.nlsMode = MadVRNlsMappingMode::ACTIVE;
			Assert::IsTrue(
				MadVRNlsOutputContractIsPrepared(snapshot));
			snapshot.activeGeometry.rendererGeneration = 3;
			Assert::IsFalse(
				MadVRNlsOutputContractIsPrepared(snapshot));
		}

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

		TEST_METHOD(UnstableOrRejectedGeometryWaits)
		{
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(EvaluateMadVRNlsMapping(
					false, 0.0, 2.35, 5.0, 1.0, false).mode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(EvaluateMadVRNlsMapping(
					true, 1.2, 2.35, 5.0, 1.3, false).mode));
		}

		TEST_METHOD(ExcessiveStretchUsesGeometryPreservingSafeFit)
		{
			const MadVRNlsMappingDecision pillarbox =
				EvaluateMadVRNlsMapping(
					true, 4.0 / 3.0, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(pillarbox.mode));
			Assert::IsFalse(pillarbox.safeFitVertical);
			Assert::AreEqual((4.0 / 3.0) / 2.35,
				pillarbox.safeFitFraction, 0.000001);
			Assert::AreEqual(1.0, pillarbox.stretchRatio, 0.000001);

			const MadVRNlsMappingDecision letterbox =
				EvaluateMadVRNlsMapping(
					true, 4.0, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(letterbox.mode));
			Assert::IsTrue(letterbox.safeFitVertical);
			Assert::AreEqual(2.35 / 4.0,
				letterbox.safeFitFraction, 0.000001);
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
				{ 4.0 / 3.0, 2.35, MadVRNlsMappingMode::SAFE_FIT,
					false, 1.0 },
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

			Assert::IsTrue(ResolveMadVRNlsOutputAspect(
				2.0, aspectX, aspectY));
			Assert::AreEqual(2ul, aspectX);
			Assert::AreEqual(1ul, aspectY);
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

		TEST_METHOD(RendererReplacementRebindsExactTrustedGeometry)
		{
			MadVRShaderRuntimeState state;
			state.SetNlsTargetAspect(2.35);
			const uint64_t firstRenderer = state.BeginRendererGeneration();
			state.SetRuleSelection("nls", "nls",
				MadVRNlsMappingMode::ACTIVE);
			MadVRActivePictureGeometry geometry{
				1.90, 0.0, 0.08, 1.0, 0.92, 7, firstRenderer, true };
			Assert::IsTrue(state.SetActiveGeometry(geometry));

			Assert::IsTrue(
				state.PrepareNlsOutputContractRendererReplacement());
			const uint64_t secondRenderer = state.BeginRendererGeneration();
			const MadVRShaderRuntimeSnapshot restored = state.GetSnapshot();
			Assert::AreEqual("nls", restored.requestedRule.c_str());
			Assert::AreEqual("nls", restored.effectiveRule.c_str());
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(restored.nlsMode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(restored.lastSafeNlsMode));
			Assert::AreEqual(2.35, restored.nlsTargetAspect, 0.000001);
			Assert::IsTrue(restored.activeGeometry.stable);
			Assert::AreEqual(secondRenderer,
				restored.activeGeometry.rendererGeneration);
			Assert::AreEqual(geometry.left,
				restored.activeGeometry.left, 0.000001);

			Assert::IsFalse(state.SetActiveGeometry(geometry));
			geometry.rendererGeneration = secondRenderer;
			geometry.generation = 1;
			Assert::IsTrue(state.SetActiveGeometry(geometry));
		}

		TEST_METHOD(UnpreparedRendererReplacementRejectsOldGeometry)
		{
			MadVRShaderRuntimeState state;
			const uint64_t firstRenderer = state.BeginRendererGeneration();
			state.SetRuleSelection("nls", "nls",
				MadVRNlsMappingMode::ACTIVE);
			Assert::IsTrue(state.SetActiveGeometry({
				1.90, 0.0, 0.08, 1.0, 0.92, 7, firstRenderer, true }));

			state.BeginRendererGeneration();
			const auto snapshot = state.GetSnapshot();
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(snapshot.nlsMode));
			Assert::IsFalse(snapshot.activeGeometry.stable);
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

		TEST_METHOD(ShaderFilesAlwaysResolveBesideExecutable)
		{
			std::string resolved;
			std::string error;
			Assert::IsTrue(MadVRShaderLoader::ResolveShaderFilename(
				"NLS.hlsl",
				"C:\\Videoprocessor\\vp\\VideoProcessor.exe",
				resolved, error));
			Assert::AreEqual(
				"C:\\Videoprocessor\\vp\\shaders\\NLS.hlsl",
				resolved.c_str());
		}

		TEST_METHOD(RenderersAcceptOnlyTheirCompatibleShaderSource)
		{
			Assert::IsTrue(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"NLS.hlsl", ShaderRendererBackend::MADVR));
			Assert::IsFalse(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"NLS.glsl", ShaderRendererBackend::MADVR));
			Assert::IsTrue(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"NLS.glsl", ShaderRendererBackend::LIBPLACEBO));
			Assert::IsTrue(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"NLS.hook", ShaderRendererBackend::LIBPLACEBO));
			Assert::IsFalse(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"NLS.hlsl", ShaderRendererBackend::LIBPLACEBO));
			Assert::IsTrue(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"", ShaderRendererBackend::MADVR));
			Assert::IsTrue(
				MadVRShaderLoader::IsShaderFilenameCompatible(
					"", ShaderRendererBackend::LIBPLACEBO));
		}

		TEST_METHOD(ShaderFileConfigurationRejectsDirectoriesAndTraversal)
		{
			const char* invalid[] = {
				"",
				"shaders\\NLS.hlsl",
				"subdir/NLS.hlsl",
				"..\\NLS.hlsl",
				"C:\\shaders\\NLS.hlsl",
				"NLS.hlsl:alternate"
			};
			for (const char* filename : invalid)
			{
				std::string resolved;
				std::string error;
				Assert::IsFalse(MadVRShaderLoader::ResolveShaderFilename(
					filename,
					"C:\\Videoprocessor\\vp\\VideoProcessor.exe",
					resolved, error));
				Assert::IsFalse(error.empty());
			}
		}

		TEST_METHOD(SafeFitRemainsEffectiveAcrossRendererReplacement)
		{
			MadVRShaderRuntimeState state;
			const uint64_t firstRenderer = state.BeginRendererGeneration();
			MadVRNlsMappingDecision decision;
			decision.mode = MadVRNlsMappingMode::SAFE_FIT;
			decision.sourceAspect = 4.0 / 3.0;
			decision.targetAspect = 2.35;
			decision.safeFitFraction = decision.sourceAspect /
				decision.targetAspect;
			state.SetNlsDecision(decision);
			state.SetRuleSelection("nls", "nls",
				MadVRNlsMappingMode::SAFE_FIT);
			MadVRActivePictureGeometry geometry;
			geometry.aspectRatio = decision.sourceAspect;
			geometry.left = 0.125;
			geometry.right = 0.875;
			geometry.bottom = 1.0;
			geometry.rendererGeneration = firstRenderer;
			geometry.stable = true;
			Assert::IsTrue(state.SetActiveGeometry(geometry));

			Assert::IsTrue(
				state.PrepareNlsOutputContractRendererReplacement());
			const uint64_t secondRenderer = state.BeginRendererGeneration();
			const MadVRShaderRuntimeSnapshot snapshot = state.GetSnapshot();
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(snapshot.nlsMode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(snapshot.lastSafeNlsMode));
			Assert::AreEqual(secondRenderer, snapshot.rendererGeneration);
			Assert::IsTrue(snapshot.activeGeometry.stable);
			Assert::AreEqual(secondRenderer,
				snapshot.activeGeometry.rendererGeneration);
			Assert::AreEqual(geometry.left,
				snapshot.activeGeometry.left, 0.000001);
			Assert::AreEqual(4.0 / 3.0,
				snapshot.nlsDecision.sourceAspect, 0.000001);
			Assert::AreEqual(2.35,
				snapshot.nlsDecision.targetAspect, 0.000001);
		}
	};
}
