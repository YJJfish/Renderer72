#include "Scene72.hpp"
#include "Engine.hpp"

template <class V>
inline V interpolate(s72::Driver::Interpolation interpolation, float begT, float endT, float currT, const V& begV, const V& endV) {
	if (interpolation == s72::Driver::Interpolation::Step)
		return begV;
	else if (interpolation == s72::Driver::Interpolation::Linear) {
		float u = (currT - begT) / (endT - begT);
		return (1.0f - u) * begV + u * endV;
	}
	else if (interpolation == s72::Driver::Interpolation::Slerp) {
		float u = (currT - begT) / (endT - begT);
		float cosTheta = 0.0f;
		for (int i = 0; i < V::length; ++i)
			cosTheta += begV[i] * endV[i];
		float theta = std::acos(cosTheta);
		float sinTheta = std::sin(theta);
		return std::sin((1.0f - u) * theta) / sinTheta * begV + std::sin(u * theta) / sinTheta * endV;
	}
	return V{};
}

s72::Scene72::Ptr Engine::load(
	const jjyou::io::Json<>& json,
	const std::filesystem::path& baseDir
) {
	s72::Scene72::Ptr pScene72(new s72::Scene72);
	s72::Scene72& scene72 = *pScene72;
	scene72.minTime = std::numeric_limits<float>::max();
	scene72.maxTime = std::numeric_limits<float>::min();
	if (json[0].string() != "s72-v1") {
		this->destroy(scene72);
		throw std::runtime_error("Scene72 file must start with \"s72-v1\"");
	}
	for (int i = 1; i < json.size(); ++i) {
		const auto& obj = json[i];
		std::string type = obj["type"].string();
		std::string name = obj["name"].string();
		if (type == "SCENE") {
			if (scene72.scene) {
				this->destroy(scene72);
				throw std::runtime_error("Scene must be unique.");
			}
			s72::Scene::Ptr scene(new s72::Scene(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				{}
			));
			scene72.scene = scene;
			scene72.graph.push_back(scene);
		}
		else if (type == "NODE") {
			jjyou::glsl::vec3 translation(
				static_cast<float>(obj["translation"][0]),
				static_cast<float>(obj["translation"][1]),
				static_cast<float>(obj["translation"][2])
			);
			jjyou::glsl::quat rotation(
				static_cast<float>(obj["rotation"][0]),
				static_cast<float>(obj["rotation"][1]),
				static_cast<float>(obj["rotation"][2]),
				static_cast<float>(obj["rotation"][3])
			);
			jjyou::glsl::vec3 scale(
				static_cast<float>(obj["scale"][0]),
				static_cast<float>(obj["scale"][1]),
				static_cast<float>(obj["scale"][2])
			);
			s72::Node::Ptr node(new s72::Node(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				translation,
				rotation,
				scale,
				{},
				{},
				{},
				{}
			));
			scene72.graph.push_back(node);
		}
		else if (type == "MESH") {
			int count(obj["count"]);
			std::string fileName(obj["attributes"]["POSITION"]["src"]);
			int offset(obj["attributes"]["POSITION"]["offset"]);
			std::ifstream fin(baseDir / fileName, std::ios::in | std::ios::binary);
			if (!fin.is_open()) {
				this->destroy(scene72);
				throw std::runtime_error("Cannot open binary file \"" + fileName + "\".");
			}
			fin.seekg(offset, std::ios::beg);
			VkDeviceSize bufferSize = 28 * count;
			VkBuffer vertexBuffer;
			jjyou::vk::Memory vertexBufferMemory;
			std::tie(vertexBuffer, vertexBufferMemory) = this->createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				{ *this->physicalDevice.graphicsQueueFamily(), *this->physicalDevice.transferQueueFamily() },
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			VkBuffer stagingBuffer;
			jjyou::vk::Memory stagingBufferMemory;
			std::tie(stagingBuffer, stagingBufferMemory) = this->createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				{ *this->physicalDevice.transferQueueFamily() },
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			JJYOU_VK_UTILS_CHECK(this->allocator.map(stagingBufferMemory));
			fin.read(reinterpret_cast<char*>(stagingBufferMemory.mappedAddress()), bufferSize);
			BBox bbox(
				count,
				[&](std::size_t i)->jjyou::glsl::vec3 {
					const char* bytePtr = reinterpret_cast<const char*>(stagingBufferMemory.mappedAddress());
					return *reinterpret_cast<const jjyou::glsl::vec3*>(bytePtr + i * 28);
				}
			);
			JJYOU_VK_UTILS_CHECK(this->allocator.unmap(stagingBufferMemory));
			this->copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
			this->allocator.free(stagingBufferMemory);
			vkDestroyBuffer(this->device.get(), stagingBuffer, nullptr);
			s72::Mesh::Ptr mesh(new s72::Mesh(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				count,
				vertexBuffer,
				vertexBufferMemory,
				bbox
			));
			scene72.meshes[name] = mesh;
			scene72.graph.push_back(mesh);
		}
		else if (type == "CAMERA") {
			s72::Camera::Ptr camera(new s72::PerspectiveCamera(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				float(obj["perspective"]["vfov"]),
				float(obj["perspective"]["aspect"]),
				float(obj["perspective"]["near"]),
				float(obj["perspective"]["far"])
			));
			if (scene72.cameras.find(name) != scene72.cameras.end()) {
				this->destroy(scene72);
				throw std::runtime_error("Multiple cameras have the same name \"" + name + "\".");
			}
			scene72.cameras[name] = camera;
			scene72.graph.push_back(camera);
		}
		else if (type == "DRIVER") {
			std::string channelStr = obj["channel"].string();
			s72::Driver::Channel channel;
			if (channelStr == "translation")
				channel = s72::Driver::Channel::Translation;
			else if (channelStr == "scale")
				channel = s72::Driver::Channel::Scale;
			else if (channelStr == "rotation")
				channel = s72::Driver::Channel::Rotation;
			else
				throw std::runtime_error("Driver \"" + name + "\" has an unknown channel.");
			std::vector<float> times(obj["times"]);
			std::vector<float> values(obj["values"]);
			times.reserve(obj["times"].size());
			if (!times.empty()) {
				scene72.minTime = std::min(scene72.minTime, times.front());
				scene72.maxTime = std::max(scene72.maxTime, times.back());
			}
			if (channel == s72::Driver::Channel::Translation && values.size() != times.size() * 3 ||
				channel == s72::Driver::Channel::Scale && values.size() != times.size() * 3 ||
				channel == s72::Driver::Channel::Rotation && values.size() != times.size() * 4)
			{
				throw std::runtime_error("Driver \"" + name + "\" values do not match times.");
			}
			s72::Driver::Interpolation interpolation = s72::Driver::Interpolation::Linear;
			s72::Driver::Ptr driver(new s72::Driver(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				{},
				channel,
				times,
				values,
				interpolation
			));
			scene72.drivers.push_back(driver);
			scene72.graph.push_back(driver);
		}
		else {
			this->destroy(scene72);
			throw std::runtime_error("Unknown object type \"" + type + "\".");
		}
	}
	for (int i = 1; i < json.size(); ++i) {
		const auto& obj = json[i];
		std::string type = obj["type"].string();
		s72::Object::Ptr object = scene72.graph[i - 1];
		if (type == "SCENE") {
			s72::Scene::Ptr scene = std::reinterpret_pointer_cast<s72::Scene>(object);
			for (const auto& rootIdx : obj["roots"]) {
				if (int(rootIdx) > scene72.graph.size() || scene72.graph[int(rootIdx) - 1]->type != "NODE") {
					this->destroy(scene72);
					throw std::runtime_error("Scene\'s roots reference " + std::to_string(int(rootIdx)) + " whose type is not node.");
				}
				scene->roots.push_back(std::reinterpret_pointer_cast<s72::Node>(scene72.graph[int(rootIdx) - 1]));
			}
		}
		else if (type == "NODE") {
			s72::Node::Ptr node = std::reinterpret_pointer_cast<s72::Node>(object);
			if (auto cameraIter = obj.find("camera"); cameraIter != obj.end()) {
				int cameraIdx(cameraIter.value());
				if (cameraIdx > scene72.graph.size() || scene72.graph[cameraIdx - 1]->type != "CAMERA") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s camera references " + std::to_string(cameraIdx) + " whose type is not camera.");
				}
				node->camera = std::reinterpret_pointer_cast<s72::Camera>(scene72.graph[cameraIdx - 1]);
			}
			if (auto meshIter = obj.find("mesh"); meshIter != obj.end()) {
				int meshIdx(meshIter.value());
				if (meshIdx > scene72.graph.size() || scene72.graph[meshIdx - 1]->type != "MESH") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s mesh references " + std::to_string(meshIdx) + " whose type is not mesh.");
				}
				node->mesh = std::reinterpret_pointer_cast<s72::Mesh>(scene72.graph[meshIdx - 1]);
			}
			if (auto childrenIter = obj.find("children"); childrenIter != obj.end()) {
				const auto& children = childrenIter.value();
				for (const auto& childIdx : children) {
					if (int(childIdx) > scene72.graph.size() || scene72.graph[int(childIdx) - 1]->type != "NODE") {
						this->destroy(scene72);
						throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s children reference " + std::to_string(int(childIdx)) + " whose type is not node.");
					}
					node->children.push_back(std::reinterpret_pointer_cast<s72::Node>(scene72.graph[int(childIdx) - 1]));
				}
			}
		}
		else if (type == "MESH") {
		}
		else if (type == "CAMERA") {
		}
		else if (type == "DRIVER") {
			s72::Driver::Ptr driver = std::reinterpret_pointer_cast<s72::Driver>(object);
			int nodeIdx(obj["node"]);
			if (nodeIdx > scene72.graph.size() || scene72.graph[nodeIdx - 1]->type != "NODE") {
				this->destroy(scene72);
				throw std::runtime_error("Driver" + std::to_string(driver->idx) + "\'s node references " + std::to_string(nodeIdx) + " whose type is not node.");
			}
			s72::Node::Ptr node = std::reinterpret_pointer_cast<s72::Node>(scene72.graph[nodeIdx - 1]);
			driver->node = node;
			node->drivers[driver->channel] = driver;
		}
	}
	return pScene72;
}

void Engine::destroy(s72::Scene72& scene72) {
	vkDeviceWaitIdle(this->device.get());
	for (s72::Object::Ptr object : scene72.graph) {
		if (object->type == "MESH") {
			s72::Mesh::Ptr mesh = std::reinterpret_pointer_cast<s72::Mesh>(object);
			this->allocator.free(mesh->vertexBufferMemory);
			vkDestroyBuffer(this->device.get(), mesh->vertexBuffer, nullptr);
			mesh->vertexBuffer = nullptr;
		}
	}
	scene72.clear();
}

bool s72::Scene72::traverse(
	float playTime,
	jjyou::glsl::mat4 rootTransform,
	const std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)>& visit
) {
	// update driver times
	if (playTime <= this->minTime) {
		for (auto& driver : this->drivers) {
			driver->timeIter = -1;
		}
	}
	else if (playTime >= this->maxTime) {
		for (auto& driver : this->drivers) {
			driver->timeIter = static_cast<int>(driver->times.size()) - 1;
		}
	}
	else if (playTime > this->currPlayTime) {
		for (auto& driver : this->drivers) {
			while (driver->timeIter + 1 < driver->times.size() && playTime >= driver->times[driver->timeIter + 1])
				++driver->timeIter;
		}
	}
	else if (playTime < this->currPlayTime) {
		for (auto& driver : this->drivers) {
			while (driver->timeIter >= 0 && playTime < driver->times[driver->timeIter])
				--driver->timeIter;
		}
	}
	this->currPlayTime = playTime;
	// traverse
	for (s72::Node::WeakPtr node : this->scene->roots)
		if (!this->_traverse(node.lock(), rootTransform, visit))
			return false;
	return true;
}

bool s72::Scene72::_traverse(
	s72::Node::Ptr node,
	const jjyou::glsl::mat4& parentTransform,
	const std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)>& visit
) {
	jjyou::glsl::mat4 translate(1.0f);
	if (!node->drivers[s72::Driver::Channel::Translation].expired()) {
		const auto& driver = node->drivers[s72::Driver::Channel::Translation].lock();
		if (driver->timeIter + 1 == driver->times.size()) {
			translate[3] = jjyou::glsl::vec4(driver->values[driver->timeIter * 3 + 0], driver->values[driver->timeIter * 3 + 1], driver->values[driver->timeIter * 3 + 2], 1.0f);
		}
		else if (driver->timeIter == -1) {
			translate[3] = jjyou::glsl::vec4(driver->values[0], driver->values[1], driver->values[2], 1.0f);
		}
		else {
			float begT = driver->times[driver->timeIter];
			jjyou::glsl::vec3 begV(driver->values[driver->timeIter * 3 + 0], driver->values[driver->timeIter * 3 + 1], driver->values[driver->timeIter * 3 + 2]);
			float endT = driver->times[driver->timeIter + 1];
			jjyou::glsl::vec3 endV(driver->values[driver->timeIter * 3 + 3], driver->values[driver->timeIter * 3 + 4], driver->values[driver->timeIter * 3 + 5]);
			jjyou::glsl::vec3 interp = interpolate(driver->interpolation, begT, endT, this->currPlayTime, begV, endV);
			translate[3] = jjyou::glsl::vec4(interp, 1.0f);
		}
	}
	else {
		translate[3] = jjyou::glsl::vec4(node->translation, 1.0f);
	}
	jjyou::glsl::mat4 rotate(1.0f);
	if (!node->drivers[s72::Driver::Channel::Rotation].expired()) {
		const auto& driver = node->drivers[s72::Driver::Channel::Rotation].lock();
		if (driver->timeIter + 1 == driver->times.size()) {
			rotate = jjyou::glsl::mat4(jjyou::glsl::quat(driver->values[driver->timeIter * 4 + 0], driver->values[driver->timeIter * 4 + 1], driver->values[driver->timeIter * 4 + 2], driver->values[driver->timeIter * 4 + 3]));
		}
		else if (driver->timeIter == -1) {
			rotate = jjyou::glsl::mat4(jjyou::glsl::quat(driver->values[0], driver->values[1], driver->values[2], driver->values[3]));
		}
		else {
			float begT = driver->times[driver->timeIter];
			jjyou::glsl::quat begV(driver->values[driver->timeIter * 4 + 0], driver->values[driver->timeIter * 4 + 1], driver->values[driver->timeIter * 4 + 2], driver->values[driver->timeIter * 4 + 3]);
			float endT = driver->times[driver->timeIter + 1];
			jjyou::glsl::quat endV(driver->values[driver->timeIter * 4 + 4], driver->values[driver->timeIter * 4 + 5], driver->values[driver->timeIter * 4 + 6], driver->values[driver->timeIter * 4 + 7]);
			jjyou::glsl::quat interp = interpolate(driver->interpolation, begT, endT, this->currPlayTime, begV, endV);
			rotate = jjyou::glsl::mat4(interp);
		}
	}
	else {
		rotate = jjyou::glsl::mat4(node->rotation);
	}
	jjyou::glsl::mat4 scale(1.0f);
	if (!node->drivers[s72::Driver::Channel::Scale].expired()) {
		const auto& driver = node->drivers[s72::Driver::Channel::Scale].lock();
		if (driver->timeIter + 1 == driver->times.size()) {
			scale[0][0] = driver->values[driver->timeIter * 3 + 0];
			scale[1][1] = driver->values[driver->timeIter * 3 + 1];
			scale[2][2] = driver->values[driver->timeIter * 3 + 2];
		}
		else if (driver->timeIter == -1) {
			scale[0][0] = driver->values[0];
			scale[1][1] = driver->values[1];
			scale[2][2] = driver->values[2];
		}
		else {
			float begT = driver->times[driver->timeIter];
			jjyou::glsl::vec3 begV(driver->values[driver->timeIter * 3 + 0], driver->values[driver->timeIter * 3 + 1], driver->values[driver->timeIter * 3 + 2]);
			float endT = driver->times[driver->timeIter + 1];
			jjyou::glsl::vec3 endV(driver->values[driver->timeIter * 3 + 3], driver->values[driver->timeIter * 3 + 4], driver->values[driver->timeIter * 3 + 5]);
			jjyou::glsl::vec3 interp = interpolate(driver->interpolation, begT, endT, this->currPlayTime, begV, endV);
			scale[0][0] = interp[0];
			scale[1][1] = interp[1];
			scale[2][2] = interp[2];
		}
	}
	else {
		scale[0][0] = node->scale[0]; scale[1][1] = node->scale[1]; scale[2][2] = node->scale[2];
	}
	jjyou::glsl::mat4 currentTransform = parentTransform * translate * rotate * scale;
	if (!visit(node, currentTransform))
		return false;
	for (s72::Node::WeakPtr child : node->children)
		if (!this->_traverse(child.lock(), currentTransform, visit))
			return false;
	return true;
}