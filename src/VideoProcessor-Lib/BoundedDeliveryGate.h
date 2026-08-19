#pragma once

#include <chrono>
#include <functional>
#include <mutex>


// Presentation-only control work may briefly serialize with Deliver so one
// sample cannot observe a half-applied aspect/shader contract. It must never
// wait indefinitely: Deliver is third-party code and the graph reset capable
// of releasing it runs on the same owner queue as this control transaction.
inline bool TryRunBoundedDeliveryTransaction(
	std::timed_mutex& gate,
	std::chrono::milliseconds timeout,
	const std::function<void()>& operation)
{
	std::unique_lock<std::timed_mutex> lock(gate, std::defer_lock);
	if (!lock.try_lock_for(timeout))
		return false;
	operation();
	return true;
}
