Texture2D texSpectrogram : register(t0);
SamplerState linearSampler : register(s0)
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct Params
{
    row_major float3x3 transform;
    uint pad;
    uint verticesPerLine;
    uint lineIdx;
};
cbuffer params : register(b0)
{
    Params params;
}


float4 main(uint vI : SV_VERTEXID) : SV_POSITION
{
    float width, height;
	texSpectrogram.GetDimensions(width, height);

    float2 sampleCoord = float2(float(vI) / float(params.verticesPerLine - 1), params.lineIdx / height);
    sampleCoord.y = (floor(sampleCoord.y * height) + 0.5f) / height;
    float value = (float) texSpectrogram.SampleLevel(linearSampler, sampleCoord, 0);

    float2 pos0 = float2(sampleCoord.x * 2 - 1, 2*value / height);
    pos0 = mul(float3(pos0, 1), params.transform).xy;

    return float4(pos0, 0, 1);
}