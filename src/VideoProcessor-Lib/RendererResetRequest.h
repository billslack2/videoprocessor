#pragma once

#include <cstdint>

#include <RendererResetPolicy.h>


enum class RendererResetScope
{
	LiveQueue,
	Graph,
	GraphRetarget,
};


using RendererBindingToken = uint64_t;


struct RendererResetRequest
{
	RendererBindingToken bindingToken = 0;
	uint64_t sequence = 0;
	uint64_t backendEpoch = 0;
	RendererResetReason reason = RendererResetReason::None;
	RendererResetScope scope = RendererResetScope::Graph;
	uintptr_t targetWindow = 0;
	uint64_t requestedTick = 0;
	uint64_t deadlineTick = 0;
};


class IRendererResetRequestSink
{
public:
	virtual ~IRendererResetRequestSink() = default;

	// Thread-safe, non-blocking with respect to UI and graph control. The sink
	// stamps its immutable renderer binding token onto the request.
	virtual void Submit(RendererResetRequest request) noexcept = 0;
};
