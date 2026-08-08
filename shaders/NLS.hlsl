/*
 * VideoProcessor nonlinear stretch (NLS)
 *
 * A continuous SuperView-inspired mapping intended for use before resize while
 * madVR supplies the final zoom/aspect geometry. Classic mode implements the
 * established quadratic mapping. Protected mode preserves a configurable
 * central band and moves more of the required distortion toward the edges.
 * VideoProcessor selects the horizontal axis for content narrower than the
 * configured screen and the vertical axis for content wider than the screen.
 *
 * $MinimumShaderProfile: ps_3_0
 */

sampler s0 : register(s0);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    const float strength = saturate({{strength}});
    // Limits keep the coordinate mapping monotonic for all accepted settings.
	// A classic power curve needs an exponent of at least one to preserve a
	// monotonic source-coordinate map. The upper bound also keeps its edge
	// slope positive at the largest supported stretch ratio.
	const float curve = clamp({{curve}}, 1.0, 2.9);
	const float stretchRatio = clamp({{stretch_ratio}}, 1.0, 1.5);
	const bool verticalWarp = {{warp_axis}} >= 0.5;
	// VP publishes a stable active-picture boundary. Pixels outside it are
	// preserved byte-for-byte by sampling the original coordinate, so madVR can
	// remove its independently detected bars exactly once after this pass.
	const float activeLeft = saturate({{active_left}});
	const float activeTop = saturate({{active_top}});
	const float activeRight = clamp({{active_right}}, activeLeft + 0.01, 1.0);
	const float activeBottom = clamp({{active_bottom}}, activeTop + 0.01, 1.0);
	const float activeWidthFraction = activeRight - activeLeft;
	const float activeHeightFraction = activeBottom - activeTop;
	const int geometry = (int)clamp({{geometry}}, 0.0, 1.0);
	const int quality = (int)clamp({{quality}}, 0.0, 3.0);
	const bool safeFit = {{safe_fit}} >= 0.5;
	const bool safeFitVertical = {{safe_fit_axis}} >= 0.5;
	const float safeFitFraction =
		clamp({{safe_fit_fraction}}, 0.01, 1.0);

	if (safeFit)
	{
		// Excessive nonlinear expansion is replaced by a centered safe fit.
		// The declared output remains the selected viewport aspect, while the
		// active picture retains its geometry inside side or top/bottom bars.
		float2 fittedTex = tex;
		float safeFitStart = (1.0 - safeFitFraction) * 0.5;
		float safeFitEnd = 1.0 - safeFitStart;
		float fittedCoordinate =
			safeFitVertical ? tex.y : tex.x;
		if (fittedCoordinate < safeFitStart ||
			fittedCoordinate > safeFitEnd)
			return float4(0.0, 0.0, 0.0, 1.0);
		fittedCoordinate =
			(fittedCoordinate - safeFitStart) / safeFitFraction;
		if (safeFitVertical)
			fittedTex.y = fittedCoordinate;
		else
			fittedTex.x = fittedCoordinate;
		return tex2D(s0, float2(
			lerp(activeLeft, activeRight, fittedTex.x),
			lerp(activeTop, activeBottom, fittedTex.y)));
	}

	const float2 activeMinimum = float2(activeLeft, activeTop);
	const float2 activeMaximum = float2(activeRight, activeBottom);
	const float2 activeSize = activeMaximum - activeMinimum;
	const bool insideActivePicture =
		tex.x >= activeLeft && tex.x <= activeRight &&
		tex.y >= activeTop && tex.y <= activeBottom;
	// Clamp only the coordinate used to calculate the active-local map. Exterior
	// pixels still sample their exact original coordinate below.
	float2 activeTex = saturate((tex - activeMinimum) / activeSize);
	float centeredCoordinate =
		(verticalWarp ? activeTex.y : activeTex.x) * 2.0 - 1.0;
	float radius = abs(centeredCoordinate);

	float mappedRadius;
	const float effectiveRatio = lerp(1.0, stretchRatio, strength);
	// A maximum central band of 45% leaves enough outer span for a monotonic
	// transition across every accepted stretch ratio and curve setting.
	const float centerProtection = clamp({{center_protection}}, 0.0, 0.45);
	if (geometry == 0 || centerProtection <= 0.0)
	{
		// Preserve the centre after the presentation layer's required linear
		// expansion, then return smoothly to the untouched source edge. The
		// previous r * localScale formulation had a 0.356 edge slope for the
		// default 16:9-to-scope ratio and curve=2, making the sides appear
		// crushed. This normalized power curve gives 0.678 instead: plainly
		// nonlinear while retaining believable edge geometry.
		mappedRadius = effectiveRatio * radius -
			(effectiveRatio - 1.0) * pow(radius, curve);
	}
	else if (radius <= centerProtection)
	{
		// Counter the renderer's final linear expansion throughout the protected
		// band so faces and other central geometry retain their original width.
		mappedRadius = radius * effectiveRatio;
	}
	else
	{
		// Join the protected band to the untouched source edge with a quintic
		// Hermite segment. Matching zero second derivative at both ends avoids a
		// visible change in curvature during horizontal camera movement.
		float span = 1.0 - centerProtection;
		float t = saturate((radius - centerProtection) / span);
		float t2 = t * t;
		float t3 = t2 * t;
		float t4 = t3 * t;
		float t5 = t4 * t;
		float h00 = 1.0 - 10.0 * t3 + 15.0 * t4 - 6.0 * t5;
		float h10 = t - 6.0 * t3 + 8.0 * t4 - 3.0 * t5;
		float h01 = 10.0 * t3 - 15.0 * t4 + 6.0 * t5;
		float h11 = -4.0 * t3 + 7.0 * t4 - 3.0 * t5;

		float startPosition = centerProtection * effectiveRatio;
		float startSlope = effectiveRatio;
		// Curve remains useful in protected mode: larger values place more
		// stretch near the sides. The lower bound preserves monotonic sampling.
		float edgeSlope = max(0.20,
			1.0 - 0.5 * (effectiveRatio - 1.0) * curve);
		mappedRadius = h00 * startPosition + h10 * span * startSlope +
			h01 + h11 * span * edgeSlope;
	}

	centeredCoordinate =
		(centeredCoordinate < 0.0 ? -mappedRadius : mappedRadius);
	float warpedCoordinate = centeredCoordinate * 0.5 + 0.5;

	// The nonlinear map changes its derivative across the image. Use that
	// derivative as the reconstruction footprint so filtering grows only where
	// the warp needs it instead of applying a fixed full-frame blur. Vertical
	// samples remain inside the detected active-picture region so reconstruction
	// never blends an encoded bar into the picture.
	float footprint = verticalWarp ?
		max(abs(ddy(warpedCoordinate)), abs(ddy(activeTex.y))) :
		max(abs(ddx(warpedCoordinate)), abs(ddx(activeTex.x)));
	if (!insideActivePicture)
		return tex2D(s0, tex);
	float2 sampleTex = tex;
	if (verticalWarp)
		sampleTex.y = lerp(activeTop, activeBottom, warpedCoordinate);
	else
		sampleTex.x = lerp(activeLeft, activeRight, warpedCoordinate);
	float2 sampleAxis = verticalWarp ?
		float2(0.0, activeHeightFraction) : float2(activeWidthFraction, 0.0);
	if (quality == 0)
		return tex2D(s0, sampleTex);

	if (quality == 1)
	{
		// Four-tap symmetric filter: modest cost and the recommended balance for
		// real-time 4K madVR use.
		float4 color = 0.0;
		color += tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 1.125, activeMinimum, activeMaximum)) * 0.125;
		color += tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 0.375, activeMinimum, activeMaximum)) * 0.375;
		color += tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 0.375, activeMinimum, activeMaximum)) * 0.375;
		color += tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 1.125, activeMinimum, activeMaximum)) * 0.125;
		return color;
	}

	if (quality == 2)
	{
		// Six-tap Lanczos-3-style reconstruction. This is the recommended sharp
		// setting: less expensive and less prone to ringing than Lanczos-4.
		float4 c0 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 2.5, activeMinimum, activeMaximum));
		float4 c1 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 1.5, activeMinimum, activeMaximum));
		float4 c2 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 0.5, activeMinimum, activeMaximum));
		float4 c3 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 0.5, activeMinimum, activeMaximum));
		float4 c4 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 1.5, activeMinimum, activeMaximum));
		float4 c5 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 2.5, activeMinimum, activeMaximum));
		float4 color =
			(c2 + c3) * 0.6114130435 +
			(c1 + c4) * -0.1358695652 +
			(c0 + c5) * 0.0244565217;
		float4 neighborhoodMin = min(min(c0, c1), min(min(c2, c3), min(c4, c5)));
		float4 neighborhoodMax = max(max(c0, c1), max(max(c2, c3), max(c4, c5)));
		return clamp(color, neighborhoodMin, neighborhoodMax);
	}

	// Eight-tap Lanczos-4-style reconstruction. The normalized windowed-sinc
	// coefficients include negative lobes for sharper detail. Clamp the result
	// to the sampled neighborhood to prevent visible HDR/text ringing.
	float4 c0 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 3.5, activeMinimum, activeMaximum));
	float4 c1 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 2.5, activeMinimum, activeMaximum));
	float4 c2 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 1.5, activeMinimum, activeMaximum));
	float4 c3 = tex2D(s0, clamp(sampleTex - sampleAxis * footprint * 0.5, activeMinimum, activeMaximum));
	float4 c4 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 0.5, activeMinimum, activeMaximum));
	float4 c5 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 1.5, activeMinimum, activeMaximum));
	float4 c6 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 2.5, activeMinimum, activeMaximum));
	float4 c7 = tex2D(s0, clamp(sampleTex + sampleAxis * footprint * 3.5, activeMinimum, activeMaximum));
	float4 color =
		(c3 + c4) * 0.6188774241 +
		(c2 + c5) * -0.1660113634 +
		(c1 + c6) * 0.0597640908 +
		(c0 + c7) * -0.0126301515;
	float4 neighborhoodMin = min(min(min(c0, c1), min(c2, c3)),
		min(min(c4, c5), min(c6, c7)));
	float4 neighborhoodMax = max(max(max(c0, c1), max(c2, c3)),
		max(max(c4, c5), max(c6, c7)));
	return clamp(color, neighborhoodMin, neighborhoodMax);
}
