float2 texOffset(float shadowMapSize, int u, int v)
{
    return float2(u * 1.0f / shadowMapSize, v * 1.0f / shadowMapSize);
}

float depthTest(Texture2D shadowMap, SamplerState samplerState, float2 uv, float compare)
{
    float depth = shadowMap.Sample(samplerState, uv).r;
    return step(compare, depth);
}

float sampleShadowMap(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 uv, float compare)
{
    float2 texelSize = float2(1.0f, 1.0f) / size;
    float2 f = frac(uv * size + 0.5f);
    float2 centroidUV = floor(uv * size + 0.5f) / size;

    float lb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 0.0f), compare);
    float lt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 1.0f), compare);
    float rb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 0.0f), compare);
    float rt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 1.0f), compare);
    float a = lerp(lb, lt, f.y);
    float b = lerp(rb, rt, f.y);
    float c = lerp(a, b, f.x);
	
    return c;
}

float random(float2 seed2) 
{
	float4 seed4 = float4(seed2.x, seed2.y, seed2.y, 1.0f);
	float dot_product = dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float2 get_shadow_offsets(float3 N, float3 L) 
{
    float cos_alpha = saturate(dot(N, L));
    float offset_scale_N = sqrt(1 - cos_alpha*cos_alpha); // sin(acos(L·N))
    float offset_scale_L = offset_scale_N / cos_alpha;    // tan(acos(L·N))
    return float2(offset_scale_N, min(2, offset_scale_L));
}

float DoShadowMapping(Texture2D shadowMap, SamplerState samplerState, float shadowMapResolution, float shadowMappingQuality, float4 pos, float bias, float3 normal, float3 lightDir)
{
	// Re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	// If position is not visible to the light, dont illuminate it
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return 1.0f;

	// Transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	// Apply shadow map bias
    pos.z -= bias;

	//float2 normalOffset = get_shadow_offsets(normal, -lightDir);
	
	float percentLit = 0.0f;
	
	// Hard shadows (0.5f) --> PCF + Interpolation
	if (shadowMappingQuality == 0.5f)
	{
		// Perform PCF filtering on a 4 x 4 texel neighborhood
		[unroll(4)]
		for (float y = -1.5f; y <= 1.5f; ++y)
		{
			[unroll(4)]
			for (float x = -1.5f; x <= 1.5f; ++x)
			{
				percentLit += sampleShadowMap(shadowMap, samplerState, shadowMapResolution, pos.xy + texOffset(shadowMapResolution, x,y), pos.z);	
			}
		}
		percentLit /= 16.0f;
	}
	// Soft shadows (1.0f) --> Interpolation + Stratified Poisson Sampling
	else
	{
		// Poisson sampling for shadow map
		float packing = 4000.0f; // how close together are the samples
		float2 poissonDisk[4] = 
		{
		  float2( -0.94201624f, -0.39906216f ),
		  float2( 0.94558609f, -0.76890725f ),
		  float2( -0.094184101f, -0.92938870f ),
		  float2( 0.34495938f, 0.29387760f )
		};

		uint samples = 4;
		[unroll(samples)]
		for (uint i = 0; i < samples; i++)
		{
			uint index = uint(samples * random(pos.xy * i)) % samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
			percentLit += sampleShadowMap(shadowMap, samplerState, shadowMapResolution, pos.xy + (poissonDisk[index] / packing), pos.z);
		}	
		percentLit /= (float)samples;
	}
		
	return percentLit;
}

float ShadowMapping(Texture2D shadowMap, SamplerState samplerState, float shadowMapResolution, float shadowMappingQuality, float4 pos, float bias, float3 normal, float3 lightDir)
{
	float cosTheta = saturate(dot(normal, lightDir));
	float slopeScaledBias = tan(acos(cosTheta));
	slopeScaledBias = clamp(slopeScaledBias, 0.0f, 0.003f);
	
	return DoShadowMapping(shadowMap, samplerState, shadowMapResolution, shadowMappingQuality, pos, slopeScaledBias, normal, lightDir);
}