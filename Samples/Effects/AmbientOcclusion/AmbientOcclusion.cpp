#include "AmbientOcclusion.h"
#include "Graphics/Scene/SceneNoriExporter.h"

const std::string AOFX::skDefaultModel = "Arcade/Arcade.fbx";

void AOFX::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
    if (pGui->addButton("Load Model"))
    {
        std::string Filename;
        if (openFileDialog(Model::kFileExtensionFilters, Filename))
        {
            loadModelFromFile(Filename);
        }
    }

    if (pGui->addFloatVar("Camera Speed", mCameraSpeed, 0.001f))
    {
        mCameraController.setCameraSpeed(mCameraSpeed);
    }

    pGui->addDropdown("AO method", mAoMethodList, mAoMethod);

    if (mAoMethod == AO_METHOD_SSAO)
    {
        mpSSAO->renderUI(pGui, "SSAO");
    }
    else if (mAoMethod == AO_METHOD_HDAO)
    {
        mpHDAO->renderUI(pGui, "HDAO");
    }
    else if (mAoMethod == AO_METHOD_HBAO)
    {
        mpHBAO->renderUI(pGui, "HBAO");
    }

    pGui->addCheckBox("Enable Temporal Accumulation", mTemporalAccum);

    if (pGui->addButton("Export Scene To Nori Renderer"))
    {
        std::string filename;
        if (saveFileDialog(SceneNoriExporter::kFileExtensionFilters, filename))
        {
            vec2 sz{ mpGBufferFbo->getWidth(), mpGBufferFbo->getHeight() };

            Scene::SharedPtr pScene = Scene::create();
            pScene->addModelInstance(mpModel, "instance");
            SceneNoriExporter::saveScene(filename, pScene.get(), mpCamera.get(), sz);
        }
    }
}

void AOFX::resetCamera()
{
    if (mpModel)
    {
        // update the camera position
        float radius = mpModel->getRadius();
        const glm::vec3& modelCenter = mpModel->getCenter();
        glm::vec3 camPos = modelCenter;
        camPos.z += radius * 4;

        mpCamera->setPosition(camPos);
        mpCamera->setTarget(modelCenter);
        mpCamera->setUpVector(glm::vec3(0, 1, 0));

        // Update the controllers
        //mCameraController.setModelParams(modelCenter, radius, 4);
        mNearZ = std::max(0.1f, mpModel->getRadius() / 750.0f);
        mFarZ = radius * 10;
    }
}

void AOFX::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    //
    // "GBuffer" rendering
    //

    mpPrePassProgram = GraphicsProgram::createFromFile("AOPrePass.ps.hlsl", "", "main");
    mpPrePassState = GraphicsState::create();
    mpPrePassVars = GraphicsVars::create(mpPrePassProgram->getReflector());

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::Back);
    mpPrePassState->setRasterizerState(RasterizerState::create(rsDesc));

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(true);
    mpPrePassState->setDepthStencilState(DepthStencilState::create(dsDesc));

    mpPrePassState->setProgram(mpPrePassProgram);

    //
    // Apply AO pass
    //

    Sampler::Desc pointDesc;
    pointDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    mpPointSampler = Sampler::create(pointDesc);

    Sampler::Desc linearDesc;
    linearDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearSampler = Sampler::create(linearDesc);

    mpCopyPass = FullScreenPass::create("ApplyAO.ps.hlsl", Program::DefineList(), true, true, 0, false, Shader::CompilerFlags::EmitDebugInfo);
    mpCopyVars = GraphicsVars::create(mpCopyPass->getProgram()->getReflector());

    mpDownscale = DownscalePass::create();

    // Effects
    uvec2 halfSwapChainSize = uvec2(pSample->getWindow()->getClientAreaWidth()/2, pSample->getWindow()->getClientAreaHeight()/2);
    mAoMethodList.push_back({AO_METHOD_SSAO, "SSAO"});
    mAoMethodList.push_back({AO_METHOD_HDAO, "HDAO"});
    mAoMethodList.push_back({AO_METHOD_HBAO, "HBAO"});
    mpSSAO = SSAO::create(halfSwapChainSize);
    mpHDAO = HDAO::create(halfSwapChainSize);
    mpHBAO = HBAO::create(halfSwapChainSize);

    mpCamera = Camera::create();
    mpCamera->setAspectRatio((float)pSample->getCurrentFbo()->getWidth() / (float)pSample->getCurrentFbo()->getHeight());
    mCameraController.attachCamera(mpCamera);

    // Model
    loadModelFromFile(skDefaultModel);
}

Texture::SharedPtr AOFX::generateAOMap(RenderContext* pRenderContext)
{
    if (mAoMethod == AO_METHOD_SSAO)
        return mpSSAO->generateAOMap(pRenderContext, mpCamera.get(), mpHalfResDepth->getColorTexture(0), mpGBufferFbo->getColorTexture(1));
    else if (mAoMethod == AO_METHOD_HDAO)
        return mpHDAO->generateAOMap(pRenderContext, mpCamera.get(), mpHalfResDepth->getColorTexture(0), mpGBufferFbo->getColorTexture(1));
    else if (mAoMethod == AO_METHOD_HBAO)
        return mpHBAO->generateAOMap(pRenderContext, mpCamera.get(), mpHalfResDepth->getColorTexture(0), mpGBufferFbo->getColorTexture(1));
    else
        return nullptr;
}

void AOFX::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    mpCamera->setDepthRange(mNearZ, mFarZ);
    mCameraController.update();

    const glm::vec4 clearColor(0.38f, 0.52f, 0.10f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpGBufferFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    // Render Scene
    mpPrePassState->setFbo(mpGBufferFbo);

    pRenderContext->setGraphicsState(mpPrePassState);
    pRenderContext->setGraphicsVars(mpPrePassVars);
    ModelRenderer::render(pRenderContext, mpModel, mpCamera.get());

    mpDownscale->execute(pRenderContext, mpGBufferFbo->getDepthStencilTexture(), 0, mpHalfResDepth);

    // Generate AO Map
    Texture::SharedPtr pAOMap = generateAOMap(pRenderContext);

    if (mTemporalAccum)
    {
        // Apply AO Map to scene
        mpCopyVars->setSampler("gLinearSampler", mpLinearSampler);
        mpCopyVars->setSampler("gPointSampler", mpPointSampler);
        mpCopyVars->setTexture("gColor", mpGBufferFbo->getColorTexture(0));
        mpCopyVars->setTexture("gAOMap", pAOMap);
        mpCopyVars->setTexture("gAOHistory", mpAoFbo[1]->getColorTexture(0));
        mpCopyVars->setTexture("gLowResDepth", mpHalfResDepth->getColorTexture(0));
        mpCopyVars->setTexture("gFullResDepth", mpGBufferFbo->getDepthStencilTexture());
        mpCopyVars["PerFrameCB"]["gProjMatrix"] = mpCamera->getProjMatrix();
        pRenderContext->setGraphicsVars(mpCopyVars);
        pRenderContext->getGraphicsState()->setFbo(mpAoFbo[0]);
        mpCopyPass->execute(pRenderContext);

        pRenderContext->blit(mpAoFbo[0]->getColorTexture(0)->getSRV(), pTargetFbo->getRenderTargetView(0));
        pRenderContext->blit(mpAoFbo[0]->getColorTexture(0)->getSRV(), mpAoFbo[1]->getRenderTargetView(0));
    }
    else
    {
        pRenderContext->blit(pAOMap->getSRV(), pTargetFbo->getRenderTargetView(0));
    }

}

bool AOFX::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    return mCameraController.onKeyEvent(keyEvent);
}

bool AOFX::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mCameraController.onMouseEvent(mouseEvent);
}

void AOFX::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    mpCamera->setFocalLength(21.0f);
    mpCamera->setAspectRatio((float)width / (float)height);

    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float).setColorTarget(1, Falcor::ResourceFormat::RGBA8Unorm).setDepthStencilTarget(Falcor::ResourceFormat::D32Float);
    mpGBufferFbo = FboHelper::create2D(width, height, fboDesc);

    Fbo::Desc aoFboDesc;
    aoFboDesc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float);
    mpAoFbo[0] = FboHelper::create2D(width, height, aoFboDesc);
    mpAoFbo[1] = FboHelper::create2D(width, height, aoFboDesc);

    Fbo::Desc halfResDepthfboDesc;
    halfResDepthfboDesc.setColorTarget(0, Falcor::ResourceFormat::R32Float);
    mpHalfResDepth = FboHelper::create2D(width/2, height/2, halfResDepthfboDesc);

    mpHDAO->resizeAOMap(glm::uvec2(width/2, height/2));
    mpHBAO->resizeAOMap(glm::uvec2(width/2, height/2));
}

void AOFX::loadModelFromFile(const std::string& filename)
{
    Model::LoadFlags flags = Model::LoadFlags::None;
    mpModel = Model::createFromFile(filename.c_str(), flags);
    if(mpModel == nullptr)
    {
        msgBox("Could not load model");
        return;
    }

    resetCamera();
}

#ifdef _WIN32
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int main(int argc, char** argv)
#endif
{
    ::LoadLibraryA("C:\\bin\\RenderDoc_1.4_64\\renderdoc.dll");

    AOFX::UniquePtr pRenderer = std::make_unique<AOFX>();
    SampleConfig config;
    config.windowDesc.title = "Ambient Occlusion";
    config.windowDesc.resizableWindow = true;
#ifdef _WIN32
    Sample::run(config, pRenderer);
#else
    config.argc = (uint32_t)argc;
    config.argv = argv;
    Sample::run(config, pRenderer);
#endif
    return 0;
}
