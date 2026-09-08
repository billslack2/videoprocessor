#pragma once

#include <exception>
#include <winerror.h>

// A vendor invokes these callbacks on its own thread. Neither application
// work nor error reporting may unwind through that ABI boundary.
template<class Work, class Report>
HRESULT CaptureCallbackBoundary(Work&& work, Report&& report) noexcept
{
	try
	{
		return work();
	}
	catch (const std::exception& error)
	{
		try { report(error.what()); } catch (...) {}
	}
	catch (...)
	{
		try { report("unknown C++ exception"); } catch (...) {}
	}
	return E_FAIL;
}
