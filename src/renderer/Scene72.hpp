#pragma once
#include "fwd.hpp"

#include <unordered_map>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <iostream>
#include <functional>
#include <cmath>

#include <vulkan/vulkan.h>
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include "Engine.hpp"
#include "Culling.hpp"

namespace s72 {

	class Object {
	public:
		using Ptr = std::shared_ptr<Object>;
		using WeakPtr = std::weak_ptr<Object>;
		Object(std::uint32_t idx, const std::string& type, const std::string& name) : idx(idx), type(type), name(name) {}
		virtual ~Object(void) {}
		std::string type;
		std::string name;
		std::uint32_t idx;
	};

	class Camera : public Object {
	public:
		using Ptr = std::shared_ptr<Camera>;
		using WeakPtr = std::weak_ptr<Camera>;
		Camera(std::uint32_t idx, const std::string& name) : Object(idx, "CAMERA", name) {}
		virtual ~Camera(void) override {}
		virtual float getAspectRatio(void) const = 0;
		virtual jjyou::glsl::mat4 getProjectionMatrix(void) const = 0;
	};

	class PerspectiveCamera : public Camera {
	public:
		using Ptr = std::shared_ptr<PerspectiveCamera>;
		using WeakPtr = std::weak_ptr<PerspectiveCamera>;
		PerspectiveCamera(
			std::uint32_t idx,
			const std::string& name,
			float yFov,
			float aspectRatio,
			float zNear,
			float zFar
		)
			: Camera(idx, name), yFov(yFov), aspectRatio(aspectRatio), zNear(zNear), zFar(zFar)
		{}
		virtual ~PerspectiveCamera(void) override {}
		virtual float getAspectRatio(void) const override {
			return this->aspectRatio;
		}
		virtual jjyou::glsl::mat4 getProjectionMatrix(void) const override {
			return jjyou::glsl::perspective(this->yFov, this->aspectRatio, this->zNear, this->zFar);
		}
	private:
		float yFov;
		float aspectRatio;
		float zNear;
		float zFar;
	};

	class OrthographicCamera : public Camera {
	public:
		using Ptr = std::shared_ptr<OrthographicCamera>;
		using WeakPtr = std::weak_ptr<OrthographicCamera>;
		OrthographicCamera(
			std::uint32_t idx,
			const std::string& name,
			float left,
			float right,
			float bottom,
			float top,
			float zNear,
			float zFar)
			: Camera(idx, name), left(left), right(right), bottom(bottom), top(top), zNear(zNear), zFar(zFar)
		{}
		virtual ~OrthographicCamera(void) override {}
		virtual float getAspectRatio(void) const override {
			return 0.0f;
		}
		virtual jjyou::glsl::mat4 getProjectionMatrix(void) const override {
			return jjyou::glsl::mat4();
		}
	private:
		float left;
		float right;
		float bottom;
		float top;
		float zNear;
		float zFar;
	};

	class Material : public Object {
	public:
		using Ptr = std::shared_ptr<Material>;
		using WeakPtr = std::weak_ptr<Material>;
		Material(
			std::uint32_t idx,
			const std::string& name,
			const std::string& materialType
		) : Object(idx, "MATERIAL", name), materialType(materialType) {}
		virtual ~Material(void) override {}
		virtual std::uint32_t numTextures(void) const = 0;
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const = 0;
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) = 0;
		std::string materialType;
	};

	class SimpleMaterial : public Material {
	public:
		using Ptr = std::shared_ptr<SimpleMaterial>;
		using WeakPtr = std::weak_ptr<SimpleMaterial>;
		SimpleMaterial(
			std::uint32_t idx,
			const std::string& name
		) : Material(idx, name, "simple") {}
		virtual ~SimpleMaterial(void) override {}
		virtual std::uint32_t numTextures(void) const override { return 0; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const override { throw std::runtime_error("Simple material has no textures."); }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) override { throw std::runtime_error("Simple material has no textures."); }
	};

	class EnvironmentMaterial : public Material {
	public:
		using Ptr = std::shared_ptr<EnvironmentMaterial>;
		using WeakPtr = std::weak_ptr<EnvironmentMaterial>;
		EnvironmentMaterial(
			std::uint32_t idx,
			const std::string& name,
			jjyou::vk::Texture2D normalMap,
			jjyou::vk::Texture2D displacementMap
		) : Material(idx, name, "environment"), normalMap(normalMap), displacementMap(displacementMap) {}
		virtual ~EnvironmentMaterial(void) override {}
		virtual std::uint32_t numTextures(void) const override { return 2; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; default: throw std::runtime_error("Environment material has exactly 2 textures."); } }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; default: throw std::runtime_error("Environment material has exactly 2 textures."); } }
		jjyou::vk::Texture2D normalMap;
		jjyou::vk::Texture2D displacementMap;
	};

	class MirrorMaterial : public Material {
	public:
		using Ptr = std::shared_ptr<MirrorMaterial>;
		using WeakPtr = std::weak_ptr<MirrorMaterial>;
		MirrorMaterial(
			std::uint32_t idx,
			const std::string& name,
			jjyou::vk::Texture2D normalMap,
			jjyou::vk::Texture2D displacementMap
		) : Material(idx, name, "mirror"), normalMap(normalMap), displacementMap(displacementMap) {}
		virtual ~MirrorMaterial(void) override {}
		virtual std::uint32_t numTextures(void) const override { return 2; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; default: throw std::runtime_error("Mirror material has exactly 2 textures."); } }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; default: throw std::runtime_error("Mirror material has exactly 2 textures."); } }
		jjyou::vk::Texture2D normalMap;
		jjyou::vk::Texture2D displacementMap;
	};

	class LambertianMaterial : public Material {
	public:
		using Ptr = std::shared_ptr<LambertianMaterial>;
		using WeakPtr = std::weak_ptr<LambertianMaterial>;
		LambertianMaterial(
			std::uint32_t idx,
			const std::string& name,
			jjyou::vk::Texture2D normalMap,
			jjyou::vk::Texture2D displacementMap,
			jjyou::vk::Texture2D albedo
		) : Material(idx, name, "lambertian"), normalMap(normalMap), displacementMap(displacementMap), albedo(albedo) {}
		virtual ~LambertianMaterial(void) override {}
		virtual std::uint32_t numTextures(void) const override { return 3; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; case 2: return this->albedo; default: throw std::runtime_error("Lambertian material has exactly 3 textures."); } }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; case 2: return this->albedo; default: throw std::runtime_error("Lambertian material has exactly 3 textures."); } }
		jjyou::vk::Texture2D normalMap;
		jjyou::vk::Texture2D displacementMap;
		jjyou::vk::Texture2D albedo;
	};

	class PbrMaterial : public Material {
	public:
		using Ptr = std::shared_ptr<PbrMaterial>;
		using WeakPtr = std::weak_ptr<PbrMaterial>;
		PbrMaterial(
			std::uint32_t idx,
			const std::string& name,
			jjyou::vk::Texture2D normalMap,
			jjyou::vk::Texture2D displacementMap,
			jjyou::vk::Texture2D albedo,
			jjyou::vk::Texture2D roughness,
			jjyou::vk::Texture2D metalness
		) : Material(idx, name, "pbr"), normalMap(normalMap), displacementMap(displacementMap), albedo(albedo), roughness(roughness), metalness(metalness) {}
		virtual ~PbrMaterial(void) override {}
		virtual std::uint32_t numTextures(void) const override { return 5; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; case 2: return this->albedo; case 3: return this->roughness; case 4: return this->metalness; default: throw std::runtime_error("Pbr material has exactly 5 textures."); } }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) override { switch (idx) { case 0:return this->normalMap; case 1:return this->displacementMap; case 2: return this->albedo; case 3: return this->roughness; case 4: return this->metalness; default: throw std::runtime_error("Pbr material has exactly 5 textures."); } }
		jjyou::vk::Texture2D normalMap;
		jjyou::vk::Texture2D displacementMap;
		jjyou::vk::Texture2D albedo;
		jjyou::vk::Texture2D roughness;
		jjyou::vk::Texture2D metalness;
	};

	class Environment : public Object {
	public:
		using Ptr = std::shared_ptr<Environment>;
		using WeakPtr = std::weak_ptr<Environment>;
		Environment(
			std::uint32_t idx,
			const std::string& name,
			jjyou::vk::Texture2D radiance,
			jjyou::vk::Texture2D lambertian,
			jjyou::vk::Texture2D environmentBRDF
		) : Object(idx, "ENVIRONMENT", name), radiance(radiance), lambertian(lambertian), environmentBRDF(environmentBRDF){}
		virtual ~Environment(void) override {}
		virtual std::uint32_t numTextures(void) const { return 3; }
		virtual const jjyou::vk::Texture2D& texture(std::uint32_t idx) const { switch (idx) { case 0:return this->radiance; case 1:return this->lambertian; case 2:return this->environmentBRDF; default: throw std::runtime_error("Environment has exactly 3 textures."); } }
		virtual jjyou::vk::Texture2D& texture(std::uint32_t idx) { switch (idx) { case 0:return this->radiance; case 1:return this->lambertian; case 2:return this->environmentBRDF; default: throw std::runtime_error("Environment has exactly 3 textures."); } }
		jjyou::vk::Texture2D radiance;
		jjyou::vk::Texture2D lambertian;
		jjyou::vk::Texture2D environmentBRDF;
	};

	class Mesh : public Object {
	public:
		using Ptr = std::shared_ptr<Mesh>;
		using WeakPtr = std::weak_ptr<Mesh>;
		VkPrimitiveTopology topology;
		std::uint32_t count;
		VkBuffer vertexBuffer;
		jjyou::vk::Memory vertexBufferMemory;
		Material::WeakPtr material;
		BBox bbox;
		Mesh(
			std::uint32_t idx,
			const std::string& name,
			VkPrimitiveTopology topology,
			std::uint32_t count,
			VkBuffer vertexBuffer,
			jjyou::vk::Memory vertexBufferMemory,
			Material::WeakPtr material,
			const BBox& bbox
		) : Object(idx, "MESH", name), topology(topology), count(count), vertexBuffer(vertexBuffer), vertexBufferMemory(vertexBufferMemory), material(material), bbox(bbox)
		{}
		virtual ~Mesh(void) override {}
	};

	class Node : public Object {
	public:
		using Ptr = std::shared_ptr<Node>;
		using WeakPtr = std::weak_ptr<Node>;
		jjyou::glsl::vec3 translation;
		jjyou::glsl::quat rotation;
		jjyou::glsl::vec3 scale;
		std::vector<Node::WeakPtr> children;
		Camera::WeakPtr camera;
		Mesh::WeakPtr mesh;
		Environment::WeakPtr environment;
		std::array<std::weak_ptr<Driver>, 3> drivers;
		Node(
			std::uint32_t idx,
			const std::string& name,
			const jjyou::glsl::vec3& translation,
			const jjyou::glsl::quat& rotation,
			const jjyou::glsl::vec3& scale,
			const std::vector<Node::WeakPtr>& children,
			Camera::WeakPtr camera,
			Mesh::WeakPtr mesh,
			Environment::WeakPtr environment,
			const std::array<std::weak_ptr<Driver>, 3>& drivers
		) : Object(idx, "NODE", name), translation(translation), rotation(rotation), scale(scale), children(children), camera(camera), mesh(mesh), environment(environment), drivers(drivers)
		{}
		virtual ~Node(void) override {}
	};

	class Scene : public Object {
	public:
		using Ptr = std::shared_ptr<Scene>;
		using WeakPtr = std::weak_ptr<Scene>;
		std::vector<Node::WeakPtr> roots;
		Scene(
			std::uint32_t idx,
			const std::string& name,
			const std::vector<Node::WeakPtr>& roots
		) : Object(idx, "SCENE", name), roots(roots) {}
		virtual ~Scene(void) override {}
	};
	

	class Driver : public Object {
	public:
		using Ptr = std::shared_ptr<Driver>;
		using WeakPtr = std::weak_ptr<Driver>;
		enum Channel {
			Translation = 0,
			Scale = 1,
			Rotation = 2
		};
		Node::WeakPtr node;
		Channel channel;
		std::vector<float> times;
		std::vector<float> values;
		enum Interpolation {
			Step = 0,
			Linear = 1,
			Slerp = 2
		};
		Interpolation interpolation;
		int timeIter = 0;
		Driver(
			std::uint32_t idx,
			const std::string& name,
			Node::WeakPtr node,
			Channel channel,
			const std::vector<float>& times,
			const std::vector<float>& values,
			Interpolation interpolation
		) : Object(idx, "DRIVER", name), node(node), channel(channel), times(times), values(values), interpolation(interpolation)
		{}
		virtual ~Driver(void) override {}
	};

	class Scene72 {

	public:
		using Ptr = std::shared_ptr<Scene72>;
		std::unordered_map<std::string, Camera::Ptr> cameras;
		std::unordered_map<std::string, Mesh::Ptr> meshes;
		std::vector<Driver::Ptr> drivers;
		Scene::Ptr scene;
		Environment::Ptr environment;

		SimpleMaterial::Ptr defaultMaterial;

		std::vector<Object::Ptr> graph;
		float minTime = 0.0f;
		float maxTime = 0.0f;

		// Reset timestamp related variables
		void reset(void) {
			for (auto& driver : this->drivers) {
					driver->timeIter = 0;
			}
		}

		// Traverse the scene graph
		float currPlayTime = 0.0f;
		bool traverse(
			float playTime,
			jjyou::glsl::mat4 rootTransform,
			const std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)>& visit
		);

		// Shader descriptors and uniforms
		struct FrameDescriptorSets {
			VkDescriptorSet viewLevelUniformDescriptorSet = nullptr;
			VkBuffer viewLevelUniformBuffer = nullptr;
			jjyou::vk::Memory viewLevelUniformBufferMemory{};

			VkDescriptorSet objectLevelUniformDescriptorSet = nullptr;
			VkBuffer objectLevelUniformBuffer = nullptr;
			jjyou::vk::Memory objectLevelUniformBufferMemory{};

			std::map<std::uint32_t, VkDescriptorSet> materialLevelUniformDescriptorSets{};

			VkBuffer skyboxUniformBuffer = nullptr;
			jjyou::vk::Memory skyboxUniformBufferMemory{};
			VkDescriptorSet skyboxUniformDescriptorSet = nullptr;
		};
		std::array<FrameDescriptorSets, Engine::MAX_FRAMES_IN_FLIGHT> frameDescriptorSets{};
		VkDescriptorPool descriptorPool = nullptr;
		

	private:
		bool _traverse(
			s72::Node::Ptr node,
			const jjyou::glsl::mat4& parentTransform,
			const std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)>& visit
		);
	};

}