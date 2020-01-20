#pragma once

#include "Falcor.h"
#include "HDAO.h"
#include "HBAO.h"
#include "DownscalePass.h"

using namespace Falcor;

class AOFX : public Renderer
{
public:
    void onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext) override;
    void onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent) override;
    void onGuiRender(SampleCallbacks* pSample, Gui* pGui) override;

private:

    Texture::SharedPtr generateAOMap(RenderContext* pRenderContext);

    void resetCamera();

    void loadModelFromFile(const std::string& filename);

    Model::SharedPtr mpModel;

    Camera::SharedPtr mpCamera;
    FirstPersonCameraController mCameraController;
    float mCameraSpeed = 1;

    float mNearZ = 1e-2f;
    float mFarZ = 1e3f;

    Fbo::SharedPtr mpGBufferFbo;
    Fbo::SharedPtr mpAoFbo[2];

    DownscalePass::SharedPtr mpDownscale;
    Fbo::SharedPtr mpHalfResDepth;

    GraphicsProgram::SharedPtr mpPrePassProgram;
    GraphicsVars::SharedPtr mpPrePassVars;
    GraphicsState::SharedPtr mpPrePassState;

    FullScreenPass::UniquePtr mpCopyPass;
    GraphicsVars::SharedPtr mpCopyVars;

    Sampler::SharedPtr mpPointSampler;
    Sampler::SharedPtr mpLinearSampler;

    enum
    {
        AO_METHOD_SSAO,
        AO_METHOD_HDAO,
        AO_METHOD_HBAO,
    };
    Gui::DropdownList mAoMethodList;
    uint32_t mAoMethod = AO_METHOD_HBAO;

    SSAO::SharedPtr mpSSAO;
    HDAO::SharedPtr mpHDAO;
    HBAO::SharedPtr mpHBAO;
 
    bool mTemporalAccum = false;

    static const std::string skDefaultModel;
};
