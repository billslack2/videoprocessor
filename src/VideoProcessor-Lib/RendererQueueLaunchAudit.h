#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>


template<typename T>
struct RendererQueueLaunchAuditValue
{
	bool available = false;
	T value = {};

	RendererQueueLaunchAuditValue& operator=(const T& assigned)
	{
		available = true;
		value = assigned;
		return *this;
	}
};


// One parseable schema is shared by construction-commit and stable lifecycle
// evidence. Unknown values remain explicit instead of disappearing from an
// earlier phase and changing the meaning of downstream diagnostics.
struct RendererQueueLaunchAuditRecord
{
	std::string trigger = "unknown";
	std::string phase = "unknown";
	std::string backend = "unknown";
	std::string profile = "unavailable";
	uint32_t generation = 0;
	uint64_t profileGeneration = 0;
	uint64_t effectiveRevision = 0;
	uint64_t desiredRevision = 0;
	uint64_t constructedRevision = 0;
	RendererQueueLaunchAuditValue<size_t> desired;
	RendererQueueLaunchAuditValue<size_t> retained;
	RendererQueueLaunchAuditValue<size_t> constructed;
	RendererQueueLaunchAuditValue<size_t> current;
	RendererQueueLaunchAuditValue<size_t> allocatorRequested;
	RendererQueueLaunchAuditValue<size_t> allocatorActual;
	RendererQueueLaunchAuditValue<size_t> prime;
	RendererQueueLaunchAuditValue<size_t> reservoir;
	RendererQueueLaunchAuditValue<size_t> downstreamEstimate;
	RendererQueueLaunchAuditValue<int> estimateKnown;
	RendererQueueLaunchAuditValue<int> estimateSatisfied;
	std::string state = "unknown";
	RendererQueueLaunchAuditValue<int> settled;
	RendererQueueLaunchAuditValue<int> reset;
	RendererQueueLaunchAuditValue<int> action;
};


template<typename T>
inline void AppendRendererQueueLaunchAuditValue(
	std::ostringstream& stream,
	const RendererQueueLaunchAuditValue<T>& value)
{
	if (value.available)
		stream << value.value;
	else
		stream << "unavailable";
}


inline std::string FormatRendererQueueLaunchAudit(
	const RendererQueueLaunchAuditRecord& record)
{
	std::ostringstream stream;
	stream << "Renderer queue launch contract audit: "
		<< "trigger=" << record.trigger
		<< " phase=" << record.phase
		<< " backend=" << record.backend
		<< " profile=" << record.profile
		<< " generation=" << record.generation
		<< " profile_generation=" << record.profileGeneration
		<< " effective_revision=" << record.effectiveRevision
		<< " desired_revision=" << record.desiredRevision
		<< " constructed_revision=" << record.constructedRevision
		<< " desired=";
	AppendRendererQueueLaunchAuditValue(stream, record.desired);
	stream << " retained=";
	AppendRendererQueueLaunchAuditValue(stream, record.retained);
	stream << " constructed=";
	AppendRendererQueueLaunchAuditValue(stream, record.constructed);
	stream << " current=";
	AppendRendererQueueLaunchAuditValue(stream, record.current);
	stream << " allocator_requested=";
	AppendRendererQueueLaunchAuditValue(stream, record.allocatorRequested);
	stream << " allocator_actual=";
	AppendRendererQueueLaunchAuditValue(stream, record.allocatorActual);
	stream << " prime=";
	AppendRendererQueueLaunchAuditValue(stream, record.prime);
	stream << " reservoir=";
	AppendRendererQueueLaunchAuditValue(stream, record.reservoir);
	stream << " downstream_estimate=";
	AppendRendererQueueLaunchAuditValue(stream, record.downstreamEstimate);
	stream << " estimate_known=";
	AppendRendererQueueLaunchAuditValue(stream, record.estimateKnown);
	stream << " estimate_satisfied=";
	AppendRendererQueueLaunchAuditValue(stream, record.estimateSatisfied);
	stream << " state=" << record.state
		<< " settled=";
	AppendRendererQueueLaunchAuditValue(stream, record.settled);
	stream << " reset=";
	AppendRendererQueueLaunchAuditValue(stream, record.reset);
	stream << " action=";
	AppendRendererQueueLaunchAuditValue(stream, record.action);
	return stream.str();
}
