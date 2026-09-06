/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <dshow.h>
#include <VideoState.h>
#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include <map>
#include <climits>
#include <string>
#include <vector>

class ConfigFile;


struct ActiveMadVRShader
{
	std::string name;
	bool postResize = false;
};


struct MadVRShaderSelection
{
	std::string ruleName;
	std::string ruleLabel = "None";
	// Non-NLS labels retained when the runtime OSD replaces the NLS portion
	// with its current mapping mode.
	std::string companionRuleLabel;
	std::vector<ActiveMadVRShader> activeShaders;
	std::vector<std::string> activeSections;
	bool activeSectionsAvailable = false;
	unsigned long outputAspectRatioX = 0;
	unsigned long outputAspectRatioY = 0;
};

enum class ShaderRendererBackend
{
	MADVR,
	LIBPLACEBO
};


struct ConfiguredShaderRule
{
	std::string name;
	std::string label;
	std::string filename;
	std::map<std::string, std::string> parameters;
	bool nls = false;
	bool none = false;
	// Unified profile selections execute primarily in this list order. The
	// per-profile stage order remains a secondary ordering key.
	unsigned int profileSelectionOrder = UINT_MAX;
	unsigned int stageOrder = 0;
	double aspectTolerancePercent = 5.0;
	double maximumStretchRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO;
	double stableGeometryDeadbandPercent = 2.0;
	double activeAspectMinimum = 0.0;
	// Preserve the historical unrestricted default. Configured NLS profiles
	// explicitly opt into narrower-only or wider-only behavior when required.
	NlsAspectDirection aspectDirection = NlsAspectDirection::ANY;
	double vpRendererMaximumCropPercent = 0.0;
	NlsPresentationCropPreference vpRendererCropPreference =
		NlsPresentationCropPreference::PRESERVE_IMAGE;
};


class MadVRShaderLoader
{
public:
	// Legacy rule names are case-insensitive. Target selectors use the canonical
	// chord after @shader-key: (N and n are equivalent; Shift+N is distinct).
	static std::string CanonicalizeRuleSelector(const std::string& selector);
	static bool RuleSelectorsEqual(const std::string& left,
		const std::string& right);
	// Loads [shaders] from VideoProcessor.cfg and applies the configured shader
	// chains when the selected renderer exposes an external shader interface.
	// Configuration or compilation failures are logged and never abort renderer
	// startup. Returns the shaders from stages that were installed completely.
	static MadVRShaderSelection ApplyConfiguredShaders(IBaseFilter* renderer,
		const VideoState& videoState);

	// Selects a named rule immediately and remembers it across renderer rebuilds.
	// An empty name returns rule selection to the automatic configured rules.
	static MadVRShaderSelection ApplyConfiguredShaderRule(IBaseFilter* renderer,
		const VideoState& videoState, const std::string& ruleName,
		bool updateRuntimeRequest = true);

	// Returns the presentation aspect ratio requested by the currently selected
	// runtime rule. False means retain the source's native aspect ratio.
	static bool GetRuntimeOutputAspectRatio(unsigned long& aspectX,
		unsigned long& aspectY);

	// Validates optional active-picture conditions on a manual rule. Rules with
	// no conditions remain compatible with all formats.
	static bool ValidateActivePictureAspect(const std::string& ruleName,
		bool aspectAvailable, double activeAspectRatio, std::string& reason);
	static bool EvaluateNlsMapping(const std::string& ruleName,
		bool aspectAvailable, double activeAspectRatio,
		MadVRNlsMappingDecision& decision,
		ShaderRendererBackend backend = ShaderRendererBackend::MADVR);
	// Supplies stable measured geometry for the current renderer generation.
	// Geometry from a replaced renderer is rejected while the armed request and
	// output contract remain durable.
	static bool SetRuntimeActivePictureGeometry(
		const MadVRActivePictureGeometry& geometry);
	static void SetRuntimeNlsTargetAspect(double targetAspect);
	static void SetRuntimeNlsDecision(
		const MadVRNlsMappingDecision& decision);
	static void SetRuntimeShaderSelection(const std::string& requestedRule,
		const std::string& effectiveRule, MadVRNlsMappingMode nlsMode);
	static MadVRShaderRuntimeSnapshot GetRuntimeShaderState();
	static uint64_t BeginRendererGeneration();
	static bool PrepareNlsOutputContractRendererReplacement();
	static bool GetRuleActivationInfo(const std::string& ruleName,
		std::string& label, std::string& inactiveRule, bool& nlsMapping,
		ShaderRendererBackend backend = ShaderRendererBackend::MADVR);
	// Resolves the members of one same-shortcut selector that apply to a
	// renderer. Known incompatible source formats are not opened or compiled.
	static bool GetConfiguredRuleSelection(const std::string& ruleName,
		ShaderRendererBackend backend,
		std::vector<ConfiguredShaderRule>& selection,
		std::string& reason);
	static bool GetConfiguredRuleSelection(const std::string& ruleName,
		ShaderRendererBackend backend,
		std::vector<ConfiguredShaderRule>& selection,
		std::vector<std::string>& activeSections,
		std::string& reason);
	// Resolves a rule selection from an already-loaded configuration.  Keeping
	// this separate from the active-file wrapper makes the target shader grammar
	// directly testable and preserves an empty target selection as explicit OFF.
	static bool ResolveConfiguredRuleSelection(const ConfigFile& config,
		const std::string& ruleName, ShaderRendererBackend backend,
		std::vector<ConfiguredShaderRule>& selection,
		std::string& reason);
	static bool ResolveConfiguredRuleSelection(const ConfigFile& config,
		const std::string& ruleName, ShaderRendererBackend backend,
		std::vector<ConfiguredShaderRule>& selection,
		std::vector<std::string>& activeSections,
		std::string& reason);
	static bool IsShaderFilenameCompatible(const std::string& filename,
		ShaderRendererBackend backend);
	// Resolves one shader filename under <executable>\shaders. Directory
	// components, absolute paths, and traversal are deliberately rejected.
	static bool ResolveShaderFilename(const std::string& filename,
		const std::string& executablePath, std::string& resolvedPath,
		std::string& error);
};
