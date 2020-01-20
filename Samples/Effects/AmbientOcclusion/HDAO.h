#pragma once

#include "API/FBO.h"
#include "Graphics/Program/ProgramVars.h"
#include "Graphics/FullScreenPass.h"
#include "Effects/Utils/GaussianBlur.h"
#include "Utils/Gui.h"

namespace Falcor
{
    class Gui;
    class Camera;
    class Scene;

    class HDAO : public std::enable_shared_from_this<HDAO>
    {
    public:
        using SharedPtr = std::shared_ptr<HDAO>;

        static SharedPtr create(const uvec2& aoMapSize, uint32_t kernelSize = 16, uint32_t blurSize = 5, float blurSigma = 2.0f);

        void renderUI(Gui* pGui, const char* uiGroup = "");

        Texture::SharedPtr generateAOMap(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture);

        void resizeAOMap(const uvec2& aoMapSize);

    private:

        HDAO(const uvec2& aoMapSize, uint32_t kernelSize, uint32_t blurSize, float blurSigma);

        void createAOPass();
        void createNormalConvPass();

        void convertToViewSpaceNormal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pNormalTexture);
        void generateAOMapInternal(RenderContext* pContext, const Camera* pCamera, const Texture::SharedPtr& pDepthTexture, const Texture::SharedPtr& pNormalTexture);

        Fbo::SharedPtr mpViewSpaceNormalFbo;
        Fbo::SharedPtr mpAOFbo;

        GraphicsState::SharedPtr mpSSAOState;

        Sampler::SharedPtr mpPointSampler;

        struct
        {
            ProgramReflection::BindLocation internalPerFrameCB;
            ProgramReflection::BindLocation ssaoCB;
            ProgramReflection::BindLocation pointSampler;
            ProgramReflection::BindLocation depthTex;
            ProgramReflection::BindLocation normalTex;
        } mBindLocations;

        FullScreenPass::UniquePtr mpNormalConvPass;
        GraphicsVars::SharedPtr mpNormalConvVars;

        FullScreenPass::UniquePtr mpSSAOPass;
        GraphicsVars::SharedPtr mpSSAOVars;

        int mNumRing = 4;

        bool mApplyBlur = true;
        GaussianBlur::UniquePtr mpBlur;

        struct HDAOData
        {
            float NormalScale = 0.30f;
            float AcceptAngle = 0.98f;
            float HDAORejectRadius = 0.43f;
            float HDAOAcceptRadius = 0.000312f;
            float HDAOIntensity = 2.14f;
            float HDAOKernelScale = 1.0f;
        } mHDAOData;
    };

}
