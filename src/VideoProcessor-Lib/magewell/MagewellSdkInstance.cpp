/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "MagewellSdkInstance.h"

#include <LibMWCapture/MWCapture.h>

#include <mutex>


namespace
{
	std::mutex g_magewellSdkMutex;
	std::weak_ptr<MagewellSdkInstance> g_magewellSdkInstance;
}


std::shared_ptr<MagewellSdkInstance> MagewellSdkInstance::Acquire()
{
	std::lock_guard<std::mutex> lock(g_magewellSdkMutex);

	if (auto existing = g_magewellSdkInstance.lock())
		return existing;

	if (!MWCaptureInitInstance())
		return nullptr;

	// std::make_shared cannot reach the private constructor, so the instance
	// is created directly and handed to shared_ptr with an explicit deleter.
	std::shared_ptr<MagewellSdkInstance> instance(
		new MagewellSdkInstance(),
		[](MagewellSdkInstance* value) { delete value; });

	g_magewellSdkInstance = instance;
	return instance;
}


MagewellSdkInstance::~MagewellSdkInstance()
{
	MWCaptureExitInstance();
}
