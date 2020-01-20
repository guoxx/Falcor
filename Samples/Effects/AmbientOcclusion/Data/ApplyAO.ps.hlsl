/***************************************************************************
# Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
__import Helpers;

cbuffer PerFrameCB
{
    float4x4 gProjMatrix;
};

SamplerState gLinearSampler;
SamplerState gPointSampler;

Texture2D gColor;
Texture2D gAOMap;
Texture2D gAOHistory;
Texture2D gLowResDepth;
Texture2D gFullResDepth;

float4 runFilter(float2 texC)
{
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    float2 fullRes;
    gFullResDepth.GetDimensions(fullRes.x, fullRes.y);
    float2 lowRes;
    gLowResDepth.GetDimensions(lowRes.x, lowRes.y);

    //float depthInLowRes = gLowResDepth.SampleLevel(gPointSampler, texC, 0).r;
    //float linearZInLowRes = depthToLinearZ(depthInLowRes, gProjMatrix);
    //float4 depthInHighRes = gFullResDepth.GatherRed(gPointSampler, texC);
    //float4 linearZInHighRes;
    //linearZInHighRes.x = depthToLinearZ(depthInHighRes.x, gProjMatrix);
    //linearZInHighRes.y = depthToLinearZ(depthInHighRes.y, gProjMatrix);
    //linearZInHighRes.z = depthToLinearZ(depthInHighRes.z, gProjMatrix);
    //linearZInHighRes.w = depthToLinearZ(depthInHighRes.w, gProjMatrix);

    //float4 aoFactors = gAOMap.GatherRed(gPointSampler, texC);

    //float2 lowResST = texC * lowRes;
    //float2 weightST = lowResST - floor(lowResST) - 0.5;
    //weightST += step(weightST, 0);

    //float4 weights;
    //weights.x = (1-weightST.x) * weightST.y;
    //weights.y = weightST.x * weightST.y;
    //weights.z = weightST.x * (1-weightST.y);
    //weights.w = (1-weightST.x) * (1-weightST.y);
    //weights = 0.25;

    float accumVal = 0;
    float accumWeight = 0;

    float2 stInLowRes = floor(texC*lowRes);

    float depthInHighRes = gFullResDepth[int2(texC * fullRes)].r;
    float linearZInHighRes = depthToLinearZ(depthInHighRes, gProjMatrix);

    // Brute force filtering
    for (int row = -1; row <= 2; ++row)
    {
        for (int col = -1; col <= 2; ++col)
        {
            float depthInLowRes = gLowResDepth[int2(stInLowRes + int2(row, col))].r;
            float linearZInLowRes = depthToLinearZ(depthInLowRes, gProjMatrix);

            float aoFactor = gAOMap[int2(stInLowRes + int2(row, col))].r;
            float weight = GaussianCoefficient(2, row*2+col*2) * GaussianCoefficient(1, (linearZInHighRes - linearZInLowRes));
            accumVal += aoFactor * weight;
            accumWeight += weight;
        }
    }
    float currentAO = saturate(accumVal/accumWeight);
    float historyAO = saturate(gAOHistory.Sample(gPointSampler, texC).x);
    return lerp(historyAO, currentAO, 1.0/8);
}
