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
#include "ImageComparer.h"

void ImageComparer::onGuiRender()
{
    mpGui->addSeparator();

    if (mpGui->addCheckBox("sRGB", mSrgb))
    {
        resetImages();
        if (mLeftFilename.length())
        {
            loadImage(true, mLeftFilename);
        }
        if (mRightFilename.length())
        {
            loadImage(false, mRightFilename);
        }
    }
    if (mpGui->addButton("Reset Images"))
    {
        resetImages();
    }
    if (mpGui->addButton("Load Image Left"))
    {
        if (openFileDialog(nullptr, mLeftFilename))
        {
            loadImage(true, mLeftFilename);
        }
    }
    if (mpGui->addButton("Load Image Right", true))
    {
        if (openFileDialog(nullptr, mRightFilename))
        {
            loadImage(false, mRightFilename);
        }
    }

    mpGui->addSeparator();
    mpGui->addFloatVar("Exposure", mExposure, -10.0f, 10.0f, 1.0f);
}

void ImageComparer::onResizeSwapChain()
{
    mWindowWidth = (float)mpDefaultFBO->getWidth();
}

void ImageComparer::onLoad()
{
    initShader();

    if (mArgList.argExists("left"))
    {
        mLeftFilename = mArgList["left"].asString();
        loadImage(true, mLeftFilename);
    }
    if (mArgList.argExists("right"))
    {
        mRightFilename = mArgList["right"].asString();
        loadImage(false, mRightFilename);
    }
    if (mArgList.argExists("exposure"))
    {
        mExposure = mArgList["exposure"].asFloat();
    }

    mSrgb = mArgList.argExists("srgb");
}

void ImageComparer::initShader()
{
    mpComparisonPass = FullScreenPass::create("ImageComparer.ps.slang");
    mpProgVars = GraphicsVars::create(mpComparisonPass->getProgram()->getActiveVersion()->getReflector());

    Sampler::Desc desc;
    Sampler::SharedPtr pointSamp = Sampler::create(desc);
    mpProgVars->setSampler("gSampler", pointSamp);
}

void ImageComparer::loadImage(bool left, std::string filename)
{
    auto compareTextureSize = [](Texture::SharedConstPtr src, Texture::SharedConstPtr dst)
    {
        if (src != nullptr && dst != nullptr)
        {
            if ((src->getWidth() != dst->getWidth()) ||
                (src->getHeight() != dst->getHeight()))
            {
                return false;
            }
        }
        return true;
    };

    Texture::SharedPtr pTex = Falcor::createTextureFromFile(filename, false, mSrgb);
    if (left)
    {
        if (compareTextureSize(pTex, mpRightTexture))
        {
            mpLeftTexture = pTex;
            resizeSwapChain(pTex->getWidth(), pTex->getHeight());
        }
        else
        {
            logWarning("Two texture size is not matching.");
        }
    }
    else
    {
        if (compareTextureSize(pTex, mpLeftTexture))
        {
            mpRightTexture = pTex;
            resizeSwapChain(pTex->getWidth(), pTex->getHeight());
        }
        else
        {
            logWarning("Two texture size is not matching.");
        }
    }
}

void ImageComparer::resetImages()
{
    mpLeftTexture = nullptr;
    mpRightTexture = nullptr;
}

void ImageComparer::onFrameRender()
{

    const glm::vec4 clearColor(0.33f, 0.33f, 0.33f, 1);
    mpRenderContext->clearFbo(mpDefaultFBO.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    const GraphicsState::Scissor scissorBak = mpRenderContext->getGraphicsState()->getScissors(0);
    mpRenderContext->pushGraphicsVars(mpProgVars);

    mpProgVars["PerFrameCB"]["gExposure"] = mExposure;

    const int32_t sliderPosX = (int32_t)std::floor(mWindowWidth * mSliderPos);

    if (mpLeftTexture != nullptr)
    {

        GraphicsState::Scissor scissor = scissorBak;
        scissor.right = sliderPosX - mSliderWidth;
        mpRenderContext->getGraphicsState()->pushScissors(0, scissor);

        mpProgVars->setTexture("gTexture", mpLeftTexture);
        mpComparisonPass->execute(mpRenderContext.get());

        mpRenderContext->getGraphicsState()->popScissors(0);
    }

    if (mpRightTexture != nullptr)
    {
        GraphicsState::Scissor scissor = scissorBak;
        scissor.left = sliderPosX + mSliderWidth;
        mpRenderContext->getGraphicsState()->pushScissors(0, scissor);

        mpProgVars->setTexture("gTexture", mpRightTexture);
        mpComparisonPass->execute(mpRenderContext.get());

        mpRenderContext->getGraphicsState()->popScissors(0);
    }

    mpRenderContext->popGraphicsVars();
}

void ImageComparer::onShutdown()
{
    mpLeftTexture = nullptr;
    mpRightTexture = nullptr;
    mpComparisonPass = nullptr;
    mpProgVars = nullptr;
}

bool ImageComparer::onKeyEvent(const KeyboardEvent& keyEvent)
{
    if (mSliderMoveMode)
    {
    }

    return false;
}

bool ImageComparer::onMouseEvent(const MouseEvent& mouseEvent)
{
    if (mouseEvent.type == MouseEvent::Type::LeftButtonDown)
    {
        mSliderMoveMode = true;
        mSliderPos = mouseEvent.pos.x;
    }
    else if (mouseEvent.type == MouseEvent::Type::LeftButtonUp)
    {
        mSliderMoveMode = false;
    }
    else if (mouseEvent.type == MouseEvent::Type::Move)
    {
        if (mSliderMoveMode)
        {
            mSliderPos = mouseEvent.pos.x;
        }
    }

    return false;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    ImageComparer sceneEditor;
    SampleConfig config;
    config.windowDesc.title = "Image Comparer";
    config.windowDesc.width = 512;
    config.windowDesc.height = 512;
    config.deviceDesc.enableVsync = true;
    config.freezeTimeOnStartup = true;
    config.showMessageBoxOnError = true;
    sceneEditor.run(config);
}
