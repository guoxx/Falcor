__import ShaderCommon;

cbuffer SSAOCB : register(b0)
{
    float g_fNormalScale;               // Normal scale
    float g_fAcceptAngle;               // Accept angle
    float2 g_f2RTSize;                  // Used by HDAO shaders for scaling texture coords
    float g_fHDAORejectRadius;          // HDAO param
    float g_fHDAOIntensity;             // HDAO param
    float g_fHDAOAcceptRadius;          // HDAO param
    float2 g_f2KernelScale;
}

SamplerState gPointSampler;

Texture2D gDepthTex;
Texture2D gNormalTex;

float4 getPosition(float2 uv)
{
    float4 pos;
    pos.x = uv.x * 2.0f - 1.0f;
    pos.y = (1.0f - uv.y) * 2.0f - 1.0f;
#ifdef FALCOR_VK
    // NDC space is inverted
    pos.y = -pos.y;
#endif
    pos.z = gDepthTex.SampleLevel(gPointSampler, uv, 0).r;
    pos.w = 1.0f;

    float4 posW = mul(pos, gCamera.invViewProj);
    posW /= posW.w;

    return posW;
}

float3 getPositionInCameraSpace(float2 uv)
{
    // TODO: optimization
    float4 posW = getPosition(uv);
    return mul(posW, gCamera.viewMat).xyz;
}

//--------------------------------------------------------------------------------------
// Gather pattern
//--------------------------------------------------------------------------------------

// Gather defines
#define RING_1    (1)
#define RING_2    (2)
#define RING_3    (3)
#define RING_4    (4)
#define NUM_RING_1_GATHERS    (2)
#define NUM_RING_2_GATHERS    (6)
#define NUM_RING_3_GATHERS    (12)
#define NUM_RING_4_GATHERS    (20)

#ifndef RING_CNT
    #define RING_CNT RING_4
#endif
#if RING_CNT == RING_1
    #define NUM_RING_GATHERS NUM_RING_1_GATHERS
#elif RING_CNT == RING_2
    #define NUM_RING_GATHERS NUM_RING_2_GATHERS
#elif RING_CNT == RING_3
    #define NUM_RING_GATHERS NUM_RING_3_GATHERS
#elif RING_CNT == RING_4
    #define NUM_RING_GATHERS NUM_RING_4_GATHERS
#endif

// Ring sample pattern
static const float2 g_f2HDAORingPattern[NUM_RING_4_GATHERS] = 
{
    // Ring 1
    { 1, -1 },
    { 0, 1 },
    
    // Ring 2
    { 0, 3 },
    { 2, 1 },
    { 3, -1 },
    { 1, -3 },
        
    // Ring 3
    { 1, -5 },
    { 3, -3 },
    { 5, -1 },
    { 4, 1 },
    { 2, 3 },
    { 0, 5 },
    
    // Ring 4
    { 0, 7 },
    { 2, 5 },
    { 4, 3 },
    { 6, 1 },
    { 7, -1 },
    { 5, -3 },
    { 3, -5 },
    { 1, -7 },
};

// Ring weights
static const float4 g_f4HDAORingWeight[NUM_RING_4_GATHERS] = 
{
    // Ring 1 (Sum = 5.30864)
    { 1.00000, 0.50000, 0.44721, 0.70711 },
    { 0.50000, 0.44721, 0.70711, 1.00000 },
    
    // Ring 2 (Sum = 6.08746)
    { 0.30000, 0.29104, 0.37947, 0.40000 },
    { 0.42426, 0.33282, 0.37947, 0.53666 },
    { 0.40000, 0.30000, 0.29104, 0.37947 },
    { 0.53666, 0.42426, 0.33282, 0.37947 },
    
    // Ring 3 (Sum = 6.53067)
    { 0.31530, 0.29069, 0.24140, 0.25495 },
    { 0.36056, 0.29069, 0.26000, 0.30641 },
    { 0.26000, 0.21667, 0.21372, 0.25495 },
    { 0.29069, 0.24140, 0.25495, 0.31530 },
    { 0.29069, 0.26000, 0.30641, 0.36056 },
    { 0.21667, 0.21372, 0.25495, 0.26000 },
    
    // Ring 4 (Sum = 7.00962)
    { 0.17500, 0.17365, 0.19799, 0.20000 },
    { 0.22136, 0.20870, 0.24010, 0.25997 },
    { 0.24749, 0.21864, 0.24010, 0.28000 },
    { 0.22136, 0.19230, 0.19799, 0.23016 },
    { 0.20000, 0.17500, 0.17365, 0.19799 },
    { 0.25997, 0.22136, 0.20870, 0.24010 },
    { 0.28000, 0.24749, 0.21864, 0.24010 },
    { 0.23016, 0.22136, 0.19230, 0.19799 },
};

static const float g_fRingWeightsTotal[RING_4] =
{
    5.30864,
    11.39610,
    17.92677,
    24.93639,
};

#define NUM_NORMAL_LOADS (4)
static const float2 g_fNormalLoadPattern[NUM_NORMAL_LOADS] = 
{
    { 1, 8 },
    { 8, -1 },
    { 5, 4 },
    { 4, -4 },
};

float GetLinearZ(float depth, float4x4 mProj)
{
    /*
        mProj = [[m11,   0,   0,   0],
                 [  0, m22,   0,   0],
                 [  0,   0, m22, m23],
                 [  0,   0, m32,   0]]
        m23 is -1 for right hand coordinate, 1 for left hand

        depth = (m22 * z + m32) / (m23 * z)
        depth * m23 = m22 + m32 / z
        z = m32 / (depth * m23 - m22)
    */
    float m22 = mProj[2][2];
    float m23 = mProj[2][3];
    float m32 = mProj[3][2];
    float z = m32 / (depth * m23 - m22);
    return z;
}

float GetProjectionDepth(float z, float4x4 mProj)
{
    // we are using right hand coordinate, but assume z is positive here

    // behind the camera, just return 0
    if (z <= 0)
        return 0;

    float m22 = mProj[2][2];
    float m23 = mProj[2][3];
    float m32 = mProj[3][2];
    z = -z;
    float depth = (m22 * z + m32) / (m23 * z);
    return depth;
}

//--------------------------------------------------------------------------------------
// Helper function to gather Z values in 10.1 and 10.0 modes
//--------------------------------------------------------------------------------------
float RetrieveZSample(Texture2D Tex, float2 f2TexCoord)
{
    float depthSample = Tex.SampleLevel(gPointSampler, f2TexCoord, 0).x;
    float linearZ = GetLinearZ(depthSample, gCamera.projMat);
    // we are using right hand coordinate, but HDAO calculation code assume z to be positive
    return -linearZ;
}

//--------------------------------------------------------------------------------------
// Used as an early rejection test - based on normals
//--------------------------------------------------------------------------------------
float NormalRejectionTest(float2 texC)
{
    float fSummedDot = 0.0f;
    for (int i=0; i<NUM_NORMAL_LOADS; i++)
    {
        float2 offsetScreenCoord = texC + g_fNormalLoadPattern[i] / g_f2RTSize;
        float2 mirrorOffsetScreenCoord = texC - g_fNormalLoadPattern[i] / g_f2RTSize;

        float3 f3N1 = gNormalTex.SampleLevel(gPointSampler, offsetScreenCoord, 0).xyz * 2.0 - 1.0;
        float3 f3N2 = gNormalTex.SampleLevel(gPointSampler, mirrorOffsetScreenCoord, 0).xyz * 2.0 - 1.0;

        float fDot = dot(f3N1, f3N2);

        fSummedDot += ( fDot > g_fAcceptAngle ) ? ( 0.0f ) : ( 1.0f - ( abs( fDot ) * 0.25f ) );
    }
    return (0.5f + fSummedDot * 0.25f);
}

float RenderHDAO(float2 texC)
{
    float4 f4Occlusion = 0.0;

    const float2 f2InvRTSize = 1.0 / g_f2RTSize;

    // Test the normals to see if we should apply occlusion
    const float fDot = NormalRejectionTest(texC);
    if (fDot > 0.5)
    {
        // Sample the center pixel for camera Z
        const float fCenterZ = RetrieveZSample(gDepthTex, texC);
        const float rejectThreshold = GetProjectionDepth(fCenterZ - g_fHDAORejectRadius, gCamera.projMat);
        const float acceptThreshold = GetProjectionDepth(fCenterZ - g_fHDAOAcceptRadius, gCamera.projMat);

        // Loop through each gather location, and compare with its mirrored location
        for (int iGather=0; iGather<NUM_RING_GATHERS; iGather++)
        {
            float2 f2TexCoord = texC + g_f2HDAORingPattern[iGather] * f2InvRTSize * g_f2KernelScale;
            float2 f2MirrorTexCoord = texC - g_f2HDAORingPattern[iGather] * f2InvRTSize * g_f2KernelScale;

            // offset for gather
            f2TexCoord += 0.5*f2InvRTSize;
            f2MirrorTexCoord -= 0.5*f2InvRTSize;

            // Sample
            float4 f4SampledZ[2];
            f4SampledZ[0] = gDepthTex.Gather(gPointSampler, f2TexCoord);
            f4SampledZ[1] = gDepthTex.Gather(gPointSampler, f2MirrorTexCoord);

            // Detect valleys
            float4 f4Compare[2];
            f4Compare[0] = step(rejectThreshold.xxxx, f4SampledZ[0]);
            f4Compare[0] *= step(f4SampledZ[0], acceptThreshold);
            f4Compare[1] = step(rejectThreshold.xxxx, f4SampledZ[1]);
            f4Compare[1] *= step(f4SampledZ[1], acceptThreshold);

            f4Occlusion.xyzw += ( g_f4HDAORingWeight[iGather].xyzw * ( f4Compare[0].xyzw * f4Compare[1].zwxy ) * fDot );    
        }
    }

    // Finally calculate the HDAO occlusion value
    float fOcclusion = ( ( f4Occlusion.x + f4Occlusion.y + f4Occlusion.z + f4Occlusion.w ) / ( 2.0f * g_fRingWeightsTotal[RING_CNT - 1] ) );
    fOcclusion *= ( g_fHDAOIntensity );
    fOcclusion = 1.0f - saturate( fOcclusion );
    return fOcclusion;
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    if (gDepthTex.SampleLevel(gPointSampler, texC, 0).r >= 1)
    {
        return 1.0f;
    }

    float ao = RenderHDAO(texC);
    return float4(ao.xxx, 1.0);
}
