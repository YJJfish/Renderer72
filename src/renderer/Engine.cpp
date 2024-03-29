#include "Engine.hpp"
#include <fstream>
#include <exception>
#include <stdexcept>
#include "Scene72.hpp"
#include "Culling.hpp"

void Engine::setCameraMode(CameraMode cameraMode, std::optional<std::string> camera) {
	switch (cameraMode) {
	case CameraMode::USER:
		break;
	case CameraMode::SCENE:
	case CameraMode::DEBUG:
		if (this->pScene72 == nullptr)
			throw std::runtime_error("Please firstly set the scene before setting camera mode.");
		if (!camera.has_value())
			throw std::runtime_error("Please specify the camera name for SCENE or DEBUG mode.");
		if (this->pScene72->cameras.find(*camera) == this->pScene72->cameras.end())
			throw std::runtime_error("Cannot find camera \"" + *camera + "\".");
		this->cameraName = *camera;
		break;
	default:
		throw std::runtime_error("Unknown camera mode.");
	}
	this->cameraMode = cameraMode;
}

void Engine::drawFrame() {
	// Compute play time
	float now = this->clock->now();
	if (!this->paused) {
		this->currPlayTime += this->playRate * (now - this->currClockTime);
		if (this->playMode == PlayMode::CYCLE && this->currPlayTime > this->pScene72->maxTime) {
			this->currPlayTime = this->pScene72->minTime;
			this->pScene72->reset();
		}
	}
	this->currClockTime = now;
	vkWaitForFences(*this->context.device(), 1, &this->frameData[this->currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	if (!this->offscreen) {
		VkResult acquireImageResult = vkAcquireNextImageKHR(*this->context.device(), *this->swapchain.swapchain(), UINT64_MAX, this->frameData[this->currentFrame].imageAvailableSemaphore, nullptr, &imageIndex);

		if (acquireImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
			this->handleFramebufferResizing();
			return;
		}
		else if (acquireImageResult == VK_SUBOPTIMAL_KHR) {
		}
		JJYOU_VK_UTILS_CHECK(acquireImageResult);
	}
	else {
		JJYOU_VK_UTILS_CHECK(this->virtualSwapchain.acquireNextImage(&imageIndex));
	}
	

	vkResetFences(*this->context.device(), 1, &this->frameData[this->currentFrame].inFlightFence);

	vkResetCommandBuffer(this->frameData[this->currentFrame].graphicsCommandBuffer, 0);

	VkExtent2D screenExtent = this->offscreen ? this->virtualSwapchain.extent() : static_cast<VkExtent2D>(this->swapchain.extent());
	
	// Traverse the scene to get the instances to render, the environment model matrix, and camera transforms
	struct InstanceToDraw {
		jjyou::glsl::mat4 transform;
		s72::Mesh::Ptr mesh;
	};
	SkyboxUniform skyboxUniform{
		.model = jjyou::glsl::mat4(1.0f)
	};
	struct CameraInfo {
		float aspectRatio;
		jjyou::glsl::mat4 projection;
		jjyou::glsl::mat4 view;
	};
	std::vector<InstanceToDraw> simpleInstances;
	std::vector<InstanceToDraw> mirrorInstances;
	std::vector<InstanceToDraw> environmentInstances;
	std::vector<InstanceToDraw> lambertianInstances;
	std::vector<InstanceToDraw> pbrInstances;
	Lights lights{};
	std::array<SpotLightShadowMapUniform, Engine::MAX_NUM_SPOT_LIGHTS> spotLightShadowMapUniforms{};
	std::array<SphereLightShadowMapUniform, Engine::MAX_NUM_SPHERE_LIGHTS> sphereLightShadowMapUniforms{};
	std::array<SunLightShadowMapUniform, Engine::MAX_NUM_SUN_LIGHTS> sunLightShadowMapUniforms{};
	std::unordered_map<std::string, CameraInfo> cameraInfos;
	std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)> traverseSceneVisitor =
		[&](s72::Node::Ptr node, const jjyou::glsl::mat4& transform) -> bool {
		if (!node->mesh.expired()) {
			s72::Mesh::Ptr mesh = node->mesh.lock();
			InstanceToDraw instanceToDraw{ .transform = transform, .mesh = mesh };
			if (mesh->material.lock()->materialType == "simple")
				simpleInstances.push_back(instanceToDraw);
			else if (mesh->material.lock()->materialType == "mirror")
				mirrorInstances.push_back(instanceToDraw);
			else if (mesh->material.lock()->materialType == "environment")
				environmentInstances.push_back(instanceToDraw);
			else if (mesh->material.lock()->materialType == "lambertian")
				lambertianInstances.push_back(instanceToDraw);
			else if (mesh->material.lock()->materialType == "pbr")
				pbrInstances.push_back(instanceToDraw);
		}
		if (!node->environment.expired()) {
			skyboxUniform.model = jjyou::glsl::inverse(jjyou::glsl::mat3(transform));
		}
		if (!node->camera.expired()) {
			s72::Camera::Ptr camera = node->camera.lock();
			cameraInfos.emplace(
				camera->name,
				CameraInfo{
					.aspectRatio = camera->getAspectRatio(),
					.projection = camera->getProjectionMatrix(),
					.view = jjyou::glsl::inverse(transform)
				}
			);
		}
		if (!node->light.expired()) {
			s72::Light::Ptr light = node->light.lock();
			if (light->lightType == "sun") {
				s72::SunLight::Ptr sunLight = std::reinterpret_pointer_cast<s72::SunLight>(light);
				jjyou::glsl::vec3 direction = jjyou::glsl::normalized(jjyou::glsl::vec3(transform[2]));
				jjyou::glsl::vec3 orthoX{ 1.0f, 0.0f, 0.0f };
				if (jjyou::glsl::norm(orthoX - direction) <= 1e-1f)
					orthoX = jjyou::glsl::vec3(0.0f, 1.0f, 0.0f);
				jjyou::glsl::vec3 orthoY = jjyou::glsl::cross(-direction, orthoX);
				orthoX = jjyou::glsl::cross(orthoY, -direction);
				Engine::SunLight sunLightUniform{
					.cascadeSplits = {}, // To set
					.orthographic = {}, // To set
					.direction = direction,
					.angle = sunLight->angle,
					.tint = sunLight->tint * sunLight->strength,
					.shadow = static_cast<int>(sunLight->shadow)
				};
				Engine::SunLightShadowMapUniform sunLightShadowMapUniform{
					.orthoX = orthoX,
					.orthoY = orthoY,
					.center = {}, // To set
					.width = {}, // To set
					.height = {}, // To set
					.zNear = {}, // To set
					.zFar = {} // To set
				};
				if (sunLight->shadow == 0) {
					lights.sunLightsNoShadow[lights.numSunLightsNoShadow] = sunLightUniform;
					++lights.numSunLightsNoShadow;
				}
				else {
					lights.sunLights[lights.numSunLights] = sunLightUniform;
					sunLightShadowMapUniforms[lights.numSunLights] = sunLightShadowMapUniform;
					++lights.numSunLights;
				}
			}
			else if (light->lightType == "sphere") {
				s72::SphereLight::Ptr sphereLight = std::reinterpret_pointer_cast<s72::SphereLight>(light);
				Engine::SphereLight sphereLightUniform{
					.position = jjyou::glsl::vec3(transform[3]),
					.radius = sphereLight->radius,
					.tint = sphereLight->tint * sphereLight->power,
					.limit = sphereLight->limit
				};
				Engine::SphereLightShadowMapUniform sphereLightShadowMapUniform{
					.position = sphereLightUniform.position,
					.radius = sphereLightUniform.radius,
					.perspective = jjyou::glsl::perspective(std::numbers::pi_v<float> / 2.0f, 1.0f, sphereLightUniform.radius / std::sqrt(2.0f), sphereLightUniform.limit),
					.limit = sphereLightUniform.limit
				};
				if (sphereLight->shadow == 0) {
					lights.sphereLightsNoShadow[lights.numSphereLightsNoShadow] = sphereLightUniform;
					++lights.numSphereLightsNoShadow;
				}
				else {
					lights.sphereLights[lights.numSphereLights] = sphereLightUniform;
					sphereLightShadowMapUniforms[lights.numSphereLights] = sphereLightShadowMapUniform;
					++lights.numSphereLights;
				}
			}
			else if (light->lightType == "spot") {
				s72::SpotLight::Ptr spotLight = std::reinterpret_pointer_cast<s72::SpotLight>(light);
				jjyou::glsl::mat4 invZ = jjyou::glsl::mat4(1.0f); invZ[2][2] = -1.0f; invZ[0][0] = -1.0f;
				Engine::SpotLight spotLightUniform{
					.perspective = jjyou::glsl::perspective(spotLight->fov, 1.0f, std::cos(spotLight->fov / 2.0f) * spotLight->radius, spotLight->limit) * invZ * jjyou::glsl::inverse(transform),
					.position = jjyou::glsl::vec3(transform[3]),
					.radius = spotLight->radius,
					.direction = jjyou::glsl::normalized(jjyou::glsl::vec3(transform[2])),
					.fov = spotLight->fov,
					.tint = spotLight->tint * spotLight->power,
					.blend = spotLight->blend,
					.limit = spotLight->limit,
					.shadow = static_cast<int>(spotLight->shadow)
				};
				Engine::SpotLightShadowMapUniform spotLightShadowMapUniform{
					.perspective = spotLightUniform.perspective,
				};
				if (spotLight->shadow == 0) {
					lights.spotLightsNoShadow[lights.numSpotLightsNoShadow] = spotLightUniform;
					++lights.numSpotLightsNoShadow;
				}
				else {
					lights.spotLights[lights.numSpotLights] = spotLightUniform;
					spotLightShadowMapUniforms[lights.numSpotLights] = spotLightShadowMapUniform;
					++lights.numSpotLights;
				}
			}
		}
		return true;
		};
	if (this->pScene72 != nullptr) {
		//Scene72 are "+z" up, however in our coordinate the scene is "-y" up
		jjyou::glsl::mat4 rootTransform;
		rootTransform[0][0] = 1.0f;
		rootTransform[2][1] = -1.0f;
		rootTransform[1][2] = 1.0f;
		rootTransform[3][3] = 1.0f;
		this->pScene72->traverse(
			this->currPlayTime,
			rootTransform,
			traverseSceneVisitor
		);
	}

	// Get view matrices and culling matrices
	jjyou::glsl::mat4 viewingProjection;
	jjyou::glsl::mat4 viewingView;
	float viewingAspectRatio{};
	jjyou::glsl::mat4 debugProjection;
	jjyou::glsl::mat4 debugView;
	float debugNearZ{}, debugFarZ{};
	if (this->cameraMode == CameraMode::USER) {
		viewingAspectRatio = static_cast<float>(screenExtent.width) / screenExtent.height;
		viewingProjection = jjyou::glsl::perspective(jjyou::glsl::radians(45.0f), viewingAspectRatio, 0.01f, 5000.0f);
		viewingView = this->sceneViewer.getViewMatrix();
		debugProjection = viewingProjection;
		debugView = viewingView;
		debugNearZ = 0.01f;
		debugFarZ = 5000.0f;
	}
	else if (this->cameraMode == CameraMode::SCENE) {
		viewingAspectRatio = cameraInfos[this->cameraName].aspectRatio;
		viewingProjection = cameraInfos[this->cameraName].projection;
		viewingView = cameraInfos[this->cameraName].view;
		debugProjection = viewingProjection;
		debugView = viewingView;
		debugNearZ = std::reinterpret_pointer_cast<s72::PerspectiveCamera>(this->pScene72->cameras[this->cameraName])->zNear;
		debugFarZ = std::reinterpret_pointer_cast<s72::PerspectiveCamera>(this->pScene72->cameras[this->cameraName])->zFar;
	}
	else if (this->cameraMode == CameraMode::DEBUG) {
		viewingAspectRatio = static_cast<float>(screenExtent.width) / screenExtent.height;;
		viewingProjection = jjyou::glsl::perspective(jjyou::glsl::radians(45.0f), viewingAspectRatio, 0.01f, 5000.0f);
		viewingView = this->sceneViewer.getViewMatrix();
		debugProjection = cameraInfos[this->cameraName].projection;
		debugView = cameraInfos[this->cameraName].view;
		debugNearZ = std::reinterpret_pointer_cast<s72::PerspectiveCamera>(this->pScene72->cameras[this->cameraName])->zNear;
		debugFarZ = std::reinterpret_pointer_cast<s72::PerspectiveCamera>(this->pScene72->cameras[this->cameraName])->zFar;
	}

	// Compute sun light shadow map parameters (because this is dependent on the viewing camera)
	if (lights.numSunLights > 0) {
		// Compute cascade splits
		std::array<float, Engine::NUM_CASCADE_LEVELS> cascadeSplits{};
		// Reference: https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		for (uint32_t i = 0; i < Engine::NUM_CASCADE_LEVELS; ++i) {
			float p = static_cast<float>(i + 1) / static_cast<float>(Engine::NUM_CASCADE_LEVELS);
			float log = debugNearZ * std::pow(debugFarZ / debugNearZ, p);
			float uniform = debugNearZ + (debugFarZ - debugNearZ) * p;
			float d = 0.94f * (log - uniform) + uniform;
			cascadeSplits[i] = d;
		}
		// For each sun light
		for (int i = 0; i < lights.numSunLights; ++i) {
			lights.sunLights[i].cascadeSplits = cascadeSplits;
			jjyou::glsl::mat3 lightSpaceRotation = jjyou::glsl::transpose(jjyou::glsl::mat3(
				sunLightShadowMapUniforms[i].orthoX,
				sunLightShadowMapUniforms[i].orthoY,
				-lights.sunLights[i].direction
			));
			for (std::uint32_t l = 0; l < Engine::NUM_CASCADE_LEVELS; ++l) {
				float lastSplit = (l == 0) ? 0.0f : (cascadeSplits[l - 1] - debugNearZ) / (debugFarZ - debugNearZ);
				float currSplit = (cascadeSplits[l] - debugNearZ) / (debugFarZ - debugNearZ);
				// Back-project frustum corners to world space, then transform to base light space
				std::array<jjyou::glsl::vec3, 8> corners = { {
					jjyou::glsl::vec3(-1.0f,  1.0f, 0.0f),
					jjyou::glsl::vec3(1.0f,  1.0f, 0.0f),
					jjyou::glsl::vec3(1.0f, -1.0f, 0.0f),
					jjyou::glsl::vec3(-1.0f, -1.0f, 0.0f),
					jjyou::glsl::vec3(-1.0f,  1.0f,  1.0f),
					jjyou::glsl::vec3(1.0f,  1.0f,  1.0f),
					jjyou::glsl::vec3(1.0f, -1.0f,  1.0f),
					jjyou::glsl::vec3(-1.0f, -1.0f,  1.0f),
				} };
				jjyou::glsl::mat4 invCamera = jjyou::glsl::inverse(debugProjection * debugView);
				for (auto& corner : corners) {
					jjyou::glsl::vec4 tmp = invCamera * jjyou::glsl::vec4(corner, 1.0f);
					corner = jjyou::glsl::vec3(tmp / tmp.w);
				}
				for (std::size_t c = 0; c < 4; ++c) {
					jjyou::glsl::vec3 dist = corners[c + 4] - corners[c];
					corners[c] = corners[c] + lastSplit * dist;
					corners[c + 4] = corners[c] + currSplit * dist;
				}
				for (auto& corner : corners) {
					corner = lightSpaceRotation * corner;
				}
				/*std::cout << "level " << l << ":";
				for (auto& corner : corners) {
					std::cout << ", [";
					std::cout << corner.x << ", " << corner.y << ", " << corner.z << "]";
				}
				std::cout << std::endl;*/
				// Get the range
				jjyou::glsl::vec3 min(std::numeric_limits<float>::max());
				jjyou::glsl::vec3 max(std::numeric_limits<float>::lowest());
				for (auto& corner : corners) {
					min = jjyou::glsl::min(min, corner);
					max = jjyou::glsl::max(max, corner);
				}
				// Objects outside of the camera frustum can also result in shadows.
				jjyou::glsl::vec3 tmpRange = max.z - min.z;
				min.z -= tmpRange.z * 10.0f;
				min.x -= tmpRange.x * 0.05f;
				min.y -= tmpRange.y * 0.05f;
				max.x += tmpRange.x * 0.05f;
				max.y += tmpRange.y * 0.05f;
				// Now fill the light parameters.
				jjyou::glsl::mat4 lightSpace = jjyou::glsl::mat4(lightSpaceRotation);
				jjyou::glsl::vec2 center((min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f);
				float width = max.x - min.x;
				float height = max.y - min.y;
				lightSpace[3].x = -center.x;
				lightSpace[3].y = -center.y;
				lights.sunLights[i].orthographic[l] = jjyou::glsl::orthographic(width, height, min.z, max.z) * lightSpace;
				sunLightShadowMapUniforms[i].center[l] = center;
				sunLightShadowMapUniforms[i].width[l] = width;
				sunLightShadowMapUniforms[i].height[l] = height;
				sunLightShadowMapUniforms[i].zNear[l] = min.z;
				sunLightShadowMapUniforms[i].zFar[l] = max.z;
			}
		}
	}
	
	// Compute dynamic uniform buffer offset
	VkDeviceSize minAlignment = this->context.physicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
	VkDeviceSize dynamicBufferOffset = sizeof(Engine::ObjectLevelUniform);
	if (minAlignment > 0)
		dynamicBufferOffset = (dynamicBufferOffset + minAlignment - 1) & ~(minAlignment - 1);
	// Fill model matrix dynamic uniform buffer
	std::size_t instanceCount = 0;
	for (const auto& instancesToDraw : { std::cref(simpleInstances), std::cref(mirrorInstances), std::cref(environmentInstances), std::cref(lambertianInstances), std::cref(pbrInstances) }) {
		for (const auto& instanceToDraw : instancesToDraw.get()) {
			VkDeviceSize vertexBufferOffsets = 0;
			std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
			instanceCount++;
			Engine::ObjectLevelUniform objectLevelUniform{
				.model = instanceToDraw.transform,
				.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
			};
			char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
			memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
		}
	}
	
	// Record command buffer
	{
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = nullptr,
			.flags = 0,
			.pInheritanceInfo = nullptr
		};

		JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(this->frameData[this->currentFrame].graphicsCommandBuffer, &beginInfo));

		// Compute shadow mapping
		for (int i = 0; i < lights.numSpotLights; ++i) {
			VkClearValue clearValue = VkClearValue{
				.depthStencil = { 1.0f, 0 }
			};
			VkRenderPassBeginInfo renderPassInfo{
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.pNext = nullptr,
				.renderPass = *this->shadowMappingRenderPass,
				.framebuffer = *this->pScene72->spotLightShadowMaps[i].framebuffer(),
				.renderArea = {
					.offset = {0, 0},
					.extent = this->pScene72->spotLightShadowMaps[i].extent()
				},
				.clearValueCount = 1U,
				.pClearValues = &clearValue
			};
			vkCmdBeginRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->spotlightPipeline);
			VkViewport shadowMappingViewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<float>(this->pScene72->spotLightShadowMaps[i].extent().width),
				.height = static_cast<float>(this->pScene72->spotLightShadowMaps[i].extent().height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f
			};
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingViewport);
			VkRect2D shadowMappingScissor{
				.offset = { 0, 0 },
				.extent = this->pScene72->spotLightShadowMaps[i].extent(),
			};
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingScissor);
			vkCmdPushConstants(this->frameData[this->currentFrame].graphicsCommandBuffer, this->spotlightPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(Engine::SpotLightShadowMapUniform), &spotLightShadowMapUniforms[i]);
			instanceCount = static_cast<std::uint32_t>(simpleInstances.size()); // Skip simple material
			for (const auto& instancesToDraw : { std::cref(mirrorInstances), std::cref(environmentInstances), std::cref(lambertianInstances), std::cref(pbrInstances) }) {
				for (const auto& instanceToDraw : instancesToDraw.get()) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->spotlightPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
			vkCmdEndRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer);
		}
		for (int i = 0; i < lights.numSphereLights; ++i) {
			VkClearValue clearValue = VkClearValue{
				.depthStencil = { 1.0f, 0 }
			};
			VkRenderPassBeginInfo renderPassInfo{
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.pNext = nullptr,
				.renderPass = *this->shadowMappingRenderPass,
				.framebuffer = *this->pScene72->sphereLightShadowMaps[i].framebuffer(),
				.renderArea = {
					.offset = {0, 0},
					.extent = this->pScene72->sphereLightShadowMaps[i].extent()
				},
				.clearValueCount = 1U,
				.pClearValues = &clearValue
			};
			vkCmdBeginRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->spherelightPipeline);
			VkViewport shadowMappingViewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<float>(this->pScene72->sphereLightShadowMaps[i].extent().width),
				.height = static_cast<float>(this->pScene72->sphereLightShadowMaps[i].extent().height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f
			};
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingViewport);
			VkRect2D shadowMappingScissor{
				.offset = { 0, 0 },
				.extent = this->pScene72->sphereLightShadowMaps[i].extent(),
			};
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingScissor);
			vkCmdPushConstants(this->frameData[this->currentFrame].graphicsCommandBuffer, this->spherelightPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(Engine::SphereLightShadowMapUniform), &sphereLightShadowMapUniforms[i]);
			instanceCount = static_cast<std::uint32_t>(simpleInstances.size()); // Skip simple material
			for (const auto& instancesToDraw : { std::cref(mirrorInstances), std::cref(environmentInstances), std::cref(lambertianInstances), std::cref(pbrInstances) }) {
				for (const auto& instanceToDraw : instancesToDraw.get()) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->spherelightPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
			vkCmdEndRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer);
		}
		for (int i = 0; i < lights.numSunLights; ++i) {
			VkClearValue clearValue = VkClearValue{
				.depthStencil = { 1.0f, 0 }
			};
			VkRenderPassBeginInfo renderPassInfo{
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.pNext = nullptr,
				.renderPass = *this->shadowMappingRenderPass,
				.framebuffer = *this->pScene72->sunLightShadowMaps[i].framebuffer(),
				.renderArea = {
					.offset = {0, 0},
					.extent = this->pScene72->sunLightShadowMaps[i].extent()
				},
				.clearValueCount = 1U,
				.pClearValues = &clearValue
			};
			vkCmdBeginRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->sunlightPipeline);
			VkViewport shadowMappingViewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<float>(this->pScene72->sunLightShadowMaps[i].extent().width),
				.height = static_cast<float>(this->pScene72->sunLightShadowMaps[i].extent().height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f
			};
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingViewport);
			VkRect2D shadowMappingScissor{
				.offset = { 0, 0 },
				.extent = this->pScene72->sunLightShadowMaps[i].extent(),
			};
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &shadowMappingScissor);
			vkCmdPushConstants(this->frameData[this->currentFrame].graphicsCommandBuffer, this->sunlightPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT, 0U, sizeof(Engine::SunLightShadowMapUniform), &sunLightShadowMapUniforms[i]);
			instanceCount = static_cast<std::uint32_t>(simpleInstances.size()); // Skip simple material
			for (const auto& instancesToDraw : { std::cref(mirrorInstances), std::cref(environmentInstances), std::cref(lambertianInstances), std::cref(pbrInstances) }) {
				for (const auto& instanceToDraw : instancesToDraw.get()) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->sunlightPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
			vkCmdEndRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer);
		}

		// Draw the scene

		// Set viewport and scissor.
		// This is easy for the scene/debug camera.
		// But for user cameras, the viewport needs to be computed according to the camera parameters.
		float viewPortWidth = static_cast<float>(screenExtent.width);
		float viewPortHeight = static_cast<float>(screenExtent.height);
		if (viewPortWidth / viewPortHeight < viewingAspectRatio)
			viewPortHeight = viewPortWidth / viewingAspectRatio;
		else if (viewPortWidth / viewPortHeight > viewingAspectRatio)
			viewPortWidth = viewPortHeight * viewingAspectRatio;
		VkViewport sceneViewport{
			.x = (screenExtent.width - viewPortWidth) / 2.0f,
			.y = (screenExtent.height - viewPortHeight) / 2.0f,
			.width = viewPortWidth,
			.height = viewPortHeight,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		VkRect2D sceneScissor{
			.offset = { 0, 0 },
			.extent = screenExtent,
		};
		// Copy uniform buffer
		Engine::ViewLevelUniform viewLevelUniform{
			.projection = viewingProjection,
			.view = viewingView,
			.viewPos = -jjyou::glsl::vec4(jjyou::glsl::transpose(jjyou::glsl::mat3(viewingView)) * jjyou::glsl::vec3(viewLevelUniform.view[3]), 1.0f)
		};
		memcpy(this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformBufferMemory.mappedAddress(), &viewLevelUniform, sizeof(Engine::ViewLevelUniform));
		if (this->pScene72->environment) {
			memcpy(this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformBufferMemory.mappedAddress(), &skyboxUniform, sizeof(Engine::SkyboxUniform));
		}
		memcpy(this->pScene72->frameDescriptorSets[this->currentFrame].lightsBufferMemory.mappedAddress(), &lights, sizeof(Engine::Lights));

		std::array<VkClearValue, 2> clearValues{ {
			VkClearValue{
				.color = { {0.0f, 0.0f, 0.0f, 1.0f} }
			},
			VkClearValue{
				.depthStencil = { 1.0f, 0 }
			}
		} };
		VkRenderPassBeginInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.pNext = nullptr,
			.renderPass = this->renderPass,
			.framebuffer = this->framebuffers[imageIndex],
			.renderArea = {
				.offset = {0, 0},
				.extent = screenExtent
			},
			.clearValueCount = static_cast<uint32_t>(clearValues.size()),
			.pClearValues = clearValues.data()
		};

		vkCmdBeginRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		if (this->pScene72->environment) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->skyboxPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->skyboxPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->skyboxPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformDescriptorSet, 0, nullptr);
			vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, 36, 1, 0, 0);
		}

		instanceCount = 0;
		if (!simpleInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : simpleInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(debugProjection, debugView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
				instanceCount++;
			}
		}
		if (!mirrorInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 3, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : mirrorInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(debugProjection, debugView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
				instanceCount++;
			}
		}
		if (!environmentInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 3, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : environmentInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(debugProjection, debugView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
				instanceCount++;
			}
		}
		if (!lambertianInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 3, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : lambertianInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(debugProjection, debugView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
				instanceCount++;
			}
		}
		if (!pbrInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneViewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &sceneScissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 3, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].skyboxUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : pbrInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(debugProjection, debugView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
				instanceCount++;
			}
		}
		// Finish
		vkCmdEndRenderPass(this->frameData[this->currentFrame].graphicsCommandBuffer);

		JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(this->frameData[this->currentFrame].graphicsCommandBuffer));
	}

	// Submit
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = (this->offscreen) ? 0U : 1U,
		.pWaitSemaphores = (this->offscreen) ? nullptr : &this->frameData[this->currentFrame].imageAvailableSemaphore,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &this->frameData[this->currentFrame].graphicsCommandBuffer,
		.signalSemaphoreCount = (this->offscreen) ? 0U : 1U,
		.pSignalSemaphores = (this->offscreen) ? nullptr : &this->frameData[this->currentFrame].renderFinishedSemaphore
	};
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(**this->context.queue(jjyou::vk::Context::QueueType::Main), 1, &submitInfo, this->frameData[this->currentFrame].inFlightFence));

	if (!this->offscreen) {
		VkSwapchainKHR swapChains[] = { *this->swapchain.swapchain() };
		VkPresentInfoKHR presentInfo{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &this->frameData[this->currentFrame].renderFinishedSemaphore,
			.swapchainCount = 1,
			.pSwapchains = swapChains,
			.pImageIndices = &imageIndex,
			.pResults = nullptr
		};
		VkResult presentResult = vkQueuePresentKHR(**this->context.queue(jjyou::vk::Context::QueueType::Main), &presentInfo);

		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized) {
			framebufferResized = false;
			this->handleFramebufferResizing();
		}
		else
			JJYOU_VK_UTILS_CHECK(presentResult);
	}
	this->currentFrame = (this->currentFrame + 1) % Engine::MAX_FRAMES_IN_FLIGHT;
	return;
}


HostImage Engine::getLastRenderedFrame(void) {
	if (!this->offscreen)
		return HostImage{};
	int lastFrame = (this->currentFrame + Engine::MAX_FRAMES_IN_FLIGHT - 1) % Engine::MAX_FRAMES_IN_FLIGHT;
	vkWaitForFences(*this->context.device(), 1, &this->frameData[lastFrame].inFlightFence, VK_TRUE, UINT64_MAX);
	std::uint32_t imageIndex;
	JJYOU_VK_UTILS_CHECK(this->virtualSwapchain.acquireLastImage(&imageIndex));
	// create host visible and host coherent image
	VkImage image;
	jjyou::vk::Memory imageMemory;
	std::tie(image, imageMemory) = this->createImage(
		this->virtualSwapchain.extent().width,
		this->virtualSwapchain.extent().height,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_TILING_LINEAR,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main), *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Transfer) },
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	// create transfer command buffer
	VkCommandBuffer transferCommandBuffer;
	VkCommandBufferAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = this->transferCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(*this->context.device(), &allocInfo, &transferCommandBuffer));
	// begin command buffer
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr
	};
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(transferCommandBuffer, &beginInfo));
	// transition destination image to transfer destination layout
	this->insertImageMemoryBarrier(
		transferCommandBuffer,
		image,
		0,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);
	// copy image
	VkImageCopy copyInfo{
		.srcSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.srcOffset = {
			.x = 0,
			.y = 0,
			.z = 0
		},
		.dstSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.dstOffset = {
			.x = 0,
			.y = 0,
			.z = 0
		},
		.extent = {
			.width = this->virtualSwapchain.extent().width,
			.height = this->virtualSwapchain.extent().height,
			.depth = 1
		}
	};
	vkCmdCopyImage(
		transferCommandBuffer,
		this->virtualSwapchain.images()[imageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&copyInfo
	);
	// transition destination image to general layout, which is the required layout for mapping the image memory later on
	this->insertImageMemoryBarrier(
		transferCommandBuffer,
		image,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_MEMORY_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
	// end command buffer
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(transferCommandBuffer));
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = 0,
		.commandBufferCount = 1,
		.pCommandBuffers = &transferCommandBuffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr
	};
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(**this->context.queue(jjyou::vk::Context::QueueType::Transfer), 1, &submitInfo, nullptr));
	JJYOU_VK_UTILS_CHECK(vkQueueWaitIdle(**this->context.queue(jjyou::vk::Context::QueueType::Transfer)));
	vkFreeCommandBuffers(*this->context.device(), this->transferCommandPool, 1, &transferCommandBuffer);
	// copy buffer to cpu memory
	// Get layout of the image (including row pitch)
	VkImageSubresource subResource{};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout subResourceLayout;
	vkGetImageSubresourceLayout(*this->context.device(), image, &subResource, &subResourceLayout);
	this->allocator.map(imageMemory);
	HostImage hostImage(this->virtualSwapchain.extent().width, this->virtualSwapchain.extent().height);
	for (std::uint32_t r = 0; r < this->virtualSwapchain.extent().height; ++r) {
		for (std::uint32_t c = 0; c < this->virtualSwapchain.extent().width; ++c) {
			auto& pixel = hostImage.at(r, c);
			unsigned char* pData = reinterpret_cast<unsigned char*>(imageMemory.mappedAddress()) + subResourceLayout.offset + r * subResourceLayout.rowPitch + c * 4;
			pixel[0] = pData[0];
			pixel[1] = pData[1];
			pixel[2] = pData[2];
			pixel[3] = pData[3];
		}
	}
	this->allocator.unmap(imageMemory);
	this->allocator.free(imageMemory);
	vkDestroyImage(*this->context.device(), image, nullptr);
	// return
	return hostImage;
}

void Engine::handleFramebufferResizing(void) {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwWaitEvents();
		glfwGetFramebufferSize(window, &width, &height);
	}
	vkDeviceWaitIdle(*this->context.device());

	// Destroy frame buffers
	for (int i = 0; i < this->framebuffers.size(); ++i) {
		vkDestroyFramebuffer(*this->context.device(), this->framebuffers[i], nullptr);
	}

	// Destroy depth image
	vkDestroyImageView(*this->context.device(), this->depthImageView, nullptr);
	vkDestroyImage(*this->context.device(), this->depthImage, nullptr);
	this->allocator.free(this->depthImageMemory);

	this->createSwapchain();
	this->createDepthImage();
	this->createFramebuffers();
}

void Engine::setScene(s72::Scene72::Ptr pScene72) {
	pScene72->reset();
	this->pScene72 = pScene72;
	this->currPlayTime = pScene72->minTime;
	this->resetClockTime();
}