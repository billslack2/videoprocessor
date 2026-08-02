/*
 * Minimal declarations from madVR's published mvrInterfaces.h (BSD license).
 * Copyright (C) 2011-2017 www.madshi.net.
 */
#pragma once

#include <Unknwn.h>


DECLARE_INTERFACE_IID_(IMadVRInfo, IUnknown,
	"8FAB7F31-06EF-444C-A798-10314E185532")
{
	STDMETHOD(GetBool)(LPCSTR field, bool* value) = 0;
	STDMETHOD(GetInt)(LPCSTR field, int* value) = 0;
	STDMETHOD(GetSize)(LPCSTR field, SIZE* value) = 0;
	STDMETHOD(GetRect)(LPCSTR field, RECT* value) = 0;
	STDMETHOD(GetUlonglong)(LPCSTR field, ULONGLONG* value) = 0;
	STDMETHOD(GetDouble)(LPCSTR field, double* value) = 0;
	STDMETHOD(GetString)(LPCSTR field, LPWSTR* value, int* chars) = 0;
	STDMETHOD(GetBin)(LPCSTR field, LPVOID* value, int* size) = 0;
};


DECLARE_INTERFACE_IID_(IMadVRSettings, IUnknown,
	"6F8A566C-4E19-439E-8F07-20E46ED06DEE")
{
	STDMETHOD_(BOOL, SettingsGetRevision)(LONGLONG* revision) = 0;
	STDMETHOD_(BOOL, SettingsExport)(LPVOID* buffer, int* size) = 0;
	STDMETHOD_(BOOL, SettingsImport)(LPVOID buffer, int size) = 0;
	STDMETHOD_(BOOL, SettingsSetString)(LPCWSTR path, LPCWSTR value) = 0;
	STDMETHOD_(BOOL, SettingsSetInteger)(LPCWSTR path, int value) = 0;
	STDMETHOD_(BOOL, SettingsSetBoolean)(LPCWSTR path, BOOL value) = 0;
	STDMETHOD_(BOOL, SettingsGetString)(
		LPCWSTR path, LPCWSTR value, int* bufferLengthInChars) = 0;
	STDMETHOD_(BOOL, SettingsGetInteger)(LPCWSTR path, int* value) = 0;
	STDMETHOD_(BOOL, SettingsGetBoolean)(LPCWSTR path, BOOL* value) = 0;
	STDMETHOD_(BOOL, SettingsGetBinary)(
		LPCWSTR path, LPVOID* value, int* bufferLengthInBytes) = 0;
};
