#pragma once

#include <cstdint>

#include <RendererResetPolicy.h>


enum class RendererResetScope
{
	LiveQueue,
	Graph,
	GraphRetarget,
};


// Describes who initiated a reset independently of the policy reason which
// selects its priority.  In particular, an automatic queue-profile apply can
// intentionally retain Manual priority without being reported as a user
// command.
enum class RendererResetOrigin : uint8_t
{
	Unspecified,
	UserManual,
	AutomaticProfile,
	AutomaticReadiness,
	AutomaticRetargetSettle,
};


using RendererResetOriginContributors = uint32_t;


constexpr RendererResetOriginContributors RendererResetOriginBit(
	RendererResetOrigin origin)
{
	const uint32_t value = static_cast<uint32_t>(origin);
	return value == 0 || value > 32 ? 0u : 1u << (value - 1);
}


constexpr bool RendererResetHasOrigin(
	RendererResetOriginContributors contributors,
	RendererResetOrigin origin)
{
	const RendererResetOriginContributors bit = RendererResetOriginBit(origin);
	return bit != 0 && (contributors & bit) != 0;
}


// Only an ordinary graph stop/reset/run preserves the display measurement
// that selected an automatic readiness reset. A GraphRetarget changes the
// output/window path and must collect fresh DXGI evidence even if it also
// covers a readiness contributor.
constexpr bool RendererResetPreservesReadinessDisplayMeasurement(
	RendererResetScope scope,
	RendererResetOriginContributors contributors)
{
	return scope == RendererResetScope::Graph &&
		RendererResetHasOrigin(
			contributors, RendererResetOrigin::AutomaticReadiness);
}


// When a newer retarget subsumes an older delayed retarget-settle request, the
// newer retarget publishes a fresh lineage and schedules its own delayed phase.
// A successful ordinary Graph reset supersedes that phase outright.
constexpr bool RendererResetKeepsFreshRetargetSettleLineage(
	RendererResetScope scope, bool succeeded)
{
	return succeeded && scope == RendererResetScope::GraphRetarget;
}


using RendererBindingToken = uint64_t;


struct RendererResetRequest
{
	RendererBindingToken bindingToken = 0;
	uint64_t sequence = 0;
	uint64_t backendEpoch = 0;
	RendererResetReason reason = RendererResetReason::None;
	RendererResetScope scope = RendererResetScope::Graph;
	RendererResetOrigin origin = RendererResetOrigin::Unspecified;
	uint64_t originGeneration = 0;
	// The selected request keeps its own origin.  This mask records every
	// coalesced request which the selected operation is expected to cover.
	RendererResetOriginContributors originContributors = 0;
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
