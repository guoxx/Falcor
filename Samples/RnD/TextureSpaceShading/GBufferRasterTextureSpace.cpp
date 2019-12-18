/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
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
#include "Framework.h"
#include "GBufferRasterTextureSpace.h"

namespace
{
    const char kFileRasterPrimary[] = "GBufferRasterTextureSpace.slang";
}

GBufferRasterTextureSpace::SharedPtr GBufferRasterTextureSpace::create(RasterizerState::CullMode cullMode)
{
    SharedPtr pPass = SharedPtr(new GBufferRasterTextureSpace);
    pPass->setCullMode(cullMode);
    return pPass;
}

Fbo::SharedPtr GBufferRasterTextureSpace::createGBufferFbo(int32_t w, int32_t h, bool hasDepthStencil)
{
    Fbo::Desc desc;
    RenderPassReflection r;
    for (int i = 0; i < 8; ++i)
    {
        desc.setColorTarget(i, ResourceFormat::RGBA32Float);
    }
    if (hasDepthStencil)
    {
        desc.setDepthStencilTarget(ResourceFormat::D32Float);
    }
    return FboHelper::create2D(w, h, desc);
}

GBufferRasterTextureSpace::GBufferRasterTextureSpace() : RenderPass("GBufferRasterTextureSpace")
{
    mRaster.pProgram = GraphicsProgram::createFromFile(kFileRasterPrimary, "vs", "ps");

    // Initialize graphics state
    mRaster.pState = GraphicsState::create();

    // Set default culling mode
    setCullMode(mCullMode); 

    mRaster.pVars = GraphicsVars::create(mRaster.pProgram->getReflector());
    mRaster.pState->setProgram(mRaster.pProgram);
}

void GBufferRasterTextureSpace::onResize(uint32_t width, uint32_t height)
{
}

void GBufferRasterTextureSpace::setScene(const Scene::SharedPtr& pScene)
{
    mpSceneRenderer = (pScene == nullptr) ? nullptr : SceneRenderer::create(pScene);
}

void GBufferRasterTextureSpace::setCullMode(RasterizerState::CullMode mode)
{
    mCullMode = mode;

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(mCullMode);
    // Enable conservative rasterization
    rsDesc.setConservativeRasterization(true);
    mRaster.pState->setRasterizerState(RasterizerState::create(rsDesc));

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthFunc(DepthStencilState::Func::LessEqual);
    mRaster.pState->setDepthStencilState(DepthStencilState::create(dsDesc));
}

void GBufferRasterTextureSpace::execute(RenderContext* pContext, Fbo::SharedPtr pGBufferFbo, Camera::SharedConstPtr pCamera)
{
    if (mpSceneRenderer == nullptr)
    {
        logWarning("Invalid SceneRenderer in GBufferRasterTextureSpace::execute()");
        return;
    }

    mRaster.pVars["PerFrameCB"]["gRenderTargetDim"] = vec2(pGBufferFbo->getWidth(), pGBufferFbo->getHeight());

    mRaster.pState->pushFbo(pGBufferFbo);

    pContext->pushGraphicsState(mRaster.pState);
    pContext->pushGraphicsVars(mRaster.pVars);
    const Camera* pValidCamera = pCamera ? pCamera.get() : mpSceneRenderer->getScene()->getActiveCamera().get();
    mpSceneRenderer->renderScene(pContext, pValidCamera);
    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    mRaster.pState->popFbo();    
}

RenderPassReflection GBufferRasterTextureSpace::reflect() const
{
    should_not_get_here();
    RenderPassReflection r;
    return r;
}

void GBufferRasterTextureSpace::execute(RenderContext* pContext, const RenderData* pRenderData)
{
    should_not_get_here();
}