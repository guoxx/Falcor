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
#include "Graphics/Scene/Scene.h"
#include "Externals/pugixml/pugixml-1.8/src/pugixml.hpp"

namespace Falcor
{

    class SceneMitsubaExporter
    {
    public:
        friend class Scene;

        enum : uint32_t
        {
            ExportGlobalSettings = 0x1,
            ExportModels         = 0x2,
            ExportLights         = 0x4,
            ExportCameras        = 0x8,
            ExportPaths          = 0x10,
            ExportUserDefined    = 0x20,
            ExportMaterials      = 0x40,
            ExportAll = 0xFFFFFFFF
        };

        static bool saveScene(const std::string& filename, const Scene::SharedPtr& pScene, float viewportWidth, float viewportHeight, uint32_t exportOptions = ExportAll);

        static const uint32_t kVersion = 2;

    private:

        SceneMitsubaExporter(const std::string& filename, const Scene::SharedPtr& pScene)
            : mpScene(pScene), mFilename(filename) {}

        bool save(float viewportWidth, float viewportHeight, uint32_t exportOptions);

        void writeModels();
        void writeLights();
        void writeCameras();
        void writeGlobalSettings(bool writeActivePath);
        void writePaths();
        void writeUserDefinedSection();
        void writeMaterials();


		pugi::xml_document mRootDoc;
		pugi::xml_node mSceneNode;

        Scene::SharedPtr mpScene = nullptr;
        std::string mFilename;
        float mViewportWidth = 1024.0f;
        float mViewportHeight = 1024.0f;
        uint32_t mExportOptions = 0;
    };
}