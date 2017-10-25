struct Transform {
    row_major float3x3 m;
};
cbuffer transform : register(b0)
{
    Transform transform;
}


void main(uint vI : SV_VERTEXID, out float4 pos : SV_POSITION, out float2 texcoord : TEX0)
{
	texcoord = float2(vI & 1,vI >> 1); //you can use these for texture coordinates later
    float2 pos0 = float2((texcoord.x - 0.5f) * 2, -(texcoord.y - 0.5f) * 2);
	pos0 = mul(float3(pos0, 1), transform.m).xy;
	pos = float4(pos0, 0, 1);
}