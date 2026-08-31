#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

// A latest-wins mailbox whose payload and viewport boundary are consumed as
// one value. This prevents a render frame from pairing settings from request N
// with target geometry from request N+1.
template <typename Settings>
class ViewportIntentMailbox
{
public:
	struct Intent
	{
		Settings settings;
		bool configuredScreenActive = false;
		uint64_t viewportRequestSerial = 0;
		int64_t viewportRequestNs = 0;
	};

	bool Publish(const Settings& settings, bool configuredScreenActive,
		uint64_t viewportRequestSerial, int64_t viewportRequestNs)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		const bool superseded = m_pending != nullptr;
		m_pending.reset(new Intent{
			settings, configuredScreenActive,
			viewportRequestSerial, viewportRequestNs });
		return superseded;
	}

	bool HasPending() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		return m_pending != nullptr;
	}

	bool Consume(Intent& intent)
	{
		std::unique_ptr<Intent> pending;
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			pending.swap(m_pending);
		}
		if (!pending)
			return false;
		intent = std::move(*pending);
		return true;
	}

private:
	mutable std::mutex m_mutex;
	std::unique_ptr<Intent> m_pending;
};
