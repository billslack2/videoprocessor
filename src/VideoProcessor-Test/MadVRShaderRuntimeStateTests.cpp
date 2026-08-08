#include "pch.h"

#include <microsoft_directshow/MadVRShaderLoader.h>
#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include "CppUnitTest.h"

#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <map>

#pragma comment(lib, "d3dcompiler.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	namespace
	{
		std::string LoadNlsHlsl()
		{
			std::string path = __FILE__;
			for (int level = 0; level < 3; ++level)
			{
				const size_t separator = path.find_last_of("\\/");
				if (separator == std::string::npos)
					return {};
				path.resize(separator);
			}
			path += "\\shaders\\NLS.hlsl";
			std::ifstream input(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>());
		}

		void ReplaceAll(std::string& source, const std::string& token,
			const std::string& value)
		{
			size_t position = 0;
			while ((position = source.find(token, position)) !=
				std::string::npos)
			{
				source.replace(position, token.size(), value);
				position += value.size();
			}
		}

		std::string ExpandNlsHlsl(int quality, bool vertical, int geometry)
		{
			std::string source = LoadNlsHlsl();
			const std::map<std::string, std::string> parameters = {
				{ "strength", "1.0" },
				{ "curve", "2.0" },
				{ "stretch_ratio", "1.33333333" },
				{ "warp_axis", vertical ? "1" : "0" },
				{ "active_left", "0.125" },
				{ "active_top", "0.05" },
				{ "active_right", "0.875" },
				{ "active_bottom", "0.95" },
				{ "geometry", std::to_string(geometry) },
				{ "quality", std::to_string(quality) },
				{ "safe_fit", "0" },
				{ "safe_fit_axis", "0" },
				{ "safe_fit_fraction", "1.0" },
				{ "center_protection", geometry ? "0.25" : "0.0" }
			};
			for (const auto& parameter : parameters)
				ReplaceAll(source, "{{" + parameter.first + "}}",
					parameter.second);
			return source;
		}
	}

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
				EvaluateNlsMapping(true, 2.35, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::LINEAR_PASSTHROUGH),
				static_cast<int>(decision.mode));
			Assert::AreEqual(1.0, decision.stretchRatio, 0.000001);
			Assert::IsFalse(decision.verticalWarp);
		}

		TEST_METHOD(SharedRuntimeSnapshotContainsTheFinalAlphaNlsDecision)
		{
			const uint64_t rendererGeneration =
				MadVRShaderLoader::BeginRendererGeneration();
			const MadVRNlsMappingDecision decision =
				EvaluateNlsMapping(
					true, 1.90, 2.35, 5.0, 1.0, false);
			MadVRShaderLoader::SetRuntimeNlsTargetAspect(2.35);
			MadVRShaderLoader::SetRuntimeNlsDecision(decision);
			Assert::IsTrue(MadVRShaderLoader::SetRuntimeActivePictureGeometry({
				1.90, 0.0, 0.03, 1.0, 0.97,
				7, rendererGeneration, true }));
			MadVRShaderLoader::SetRuntimeShaderSelection(
				"nls_alpha", "nls_alpha", decision.mode);

			const MadVRShaderRuntimeSnapshot snapshot =
				MadVRShaderLoader::GetRuntimeShaderState();
			Assert::AreEqual(static_cast<int>(decision.mode),
				static_cast<int>(snapshot.nlsMode));
			Assert::AreEqual(static_cast<int>(decision.mode),
				static_cast<int>(snapshot.nlsDecision.mode));
			Assert::AreEqual(decision.sourceAspect,
				snapshot.nlsDecision.sourceAspect, 0.000001);
			Assert::AreEqual(decision.targetAspect,
				snapshot.nlsDecision.targetAspect, 0.000001);
			Assert::AreEqual(decision.stretchRatio,
				snapshot.nlsDecision.stretchRatio, 0.000001);
			Assert::AreEqual(decision.verticalWarp,
				snapshot.nlsDecision.verticalWarp);
			Assert::AreEqual(decision.reason.c_str(),
				snapshot.nlsDecision.reason.c_str());
		}

		TEST_METHOD(ImaxContentUsesNonlinearHorizontalMapping)
		{
			const MadVRNlsMappingDecision decision =
				EvaluateNlsMapping(true, 1.90, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(decision.mode));
			Assert::IsFalse(decision.verticalWarp);
			Assert::AreEqual(2.35 / 1.90, decision.stretchRatio, 0.000001);
		}

		TEST_METHOD(WiderContentUsesNonlinearVerticalMapping)
		{
			const MadVRNlsMappingDecision decision =
				EvaluateNlsMapping(true, 2.55, 2.35, 5.0, 1.0, false);
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
				static_cast<int>(EvaluateNlsMapping(
					false, 0.0, 2.35, 5.0, 1.0, false).mode));
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::WAITING),
				static_cast<int>(EvaluateNlsMapping(
					true, 1.2, 2.35, 5.0, 1.3, false).mode));
		}

		TEST_METHOD(ExcessiveStretchUsesGeometryPreservingSafeFit)
		{
			const MadVRNlsMappingDecision pillarbox =
				EvaluateNlsMapping(
					true, 4.0 / 3.0, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(pillarbox.mode));
			Assert::IsFalse(pillarbox.safeFitVertical);
			Assert::AreEqual((4.0 / 3.0) / 2.35,
				pillarbox.safeFitFraction, 0.000001);
			Assert::AreEqual(1.0, pillarbox.stretchRatio, 0.000001);

			const MadVRNlsMappingDecision letterbox =
				EvaluateNlsMapping(
					true, 4.0, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SAFE_FIT),
				static_cast<int>(letterbox.mode));
			Assert::IsTrue(letterbox.safeFitVertical);
			Assert::AreEqual(2.35 / 4.0,
				letterbox.safeFitFraction, 0.000001);
		}

		TEST_METHOD(Vp0099DefaultSafetyAllowsUsefulAndRejectsExtremeMappings)
		{
			struct SafetyCase
			{
				double source;
				double target;
				NlsMappingMode mode;
			};
			const SafetyCase cases[] = {
				{ 4.0 / 3.0, 16.0 / 9.0, NlsMappingMode::ACTIVE },
				{ 16.0 / 9.0, 2.35, NlsMappingMode::ACTIVE },
				{ 4.0 / 3.0, 2.35, NlsMappingMode::SAFE_FIT },
				{ 4.0 / 3.0, 2.76, NlsMappingMode::SAFE_FIT },
				{ 16.0 / 9.0, 2.76, NlsMappingMode::SAFE_FIT },
			};
			for (const SafetyCase& testCase : cases)
			{
				const NlsMappingDecision decision = EvaluateNlsMapping(
					true, testCase.source, testCase.target,
					0.0, 0.0, false);
				Assert::AreEqual(static_cast<int>(testCase.mode),
					static_cast<int>(decision.mode));
				Assert::AreEqual(NLS_DEFAULT_MAXIMUM_STRETCH_RATIO,
					decision.maximumRatio, 0.000001);
				Assert::AreEqual(std::max(
					testCase.target / testCase.source,
					testCase.source / testCase.target),
					decision.requestedRatio, 0.000001);
			}
		}

		TEST_METHOD(Vp0099CustomSafetyLimitHasDeterministicBoundaries)
		{
			const NlsMappingDecision exact = EvaluateNlsMapping(
				true, 1.0, 1.35, 0.0, 0.0, false, 1.35);
			Assert::AreEqual(static_cast<int>(NlsMappingMode::ACTIVE),
				static_cast<int>(exact.mode));
			Assert::AreEqual(1.35, exact.stretchRatio, 0.000001);

			const NlsMappingDecision above = EvaluateNlsMapping(
				true, 1.0, 1.3501, 0.0, 0.0, false, 1.35);
			Assert::AreEqual(static_cast<int>(NlsMappingMode::SAFE_FIT),
				static_cast<int>(above.mode));

			for (double invalid : { 0.99, 1.5001,
				(std::numeric_limits<double>::quiet_NaN)() })
			{
				const NlsMappingDecision rejected = EvaluateNlsMapping(
					true, 1.0, 1.2, 0.0, 0.0, false, invalid);
				Assert::AreEqual(static_cast<int>(NlsMappingMode::WAITING),
					static_cast<int>(rejected.mode));
				Assert::IsTrue(rejected.reason.find("maximum stretch") !=
					std::string::npos);
			}
		}

		TEST_METHOD(Vp0099TargetResolutionUsesConfiguredOrExactPanelAspect)
		{
			Assert::AreEqual(2.35,
				ResolveNlsTargetAspect(true, 2.35, 16.0 / 9.0),
				0.000001);
			for (double panel : { 16.0 / 9.0, 4096.0 / 2160.0, 2.20 })
			{
				Assert::AreEqual(panel,
					ResolveNlsTargetAspect(false, 0.0, panel),
					0.000001);
			}
			Assert::AreEqual(0.0,
				ResolveNlsTargetAspect(false, 0.0, 0.0), 0.000001);
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
					MadVRNlsMappingMode::LINEAR_PASSTHROUGH, false, 1.0 },
				{ 2.35, 16.0 / 9.0, MadVRNlsMappingMode::ACTIVE,
					true, 2.35 / (16.0 / 9.0) },
				{ 16.0 / 9.0, 2.35, MadVRNlsMappingMode::ACTIVE,
					false, 2.35 / (16.0 / 9.0) },
				{ 4.0 / 3.0, 2.35, MadVRNlsMappingMode::SAFE_FIT,
					false, 1.0 },
				{ 1.90, 2.35, MadVRNlsMappingMode::ACTIVE,
					false, 2.35 / 1.90 },
				{ 2.35, 2.35, MadVRNlsMappingMode::LINEAR_PASSTHROUGH,
					false, 1.0 }
			};

			for (const MatrixCase& testCase : cases)
			{
				const MadVRNlsMappingDecision decision =
					EvaluateNlsMapping(true, testCase.source,
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
			Assert::AreEqual(47ul, aspectX);
			Assert::AreEqual(20ul, aspectY);

			Assert::IsTrue(ResolveMadVRNlsOutputAspect(
				2.0, aspectX, aspectY));
			Assert::AreEqual(2ul, aspectX);
			Assert::AreEqual(1ul, aspectY);
		}

		TEST_METHOD(EternalsLetterboxUsesCompensatedMadVRPresentation)
		{
			MadVRNlsMappingDecision decision;
			decision.mode = MadVRNlsMappingMode::ACTIVE;
			decision.sourceAspect = 3840.0 / 2024.0;
			decision.targetAspect = 2.35;
			decision.stretchRatio =
				decision.targetAspect / decision.sourceAspect;
			decision.verticalWarp = false;
			decision.reason = "active picture is narrower than the target";
			const MadVRActivePictureGeometry geometry{
				decision.sourceAspect, 0.0, 68.0 / 2160.0,
				1.0, 2092.0 / 2160.0, 7, 3, true };

			const MadVRNlsPresentationPlan plan =
				ResolveMadVRNlsPresentationPlan(decision, geometry);
			Assert::IsTrue(plan.customShader);
			Assert::AreEqual(2.35 * 2024.0 / 2160.0,
				plan.rasterAspect, 0.000001);
			Assert::AreEqual(plan.rasterAspect,
				static_cast<double>(plan.aspectX) / plan.aspectY, 0.000001);
			Assert::AreEqual(geometry.left,
				plan.shaderGeometry.left, 0.000001);
			Assert::AreEqual(geometry.top,
				plan.shaderGeometry.top, 0.000001);
			Assert::AreEqual(geometry.right,
				plan.shaderGeometry.right, 0.000001);
			Assert::AreEqual(geometry.bottom,
				plan.shaderGeometry.bottom, 0.000001);
			Assert::AreEqual(2.35,
				plan.rasterAspect *
				(geometry.right - geometry.left) /
				(geometry.bottom - geometry.top),
				0.000001);
			Assert::AreEqual(static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(ConstrainMadVRNlsMappingToGeometry(
					decision, geometry).mode));
		}

		TEST_METHOD(MadVRHybridNlsSupportsBarsOnEitherAxis)
		{
			MadVRNlsMappingDecision horizontal;
			horizontal.mode = MadVRNlsMappingMode::ACTIVE;
			horizontal.sourceAspect = 1.90;
			horizontal.targetAspect = 2.35;
			horizontal.verticalWarp = false;
			horizontal.reason = "active picture is narrower than the target";

			const MadVRActivePictureGeometry fullRaster{
				1.90, 0.0, 0.0, 1.0, 1.0, 1, 1, true };
			Assert::IsTrue(ResolveMadVRNlsPresentationPlan(
				horizontal, fullRaster).customShader);
			Assert::AreEqual(47ul, ResolveMadVRNlsPresentationPlan(
				horizontal, fullRaster).aspectX);

			const MadVRActivePictureGeometry onePixelSideCrop{
				1.90, 1.0 / 3840.0, 0.03, 1.0, 0.97, 2, 1, true };
			Assert::IsTrue(ResolveMadVRNlsPresentationPlan(
				horizontal, onePixelSideCrop).customShader);

			MadVRNlsMappingDecision vertical = horizontal;
			vertical.sourceAspect = 2.60;
			vertical.targetAspect = 2.35;
			vertical.verticalWarp = true;
			const MadVRActivePictureGeometry pillarbox{
				2.60, 0.05, 0.0, 0.95, 1.0, 3, 1, true };
			Assert::IsTrue(ResolveMadVRNlsPresentationPlan(
				vertical, pillarbox).customShader);
			const MadVRActivePictureGeometry letterbox{
				2.60, 0.0, 0.03, 1.0, 0.97, 4, 1, true };
			Assert::IsTrue(ResolveMadVRNlsPresentationPlan(
				vertical, letterbox).customShader);

			const MadVRActivePictureGeometry windowbox{
				1.90, 0.05, 0.03, 0.95, 0.97, 5, 1, true };
			Assert::IsTrue(ResolveMadVRNlsPresentationPlan(
				horizontal, windowbox).customShader);
			const MadVRNlsMappingDecision constrained =
				ConstrainMadVRNlsMappingToGeometry(horizontal, windowbox);
			Assert::AreEqual(static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(constrained.mode));
			Assert::AreEqual(horizontal.reason.c_str(),
				constrained.reason.c_str());
		}

		TEST_METHOD(FourByThreePillarboxCanMapToSixteenByNineInMadVR)
		{
			const MadVRNlsMappingDecision decision = EvaluateNlsMapping(
				true, 4.0 / 3.0, 16.0 / 9.0, 5.0, 1.0, false, 1.4);
			Assert::AreEqual(static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(decision.mode));
			Assert::IsFalse(decision.verticalWarp);
			const MadVRActivePictureGeometry geometry{
				4.0 / 3.0, 0.125, 0.0, 0.875, 1.0, 9, 2, true };
			const MadVRNlsPresentationPlan plan =
				ResolveMadVRNlsPresentationPlan(decision, geometry);
			Assert::IsTrue(plan.customShader);
			Assert::AreEqual(geometry.left,
				plan.shaderGeometry.left, 0.000001);
			Assert::AreEqual(geometry.right,
				plan.shaderGeometry.right, 0.000001);
			Assert::AreEqual(16.0 / 9.0,
				plan.rasterAspect * 0.75, 0.000001);
		}

		TEST_METHOD(BarPreservingNlsHlslCompilesEveryRuntimeVariant)
		{
			const std::string templateSource = LoadNlsHlsl();
			Assert::IsFalse(templateSource.empty());
			Assert::IsTrue(templateSource.find(
				"if (!insideActivePicture)") != std::string::npos);
			Assert::IsTrue(templateSource.find(
				"return tex2D(s0, tex);") != std::string::npos);
			Assert::IsTrue(templateSource.find(
				"activeMinimum, activeMaximum") != std::string::npos);

			for (int quality = 0; quality <= 3; ++quality)
			{
				for (int geometry = 0; geometry <= 1; ++geometry)
				{
					for (int vertical = 0; vertical <= 1; ++vertical)
					{
						const std::string source = ExpandNlsHlsl(
							quality, vertical != 0, geometry);
						Assert::IsTrue(source.find("{{") ==
							std::string::npos);
						CComPtr<ID3DBlob> bytecode;
						CComPtr<ID3DBlob> diagnostics;
						const HRESULT result = D3DCompile(source.data(),
							source.size(), "NLS.hlsl", nullptr, nullptr,
							"main", "ps_3_0", 0, 0,
							&bytecode, &diagnostics);
						Assert::IsTrue(SUCCEEDED(result));
						Assert::IsNotNull(bytecode.p);
					}
				}
			}
		}

		TEST_METHOD(MadVRHybridNlsRejectsInvalidOrInactiveGeometry)
		{
			MadVRNlsMappingDecision decision;
			decision.mode = MadVRNlsMappingMode::ACTIVE;
			decision.sourceAspect = 1.90;
			decision.targetAspect = 2.35;
			MadVRActivePictureGeometry geometry{
				1.90, 0.0, 0.03, 1.0, 0.97, 1, 1, false };
			Assert::IsFalse(ResolveMadVRNlsPresentationPlan(
				decision, geometry).customShader);

			geometry.stable = true;
			geometry.right = 1.01;
			Assert::IsFalse(ResolveMadVRNlsPresentationPlan(
				decision, geometry).customShader);
			geometry.right = 1.0;
			geometry.bottom = geometry.top;
			Assert::IsFalse(ResolveMadVRNlsPresentationPlan(
				decision, geometry).customShader);
			geometry.bottom = 0.97;
			geometry.aspectRatio = 0.0;
			Assert::IsFalse(ResolveMadVRNlsPresentationPlan(
				decision, geometry).customShader);

			geometry = {
				1.90, 0.0, 0.03, 1.0, 0.97, 1, 1, true };
			decision.mode = MadVRNlsMappingMode::SAFE_FIT;
			const MadVRNlsPresentationPlan safeFit =
				ResolveMadVRNlsPresentationPlan(decision, geometry);
			Assert::IsFalse(safeFit.customShader);
			Assert::AreEqual(0ul, safeFit.aspectX);
			Assert::AreEqual(0ul, safeFit.aspectY);
			decision.mode = MadVRNlsMappingMode::WAITING;
			Assert::IsFalse(ResolveMadVRNlsPresentationPlan(
				decision, geometry).customShader);
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
				MadVRNlsMappingMode::LINEAR_PASSTHROUGH);
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
