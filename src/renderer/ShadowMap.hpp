#pragma once
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/vk/Legacy/Memory.hpp>

class ShadowMap {

public:

	enum class Type {
		Undefined = 0,
		Perspective = 1,
		Omnidirectional = 2,
		Cascade = 3
	};

	/** @brief	Construct an empty shadow map.
			  */
	ShadowMap(std::nullptr_t) {}

	/** @brief	Copy constructor is disabled.
	  */
	ShadowMap(const ShadowMap&) = delete;

	/** @brief	Move constructor.
	  */
	ShadowMap(ShadowMap&& other) = default;

	/** @brief	Destructor.
	  */
	~ShadowMap(void) = default;

	/** @brief	Copy assignment is disabled.
	  */
	ShadowMap& operator=(const ShadowMap&) = delete;

	/** @brief	Move assignment.
	  */
	ShadowMap& operator=(ShadowMap&& other) noexcept {
		if (this != &other) {
			this->_pContext = other._pContext;
			this->_pAllocator = other._pAllocator;
			this->_type = other._type;
			this->_extent = other._extent;
			this->_format = other._format;
			this->_numLayers = other._numLayers;
			this->_image = std::move(other._image);
			this->_imageMemory = std::move(other._imageMemory);
			this->_outputImageView = std::move(other._outputImageView);
			this->_framebuffer = std::move(other._framebuffer);
			this->_inputImageView = std::move(other._inputImageView);
		}
		return *this;
	}

	/** @brief	Create a shadow map.
	  */
	ShadowMap(
		const jjyou::vk::Context& context,
		jjyou::vk::MemoryAllocator& allocator,
		Type type,
		vk::Extent2D extent,
		vk::Format format,
		std::uint32_t numLayers, /* 1 for perspective, 6 for omnidirectional, number of levels for cascade */
		const vk::raii::RenderPass& renderPass
	) : _pContext(&context), _pAllocator(&allocator), _type(type), _extent(extent), _format(format)
	{
		switch (this->_type) {
		case Type::Perspective:
			if (numLayers != 1U)
				throw std::runtime_error("[Shadow Map] The number of layers for perspective shadow map should be 1.");
			this->_numLayers = 1U;
			break;
		case Type::Omnidirectional:
			if (numLayers != 6U)
				throw std::runtime_error("[Shadow Map] The number of layers for omnidirectional shadow map should be 6.");
			this->_numLayers = 6U;
			break;
		case Type::Cascade:
			this->_numLayers = numLayers;
			break;
		default:
			throw std::runtime_error("[Shadow Map] Unknown shadow map type.");
		}
		vk::DeviceSize elementSize = 0;
		switch (this->_format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD16UnormS8Uint:
			elementSize = 2;
			break;
		case vk::Format::eD24UnormS8Uint:
			elementSize = 3;
			break;
		case vk::Format::eD32Sfloat:
		case vk::Format::eD32SfloatS8Uint:
			elementSize = 4;
			break;
		default:
			throw std::runtime_error("[Shadow Map] Unsupported shadow map format.");
		}
		std::uint32_t graphicsQueueFamily = *this->_pContext->queueFamilyIndex(jjyou::vk::Context::QueueType::Main);
		// Create the image
		vk::ImageCreateInfo imageCreateInfo(
			(this->_type == Type::Omnidirectional) ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags(0U),
			vk::ImageType::e2D,
			this->_format,
			vk::Extent3D(this->_extent, 1U),
			1U,
			this->_numLayers,
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::SharingMode::eExclusive,
			1U,
			&graphicsQueueFamily,
			vk::ImageLayout::eUndefined
		);
		this->_image = this->_pContext->device().createImage(imageCreateInfo);
		vk::MemoryRequirements imageMemoryRequirements = this->_image.getMemoryRequirements();
		vk::MemoryAllocateInfo imageMemoryAllocInfo(
			imageMemoryRequirements.size,
			this->_pContext->findMemoryType(imageMemoryRequirements.memoryTypeBits, ::vk::MemoryPropertyFlagBits::eDeviceLocal).value()
		);
		JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(reinterpret_cast<VkMemoryAllocateInfo*>(&imageMemoryAllocInfo), this->_imageMemory));
		this->_image.bindMemory(this->_imageMemory.memory(), this->_imageMemory.offset());
		// Create the output image view and framebuffer
		vk::ImageViewCreateInfo imageViewCreateInfo(
			vk::ImageViewCreateFlagBits(0U),
			*this->_image,
			(this->_type == Type::Perspective) ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray,
			this->_format,
			vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0U, 1U, 0U, this->_numLayers)
		);
		this->_outputImageView = vk::raii::ImageView(
			this->_pContext->device(),
			imageViewCreateInfo
		);
		vk::FramebufferCreateInfo framebufferCreateInfo(
			vk::FramebufferCreateFlags(0U),
			*renderPass,
			1U,
			&*this->_outputImageView,
			this->_extent.width,
			this->_extent.height,
			this->_numLayers
		);
		this->_framebuffer = vk::raii::Framebuffer(
			this->_pContext->device(),
			framebufferCreateInfo
		);
		// Create the input image views
		switch (this->_type) {
		case Type::Perspective: {
			imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
			break;
		}
		case Type::Omnidirectional: {
			imageViewCreateInfo.viewType = vk::ImageViewType::eCube;
			break;
		}
		case Type::Cascade: {
			imageViewCreateInfo.viewType = vk::ImageViewType::e2DArray;
			break;
		}
		default:
			break;
		}
		this->_inputImageView = vk::raii::ImageView(
			this->_pContext->device(),
			imageViewCreateInfo
		);
	}

	/** @brief	Get the shadow map type.
	  * @return Shadow map type.
	  */
	Type type(void) const { return this->_type; }

	/** @brief	Get the texture image extent.
	  * @return Texture image extent.
	  */
	vk::Extent2D extent(void) const { return this->_extent; }

	/** @brief	Get the texture format.
	  * @return Texture format.
	  */
	vk::Format format(void) const { return this->_format; }

	/** @brief	Get the number of texture layers.
	  * @return Number of texture image layers.
	  */
	std::uint32_t numLayers(void) const { return this->_numLayers; }

	/** @brief	Get the texture image mipmap levels.
	  * @return Texture image mipmap levels.
	  */
	std::uint32_t mipLevels(void) const { return 1U; }

	/** @brief	Get the image for this texture.
	  * @return `vk::raii::Image` instance.
	  */
	const vk::raii::Image& image(void) const { return this->_image; }

	/** @brief	Get the output image view for this texture.
	  * @return `vk::raii::ImageView` instance.
	  *
	  * For perspective shadow map,
	  * the returned image view is a 2D view.
	  * For omnidirectional shadow map,
	  * the returned image view is a 2D view with 6 layers.
	  * For cascade shadow map,
	  * the returned image view is a 2D view with user specified number of layers.
	  */
	const vk::raii::ImageView& outputImageView(void) const { return this->_outputImageView; }
	
	/** @brief	Get the output framebuffer for this texture.
	  * @return `vk::raii::Framebuffer` instance.
	  *
	  * @sa ShadowMap::outputImageView
	  */
	const vk::raii::Framebuffer& framebuffer(void) const { return this->_framebuffer; }

	/** @brief	Get the input image view for this texture.
	  * @return `vk::raii::ImageView` instance.
	  *
	  * For perspective shadow map,
	  * the returned image view is a 2D view.
	  * For omnidirectional shadow map,
	  * the returned image view is a cube view.
	  * For cascade shadow map,
	  * the returned image view is a 2D array view.
	  */
	const vk::raii::ImageView& inputImageView(void) const { return this->_inputImageView; }

	/** @brief	Transfer image layout from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL.
	  * @note	This method is only for placeholder shadow maps.
	  */
	void transferImageLayout(
		VkCommandPool commandPool,
		VkQueue queue,
		std::uint32_t queueFamilyIndex
	) const {
		VkCommandBuffer commandBuffer{};
		VkFence fence{};
		{
			VkCommandBufferAllocateInfo commandBufferAllocInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = commandPool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(*this->_pContext->device(), &commandBufferAllocInfo, &commandBuffer));
			VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.pNext = nullptr,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
				.pInheritanceInfo = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		}
		{
			VkFenceCreateInfo fenceCreateInfo{
				.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.pNext = nullptr,
				.flags = VkFenceCreateFlags(0)
			};
			vkCreateFence(*this->_pContext->device(), &fenceCreateInfo, nullptr, &fence);
		}
		{
			VkImageMemoryBarrier imageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = VK_ACCESS_NONE,
				.dstAccessMask = VK_ACCESS_NONE,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = queueFamilyIndex,
				.dstQueueFamilyIndex = queueFamilyIndex,
				.image = *this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = this->_numLayers
				}
			};
			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier
			);
		}
		{
			JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(commandBuffer));
			VkSubmitInfo submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.pNext = nullptr,
				.waitSemaphoreCount = 0U,
				.pWaitSemaphores = nullptr,
				.pWaitDstStageMask = 0,
				.commandBufferCount = 1,
				.pCommandBuffers = &commandBuffer,
				.signalSemaphoreCount = 0U,
				.pSignalSemaphores = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
			JJYOU_VK_UTILS_CHECK(vkWaitForFences(*this->_pContext->device(), 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()));
			vkFreeCommandBuffers(*this->_pContext->device(), commandPool, 1, &commandBuffer);
			vkDestroyFence(*this->_pContext->device(), fence, nullptr);
		}
	}

private:

	const jjyou::vk::Context* _pContext = nullptr;
	jjyou::vk::MemoryAllocator* _pAllocator = nullptr;
	Type _type = Type::Undefined;
	vk::Extent2D _extent{};
	vk::Format _format = vk::Format::eUndefined;
	std::uint32_t _numLayers = 0;
	vk::raii::Image _image{ nullptr };
	jjyou::vk::Memory _imageMemory{};
	vk::raii::ImageView _outputImageView{ nullptr };
	vk::raii::Framebuffer _framebuffer{ nullptr };
	vk::raii::ImageView _inputImageView{ nullptr };

};