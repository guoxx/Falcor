#include "Framework.h"
#include "HDAO.h"
#include "Graphics/FboHelper.h"
#include "API/RenderContext.h"
#include "Graphics/Camera/Camera.h"
#include "glm/gtc/random.hpp"
#include "glm/gtc/packing.hpp"
#include "Utils/Math/FalcorMath.h"
#include "Graphics/Scene/Scene.h"

using namespace Falcor;

HDAO::SharedPtr HDAO::create(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma)
{
    return SharedPtr(new HDAO(aoMapSize, kernelSize, blurSize, blurSigma));
}

HDAO::HDAO(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma)
{
    resizeAOMap(aoMapSize);

    createAOPass();
    createNormalConvPass();

    mpSSAOState = GraphicsState::create();

    mpBlur = GaussianBlur::create(5, 2.0f);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point).setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    mpPointSampler = Sampler::create(samplerDesc);
}

void HDAO::renderUI(Gui* pGui, const char* uiGroup)
{
    if (!uiGroup || pGui->beginGroup(uiGroup))
    {
        pGui->addFloatVar("Reject Radius", mHDAOData.HDAORejectRadius, 0, 4.0f);
        pGui->addFloatVar("Accept Radius", mHDAOData.HDAOAcceptRadius, 0, 1.0f);
        pGui->addFloatVar("Intensity", mHDAOData.HDAOIntensity, 0, 4.0f);
        pGui->addFloatVar("Normal Scale", mHDAOData.NormalScale, 0, 2.0f);
        pGui->addFloatVar("Accept Angle", mHDAOData.AcceptAngle, 0, 1.0f);
        pGui->addFloatVar("Kernel Scale", mHDAOData.HDAOKernelScale, 0, 4.0f);

        if (pGui->addIntVar("Number Of Ring", mNumRing, 1, 4))
        {
            createAOPass();
        }

        pGui->addCheckBox("Apply Blur", mApplyBlur);

        if (mApplyBlur)
        {
            mpBlur->renderUI(pGui, "Blur Settings");
        }

        if (uiGroup) pGui->endGroup();
    }
}

void HDAO::convertToViewSpaceNormal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pNormalTexture)
{
    mpSSAOState->setFbo(mpViewSpaceNormalFbo);

    mpNormalConvVars["CB"]["viewMat"] = pCamera->getViewMatrix();
    mpNormalConvVars->setSampler("gPointSampler", mpPointSampler);
    mpNormalConvVars->setTexture("gNormalMap", pNormalTexture);

    pContext->pushGraphicsVars(mpNormalConvVars);
    mpNormalConvPass->execute(pContext);
    pContext->popGraphicsVars();
}

void HDAO::generateAOMapInternal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture)
{
    // update constants buffer
    ConstantBuffer* pSSAOCB = mpSSAOVars->getDefaultBlock()->getConstantBuffer(mBindLocations.ssaoCB, 0).get();
    pSSAOCB->setVariable("g_fNormalScale", mHDAOData.NormalScale);
    pSSAOCB->setVariable("g_fAcceptAngle", mHDAOData.AcceptAngle);
    pSSAOCB->setVariable("g_f2RTSize", glm::vec2(mpAOFbo->getWidth(), mpAOFbo->getHeight()));
    pSSAOCB->setVariable("g_fHDAORejectRadius", mHDAOData.HDAORejectRadius);
    pSSAOCB->setVariable("g_fHDAOIntensity", mHDAOData.HDAOIntensity);
    pSSAOCB->setVariable("g_fHDAOAcceptRadius", mHDAOData.HDAOAcceptRadius);
    // kernel scale can be different in x/y direction, make it the same for convenience
    pSSAOCB->setVariable("g_f2KernelScale", glm::vec2(mHDAOData.HDAOKernelScale, mHDAOData.HDAOKernelScale));

    // Update state/vars
    mpSSAOState->setFbo(mpAOFbo);
    ParameterBlock* pDefaultBlock = mpSSAOVars->getDefaultBlock().get();
    pDefaultBlock->setSampler(mBindLocations.pointSampler, 0, mpPointSampler);
    pDefaultBlock->setSrv(mBindLocations.depthTex, 0, pDepthTexture->getSRV());
    pDefaultBlock->setSrv(mBindLocations.normalTex, 0, pNormalTexture ? pNormalTexture->getSRV() : nullptr);

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

Texture::SharedPtr HDAO::generateAOMap(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture)
{
    assert(mpAOFbo->getWidth() == pDepthTexture->getWidth() && mpAOFbo->getHeight() == pDepthTexture->getHeight());
    assert(mpAOFbo->getWidth() == pNormalTexture->getWidth() && mpAOFbo->getHeight() == pNormalTexture->getHeight());

    pContext->pushGraphicsState(mpSSAOState);
    convertToViewSpaceNormal(pContext, pCamera, pNormalTexture);
    generateAOMapInternal(pContext, pCamera, pDepthTexture, mpViewSpaceNormalFbo->getColorTexture(0));
    pContext->popGraphicsState();    

    // Blur
    if (mApplyBlur)
    {
        mpBlur->execute(pContext, mpAOFbo->getColorTexture(0), mpAOFbo);
    }

    return mpAOFbo->getColorTexture(0);
}

void HDAO::resizeAOMap(const uvec2& aoMapSize)
{
    if (mpAOFbo == nullptr ||
        mpAOFbo->getWidth() != aoMapSize.x ||
        mpAOFbo->getHeight() != aoMapSize.y)
    {
        Fbo::Desc aoFboDesc;
        aoFboDesc.setColorTarget(0, Falcor::ResourceFormat::R8Unorm);
        mpAOFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, aoFboDesc);

        Fbo::Desc normalFboDesc;
        normalFboDesc.setColorTarget(0, Falcor::ResourceFormat::RGBA8Unorm);
        mpViewSpaceNormalFbo = FboHelper::create2D(aoMapSize.x, aoMapSize.y, normalFboDesc);
    }
}

void HDAO::createAOPass()
{
    mpSSAOPass = FullScreenPass::create("HDAO.ps.hlsl");
    mpSSAOPass->getProgram()->addDefine("RING_CNT", std::to_string(mNumRing));
    mpSSAOVars = GraphicsVars::create(mpSSAOPass->getProgram()->getReflector());

    const ParameterBlockReflection* pReflector = mpSSAOPass->getProgram()->getReflector()->getDefaultParameterBlock().get();
    mBindLocations.internalPerFrameCB = pReflector->getResourceBinding("InternalPerFrameCB");
    mBindLocations.ssaoCB = pReflector->getResourceBinding("SSAOCB");
    mBindLocations.pointSampler = pReflector->getResourceBinding("gPointSampler");
    mBindLocations.depthTex = pReflector->getResourceBinding("gDepthTex");
    mBindLocations.normalTex = pReflector->getResourceBinding("gNormalTex");
}

void HDAO::createNormalConvPass()
{
    mpNormalConvPass = FullScreenPass::create("ConvertToViewSpaceNormal.ps.slang");
    mpNormalConvVars = GraphicsVars::create(mpNormalConvPass->getProgram()->getReflector());
}
