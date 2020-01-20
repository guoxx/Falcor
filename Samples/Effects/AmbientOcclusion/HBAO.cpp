#include "Framework.h"
#include "HBAO.h"
#include "Graphics/FboHelper.h"
#include "API/RenderContext.h"
#include "Graphics/Camera/Camera.h"
#include "glm/gtc/random.hpp"
#include "glm/gtc/packing.hpp"
#include "Utils/Math/FalcorMath.h"
#include "Graphics/Scene/Scene.h"

using namespace Falcor;

Gui::DropdownList kBlurModeList =
{
    { 0, "None" },
    { 1, "Gaussian" },
    { 2, "Bilateral" },
};

HBAO::SharedPtr HBAO::create(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma)
{
    return SharedPtr(new HBAO(aoMapSize, kernelSize, blurSize, blurSigma));
}

HBAO::HBAO(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma)
{
    resizeAOMap(aoMapSize);

    createAOPass();

    mpSSAOState = GraphicsState::create();

    mpBlur = GaussianBlur::create(5, 2.0f);
    mpBilateralFilter = BilateralFilter::create(5, 2.0f);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point).setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    mpPointSampler = Sampler::create(samplerDesc);
}

void HBAO::renderUI(Gui* pGui, const char* uiGroup)
{
    if (!uiGroup || pGui->beginGroup(uiGroup))
    {
        pGui->addFloatVar("Occlusion Ray Length", mHBAOData.mOcclusionRayLength, 0.0001f);

        if (pGui->addCheckBox("Physically Correct", mPhysicallyCorrect))
        {
            createAOPass();
        }
        if (pGui->addCheckBox("Cosine Weighted AO", mCosWeightedAO))
        {
            createAOPass();
        }

        pGui->addCheckBox("Debug View", mDebugView);
        pGui->addInt2Var("Debug Pixel", mDebugPixel);
        pGui->addFloatSlider("Step Size Linear Blend", mStepSizeLinearBlend, 0, 1);

        pGui->addDropdown("Blur Mode", kBlurModeList, (uint32_t&)mBlurMode);

        if (mBlurMode == BlurMode::Gaussian)
        {
            mpBlur->renderUI(pGui, "Gaussian Filter");
        }
        else if (mBlurMode == BlurMode::Bilateral)
        {
            mpBilateralFilter->renderUI(pGui, "Bilateral Filter");
        }

        if (uiGroup) pGui->endGroup();
    }
}

void HBAO::generateAOMapInternal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture)
{
    // update constants buffer
    ConstantBuffer* pSSAOCB = mpSSAOVars->getDefaultBlock()->getConstantBuffer(mBindLocations.ssaoCB, 0).get();
    pSSAOCB->setVariable("gRTSize", glm::vec4(mpAOFbo->getWidth(), mpAOFbo->getHeight(), 1.0f/mpAOFbo->getWidth(), 1.0f/mpAOFbo->getHeight()));
    const float fovY = Falcor::focalLengthToFovY(pCamera->getFocalLength(), pCamera->getFrameHeight());
    pSSAOCB->setVariable("gTanHalfFovY", std::tan(fovY/2.0f));
    pSSAOCB->setVariable("gAspectRatio", pCamera->getAspectRatio());
    pSSAOCB->setVariable("gRayLength", mHBAOData.mOcclusionRayLength);
    pSSAOCB->setVariable("gSquaredRayLength", mHBAOData.mOcclusionRayLength*mHBAOData.mOcclusionRayLength);
    pSSAOCB->setVariable("gFrameCount", mFrameCount);
    pSSAOCB->setVariable("gDebugPixel", mDebugPixel);
    pSSAOCB->setVariable("gStepSizeLinearBlend", mStepSizeLinearBlend);
    mFrameCount += 1;

    // Update state/vars
    mpSSAOState->setFbo(mpAOFbo);
    ParameterBlock* pDefaultBlock = mpSSAOVars->getDefaultBlock().get();
    pDefaultBlock->setSampler(mBindLocations.pointSampler, 0, mpPointSampler);
    pDefaultBlock->setSrv(mBindLocations.depthTex, 0, pDepthTexture->getSRV());
    pDefaultBlock->setSrv(mBindLocations.normalTex, 0, pNormalTexture->getSRV());
    pDefaultBlock->setSrv(mBindLocations.historyTex, 0, mpAOHistoryFbo->getColorTexture(0)->getSRV());

    pDefaultBlock->setUav(mBindLocations.debugTex, 0, mpDebugFbo->getColorTexture(0)->getUAV());
    pDefaultBlock->setUav(mBindLocations.debugData, 0, mpDebugData->getUAV());

    ConstantBuffer* pCB = pDefaultBlock->getConstantBuffer(mBindLocations.internalPerFrameCB, 0).get();
    if (pCB != nullptr)
    {
        pCamera->setIntoConstantBuffer(pCB, 0);
    }

    // Generate AO
    pContext->pushGraphicsVars(mpSSAOVars);
    mpSSAOPass->execute(pContext);
    pContext->popGraphicsVars();
}

Texture::SharedPtr HBAO::generateAOMap(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture)
{
    //assert(mpAOFbo->getWidth() == pDepthTexture->getWidth() && mpAOFbo->getHeight() == pDepthTexture->getHeight());
    //assert(mpAOFbo->getWidth() == pNormalTexture->getWidth() && mpAOFbo->getHeight() == pNormalTexture->getHeight());

    pContext->clearUAV(mpDebugFbo->getColorTexture(0)->getUAV().get(), glm::vec4(0.0f));
    pContext->clearUAV(mpDebugData->getUAV().get(), glm::vec4(0.0f));

    pContext->pushGraphicsState(mpSSAOState);
    generateAOMapInternal(pContext, pCamera, pDepthTexture, pNormalTexture);
    pContext->popGraphicsState();

    // Blur
    if (mBlurMode == BlurMode::Gaussian)
    {
        mpBlur->execute(pContext, mpAOFbo->getColorTexture(0), mpAOFbo); 
    }
    else if (mBlurMode == BlurMode::Bilateral)
    {
        mpBilateralFilter->execute(pContext, pCamera, mpAOFbo->getColorTexture(0), pDepthTexture, mpAOTmpFbo);
        pContext->blit(mpAOTmpFbo->getColorTexture(0)->getSRV(), mpAOFbo->getColorTexture(0)->getRTV());
    }

    pContext->blit(mpAOFbo->getColorTexture(0)->getSRV(), mpAOHistoryFbo->getColorTexture(0)->getRTV());

    if (mDebugView)
    {
        pContext->blit(mpDebugFbo->getColorTexture(0)->getSRV(), mpAOFbo->getColorTexture(0)->getRTV());
    }
    return mpAOFbo->getColorTexture(0);
}

void HBAO::resizeAOMap(const uvec2& aoMapSize)
{
    if (mpAOFbo == nullptr ||
        mpAOFbo->getWidth() != aoMapSize.x ||
        mpAOFbo->getHeight() != aoMapSize.y)
    {
        Fbo::Desc aoFboDesc;
        aoFboDesc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float);
        mpAOFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, aoFboDesc);
        mpAOTmpFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, aoFboDesc);
        mpAOHistoryFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, aoFboDesc);

        Fbo::Desc desc;
        desc.setColorTarget(0, ResourceFormat::RGBA32Float, true);
        mpDebugFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, desc);
        mpDebugData = TypedBuffer<glm::vec4>::create(4096, ResourceBindFlags::UnorderedAccess);
    }
}

void HBAO::createAOPass()
{
    mpSSAOPass = FullScreenPass::create("HBAO.ps.hlsl", Program::DefineList(), true, true, 0, false, Shader::CompilerFlags::EmitDebugInfo);
    mpSSAOPass->getProgram()->addDefine("PHYSICALLY_CORRECT", mPhysicallyCorrect ? std::to_string(1) : std::to_string(0));
    mpSSAOPass->getProgram()->addDefine("COSINE_WEIGHTED_AO", mCosWeightedAO ? std::to_string(1) : std::to_string(0));
    mpSSAOVars = GraphicsVars::create(mpSSAOPass->getProgram()->getReflector());

    const ParameterBlockReflection* pReflector = mpSSAOPass->getProgram()->getReflector()->getDefaultParameterBlock().get();
    mBindLocations.internalPerFrameCB = pReflector->getResourceBinding("InternalPerFrameCB");
    mBindLocations.ssaoCB = pReflector->getResourceBinding("SSAOCB");
    mBindLocations.pointSampler = pReflector->getResourceBinding("gPointSampler");
    mBindLocations.depthTex = pReflector->getResourceBinding("gDepthTex");
    mBindLocations.normalTex = pReflector->getResourceBinding("gNormalTex");
    mBindLocations.historyTex = pReflector->getResourceBinding("gHistoryTex");
    mBindLocations.debugTex = pReflector->getResourceBinding("gDebugTex");
    mBindLocations.debugData = pReflector->getResourceBinding("gDebugData");
}
