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
	vkWaitForFences(this->device.get(), 1, &this->frameData[this->currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	if (!this->offscreen) {
		VkResult acquireImageResult = vkAcquireNextImageKHR(this->device.get(), this->swapchain.get(), UINT64_MAX, this->frameData[this->currentFrame].imageAvailableSemaphore, nullptr, &imageIndex);

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
	

	vkResetFences(this->device.get(), 1, &this->frameData[this->currentFrame].inFlightFence);

	vkResetCommandBuffer(this->frameData[this->currentFrame].graphicsCommandBuffer, 0);

	VkExtent2D screenExtent = this->offscreen ? this->virtualSwapchain.extent() : this->swapchain.extent();
	// Record command buffer
	{
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = nullptr,
			.flags = 0,
			.pInheritanceInfo = nullptr
		};

		JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(this->frameData[this->currentFrame].graphicsCommandBuffer, &beginInfo));

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

		// Get the instances to render
		struct InstanceToDraw {
			jjyou::glsl::mat4 transform;
			s72::Mesh::Ptr mesh;
		};
		std::vector<InstanceToDraw> simpleInstances;
		std::vector<InstanceToDraw> mirrorInstances;
		std::vector<InstanceToDraw> environmentInstances;
		std::vector<InstanceToDraw> lambertianInstances;
		std::vector<InstanceToDraw> pbrInstances;
		std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)> getInstancesToDraw =
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
				else if (mesh->material.lock()->materialType == "pbd")
					pbrInstances.push_back(instanceToDraw);
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
				getInstancesToDraw
			);
		}
		// Get view matrices and culling matrices
		jjyou::glsl::mat4 viewingProjection;
		jjyou::glsl::mat4 viewingView;
		float viewingAspectRatio{};
		jjyou::glsl::mat4 cullingProjection;
		jjyou::glsl::mat4 cullingView;
		if (this->cameraMode == CameraMode::USER) {
			viewingAspectRatio = static_cast<float>(screenExtent.width) / screenExtent.height;
			viewingProjection = jjyou::glsl::perspective(jjyou::glsl::radians(45.0f), viewingAspectRatio, 0.01f, 500.0f);
			viewingView = this->sceneViewer.getViewMatrix();
			cullingProjection = viewingProjection;
			cullingView = viewingView;
		}
		else if (this->cameraMode == CameraMode::SCENE || this->cameraMode == CameraMode::DEBUG) {
			float userCameraAspectRatio{};
			jjyou::glsl::mat4 userCameraProjection;
			jjyou::glsl::mat4 userCameraView;
			std::function<bool(s72::Node::Ptr, const jjyou::glsl::mat4&)> getCameraParam =
				[&](s72::Node::Ptr node, const jjyou::glsl::mat4& transform) -> bool {
				if (!node->camera.expired()) {
					s72::Camera::Ptr camera = node->camera.lock();
					if (camera->name == this->cameraName) {
						userCameraAspectRatio = camera->getAspectRatio();
						userCameraProjection = camera->getProjectionMatrix();
						userCameraView = jjyou::glsl::inverse(transform);
						return false;
					}
				}
				return true;
				};
			//Scene72 are "+z" up, however in our coordinate the scene is "-y" up
			jjyou::glsl::mat4 rootTransform;
			rootTransform[0][0] = 1.0f;
			rootTransform[2][1] = -1.0f;
			rootTransform[1][2] = 1.0f;
			rootTransform[3][3] = 1.0f;
			this->pScene72->traverse(
				this->currPlayTime,
				rootTransform,
				getCameraParam
			);
			if (this->cameraMode == CameraMode::SCENE) {
				viewingAspectRatio = userCameraAspectRatio;
				viewingProjection = userCameraProjection;
				viewingView = userCameraView;
				cullingProjection = userCameraProjection;
				cullingView = userCameraView;
			}
			else if (this->cameraMode == CameraMode::DEBUG) {
				viewingAspectRatio = static_cast<float>(screenExtent.width) / screenExtent.height;;
				viewingProjection = jjyou::glsl::perspective(jjyou::glsl::radians(45.0f), viewingAspectRatio, 0.01f, 500.0f);
				viewingView = this->sceneViewer.getViewMatrix();
				cullingProjection = userCameraProjection;
				cullingView = userCameraView;
			}
		}

		// Set viewport and scissor.
		// This is easy for the scene/debug camera.
		// But for user cameras, the viewport needs to be computed according to the camera parameters.
		float viewPortWidth = static_cast<float>(screenExtent.width);
		float viewPortHeight = static_cast<float>(screenExtent.height);
		if (viewPortWidth / viewPortHeight < viewingAspectRatio)
			viewPortHeight = viewPortWidth / viewingAspectRatio;
		else if (viewPortWidth / viewPortHeight > viewingAspectRatio)
			viewPortWidth = viewPortHeight * viewingAspectRatio;
		VkViewport viewport{
			.x = (screenExtent.width - viewPortWidth) / 2.0f,
			.y = (screenExtent.height - viewPortHeight) / 2.0f,
			.width = viewPortWidth,
			.height = viewPortHeight,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		VkRect2D scissor{
			.offset = { 0, 0 },
			.extent = screenExtent,
		};
		// Compute dynamic uniform buffer offset
		VkDeviceSize minAlignment = this->physicalDevice.deviceProperties().limits.minUniformBufferOffsetAlignment;
		VkDeviceSize dynamicBufferOffset = sizeof(Engine::ObjectLevelUniform);
		if (minAlignment > 0)
			dynamicBufferOffset = (dynamicBufferOffset + minAlignment - 1) & ~(minAlignment - 1);

		// Actually draw
		Engine::ViewLevelUniform viewLevelUniform{
				.projection = viewingProjection,
				.view = viewingView
		};
		memcpy(this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformBufferMemory.mappedAddress(), &viewLevelUniform, sizeof(Engine::ViewLevelUniform));
		std::size_t instanceCount = 0;

		if (!simpleInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : simpleInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(cullingProjection, cullingView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					Engine::ObjectLevelUniform objectLevelUniform{
						.model = instanceToDraw.transform,
						.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
					};
					char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
					memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->simplePipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
		}
		if (!mirrorInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : mirrorInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(cullingProjection, cullingView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					Engine::ObjectLevelUniform objectLevelUniform{
						.model = instanceToDraw.transform,
						.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
					};
					char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
					memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mirrorPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
		}
		if (!environmentInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : environmentInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(cullingProjection, cullingView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					Engine::ObjectLevelUniform objectLevelUniform{
						.model = instanceToDraw.transform,
						.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
					};
					char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
					memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->environmentPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
		}
		if (!lambertianInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : lambertianInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(cullingProjection, cullingView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					Engine::ObjectLevelUniform objectLevelUniform{
						.model = instanceToDraw.transform,
						.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
					};
					char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
					memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->lambertianPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
			}
		}
		if (!pbrInstances.empty()) {
			vkCmdBindPipeline(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipeline);
			vkCmdSetViewport(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 0, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].viewLevelUniformDescriptorSet, 0, nullptr);
			for (const auto& instanceToDraw : lambertianInstances) {
				if (this->cullingMode == CullingMode::NONE ||
					this->cullingMode == CullingMode::FRUSTUM && instanceToDraw.mesh->bbox.insideFrustum(cullingProjection, cullingView, instanceToDraw.transform)
					) {
					VkDeviceSize vertexBufferOffsets = 0;
					vkCmdBindVertexBuffers(this->frameData[this->currentFrame].graphicsCommandBuffer, 0, 1, &instanceToDraw.mesh->vertexBuffer, &vertexBufferOffsets);
					std::uint32_t dynamicOffset = static_cast<std::uint32_t>(dynamicBufferOffset * instanceCount);
					instanceCount++;
					Engine::ObjectLevelUniform objectLevelUniform{
						.model = instanceToDraw.transform,
						.normal = jjyou::glsl::transpose(jjyou::glsl::inverse(instanceToDraw.transform))
					};
					char* dst = reinterpret_cast<char*>(this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformBufferMemory.mappedAddress()) + dynamicOffset;
					memcpy(reinterpret_cast<void*>(dst), &objectLevelUniform, sizeof(Engine::ObjectLevelUniform));
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 1, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].objectLevelUniformDescriptorSet, 1, &dynamicOffset);
					vkCmdBindDescriptorSets(this->frameData[this->currentFrame].graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pbrPipelineLayout, 2, 1, &this->pScene72->frameDescriptorSets[this->currentFrame].materialLevelUniformDescriptorSets[instanceToDraw.mesh->material.lock()->idx], 0, nullptr);
					vkCmdDraw(this->frameData[this->currentFrame].graphicsCommandBuffer, instanceToDraw.mesh->count, 1, 0, 0);
				}
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
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*this->device.graphicsQueues(), 1, &submitInfo, this->frameData[this->currentFrame].inFlightFence));

	if (!this->offscreen) {
		VkSwapchainKHR swapChains[] = { this->swapchain.get() };
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
		VkResult presentResult = vkQueuePresentKHR(*this->device.presentQueues(), &presentInfo);

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
	vkWaitForFences(this->device.get(), 1, &this->frameData[lastFrame].inFlightFence, VK_TRUE, UINT64_MAX);
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
		{ *this->physicalDevice.graphicsQueueFamily(), *this->physicalDevice.transferQueueFamily() },
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
	JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(this->device.get(), &allocInfo, &transferCommandBuffer));
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
	// copy image to buffer
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
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*this->device.transferQueues(), 1, &submitInfo, nullptr));
	JJYOU_VK_UTILS_CHECK(vkQueueWaitIdle(*this->device.transferQueues()));
	vkFreeCommandBuffers(this->device.get(), this->transferCommandPool, 1, &transferCommandBuffer);
	// copy buffer to cpu memory
	// Get layout of the image (including row pitch)
	VkImageSubresource subResource{};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout subResourceLayout;
	vkGetImageSubresourceLayout(this->device.get(), image, &subResource, &subResourceLayout);
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
	vkDestroyImage(this->device.get(), image, nullptr);
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
	vkDeviceWaitIdle(this->device.get());

	// Destroy frame buffers
	for (int i = 0; i < this->framebuffers.size(); ++i) {
		vkDestroyFramebuffer(this->device.get(), this->framebuffers[i], nullptr);
	}

	// Destroy depth image
	vkDestroyImageView(this->device.get(), this->depthImageView, nullptr);
	vkDestroyImage(this->device.get(), this->depthImage, nullptr);
	this->allocator.free(this->depthImageMemory);

	// Destroy swapchain
	if (!this->offscreen) {
		this->swapchain.destroy();
	}

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