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
#include "Framework.h"
#include "SceneMitsubaExporter.h"
#include "Graphics/Scene/SceneExportImportCommon.h"
#include "glm/detail/func_trigonometric.hpp"
#include "Utils/Platform/OS.h"
#include "Graphics/Scene/Editor/SceneEditor.h"
#include "Externals/ASSIMP/Include/assimp/Exporter.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Falcor
{
    bool SceneMitsubaExporter::saveScene(const std::string& filename, const Scene::SharedPtr& pScene, float viewportWidth, float viewportHeight, uint32_t exportOptions)
    {
        SceneMitsubaExporter exporter(filename, pScene);
        return exporter.save(viewportWidth, viewportHeight, exportOptions);
    }

    static const char* getMaterialLayerNDF(uint32_t ndf)
    {
        switch (ndf)
        {
        case NDFBeckmann:
            return "beckmann";
        case NDFGGX:
            return "ggx";
        case NDFUser:
        default:
            should_not_get_here();
            return "";
        }
    }


	template <typename T>
	pugi::xml_attribute setNodeAttr(pugi::xml_node& node, const char* attrName, const T value)
	{
		pugi::xml_attribute attr = node.append_attribute(attrName);
		attr.set_value(value);
		return attr;
	}

    template <>
    pugi::xml_attribute setNodeAttr<std::string>(pugi::xml_node& node, const char* attrName, const std::string value)
    {
        pugi::xml_attribute attr = node.append_attribute(attrName);
        attr.set_value(value.c_str());
        return attr;
    }

	pugi::xml_node addNodeWithType(pugi::xml_node& parent, const std::string type)
    {
		pugi::xml_node comments = parent.append_child(pugi::xml_node_type::node_element);
		comments.set_name(type.c_str());
		return comments;
    }

	pugi::xml_node addComments(pugi::xml_node& parent, const std::string text)
	{
		pugi::xml_node comments = parent.append_child(pugi::xml_node_type::node_comment);
		comments.set_value(text.c_str());
		return comments;
	}

    pugi::xml_node addBoolean(pugi::xml_node& parent, const std::string name, bool b)
    {
        pugi::xml_node string = addNodeWithType(parent, "boolean");
        setNodeAttr(string, "name", name);
        setNodeAttr(string, "value", b ? "true" : "false");
        return string;
    }

	pugi::xml_node addFloat(pugi::xml_node& parent, const std::string name, float v)
	{
		pugi::xml_node nd = addNodeWithType(parent, "float");
		setNodeAttr(nd, "name", name);
		setNodeAttr(nd, "value", v);
		return nd;
	}

    pugi::xml_node addInteger(pugi::xml_node& parent, const std::string name, int32_t v)
    {
        pugi::xml_node integer = addNodeWithType(parent, "integer");
        setNodeAttr(integer, "name", name);
        setNodeAttr(integer, "value", v);
        return integer;
    }

	pugi::xml_node addString(pugi::xml_node& parent, const std::string name, const std::string str)
	{
		pugi::xml_node string = addNodeWithType(parent, "string");
		setNodeAttr(string, "name", name);
		setNodeAttr(string, "value", str);
		return string;
	}

	pugi::xml_node addSpectrum(pugi::xml_node& parent, const std::string name, const glm::vec3& rgb)
	{
		pugi::xml_node spectrum = addNodeWithType(parent, "spectrum");
		setNodeAttr(spectrum, "name", name);
		setNodeAttr(spectrum, "value", std::to_string(rgb.x) + " " + std::to_string(rgb.y) + " " + std::to_string(rgb.z));
		return spectrum;
	}

    pugi::xml_node addSpectrum(pugi::xml_node& parent, const std::string name, const float v)
    {
        pugi::xml_node spectrum = addNodeWithType(parent, "spectrum");
        setNodeAttr(spectrum, "name", name);
        setNodeAttr(spectrum, "value", std::to_string(v));
        return spectrum;
    }

	pugi::xml_node addPoint(pugi::xml_node& parent, const std::string name, const glm::vec3& pos)
	{
		pugi::xml_node point = addNodeWithType(parent, "point");
		setNodeAttr(point, "name", name);
		setNodeAttr(point, "x", pos.x);
		setNodeAttr(point, "y", pos.y);
		setNodeAttr(point, "z", pos.z);
		return point;
	}

	pugi::xml_node addVector(pugi::xml_node& parent, const std::string name, const glm::vec3& vec)
	{
		pugi::xml_node vector = addNodeWithType(parent, "vector");
		setNodeAttr(vector, "name", name);
		setNodeAttr(vector, "x", vec.x);
		setNodeAttr(vector, "y", vec.y);
		setNodeAttr(vector, "z", vec.z);
		return vector;
	}

    pugi::xml_node addTransform(pugi::xml_node& parent,
                                const std::string name,
                                const glm::vec3& origin,
                                const glm::vec3& target,
                                const glm::vec3& up)
    {
		pugi::xml_node transform = addNodeWithType(parent, "transform");
		setNodeAttr(transform, "name", name);

		pugi::xml_node lookat = addNodeWithType(transform, "lookat");
		setNodeAttr(lookat, "origin", std::to_string(origin.x) + ", " + std::to_string(origin.y) + ", " + std::to_string(origin.z));
		setNodeAttr(lookat, "target", std::to_string(target.x) + ", " + std::to_string(target.y) + ", " + std::to_string(target.z));
		setNodeAttr(lookat, "up", std::to_string(up.x) + ", " + std::to_string(up.y) + ", " + std::to_string(up.z));

		return transform;
    }

    pugi::xml_node addTransformWithMatrix(pugi::xml_node& parent,
                                          const std::string name,
                                          const glm::mat4x4& transformMatrix)
    {
        pugi::xml_node transform = addNodeWithType(parent, "transform");
        setNodeAttr(transform, "name", name);

        pugi::xml_node matrix = addNodeWithType(transform, "matrix");

        std::string valueStr;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                valueStr += std::to_string(transformMatrix[j][i]) + " ";
            }
        }
        setNodeAttr(matrix, "value", valueStr);

        return transform;
    }

	pugi::xml_node addScene(pugi::xml_node& parent)
	{
		pugi::xml_node scene = addNodeWithType(parent, "scene");
		setNodeAttr(scene, "version", "0.6.0");
		return scene;
	}

	pugi::xml_node addIntegrator(pugi::xml_node& parent)
	{
		pugi::xml_node ingetrator = addNodeWithType(parent, "integrator");
		setNodeAttr(ingetrator, "id", "integrator");
		setNodeAttr(ingetrator, "type", "path");
		return ingetrator;
	}

    pugi::xml_node addSampler(pugi::xml_node& parent)
    {
        pugi::xml_node sampler = addNodeWithType(parent, "sampler");
        setNodeAttr(sampler, "type", "independent");
        addInteger(sampler, "sampleCount", 64);
        return sampler;
    }

    pugi::xml_node addFilm(pugi::xml_node& parent, int32_t width, int32_t height)
    {
        pugi::xml_node sampler = addNodeWithType(parent, "film");
        setNodeAttr(sampler, "type", "hdrfilm");
        addInteger(sampler, "width", width);
        addInteger(sampler, "height", height);
        return sampler;
    }

    bool SceneMitsubaExporter::save(float viewportWidth, float viewportHeight, uint32_t exportOptions)
    {
        mViewportWidth = viewportWidth;
        mViewportHeight = viewportHeight;

        mExportOptions = exportOptions;

		mRootDoc.reset();

		addComments(mRootDoc, "\nAutomatic exported from Falcor\n");
		mSceneNode = addScene(mRootDoc);

		addIntegrator(mSceneNode);

        // Write everything else
        bool exportPaths = (exportOptions & ExportPaths) != 0;
        if (exportOptions & ExportGlobalSettings)    writeGlobalSettings(exportPaths);
        if (exportOptions & ExportModels)            writeModels();
        if (exportOptions & ExportLights)            writeLights();
        if (exportOptions & ExportCameras)           writeCameras();
        if (exportOptions & ExportUserDefined)       writeUserDefinedSection();
        if (exportOptions & ExportPaths)             writePaths();
        if (exportOptions & ExportMaterials)         writeMaterials();

		mRootDoc.save_file(mFilename.c_str(), PUGIXML_TEXT("    "), pugi::format_default, pugi::xml_encoding::encoding_utf8);

        return true;
    }

    void SceneMitsubaExporter::writeGlobalSettings(bool writeActivePath)
    {
		// TODO
    }

    //bool createMaterialOverrideValue(const Model* pModel, const MaterialHistory::SharedPtr& pMatHistory, const std::unordered_map<const Material*, uint32_t>& matIDLookup, rapidjson::Document::AllocatorType& allocator, rapidjson::Value& jOverrideArray)
    //{
    //    bool overridesExist = false;

    //    for (uint32_t i = 0; i < pModel->getMeshCount(); i++)
    //    {
    //        const Mesh* pMesh = pModel->getMesh(i).get();

    //        if (pMatHistory->hasOverride(pMesh))
    //        {
    //            // Mesh's material should be found in scene
    //            assert(matIDLookup.count(pMesh->getMaterial().get()) > 0);

    //            rapidjson::Value matValue(rapidjson::kObjectType);
    //            addLiteral(matValue, allocator, SceneKeys::kMeshID, i);
    //            addLiteral(matValue, allocator, SceneKeys::kMaterialID, matIDLookup.at(pMesh->getMaterial().get()));

    //            jOverrideArray.PushBack(matValue, allocator);

    //            overridesExist = true;
    //        }
    //    }

    //    return overridesExist;
    //}

    pugi::xml_node addCoatingLayer(const Material::Layer& layer, pugi::xml_node& parent)
    {
        assert(layer.type == Material::Layer::Type::Dielectric);
        assert(layer.blend == Material::Layer::Blend::Fresnel);

        if (layer.roughness.x == 0.0f)
        {
            pugi::xml_node coating = addNodeWithType(parent, "bsdf");
            setNodeAttr(coating, "type", "coating");

            addSpectrum(coating, "intIOR", layer.extraParam.x);

            if (layer.pTexture != nullptr)
            {
                pugi::xml_node specularReflectance = addNodeWithType(coating, "texture");
                setNodeAttr(specularReflectance, "type", "bitmap");
                setNodeAttr(specularReflectance, "name", "specularReflectance");
                addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
                addFloat(specularReflectance, "gamma", -1);
            }
            else
            {
                addSpectrum(coating, "specularReflectance", layer.albedo);
            }
            return coating;
        }
        else
        {
            pugi::xml_node roughcoating = addNodeWithType(parent, "bsdf");
            setNodeAttr(roughcoating, "type", "roughcoating");

            addString(roughcoating, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));

            addSpectrum(roughcoating, "intIOR", layer.extraParam.x);

            if (layer.pTexture != nullptr)
            {
#if 0
                pugi::xml_node alpha = addNodeWithType(roughcoating, "texture");
                setNodeAttr(alpha, "type", "bitmap");
                setNodeAttr(alpha, "name", "alpha");
                addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
                addFloat(alpha, "gamma", -1);
                addString(alpha, "channel", "a");
#else
                addFloat(roughcoating, "alpha", layer.roughness.x);
#endif

                pugi::xml_node specularReflectance = addNodeWithType(roughcoating, "texture");
                setNodeAttr(specularReflectance, "type", "bitmap");
                setNodeAttr(specularReflectance, "name", "specularReflectance");
                addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
                addFloat(specularReflectance, "gamma", -1);
            }
            else
            {
                addFloat(roughcoating, "alpha", layer.roughness.x);
                addSpectrum(roughcoating, "specularReflectance", layer.albedo);
            }
            return roughcoating;
        }
    }

	pugi::xml_node addSingleLayer(const Material::Layer& layer, pugi::xml_node& parent)
    {
		switch (layer.type)
		{
		case Material::Layer::Type::Lambert:
		{
			pugi::xml_node diffuse = addNodeWithType(parent, "bsdf");
			setNodeAttr(diffuse, "type", "diffuse");
			if (layer.pTexture != nullptr)
			{
				pugi::xml_node texture = addNodeWithType(diffuse, "texture");
				setNodeAttr(texture, "type", "bitmap");
				setNodeAttr(texture, "name", "reflectance");
				addString(texture, "filename", layer.pTexture->getAbsoluteSourceFilename());
				addFloat(texture, "gamma", -1);
			}
			else
			{
				pugi::xml_node spectrunm = addSpectrum(diffuse, "reflectance", layer.albedo);
			}
			return diffuse;
		}
		case Material::Layer::Type::Conductor:
		{
			if (layer.roughness.x == 0.0f)
			{
				pugi::xml_node conductor = addNodeWithType(parent, "bsdf");
				setNodeAttr(conductor, "type", "conductor");

                addSpectrum(conductor, "eta", layer.extraParam.x);
                addSpectrum(conductor, "k", layer.extraParam.y);

				if (layer.pTexture != nullptr)
				{
					pugi::xml_node specularReflectance = addNodeWithType(conductor, "texture");
					setNodeAttr(specularReflectance, "type", "bitmap");
					setNodeAttr(specularReflectance, "name", "specularReflectance");
					addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
					addFloat(specularReflectance, "gamma", -1);
				}
				else
				{
					pugi::xml_node spectrunm = addSpectrum(conductor, "specularReflectance", layer.albedo);
				}
				return conductor;
			}
			else
			{
                pugi::xml_node roughconductor = addNodeWithType(parent, "bsdf");
                setNodeAttr(roughconductor, "type", "roughconductor");

                addString(roughconductor, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));

                addSpectrum(roughconductor, "eta", layer.extraParam.x);
                addSpectrum(roughconductor, "k", layer.extraParam.y);

                if (layer.pTexture != nullptr)
                {
#if 0
                    pugi::xml_node alpha = addNodeWithType(roughconductor, "texture");
                    setNodeAttr(alpha, "type", "bitmap");
                    setNodeAttr(alpha, "name", "alpha");
                    addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
                    addFloat(alpha, "gamma", -1);
                    addString(alpha, "channel", "a");
#else
                    addFloat(roughconductor, "alpha", layer.roughness.x);
#endif

                    pugi::xml_node specularReflectance = addNodeWithType(roughconductor, "texture");
                    setNodeAttr(specularReflectance, "type", "bitmap");
                    setNodeAttr(specularReflectance, "name", "specularReflectance");
                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
                    addFloat(specularReflectance, "gamma", -1);
                }
                else
                {
                    addFloat(roughconductor, "alpha", layer.roughness.x);
                    addSpectrum(roughconductor, "specularReflectance", layer.albedo);
                }
                return roughconductor;
			}
		}
		case Material::Layer::Type::Dielectric:
		{
            if (layer.roughness.x == 0.0f)
            {
                pugi::xml_node dielectric = addNodeWithType(parent, "bsdf");
                setNodeAttr(dielectric, "type", "dielectric");

                addSpectrum(dielectric, "intIOR", layer.extraParam.x);

                if (layer.pTexture != nullptr)
                {
                    pugi::xml_node specularReflectance = addNodeWithType(dielectric, "texture");
                    setNodeAttr(specularReflectance, "type", "bitmap");
                    setNodeAttr(specularReflectance, "name", "specularReflectance");
                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
                    addFloat(specularReflectance, "gamma", -1);
                }
                else
                {
                    addSpectrum(dielectric, "specularReflectance", layer.albedo);
                }
                return dielectric;
            }
            else
            {
                pugi::xml_node roughdielectric = addNodeWithType(parent, "bsdf");
                setNodeAttr(roughdielectric, "type", "roughdielectric");

                addString(roughdielectric, "distribution", getMaterialLayerNDF((uint32_t)layer.ndf));

                addSpectrum(roughdielectric, "intIOR", layer.extraParam.x);

                if (layer.pTexture != nullptr)
                {
#if 0
                    pugi::xml_node alpha = addNodeWithType(roughdielectric, "texture");
                    setNodeAttr(alpha, "type", "bitmap");
                    setNodeAttr(alpha, "name", "alpha");
                    addString(alpha, "filename", layer.pTexture->getAbsoluteSourceFilename());
                    addFloat(alpha, "gamma", -1);
                    addString(alpha, "channel", "a");
#else
                    addFloat(roughdielectric, "alpha", layer.roughness.x);
#endif

                    pugi::xml_node specularReflectance = addNodeWithType(roughdielectric, "texture");
                    setNodeAttr(specularReflectance, "type", "bitmap");
                    setNodeAttr(specularReflectance, "name", "specularReflectance");
                    addString(specularReflectance, "filename", layer.pTexture->getAbsoluteSourceFilename());
                    addFloat(specularReflectance, "gamma", -1);
                }
                else
                {
                    addFloat(roughdielectric, "alpha", layer.roughness.x);
                    addSpectrum(roughdielectric, "specularReflectance", layer.albedo);
                }
                return roughdielectric;
            }
		}

		case Material::Layer::Type::Emissive:
		case Material::Layer::Type::User:
		default:
		{
			assert(false);
			break;
		}
		}

		return pugi::xml_node{};
    }

	void addMaterial(const Material::SharedPtr& pMat, pugi::xml_node& parent, bool overwriteByName)
    {
		pugi::xml_node curParent = parent;

		bool overwrited = false;
		auto overwriteMaterialByName = [&](pugi::xml_node xmlnode)
		{
			if (!overwrited)
			{
				overwrited = true;

				if (overwriteByName)
				{
					setNodeAttr(xmlnode, "name", pMat->getName());
				}
				else
				{
                    pugi::xml_node comments = parent.prepend_child(pugi::xml_node_type::node_comment);
                    comments.set_value(pMat->getName().c_str());
				}
			}
		};

		// modifiers
		if (pMat->getNormalMap())
		{
			pugi::xml_node normalmap = addNodeWithType(curParent, "bsdf");
			setNodeAttr(normalmap, "type", "normalmap");

			pugi::xml_node texture = addNodeWithType(normalmap, "texture");
			setNodeAttr(texture, "type", "bitmap");
			addString(texture, "filename", pMat->getNormalMap()->getAbsoluteSourceFilename());

            addFloat(texture, "gamma", 1.0f);

			overwriteMaterialByName(normalmap);
			curParent = normalmap;
		}

		// layers
        uint32_t numLayers = 0;
        for (uint32_t layerIdx = 0; layerIdx < pMat->getNumLayers(); ++layerIdx)
        {
            const Material::Layer layer = pMat->getLayer(layerIdx);
            switch (layer.type)
            {
            case Material::Layer::Type::Lambert:
            case Material::Layer::Type::Conductor:
            case Material::Layer::Type::Dielectric:
                numLayers += 1;
                break;
            default:
                break;
            }
        }

        if (numLayers == 1)
        {
            for (uint32_t layerIdx = 0; layerIdx < pMat->getNumLayers(); ++layerIdx)
            {
                const Material::Layer layer = pMat->getLayer(layerIdx);
                switch (layer.type)
                {
                case Material::Layer::Type::Lambert:
                case Material::Layer::Type::Conductor:
                case Material::Layer::Type::Dielectric:
                {
                    pugi::xml_node layerNode = addSingleLayer(pMat->getLayer(layerIdx), curParent);
                    overwriteMaterialByName(layerNode);
                    break;
                }
                default:
                    break;
                }
            }
        }
        else
        {
            pugi::xml_node diffuseLayer{};
            pugi::xml_node conductorLayer{};
            pugi::xml_node dielectricLayer{};

            for (uint32_t layerIdx = 0; layerIdx < numLayers; ++layerIdx)
            {
                const Material::Layer layer = pMat->getLayer(layerIdx);
                switch (layer.type)
                {
                case Material::Layer::Type::Lambert:
                    diffuseLayer = addSingleLayer(layer, curParent);
                    break;
                case Material::Layer::Type::Conductor:
                    conductorLayer = addSingleLayer(layer, curParent);
                    break;
                case Material::Layer::Type::Dielectric:
                    dielectricLayer = addCoatingLayer(layer, curParent);
                    break;
                default:
                    break;
                }
            }

            if (!dielectricLayer.empty())
            {
                curParent = dielectricLayer;
                overwriteMaterialByName(curParent);
            }

            if (diffuseLayer.empty())
            {
                conductorLayer = dielectricLayer.append_move(conductorLayer);
            }
            else if (conductorLayer.empty())
            {
                diffuseLayer = dielectricLayer.append_move(diffuseLayer);
            }
            else
            {
                pugi::xml_node mixturebsdf = addNodeWithType(curParent, "bsdf");
                setNodeAttr(mixturebsdf, "type", "mixturebsdf");

                addBoolean(mixturebsdf, "ensureEnergyConservation", false);
                addString(mixturebsdf, "weights", "1.0, 1.0");

                diffuseLayer = mixturebsdf.append_move(diffuseLayer);
                conductorLayer = mixturebsdf.append_move(conductorLayer);

                overwriteMaterialByName(mixturebsdf);
            }
        }
    }

    void addWavefrontOBJ(const Scene::SharedPtr& pScene, uint32_t modelID, pugi::xml_node& parent)
    {
        assert(pScene->getModelInstanceCount(modelID) > 0);

        const Model* pModel = pScene->getModel(modelID).get();

        std::string shapegroupId = pModel->getName();

        assert(parent.child(shapegroupId.c_str()).empty());

        for (uint32_t i = 0; i < pScene->getModelInstanceCount(modelID); i++)
        {
            auto& pInstance = pScene->getModelInstance(modelID, i);

            pugi::xml_node obj = addNodeWithType(parent, "shape");
            setNodeAttr(obj, "type", "obj");

            addComments(obj, pInstance->getName().c_str());

            addString(obj, "filename", pModel->getAbsoluteFilename().c_str());

            addTransformWithMatrix(obj, "toWorld", pInstance->getTransformMatrix());

			const bool overwriteByName = pInstance->getObject()->getMaterialCount() > 1;
			for (uint32_t meshId = 0; meshId < pInstance->getObject()->getMeshCount(); ++meshId)
			{
				const Material::SharedPtr pMat = pInstance->getObject()->getMesh(meshId)->getMaterial();
				addMaterial(pMat, obj, overwriteByName);
			}
        }
    }

    void SceneMitsubaExporter::writeModels()
    {
        if (mpScene->getModelCount() == 0)
        {
            return;
        }

		addComments(mSceneNode, "Models");

        for (uint32_t i = 0; i < mpScene->getModelCount(); i++)
        {
            addWavefrontOBJ(mpScene, i, mSceneNode);
        }
    }

	void addSpotLight(const PointLight* pLight, pugi::xml_node& parent)
	{
		pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
		setNodeAttr(pointLight, "type", "spot");

		addComments(pointLight, pLight->getName().c_str());

		// TODO
		assert(false);

		//addVector(jsonLight, allocator, SceneKeys::kLightIntensity, pLight->getIntensity());
		//addVector(jsonLight, allocator, SceneKeys::kLightPos, pLight->getWorldPosition());
		//addVector(jsonLight, allocator, SceneKeys::kLightDirection, pLight->getWorldDirection());
		//addLiteral(jsonLight, allocator, SceneKeys::kLightOpeningAngle, glm::degrees(pLight->getOpeningAngle()));
		//addLiteral(jsonLight, allocator, SceneKeys::kLightPenumbraAngle, glm::degrees(pLight->getPenumbraAngle()));
	}

    void addPointLight(const PointLight* pLight, pugi::xml_node& parent)
    {
		assert(pLight->getOpeningAngle() >= (float)M_PI);

		pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
		setNodeAttr(pointLight, "type", "point");

		addComments(pointLight, pLight->getName().c_str());
		addSpectrum(pointLight, "intensity", pLight->getIntensity());
		addPoint(pointLight, "position", pLight->getWorldPosition());
    }

    void addDirectionalLight(const DirectionalLight* pLight, pugi::xml_node& parent)
    {
		pugi::xml_node pointLight = addNodeWithType(parent, "emitter");
		setNodeAttr(pointLight, "type", "directional");

		addComments(pointLight, pLight->getName().c_str());
		addSpectrum(pointLight, "irradiance", pLight->getIntensity());
        addVector(pointLight, "direction", pLight->getWorldDirection());
    }

    void addPunctualLight(const Scene::SharedPtr& pScene, uint32_t lightID, pugi::xml_node& parent)
    {
        const auto pLight = pScene->getLight(lightID);

		switch (pLight->getType())
		{
		case LightPoint:
		{
			PointLight* pl = (PointLight*)pLight.get();
			if (pl->getOpeningAngle() >= (float)M_PI)
			{
				addPointLight(pl, parent);
			}
			else
			{
				addSpotLight(pl, parent);
			}
			break;
		}
        case LightDirectional:
            addDirectionalLight((DirectionalLight*)pLight.get(), parent);
            break;
        default:
            should_not_get_here();
            break;
        }
    }

    void SceneMitsubaExporter::writeLights()
    {
        if (mpScene->getLightCount() == 0)
        {
            return;
        }

		addComments(mSceneNode, "Punctual light sources");

        for (uint32_t i = 0; i < mpScene->getLightCount(); i++)
        {
            if (mpScene->getLights()[i]->getType() != LightPoint &&
                mpScene->getLights()[i]->getType() != LightDirectional)
            {
                continue;
            }

            addPunctualLight(mpScene, i, mSceneNode);
        }
    }

    void addPerspectiveCamera(const Scene::SharedConstPtr& pScene, const Camera::SharedPtr pCamera, pugi::xml_node& parent, float viewportWidth, float viewportHeight)
    {
		pugi::xml_node sensor = addNodeWithType(parent, "sensor");
		setNodeAttr(sensor, "type", "perspective");

		addComments(sensor, pCamera->getName().c_str());

		addTransform(sensor, "toWorld", pCamera->getPosition(), pCamera->getTarget(), pCamera->getUpVector());

#if 1
        const float fovY = pCamera->getFocalLength() == 0.0f ? 0.0f : focalLengthToFovY(pCamera->getFocalLength(), Camera::kDefaultFrameHeight);
        addFloat(sensor, "fov", glm::degrees(fovY));
        addString(sensor, "fovAxis", "y");
#else
		std::string focalLenStr = std::to_string(pCamera->getFocalLength()) + "mm";
        addString(sensor, "focalLength", focalLenStr.c_str());
#endif

        glm::vec2 depthRange;
        depthRange[0] = pCamera->getNearPlane();
        depthRange[1] = pCamera->getFarPlane();

		addFloat(sensor, "nearClip", pCamera->getNearPlane());
		addFloat(sensor, "farClip", pCamera->getFarPlane());

        //addLiteral(jsonCamera, allocator, SceneKeys::kCamAspectRatio, pCamera->getAspectRatio());

        addSampler(sensor);
        addFilm(sensor, (int32_t)viewportWidth, (int32_t)viewportHeight);
    }

    void SceneMitsubaExporter::writeCameras()
    {
        if (mpScene->getCameraCount() == 0)
        {
            return;
        }

		addComments(mSceneNode, "Default Camera");

	    const auto pCamera = mpScene->getActiveCamera();
	    addPerspectiveCamera(mpScene, pCamera, mSceneNode, mViewportWidth, mViewportHeight);
    }

    void SceneMitsubaExporter::writePaths()
    {
        if (mpScene->getPathCount() == 0)
        {
            return;
        }

		// TODO
		assert(false);
    }

    void SceneMitsubaExporter::writeUserDefinedSection()
    {
        if (mpScene->getUserVariableCount() == 0)
        {
            return;
        }

		// TODO
		assert(false);
    }

    static const char* getMaterialLayerType(uint32_t type)
    {
        switch (type)
        {
        case MatLambert:
            return SceneKeys::kMaterialLambert;
        case MatConductor:
            return SceneKeys::kMaterialConductor;
        case MatDielectric:
            return SceneKeys::kMaterialDielectric;
        case MatEmissive:
            return SceneKeys::kMaterialEmissive;
        case MatUser:
            return SceneKeys::kMaterialUser;
        default:
            should_not_get_here();
            return "";
        }
    }

    static const char* getMaterialLayerBlending(uint32_t blend)
    {
        switch (blend)
        {
        case BlendFresnel:
            return SceneKeys::kMaterialBlendFresnel;
        case BlendConstant:
            return SceneKeys::kMaterialBlendConstant;
        case BlendAdd:
            return SceneKeys::kMaterialBlendAdd;
        default:
            should_not_get_here();
            return "";
        }
    }

    //void createMaterialTextureValue(const Texture::SharedPtr& pTexture, rapidjson::Value& jsonVal, rapidjson::Document::AllocatorType& allocator)
    //{
    //    if (pTexture)
    //    {
    //        std::string filename = stripDataDirectories(pTexture->getAbsoluteSourceFilename());
    //        addString(jsonVal, allocator, SceneKeys::kMaterialTexture, filename);
    //    }
    //}

    //void createMaterialLayer(const Material::Layer& layer, rapidjson::Value& jsonVal, rapidjson::Document::AllocatorType& allocator)
    //{
    //    jsonVal.SetObject();

    //    if (layer.pTexture != nullptr)
    //    {
    //        addString(jsonVal, allocator, SceneKeys::kMaterialTexture, stripDataDirectories(layer.pTexture->getAbsoluteSourceFilename()));
    //    }

    //    addString(jsonVal, allocator, SceneKeys::kMaterialLayerType, getMaterialLayerType((uint32_t)layer.type));
    //    addString(jsonVal, allocator, SceneKeys::kMaterialNDF, getMaterialLayerNDF((uint32_t)layer.ndf));
    //    addString(jsonVal, allocator, SceneKeys::kMaterialBlend, getMaterialLayerBlending((uint32_t)layer.blend));

    //    addVector(jsonVal, allocator, SceneKeys::kMaterialAlbedo, layer.albedo);
    //    addVector(jsonVal, allocator, SceneKeys::kMaterialRoughness, layer.roughness);
    //    addVector(jsonVal, allocator, SceneKeys::kMaterialExtraParam, layer.extraParam);
    //}

    //void createMaterialValue(const Material* pMaterial, rapidjson::Value& jsonMaterial, rapidjson::Document::AllocatorType& allocator)
    //{
    //    // Name
    //    addString(jsonMaterial, allocator, SceneKeys::kName, pMaterial->getName());

    //    // ID
    //    addLiteral(jsonMaterial, allocator, SceneKeys::kID, pMaterial->getId());

    //    // Double-Sided
    //    addBool(jsonMaterial, allocator, SceneKeys::kMaterialDoubleSided, pMaterial->isDoubleSided());

    //    // Alpha layer
    //    auto pAlphaMap = pMaterial->getAlphaMap();
    //    if (pAlphaMap != nullptr)
    //    {
    //        addString(jsonMaterial, allocator, SceneKeys::kMaterialAlpha, stripDataDirectories(pAlphaMap->getAbsoluteSourceFilename()));
    //    }

    //    // Normal
    //    auto pNormalMap = pMaterial->getNormalMap();
    //    if (pNormalMap != nullptr)
    //    {
    //        addString(jsonMaterial, allocator, SceneKeys::kMaterialNormal, stripDataDirectories(pNormalMap->getAbsoluteSourceFilename()));
    //    }

    //    // Height
    //    auto pHeightMap = pMaterial->getHeightMap();
    //    if (pHeightMap != nullptr)
    //    {
    //        addString(jsonMaterial, allocator, SceneKeys::kMaterialHeight, stripDataDirectories(pHeightMap->getAbsoluteSourceFilename()));
    //    }

    //    // Ambient Occlusion
    //    auto pAOMap = pMaterial->getAmbientOcclusionMap();
    //    if (pAOMap != nullptr)
    //    {
    //        addString(jsonMaterial, allocator, SceneKeys::kMaterialAO, stripDataDirectories(pAOMap->getAbsoluteSourceFilename()));
    //    }

    //    // Loop over the layers
    //    if (pMaterial->getNumLayers() > 0)
    //    {
    //        rapidjson::Value jsonLayerArray(rapidjson::kArrayType);
    //        for (uint32_t i = 0; i < pMaterial->getNumLayers(); i++)
    //        {
    //            Material::Layer layer = pMaterial->getLayer(i);

    //            rapidjson::Value jsonLayer;
    //            createMaterialLayer(layer, jsonLayer, allocator);
    //            jsonLayerArray.PushBack(jsonLayer, allocator);
    //        }

    //        addJsonValue(jsonMaterial, allocator, SceneKeys::kMaterialLayers, jsonLayerArray);
    //    }
    //}

    void SceneMitsubaExporter::writeMaterials()
    {
        if (mpScene->getMaterialCount() == 0)
        {
            return;
        }

        //auto& allocator = mJDoc.GetAllocator();
        //rapidjson::Value jsonMaterialArray(rapidjson::kArrayType);

        //for (uint32_t i = 0; i < mpScene->getMaterialCount(); i++)
        //{
        //    const auto pMaterial = mpScene->getMaterial(i);
        //    rapidjson::Value jsonMaterial(rapidjson::kObjectType);
        //    createMaterialValue(pMaterial.get(), jsonMaterial, allocator);
        //    jsonMaterialArray.PushBack(jsonMaterial, allocator);
        //}

        //addJsonValue(mJDoc, allocator, SceneKeys::kMaterials, jsonMaterialArray);
    }
}
