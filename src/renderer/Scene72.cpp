#include "Scene72.hpp"
#include "Engine.hpp"
#include <type_traits>
#include <jjyou/utils.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

inline jjyou::glsl::vec3 unpackRGBE(const jjyou::glsl::vec<unsigned char, 4>& rgbe) {
	if (rgbe == jjyou::glsl::vec<unsigned char, 4>(0)) {
		return jjyou::glsl::vec3(0.0f, 0.0f, 0.0f);
	}
	else {
		return pow(2.0f, static_cast<float>(rgbe.a) - 128.0f) * (jjyou::glsl::vec3(rgbe.cast<float>()) + 0.5f) / 256.0f;
	}
}

inline jjyou::glsl::vec<unsigned char, 4> packRGBE(jjyou::glsl::vec3 color) {
	if (color == jjyou::glsl::vec3(0.0))
		return jjyou::glsl::vec<unsigned char, 4>(0);
	float maxCoeff = std::max(std::max(color.r, color.g), color.b);
	int expo = int(std::ceil(std::log2(maxCoeff / (255.5f / 256.0f))));
	if (expo < -128)
		return jjyou::glsl::vec<unsigned char, 4>(0);
	jjyou::glsl::vec<unsigned char, 4> ret{};
	for (int i = 0; i < 3; ++i)
		ret[i] = static_cast<unsigned char>(std::clamp<float>(color[i] / std::powf(2.0f, static_cast<float>(expo)) * 256.0f - 0.5f, 0.0f, 255.0f));
	ret.a = static_cast<unsigned char>(std::clamp<int>(expo + 128, 0, 255));
	return ret;
}

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

template <int Length>
static jjyou::vk::Texture2D loadTexture(
	const std::filesystem::path& baseDir,
	const jjyou::vk::Context& context,
	jjyou::vk::MemoryAllocator& allocator,
	VkCommandPool graphicsCommandPool,
	VkCommandPool transferCommandPool,
	const jjyou::io::Json<>& material,
	const std::string& textureName,
	const std::array<unsigned char, Length>& defaultValue,
	bool normalConversion// normal = texture * 2 - 1
) requires (Length == 1 || Length == 3 || Length == 4)
{
	using ElementType = std::conditional_t<Length == 3, std::array<unsigned char, Length + 1>, std::array<unsigned char, Length>>;
	jjyou::vk::Texture2D texture;
	VkFormat format = VK_FORMAT_UNDEFINED;
	switch (Length) {
	case 1:
		format = VK_FORMAT_R8_UNORM;
		break;
	case 3:
		// Note: VK_FORMAT_R8G8B8_UNORM is usually not supported.
		// We will use VK_FORMAT_R8G8B8A8_UNORM for 3-dimensional data.
	case 4:
		format = VK_FORMAT_R8G8B8A8_UNORM;
		break;
	default:
		return {};
	}
	if (material.find(textureName) == material.end()) {
		// Texture not found. Create 1x1 texture with default value.
		ElementType _defaultValue{};
		for (int i = 0; i < Length; ++i)
			_defaultValue[i] = defaultValue[i];
		if constexpr (Length == 3) {
			_defaultValue[3] = 255;
		}
		texture.create(
			context,
			allocator,
			graphicsCommandPool,
			transferCommandPool,
			_defaultValue.data(),
			format,
			VkExtent2D{ .width = 1,.height = 1 }
		);
	}
	else {
		// Texture found, Check whether the texture is a constant value or an external image.
		if (material[textureName].type() != jjyou::io::JsonType::Object) {
			// Constant value.
			ElementType constantValue{};
			if constexpr (Length == 1) {
				if (!normalConversion)
					constantValue[0] = jjyou::utils::color_cast<unsigned char>(static_cast<float>(material[textureName]));
				else
					constantValue[0] = jjyou::utils::color_cast<unsigned char>(static_cast<float>(material[textureName]) / 2.0 + 0.5);
			}
			else {
				if (!normalConversion)
					for (int i = 0; i < Length; ++i)
						constantValue[i] = jjyou::utils::color_cast<unsigned char>(static_cast<float>(material[textureName][i]));
				else
					for (int i = 0; i < Length; ++i)
						constantValue[i] = jjyou::utils::color_cast<unsigned char>(static_cast<float>(material[textureName][i]) / 2.0 + 0.5);
				if constexpr (Length == 3)
					constantValue[3] = 255;
			}
			texture.create(
				context,
				allocator,
				graphicsCommandPool,
				transferCommandPool,
				constantValue.data(),
				format,
				VkExtent2D{ .width = 1,.height = 1 }
			);
		}
		else {
			int texWidth, texHeight, texChannels;
			std::filesystem::path imagePath = baseDir / static_cast<std::string>(material[textureName]["src"]);
			stbi_uc* pixels = stbi_load(imagePath.string().c_str(), &texWidth, &texHeight, &texChannels, (Length == 1) ? STBI_grey : STBI_rgb_alpha);
			if (pixels == nullptr) {
				return {};
			}
			VkExtent2D extent{
				.width = static_cast<std::uint32_t>(texWidth),
				.height = static_cast<std::uint32_t>(texHeight)
			};
			texture.create(
				context,
				allocator,
				graphicsCommandPool,
				transferCommandPool,
				pixels,
				format,
				extent
			);
			stbi_image_free(pixels);
		}
	}
	return texture;
}

s72::Scene72::Ptr Engine::load(
	const jjyou::io::Json<>& json,
	const std::filesystem::path& baseDir
) {
	s72::Scene72::Ptr pScene72(new s72::Scene72);
	s72::Scene72& scene72 = *pScene72;
	scene72.minTime = std::numeric_limits<float>::max();
	scene72.maxTime = -std::numeric_limits<float>::max();
	// Create a default simple material
	scene72.defaultMaterial.reset(new s72::SimpleMaterial(std::numeric_limits<std::uint32_t>::max(), "default material"));
	// Load objects
	if (json[0].string() != "s72-v1") {
		this->destroy(scene72);
		throw std::runtime_error("Scene72 file must start with \"s72-v1\"");
	}
	for (int i = 1; i < json.size(); ++i) {
		const auto& obj = json[i];
		std::string type(obj["type"]);
		std::string name(obj["name"]);
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
			int stride(obj["attributes"]["POSITION"]["stride"]);
			std::ifstream fin(baseDir / fileName, std::ios::in | std::ios::binary);
			if (!fin.is_open()) {
				this->destroy(scene72);
				throw std::runtime_error("Cannot open binary file \"" + fileName + "\".");
			}
			fin.seekg(offset, std::ios::beg);
			VkDeviceSize bufferSize = stride * count;
			VkBuffer vertexBuffer;
			jjyou::vk::Memory vertexBufferMemory;
			std::tie(vertexBuffer, vertexBufferMemory) = this->createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main), *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Transfer) },
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			VkBuffer stagingBuffer;
			jjyou::vk::Memory stagingBufferMemory;
			std::tie(stagingBuffer, stagingBufferMemory) = this->createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Transfer) },
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			JJYOU_VK_UTILS_CHECK(this->allocator.map(stagingBufferMemory));
			fin.read(reinterpret_cast<char*>(stagingBufferMemory.mappedAddress()), bufferSize);
			BBox bbox(
				count,
				[&](std::size_t i)->jjyou::glsl::vec3 {
					const char* bytePtr = reinterpret_cast<const char*>(stagingBufferMemory.mappedAddress());
					return *reinterpret_cast<const jjyou::glsl::vec3*>(bytePtr + i * stride);
				}
			);
			JJYOU_VK_UTILS_CHECK(this->allocator.unmap(stagingBufferMemory));
			this->copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
			this->allocator.free(stagingBufferMemory);
			vkDestroyBuffer(*this->context.device(), stagingBuffer, nullptr);
			s72::Mesh::Ptr mesh(new s72::Mesh(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				count,
				vertexBuffer,
				std::move(vertexBufferMemory),
				{},
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
				this->destroy(scene72);
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
		else if (type == "MATERIAL") {
			s72::Material::Ptr material;
			if (obj.find("simple") != obj.end()) {
				// Simple material
				material.reset(new s72::SimpleMaterial(
					static_cast<std::uint32_t>(scene72.graph.size() + 1),
					name
				));
			}
			else {
				// Not simple material. Load normal map and displacement map.
				// Note: VK_FORMAT_R8G8B8_UNORM is usually not supported. We will use VK_FORMAT_R8G8B8A8_UNORM for 3-dimensional data.
				jjyou::vk::Texture2D normalMap = loadTexture(
					baseDir,
					this->context,
					this->allocator,
					this->graphicsCommandPool,
					this->transferCommandPool,
					obj,
					"normalMap",
					std::array<unsigned char, 3>{{127, 127, 255}},
					true
				);
				if (!normalMap.has_value()) {
					this->destroy(scene72);
					throw std::runtime_error("Material \"" + name + "\" failed to create normal map texture.");
				}
				jjyou::vk::Texture2D displacementMap = loadTexture(
					baseDir,
					this->context,
					this->allocator,
					this->graphicsCommandPool,
					this->transferCommandPool,
					obj,
					"displacementMap",
					std::array<unsigned char, 1>{{0}},
					false
				);
				if (!displacementMap.has_value()) {
					normalMap.destroy();
					this->destroy(scene72);
					throw std::runtime_error("Material \"" + name + "\" failed to create displacement map texture.");
				}
				if (obj.find("mirror") != obj.end()) {
					material.reset(new s72::MirrorMaterial(
						static_cast<std::uint32_t>(scene72.graph.size() + 1),
						name,
						std::move(normalMap),
						std::move(displacementMap)
					));
				}
				else if (obj.find("environment") != obj.end()) {
					material.reset(new s72::EnvironmentMaterial(
						static_cast<std::uint32_t>(scene72.graph.size() + 1),
						name,
						std::move(normalMap),
						std::move(displacementMap)
					));
				}
				else if (obj.find("lambertian") != obj.end()) {
					jjyou::vk::Texture2D albedo = loadTexture(
						baseDir,
						this->context,
						this->allocator,
						this->graphicsCommandPool,
						this->transferCommandPool,
						obj["lambertian"],
						"albedo",
						std::array<unsigned char, 3>{{255, 255, 255}},
						false
					);
					if (!albedo.has_value()) {
						normalMap.destroy();
						displacementMap.destroy();
						this->destroy(scene72);
						throw std::runtime_error("Material \"" + name + "\" failed to create base color texture.");
					}
					material.reset(new s72::LambertianMaterial(
						static_cast<std::uint32_t>(scene72.graph.size() + 1),
						name,
						std::move(normalMap),
						std::move(displacementMap),
						std::move(albedo)
					));
				}
				else if (obj.find("pbr") != obj.end()) {
					jjyou::vk::Texture2D albedo = loadTexture(
						baseDir,
						this->context,
						this->allocator,
						this->graphicsCommandPool,
						this->transferCommandPool,
						obj["pbr"],
						"albedo",
						std::array<unsigned char, 3>{{255, 255, 255}},
						false
					);
					if (!albedo.has_value()) {
						normalMap.destroy();
						displacementMap.destroy();
						this->destroy(scene72);
						throw std::runtime_error("Material \"" + name + "\" failed to create albedo texture.");
					}
					jjyou::vk::Texture2D roughness = loadTexture(
						baseDir,
						this->context,
						this->allocator,
						this->graphicsCommandPool,
						this->transferCommandPool,
						obj["pbr"],
						"roughness",
						std::array<unsigned char, 1>{{255}},
						false
					);
					if (!roughness.has_value()) {
						normalMap.destroy();
						displacementMap.destroy();
						albedo.destroy();
						this->destroy(scene72);
						throw std::runtime_error("Material \"" + name + "\" failed to create roughness texture.");
					}
					jjyou::vk::Texture2D metalness = loadTexture(
						baseDir,
						this->context,
						this->allocator,
						this->graphicsCommandPool,
						this->transferCommandPool,
						obj["pbr"],
						"metalness",
						std::array<unsigned char, 1>{{0}},
						false
					);
					if (!metalness.has_value()) {
						normalMap.destroy();
						displacementMap.destroy();
						albedo.destroy();
						roughness.destroy();
						this->destroy(scene72);
						throw std::runtime_error("Material \"" + name + "\" failed to create metalness texture.");
					}
					material.reset(new s72::PbrMaterial(
						static_cast<std::uint32_t>(scene72.graph.size() + 1),
						name,
						std::move(normalMap),
						std::move(displacementMap),
						std::move(albedo),
						std::move(roughness),
						std::move(metalness)
					));
				}
				else {
					normalMap.destroy();
					displacementMap.destroy();
					this->destroy(scene72);
					throw std::runtime_error("Material \"" + name + "\" must have exactly one of these properties: \"simple\", \"mirror\", \"environment\", \"lambertian\", or \"pbr\".");
				}
			}
			scene72.graph.push_back(material);
		}
		else if (type == "ENVIRONMENT") {
			if (scene72.environment) {
				this->destroy(scene72);
				throw std::runtime_error("Find multiple environments.");
			}
			// Load radiance map
			jjyou::vk::Texture2D radiance;
			{
				if (obj.find("radiance") == obj.end()) {
					this->destroy(scene72);
					throw std::runtime_error("Environment \"" + name + "\" must have a \"radiance\" property to specify the path to the radiance texture.");
				}
				int texWidth, texHeight, texChannels;
				std::filesystem::path imagePath = baseDir / static_cast<std::string>(obj["radiance"]["src"]);
				stbi_uc* pixels = stbi_load(imagePath.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
				if (pixels == nullptr) {
					this->destroy(scene72);
					throw std::runtime_error("Environment \"" + name + "\" failed to load radiance texture from \"" + imagePath.string() + "\".");
				}
				std::vector<jjyou::glsl::vec4> baseRgb; baseRgb.reserve(texWidth * texHeight);
				for (int j = 0; j < texWidth * texHeight; ++j)
					baseRgb.emplace_back(unpackRGBE(reinterpret_cast<jjyou::glsl::vec<unsigned char, 4>*>(pixels)[j]), 1.0f);
				stbi_image_free(pixels);
				VkExtent2D extent{
					.width = static_cast<std::uint32_t>(texWidth),
					.height = static_cast<std::uint32_t>(texHeight) / 6
				};
				std::vector<std::vector<jjyou::glsl::vec4>> mipRgb; mipRgb.reserve(20);
				std::vector<void*> mipData; mipData.reserve(20);
				for (int j = 1; ; ++j) {
					imagePath = static_cast<std::string>(obj["radiance"]["src"]);
					imagePath.replace_filename(imagePath.stem().string() + ".prefilteredenv." + std::to_string(j) + imagePath.extension().string());
					imagePath = baseDir / imagePath;
					if (!std::filesystem::exists(imagePath))
						break;
					pixels = stbi_load(imagePath.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
					if (pixels == nullptr) {
						this->destroy(scene72);
						throw std::runtime_error("Environment \"" + name + "\" failed to load pre-filtered environment texture from \"" + imagePath.string() + "\".");
					}
					mipRgb.emplace_back();
					std::vector<jjyou::glsl::vec4>& rgb = mipRgb.back(); rgb.reserve(texWidth * texHeight);
					for (int k = 0; k < texWidth * texHeight; ++k)
						rgb.emplace_back(unpackRGBE(reinterpret_cast<jjyou::glsl::vec<unsigned char, 4>*>(pixels)[k]), 1.0f);
					stbi_image_free(pixels);
					mipData.push_back(rgb.data());
				}
				if (mipData.size() == 0) {
					this->destroy(scene72);
					throw std::runtime_error("Environment \"" + name + "\" failed to load any pre-filtered environment texture.");
				}
				radiance.create(
					this->context,
					this->allocator,
					this->graphicsCommandPool,
					this->transferCommandPool,
					baseRgb.data(),
					VK_FORMAT_R32G32B32A32_SFLOAT,
					extent,
					static_cast<int>(mipData.size() + 1),
					mipData,
					true
				);
			}
			// Load lambertian map
			jjyou::vk::Texture2D lambertian;
			{
				int texWidth, texHeight, texChannels;
				std::filesystem::path imagePath = static_cast<std::string>(obj["radiance"]["src"]);
				imagePath.replace_filename(imagePath.stem().string() + ".lambertian" + imagePath.extension().string());
				imagePath = baseDir / imagePath;
				stbi_uc* pixels = stbi_load(imagePath.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
				if (pixels == nullptr) {
					radiance.destroy();
					this->destroy(scene72);
					throw std::runtime_error("Environment \"" + name + "\" failed to load lambertian texture from \"" + imagePath.string() + "\".");
				}
				std::vector<jjyou::glsl::vec4>rgb; rgb.reserve(texWidth * texHeight);
				for (int j = 0; j < texWidth * texHeight; ++j)
					rgb.emplace_back(unpackRGBE(reinterpret_cast<jjyou::glsl::vec<unsigned char, 4>*>(pixels)[j]), 1.0f);
				VkExtent2D extent{
					.width = static_cast<std::uint32_t>(texWidth),
					.height = static_cast<std::uint32_t>(texHeight) / 6
				};
				lambertian.create(
					this->context,
					this->allocator,
					this->graphicsCommandPool,
					this->transferCommandPool,
					rgb.data(),
					VK_FORMAT_R32G32B32A32_SFLOAT,
					extent,
					1,
					{},
					true
				);
				stbi_image_free(pixels);
			}
			// Load environment BRDF map
			jjyou::vk::Texture2D environmentBRDF;
			{
				std::uint32_t height;
				std::filesystem::path imagePath = static_cast<std::string>(obj["radiance"]["src"]);
				imagePath.replace_filename("envbrdf.bin");
				imagePath = baseDir / imagePath;
				std::ifstream fin(imagePath, std::ios::in | std::ios::binary);
				if (!fin.is_open()) {
					radiance.destroy();
					lambertian.destroy();
					this->destroy(scene72);
					throw std::runtime_error("Environment \"" + name + "\" failed to load environment BRDF lookup table from \"" + imagePath.string() + "\".");
				}
				fin.read(reinterpret_cast<char*>(&height), sizeof(height));
				std::vector<float> data(height * height * 2);
				fin.read(reinterpret_cast<char*>(data.data()), height * height * sizeof(float) * 2);
				VkExtent2D extent{
					.width = height,
					.height = height
				};
				environmentBRDF.create(
					this->context,
					this->allocator,
					this->graphicsCommandPool,
					this->transferCommandPool,
					data.data(),
					VK_FORMAT_R32G32_SFLOAT,
					extent,
					1,
					{},
					false,
					VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				);
			}
			s72::Environment::Ptr environment(new s72::Environment(
				static_cast<std::uint32_t>(scene72.graph.size() + 1),
				name,
				std::move(radiance),
				std::move(lambertian),
				std::move(environmentBRDF)
			));
			scene72.environment = environment;
			scene72.graph.push_back(environment);
		}
		else if (type == "LIGHT") {
			s72::Light::Ptr light;
			jjyou::glsl::vec3 tint{ 1.0f };
			if (obj.find("tint") != obj.end()) {
				tint[0] = static_cast<float>(obj["tint"][0]);
				tint[1] = static_cast<float>(obj["tint"][1]);
				tint[2] = static_cast<float>(obj["tint"][2]);
			}
			std::uint32_t shadow = 0U;
			if (obj.find("shadow") != obj.end())
				shadow = static_cast<std::uint32_t>(static_cast<int>(obj["shadow"]));
			if (obj.find("sun") != obj.end()) {
				// Sun light
				float angle(obj["sun"]["angle"]);
				float strength(obj["sun"]["strength"]);
				light.reset(new s72::SunLight(
					static_cast<std::uint32_t>(scene72.graph.size() + 1),
					name,
					tint,
					shadow,
					angle,
					strength
				));
			}
			else if (obj.find("sphere") != obj.end()) {
				// Sphere light
				float radius(obj["sphere"]["radius"]);
				float power(obj["sphere"]["power"]);
				float limit(obj["sphere"]["limit"]);
				light.reset(new s72::SphereLight(
					static_cast<std::uint32_t>(scene72.graph.size() + 1),
					name,
					tint,
					shadow,
					radius,
					power,
					limit
				));
			}
			else if (obj.find("spot") != obj.end()) {
				// Spot light
				float radius(obj["spot"]["radius"]);
				float power(obj["spot"]["power"]);
				float fov(obj["spot"]["fov"]);
				float blend(obj["spot"]["blend"]);
				float limit(obj["spot"]["limit"]);
				light.reset(new s72::SpotLight(
					static_cast<std::uint32_t>(scene72.graph.size() + 1),
					name,
					tint,
					shadow,
					radius,
					power,
					fov,
					blend,
					limit
				));
			}else {
				throw std::runtime_error("Light \"" + name + "\" has an unknown lighting type.");
			}
			scene72.graph.push_back(light);
		}
		else {
			this->destroy(scene72);
			throw std::runtime_error("Unknown object type \"" + type + "\".");
		}
	}

	// Set objects reference
	for (int i = 1; i < json.size(); ++i) {
		const auto& obj = json[i];
		std::string type = obj["type"].string();
		s72::Object::Ptr object = scene72.graph[i - 1];
		if (type == "SCENE") {
			s72::Scene::Ptr scene = std::reinterpret_pointer_cast<s72::Scene>(object);
			for (const auto& rootIdx : obj["roots"]) {
				if (static_cast<int>(rootIdx) <= 0 || static_cast<int>(rootIdx) > scene72.graph.size() || scene72.graph[static_cast<int>(rootIdx) - 1]->type != "NODE") {
					this->destroy(scene72);
					throw std::runtime_error("Scene\'s roots reference " + std::to_string(static_cast<int>(rootIdx)) + " whose type is not node.");
				}
				scene->roots.push_back(std::reinterpret_pointer_cast<s72::Node>(scene72.graph[static_cast<int>(rootIdx) - 1]));
			}
		}
		else if (type == "NODE") {
			s72::Node::Ptr node = std::reinterpret_pointer_cast<s72::Node>(object);
			if (obj.find("camera") != obj.end()) {
				int cameraIdx(obj["camera"]);
				if (cameraIdx <= 0 || cameraIdx > scene72.graph.size() || scene72.graph[cameraIdx - 1]->type != "CAMERA") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s camera references " + std::to_string(cameraIdx) + " whose type is not camera.");
				}
				node->camera = std::reinterpret_pointer_cast<s72::Camera>(scene72.graph[cameraIdx - 1]);
			}
			if (obj.find("mesh") != obj.end()) {
				int meshIdx(obj["mesh"]);
				if (meshIdx <= 0 || meshIdx > scene72.graph.size() || scene72.graph[meshIdx - 1]->type != "MESH") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s mesh references " + std::to_string(meshIdx) + " whose type is not mesh.");
				}
				node->mesh = std::reinterpret_pointer_cast<s72::Mesh>(scene72.graph[meshIdx - 1]);
			}
			if (obj.find("children") != obj.end()) {
				const auto& children = obj["children"];
				for (const auto& childId : children) {
					int childIdx(childId);
					if (childIdx <= 0 || childIdx > scene72.graph.size() || scene72.graph[childIdx - 1]->type != "NODE") {
						this->destroy(scene72);
						throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s children reference " + std::to_string(childIdx) + " whose type is not node.");
					}
					node->children.push_back(std::reinterpret_pointer_cast<s72::Node>(scene72.graph[childIdx - 1]));
				}
			}
			if (obj.find("environment") != obj.end()) {
				int environmentIdx(obj["environment"]);
				if (environmentIdx <= 0 || environmentIdx > scene72.graph.size() || scene72.graph[environmentIdx - 1]->type != "ENVIRONMENT") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s environment reference " + std::to_string(environmentIdx) + " whose type is not environment.");
				}
				node->environment = std::reinterpret_pointer_cast<s72::Environment>(scene72.graph[environmentIdx - 1]);
			}
			if (obj.find("light") != obj.end()) {
				int lightIdx(obj["light"]);
				if (lightIdx <= 0 || lightIdx > scene72.graph.size() || scene72.graph[lightIdx - 1]->type != "LIGHT") {
					this->destroy(scene72);
					throw std::runtime_error("Node" + std::to_string(node->idx) + "\'s light reference " + std::to_string(lightIdx) + " whose type is not light.");
				}
				node->light = std::reinterpret_pointer_cast<s72::Light>(scene72.graph[lightIdx - 1]);
			}
		}
		else if (type == "MESH") {
			s72::Mesh::Ptr mesh = std::reinterpret_pointer_cast<s72::Mesh>(object);
			if (obj.find("material") != obj.end()) {
				int materialIdx(obj["material"]);
				if (materialIdx <= 0 || materialIdx > scene72.graph.size() || scene72.graph[materialIdx - 1]->type != "MATERIAL") {
					this->destroy(scene72);
					throw std::runtime_error("Mesh" + std::to_string(mesh->idx) + "\'s material reference " + std::to_string(materialIdx) + " whose type is not material.");
				}
				mesh->material = std::reinterpret_pointer_cast<s72::Material>(scene72.graph[materialIdx - 1]);
			}
			else {
				mesh->material = scene72.defaultMaterial;
			}
			if (mesh->material.lock()->materialType == "simple" && obj["attributes"].find("TANGENT") != obj["attributes"].end()) {
				this->destroy(scene72);
				throw std::runtime_error("Mesh" + std::to_string(mesh->idx) + "\'s material is a simple material, but it has TANGENT/TEXCOORD attributes.");
			}
			else if (mesh->material.lock()->materialType != "simple" && obj["attributes"].find("TANGENT") == obj["attributes"].end()) {
				this->destroy(scene72);
				throw std::runtime_error("Mesh" + std::to_string(mesh->idx) + "\'s material is not a simple material, but it does not have TANGENT/TEXCOORD attributes.");
			}
		}
		else if (type == "CAMERA") {
		}
		else if (type == "DRIVER") {
			s72::Driver::Ptr driver = std::reinterpret_pointer_cast<s72::Driver>(object);
			int nodeIdx(obj["node"]);
			if (nodeIdx <= 0 || nodeIdx > scene72.graph.size() || scene72.graph[nodeIdx - 1]->type != "NODE") {
				this->destroy(scene72);
				throw std::runtime_error("Driver" + std::to_string(driver->idx) + "\'s node references " + std::to_string(nodeIdx) + " whose type is not node.");
			}
			s72::Node::Ptr node = std::reinterpret_pointer_cast<s72::Node>(scene72.graph[nodeIdx - 1]);
			driver->node = node;
			node->drivers[driver->channel] = driver;
		}
		else if (type == "MATERIAL") {
		}
		else if (type == "ENVIRONMENT") {
		}
		else if (type == "LIGHT") {
		}
	}


	// Get some values for creating descriptor sets
	std::uint32_t numMirrorMaterials = 0;
	std::uint32_t numEnvironmentMaterials = 0;
	std::uint32_t numLambertianMaterials = 0;
	std::uint32_t numPbrMaterials = 0;
	for (const auto& object : scene72.graph) {
		if (object->type == "MATERIAL") {
			s72::Material::Ptr material = std::reinterpret_pointer_cast<s72::Material>(object);
			if (material->materialType == "mirror")
				++numMirrorMaterials;
			else if (material->materialType == "environment")
				++numEnvironmentMaterials;
			else if (material->materialType == "lambertian")
				++numLambertianMaterials;
			else if (material->materialType == "pbr")
				++numPbrMaterials;
		}
	}
	if ((numMirrorMaterials || numEnvironmentMaterials || numLambertianMaterials || numPbrMaterials) && !scene72.environment) {
		this->destroy(scene72);
		throw std::runtime_error("The scene has non simple materials, but does not have an environment object.");
	}
	std::uint32_t numInstances = 0;
	std::uint32_t numSunLights = 0;
	std::uint32_t numSunLightsNoShadow = 0;
	std::uint32_t numSphereLights = 0;
	std::uint32_t numSphereLightsNoShadow = 0;
	std::uint32_t numSpotLights = 0;
	std::uint32_t numSpotLightsNoShadow = 0;
	scene72.traverse(
		scene72.minTime,
		{},
		[&](s72::Node::Ptr node, const jjyou::glsl::mat4& transform) -> bool {
			if (!node->mesh.expired()) {
				++numInstances;
			}
			if (!node->light.expired()) {
				s72::Light::Ptr light = node->light.lock();
				if (light->lightType == "sun") {
					s72::SunLight::Ptr sunLight = std::reinterpret_pointer_cast<s72::SunLight>(light);
					if (sunLight->shadow == 0U) {
						if (numSunLightsNoShadow >= Engine::MAX_NUM_SUM_LIGHTS_NO_SHADOW)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SUM_LIGHTS_NO_SHADOW) + " sun lights with no shadow.");
						++numSunLightsNoShadow;
					}
					else {
						if (numSunLights >= Engine::MAX_NUM_SUM_LIGHTS)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SUM_LIGHTS) + " sun lights with shadow.");
						++numSunLights;
					}
				}
				else if (light->lightType == "sphere") {
					s72::SphereLight::Ptr sphereLight = std::reinterpret_pointer_cast<s72::SphereLight>(light);
					if (sphereLight->shadow == 0U) {
						if (numSphereLightsNoShadow >= Engine::MAX_NUM_SPHERE_LIGHTS_NO_SHADOW)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SPHERE_LIGHTS_NO_SHADOW) + " sphere lights with no shadow.");
						++numSphereLightsNoShadow;
					}
					else {
						if (numSphereLights >= Engine::MAX_NUM_SPHERE_LIGHTS)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SPHERE_LIGHTS) + " sphere lights with shadow.");
						++numSphereLights;
					}
				}
				else if (light->lightType == "spot") {
					s72::SpotLight::Ptr spotLight = std::reinterpret_pointer_cast<s72::SpotLight>(light);
					if (spotLight->shadow == 0U) {
						if (numSpotLightsNoShadow >= Engine::MAX_NUM_SPOT_LIGHTS_NO_SHADOW)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SPOT_LIGHTS_NO_SHADOW) + " spot lights with no shadow.");
						++numSpotLightsNoShadow;
					}
					else {
						if (numSpotLights >= Engine::MAX_NUM_SPOT_LIGHTS)
							throw std::runtime_error("The scene can at most have " + std::to_string(Engine::MAX_NUM_SPOT_LIGHTS) + " spot lights with shadow.");
						scene72.spotLightShadowMaps.push_back(ShadowMap(
							this->context,
							this->allocator,
							ShadowMap::Type::Perspective,
							vk::Extent2D(spotLight->shadow, spotLight->shadow),
							vk::Format::eD32Sfloat,
							this->shadowMappingRenderPass
						));
						++numSpotLights;
					}
				}
			}
			return true;
		}
	);
	// Create shadow map sampler
	{
		vk::SamplerCreateInfo samplerCreateInfo(
			vk::SamplerCreateFlags(0U),
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder,
			vk::SamplerAddressMode::eClampToBorder,
			vk::SamplerAddressMode::eClampToBorder,
			0.0f,
			this->context.enabledDeviceFeatures().samplerAnisotropy,
			this->context.physicalDevice().getProperties().limits.maxSamplerAnisotropy,
			VK_FALSE,
			vk::CompareOp::eAlways,
			0.0f,
			VK_LOD_CLAMP_NONE,
			vk::BorderColor::eFloatOpaqueWhite,
			VK_FALSE
		);
		scene72.spotLightShadowMapSampler = this->context.device().createSampler(samplerCreateInfo);
	}
	// Create descriptor pool
	{
		VkDescriptorPoolSize uniformBufferPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (1 + 3) // 1 for projection&view, 3 for skybox model (radiance/lambertian/pbr)
		};
		VkDescriptorPoolSize uniformBufferDynamicPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (1) // 1 for model
		};
		VkDescriptorPoolSize uniformSamplerPoolSize{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (
				numMirrorMaterials * 2 + // 2 for normal&displacement sampler
				numEnvironmentMaterials * 2 + // 2 for normal&displacement sampler
				numLambertianMaterials * 3 + // 3 for normal&displacement&albedo sampler
				numPbrMaterials * 5 + // 5 for normal&displacement&albedo&roughness&metalness sampler
				1 + // 1 for skybox radiance sampler
				1 + // 1 for skybox lambertian sampler
				1 // 1 for environment BRDF sampler
				)
		};
		VkDescriptorPoolSize uniformStorageBufferPoolSize{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (1) // 1 for lights storage buffer
		};
		VkDescriptorPoolSize uniformShadowMapSamplerPoolSize{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (1) // 1 for spot light shadow map sampler
		};
		VkDescriptorPoolSize uniformShadowMapTexturePoolSize{
			.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT * (Engine::MAX_NUM_SPOT_LIGHTS) // MAX_NUM_SPOT_LIGHTS for spot light shadow maps
		};
		std::vector<VkDescriptorPoolSize> poolSizes = { uniformBufferPoolSize, uniformBufferDynamicPoolSize, uniformStorageBufferPoolSize, uniformShadowMapSamplerPoolSize, uniformShadowMapTexturePoolSize };
		if (uniformSamplerPoolSize.descriptorCount > 0)
			poolSizes.push_back(uniformSamplerPoolSize);
		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.maxSets = static_cast<uint32_t>(Engine::MAX_FRAMES_IN_FLIGHT * (
				1 + // 1 for view level uniform
				1 + // 1 for model level uniform
				numMirrorMaterials +
				numEnvironmentMaterials +
				numLambertianMaterials +
				numPbrMaterials +
				1 // 1 for skybox uniform
				)),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorPool(*this->context.device(), &poolInfo, nullptr, &scene72.descriptorPool));
	}
	// Create view level descriptor sets
	{
		std::vector<VkDescriptorSetLayout> layouts(Engine::MAX_FRAMES_IN_FLIGHT, this->viewLevelUniformDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = scene72.descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data()
		};
		std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> viewLevelUniformDescriptorSets{};
		JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(*this->context.device(), &allocInfo, viewLevelUniformDescriptorSets.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			scene72.frameDescriptorSets[i].viewLevelUniformDescriptorSet = viewLevelUniformDescriptorSets[i];
		}
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			std::tie(scene72.frameDescriptorSets[i].viewLevelUniformBuffer, scene72.frameDescriptorSets[i].viewLevelUniformBufferMemory) =
				this->createBuffer(
					sizeof(Engine::ViewLevelUniform),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main) },
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			this->allocator.map(scene72.frameDescriptorSets[i].viewLevelUniformBufferMemory);
			auto [storageBuffer, storageBufferMemory] = this->createBuffer(
				sizeof(Engine::Lights),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main) },
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			scene72.frameDescriptorSets[i].lightsBuffer = vk::raii::Buffer(this->context.device(), storageBuffer);
			scene72.frameDescriptorSets[i].lightsBufferMemory = std::move(storageBufferMemory);
			this->allocator.map(scene72.frameDescriptorSets[i].lightsBufferMemory);
		}
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bufferInfo1{
				.buffer = scene72.frameDescriptorSets[i].viewLevelUniformBuffer,
				.offset = 0,
				.range = sizeof(Engine::ViewLevelUniform)
			};
			VkDescriptorBufferInfo bufferInfo2{
				.buffer = *scene72.frameDescriptorSets[i].lightsBuffer,
				.offset = 0,
				.range = sizeof(Engine::Lights)
			};
			VkDescriptorImageInfo spotLightShadowMapSamplerInfo3{
				.sampler = *scene72.spotLightShadowMapSampler,
				.imageView = nullptr,
				.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
			};
			std::array<VkDescriptorImageInfo, Engine::MAX_NUM_SPOT_LIGHTS> spotLightShadowMapsInfo4{};
			for (std::uint32_t i = 0; i < numSpotLights; ++i) {
				spotLightShadowMapsInfo4[i].imageView = *scene72.spotLightShadowMaps[i].inputImageView(i);
				spotLightShadowMapsInfo4[i].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			VkWriteDescriptorSet descriptorWrite1{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = scene72.frameDescriptorSets[i].viewLevelUniformDescriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pImageInfo = nullptr,
				.pBufferInfo = &bufferInfo1,
				.pTexelBufferView = nullptr
			};
			VkWriteDescriptorSet descriptorWrite2{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = scene72.frameDescriptorSets[i].viewLevelUniformDescriptorSet,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pImageInfo = nullptr,
				.pBufferInfo = &bufferInfo2,
				.pTexelBufferView = nullptr
			};
			VkWriteDescriptorSet descriptorWrite3{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = scene72.frameDescriptorSets[i].viewLevelUniformDescriptorSet,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
				.pImageInfo = &spotLightShadowMapSamplerInfo3,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr
			};
			VkWriteDescriptorSet descriptorWrite4{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = scene72.frameDescriptorSets[i].viewLevelUniformDescriptorSet,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = numSpotLights,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.pImageInfo = spotLightShadowMapsInfo4.data(),
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr
			};
			std::vector<VkWriteDescriptorSet> descriptorWrites = { descriptorWrite1, descriptorWrite2, descriptorWrite3 };
			if (numSpotLights) {
				descriptorWrites.push_back(descriptorWrite4);
			}
			vkUpdateDescriptorSets(*this->context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
	}
	// Create object level descriptor sets
	{
		std::vector<VkDescriptorSetLayout> layouts(Engine::MAX_FRAMES_IN_FLIGHT, this->objectLevelUniformDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = scene72.descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data()
		};
		std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> objectLevelUniformDescriptorSets;
		JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(*this->context.device(), &allocInfo, objectLevelUniformDescriptorSets.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			scene72.frameDescriptorSets[i].objectLevelUniformDescriptorSet = objectLevelUniformDescriptorSets[i];
		}
		VkDeviceSize dynamicBufferOffset = sizeof(Engine::ObjectLevelUniform);
		VkDeviceSize minAlignment = this->context.physicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
		if (minAlignment > 0)
			dynamicBufferOffset = (dynamicBufferOffset + minAlignment - 1) & ~(minAlignment - 1);
		VkDeviceSize bufferSize = numInstances * dynamicBufferOffset;
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			std::tie(scene72.frameDescriptorSets[i].objectLevelUniformBuffer, scene72.frameDescriptorSets[i].objectLevelUniformBufferMemory) =
				this->createBuffer(
					bufferSize,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main) },
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			this->allocator.map(scene72.frameDescriptorSets[i].objectLevelUniformBufferMemory);
		}
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bufferInfo{
				.buffer = scene72.frameDescriptorSets[i].objectLevelUniformBuffer,
				.offset = 0,
				.range = sizeof(Engine::ObjectLevelUniform)
			};
			VkWriteDescriptorSet descriptorWrite{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = scene72.frameDescriptorSets[i].objectLevelUniformDescriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pImageInfo = nullptr,
				.pBufferInfo = &bufferInfo,
				.pTexelBufferView = nullptr
			};
			std::vector<VkWriteDescriptorSet> descriptorWrites = { descriptorWrite };
			vkUpdateDescriptorSets(*this->context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
	}
	// Create skybox uniform buffer
	if (scene72.environment) {
		VkDeviceSize bufferSize = sizeof(Engine::SkyboxUniform);
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			std::tie(scene72.frameDescriptorSets[i].skyboxUniformBuffer, scene72.frameDescriptorSets[i].skyboxUniformBufferMemory) =
				this->createBuffer(
					bufferSize,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main) },
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			this->allocator.map(scene72.frameDescriptorSets[i].skyboxUniformBufferMemory);
		}
	}
	// Create skybox uniform descriptor sets
	if (scene72.environment) {
		std::vector<VkDescriptorSetLayout> layouts(Engine::MAX_FRAMES_IN_FLIGHT, this->skyboxUniformDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = scene72.descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data()
		};
		std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> skyboxUniformDescriptorSets;
		JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(*this->context.device(), &allocInfo, skyboxUniformDescriptorSets.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			scene72.frameDescriptorSets[i].skyboxUniformDescriptorSet = skyboxUniformDescriptorSets[i];
		}
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bufferInfo{
				.buffer = scene72.frameDescriptorSets[i].skyboxUniformBuffer,
				.offset = 0,
				.range = sizeof(Engine::SkyboxUniform)
			};
			VkDescriptorImageInfo radianceImageInfo{
				.sampler = scene72.environment->radiance.sampler(),
				.imageView = scene72.environment->radiance.imageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			VkDescriptorImageInfo lambertianImageInfo{
				.sampler = scene72.environment->lambertian.sampler(),
				.imageView = scene72.environment->lambertian.imageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			VkDescriptorImageInfo environmentBRDFImageInfo{
				.sampler = scene72.environment->environmentBRDF.sampler(),
				.imageView = scene72.environment->environmentBRDF.imageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			std::vector<VkWriteDescriptorSet> descriptorWrites = {
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = scene72.frameDescriptorSets[i].skyboxUniformDescriptorSet,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pImageInfo = nullptr,
					.pBufferInfo = &bufferInfo,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = scene72.frameDescriptorSets[i].skyboxUniformDescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &radianceImageInfo,
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = scene72.frameDescriptorSets[i].skyboxUniformDescriptorSet,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &lambertianImageInfo,
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = scene72.frameDescriptorSets[i].skyboxUniformDescriptorSet,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &environmentBRDFImageInfo,
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
			};
			vkUpdateDescriptorSets(*this->context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
	}
	// Create material level descriptor sets
	{
		for (const auto& object : scene72.graph) {
			if (object->type == "MATERIAL") {
				s72::Material::Ptr material = std::reinterpret_pointer_cast<s72::Material>(object);
				std::vector<VkDescriptorSetLayout> layouts;
				if (material->materialType == "simple") {
					continue;
				}
				if (material->materialType == "mirror") {
					layouts.resize(Engine::MAX_FRAMES_IN_FLIGHT, this->mirrorMaterialLevelUniformDescriptorSetLayout);
				}
				else if (material->materialType == "environment") {
					layouts.resize(Engine::MAX_FRAMES_IN_FLIGHT, this->environmentMaterialLevelUniformDescriptorSetLayout);
				}
				else if (material->materialType == "lambertian") {
					layouts.resize(Engine::MAX_FRAMES_IN_FLIGHT, this->lambertianMaterialLevelUniformDescriptorSetLayout);
				}
				else if (material->materialType == "pbr") {
					layouts.resize(Engine::MAX_FRAMES_IN_FLIGHT, this->pbrMaterialLevelUniformDescriptorSetLayout);
				};
				VkDescriptorSetAllocateInfo allocInfo{
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.pNext = nullptr,
					.descriptorPool = scene72.descriptorPool,
					.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
					.pSetLayouts = layouts.data()
				};
				std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> materialLevelUniformDescriptorSets;
				JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(*this->context.device(), &allocInfo, materialLevelUniformDescriptorSets.data()));
				for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
					scene72.frameDescriptorSets[i].materialLevelUniformDescriptorSets[material->idx] = materialLevelUniformDescriptorSets[i];
				}
				for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
					std::vector<VkWriteDescriptorSet> descriptorWrites;
					std::vector<VkDescriptorImageInfo> imageInfos;
					descriptorWrites.reserve(material->numTextures());
					imageInfos.reserve(material->numTextures());
					for (std::uint32_t j = 0; j < material->numTextures(); ++j) {
						VkDescriptorImageInfo imageInfo{
							.sampler = material->texture(j).sampler(),
							.imageView = material->texture(j).imageView(),
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
						};
						imageInfos.push_back(imageInfo);
						VkWriteDescriptorSet descriptorWrite{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = scene72.frameDescriptorSets[i].materialLevelUniformDescriptorSets[material->idx],
							.dstBinding = j,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &imageInfos.back(),
							.pBufferInfo = nullptr,
							.pTexelBufferView = nullptr
						};
						descriptorWrites.push_back(descriptorWrite);
					}
					vkUpdateDescriptorSets(*this->context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
				}
			}
		}
	}
	return pScene72;
}

void Engine::destroy(s72::Scene72& scene72) {
	vkDeviceWaitIdle(*this->context.device());
	// Destroy vertex buffer and textures
	for (const auto& object : scene72.graph) {
		if (object->type == "MESH") {
			s72::Mesh::Ptr mesh = std::reinterpret_pointer_cast<s72::Mesh>(object);
			this->allocator.free(mesh->vertexBufferMemory);
			vkDestroyBuffer(*this->context.device(), mesh->vertexBuffer, nullptr);
			mesh->vertexBuffer = nullptr;
		}
		else if (object->type == "MATERIAL") {
			s72::Material::Ptr material = std::reinterpret_pointer_cast<s72::Material>(object);
			for (std::uint32_t i = 0; i < material->numTextures(); ++i)
				material->texture(i).destroy();
		}
		else if (object->type == "ENVIRONMENT") {
			s72::Environment::Ptr environment = std::reinterpret_pointer_cast<s72::Environment>(object);
			for (std::uint32_t i = 0; i < environment->numTextures(); ++i)
				environment->texture(i).destroy();
		}
	}
	scene72.cameras.clear();
	scene72.meshes.clear();
	scene72.drivers.clear();
	scene72.scene.reset();
	bool hasEnvironment = (scene72.environment != nullptr);
	scene72.environment.reset();
	scene72.defaultMaterial.reset();
	scene72.graph.clear();
	scene72.currPlayTime = scene72.minTime = scene72.maxTime = 0.0f;
	if (scene72.descriptorPool == nullptr)
		return;
	// Destroy shadow map sampler
	scene72.spotLightShadowMapSampler.clear();
	// Destroy uniform buffers
	for (int i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; ++i) {
		this->allocator.unmap(scene72.frameDescriptorSets[i].viewLevelUniformBufferMemory);
		vkDestroyBuffer(*this->context.device(), scene72.frameDescriptorSets[i].viewLevelUniformBuffer, nullptr);
		this->allocator.free(scene72.frameDescriptorSets[i].viewLevelUniformBufferMemory);
		this->allocator.unmap(scene72.frameDescriptorSets[i].lightsBufferMemory);
		scene72.frameDescriptorSets[i].lightsBuffer.clear();
		this->allocator.free(scene72.frameDescriptorSets[i].lightsBufferMemory);
		this->allocator.unmap(scene72.frameDescriptorSets[i].objectLevelUniformBufferMemory);
		vkDestroyBuffer(*this->context.device(), scene72.frameDescriptorSets[i].objectLevelUniformBuffer, nullptr);
		this->allocator.free(scene72.frameDescriptorSets[i].objectLevelUniformBufferMemory);
		if (hasEnvironment) {
			this->allocator.unmap(scene72.frameDescriptorSets[i].skyboxUniformBufferMemory);
			vkDestroyBuffer(*this->context.device(), scene72.frameDescriptorSets[i].skyboxUniformBuffer, nullptr);
			this->allocator.free(scene72.frameDescriptorSets[i].skyboxUniformBufferMemory);
		}
	}
	// Destroy descriptor pool
	vkDestroyDescriptorPool(*this->context.device(), scene72.descriptorPool, nullptr);
	// Destroy shadow map
	scene72.sunLightShadowMaps.clear();
	scene72.sphereLightShadowMaps.clear();
	scene72.spotLightShadowMaps.clear();
	scene72.descriptorPool = nullptr;
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