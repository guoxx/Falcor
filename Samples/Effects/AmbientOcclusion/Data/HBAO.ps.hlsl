#include "HostDeviceSharedMacros.h"
__import ShaderCommon;
__import Helpers;

#ifndef COSINE_WEIGHTED_AO
#define COSINE_WEIGHTED_AO 1
#endif

#define DISTANCE_FALLOFF 1

static const int NUM_DIRECTION_SAMPLE = 3;
static const float DIRECTION_SAMPLE_ANGEL_STEP = M_PI/NUM_DIRECTION_SAMPLE;
static const int NUM_STEPS = 6;

cbuffer SSAOCB : register(b0)
{
    float4 gRTSize;
    float gTanHalfFovY;
    float gAspectRatio;
    float gRayLength;
    float gSquaredRayLength;
    float gFrameCount;
    float gStepSizeLinearBlend;
    float gThickness;
    int2 gDebugPixel;
}

SamplerState gPointSampler;

Texture2D gDepthTex;
Texture2D gNormalTex;
Texture2D gHistoryTex;

RWTexture2D gDebugTex;
struct DEBUG_DATA {
float azimuth;
float2 uvStep;
float2 uvH1;
float3 posH1;
float2 uvH2;
float3 posH2;
float h1;
float h2;
float n;
float3 frameT;
float3 frameB;
float3 frameN;
float3 normal;
float3 pos;
};
RWStructuredBuffer<DEBUG_DATA> gDebugData;

float3 getNormal(float2 uv)
{
    float3 N = gNormalTex.SampleLevel(gPointSampler, uv, 0).xyz * 2.0 - 1.0;
    return mul(float4(normalize(N), 0), gCamera.viewMat).xyz;
}

float getLinearZ(float2 uv)
{
    float depth = gDepthTex.SampleLevel(gPointSampler, uv, 0).r;
    return depthToLinearZ(depth, gCamera.projMat);
}

// note: unnormalized
float3 getCameraRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2 - 1, -(uv.y * 2 - 1));
    return float3(ndc.x * gTanHalfFovY * gAspectRatio, ndc.y * gTanHalfFovY, -1);
}

float3 getPosition(float2 uv)
{
    //return reconstructPositionFromDepth(gDepthTex, gPointSampler, uv, gCamera.invViewProj).xyz;

    float3 ray = getCameraRay(uv);
    return ray / ray.z * getLinearZ(uv);
}

float IntegrateArc(float h1, float h2, float n)
{
#if COSINE_WEIGHTED_AO
	float cosN = cos(n);
	float sinN = sin(n);
	return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN - cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
#else
    return (2.0 - cos(h1) - cos(h2)) / 2.0;
#endif
}

float4 snapToTexelCenter(float4 uv)
{
    //return uv;
    float4 st = floor(uv * gRTSize.xyxy);
    return (st + 0.5) / gRTSize.xyxy;
}

#if DISTANCE_FALLOFF
float2 distanceFalloff(float2 x)
{
    return saturate(x*x);
}
#endif

float4 HBAOKernel(float2 texC)
{
    bool debugPixel = false;
    int2 iTexcoord = int2(texC * gRTSize.xy);
    if (all(iTexcoord == gDebugPixel))
    {
        debugPixel = true;
    }

    const float3 P = getPosition(texC).xyz;
    const float3 N = getNormal(texC);

    if (debugPixel)
    {
        gDebugTex[iTexcoord] = float4(P, 1);
    }

#if 1
    //float4x4 invViewMat = transpose(gCamera.viewMat);
    //const float3 localFrameT = normalize(mul(float3(1, 0, 0), (float3x3)invViewMat));
    //const float3 localFrameB = normalize(mul(float3(0, 1, 0), (float3x3)invViewMat));
    //const float3 localFrameN = normalize(mul(float3(0, 0, 1), (float3x3)invViewMat));
    //const float3 localFrameT = float3(1, 0, 0);
    //const float3 localFrameB = float3(0, 1, 0);
    //const float3 localFrameN = float3(0, 0, 1);
    const float3 localFrameN = normalize(-P);
    //const float3 localFrameT = getPerpendicularStark(localFrameN);
    //const float3 localFrameB = cross(localFrameN, localFrameT);
#else
    const float3 localFrameN = N;
    const float3 localFrameT = getPerpendicularStark(localFrameN);
    const float3 localFrameB = cross(localFrameN, localFrameT);
#endif

    // Initialize our random number generator
    uint randSeed = rand_init(texC.x * gRTSize.x * gFrameCount, texC.y * gRTSize.y * gFrameCount);
    //uint randSeed = rand_init(texC.x * gRTSize.x, texC.y * gRTSize.y);

    float occlusion = 0.0;
    for (int i = 0; i < NUM_DIRECTION_SAMPLE; ++i)
    {
        //const float azimuth = (asint(rand_next(randSeed)) % 16)* M_PI / 16;
        const float azimuth = DIRECTION_SAMPLE_ANGEL_STEP * (i + rand_next(randSeed));
        //const float azimuth = (int(texC.x * gRTSize.x + gFrameCount * 0) % 8) * (M_PI / 8);
        float2 sampleDir = float2(cos(azimuth), sin(azimuth));
        float2 uvDir = float2(sampleDir.x, -sampleDir.y);

//#if 1
//        float3 P_prime = P + localFrameT * sampleDir.x + localFrameB * sampleDir.y;
//        float4 projP_prime = mul(float4(P_prime, 1), gCamera.projMat);
//        projP_prime /= projP_prime.w;
//        float2 uv = NdcToUv(projP_prime.xy);
//        uvDir = uv - texC;
//        uvDir *= gRTSize.xy;
//        uvDir /= length(uvDir);
//#endif

        float maxSearchRadius = gRayLength / abs(P.z) / gTanHalfFovY * gRTSize.y;
        // from ndc to uv
        maxSearchRadius /= 2;
        maxSearchRadius = min(maxSearchRadius, 8*NUM_STEPS);

        float jacobian = 1;
#if 0
        // TODO: Uniform sampling in texel space and apply Jacobian factor
        // however it doesn't provide convincing result, need further investigation
        float2 uvStep = float2(sampleDir.x, -sampleDir.y);

        uvStep *= gRayLength / (gTanHalfFovY * abs(P.z));
        // stretch x coordinate in order to match local frame to viewport transformation
        //uvStep.x /= gAspectRatio;

        uvStep = min(uvStep, 128/gRTSize.xy);

        uvStep /= NUM_STEPS;

        if (all(abs(uvStep) < 1/gRTSize.xy))
        {
            uvStep /= max(abs(uvStep.x), abs(uvStep.y));
            uvStep /= gRTSize.xy;
        }

        float ratio = gAspectRatio;
        float norm = 3.83135442 + (-1.63271422 * ratio) + (1.15080682 * ratio * ratio) + (-0.20854034 * ratio * ratio * ratio);
        if (abs(sampleDir.y) > abs(sampleDir.x))
        {
            float invTanTheta = sampleDir.x/sampleDir.y;
            jacobian = sqrt((invTanTheta*invTanTheta+ratio*ratio) / (1 + invTanTheta*invTanTheta*ratio*ratio));
        }
        else
        {
            float tanTheta = sampleDir.y/sampleDir.x;
            jacobian = sqrt((1+tanTheta*tanTheta*ratio*ratio) / (tanTheta*tanTheta + ratio*ratio));
        }
        jacobian /= norm;
        jacobian *= M_PI;
#endif

        //float3 auxiliaryVec = getCameraRay(texC + uvDir);
        //float3 tangentVec = normalize(auxiliaryVec - dot(auxiliaryVec, localFrameN) * localFrameN);
        //float3 tangentVec = localFrameT * sampleDir.x + localFrameB * sampleDir.y;
        float3 bitangentVec = cross(localFrameN, float3(sampleDir, 0));
        float3 tangentVec = cross(bitangentVec, localFrameN);
        float3 projectedNormal = N - dot(N, bitangentVec) * bitangentVec;
        float projectedNormalLength = sqrt(dot(projectedNormal, projectedNormal));
        projectedNormal /= projectedNormalLength;

        float n = acos(clamp(dot(projectedNormal, localFrameN), -1, 1));
        n *= -sign(dot(projectedNormal, tangentVec));

        //float2 h1h2 = float2(M_PI, M_PI);
        float2 h1h2 = float2(-1, -1);
        for (int j = 1; j <= NUM_STEPS; ++j)
        {
            // TODO: reconsider about this part
            float stepScale = float(j) / float(NUM_STEPS);
            float powSplit = stepScale * stepScale * maxSearchRadius;
            float uniSplit = stepScale * maxSearchRadius;
            float linearBlend = gStepSizeLinearBlend;
            float searchRadiusStep = linearBlend * powSplit + (1 - linearBlend) * uniSplit;
            searchRadiusStep *= (0.75 + rand_next(randSeed) * 0.25);
            searchRadiusStep = max(searchRadiusStep, j);

            const float2 jitter = 0;//-rand_next(randSeed) * 0.5;
            float2 uvOffset = searchRadiusStep * uvDir / gRTSize.xy;
            float4 uv = snapToTexelCenter(texC.xyxy + float4(uvOffset, -uvOffset));

            float3 ds = getPosition(uv.xy).xyz - P;
            float3 dt = getPosition(uv.zw).xyz - P;
            float2 dsdt = float2(dot(ds, ds), dot(dt, dt));
            float2 dsdtInvLength = rsqrt(dsdt + float2(0.0001));
            float2 dsdtCos = float2(dot(ds, localFrameN), dot(dt, localFrameN))*dsdtInvLength;

            //float2 tmp = acos(dsdtCos);
            //tmp = lerp(tmp, h1h2, pow(saturate(dsdt / gSquaredRayLength), 4));
            //h1h2 = min(h1h2, tmp);

            float2 falloff = distanceFalloff(dsdt / gSquaredRayLength);
            float2 weight = step(dsdtCos.xy, h1h2.xy);
            h1h2.xy = lerp(dsdtCos, h1h2, falloff)*(1-weight) + lerp(dsdtCos, h1h2, gThickness)*weight;

            if (debugPixel)
            {
                float grayscale = float(i + 1)/NUM_DIRECTION_SAMPLE;
                gDebugTex[uv.xy * gRTSize.xy] = float4(0, grayscale, 0, 0);
                gDebugTex[uv.zw * gRTSize.xy] = float4(0, 0, grayscale, 0);
            }
            //h1h2.xy = max(dsdtCos, h1h2);
        }

        //float h1 = -h1h2.x;
        //float h2 = h1h2.y;
        float h1 = -acos(h1h2.x);
        float h2 = acos(h1h2.y);

        h1 = n + max(h1 - n, -M_PI*0.5);
        h2 = n + min(h2 - n, M_PI*0.5);

#if COSINE_WEIGHTED_AO
        occlusion += IntegrateArc(h1, h2, n) * projectedNormalLength * jacobian;
#else
        occlusion += IntegrateArc(h1, h2, n) * jacobian;
#endif
    }

    occlusion /= NUM_DIRECTION_SAMPLE;
    return occlusion;
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    if (gDepthTex.SampleLevel(gPointSampler, texC, 0).r >= 1)
    {
        return 1.0f;
    }

    float4 current = HBAOKernel(texC);
    return current;
    float4 history = gHistoryTex.SampleLevel(gPointSampler, texC, 0);
    return lerp(history, current, 1/8);
}
