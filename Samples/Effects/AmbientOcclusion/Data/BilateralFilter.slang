/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
import Helpers;

texture2D gSrcTex;
texture2D gDepthTex;
SamplerState gPointSampler;

cbuffer PerFrameCB
{
    int gKernelWidth;
    float gSigmaS;
    float2 gTexSize;

    float4x4 gProjMatrix;
    float gSigmaDepth;
};

struct VsOut
{
    float2 texC : TEXCOORD;
    float4 pos : SV_POSITION;
};

VsOut VSMain(uint id: SV_VertexID)
{
    VsOut vOut; 
	vOut.texC = float2((id & 0x02) * 1.0, (id & 0x01) * 2.0);
	vOut.pos = float4(vOut.texC * float2(2, -2) + float2(-1, 1), 0, 1);
    return vOut;
}


float getLinearZ(float2 texC)
{
    float d = gDepthTex.SampleLevel(gPointSampler, texC, 0).r;
    return depthToLinearZ(d, gProjMatrix);
}

float4 blur(float2 texC)
{
    float4 accumValue = float4(0,0,0,0);
    float accumWeight = 0;

    float centerZ = getLinearZ(texC);

    int w = gKernelWidth - 1;
    for (int i = -w; i <= w; ++i)
    {
        for (int j = -w; j <= w; ++j)
        {
            float2 offset = int2(i, j) / gTexSize;

            float weight = GaussianCoefficient(gSigmaS, i) * GaussianCoefficient(gSigmaS, j);

            float sampleZ = getLinearZ(texC + offset);
            weight *= GaussianCoefficient(gSigmaDepth, abs(centerZ - sampleZ));

            accumValue += gSrcTex.SampleLevel(gPointSampler, texC + offset, 0) * weight;
            accumWeight += weight;
        }
    }
    return accumValue/accumWeight;
}

float4 PSMain(VsOut pIn) : SV_TARGET0
{
    float4 fragColor = float4(1.f, 1.f, 1.f, 1.f);
    fragColor = blur(pIn.texC);
    return fragColor;
}
