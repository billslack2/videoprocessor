/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <memory>


/**
 * Ownership token for the Magewell SDK global instance.
 *
 * MWCaptureInitInstance() and MWCaptureExitInstance() bracket all other SDK
 * use. The discoverer and every device it produces hold one of these, so the
 * SDK stays initialised for as long as any of them is alive regardless of the
 * order in which they are released.
 */
class MagewellSdkInstance
{
public:

	// Returns a shared token, initialising the SDK if this is the first
	// holder. Returns nullptr if the SDK could not be initialised, which is
	// the normal result on a machine with no Magewell runtime installed.
	static std::shared_ptr<MagewellSdkInstance> Acquire();

	~MagewellSdkInstance();

	MagewellSdkInstance(const MagewellSdkInstance&) = delete;
	MagewellSdkInstance& operator=(const MagewellSdkInstance&) = delete;

private:

	MagewellSdkInstance() = default;
};


typedef std::shared_ptr<MagewellSdkInstance> MagewellSdkInstancePtr;
