#pragma once

#include <vprenderer/LibplaceboExternalHdrLutSet.h>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <libplacebo/renderer.h>
#pragma warning(pop)

namespace LibplaceboExternalHdrLut
{
	struct FrameProjection
	{
		ResolvedResource resolved;
		bool attached = false;
	};

	inline enum pl_color_primaries SlotPrimaries(Slot slot)
	{
		switch (slot)
		{
		case Slot::BT709: return PL_COLOR_PRIM_BT_709;
		case Slot::P3_D65: return PL_COLOR_PRIM_DISPLAY_P3;
		case Slot::BT2020: return PL_COLOR_PRIM_BT_2020;
		default: return PL_COLOR_PRIM_UNKNOWN;
		}
	}

	inline enum pl_color_primaries OutputPrimaries(Primaries primaries)
	{
		switch (primaries)
		{
		case Primaries::BT709: return PL_COLOR_PRIM_BT_709;
		case Primaries::P3_D65: return PL_COLOR_PRIM_DISPLAY_P3;
		case Primaries::BT2020: return PL_COLOR_PRIM_BT_2020;
		default: return PL_COLOR_PRIM_UNKNOWN;
		}
	}

	inline Primaries FromLibplaceboPrimaries(enum pl_color_primaries primaries)
	{
		switch (primaries)
		{
		case PL_COLOR_PRIM_BT_709: return Primaries::BT709;
		case PL_COLOR_PRIM_DISPLAY_P3: return Primaries::P3_D65;
		case PL_COLOR_PRIM_BT_2020: return Primaries::BT2020;
		default: return Primaries::UNKNOWN;
		}
	}

	inline bool IsSdrOutputTarget(const struct pl_frame& target)
	{
		return target.repr.sys == PL_COLOR_SYSTEM_RGB &&
			target.color.transfer != PL_COLOR_TRC_UNKNOWN &&
			target.color.transfer != PL_COLOR_TRC_PQ &&
			target.color.transfer != PL_COLOR_TRC_HLG;
	}

	// Produces the complete frame-local render description. The caller owns both
	// output objects until the synchronous pl_render_image call returns. The
	// external Cube replaces HDR tone/gamut conversion and produces the ordinary
	// SDR target selected by the active VP profile. If that complete contract is
	// unavailable, the shared internal tone-mapping description remains intact.
	inline FrameProjection PrepareFrameProjection(
		const ActiveSet& activeSet,
		uint64_t expectedTransactionGeneration,
		ToneMappingMode requestedMode,
		SdrOutputReference outputReference,
		Primaries sourcePrimaries,
		const struct pl_color_space& sourceColor,
		const struct pl_frame& sharedTarget,
		const struct pl_render_params& sharedParams,
		const struct pl_gamut_map_function* fallbackGamutMapper,
		struct pl_frame& frameTarget,
		struct pl_render_params& frameParams,
		struct pl_custom_lut& frameLut,
		struct pl_color_map_params& frameColorMapParams)
	{
		frameTarget = sharedTarget;
		frameParams = sharedParams;
		FrameProjection projection;
		const bool inputIsPq = sourceColor.transfer == PL_COLOR_TRC_PQ;
		const bool targetIsSdrRgb = IsSdrOutputTarget(sharedTarget);
		projection.resolved = activeSet.Resolve(
			expectedTransactionGeneration,
			inputIsPq && targetIsSdrRgb && outputReference.Valid() ? requestedMode :
				ToneMappingMode::PIXEL_SHADERS,
			inputIsPq, sourcePrimaries);
		if (!inputIsPq || !targetIsSdrRgb || !outputReference.Valid() ||
			!projection.resolved.selection.useExternalLut ||
			!projection.resolved.lut ||
			!activeSet.IsCurrent(projection.resolved))
		{
			return projection;
		}

		frameLut = *projection.resolved.lut;
		frameLut.color_in = sourceColor;
		if (projection.resolved.selection.requiresExplicitPrimariesTransform)
		{
			if (!fallbackGamutMapper)
				return projection;
			frameLut.color_in.primaries =
				SlotPrimaries(projection.resolved.selection.slot);
			frameColorMapParams = sharedParams.color_map_params ?
				*sharedParams.color_map_params : pl_color_map_params{};
			frameColorMapParams.gamut_mapping = fallbackGamutMapper;
			frameParams.color_map_params = &frameColorMapParams;
		}
		frameTarget.color.primaries = OutputPrimaries(outputReference.primaries);
		frameTarget.color.hdr.max_luma =
			static_cast<float>(outputReference.peakNits);
		frameLut.color_out = frameTarget.color;
		frameParams.lut = &frameLut;
		frameParams.lut_type = PL_LUT_CONVERSION;
		frameParams.peak_detect_params = nullptr;
		frameTarget.lut = nullptr;
		frameTarget.lut_type = PL_LUT_UNKNOWN;
		projection.attached = true;
		return projection;
	}
}
