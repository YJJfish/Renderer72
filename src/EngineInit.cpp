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
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = (this->offscreen) ? this->virtualSwapchain.format() : this->swapchain.surfaceFormat().format;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = this->offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = VK_FORMAT_D32_SFLOAT;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef{};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

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
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = { viewLevelUniformLayoutBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		}; JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(this->device.get(), &layoutInfo, nullptr, &this->viewLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding objectLevelUniformLayoutBinding{
			.binding = 1,
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

	// Create descriptor pool
	{
		VkDescriptorPoolSize uniformBufferPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT
		};
		VkDescriptorPoolSize uniformBufferDynamicPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = Engine::MAX_FRAMES_IN_FLIGHT
		};
		std::vector<VkDescriptorPoolSize> poolSizes = { uniformBufferPoolSize, uniformBufferDynamicPoolSize };
		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.maxSets = static_cast<uint32_t>(2 * Engine::MAX_FRAMES_IN_FLIGHT),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorPool(this->device.get(), &poolInfo, nullptr, &this->descriptorPool));
	}

	// Create uniform buffers
	{
		VkDeviceSize bufferSize = sizeof(Engine::ViewLevelUniform);
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			std::tie(this->frameData[i].viewLevelUniformBuffer, this->frameData[i].viewLevelUniformBufferMemory) =
				this->createBuffer(
					bufferSize,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					{ *this->physicalDevice.graphicsQueueFamily() },
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			this->allocator.map(this->frameData[i].viewLevelUniformBufferMemory);
		}
	}

	// Create descriptor sets
	{
		std::vector<VkDescriptorSetLayout> layouts(Engine::MAX_FRAMES_IN_FLIGHT, this->viewLevelUniformDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = this->descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data()
		};
		std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> viewLevelUniformDescriptorSets;
		JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(this->device.get(), &allocInfo, viewLevelUniformDescriptorSets.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			this->frameData[i].viewLevelUniformDescriptorSet = viewLevelUniformDescriptorSets[i];
			VkDescriptorBufferInfo bufferInfo{
				.buffer = this->frameData[i].viewLevelUniformBuffer,
				.offset = 0,
				.range = sizeof(Engine::ViewLevelUniform)
			};
			VkWriteDescriptorSet descriptorWrite{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = this->frameData[i].viewLevelUniformDescriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pImageInfo = nullptr,
				.pBufferInfo = &bufferInfo,
				.pTexelBufferView = nullptr
			};
			std::vector<VkWriteDescriptorSet> descriptorWrites = { descriptorWrite };
			vkUpdateDescriptorSets(this->device.get(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
	}
	{
		std::vector<VkDescriptorSetLayout> layouts(Engine::MAX_FRAMES_IN_FLIGHT, this->objectLevelUniformDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = this->descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data()
		};
		std::array<VkDescriptorSet, Engine::MAX_FRAMES_IN_FLIGHT> objectLevelUniformDescriptorSets;
		JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(this->device.get(), &allocInfo, objectLevelUniformDescriptorSets.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			this->frameData[i].objectLevelUniformDescriptorSet = objectLevelUniformDescriptorSets[i];
			// Not update descriptor set until the user set a scene and the engine knows the dynamic uniform buffer size
		}
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

	// Create graphics pipeline
	{
		std::vector<char> vertShaderCode;
		std::vector<char> fragShaderCode;

		std::ifstream fin;
		fin.open("../spv/shader.vert.spv", std::ios::binary | std::ios::in);
		fin.seekg(0, std::ios::end);
		vertShaderCode.resize(static_cast<std::size_t>(fin.tellg()));
		fin.seekg(0, std::ios::beg);
		fin.read(vertShaderCode.data(), vertShaderCode.size());
		fin.close();

		fin.open("../spv/shader.frag.spv", std::ios::binary | std::ios::in);
		fin.seekg(0, std::ios::end);
		fragShaderCode.resize(static_cast<std::size_t>(fin.tellg()));
		fin.seekg(0, std::ios::beg);
		fin.read(fragShaderCode.data(), fragShaderCode.size());
		fin.close();

		VkShaderModule vertShaderModule;
		{
			VkShaderModuleCreateInfo createInfo{
				.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.codeSize = vertShaderCode.size(),
				.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data())
			};
			JJYOU_VK_UTILS_CHECK(vkCreateShaderModule(this->device.get(), &createInfo, nullptr, &vertShaderModule));
		}
		VkShaderModule fragShaderModule;
		{
			VkShaderModuleCreateInfo createInfo{
				.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.codeSize = fragShaderCode.size(),
				.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data())
			};
			JJYOU_VK_UTILS_CHECK(vkCreateShaderModule(this->device.get(), &createInfo, nullptr, &fragShaderModule));
		}
		VkPipelineShaderStageCreateInfo vertShaderStageInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vertShaderModule,
				.pName = "main",
				.pSpecializationInfo = nullptr
		};
		VkPipelineShaderStageCreateInfo fragShaderStageInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = fragShaderModule,
				.pName = "main",
				.pSpecializationInfo = nullptr
		};
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

		VkVertexInputBindingDescription bindingDescription{
			.binding = 0,
			.stride = 28,
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = { {
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
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &bindingDescription,
			.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
			.pVertexAttributeDescriptions = attributeDescriptions.data()
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
			.depthCompareOp = VK_COMPARE_OP_LESS,
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
			.maxDepthBounds = 0.0f
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
			VK_DYNAMIC_STATE_SCISSOR,
			//VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY
		};
		VkPipelineDynamicStateCreateInfo dynamicState{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
			.pDynamicStates = dynamicStates.data()
		};

		std::vector<VkDescriptorSetLayout> setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size()),
			.pSetLayouts = setLayouts.data(),
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr
		};

		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(this->device.get(), &pipelineLayoutInfo, nullptr, &this->pipelineLayout))

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
				.layout = this->pipelineLayout,
				.renderPass = this->renderPass,
				.subpass = 0,
				.basePipelineHandle = nullptr,
				.basePipelineIndex = -1
		};

		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(this->device.get(), nullptr, 1, &pipelineInfo, nullptr, &this->pipeline));

		vkDestroyShaderModule(this->device.get(), fragShaderModule, nullptr);
		vkDestroyShaderModule(this->device.get(), vertShaderModule, nullptr);
	}
}

Engine::~Engine(void) {

	vkDeviceWaitIdle(this->device.get());

	//Destroy pipeline and pipeline layout
	vkDestroyPipeline(this->device.get(), this->pipeline, nullptr);
	vkDestroyPipelineLayout(this->device.get(), this->pipelineLayout, nullptr);

	// Destroy sync objects
	for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(this->device.get(), this->frameData[i].imageAvailableSemaphore, nullptr);
		vkDestroySemaphore(this->device.get(), this->frameData[i].renderFinishedSemaphore, nullptr);
		vkDestroyFence(this->device.get(), this->frameData[i].inFlightFence, nullptr);
	}

	// Descriptor sets will be destroyed along with the descriptor pool

	// Destory the uniform buffers
	for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
		this->allocator.unmap(this->frameData[i].viewLevelUniformBufferMemory);
		vkDestroyBuffer(this->device.get(), this->frameData[i].viewLevelUniformBuffer, nullptr);
		this->allocator.free(this->frameData[i].viewLevelUniformBufferMemory);
	}
	if (this->pScene72 != nullptr) {
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			this->allocator.unmap(this->frameData[i].objectLevelUniformBufferMemory);
			vkDestroyBuffer(this->device.get(), this->frameData[i].objectLevelUniformBuffer, nullptr);
			this->allocator.free(this->frameData[i].objectLevelUniformBufferMemory);
		}
	}

	// Destroy descriptor pool
	vkDestroyDescriptorPool(this->device.get(), this->descriptorPool, nullptr);

	// Destroy descriptor layout
	vkDestroyDescriptorSetLayout(this->device.get(), this->viewLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(this->device.get(), this->objectLevelUniformDescriptorSetLayout, nullptr);

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