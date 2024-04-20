#include "Engine.hpp"
#include <fstream>
#include <random>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

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
	this->sceneViewer.setZoomRate(15.0f);

	// Create vulkan context
	{
		jjyou::vk::ContextBuilder builder;
		// Instance
		builder
			.headless(this->offscreen)
			.enableValidation(enableValidation)
			.applicationName("Renderer72")
			.applicationVersion(0, 1, 0, 0)
			.engineName("Engine72")
			.engineVersion(0, 1, 0, 0)
			.apiVersion(0, 1, 0, 0);
		if (enableValidation)
			builder.useDefaultDebugUtilsMessenger();
		if (!this->offscreen) {
			std::uint32_t count;
			const char** instanceExtensions = glfwGetRequiredInstanceExtensions(&count);
			builder.enableInstanceExtensions(instanceExtensions, instanceExtensions + count);
		}
		builder.buildInstance(this->context);
		// Surface
		if (!this->offscreen) {
			VkSurfaceKHR vkSurface{ nullptr };
			JJYOU_VK_UTILS_CHECK(glfwCreateWindowSurface(*this->context.instance(), this->window, nullptr, &vkSurface));
			this->surface = vk::raii::SurfaceKHR(this->context.instance(), vkSurface);
		}
		// Physical device
		builder
			.requestPhysicalDeviceType(vk::PhysicalDeviceType::eDiscreteGpu)
			.addSurface(this->surface)
			.requirePhysicalDeviceFeatures(
				VkPhysicalDeviceFeatures{
					.geometryShader = true,
					.samplerAnisotropy = true,
				}
		);
		if (physicalDeviceName.has_value())
			builder.addPhysicalDeviceSelectionCriteria(
				[&](const jjyou::vk::PhysicalDeviceInfo& info)->bool {
					return (info.physicalDevice.getProperties().deviceName == physicalDeviceName);
				}
			);
		builder.selectPhysicalDevice(this->context);
		// Device
		builder.buildDevice(this->context);
	}

	// Create command pool
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main)
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(*this->context.device(), &poolInfo, nullptr, &this->graphicsCommandPool));
	}
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Transfer)
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(*this->context.device(), &poolInfo, nullptr, &this->transferCommandPool));
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
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(*this->context.device(), &allocInfo, graphicsCommandBuffers.data()));
		for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
			this->frameData[i].graphicsCommandBuffer = graphicsCommandBuffers[i];
		}
	}

	// Create descriptor pool
	{
		std::vector<vk::DescriptorPoolSize> descriptorPoolSizes = {
		vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 1000),
		vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 1000),
		};
		vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo = vk::DescriptorPoolCreateInfo()
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
			.setMaxSets(1000)
			.setPoolSizes(descriptorPoolSizes);
		this->descriptorPool = vk::raii::DescriptorPool(this->context.device(), descriptorPoolCreateInfo);
	}

	// Init memory allocator
	this->allocator.init(*this->context.device());

	// Create swap chain
	if (!this->offscreen) {
		this->createSwapchain();
	}
	else {
		this->virtualSwapchain = VirtualSwapchain(
			this->context,
			this->allocator,
			3,
			VK_FORMAT_R8G8B8A8_SRGB,
			VkExtent2D{ .width = static_cast<std::uint32_t>(winWidth), .height = static_cast<std::uint32_t>(winHeight) }
		);
	}

	// Create depth image
	this->createDepthImage();

	// Create shadow mapping renderpass
	{
		VkAttachmentDescription depthAttachment{
			.flags = 0,
			.format = VK_FORMAT_D32_SFLOAT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		};

		VkAttachmentReference depthAttachmentRef{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};

		VkSubpassDescription subpass{
			.flags = 0,
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 0,
			.pColorAttachments = nullptr,
			.pResolveAttachments = nullptr,
			.pDepthStencilAttachment = &depthAttachmentRef,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = nullptr
		};

		std::vector<VkSubpassDependency> dependencies = {
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
			},
			VkSubpassDependency{
				.srcSubpass = 0,
				.dstSubpass = VK_SUBPASS_EXTERNAL,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
			}
		};
		std::vector<VkAttachmentDescription> attachments = { depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.attachmentCount = static_cast<std::uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = static_cast<std::uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};
		this->shadowMappingRenderPass = this->context.device().createRenderPass(renderPassInfo);
	}

	// Create deferred shading renderpass
	{
		vk::AttachmentDescription colorAttachmentDescription = vk::AttachmentDescription()
			.setFlags(vk::AttachmentDescriptionFlags(0U))
			//.setFormat()
			.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		std::vector<vk::AttachmentDescription> attachmentDescriptions{};
		for (std::uint32_t i = 0; i < 4; ++i) {
			colorAttachmentDescription.setFormat(GBuffer::format(i));
			attachmentDescriptions.push_back(colorAttachmentDescription);
		}
		vk::AttachmentDescription depthAttachmentDescription = vk::AttachmentDescription()
			.setFlags(vk::AttachmentDescriptionFlags(0U))
			.setFormat(GBuffer::depthFormat())
			.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
		attachmentDescriptions.push_back(depthAttachmentDescription);

		// Subpass
		std::vector<vk::AttachmentReference> subpassColorAttachmentRefs{
			vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
			vk::AttachmentReference(1, vk::ImageLayout::eColorAttachmentOptimal),
			vk::AttachmentReference(2, vk::ImageLayout::eColorAttachmentOptimal),
			vk::AttachmentReference(3, vk::ImageLayout::eColorAttachmentOptimal),
		};
		vk::AttachmentReference subpassDepthAttachmentRef(4, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		vk::SubpassDescription subpassDescription = vk::SubpassDescription()
			.setFlags(vk::SubpassDescriptionFlags(0U))
			.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
			.setInputAttachments(nullptr)
			.setColorAttachments(subpassColorAttachmentRefs)
			//.setResolveAttachments(nullptr)
			.setPDepthStencilAttachment(&subpassDepthAttachmentRef)
			.setPreserveAttachments(nullptr);

		// Subpass dependencies
		std::vector<vk::SubpassDependency> subpassDependencies = {
			vk::SubpassDependency()
			.setSrcSubpass(VK_SUBPASS_EXTERNAL)
			.setDstSubpass(0)
			.setSrcStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
			.setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
			.setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
			.setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead)
			.setDependencyFlags(vk::DependencyFlags(0U)),
			vk::SubpassDependency()
			.setSrcSubpass(VK_SUBPASS_EXTERNAL)
			.setDstSubpass(0)
			.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
			.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
			.setSrcAccessMask(vk::AccessFlags(0U))
			.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead)
			.setDependencyFlags(vk::DependencyFlags(0U))
		};

		// Create renderpass
		vk::RenderPassCreateInfo renderPassCreateInfo = vk::RenderPassCreateInfo()
			.setFlags(vk::RenderPassCreateFlags(0U))
			.setAttachments(attachmentDescriptions)
			.setSubpasses(subpassDescription)
			.setDependencies(subpassDependencies);
		this->deferredRenderPass = vk::raii::RenderPass(this->context.device(), renderPassCreateInfo);
	}

	// Create G buffer
	if (!this->offscreen){
		int width, height;
		glfwGetFramebufferSize(this->window, &width, &height);
		this->gBuffer = GBuffer(this->context, this->allocator);
		this->gBuffer.createTextures(
			vk::Extent2D(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)),
			this->deferredRenderPass
		);
	}
	else {
		this->gBuffer = GBuffer(this->context, this->allocator);
		this->gBuffer.createTextures(
			vk::Extent2D(static_cast<std::uint32_t>(winWidth), static_cast<std::uint32_t>(winHeight)),
			this->deferredRenderPass
		);
	}

	// Create ssao and ssao blur renderpass
	{
		
		std::vector<vk::AttachmentDescription> attachmentDescriptions{
			vk::AttachmentDescription()
			.setFlags(vk::AttachmentDescriptionFlags(0U))
			.setFormat(SSAO::format(0))
			.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
		};
		// No depth attachment

		// Subpass
		std::vector<vk::AttachmentReference> subpassColorAttachmentRefs{
			vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
		};
		vk::SubpassDescription subpassDescription = vk::SubpassDescription()
			.setFlags(vk::SubpassDescriptionFlags(0U))
			.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
			.setInputAttachments(nullptr)
			.setColorAttachments(subpassColorAttachmentRefs)
			//.setResolveAttachments(nullptr)
			.setPDepthStencilAttachment(nullptr)
			.setPreserveAttachments(nullptr);

		// Subpass dependencies
		std::vector<vk::SubpassDependency> subpassDependencies = {
			vk::SubpassDependency()
			.setSrcSubpass(VK_SUBPASS_EXTERNAL)
			.setDstSubpass(0)
			.setSrcStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
			.setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
			.setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
			.setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead)
			.setDependencyFlags(vk::DependencyFlags(0U)),
			vk::SubpassDependency()
			.setSrcSubpass(VK_SUBPASS_EXTERNAL)
			.setDstSubpass(0)
			.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
			.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
			.setSrcAccessMask(vk::AccessFlags(0U))
			.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead)
			.setDependencyFlags(vk::DependencyFlags(0U))
		};

		// Create renderpass
		vk::RenderPassCreateInfo renderPassCreateInfo = vk::RenderPassCreateInfo()
			.setFlags(vk::RenderPassCreateFlags(0U))
			.setAttachments(attachmentDescriptions)
			.setSubpasses(subpassDescription)
			.setDependencies(subpassDependencies);
		this->ssaoRenderPass = vk::raii::RenderPass(this->context.device(), renderPassCreateInfo);
	}

	// Create SSAO buffer
	if (!this->offscreen) {
		int width, height;
		glfwGetFramebufferSize(this->window, &width, &height);
		this->ssao = SSAO(this->context, this->allocator);
		this->ssao.createTextures(
			vk::Extent2D(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)),
			this->ssaoRenderPass,
			this->ssaoRenderPass
		);
	}
	else {
		this->ssao = SSAO(this->context, this->allocator);
		this->ssao.createTextures(
			vk::Extent2D(static_cast<std::uint32_t>(winWidth), static_cast<std::uint32_t>(winHeight)),
			this->ssaoRenderPass,
			this->ssaoRenderPass
		);
	}

	// Create output renderpass
	{
		VkAttachmentDescription colorAttachment{
			.flags = 0,
			.format = (this->offscreen) ? this->virtualSwapchain.format() : static_cast<VkFormat>(this->swapchain.surfaceFormat().format),
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

		std::vector<VkSubpassDependency> dependencies = {
			VkSubpassDependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				.dependencyFlags = 0
			},
			VkSubpassDependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
				.dependencyFlags = 0
			}
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
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};

		JJYOU_VK_UTILS_CHECK(vkCreateRenderPass(*this->context.device(), &renderPassInfo, nullptr, &this->outputRenderPass));
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
		VkDescriptorSetLayoutBinding lightsStorageBufferUniformBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding shadowMapSamplerUniformBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding spotShadowMapsUniformBinding{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SPOT_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding sphereShadowMapsUniformBinding{
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SPHERE_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding sunShadowMapsUniformBinding{
			.binding = 5,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SUN_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			viewLevelUniformLayoutBinding,
			lightsStorageBufferUniformBinding,
			shadowMapSamplerUniformBinding,
			spotShadowMapsUniformBinding,
			sphereShadowMapsUniformBinding,
			sunShadowMapsUniformBinding
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->viewLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding viewLevelUniformLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding lightsStorageBufferUniformBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding shadowMapSamplerUniformBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding spotShadowMapsUniformBinding{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SPOT_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding sphereShadowMapsUniformBinding{
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SPHERE_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding sunShadowMapsUniformBinding{
			.binding = 5,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = Engine::MAX_NUM_SUN_LIGHTS,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferPositionDepthUniformBinding{
			.binding = 6,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferNormalUniformBinding{
			.binding = 7,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferAlbedoUniformBinding{
			.binding = 8,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferPbrUniformBinding{
			.binding = 9,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding ssaoUniformBinding{
			.binding = 10,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding ssaoBlurUniformBinding{
			.binding = 11,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			viewLevelUniformLayoutBinding,
			lightsStorageBufferUniformBinding,
			shadowMapSamplerUniformBinding,
			spotShadowMapsUniformBinding,
			sphereShadowMapsUniformBinding,
			sunShadowMapsUniformBinding,
			gBufferPositionDepthUniformBinding,
			gBufferNormalUniformBinding,
			gBufferAlbedoUniformBinding,
			gBufferPbrUniformBinding,
			ssaoUniformBinding,
			ssaoBlurUniformBinding
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->viewLevelUniformWithSSAODescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->objectLevelUniformDescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->skyboxUniformDescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->mirrorMaterialLevelUniformDescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->environmentMaterialLevelUniformDescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->lambertianMaterialLevelUniformDescriptorSetLayout));
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
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->pbrMaterialLevelUniformDescriptorSetLayout));
	}
	{
		VkDescriptorSetLayoutBinding ssaoSampleUniformBufferBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding ssaoNoiseSamplerUniformBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferPositionDepthUniformBinding{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferNormalUniformBinding{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferAlbedoUniformBinding{
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		VkDescriptorSetLayoutBinding gBufferPbrUniformBinding{
			.binding = 5,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			ssaoSampleUniformBufferBinding,
			ssaoNoiseSamplerUniformBinding,
			gBufferPositionDepthUniformBinding,
			gBufferNormalUniformBinding,
			gBufferAlbedoUniformBinding,
			gBufferPbrUniformBinding
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->ssaoDescriptorSetLayout));
	}

	{
		VkDescriptorSetLayoutBinding ssaoUniformBufferBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr
		};
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			ssaoUniformBufferBinding
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(*this->context.device(), &layoutInfo, nullptr, &this->ssaoBlurDescriptorSetLayout));
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
			JJYOU_VK_UTILS_CHECK(vkCreateSemaphore(*this->context.device(), &semaphoreInfo, nullptr, &this->frameData[i].imageAvailableSemaphore));
			JJYOU_VK_UTILS_CHECK(vkCreateSemaphore(*this->context.device(), &semaphoreInfo, nullptr, &this->frameData[i].renderFinishedSemaphore));
			JJYOU_VK_UTILS_CHECK(vkCreateFence(*this->context.device(), &fenceInfo, nullptr, &this->frameData[i].inFlightFence));
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
			.pushConstantRangeCount = 0, // To set
			.pPushConstantRanges = nullptr // To set
		};

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->simpleForwardPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->mirrorMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->mirrorForwardPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->environmentMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->environmentForwardPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->lambertianMaterialLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->lambertianForwardPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->objectLevelUniformDescriptorSetLayout, this->pbrMaterialLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->pbrDeferredPipelineLayout));

		setLayouts = { this->viewLevelUniformDescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->skyboxPipelineLayout));

		setLayouts = { this->objectLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1U;
		VkPushConstantRange spotLightShadowMapPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.offset = 0U,
			.size = sizeof(Engine::SpotLightShadowMapUniform)
		};
		pipelineLayoutInfo.pPushConstantRanges = &spotLightShadowMapPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->spotlightPipelineLayout));

		setLayouts = { this->objectLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1U;
		VkPushConstantRange sphereLightShadowMapPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0U,
			.size = sizeof(Engine::SphereLightShadowMapUniform)
		};
		pipelineLayoutInfo.pPushConstantRanges = &sphereLightShadowMapPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->spherelightPipelineLayout));

		setLayouts = { this->objectLevelUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1U;
		VkPushConstantRange sunLightShadowMapPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT,
			.offset = 0U,
			.size = sizeof(Engine::SunLightShadowMapUniform)
		};
		pipelineLayoutInfo.pPushConstantRanges = &sunLightShadowMapPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->sunlightPipelineLayout));

		setLayouts = { this->ssaoDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		VkPushConstantRange ssaoPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0U,
			.size = sizeof(jjyou::glsl::mat4) + sizeof(int) + sizeof(float)
		};
		pipelineLayoutInfo.pPushConstantRanges = &ssaoPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->ssaoPipelineLayout));

		setLayouts = { this->ssaoBlurDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		VkPushConstantRange ssaoBlurPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0U,
			.size = sizeof(int)
		};
		pipelineLayoutInfo.pPushConstantRanges = &ssaoBlurPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->ssaoBlurPipelineLayout));

		setLayouts = { this->viewLevelUniformWithSSAODescriptorSetLayout, this->skyboxUniformDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		VkPushConstantRange deferredShadingCompositionPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0U,
			.size = 2 * sizeof(int)
		};
		pipelineLayoutInfo.pPushConstantRanges = &deferredShadingCompositionPushConstantRange;
		JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(*this->context.device(), &pipelineLayoutInfo, nullptr, &this->deferredShadingCompositionPipelineLayout));
	}

	// Init ImGui
	if (!this->offscreen) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Load font
		io.Fonts->AddFontDefault();

		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForVulkan(this->window, true);
		ImGui_ImplVulkan_InitInfo initInfo = {
			.Instance = *this->context.instance(),
			.PhysicalDevice = *this->context.physicalDevice(),
			.Device = *this->context.device(),
			.QueueFamily = *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main),
			.Queue = **this->context.queue(jjyou::vk::Context::QueueType::Main),
			.DescriptorPool = *this->descriptorPool,
			.RenderPass = this->outputRenderPass,
			.MinImageCount = this->swapchain.numImages(),
			.ImageCount = this->swapchain.numImages(),
			.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
			.PipelineCache = nullptr,
			.Subpass = 0,
			.UseDynamicRendering = false,
			// .PipelineRenderingCreateInfo = {},
			.Allocator = nullptr,
			.CheckVkResultFn = [](VkResult result) { if (result != VK_SUCCESS) throw std::runtime_error("[ImGui] Internal error."); },
			.MinAllocationSize = 0
		};
		ImGui_ImplVulkan_Init(&initInfo);
	}

	// Create graphics pipeline
	{
		vk::raii::ShaderModule simpleForwardVertShaderModule = this->createShaderModule("../spv/renderer/shader/simpleForward.vert.spv");
		vk::raii::ShaderModule simpleForwardFragShaderModule = this->createShaderModule("../spv/renderer/shader/simpleForward.frag.spv");
		
		vk::raii::ShaderModule materialForwardVertShaderModule = this->createShaderModule("../spv/renderer/shader/materialForward.vert.spv");
		vk::raii::ShaderModule mirrorForwardFragShaderModule = this->createShaderModule("../spv/renderer/shader/mirrorForward.frag.spv");
		vk::raii::ShaderModule environmentForwardFragShaderModule = this->createShaderModule("../spv/renderer/shader/environmentForward.frag.spv");
		vk::raii::ShaderModule lambertianForwardFragShaderModule = this->createShaderModule("../spv/renderer/shader/lambertianForward.frag.spv");
		
		vk::raii::ShaderModule pbrDeferredVertShaderModule = this->createShaderModule("../spv/renderer/shader/pbrDeferred.vert.spv");
		vk::raii::ShaderModule pbrDeferredFragShaderModule = this->createShaderModule("../spv/renderer/shader/pbrDeferred.frag.spv");

		vk::raii::ShaderModule skyboxVertShaderModule = this->createShaderModule("../spv/renderer/shader/skybox.vert.spv");
		vk::raii::ShaderModule skyboxFragShaderModule = this->createShaderModule("../spv/renderer/shader/skybox.frag.spv");
		
		vk::raii::ShaderModule spotlightVertShaderModule = this->createShaderModule("../spv/renderer/shader/spotlight.vert.spv");
		vk::raii::ShaderModule spherelightVertShaderModule = this->createShaderModule("../spv/renderer/shader/spherelight.vert.spv");
		vk::raii::ShaderModule spherelightGeomShaderModule = this->createShaderModule("../spv/renderer/shader/spherelight.geom.spv");
		vk::raii::ShaderModule spherelightFragShaderModule = this->createShaderModule("../spv/renderer/shader/spherelight.frag.spv");
		vk::raii::ShaderModule sunlightVertShaderModule = this->createShaderModule("../spv/renderer/shader/sunlight.vert.spv");
		vk::raii::ShaderModule sunlightGeomShaderModule = this->createShaderModule("../spv/renderer/shader/sunlight.geom.spv");

		vk::raii::ShaderModule fullscreenVertShaderModule = this->createShaderModule("../spv/renderer/shader/fullscreen.vert.spv");
		vk::raii::ShaderModule deferredShadingSSAOFragShaderModule = this->createShaderModule("../spv/renderer/shader/ssao.frag.spv");
		vk::raii::ShaderModule deferredShadingSSAOBlurFragShaderModule = this->createShaderModule("../spv/renderer/shader/ssaoBlur.frag.spv");
		vk::raii::ShaderModule deferredShadingCompositionFragShaderModule = this->createShaderModule("../spv/renderer/shader/deferredShadingComposition.frag.spv");


		std::array<VkPipelineShaderStageCreateInfo, 3> shaderStages = { {
			VkPipelineShaderStageCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_VERTEX_BIT, // To set
				.module = nullptr, // To set
				.pName = "main",
				.pSpecializationInfo = nullptr
			},
			VkPipelineShaderStageCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT, // To set
				.module = nullptr, // To set
				.pName = "main",
				.pSpecializationInfo = nullptr
			},
			VkPipelineShaderStageCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_GEOMETRY_BIT,  // To set
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

		std::vector<VkPipelineColorBlendAttachmentState> gBufferColorBlendAttachments(4, colorBlendAttachment);

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
			.stageCount = 2U,
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
			.renderPass = this->outputRenderPass, // to set
			.subpass = 0,
			.basePipelineHandle = nullptr,
			.basePipelineIndex = -1
		};

		// Rendering pipeline
		rasterizer.cullMode = (enableValidation) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

		shaderStages[0].module = *simpleForwardVertShaderModule;
		shaderStages[1].module = *simpleForwardFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &simpleVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(simpleAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = simpleAttributeDescriptions.data();
		pipelineInfo.layout = this->simpleForwardPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->simpleForwardPipeline));

		shaderStages[0].module = *materialForwardVertShaderModule;
		shaderStages[1].module = *mirrorForwardFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->mirrorForwardPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->mirrorForwardPipeline));

		shaderStages[0].module = *materialForwardVertShaderModule;
		shaderStages[1].module = *environmentForwardFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->environmentForwardPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->environmentForwardPipeline));

		shaderStages[0].module = *materialForwardVertShaderModule;
		shaderStages[1].module = *lambertianForwardFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->lambertianForwardPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->lambertianForwardPipeline));

		colorBlending.attachmentCount = 4;
		colorBlending.pAttachments = gBufferColorBlendAttachments.data();
		shaderStages[0].module = *pbrDeferredVertShaderModule;
		shaderStages[1].module = *pbrDeferredFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(materialAttributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = materialAttributeDescriptions.data();
		pipelineInfo.layout = this->pbrDeferredPipelineLayout;
		pipelineInfo.renderPass = *this->deferredRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->pbrDeferredPipeline));
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;
		shaderStages[0].module = *fullscreenVertShaderModule;
		shaderStages[1].module = *deferredShadingSSAOFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		pipelineInfo.layout = this->ssaoPipelineLayout;
		pipelineInfo.renderPass = *this->ssaoRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->ssaoPipeline));
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;

		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;
		shaderStages[0].module = *fullscreenVertShaderModule;
		shaderStages[1].module = *deferredShadingSSAOBlurFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		pipelineInfo.layout = this->ssaoBlurPipelineLayout;
		pipelineInfo.renderPass = *this->ssaoRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->ssaoBlurPipeline));
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;

		shaderStages[0].module = *fullscreenVertShaderModule;
		shaderStages[1].module = *deferredShadingCompositionFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		pipelineInfo.layout = this->deferredShadingCompositionPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->deferredShadingCompositionPipeline));

		shaderStages[0].module = *skyboxVertShaderModule;
		shaderStages[1].module = *skyboxFragShaderModule;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		pipelineInfo.layout = this->skyboxPipelineLayout;
		pipelineInfo.renderPass = this->outputRenderPass;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->skyboxPipeline));

		// Shadow mapping pipeline
		pipelineInfo.renderPass = *this->shadowMappingRenderPass;
		rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // Front face culling for shadow map

		shaderStages[0].module = *spotlightVertShaderModule;
		pipelineInfo.stageCount = 1;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1; // Only position is needed
		vertexInputInfo.pVertexAttributeDescriptions = &materialAttributeDescriptions[0];
		pipelineInfo.layout = this->spotlightPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->spotlightPipeline));

		shaderStages[0].module = *spherelightVertShaderModule;
		shaderStages[1].module = *spherelightFragShaderModule;
		shaderStages[2].module = *spherelightGeomShaderModule;
		pipelineInfo.stageCount = 3;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1; // Only position is needed
		vertexInputInfo.pVertexAttributeDescriptions = &materialAttributeDescriptions[0];
		pipelineInfo.layout = this->spherelightPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->spherelightPipeline));

		shaderStages[0].module = *sunlightVertShaderModule;
		shaderStages[1].module = *sunlightGeomShaderModule;
		shaderStages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
		pipelineInfo.stageCount = 2;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &materialVertexBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1; // Only position is needed
		vertexInputInfo.pVertexAttributeDescriptions = &materialAttributeDescriptions[0];
		pipelineInfo.layout = this->sunlightPipelineLayout;
		JJYOU_VK_UTILS_CHECK(vkCreateGraphicsPipelines(*this->context.device(), nullptr, 1, &pipelineInfo, nullptr, &this->sunlightPipeline));

	}

	// Create ssao samples and ssao noise textures
	// https://learnopengl.com/Advanced-Lighting/SSAO
	std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	std::default_random_engine generator;
	auto lerp = [](float a, float b, float f) {
		return a + f * (b - a);
		};
	for (std::uint32_t i = 0; i < 256; ++i) {
		jjyou::glsl::vec3 sample(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator)
		);
		jjyou::glsl::normalize(sample);
		sample *= randomFloats(generator);
		float scale = static_cast<float>(i % 64) / 256.0f;
		scale = lerp(0.1f, 1.0f, scale * scale);
		sample *= scale;
		this->ssaoParameters.ssaoSamples[i] = jjyou::glsl::vec4(sample, 1.0f);
	}
	std::array<jjyou::glsl::vec4, 16> ssaoNoiseData;
	for (unsigned int i = 0; i < 16; i++) {
		jjyou::glsl::vec4 noise(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			0.0f,
			0.0f
		);
		ssaoNoiseData[i] = noise;
	}
	this->ssaoNoise.create(
		this->context,
		this->allocator,
		this->graphicsCommandPool,
		this->transferCommandPool,
		ssaoNoiseData.data(),
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VkExtent2D{ .width = 4,.height = 4 }
	);
}

Engine::~Engine(void) {

	vkDeviceWaitIdle(*this->context.device());

	this->pScene72 = nullptr;

	this->ssaoNoise.destroy();

	//Destroy pipeline and pipeline layout
	vkDestroyPipeline(*this->context.device(), this->simpleForwardPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->simpleForwardPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->mirrorForwardPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->mirrorForwardPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->environmentForwardPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->environmentForwardPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->lambertianForwardPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->lambertianForwardPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->pbrDeferredPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->pbrDeferredPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->ssaoPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->ssaoPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->ssaoBlurPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->ssaoBlurPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->deferredShadingCompositionPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->deferredShadingCompositionPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->skyboxPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->spotlightPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->spotlightPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->spherelightPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->spherelightPipelineLayout, nullptr);
	vkDestroyPipeline(*this->context.device(), this->sunlightPipeline, nullptr);
	vkDestroyPipelineLayout(*this->context.device(), this->sunlightPipelineLayout, nullptr);

	// Destroy sync objects
	for (size_t i = 0; i < Engine::MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(*this->context.device(), this->frameData[i].imageAvailableSemaphore, nullptr);
		vkDestroySemaphore(*this->context.device(), this->frameData[i].renderFinishedSemaphore, nullptr);
		vkDestroyFence(*this->context.device(), this->frameData[i].inFlightFence, nullptr);
	}

	// Destroy descriptor layout
	vkDestroyDescriptorSetLayout(*this->context.device(), this->viewLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->objectLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->skyboxUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->mirrorMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->environmentMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->lambertianMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->pbrMaterialLevelUniformDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->viewLevelUniformWithSSAODescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->ssaoDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(*this->context.device(), this->ssaoBlurDescriptorSetLayout, nullptr);

	// Destroy frame buffers
	for (int i = 0; i < this->framebuffers.size(); ++i) {
		vkDestroyFramebuffer(*this->context.device(), this->framebuffers[i], nullptr);
	}

	// Destroy render pass
	vkDestroyRenderPass(*this->context.device(), this->outputRenderPass, nullptr);
	this->shadowMappingRenderPass.clear();
	this->deferredRenderPass.clear();
	this->ssaoRenderPass.clear();

	// Destroy g buffer
	this->gBuffer.~GBuffer();

	// Destroy ssao buffer
	this->ssao.~SSAO();

	// Destroy depth image
	vkDestroyImageView(*this->context.device(), this->depthImageView, nullptr);
	vkDestroyImage(*this->context.device(), this->depthImage, nullptr);
	this->allocator.free(this->depthImageMemory);

	// Destroy
	if (this->offscreen)
		this->virtualSwapchain.~VirtualSwapchain();
	else
		this->swapchain.~Swapchain();

	// Deinit UI
	if (!this->offscreen) {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
	}

	// Destroy allocator
	this->allocator.destory();

	// Command buffers will be destroyed along with the command pool

	// Destroy command pool
	vkDestroyCommandPool(*this->context.device(), this->graphicsCommandPool, nullptr);
	vkDestroyCommandPool(*this->context.device(), this->transferCommandPool, nullptr);

	// Destroy descriptor pool
	this->descriptorPool.clear();

	// Destroy glfw window
	if (!this->offscreen) {
		glfwTerminate();
		glfwDestroyWindow(this->window);
	}
}

void Engine::createSwapchain(void) {
	this->swapchain.~Swapchain();
	jjyou::vk::SwapchainBuilder builder(this->context, this->surface);
	builder
		.requestSurfaceFormat(VkSurfaceFormatKHR{ .format = VK_FORMAT_B8G8R8A8_SRGB , .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.requestPresentMode(::vk::PresentModeKHR::eMailbox);
	int width, height;
	glfwGetFramebufferSize(this->window, &width, &height);
	this->swapchain = builder.build(vk::Extent2D(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)));
}

void Engine::createFramebuffers(void) {
	if (!this->offscreen) {
		this->framebuffers.resize(this->swapchain.numImages());
		for (size_t i = 0; i < this->framebuffers.size(); i++) {
			std::array<VkImageView, 2> attachments = {
				*this->swapchain.imageView(static_cast<std::uint32_t>(i)),
				this->depthImageView
			};

			VkFramebufferCreateInfo framebufferInfo{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.renderPass = this->outputRenderPass,
				.attachmentCount = static_cast<uint32_t>(attachments.size()),
				.pAttachments = attachments.data(),
				.width = this->swapchain.extent().width,
				.height = this->swapchain.extent().height,
				.layers = 1
			};
			JJYOU_VK_UTILS_CHECK(vkCreateFramebuffer(*this->context.device(), &framebufferInfo, nullptr, &this->framebuffers[i]));
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
				.renderPass = this->outputRenderPass,
				.attachmentCount = static_cast<uint32_t>(attachments.size()),
				.pAttachments = attachments.data(),
				.width = this->virtualSwapchain.extent().width,
				.height = this->virtualSwapchain.extent().height,
				.layers = 1
			};
			JJYOU_VK_UTILS_CHECK(vkCreateFramebuffer(*this->context.device(), &framebufferInfo, nullptr, &this->framebuffers[i]));
		}
	}
}

void Engine::createDepthImage(void) {
	this->depthImageFormat = VK_FORMAT_D32_SFLOAT;
	/*static_cast<VkFormat>(this->context.findSupportedFormat(
		{ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment
	));*/
	VkExtent2D extent = (this->offscreen) ? this->virtualSwapchain.extent() : static_cast<VkExtent2D>(this->swapchain.extent());
	std::tie(this->depthImage, this->depthImageMemory) = this->createImage(
		extent.width, extent.height,
		this->depthImageFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		{ *this->context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main) },
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);
	this->depthImageView = this->createImageView(this->depthImage, this->depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}