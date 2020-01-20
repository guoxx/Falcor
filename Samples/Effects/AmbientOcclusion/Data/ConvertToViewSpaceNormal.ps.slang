__import Helpers;

SamplerState gPointSampler;
Texture2D gNormalMap;

cbuffer CB
{
    // assume no stretch on each direction so that safe to use view matrix instead of inversed transpose of view matrix
    float4x4 viewMat;
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    float3 n = gNormalMap.SampleLevel(gPointSampler, texC, 0).xyz * 2.0 - 1.0;
    n = mul(float4(n, 0.0), viewMat).xyz;
    // put z in r component, so that we can use Gather4 in HDAO
    return float4(n * 0.5 + 0.5, 1.0);
}
