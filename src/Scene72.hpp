#pragma once
#include "fwd.hpp"

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <functional>
#include <cmath>

#include <vulkan/vulkan.h>
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include "Culling.hpp"

namespace s72 {

	class Object;
	class Camera;
	class PerspectiveCamera;
	class OrthographicCamera;
	class Mesh;
	class Node;
	class Scene;
	class Driver;

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
		virtual ~Camera(void) {}
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

	class Mesh : public Object {
	public:
		using Ptr = std::shared_ptr<Mesh>;
		using WeakPtr = std::weak_ptr<Mesh>;
		VkPrimitiveTopology topology;
		std::uint32_t count;
		VkBuffer vertexBuffer;
		jjyou::vk::Memory vertexBufferMemory;
		BBox bbox;
		Mesh(
			std::uint32_t idx,
			const std::string& name,
			VkPrimitiveTopology topology,
			std::uint32_t count,
			VkBuffer vertexBuffer,
			jjyou::vk::Memory vertexBufferMemory, 
			const BBox& bbox
		) : Object(idx, "MESH", name), topology(topology), count(count), vertexBuffer(vertexBuffer), vertexBufferMemory(vertexBufferMemory), bbox(bbox)
		{}
		virtual ~Mesh(void) {}
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
			const std::array<std::weak_ptr<Driver>, 3>& drivers
		) : Object(idx, "NODE", name), translation(translation), rotation(rotation), scale(scale), children(children), camera(camera), mesh(mesh), drivers(drivers)
		{}
		virtual ~Node(void) {}
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
		) : Object(idx, "NODE", name), roots(roots) {}
		virtual ~Scene(void) {}
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
		virtual ~Driver(void) {}
	};

	class Scene72 {

	public:
		using Ptr = std::shared_ptr<Scene72>;
		std::unordered_map<std::string, Camera::Ptr> cameras;
		std::unordered_map<std::string, Mesh::Ptr> meshes;
		std::vector<Driver::Ptr> drivers;
		Scene::Ptr scene;
		std::vector<Object::Ptr> graph;
		float minTime = 0.0f;
		float maxTime = 0.0f;
		// Clear all stored information.
		// Note: you have to manually destroy the vertex buffers and memories
		void clear(void) {
			this->cameras.clear();
			this->meshes.clear();
			this->drivers.clear();
			this->scene.reset();
			this->graph.clear();
			this->minTime = this->maxTime = 0.0f;
		}
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

	private:
		bool _traverse(
			s72::Node::Ptr node,
			const jjyou::glsl::mat4& parentTransform,
			const std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)>& visit
		);
	};

}