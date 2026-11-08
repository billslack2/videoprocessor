// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: a child process on a terminal. spotread reads single
// keys from its console (it needs a TTY), so VP runs it under a Windows
// pseudo-console (ConPTY, CreatePseudoConsole, Windows 10 1809 or newer)
// rather than with redirected pipes. The interface lets the CPU tests drive
// the session with a scripted in-process terminal, and the integration tests
// with a fake spotread executable under the real ConPTY.

#include <memory>
#include <string>
#include <vector>

namespace Calibration
{
	class ITerminal
	{
	public:
		virtual ~ITerminal() = default;
		// commandLine: the full Windows command line (program first).
		virtual bool Start(const std::wstring& program, const std::wstring& commandLine,
			const std::wstring& workingDirectory, std::string& error) = 0;
		// Keys / text to the child's console input.
		virtual bool Write(const std::string& bytes) = 0;
		// Appends whatever output arrived, waiting up to timeoutMs for some;
		// returns false on timeout with nothing new.
		virtual bool Read(std::string& output, int timeoutMs) = 0;
		// True once the child has exited (exitCode set).
		virtual bool Exited(unsigned long& exitCode) = 0;
		// Ends the child this terminal started (never any other process).
		virtual void Terminate() = 0;
	};

	// The real terminal: the child under a pseudo-console, inside a job object
	// that ends it if VP itself ends. Columns are wide so spotread's lines do
	// not wrap.
	std::unique_ptr<ITerminal> CreateConPtyTerminal(short columns = 240, short rows = 60);
	// True when this Windows has CreatePseudoConsole.
	bool ConPtyAvailable();

	// Runs a program with redirected output (no console input), for spotread's
	// usage text (`spotread -?`), bounded by timeoutMs; the child is ended at
	// the timeout. False when it could not be started.
	bool RunCaptured(const std::wstring& program, const std::wstring& commandLine, int timeoutMs,
		std::string& output, unsigned long& exitCode, std::string& error);
}
