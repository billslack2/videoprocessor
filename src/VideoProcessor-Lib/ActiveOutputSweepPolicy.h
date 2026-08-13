#pragma once

#include <RendererOutputContractStatus.h>
#include <string>

namespace ActiveOutputSweepPolicy
{
	enum class Disposition
	{
		EXACT,
		FALLBACK,
		BLOCKED,
	};

	enum class Verdict
	{
		WAITING,
		PASS,
		EXPECTED,
		MEASURE,
		FAIL,
	};

	struct Expected
	{
		Disposition disposition = Disposition::EXACT;
		RendererOutputContract::Presentation presentation =
			RendererOutputContract::Presentation::UNKNOWN;
		RendererOutputContract::Range range =
			RendererOutputContract::Range::UNKNOWN;
		RendererOutputContract::Transfer transfer =
			RendererOutputContract::Transfer::UNKNOWN;
		RendererOutputContract::Primaries primaries =
			RendererOutputContract::Primaries::UNKNOWN;
		bool requireVpOwner = false;
		bool requireDxgiVerification = false;
		uint32_t swapchainBitDepth = 0;
		bool measurementRequired = false;
	};

	struct Decision
	{
		Verdict verdict = Verdict::WAITING;
		std::string reason;

		bool IsSuccessful() const
		{
			return verdict == Verdict::PASS || verdict == Verdict::EXPECTED ||
				verdict == Verdict::MEASURE;
		}
	};

	inline Decision Evaluate(const Expected& expected,
		const RendererOutputContract::Status& actual)
	{
		using namespace RendererOutputContract;
		if (!actual.available)
			return { Verdict::WAITING, "renderer output state is not available" };

		if (expected.disposition == Disposition::BLOCKED)
		{
			if (!actual.safeToRender)
				return { Verdict::EXPECTED, "requested contract was blocked as expected" };
			return { Verdict::FAIL, "expected a blocked contract, but rendering remained enabled" };
		}

		if (!actual.safeToRender)
			return { Verdict::FAIL, "renderer blocked the requested output contract: " + actual.reason };
		if (actual.successfulPresents == 0)
			return { Verdict::WAITING, "output contract has not produced a successful Present" };

		if (expected.disposition == Disposition::FALLBACK)
		{
			if (actual.requestedContractActive)
				return { Verdict::FAIL, "expected policy fallback, but the requested contract was reported active" };
			if (expected.presentation != Presentation::UNKNOWN &&
				actual.presentation != expected.presentation)
				return { Verdict::FAIL, "fallback presentation model does not match the expected safe contract" };
			if (expected.range != Range::UNKNOWN && actual.range != expected.range)
				return { Verdict::FAIL, "fallback output range does not match the expected safe contract" };
			if (expected.transfer != Transfer::UNKNOWN && actual.transfer != expected.transfer)
				return { Verdict::FAIL, "fallback pixel transfer does not match the expected safe contract" };
			if (expected.primaries != Primaries::UNKNOWN &&
				actual.primaries != expected.primaries)
				return { Verdict::FAIL, "fallback target primaries do not match the test configuration" };
			if (actual.vpOwnsPresentation != expected.requireVpOwner)
				return { Verdict::FAIL, expected.requireVpOwner ?
					"fallback did not retain VP presentation ownership" :
					"fallback unexpectedly changed to VP presentation ownership" };
			if (expected.requireDxgiVerification && !actual.dxgiAppliedVerified)
				return { Verdict::FAIL, "fallback DXGI Check/Set/Check application was not verified" };
			if (expected.swapchainBitDepth != 0 &&
				actual.swapchainBitDepth != expected.swapchainBitDepth)
				return { Verdict::FAIL, "fallback swapchain bit depth does not match the test contract" };
			return { Verdict::EXPECTED, "policy fallback occurred as expected" };
		}

		if (!actual.requestedContractActive)
			return { Verdict::FAIL, "requested output contract fell back unexpectedly" };
		if (expected.presentation != Presentation::UNKNOWN &&
			actual.presentation != expected.presentation)
			return { Verdict::FAIL, "actual presentation model does not match the test contract" };
		if (expected.range != Range::UNKNOWN && actual.range != expected.range)
			return { Verdict::FAIL, "actual output range does not match the test contract" };
		if (expected.transfer != Transfer::UNKNOWN && actual.transfer != expected.transfer)
			return { Verdict::FAIL, "actual pixel transfer does not match the test contract" };
		if (expected.primaries != Primaries::UNKNOWN &&
			actual.primaries != expected.primaries)
			return { Verdict::FAIL, "actual target primaries do not match the test configuration" };
		if (actual.vpOwnsPresentation != expected.requireVpOwner)
			return { Verdict::FAIL, expected.requireVpOwner ?
				"VP did not own presentation" :
				"VP unexpectedly owned presentation" };
		if (expected.requireDxgiVerification && !actual.dxgiAppliedVerified)
			return { Verdict::FAIL, "DXGI Check/Set/Check application was not verified" };
		if (expected.swapchainBitDepth != 0 &&
			actual.swapchainBitDepth != expected.swapchainBitDepth)
			return { Verdict::FAIL, "swapchain bit depth does not match the test contract" };

		return expected.measurementRequired ?
			Decision{ Verdict::MEASURE,
				"metadata and Present match; physical display response requires visual or meter grading" } :
			Decision{ Verdict::PASS, "metadata and Present match the expected contract" };
	}
}
