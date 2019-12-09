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
#include "TextureSpaceShading.h"
#include <Graphics/Scene/SceneNoriExporter.h>

const std::string skDefaultScene = "E:/Character_PBR_Experiment/expr.fscene";

const Gui::DropdownList aaModeList =
{
    { 0, "None"},
    { 1, "FXAA" }
};

const Gui::DropdownList renderPathList =
{
    { 0, "Deferred"},
    { 1, "Forward"},
};

void TextureSpaceShading::initShadowPass(uint32_t windowWidth, uint32_t windowHeight)
{
    mpShadowPass = CascadedShadowMaps::create(mpSceneRenderer->getScene()->getLight(0), 2048, 2048, windowWidth, windowHeight, mpSceneRenderer->getScene()->shared_from_this());
    mpShadowPass->setFilterMode(CsmFilterHwPcf);
    mpShadowPass->setVsmLightBleedReduction(0.3f);
    mpShadowPass->setVsmMaxAnisotropy(4);
    mpShadowPass->setEvsmBlur(7, 3);
}

void TextureSpaceShading::setSceneSampler(uint32_t maxAniso)
{
    Scene* pScene = mpSceneRenderer->getScene().get();
    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(maxAniso);
    mpSceneSampler = Sampler::create(samplerDesc);
    pScene->bindSampler(mpSceneSampler);
}

void TextureSpaceShading::applyCustomSceneVars(const Scene* pScene, const std::string& filename)
{
    std::string folder = getDirectoryFromFile(filename);

    Scene::UserVariable var = pScene->getUserVariable("sky_box");
    if (var.type == Scene::UserVariable::Type::String) initSkyBox(folder + '/' + var.str);
}

void TextureSpaceShading::initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene)
{
    if (pScene->getCameraCount() == 0)
    {
        // Place the camera above the center, looking slightly downwards
        const Model* pModel = pScene->getModel(0).get();
        Camera::SharedPtr pCamera = Camera::create();

        vec3 position = pModel->getCenter();
        float radius = pModel->getRadius();
        position.y += 0.1f * radius;
        pScene->setCameraSpeed(radius);

        pCamera->setPosition(position);
        pCamera->setTarget(position + vec3(0, -0.3f, -radius));
        pCamera->setDepthRange(0.1f, radius * 10);

        pScene->addCamera(pCamera);
    }

    if (pScene->getLightCount() == 0)
    {
        // Create a directional light
        DirectionalLight::SharedPtr pDirLight = DirectionalLight::create();
        pDirLight->setWorldDirection(vec3(-0.189f, -0.861f, -0.471f));
        pDirLight->setIntensity(vec3(1, 1, 0.985f) * 10.0f);
        pDirLight->setName("DirLight");
        pScene->addLight(pDirLight);
    }

    if (pScene->getLightProbeCount() > 0)
    {
        const LightProbe::SharedPtr& pProbe = pScene->getLightProbe(0);
        pProbe->setRadius(pScene->getRadius());
        pProbe->setPosW(pScene->getCenter());
        pProbe->setSampler(mpSceneSampler);
    }

    mpSceneRenderer = SceneRenderer::create(pScene);
    mpSceneRenderer->setCameraControllerType(SceneRenderer::CameraControllerType::FirstPerson);
    mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
    setSceneSampler(mpSceneSampler ? mpSceneSampler->getMaxAnisotropy() : 4);
    setActiveCameraAspectRatio(pSample->getCurrentFbo()->getWidth(), pSample->getCurrentFbo()->getHeight());

    mpDepthPass = DepthPass::create();
    mpDepthPass->setScene(pScene);

    mpForwardPass = ForwardLightingPass::create();
    mpForwardPass->setScene(pScene);
    mpForwardPass->usePreGeneratedDepthBuffer(true);

    auto pTargetFbo = pSample->getCurrentFbo();
    initShadowPass(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    initAA(pSample);

    mpSSAO = SSAO::create(float2(pTargetFbo->getWidth(), pTargetFbo->getHeight()));

    mpGBufferRaster = GBufferRaster::create();
    mpGBufferRaster->setScene(pScene);

    mpGBufferLightingPass = GBufferLightingPass::create();
    mpGBufferLightingPass->setScene(pScene);

    mpBlitPass = BlitPass::create();

    mpAdditiveBlitPass = BlitPass::create();
    mpAdditiveBlitPass->setEnableBlend(0, true);
    mpAdditiveBlitPass->setBlendParams(0,
        BlendState::BlendOp::Add, BlendState::BlendOp::Add,
        BlendState::BlendFunc::One, BlendState::BlendFunc::One,
        BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
    
    mpToneMapper = ToneMapping::create(ToneMapping::Operator::Fixed);
    mpToneMapper->setExposureValue(0);

    pSample->setCurrentTime(0);

    mShadingTexSpace.mpRaster = GBufferRasterTextureSpace::create(RasterizerState::CullMode::None);
    mShadingTexSpace.mpRaster->setScene(pScene);
    mShadingTexSpace.mpShadingPass = GBufferShadingTextureSpace::create();
    mShadingTexSpace.mpShadingPass->setScene(pScene);
    mShadingTexSpace.mpTs2Ss = TextureSpaceToScreenSpace::create();
    mShadingTexSpace.mpTs2Ss->setScene(pScene);
}

void TextureSpaceShading::resetScene()
{
    mpSceneRenderer = nullptr;
    mpSkyPass = nullptr;

    if (mpGBufferRaster)
    {
        mpGBufferRaster->setScene(nullptr);
    }
}

void TextureSpaceShading::loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Model");
    }

    Model::SharedPtr pModel = Model::createFromFile(filename.c_str());
    if (!pModel) return;
    Scene::SharedPtr pScene = Scene::create();
    pScene->addModelInstance(pModel, "instance");

    initScene(pSample, pScene);
}

void TextureSpaceShading::loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Scene", 100);
    }

    Scene::SharedPtr pScene = Scene::loadFromFile(filename);

    if (pScene != nullptr)
    {
        initScene(pSample, pScene);
        applyCustomSceneVars(pScene.get(), filename);
    }
}

void TextureSpaceShading::initSkyBox(const std::string& name)
{
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    Sampler::SharedPtr pSampler = Sampler::create(samplerDesc);
    mpSkyPass = SkyBox::create(name, true, pSampler);
}

void TextureSpaceShading::updateLightProbe(const LightProbe::SharedPtr& pLight)
{
    Scene::SharedPtr pScene = mpSceneRenderer->getScene();

    // Remove existing light probes
    while (pScene->getLightProbeCount() > 0)
    {
        pScene->deleteLightProbe(0);
    }

    // Use it as infinite environment light
    pLight->markAsInfiniteEnvironmentLight();
    pLight->setSampler(mpSceneSampler);
    pScene->addLightProbe(pLight);
}

void TextureSpaceShading::initAA(SampleCallbacks* pSample)
{
    mpFXAA = FXAA::create();
    applyAaMode(pSample);
}

void TextureSpaceShading::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    loadScene(pSample, skDefaultScene, true);
}

void TextureSpaceShading::renderSkyBox(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    if (mpSkyPass)
    {
        PROFILE("skyBox");
        GPU_EVENT(pContext, "skybox");
        mpSkyPass->render(pContext, mpSceneRenderer->getScene()->getActiveCamera().get(), pTargetFbo);
    }
}

void TextureSpaceShading::beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId)
{
    GPU_EVENT(pContext, "beginFrame");
    pContext->clearFbo(mpMainFbo.get(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 1, 0, FboAttachmentType::All);
    pContext->clearFbo(mpGBufferFbo.get(), vec4(0), 1.f, 0, FboAttachmentType::All);
    pContext->clearFbo(mpPostProcessFbo.get(), glm::vec4(), 1, 0, FboAttachmentType::Color);
}

void TextureSpaceShading::endFrame(RenderContext* pContext)
{
    GPU_EVENT(pContext, "endFrame");
    pContext->popGraphicsState();
}

void TextureSpaceShading::toneMapping(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("toneMapping");
    GPU_EVENT(pContext, "toneMapping");
    mpToneMapper->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
}

void TextureSpaceShading::renderGBuffer(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("GBuffer");
    GPU_EVENT(pContext, "GBuffer");
    mpGBufferRaster->execute(pContext, pTargetFbo, nullptr);
}

void TextureSpaceShading::deferredLighting(RenderContext* pContext, Fbo::SharedPtr pGBufferFbo, Texture::SharedPtr visibilityTexture, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("Lighting");
    GPU_EVENT(pContext, "Lighting");
    mpGBufferLightingPass->execute(pContext, pGBufferFbo, visibilityTexture, pTargetFbo);
}

void TextureSpaceShading::depthPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("depthPass");
    GPU_EVENT(pContext, "depthPass");
    mpDepthPass->execute(pContext, pTargetFbo);
}

void TextureSpaceShading::forwardLightingPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("lightingPass");
    GPU_EVENT(pContext, "lightingPass");
    mpForwardPass->execute(pContext,
                           mEnableShadows ? mpShadowPass->getVisibilityBuffer() : mpDummyVisFbo->getColorTexture(0),
                           pTargetFbo);
}

void TextureSpaceShading::shadowPass(RenderContext* pContext, Texture::SharedPtr pDepthTexture)
{
    PROFILE("shadowPass");
    GPU_EVENT(pContext, "shadowPass");
    if (mEnableShadows)
    {
        const Camera* pCamera = mpSceneRenderer->getScene()->getActiveCamera().get();
        mpShadowPass->generateVisibilityBuffer(pContext, pCamera, pDepthTexture);
        pContext->flush();
    }
    else
    {
        pContext->clearFbo(mpDummyVisFbo.get(), vec4(1), 1, 0, FboAttachmentType::All);
    }
}

void TextureSpaceShading::ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    if (mEnableSSAO)
    {
        PROFILE("SSAO");
        GPU_EVENT(pContext, "SSAO");
        const Camera* pCamera = mpSceneRenderer->getScene()->getActiveCamera().get();
        Texture::SharedPtr pDepthTex = mpResolveFbo->getDepthStencilTexture();
        Texture::SharedPtr pNormalTex = mpResolveFbo->getColorTexture(1);
        mpSSAO->execute(pContext, pCamera, mpPostProcessFbo->getColorTexture(0), pTargetFbo->getColorTexture(0), pDepthTex, pNormalTex);
    }
}

void TextureSpaceShading::postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("postProcess");
    GPU_EVENT(pContext, "postProcess");

    Fbo::SharedPtr pPostProcessDst = mEnableSSAO ? mpPostProcessFbo : pTargetFbo;
    toneMapping(pContext, pPostProcessDst);
    ambientOcclusion(pContext, pTargetFbo);
    executeFXAA(pContext, pTargetFbo);
}

void TextureSpaceShading::executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    if(mAAMode == AAMode::FXAA)
    {
        PROFILE("FXAA");
        GPU_EVENT(pContext, "FXAA");
        pContext->blit(pTargetFbo->getColorTexture(0)->getSRV(), mpResolveFbo->getRenderTargetView(0));
        mpFXAA->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
    }
}

void TextureSpaceShading::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    if (mpSceneRenderer)
    {
        beginFrame(pRenderContext, pTargetFbo.get(), pSample->getFrameID());
        {
            PROFILE("updateScene");
            GPU_EVENT(pRenderContext, "updateScene");
            mpSceneRenderer->update(pSample->getCurrentTime());
        }

        depthPass(pRenderContext, mpDepthPassFbo);
        shadowPass(pRenderContext, mpDepthPassFbo->getDepthStencilTexture());
        if (mRenderPath == RenderPath::Deferred)
        {
            renderGBuffer(pRenderContext, mpGBufferFbo);
            deferredLighting(pRenderContext,
                             mpGBufferFbo,
                             mEnableShadows ? mpShadowPass->getVisibilityBuffer() : mpDummyVisFbo->getColorTexture(0),
                             mpMainFbo);
            mpBlitPass->execute(pRenderContext, mpGBufferFbo->getColorTexture(GBufferRT::NORMAL_BITANGENT), mpMainFbo->getColorTexture(1));
            mpBlitPass->execute(pRenderContext, mpGBufferFbo->getColorTexture(GBufferRT::MOTION_VECTOR), mpMainFbo->getColorTexture(2));
        }
        else
        {
            forwardLightingPass(pRenderContext, mpMainFbo);
        }
        renderSkyBox(pRenderContext, mpMainFbo);
        postProcess(pRenderContext, pTargetFbo);

        {
            PROFILE("textureSpaceShading");
            GPU_EVENT(pRenderContext, "textureSpaceShading");

            pRenderContext->clearFbo(mShadingTexSpace.mpGBufferFbo.get(), vec4(0), 1, 0);
            pRenderContext->clearFbo(mShadingTexSpace.mpResultFbo.get(), vec4(0), 1, 0);

            mShadingTexSpace.mpRaster->execute(pRenderContext, mShadingTexSpace.mpGBufferFbo, mpSceneRenderer->getScene()->getActiveCamera());
            mShadingTexSpace.mpShadingPass->execute(pRenderContext, mShadingTexSpace.mpGBufferFbo, mShadingTexSpace.mpResultFbo);

            if (mDebugTexSpaceShading)
            {
                pRenderContext->clearFbo(pTargetFbo.get(), vec4(0), 1, 0, FboAttachmentType::All);
                mShadingTexSpace.mpTs2Ss->execute(pRenderContext,
                                                  mpSceneRenderer->getScene()->getActiveCamera(),
                                                  mShadingTexSpace.mpResultFbo->getColorTexture(0),
                                                  pTargetFbo);
            }

            pRenderContext->blit(mShadingTexSpace.mpResultFbo->getColorTexture(0)->getSRV(),
                                 pTargetFbo->getRenderTargetView(0),
                                 uvec4(-1),
                                 uvec4(0, 0, 512, 512));
        }

        if (mCaptureToFile)
        {
            captureToFile(std::string(""));
            mCaptureToFile = false;
        }

        endFrame(pRenderContext);
    }
    else
    {
        pRenderContext->clearFbo(pTargetFbo.get(), vec4(0.2f, 0.4f, 0.5f, 1), 1, 0);
    }

}

void TextureSpaceShading::applyCameraPathState()
{
    const Scene* pScene = mpSceneRenderer->getScene().get();
    if(pScene->getPathCount())
    {
        mUseCameraPath = mUseCameraPath;
        if (mUseCameraPath)
        {
            pScene->getPath(0)->attachObject(pScene->getActiveCamera());
        }
        else
        {
            pScene->getPath(0)->detachObject(pScene->getActiveCamera());
        }
    }
}

bool TextureSpaceShading::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    if (mpSceneRenderer && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        switch (keyEvent.key)
        {
        case KeyboardEvent::Key::Minus:
            mUseCameraPath = !mUseCameraPath;
            applyCameraPathState();
            return true;
        case KeyboardEvent::Key::O:
            mPerMaterialShader = !mPerMaterialShader;
            mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            return true;
        }
    }

    return mpSceneRenderer ? mpSceneRenderer->onKeyEvent(keyEvent) : false;
}

void TextureSpaceShading::onDroppedFile(SampleCallbacks* pSample, const std::string& filename)
{
    if (hasSuffix(filename, ".fscene", false) == false)
    {
        msgBox("You can only drop a scene file into the window");
        return;
    }
    loadScene(pSample, filename, true);
}

bool TextureSpaceShading::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mpSceneRenderer ? mpSceneRenderer->onMouseEvent(mouseEvent) : true;
}

void TextureSpaceShading::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    // Create the post-process FBO and AA resolve Fbo
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
    mpPostProcessFbo = FboHelper::create2D(width, height, fboDesc);

    Fbo::Desc visFboDesc;
    visFboDesc.setColorTarget(0, ResourceFormat::R16Float);
    mpDummyVisFbo = FboHelper::create2D(width, height, visFboDesc);

    applyAaMode(pSample);
    mpShadowPass->onResize(width, height);

    if(mpSceneRenderer)
    {
        setActiveCameraAspectRatio(width, height);
    }

    onResizeTextureSpace(mShadingTexSpace.mRenderTargetSize);
}

void TextureSpaceShading::setActiveCameraAspectRatio(uint32_t w, uint32_t h)
{
    mpSceneRenderer->getScene()->getActiveCamera()->setAspectRatio((float)w / (float)h);
}

void TextureSpaceShading::applyAaMode(SampleCallbacks* pSample)
{
    uint32_t w = pSample->getCurrentFbo()->getWidth();
    uint32_t h = pSample->getCurrentFbo()->getHeight();

    // Common Depth FBO, shared by depth pass, GBuffer pass and forward pass
    Fbo::Desc depthFboDesc;
    depthFboDesc.setDepthStencilTarget(ResourceFormat::D32Float);
    mpDepthPassFbo = FboHelper::create2D(w, h, depthFboDesc);

    // Common FBO desc (2 color outputs - color and normal)
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA32Float).setColorTarget(1, ResourceFormat::RGBA8Unorm);

    mpSceneRenderer->getScene()->getActiveCamera()->setPatternGenerator(nullptr);

    if (mAAMode == AAMode::FXAA)
    {
        Fbo::Desc resolveDesc;
        resolveDesc.setColorTarget(0, pSample->getCurrentFbo()->getColorTexture(0)->getFormat());
        mpResolveFbo = FboHelper::create2D(w, h, resolveDesc);
    }

    mpMainFbo = FboHelper::create2D(w, h, fboDesc);
    mpMainFbo->attachDepthStencilTarget(mpDepthPassFbo->getDepthStencilTexture());

    mpGBufferFbo = GBufferRaster::createGBufferFbo(w, h, false);
    mpGBufferFbo->attachDepthStencilTarget(mpDepthPassFbo->getDepthStencilTexture());

    mpResolveFbo = mpMainFbo;
}

void TextureSpaceShading::onResizeTextureSpace(int sz)
{
    mShadingTexSpace.mpGBufferFbo = GBufferRasterTextureSpace::createGBufferFbo(sz, sz, false);
    Fbo::Desc tsShadingResultFboDesc;
    tsShadingResultFboDesc.setColorTarget(0, ResourceFormat::RGBA32Float);
    mShadingTexSpace.mpResultFbo = FboHelper::create2D(sz, sz, tsShadingResultFboDesc);
}

void TextureSpaceShading::captureToFile(std::string& explicitOutputDirectory)
{
    std::string outputDirectory = !explicitOutputDirectory.empty() ? explicitOutputDirectory : getExecutableDirectory();

    std::time_t t = std::time(nullptr);
    struct tm* localTm = std::localtime(&t);
    char strBuf[64];
    std::strftime(strBuf, sizeof(strBuf), "%F_%H-%M-%S", std::localtime(&t));

    std::string filenameWithoutPostfix = outputDirectory + "\\" + getExecutableName() + "_" + std::string(strBuf);
    std::string finaleResultFilename = filenameWithoutPostfix + "_final.exr";
    mShadingTexSpace.mpResultFbo->getColorTexture(0)->captureToFile(0, 0, finaleResultFilename, Bitmap::FileFormat::ExrFile);
    std::string diffuseFilename = filenameWithoutPostfix + "_diffuse.exr";
    mShadingTexSpace.mpGBufferFbo->getColorTexture(2)->captureToFile(0, 0, diffuseFilename, Bitmap::FileFormat::ExrFile);
}

void TextureSpaceShading::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
    static const FileDialogFilterVec kImageFilesFilter = { {"bmp"}, {"jpg"}, {"dds"}, {"png"}, {"tiff"}, {"tif"}, {"tga"}, {"hdr"}, {"exr"}  };

    if (pGui->addButton("Capture To File"))
    {
        mCaptureToFile = true;
    }

    if (pGui->addButton("Load Model"))
    {
        std::string filename;
        if (openFileDialog(Model::kFileExtensionFilters, filename))
        {
            loadModel(pSample, filename, true);
        }
    }

    if (pGui->addButton("Load Scene"))
    {
        std::string filename;
        if (openFileDialog(Scene::kFileExtensionFilters, filename))
        {
            loadScene(pSample, filename, true);
        }
    }

    if (mpSceneRenderer)
    {
        if (pGui->addButton("Load SkyBox Texture"))
        {
            std::string filename;
            if (openFileDialog(kImageFilesFilter, filename))
            {
                initSkyBox(filename);
            }
        }

        if (pGui->addButton("Export Scene To Nori Renderer"))
        {
            std::string filename;
            if (saveFileDialog(SceneNoriExporter::kFileExtensionFilters, filename))
            {
                vec2 sz{ mpGBufferFbo->getWidth(), mpGBufferFbo->getHeight() };
                SceneNoriExporter::saveScene(filename, mpSceneRenderer->getScene().get(), nullptr, sz);
            }
        }

        if (pGui->beginGroup("Scene Settings"))
        {
            Scene* pScene = mpSceneRenderer->getScene().get();
            float camSpeed = pScene->getCameraSpeed();
            if (pGui->addFloatVar("Camera Speed", camSpeed))
            {
                pScene->setCameraSpeed(camSpeed);
            }

            vec2 depthRange(pScene->getActiveCamera()->getNearPlane(), pScene->getActiveCamera()->getFarPlane());
            if (pGui->addFloat2Var("Depth Range", depthRange, 0, FLT_MAX))
            {
                pScene->getActiveCamera()->setDepthRange(depthRange.x, depthRange.y);
            }

            if (pScene->getPathCount() > 0)
            {
                if (pGui->addCheckBox("Camera Path", mUseCameraPath))
                {
                    applyCameraPathState();
                }
            }

            if (pScene->getLightCount() && pGui->beginGroup("Light Sources"))
            {
                for (uint32_t i = 0; i < pScene->getLightCount(); i++)
                {
                    Light* pLight = pScene->getLight(i).get();
                    pLight->renderUI(pGui, pLight->getName().c_str());
                }
                pGui->endGroup();
            }

            pGui->endGroup();
        }

        if (pGui->beginGroup("Renderer Settings"))
        {
            pGui->addDropdown("Render Path", renderPathList, (uint32_t&)mRenderPath);

            if (pGui->addCheckBox("Specialize Material Shaders", mPerMaterialShader))
            {
                mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            }
            pGui->addTooltip("Create a specialized version of the lighting program for each material in the scene");

            uint32_t maxAniso = mpSceneSampler->getMaxAnisotropy();
            if (pGui->addIntVar("Max Anisotropy", (int&)maxAniso, 1, 16))
            {
                setSceneSampler(maxAniso);
            }

            pGui->endGroup();
        }

        if (pGui->beginGroup("Light Probes"))
        {
            if (pGui->addButton("Add/Change Light Probe"))
            {
                std::string filename;
                if (openFileDialog(kImageFilesFilter, filename))
                {
                    updateLightProbe(LightProbe::create(pSample->getRenderContext(), filename, true, ResourceFormat::RGBA16Float));
                }
            }

            Scene::SharedPtr pScene = mpSceneRenderer->getScene();
            if (pScene->getLightProbeCount() > 0)
            {
                pGui->addSeparator();
                pScene->getLightProbe(0)->renderUI(pGui);
            }

            pGui->endGroup();
        }

        if (pGui->beginGroup("Shadows"))
        {
            pGui->addCheckBox("Enable Shadows", mEnableShadows);
            mpShadowPass->renderUI(pGui);

            pGui->endGroup();
        }

        //  Anti-Aliasing Controls.
        if (pGui->beginGroup("Anti-Aliasing"))
        {
            bool reapply = false;
            reapply = reapply || pGui->addDropdown("AA Mode", aaModeList, (uint32_t&)mAAMode);

            if (mAAMode == AAMode::FXAA)
            {
                mpFXAA->renderUI(pGui, "FXAA");
            }

            if (reapply) applyAaMode(pSample);

            pGui->endGroup();
        }

        if (pGui->beginGroup("SSAO"))
        {
            pGui->addCheckBox("Enable SSAO", mEnableSSAO);
            if (mEnableSSAO)
            {
                mpSSAO->renderUI(pGui);
            }
            pGui->endGroup();
        }

        mpToneMapper->renderUI(pGui, "Tone-Mapping");

        if (pGui->beginGroup("Texture Space Shading"))
        {
            pGui->addCheckBox("Debug Texture Space Shading", mDebugTexSpaceShading);
            if (pGui->addIntVar("Render Target Size", mShadingTexSpace.mRenderTargetSize, 128, 1024*8))
            {
                onResizeTextureSpace(mShadingTexSpace.mRenderTargetSize);
            }

            pGui->endGroup();
        }
    }
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    ::LoadLibraryA("C:\\bin\\RenderDoc_1.4_64\\renderdoc.dll");

    TextureSpaceShading::UniquePtr pRenderer = std::make_unique<TextureSpaceShading>();
    SampleConfig config;
    config.windowDesc.title = "Falcor Project Template";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);
    return 0;
}
