/*
 * VideoProcessor nonlinear horizontal stretch (NLS)
 *
 * A continuous SuperView-inspired mapping intended for use before resize while
 * madVR supplies the final zoom/aspect geometry. The centre receives most of
 * the correction and the scale progressively returns to 1:1 at both edges.
 *
 * $MinimumShaderProfile: ps_3_0
 */

sampler s0 : register(s0);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    const float strength = saturate({{strength}});
    // Limits keep the coordinate mapping monotonic for all accepted settings.
    const float curve = clamp({{curve}}, 0.5, 4.0);
    const float stretchRatio = clamp({{stretch_ratio}}, 1.0, 1.5);
	const int quality = (int)clamp({{quality}}, 0.0, 3.0);

    float centeredX = tex.x * 2.0 - 1.0;
    float radius = abs(centeredX);

    // The half-to-one domain keeps some intentional stretch at the exact
    // centre, then reduces it continuously toward the untouched image edges.
    // A curve of 2 and ratio of 4/3 are useful defaults for 16:9-to-scope.
    float edgeWeight = pow(0.5 + 0.5 * radius, curve);
    float correction = strength * (1.0 - edgeWeight);
    float localScale = lerp(1.0, stretchRatio, correction);

    centeredX = clamp(centeredX * localScale, -1.0, 1.0);
    float warpedX = centeredX * 0.5 + 0.5;

	// The nonlinear map changes its derivative across the image. Use that
	// derivative as the reconstruction footprint so filtering grows only where
	// the warp needs it instead of applying a fixed full-frame blur.
	float footprint = max(abs(ddx(warpedX)), abs(ddx(tex.x)));
	float2 sampleTex = float2(warpedX, tex.y);
	if (quality == 0)
		return tex2D(s0, sampleTex);

	if (quality == 1)
	{
		// Four-tap symmetric filter: modest cost and the recommended balance for
		// real-time 4K madVR use.
		float4 color = 0.0;
		color += tex2D(s0, float2(saturate(warpedX - footprint * 1.125), tex.y)) * 0.125;
		color += tex2D(s0, float2(saturate(warpedX - footprint * 0.375), tex.y)) * 0.375;
		color += tex2D(s0, float2(saturate(warpedX + footprint * 0.375), tex.y)) * 0.375;
		color += tex2D(s0, float2(saturate(warpedX + footprint * 1.125), tex.y)) * 0.125;
		return color;
	}

	if (quality == 2)
	{
		// Six-tap Lanczos-3-style reconstruction. This is the recommended sharp
		// setting: less expensive and less prone to ringing than Lanczos-4.
		float4 c0 = tex2D(s0, float2(saturate(warpedX - footprint * 2.5), tex.y));
		float4 c1 = tex2D(s0, float2(saturate(warpedX - footprint * 1.5), tex.y));
		float4 c2 = tex2D(s0, float2(saturate(warpedX - footprint * 0.5), tex.y));
		float4 c3 = tex2D(s0, float2(saturate(warpedX + footprint * 0.5), tex.y));
		float4 c4 = tex2D(s0, float2(saturate(warpedX + footprint * 1.5), tex.y));
		float4 c5 = tex2D(s0, float2(saturate(warpedX + footprint * 2.5), tex.y));
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
	float4 c0 = tex2D(s0, float2(saturate(warpedX - footprint * 3.5), tex.y));
	float4 c1 = tex2D(s0, float2(saturate(warpedX - footprint * 2.5), tex.y));
	float4 c2 = tex2D(s0, float2(saturate(warpedX - footprint * 1.5), tex.y));
	float4 c3 = tex2D(s0, float2(saturate(warpedX - footprint * 0.5), tex.y));
	float4 c4 = tex2D(s0, float2(saturate(warpedX + footprint * 0.5), tex.y));
	float4 c5 = tex2D(s0, float2(saturate(warpedX + footprint * 1.5), tex.y));
	float4 c6 = tex2D(s0, float2(saturate(warpedX + footprint * 2.5), tex.y));
	float4 c7 = tex2D(s0, float2(saturate(warpedX + footprint * 3.5), tex.y));
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
