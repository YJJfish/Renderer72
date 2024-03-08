#include "Engine.hpp"
#include <fstream>

Engine::Engine(
	std::optional<std::string> physicalDeviceName,
	bool enableValidation,
	bool offscreen,
	int winWidth,
	int winHeight
) : offscreen(offscreen)
{

	// Init glfw window.
	if (!this->offscreen) {
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		this->window = glfwCreateWindow(winWidth, winHeight, "Vulkan", nullptr, nullptr);
		glfwSetWindowUserPointer(this->window, this);
		glfwSetFramebufferSizeCallback(this->window, Engine::framebufferResizeCallback);
		glfwSetMouseButtonCallback(this->window, Engine::mouseButtonCallback);
		glfwSetCursorPosCallback(this->window, Engine::cursorPosCallback);
		glfwSetScrollCallback(this->window, Engine::scrollCallback);
		glfwSetKeyCallback(this->window, Engine::keyCallback);
	}
	this->sceneViewer.setZoomRate(10.0f);

	// Create instance
	{
		jjyou::vk::InstanceBuilder builder;
		builder
			.enableValidation(enableValidation)
			.offscreen(this->offscreen)
			.applicationName("Renderer72")
			.applicationVersion(0, 1, 0, 0)
			.engineName("Engine72")
			.engineVersion(0, 1, 0, 0)
			.apiVersion(VK_API_VERSION_1_0);
		if (enableValidation)
			builder.useDefaultDebugUtilsMessenger();
		this->instance = builder.build();
	}

	// Init vulkan loader
	if (enableValidation) {
		this->loader.load(this->instance.get(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	// Create window surface
	if (!this->offscreen)
		JJYOU_VK_UTILS_CHECK(glfwCreateWindowSurface(this->instance.get(), this->window, nullptr, &this->surface));

	// Pick physical device
	{
		jjyou::vk::PhysicalDeviceSelector selector(this->instance, this->surface);
		if (physicalDeviceName.has_value()) {
			selector
				.requireDeviceName(*physicalDeviceName)
				//.requestDedicated()
				.requireGraphicsQueue(true)
				.requireComputeQueue(false)
				//.requireDistinctTransferQueue(true)
				.enableDeviceFeatures(
					VkPhysicalDeviceFeatures{
						.samplerAnisotropy = true
					}
			);
		}
		else {
			selector
				.requestDedicated()
				.requireGraphicsQueue(true)
				.requireComputeQueue(false)
				.requireDistinctTransferQueue(true)
				.enableDeviceFeatures(
					VkPhysicalDeviceFeatures{
						.samplerAnisotropy = true
					}
			);
		}
		
		this->physicalDevice = selector.select();
	}

	// Create logical device
	{
		jjyou::vk::DeviceBuilder builder(this->instance, this->physicalDevice);
		this->device = builder.build();
	}

	// Create command pool
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *physicalDevice.graphicsQueueFamily()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(this->device.get(), &poolInfo, nullptr, &this->graphicsCommandPool));
	}
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *physicalDevice.transferQueueFamily()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(this->device.get(), &poolInfo, nullptr, &this->transferCommandPool));
	}

	// Create command buffers
	{
		std::array<VkCommandBuffer, Engine::MAX_FRAMES_IN_FLIGHT> graphicsCommandBuffers;
		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = this->graphicsCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = static_cast<uint32_t>(graphicsCommandBuffers.size()),
		};
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(this->device.get(), &allocInfo, graphicsCommandBuffers.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			this->frameData[i].graphicsCommandBuffer = graphicsCommandBuffers[i];
		}
	}

	// Init memory allocator
	this->allocator.init(this->device);

	// Create swap chain
	if (!this->offscreen) {
		this->createSwapchain();
	}
	else {
		this->virtualSwapchain.create(
			this->physicalDevice,
			this->device,
			this->allocator,
			3,
			VK_FORMAT_R8G8B8A8_SRGB,
			VkExtent2D{ .width = static_cast<std::uint32_t>(winWidth), .height = static_cast<std::uint32_t>(winHeight) }
		);
	}

	// Create depth image
	this->createDepthImage();

	// Create renderpass
	{
		VkAttachmentDescription colorAttachment{
			.flags = 0,
			.format = (this->offscreen) ? this->virtualSwapchain.format() : this->swapchain.surfaceFormat().format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = this->offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		};

		VkAttachmentDescription depthAttachment{
			.flags = 0,
			.format = VK_FORMAT_D32_SFLOAT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference colorAttachmentRef{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		};

		VkAttachmentReference depthAttachmentRef{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};

		VkSubpassDescription subpass{
			.flags = 0,
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentRef,
			.pResolveAttachments = nullptr,
			.pDepthStencilAttachment = &depthAttachmentRef,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = nullptr
		};

		VkSubpassDependency dependency{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = 0
		};
		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 1,
			.pDependencies = &dependency
		};

		JJYOU_VK_UTILS_CHECK(vkCreateRenderPass(this->device.get(), &renderPassInfo, nullptr, &this->renderPass));
	}

	// Create framebuffer
	this->createFramebuffers();

	// Create descriptor set layout
	{
		VkDescriptorSetLayoutBinding viewLevelUniformLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { viewLevelUniformLayoutBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->viewLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding objectLevelUniformLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { objectLevelUniformLayoutBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->objectLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding skyboxModelUniformBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding skyboxRadianceSamplerBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding skyboxLambertianSamplerBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding skyboxEnvironmentBRDFSamplerBinding{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { skyboxModelUniformBinding, skyboxRadianceSamplerBinding, skyboxLambertianSamplerBinding, skyboxEnvironmentBRDFSamplerBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->skyboxUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding mirrorNormalMapSamplerBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding mirrorDisplacementMapSamplerBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { mirrorNormalMapSamplerBinding, mirrorDisplacementMapSamplerBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->mirrorMaterialLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding environmentNormalMapSamplerBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding environmentDisplacementMapSamplerBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { environmentNormalMapSamplerBinding, environmentDisplacementMapSamplerBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->environmentMaterialLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding lambertianNormalMapSamplerBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding lambertianDisplacementMapSamplerBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding lambertianBaseColorSamplerBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { lambertianNormalMapSamplerBinding, lambertianDisplacementMapSamplerBinding, lambertianBaseColorSamplerBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->lambertianMaterialLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding pbrNormalMapSamplerBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding pbrDisplacementMapSamplerBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding pbrAlbedoSamplerBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding pbrRoughnessSamplerBinding{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding pbrMetalnessSamplerBinding{
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { pbrNormalMapSamplerBinding, pbrDisplacementMapSamplerBinding, pbrAlbedoSamplerBinding, pbrRoughnessSamplerBinding, pbrMetalnessSamplerBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->pbrMaterialLevelUniformDescriptorSetLayout));
	}

	// Create sync objects
	{
		VkSemaphoreCreateInfo semaphoreInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0
		};
		VkFenceCreateInfo fenceInfo{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			JJYOU_VK_UTILS_CHECK(vkCreateSemaphore(this->device.get(), &semaphoreInfo, nullptr, &this->frameData[i].imageAvailableSemaphore));
			JJYOU_VK_UTILS_CHECK(vkCreateSemaphore(this->device.get(), &semaphoreInfo, nullptr, &this->frameData[i].renderFinishedSemaphore));
			JJYOU_VK_UTILS_CHECK(vkCreateFence(this->device.get(), &fenceInfo, nullptr, &this->frameData[i].inFlightFence));
		}
	}

	// Create pipeline layout
	{
		std::vector<VkDescriptorSetLayout> setLayouts;
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.setLayoutCount = 0, // To set
			.pSetLayouts = nullptr, // To set
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr
		};

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->simplePipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->mirrorMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->mirrorPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->environmentMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->environmentPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->lambertianMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->lambertianPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->pbrMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->pbrPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->skyboxPipelineLayout));
	}

	// Create graphics pipeline
	{
		VkShaderModule simpleVertShaderModule = this->createShaderModule("../spv/renderer72/simple.vert.spv");
		VkShaderModule simpleFragShaderModule = this->createShaderModule("../spv/renderer72/simple.frag.spv");
		VkShaderModule mirrorVertShaderModule = this->createShaderModule("../spv/renderer72/mirror.vert.spv");
		VkShaderModule mirrorFragShaderModule = this->createShaderModule("../spv/renderer72/mirror.frag.spv");
		VkShaderModule environmentVertShaderModule = this->createShaderModule("../spv/renderer72/environment.vert.spv");
		VkShaderModule environmentFragShaderModule = this->createShaderModule("../spv/renderer72/environment.frag.spv");
		VkShaderModule lambertianVertShaderModule = this->createShaderModule("../spv/renderer72/lambertian.vert.spv");
		VkShaderModule lambertianFragShaderModule = this->createShaderModule("../spv/renderer72/lambertian.frag.spv");
		VkShaderModule pbrVertShaderModule = this->createShaderModule("../spv/renderer72/pbr.vert.spv");
		VkShaderModule pbrFragShaderModule = this->createShaderModule("../spv/renderer72/pbr.frag.spv");
		VkShaderModule skyboxVertShaderModule = this->createShaderModule("../spv/renderer72/skybox.vert.spv");
		VkShaderModule skyboxFragShaderModule = this->createShaderModule("../spv/renderer72/skybox.frag.spv");

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = { {
			VkPipelineShaderStageCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = nullptr, // To set
				.pName = "main",
				.pSpecializationInfo = nullptr
			},
			VkPipelineShaderStageCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = nullptr, // To set
				.pName = "main",
				.pSpecializationInfo = nullptr
			}
		} };

		VkVertexInputBindingDescription simpleVertexBindingDescription{
			.binding = 0,
			.stride = 28,
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
		VkVertexInputBindingDescription materialVertexBindingDescription{
			.binding = 0,
			.stride = 52,
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
		std::array<VkVertexInputAttributeDescription, 3> simpleAttributeDescriptions = { {
			VkVertexInputAttributeDescription{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 0
			},
			VkVertexInputAttributeDescription{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 12
			},
			VkVertexInputAttributeDescription{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.offset = 24
			}
		} };
		std::array<VkVertexInputAttributeDescription, 5> materialAttributeDescriptions = { {
			VkVertexInputAttributeDescription{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 0
			},
			VkVertexInputAttributeDescription{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 12
			},
			VkVertexInputAttributeDescription{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.offset = 24
			},
			VkVertexInputAttributeDescription{
				.location = 3,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = 40
			},
			VkVertexInputAttributeDescription{
				.location = 4,
				.binding = 0,
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.offset = 48
			}
		} };
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.vertexBindingDescriptionCount = 0, // To set
			.pVertexBindingDescriptions = nullptr, // To set
			.vertexAttributeDescriptionCount = 0, // To set
			.pVertexAttributeDescriptions = nullptr // To set
		};

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE
		};

		VkPipelineViewportStateCreateInfo viewportState{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.viewportCount = 1,
			.pViewports = nullptr,
			.scissorCount = 1,
			.pScissors = nullptr
		};

		VkPipelineRasterizationStateCreateInfo rasterizer{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
			.depthBiasConstantFactor = 0.0f,
			.depthBiasClamp = 0.0f,
			.depthBiasSlopeFactor = 0.0f,
			.lineWidth = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisampling{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = VK_FALSE,
			.minSampleShading = 0.0f,
			.pSampleMask = nullptr,
			.alphaToCoverageEnable = false,
			.alphaToOneEnable = false
		};

		VkPipelineDepthStencilStateCreateInfo depthStencil{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			.stencilTestEnable = VK_FALSE,
			.front = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_KEEP,
				.compareOp = VK_COMPARE_OP_NEVER,
				.compareMask = 0,
				.writeMask = 0,
				.reference = 0
			},
			.back = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_KEEP,
				.compareOp = VK_COMPARE_OP_NEVER,
				.compareMask = 0,
				.writeMask = 0,
				.reference = 0
			},
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f
		};

		VkPipelineColorBlendAttachmentState colorBlendAttachment{
			.blendEnable = VK_FALSE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		VkPipelineColorBlendStateCreateInfo colorBlending{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.logicOpEnable = VK_FALSE,
			.logicOp = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.pAttachments = &colorBlendAttachment,
			.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
		};

		std::vector<VkDynamicState> dynamicStates = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
			.pDynamicStates = dynamicStates.data()
		};

		VkGraphicsPipelineCreateInfo pipelineInfo{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stageCount = static_cast<std::uint32_t>(shaderStages.size()),
			.pStages = shaderStages.data(),
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &inputAssembly,
			.pTessellationState = nullptr,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicState,
			.layout = nullptr, // To set
			.renderPass = this->renderPass,
			.subpass = 0,
			.basePipelineHandle = nullptr,
			.basePipelineIndex = -1
		};

		shaderStages[0].module = simpleVertShaderModule;
		shaderStages[1].module = simpleFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &simpleVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(simpleAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = simpleAttributeDescriptions.data();
		pipelineInfo.layout = this->simplePipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->simplePipeline));

		shaderStages[0].module = mirrorVertShaderModule;
		shaderStages[1].module = mirrorFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->mirrorPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->mirrorPipeline));

		shaderStages[0].module = environmentVertShaderModule;
		shaderStages[1].module = environmentFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->environmentPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->environmentPipeline));

		shaderStages[0].module = lambertianVertShaderModule;
		shaderStages[1].module = lambertianFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->lambertianPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->lambertianPipeline));

		shaderStages[0].module = pbrVertShaderModule;
		shaderStages[1].module = pbrFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->pbrPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->pbrPipeline));

		shaderStages[0].module = skyboxVertShaderModule;
		shaderStages[1].module = skyboxFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		pipelineInfo.layout = this->skyboxPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->skyboxPipeline));

		vkDestroyShaderModule(this->device.get(), simpleVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), simpleFragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), mirrorVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), mirrorFragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), environmentVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), environmentFragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), lambertianVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), lambertianFragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), pbrVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), pbrFragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), skyboxVertShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), skyboxFragShaderModule, nullptr);
	}
}

Engine::~Engine(void) {

	vkDeviceWaitIdle(this->device.get());

	//Destroy pipeline and pipeline layout
	vkDestroyPipeline(this->device.get(), this->simplePipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->simplePipelineLayout, nullptr);
	vkDestroyPipeline(this->device.get(), this->mirrorPipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->mirrorPipelineLayout, nullptr);
	vkDestroyPipeline(this->device.get(), this->environmentPipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->environmentPipelineLayout, nullptr);
	vkDestroyPipeline(this->device.get(), this->lambertianPipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->lambertianPipelineLayout, nullptr);
	vkDestroyPipeline(this->device.get(), this->pbrPipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->pbrPipelineLayout, nullptr);
	vkDestroyPipeline(this->device.get(), this->skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->skyboxPipelineLayout, nullptr);

	// Destroy sync objects
	for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(this->device.get(), this->frameData[i].imageAvailableSemaphore, nullptr);
		vkDestroySemaphore(this->device.get(), this->frameData[i].renderFinishedSemaphore, nullptr);
		vkDestroyFence(this->device.get(), this->frameData[i].inFlightFence, nullptr);
	}

	// Destroy descriptor layout
	vkDestroyDescriptorSetLayout(this->device.get(), this->viewLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->objectLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->skyboxUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->mirrorMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->environmentMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->lambertianMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->pbrMaterialLevelUniformDescriptorSetLayout, nullptr);

	// Destroy frame buffers
	for (int i = 0; i < this->framebuffers.size(); ++i) {
		vkDestroyFramebuffer(this->device.get(), this->framebuffers[i], nullptr);
	}

	// Destroy render pass
	vkDestroyRenderPass(this->device.get(), this->renderPass, nullptr);

	// Destroy depth image
	vkDestroyImageView(this->device.get(), this->depthImageView, nullptr);
	vkDestroyImage(this->device.get(), this->depthImage, nullptr);
	this->allocator.free(this->depthImageMemory);

	// Destroy swapchain
	if (!this->offscreen) {
		this->swapchain.destroy();
	}
	else {
		this->virtualSwapchain.destroy();
	}

	// Destroy allocator
	this->allocator.destory();

	// Command buffers will be destroyed along with the command pool

	// Destroy command pool
	vkDestroyCommandPool(this->device.get(), this->graphicsCommandPool, nullptr);
	vkDestroyCommandPool(this->device.get(), this->transferCommandPool, nullptr);

	// Destroy logical device
	this->device.destroy();

	// Destroy window surface
	if (!this->offscreen)
		vkDestroySurfaceKHR(this->instance.get(), this->surface, nullptr);

	// Destroy vulkan instance
	this->instance.destroy();

	// Destroy glfw window
	if (!this->offscreen) {
		glfwTerminate();
		glfwDestroyWindow(this->window);
	}
}

void Engine::createSwapchain(void) {
	jjyou::vk::SwapchainBuilder builder(this->physicalDevice, this->device, this->surface);
	builder
		.useDefaultMinImageCount()
		.preferSurfaceFormat(VkSurfaceFormatKHR{ .format = VK_FORMAT_B8G8R8A8_SRGB , .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.preferPresentMode(VK_PRESENT_MODE_MAILBOX_KHR)
		.useDefaultExtent()
		.imageArrayLayers(1)
		.imageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		.compositeAlpha(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		.clipped(true)
		.oldSwapchain(nullptr);
	this->swapchain = builder.build();
}

void Engine::createFramebuffers(void) {
	if (!this->offscreen) {
		this->framebuffers.resize(this->swapchain.imageCount());
		for (size_t i = 0; i < this->framebuffers.size(); i++) {
			std::array<VkImageView, 2> attachments = {
				this->swapchain.imageViews()[i],
				this->depthImageView
			};

			VkFramebufferCreateInfo framebufferInfo{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.renderPass = this->renderPass,
				.attachmentCount = static_cast<uint32_t>(attachments.size()),
				.pAttachments = attachments.data(),
				.width = this->swapchain.extent().width,
				.height = this->swapchain.extent().height,
				.layers = 1
			};
			JJYOU_VK_UTILS_CHECK(vkCreateFramebuffer(this->device.get(), &framebufferInfo, nullptr, &this->framebuffers[i]));
		}
	}
	else {
		this->framebuffers.resize(this->virtualSwapchain.imageCount());
		for (size_t i = 0; i < this->framebuffers.size(); i++) {
			std::array<VkImageView, 2> attachments = {
				this->virtualSwapchain.imageViews()[i],
				this->depthImageView
			};

			VkFramebufferCreateInfo framebufferInfo{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.renderPass = this->renderPass,
				.attachmentCount = static_cast<uint32_t>(attachments.size()),
				.pAttachments = attachments.data(),
				.width = this->virtualSwapchain.extent().width,
				.height = this->virtualSwapchain.extent().height,
				.layers = 1
			};
			JJYOU_VK_UTILS_CHECK(vkCreateFramebuffer(this->device.get(), &framebufferInfo, nullptr, &this->framebuffers[i]));
		}
	}
}

void Engine::createDepthImage(void) {
	this->depthImageFormat = this->physicalDevice.findSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
	VkExtent2D extent = (this->offscreen) ? this->virtualSwapchain.extent() : this->swapchain.extent();
	std::tie(this->depthImage, this->depthImageMemory) = this->createImage(
		extent.width, extent.height,
		this->depthImageFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		{ *this->physicalDevice.graphicsQueueFamily() },
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);
	this->depthImageView = this->createImageView(this->depthImage, this->depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}