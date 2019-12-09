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
#pragma once
#include <string>
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "Scene.h"

#include <Graphics/Scene/SceneExporter.h>

namespace pugi
{
    class xml_node;
}

namespace Falcor
{
    class SceneNoriExporter
    {
    public:
        static FileDialogFilterVec kFileExtensionFilters;

        static bool saveScene(const std::string& filename, const Scene* pScene, const Camera* pCamera, vec2 viewportSize);

    private:

        SceneNoriExporter(const std::string& filename, const Scene* pScene, const Camera* pCamera, vec2 viewportSize)
            : mpScene(pScene), mpCamera(pCamera), mFilename(filename), mViewportSize(viewportSize)
        {}

        bool save();

        void writeModels(pugi::xml_node& parent);
        void writeLights(pugi::xml_node& parent);
        void writeCameras(pugi::xml_node& parent);

        const Scene* mpScene = nullptr;
        const Camera* mpCamera = nullptr;
        std::string mFilename;

        vec2 mViewportSize;
        int32_t mSampleCount = 64;
    };
}
