Texture2D texSpectrogram : register(t0);
SamplerState linearSampler : register(s0)
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};


struct ColorPalette {
	float3 min, mid, max;
};
cbuffer colorPalette : register(b0)
{
    ColorPalette colorPalette;
}



float4 main(float4 pos : SV_POSITION, float2 texcoord : TEX0) : SV_TARGET
{
	// sample spectrogram
    float2 sampleCoord = texcoord;
    float width, height;
    texSpectrogram.GetDimensions(width, height);
    sampleCoord.y = (floor(sampleCoord.y * height) + 0.5f) / height;
    float value = (float) texSpectrogram.Sample(linearSampler, sampleCoord);
    value = saturate(value);

	// calculate color according to palette
    float3 color = value < 0.5f ? lerp(colorPalette.min, colorPalette.mid, value * 2.0f) :
									lerp(colorPalette.mid, colorPalette.max, (value - 0.5f) * 2.0f);
    if (value == 1)
    {
        color = float3(0.8f, 0.1f, 0.1f);
    }

    return float4(color, 1.0f);
}