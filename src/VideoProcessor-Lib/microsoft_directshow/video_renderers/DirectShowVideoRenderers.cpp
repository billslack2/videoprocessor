/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */


#include <pch.h>

#include <propvarutil.h>
#include <guiddef.h>

#include <guid.h>
#include "DirectShowVideoRenderers.h"
#include <microsoft_directshow/video_renderers/DirectShowMPCVideoRenderer.h>
#include <microsoft_directshow/video_renderers/DirectShowGenericVideoRenderer.h>
#include <microsoft_directshow/video_renderers/DirectShowEnhancedVideoRenderer.h>

namespace
{
CString GuidToCString(const GUID& guid)
{
	wchar_t value[39] = {};
	StringFromGUID2(guid, value, ARRAYSIZE(value));
	return value;
}

CString GetInprocServerPath(const GUID& clsid)
{
	const CString key = CString(TEXT("CLSID\\")) + GuidToCString(clsid) + TEXT("\\InprocServer32");
	DWORD size = 0;
	if (RegGetValue(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
		size < sizeof(wchar_t))
		return TEXT("(not registered as an in-process server)");

	std::vector<wchar_t> value(size / sizeof(wchar_t), L'\0');
	if (RegGetValue(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
		return TEXT("(not registered as an in-process server)");
	return value.data();
}

CString NormalizePath(const CString& value)
{
	CString expanded(value);
	const DWORD required = ExpandEnvironmentStrings(value, nullptr, 0);
	if (required > 0)
	{
		std::vector<wchar_t> buffer(required, L'\0');
		if (ExpandEnvironmentStrings(value, buffer.data(), required) != 0)
			expanded = buffer.data();
	}

	expanded.Trim();
	if (expanded.GetLength() >= 2 && expanded[0] == TEXT('"') &&
		expanded[expanded.GetLength() - 1] == TEXT('"'))
		expanded = expanded.Mid(1, expanded.GetLength() - 2);

	const DWORD fullPathLength = GetFullPathName(expanded, 0, nullptr, nullptr);
	if (fullPathLength > 0)
	{
		std::vector<wchar_t> fullPath(fullPathLength, L'\0');
		if (GetFullPathName(expanded, fullPathLength, fullPath.data(), nullptr) != 0)
			expanded = fullPath.data();
	}

	expanded.Replace(TEXT('/'), TEXT('\\'));
	while (expanded.GetLength() > 3 && expanded.Right(1) == TEXT("\\"))
		expanded = expanded.Left(expanded.GetLength() - 1);
	return expanded;
}

bool IsHostedUnderSystem32(const CString& serverPath)
{
	const DWORD required = GetSystemDirectory(nullptr, 0);
	if (required == 0)
		return false;

	std::vector<wchar_t> systemDirectory(required + 1, L'\0');
	if (GetSystemDirectory(systemDirectory.data(), static_cast<UINT>(systemDirectory.size())) == 0)
		return false;

	const CString normalizedServer = NormalizePath(serverPath);
	const CString normalizedSystemDirectory = NormalizePath(systemDirectory.data());
	if (normalizedServer.IsEmpty() || normalizedSystemDirectory.IsEmpty())
		return false;

	const CString directoryPrefix = normalizedSystemDirectory + TEXT("\\");
	return normalizedServer.GetLength() > directoryPrefix.GetLength() &&
		normalizedServer.Left(directoryPrefix.GetLength()).CompareNoCase(directoryPrefix) == 0;
}

bool TryGetConfiguredHideLegacyRenderers(const ConfigFile& config, bool& value)
{
	return config.TryGetBool("general", "hide_legacy_renderers", value) ||
		config.TryGetBool("command_line", "hide_legacy_renderers", value);
}
}

bool DirectShowHideLegacyRenderers(const ConfigFile& config)
{
	bool value = true;
	TryGetConfiguredHideLegacyRenderers(config, value);
	return value;
}

void DirectShowVideoRendererIds(std::vector<RendererId>& rendererIds)
{
	// https://docs.microsoft.com/en-us/windows/win32/directshow/using-the-filter-mapper
	ConfigFile config;
	const bool configLoaded = config.Load();
	const bool hideLegacyRenderers = DirectShowHideLegacyRenderers(config);
	bool configuredValue = true;
	const bool hasConfiguredValue = TryGetConfiguredHideLegacyRenderers(config, configuredValue);
	DEBUGLOG("DirectShow renderer discovery: hide_legacy_renderers=%s (%s)",
		hideLegacyRenderers ? "true" : "false",
		hasConfiguredValue ? "configured" : (configLoaded ? "default" : "configuration unavailable"));

	IFilterMapper2* pMapper = nullptr;
	IEnumMoniker* pEnum = nullptr;
	HRESULT hr;

	hr = CoCreateInstance(CLSID_FilterMapper2,
		nullptr, CLSCTX_INPROC, IID_IFilterMapper2,
		(void**)&pMapper);

	if (FAILED(hr))
		throw std::runtime_error("Failed to instantiate the filter mapper");

	GUID arrayInTypes[2];
	arrayInTypes[0] = MEDIATYPE_Video;
	arrayInTypes[1] = GUID_NULL;

	hr = pMapper->EnumMatchingFilters(
		&pEnum,
		0,                  // Reserved.
		TRUE,               // Use exact match?
		MERIT_DO_NOT_USE,   // Minimum merit.
		TRUE,               // At least one input pin?
		1,                  // Number of major type/subtype pairs for input.
		arrayInTypes,       // Array of major type/subtype pairs for input.
		nullptr,               // Input medium.
		nullptr,               // Input pin category.
		FALSE,              // Must be a renderer?
		FALSE,              // At least one output pin?
		0,                  // Number of major type/subtype pairs for output.
		nullptr,               // Array of major type/subtype pairs for output.
		nullptr,               // Output medium.
		nullptr);              // Output pin category.

	// Enumerate the monikers.
	IMoniker* pMoniker;
	ULONG cFetched;
	while (pEnum->Next(1, &pMoniker, &cFetched) == S_OK)
	{
		IPropertyBag* pPropBag = nullptr;
		hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);

		if (SUCCEEDED(hr))
		{
			VARIANT nameVariant;
			VARIANT clsidVariant;
			VariantInit(&nameVariant);
			VariantInit(&clsidVariant);

			HRESULT nameHr = pPropBag->Read(L"FriendlyName", &nameVariant, 0);
			HRESULT clsidHr = pPropBag->Read(L"CLSID", &clsidVariant, 0);
			if (SUCCEEDED(nameHr) && SUCCEEDED(clsidHr))
			{
				CString name = nameVariant.bstrVal;
				GUID clsid = GUID_NULL;
				if (FAILED(VariantToGUID(clsidVariant, &clsid)))
				{
					DEBUGLOG("DirectShow renderer discovery: skipping '%s': CLSID property could not be converted", CStringA(name).GetString());
				}
				else
				{
					const bool nameMatchesRendererHeuristic =
						((name.Find(TEXT("Video")) >= 0) && (name.Find(TEXT("Render")) >= 0)) ||
						(name.Find(TEXT("VR")) >= 0);
					const CString clsidText = GuidToCString(clsid);
					const CString inprocServer = GetInprocServerPath(clsid);
					CString normalizedName(name);
					normalizedName.MakeLower();
					const bool excludedFromUiAsDeckLink = normalizedName.Find(TEXT("decklink")) >= 0;
					const bool excludedFromUiAsLegacy =
						hideLegacyRenderers && IsHostedUnderSystem32(inprocServer);
					const char* decision = !nameMatchesRendererHeuristic ? "excluded by name heuristic" :
						(excludedFromUiAsDeckLink ? "excluded by DeckLink filter" :
							(excludedFromUiAsLegacy ? "excluded by System32 server path" : "included"));
					DEBUGLOG("DirectShow renderer discovery: friendly='%s', clsid=%s, inproc='%s', UI=%s",
						CStringA(name).GetString(), CStringA(clsidText).GetString(),
						CStringA(inprocServer).GetString(), decision);

					if (nameMatchesRendererHeuristic && !excludedFromUiAsDeckLink &&
						!excludedFromUiAsLegacy)
					{
					CString rendererEntityName;
					rendererEntityName.Format(_T("DirectShow - %s"), name);

					RendererId rendererId;
					rendererId.name = rendererEntityName;
					rendererId.guid = clsid;
					rendererIds.push_back(rendererId);
					}
				}
			}

			VariantClear(&nameVariant);
			VariantClear(&clsidVariant);

			pPropBag->Release();
		}
		pMoniker->Release();
	}

	pMapper->Release();
	pEnum->Release();
}
