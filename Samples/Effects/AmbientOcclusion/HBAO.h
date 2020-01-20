#pragma once

#include "API/FBO.h"
#include "Graphics/Program/ProgramVars.h"
#include "Graphics/FullScreenPass.h"
#include "Effects/Utils/GaussianBlur.h"
#include "Utils/Gui.h"
#include "BilateralFilter.h"

namespace Falcor
{
    class Gui;
    class Camera;
    class Scene;

    class HBAO : public std::enable_shared_from_this<HBAO>
    {
    public:
        using SharedPtr = std::shared_ptr<HBAO>;

        static SharedPtr create(const uvec2& aoMapSize, uint32_t kernelSize = 16, uint32_t blurSize = 5, float blurSigma = 2.0f);

        void renderUI(Gui* pGui, const char* uiGroup = "");

        Texture::SharedPtr generateAOMap(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture);

        void resizeAOMap(const uvec2& aoMapSize);

    private:

        HBAO(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma);

        void createAOPass();

        void generateAOMapInternal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture);

        Fbo::SharedPtr mpAOFbo;
        Fbo::SharedPtr mpAOTmpFbo;
        Fbo::SharedPtr mpAOHistoryFbo;

        BilateralFilter::SharedPtr mpBilateralFilter;

        Fbo::SharedPtr mpDebugFbo;
        TypedBuffer<glm::vec4>::SharedPtr mpDebugData;

        GraphicsState::SharedPtr mpSSAOState;

        Sampler::SharedPtr mpPointSampler;

        struct
        {
            ProgramReflection::BindLocation internalPerFrameCB;
            ProgramReflection::BindLocation ssaoCB;
            ProgramReflection::BindLocation pointSampler;
            ProgramReflection::BindLocation depthTex;
            ProgramReflection::BindLocation normalTex;
            ProgramReflection::BindLocation historyTex;

            ProgramReflection::BindLocation debugTex;
            ProgramReflection::BindLocation debugData;
        } mBindLocations;

        FullScreenPass::UniquePtr mpSSAOPass;
        GraphicsVars::SharedPtr mpSSAOVars;

        float mFrameCount = 1;
        bool mPhysicallyCorrect = true;
        bool mCosWeightedAO = true;

        enum BlurMode
        {
            None,
            Gaussian,
            Bilateral,
        };

        BlurMode mBlurMode = None;
        GaussianBlur::UniquePtr mpBlur;
    
        float mStepSizeLinearBlend = 0.5;
        bool mDebugView;
        int2 mDebugPixel;

        struct HBAOData
        {
            float mOcclusionRayLength = 1.0f;
        } mHBAOData;
    };

}
