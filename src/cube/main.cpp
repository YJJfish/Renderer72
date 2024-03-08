#include "fwd.hpp"
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "TinyArgParser.hpp"
#include "HostImage.hpp"

class Texture2D {

public:

	/** @brief	Default constructor.
	  */
	Texture2D(void) {}

	/** @brief	Destructor.
	  */
	~Texture2D(void) {}

	/** @brief	Create a texture.
	  */
	void create(
		const jjyou::vk::PhysicalDevice& physicalDevice,
		const jjyou::vk::Device& device,
		jjyou::vk::MemoryAllocator& allocator,
		VkCommandPool computeCommandPool,
		VkCommandPool transferCommandPool,
		const void* data,
		VkFormat format,
		VkExtent2D extent,
		bool cubeMap = false
	) {
		this->_pDevice = &device;
		this->_pAllocator = &allocator;
		this->_extent = extent;
		this->_format = format;
		VkDeviceSize elementSize = 0;
		switch (this->_format) {
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
			elementSize = 1;
			break;
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_USCALED:
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SNORM:
		case VK_FORMAT_B8G8R8_USCALED:
		case VK_FORMAT_B8G8R8_SSCALED:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_B8G8R8_SRGB:
			elementSize = 3;
			break;
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
			elementSize = 4;
			break;
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
			elementSize = 4;
			break;
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32_SFLOAT:
			elementSize = 12;
			break;
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			elementSize = 16;
			break;
		default:
			JJYOU_VK_UTILS_THROW(VK_ERROR_FORMAT_NOT_SUPPORTED);
		}
		// Create a stagine buffer.
		const VkDeviceSize bufferSize = elementSize * extent.width * extent.height * (cubeMap ? 6 : 1);
		std::uint32_t numLayers = (cubeMap ? 6 : 1);
		std::uint32_t transferQueueFamily = *physicalDevice.transferQueueFamily();
		std::uint32_t computeQueueFamily = *physicalDevice.computeQueueFamily();
		VkBuffer stagingBuffer = nullptr;
		jjyou::vk::Memory stagingBufferMemory{};
		VkBufferCreateInfo bufferInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.size = bufferSize,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 1U,
			.pQueueFamilyIndices = &transferQueueFamily
		};
		JJYOU_VK_UTILS_CHECK(vkCreateBuffer(this->_pDevice->get(), &bufferInfo, nullptr, &stagingBuffer));
		VkMemoryRequirements stagingBufferMemoryRequirements;
		vkGetBufferMemoryRequirements(this->_pDevice->get(), stagingBuffer, &stagingBufferMemoryRequirements);
		VkMemoryAllocateInfo stagingBufferMemoryAllocInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = nullptr,
			.allocationSize = stagingBufferMemoryRequirements.size,
			.memoryTypeIndex = physicalDevice.findMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT).value()
		};
		JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(&stagingBufferMemoryAllocInfo, stagingBufferMemory));
		vkBindBufferMemory(this->_pDevice->get(), stagingBuffer, stagingBufferMemory.memory(), stagingBufferMemory.offset());
		// Copy data to staging buffer
		this->_pAllocator->map(stagingBufferMemory);
		std::memcpy(stagingBufferMemory.mappedAddress(), data, bufferSize);
		this->_pAllocator->unmap(stagingBufferMemory);
		// Create the image
		VkImageCreateInfo imageInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = (cubeMap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : VkImageCreateFlags(0)),
			.imageType = VK_IMAGE_TYPE_2D,
			.format = this->_format,
			.extent{
				.width = this->_extent.width,
				.height = this->_extent.height,
				.depth = 1
			},
			.mipLevels = 1,
			.arrayLayers = numLayers,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 1U,
			.pQueueFamilyIndices = &transferQueueFamily,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};
		JJYOU_VK_UTILS_CHECK(vkCreateImage(this->_pDevice->get(), &imageInfo, nullptr, &this->_image));
		VkMemoryRequirements imageMemoryRequirements;
		vkGetImageMemoryRequirements(this->_pDevice->get(), this->_image, &imageMemoryRequirements);
		VkMemoryAllocateInfo imageMemoryAllocInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = nullptr,
			.allocationSize = imageMemoryRequirements.size,
			.memoryTypeIndex = physicalDevice.findMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).value()
		};
		JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(&imageMemoryAllocInfo, this->_imageMemory));
		vkBindImageMemory(this->_pDevice->get(), this->_image, this->_imageMemory.memory(), this->_imageMemory.offset());
		// Create and begin transfer command buffer
		VkCommandBuffer transferCommandBuffer, computeCommandBuffer;
		// Transfer image layout
		transferCommandBuffer = Texture2D::_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
		VkImageMemoryBarrier imageMemoryBarrier1{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = transferQueueFamily,
			.dstQueueFamilyIndex = transferQueueFamily,
			.image = this->_image,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = numLayers
			}
		};
		vkCmdPipelineBarrier(
			transferCommandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier1
		);
		Texture2D::_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
		// Copy buffer to image
		transferCommandBuffer = Texture2D::_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
		VkBufferImageCopy copyRegion{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = numLayers,
			},
			.imageOffset = {
				.x = 0,
				.y = 0,
				.z = 0
			},
			.imageExtent = {
				.width = this->_extent.width,
				.height = this->_extent.height,
				.depth = 1
			}
		};
		vkCmdCopyBufferToImage(transferCommandBuffer, stagingBuffer, this->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		Texture2D::_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
		// Transfer image layout
		if (transferQueueFamily != computeQueueFamily) {
			transferCommandBuffer = Texture2D::_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
			VkImageMemoryBarrier imageMemoryBarrier2{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_NONE,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.srcQueueFamilyIndex = transferQueueFamily,
				.dstQueueFamilyIndex = computeQueueFamily,
				.image = this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = numLayers
				}
			};
			vkCmdPipelineBarrier(
				transferCommandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier2
			);
			Texture2D::_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
			computeCommandBuffer = Texture2D::_beginCommandBuffer(this->_pDevice->get(), computeCommandPool);
			VkImageMemoryBarrier imageMemoryBarrier3{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = VK_ACCESS_NONE,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = computeQueueFamily,
				.dstQueueFamilyIndex = computeQueueFamily,
				.image = this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = numLayers
				}
			};
			vkCmdPipelineBarrier(
				computeCommandBuffer,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier3
			);
			Texture2D::_endCommandBuffer(this->_pDevice->get(), computeCommandPool, computeCommandBuffer, *this->_pDevice->computeQueues(), nullptr, nullptr);
		}
		else {
			transferCommandBuffer = Texture2D::_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
			VkImageMemoryBarrier imageMemoryBarrier2{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = transferQueueFamily,
				.dstQueueFamilyIndex = transferQueueFamily,
				.image = this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = numLayers
				}
			};
			vkCmdPipelineBarrier(
				transferCommandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier2
			);
			Texture2D::_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
		}
		// Destroy staging buffer
		this->_pAllocator->free(stagingBufferMemory);
		vkDestroyBuffer(this->_pDevice->get(), stagingBuffer, nullptr);
		// Create the image view
		VkImageViewCreateInfo viewInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.image = this->_image,
			.viewType = (cubeMap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D),
			.format = this->_format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = numLayers
			}
		};
		JJYOU_VK_UTILS_CHECK(vkCreateImageView(this->_pDevice->get(), &viewInfo, nullptr, &this->_imageView));
		// Create the sampler
		VkSamplerCreateInfo samplerInfo{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = physicalDevice.deviceProperties().limits.maxSamplerAnisotropy,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		JJYOU_VK_UTILS_CHECK(vkCreateSampler(this->_pDevice->get(), &samplerInfo, nullptr, &this->_sampler));
	}

	/** @brief	Check whether the class contains a valid texture.
	  * @return `true` if not empty.
	  */
	bool has_value() const {
		return (this->_pDevice != nullptr);
	}

	/** @brief	Call the corresponding vkDestroyXXX function to destroy the wrapped instance.
	  */
	void destroy(void) {
		if (this->_pDevice != nullptr) {
			this->_pAllocator->free(this->_imageMemory);
			this->_pAllocator = nullptr;
			vkDestroySampler(this->_pDevice->get(), this->_sampler, nullptr);
			this->_sampler = nullptr;
			vkDestroyImageView(this->_pDevice->get(), this->_imageView, nullptr);
			this->_imageView = nullptr;
			vkDestroyImage(this->_pDevice->get(), this->_image, nullptr);
			this->_image = nullptr;
			this->_extent = { .width = 0, .height = 0 };
			this->_format = VK_FORMAT_UNDEFINED;
			this->_pDevice = nullptr;
		}
	}

	/** @brief	Get the sampler for this texture.
	  * @return `VkSampler` instance.
	  */
	VkSampler sampler(void) const { return this->_sampler; }

	/** @brief	Get the image for this texture.
	  * @return `VkImage` instance.
	  */
	VkImage image(void) const { return this->_image; }

	/** @brief	Get the image view for this texture.
	  * @return `VkImageView` instance.
	  */
	VkImageView imageView(void) const { return this->_imageView; }

	/** @brief	Get the texture image extent.
	  * @return Texture image extent.
	  */
	VkExtent2D extent(void) const { return this->_extent; }

	/** @brief	Get the texture format.
	  * @return Texture format.
	  */
	VkFormat format(void) const { return this->_format; }

private:

	const jjyou::vk::Device* _pDevice = nullptr;
	VkExtent2D _extent{};
	VkFormat _format = VK_FORMAT_UNDEFINED;
	jjyou::vk::MemoryAllocator* _pAllocator = nullptr;
	VkImage _image = nullptr;
	jjyou::vk::Memory _imageMemory{};
	VkImageView _imageView = nullptr;
	VkSampler _sampler = nullptr;
	static VkCommandBuffer _beginCommandBuffer(VkDevice device, VkCommandPool commandPool) {
		VkCommandBuffer commandBuffer;
		VkCommandBufferAllocateInfo commandBufferAllocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocInfo, &commandBuffer));
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = nullptr
		};
		JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		return commandBuffer;
	}
	static void _endCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore) {
		JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(commandBuffer));
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = (waitSemaphore == nullptr) ? 0U : 1U,
			.pWaitSemaphores = &waitSemaphore,
			.pWaitDstStageMask = 0,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer,
			.signalSemaphoreCount = (signalSemaphore == nullptr) ? 0U : 1U,
			.pSignalSemaphores = &signalSemaphore
		};
		JJYOU_VK_UTILS_CHECK(vkQueueSubmit(queue, 1, &submitInfo, nullptr));
		JJYOU_VK_UTILS_CHECK(vkQueueWaitIdle(queue));
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	};

};

class StorageImage2DArray {

public:

	/** @brief	Default constructor.
	  */
	StorageImage2DArray(void) {}

	/** @brief	Destructor.
	  */
	~StorageImage2DArray(void) {}

	/** @brief	Create a texture.
	  */
	void create(
		const jjyou::vk::PhysicalDevice& physicalDevice,
		const jjyou::vk::Device& device,
		jjyou::vk::MemoryAllocator& allocator,
		VkCommandPool computeCommandPool,
		VkCommandPool transferCommandPool,
		const void* data,
		VkFormat format,
		VkExtent2D extent,
		std::uint32_t numLayers
	) {
		this->_pDevice = &device;
		this->_pAllocator = &allocator;
		this->_extent = extent;
		this->_numLayers = numLayers;
		this->_format = format;
		VkDeviceSize elementSize = 0;
		switch (this->_format) {
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
			elementSize = 1;
			break;
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_USCALED:
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SNORM:
		case VK_FORMAT_B8G8R8_USCALED:
		case VK_FORMAT_B8G8R8_SSCALED:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_B8G8R8_SRGB:
			elementSize = 3;
			break;
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
			elementSize = 4;
			break;
		case VK_FORMAT_R32G32_UINT:
		case VK_FORMAT_R32G32_SINT:
		case VK_FORMAT_R32G32_SFLOAT:
			elementSize = 8;
			break;
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
			elementSize = 4;
			break;
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32_SFLOAT:
			elementSize = 12;
			break;
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			elementSize = 16;
			break;
		default:
			JJYOU_VK_UTILS_THROW(VK_ERROR_FORMAT_NOT_SUPPORTED);
		}
		std::uint32_t transferQueueFamily = *physicalDevice.transferQueueFamily();
		std::uint32_t computeQueueFamily = *physicalDevice.computeQueueFamily();
		VkCommandBuffer transferCommandBuffer, computeCommandBuffer;
		// Copy data and transition image layout
		if (data != nullptr) {
			// Create a stagine buffer.
			const VkDeviceSize bufferSize = elementSize * this->_extent.width * this->_extent.height * this->_numLayers;
			VkBuffer stagingBuffer = nullptr;
			jjyou::vk::Memory stagingBufferMemory{};
			VkBufferCreateInfo bufferInfo{
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.size = bufferSize,
				.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
				.queueFamilyIndexCount = 1U,
				.pQueueFamilyIndices = &transferQueueFamily
			};
			JJYOU_VK_UTILS_CHECK(vkCreateBuffer(this->_pDevice->get(), &bufferInfo, nullptr, &stagingBuffer));
			VkMemoryRequirements stagingBufferMemoryRequirements;
			vkGetBufferMemoryRequirements(this->_pDevice->get(), stagingBuffer, &stagingBufferMemoryRequirements);
			VkMemoryAllocateInfo stagingBufferMemoryAllocInfo{
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.pNext = nullptr,
				.allocationSize = stagingBufferMemoryRequirements.size,
				.memoryTypeIndex = physicalDevice.findMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT).value()
			};
			JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(&stagingBufferMemoryAllocInfo, stagingBufferMemory));
			vkBindBufferMemory(this->_pDevice->get(), stagingBuffer, stagingBufferMemory.memory(), stagingBufferMemory.offset());
			// Copy data to staging buffer
			this->_pAllocator->map(stagingBufferMemory);
			std::memcpy(stagingBufferMemory.mappedAddress(), data, bufferSize);
			this->_pAllocator->unmap(stagingBufferMemory);
			// Create the image
			VkImageCreateInfo imageInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = this->_format,
				.extent{
					.width = this->_extent.width,
					.height = this->_extent.height,
					.depth = 1
				},
				.mipLevels = 1,
				.arrayLayers = this->_numLayers,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
				.queueFamilyIndexCount = 1U,
				.pQueueFamilyIndices = &transferQueueFamily,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
			};
			JJYOU_VK_UTILS_CHECK(vkCreateImage(this->_pDevice->get(), &imageInfo, nullptr, &this->_image));
			VkMemoryRequirements imageMemoryRequirements;
			vkGetImageMemoryRequirements(this->_pDevice->get(), this->_image, &imageMemoryRequirements);
			VkMemoryAllocateInfo imageMemoryAllocInfo{
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.pNext = nullptr,
				.allocationSize = imageMemoryRequirements.size,
				.memoryTypeIndex = physicalDevice.findMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).value()
			};
			JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(&imageMemoryAllocInfo, this->_imageMemory));
			vkBindImageMemory(this->_pDevice->get(), this->_image, this->_imageMemory.memory(), this->_imageMemory.offset());
			// Transition image layout
			transferCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
			VkImageMemoryBarrier imageMemoryBarrier1{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.srcQueueFamilyIndex = transferQueueFamily,
				.dstQueueFamilyIndex = transferQueueFamily,
				.image = this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = this->_numLayers
				}
			};
			vkCmdPipelineBarrier(
				transferCommandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier1
			);
			this->_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
			// Copy buffer to image
			transferCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
			VkBufferImageCopy copyRegion{
				.bufferOffset = 0,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = this->_numLayers,
				},
				.imageOffset = {
					.x = 0,
					.y = 0,
					.z = 0
				},
				.imageExtent = {
					.width = this->_extent.width,
					.height = this->_extent.height,
					.depth = 1
				}
			};
			vkCmdCopyBufferToImage(transferCommandBuffer, stagingBuffer, this->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
			this->_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
			// Transition image layout
			if (transferQueueFamily != computeQueueFamily) {
				transferCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
				VkImageMemoryBarrier imageMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.pNext = nullptr,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_NONE,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.srcQueueFamilyIndex = transferQueueFamily,
					.dstQueueFamilyIndex = computeQueueFamily,
					.image = this->_image,
					.subresourceRange{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = this->_numLayers
					}
				};
				vkCmdPipelineBarrier(
					transferCommandBuffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &imageMemoryBarrier2
				);
				this->_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
				computeCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), computeCommandPool);
				VkImageMemoryBarrier imageMemoryBarrier3{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.pNext = nullptr,
					.srcAccessMask = VK_ACCESS_NONE,
					.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.srcQueueFamilyIndex = computeQueueFamily,
					.dstQueueFamilyIndex = computeQueueFamily,
					.image = this->_image,
					.subresourceRange{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = this->_numLayers
					}
				};
				vkCmdPipelineBarrier(
					computeCommandBuffer,
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &imageMemoryBarrier3
				);
				this->_endCommandBuffer(this->_pDevice->get(), computeCommandPool, computeCommandBuffer, *this->_pDevice->computeQueues(), nullptr, nullptr);
			}
			else {
				transferCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), transferCommandPool);
				VkImageMemoryBarrier imageMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.pNext = nullptr,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.srcQueueFamilyIndex = transferQueueFamily,
					.dstQueueFamilyIndex = transferQueueFamily,
					.image = this->_image,
					.subresourceRange{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = this->_numLayers
					}
				};
				vkCmdPipelineBarrier(
					transferCommandBuffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &imageMemoryBarrier2
				);
				this->_endCommandBuffer(this->_pDevice->get(), transferCommandPool, transferCommandBuffer, *this->_pDevice->transferQueues(), nullptr, nullptr);
			}
			// Destroy staging buffer
			this->_pAllocator->free(stagingBufferMemory);
			vkDestroyBuffer(this->_pDevice->get(), stagingBuffer, nullptr);
		}
		else {
			// Create the image
			VkImageCreateInfo imageInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = this->_format,
				.extent{
					.width = this->_extent.width,
					.height = this->_extent.height,
					.depth = 1
				},
				.mipLevels = 1,
				.arrayLayers = this->_numLayers,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
				.queueFamilyIndexCount = 1U,
				.pQueueFamilyIndices = &computeQueueFamily,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
			};
			JJYOU_VK_UTILS_CHECK(vkCreateImage(this->_pDevice->get(), &imageInfo, nullptr, &this->_image));
			VkMemoryRequirements imageMemoryRequirements;
			vkGetImageMemoryRequirements(this->_pDevice->get(), this->_image, &imageMemoryRequirements);
			VkMemoryAllocateInfo imageMemoryAllocInfo{
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.pNext = nullptr,
				.allocationSize = imageMemoryRequirements.size,
				.memoryTypeIndex = physicalDevice.findMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).value()
			};
			JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(&imageMemoryAllocInfo, this->_imageMemory));
			vkBindImageMemory(this->_pDevice->get(), this->_image, this->_imageMemory.memory(), this->_imageMemory.offset());
			// Transition image layout
			computeCommandBuffer = this->_beginCommandBuffer(this->_pDevice->get(), computeCommandPool);
			VkImageMemoryBarrier imageMemoryBarrier1{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.pNext = nullptr,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = computeQueueFamily,
				.dstQueueFamilyIndex = computeQueueFamily,
				.image = this->_image,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = this->_numLayers
				}
			};
			vkCmdPipelineBarrier(
				computeCommandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier1
			);
			this->_endCommandBuffer(this->_pDevice->get(), computeCommandPool, computeCommandBuffer, *this->_pDevice->computeQueues(), nullptr, nullptr);
		}
		// Create the image view
		VkImageViewCreateInfo viewInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.image = this->_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = this->_format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0, // To set
				.layerCount = 1
			}
		};
		this->_imageView.resize(this->_numLayers);
		for (std::uint32_t i = 0; i < this->_numLayers; ++i) {
			viewInfo.subresourceRange.baseArrayLayer = i;
			JJYOU_VK_UTILS_CHECK(vkCreateImageView(this->_pDevice->get(), &viewInfo, nullptr, &this->_imageView[i]));
		}
	}

	/** @brief	Check whether the class contains a valid texture.
	  * @return `true` if not empty.
	  */
	bool has_value() const {
		return (this->_pDevice != nullptr);
	}

	/** @brief	Call the corresponding vkDestroyXXX function to destroy the wrapped instance.
	  */
	void destroy(void) {
		if (this->_pDevice != nullptr) {
			this->_pAllocator->free(this->_imageMemory);
			this->_pAllocator = nullptr;
			for (std::uint32_t i = 0; i < this->_numLayers; ++i)
				vkDestroyImageView(this->_pDevice->get(), this->_imageView[i], nullptr);
			this->_imageView.clear();
			vkDestroyImage(this->_pDevice->get(), this->_image, nullptr);
			this->_image = nullptr;
			this->_extent = { .width = 0, .height = 0 };
			this->_numLayers = 0;
			this->_format = VK_FORMAT_UNDEFINED;
			this->_pDevice = nullptr;
		}
	}

	/** @brief	Get the image.
	  * @return `VkImage` instance.
	  */
	VkImage image(void) const { return this->_image; }

	/** @brief	Get the image view.
	  * @return `VkImageView` instance.
	  */
	VkImageView imageView(std::uint32_t pos) const { return this->_imageView[pos]; }

	/** @brief	Get the image extent.
	  * @return Image extent.
	  */
	VkExtent2D extent(void) const { return this->_extent; }

	/** @brief	Get the number of image layers.
	  * @return Number of image layers.
	  */
	std::uint32_t numLayers(void) const { return this->_numLayers; }

	/** @brief	Get the image format.
	  * @return Image format.
	  */
	VkFormat format(void) const { return this->_format; }

private:

	const jjyou::vk::Device* _pDevice = nullptr;
	VkExtent2D _extent{};
	std::uint32_t _numLayers = 0;
	VkFormat _format = VK_FORMAT_UNDEFINED;
	jjyou::vk::MemoryAllocator* _pAllocator = nullptr;
	VkImage _image = nullptr;
	jjyou::vk::Memory _imageMemory{};
	std::vector<VkImageView> _imageView{};
	static VkCommandBuffer _beginCommandBuffer(VkDevice device, VkCommandPool commandPool) {
		VkCommandBuffer commandBuffer;
		VkCommandBufferAllocateInfo commandBufferAllocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocInfo, &commandBuffer));
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = nullptr
		};
		JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		return commandBuffer;
	}
	static void _endCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore) {
		JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(commandBuffer));
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = (waitSemaphore == nullptr) ? 0U : 1U,
			.pWaitSemaphores = &waitSemaphore,
			.pWaitDstStageMask = 0,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer,
			.signalSemaphoreCount = (signalSemaphore == nullptr) ? 0U : 1U,
			.pSignalSemaphores = &signalSemaphore
		};
		JJYOU_VK_UTILS_CHECK(vkQueueSubmit(queue, 1, &submitInfo, nullptr));
		JJYOU_VK_UTILS_CHECK(vkQueueWaitIdle(queue));
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	};

};

VkShaderModule createShaderModule(const jjyou::vk::Device& device, const std::filesystem::path& path) {
	std::vector<char> shaderCode;
	std::ifstream fin;
	fin.open(path, std::ios::binary | std::ios::in);
	if (!fin.is_open()) {
		throw std::runtime_error("Cannot find shader code from \"" + path.string() + "\"");
	}
	fin.seekg(0, std::ios::end);
	shaderCode.resize(static_cast<std::size_t>(fin.tellg()));
	fin.seekg(0, std::ios::beg);
	fin.read(shaderCode.data(), shaderCode.size());
	fin.close();
	VkShaderModule shaderModule;
	VkShaderModuleCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = shaderCode.size(),
		.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())
	};
	JJYOU_VK_UTILS_CHECK(vkCreateShaderModule(device.get(), &createInfo, nullptr, &shaderModule));
	return shaderModule;
}

std::pair<VkImage, jjyou::vk::Memory> downloadDeviceImage(
	const StorageImage2DArray& deviceImage,
	const jjyou::vk::PhysicalDevice& physicalDevice,
	const jjyou::vk::Device& device,
	jjyou::vk::MemoryAllocator& allocator,
	VkCommandPool computeCommandPool
) {
	VkCommandBuffer computeCommandBuffer;
	VkCommandBufferAllocateInfo commandBufferAllocInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = computeCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1U,
	};
	JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(device.get(), &commandBufferAllocInfo, &computeCommandBuffer));
	VkFenceCreateInfo FenceCreateInfo{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0
	};
	VkFence transferFinishFence;
	vkCreateFence(device.get(), &FenceCreateInfo, nullptr, &transferFinishFence);
	// Transition image from storage image to transfer src
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = 0,
		.pInheritanceInfo = nullptr
	};
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = 0,
		.commandBufferCount = 1,
		.pCommandBuffers = &computeCommandBuffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr
	};
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
	VkImageMemoryBarrier imageMemoryBarrier1{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = deviceImage.image(),
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = deviceImage.numLayers()
		}
	};
	vkCmdPipelineBarrier(
		computeCommandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier1
	);
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, transferFinishFence));
	vkWaitForFences(device.get(), 1, &transferFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	vkResetFences(device.get(), 1, &transferFinishFence);
	vkResetCommandBuffer(computeCommandBuffer, 0);
	// Create host visible and host coherent image
	VkImage hostVisibleImage;
	jjyou::vk::Memory hostVisibleImageMemory;
	std::uint32_t computeQueueFamilyIndex = *physicalDevice.computeQueueFamily();
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = deviceImage.format(),
		.extent{
			.width = deviceImage.extent().width,
			.height = deviceImage.extent().height * deviceImage.numLayers(),
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_LINEAR,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1U,
		.pQueueFamilyIndices = &computeQueueFamilyIndex,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	JJYOU_VK_UTILS_CHECK(vkCreateImage(device.get(), &imageInfo, nullptr, &hostVisibleImage));
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device.get(), hostVisibleImage, &memRequirements);
	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = physicalDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT).value()
	};
	JJYOU_VK_UTILS_CHECK(allocator.allocate(&allocInfo, hostVisibleImageMemory));
	vkBindImageMemory(device.get(), hostVisibleImage, hostVisibleImageMemory.memory(), hostVisibleImageMemory.offset());
	// transition destination image to transfer destination layout
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
	VkImageMemoryBarrier imageMemoryBarrier2{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_NONE,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = hostVisibleImage,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vkCmdPipelineBarrier(
		computeCommandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier2
	);
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, transferFinishFence));
	vkWaitForFences(device.get(), 1, &transferFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	vkResetFences(device.get(), 1, &transferFinishFence);
	vkResetCommandBuffer(computeCommandBuffer, 0);
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
	// copy image
	VkImageCopy copyInfo{
		.srcSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0, // To set
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
			.y = 0, // To set
			.z = 0
		},
		.extent = {
			.width = deviceImage.extent().width,
			.height = deviceImage.extent().height,
			.depth = 1
		}
	};
	for (std::uint32_t layer = 0; layer < deviceImage.numLayers(); ++layer) {
		copyInfo.srcSubresource.baseArrayLayer = layer;
		copyInfo.dstOffset.y = deviceImage.extent().height * layer;
		vkCmdCopyImage(
			computeCommandBuffer,
			deviceImage.image(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			hostVisibleImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyInfo
		);
	}
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, transferFinishFence));
	vkWaitForFences(device.get(), 1, &transferFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	vkResetFences(device.get(), 1, &transferFinishFence);
	vkResetCommandBuffer(computeCommandBuffer, 0);
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
	// transition destination image to general layout, which is the required layout for mapping the image memory later on
	VkImageMemoryBarrier imageMemoryBarrier3{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = hostVisibleImage,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vkCmdPipelineBarrier(
		computeCommandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier3
	);
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, transferFinishFence));
	vkWaitForFences(device.get(), 1, &transferFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	vkResetFences(device.get(), 1, &transferFinishFence);
	vkResetCommandBuffer(computeCommandBuffer, 0);
	JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
	// Transition image from transfer src back to storage image 
	VkImageMemoryBarrier imageMemoryBarrier4{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = deviceImage.image(),
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = deviceImage.numLayers()
		}
	};
	vkCmdPipelineBarrier(
		computeCommandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier4
	);
	// end command buffer
	JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
	JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, transferFinishFence));
	vkWaitForFences(device.get(), 1, &transferFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	vkFreeCommandBuffers(device.get(), computeCommandPool, 1, &computeCommandBuffer);
	vkDestroyFence(device.get(), transferFinishFence, nullptr);
	return std::make_pair(hostVisibleImage, hostVisibleImageMemory);
}

HostImage downloadDeviceImageToHostImage(
	const StorageImage2DArray& deviceImage,
	const jjyou::vk::PhysicalDevice& physicalDevice,
	const jjyou::vk::Device& device,
	jjyou::vk::MemoryAllocator& allocator,
	VkCommandPool computeCommandPool
) {
	if (deviceImage.format() != VK_FORMAT_R8G8B8A8_UNORM) {
		throw std::runtime_error("Only support download R8G8B8A8_UNORM device image to host image.");
	}
	auto [hostVisibleImage, hostVisibleImageMemory] = downloadDeviceImage(
		deviceImage,
		physicalDevice,
		device,
		allocator,
		computeCommandPool
	);
	// copy image to cpu memory
	// Get layout of the image (including row pitch)
	VkImageSubresource subResource{};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout subResourceLayout;
	vkGetImageSubresourceLayout(device.get(), hostVisibleImage, &subResource, &subResourceLayout);
	allocator.map(hostVisibleImageMemory);
	HostImage hostImage(deviceImage.extent().width, deviceImage.extent().height * deviceImage.numLayers());
	for (std::uint32_t r = 0; r < deviceImage.extent().height * deviceImage.numLayers(); ++r) {
		for (std::uint32_t c = 0; c < deviceImage.extent().width; ++c) {
			auto& pixel = hostImage.at(r, c);
			unsigned char* pData = reinterpret_cast<unsigned char*>(hostVisibleImageMemory.mappedAddress()) + subResourceLayout.offset + r * subResourceLayout.rowPitch + c * 4;
			pixel[0] = pData[0];
			pixel[1] = pData[1];
			pixel[2] = pData[2];
			pixel[3] = pData[3];
		}
	}
	allocator.unmap(hostVisibleImageMemory);
	allocator.free(hostVisibleImageMemory);
	vkDestroyImage(device.get(), hostVisibleImage, nullptr);
	return hostImage;
}



struct LambertianSampleRange {
	jjyou::glsl::ivec2 fRange;
	jjyou::glsl::ivec2 xRange;
	jjyou::glsl::ivec2 yRange;
};

struct PrefilteredenvSampleRange {
	jjyou::glsl::ivec2 range;
	int numSamples;
	float roughness;
};

struct EnvBRDFSampleRange {
	jjyou::glsl::ivec2 range;
	int numSamples;
};

TinyArgParser argParser;
jjyou::vk::Instance instance;
jjyou::vk::Loader loader;
jjyou::vk::PhysicalDevice physicalDevice;
jjyou::vk::Device device;
VkCommandPool computeCommandPool;
VkCommandPool transferCommandPool;
VkCommandBuffer computeCommandBuffer;
VkCommandBuffer transferCommandBuffer;
jjyou::vk::MemoryAllocator allocator;
StorageImage2DArray inputImage;
Texture2D inputCubeMap;
StorageImage2DArray sumLight;
StorageImage2DArray sumWeight;
StorageImage2DArray outputImage;
VkDescriptorPool descriptorPool;

VkFence computeFinishFence;

VkDescriptorSetLayout lambertianDescriptorSetLayout;
VkDescriptorSet lambertianDescriptorSet;
VkPipelineLayout lambertianPipelineLayout;
VkPipeline lambertianPipeline;

VkDescriptorSetLayout prefilteredenvDescriptorSetLayout;
VkDescriptorSet prefilteredenvDescriptorSet;
VkPipelineLayout prefilteredenvPipelineLayout;
VkPipeline prefilteredenvPipeline;

VkDescriptorSetLayout envBRDFDescriptorSetLayout;
VkDescriptorSet envBRDFDescriptorSet;
VkPipelineLayout envBRDFPipelineLayout;
VkPipeline envBRDFPipeline;

jjyou::glsl::vec3 unpackRGBE(const jjyou::glsl::vec<unsigned char, 4>& rgbe) {
	if (rgbe == jjyou::glsl::vec<unsigned char, 4>(0)) {
		return jjyou::glsl::vec3(0.0f, 0.0f, 0.0f);
	}
	else {
		return pow(2.0f, static_cast<float>(rgbe.a) - 128.0f) * (jjyou::glsl::vec3(rgbe.cast<float>()) + 0.5f) / 256.0f;
	}
}

jjyou::glsl::vec<unsigned char, 4> packRGBE(jjyou::glsl::vec3 color) {
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

int main(int argc, char** argv) {
	// Parse arguments.
	argParser.parseArgs(argc, argv);
	// Build vulkan instance
	{
		jjyou::vk::InstanceBuilder builder;
		builder
			.enableValidation(argParser.enableValidation)
			.offscreen(true)
			.applicationName("Renderer72.Cube")
			.applicationVersion(0, 1, 0, 0)
			.engineName("Engine72")
			.engineVersion(0, 1, 0, 0)
			.apiVersion(VK_API_VERSION_1_0);
		if (argParser.enableValidation)
			builder.useDefaultDebugUtilsMessenger();
		instance = builder.build();
	}
	// List the physical devices and exit, if "--list-physical-devices" is inputted.
	if (argParser.listPhysicalDevices) {
		jjyou::vk::PhysicalDeviceSelector selector(instance, nullptr);
		std::vector<jjyou::vk::PhysicalDevice> physicalDevices = selector.listAllPhysicalDevices();
		for (const auto& _physicalDevice : physicalDevices) {
			std::cout << "===================================================" << std::endl;
			std::cout << "Device name: " << _physicalDevice.deviceProperties().deviceName << std::endl;
			std::cout << "API version: " << _physicalDevice.deviceProperties().apiVersion << std::endl;
			std::cout << "Driver version: " << _physicalDevice.deviceProperties().driverVersion << std::endl;
			std::cout << "Vendor ID: " << _physicalDevice.deviceProperties().vendorID << std::endl;
			std::cout << "Device ID: " << _physicalDevice.deviceProperties().deviceID << std::endl;
			std::cout << "Device type: " << string_VkPhysicalDeviceType(_physicalDevice.deviceProperties().deviceType) << std::endl;
		}
		std::cout << "===================================================" << std::endl;
		instance.destroy();
		exit(0);
	}
	if (!argParser.lambertian && !argParser.prefilteredenv && !argParser.envbrdf) {
		instance.destroy();
		exit(0);
	}
	// Init vulkan loader
	if (argParser.enableValidation) {
		loader.load(instance.get(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	// Pick physical device
	{
		jjyou::vk::PhysicalDeviceSelector selector(instance, nullptr);
		if (argParser.physicalDevice.has_value()) {
			selector
				.requireDeviceName(*argParser.physicalDevice)
				//.requestDedicated()
				.requireGraphicsQueue(false)
				.requireComputeQueue(true)
				//.requireDistinctTransferQueue(true)
				.enableDeviceFeatures(
					VkPhysicalDeviceFeatures{
						.samplerAnisotropy = true
					}
				)
				;
		}
		else {
			selector
				.requestDedicated()
				.requireGraphicsQueue(false)
				.requireComputeQueue(true)
				.requireDistinctTransferQueue(true)
				.enableDeviceFeatures(
					VkPhysicalDeviceFeatures{
						.samplerAnisotropy = true
					}
				)
				;
		}
		physicalDevice = selector.select();
	}
	// Create logical device
	{
		jjyou::vk::DeviceBuilder builder(instance, physicalDevice);
		device = builder.build();
	}
	// Create sync object
	{
		VkFenceCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};
		vkCreateFence(device.get(), &createInfo, nullptr, &computeFinishFence);
	}
	// Create command pool
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *physicalDevice.computeQueueFamily()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(device.get(), &poolInfo, nullptr, &computeCommandPool));
	}
	{
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = *physicalDevice.transferQueueFamily()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateCommandPool(device.get(), &poolInfo, nullptr, &transferCommandPool));
	}
	// Create command buffers
	{
		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = computeCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1U,
		};
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(device.get(), &allocInfo, &computeCommandBuffer));
	}
	{
		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = transferCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1U,
		};
		JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(device.get(), &allocInfo, &transferCommandBuffer));
	}
	// Init memory allocator
	allocator.init(device);
	// Load input texture
	if (argParser.lambertian || argParser.prefilteredenv){ // env brdf does not need input
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load(argParser.inputImage->string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr) {
			throw std::runtime_error("Cannot open input file \"" + argParser.inputImage->string() + "\".");
		}
		VkExtent2D extent{
			.width = static_cast<std::uint32_t>(texWidth),
			.height = static_cast<std::uint32_t>(texHeight) / 6U
		};
		if (argParser.lambertian) {
			inputImage.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				pixels,
				VK_FORMAT_R8G8B8A8_UNORM,
				extent,
				6
			);
		}
		if (argParser.prefilteredenv) {
			std::vector<jjyou::glsl::vec4>rgb; rgb.reserve(texWidth* texHeight);
			for (int i = 0; i < texWidth * texHeight; ++i) {
				rgb.emplace_back(unpackRGBE(reinterpret_cast<jjyou::glsl::vec<unsigned char, 4>*>(pixels)[i]), 1.0f);
			}
			inputCubeMap.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				rgb.data(),
				VK_FORMAT_R32G32B32A32_SFLOAT,
				extent,
				true
			);
		}
		stbi_image_free(pixels);
	}
	// Create descriptor set layout
	if (argParser.lambertian) {
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			}
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(device.get(), &layoutInfo, nullptr, &lambertianDescriptorSetLayout));
	}
	if (argParser.prefilteredenv) {
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 6,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			}
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(device.get(), &layoutInfo, nullptr, &prefilteredenvDescriptorSetLayout));
	}
	if (argParser.envbrdf) {
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = nullptr
			}
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorSetLayout(device.get(), &layoutInfo, nullptr, &envBRDFDescriptorSetLayout));
	}
	// Create descriptor pool
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 24
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1
			},
		};
		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 1U,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		JJYOU_VK_UTILS_CHECK(vkCreateDescriptorPool(device.get(), &poolInfo, nullptr, &descriptorPool));
	}
	// Create pipeline layout
	{
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.setLayoutCount = 0, // To set
			.pSetLayouts = nullptr, // To set
			.pushConstantRangeCount = 0, // To set
			.pPushConstantRanges = nullptr // To set
		};
		if (argParser.lambertian) {
			VkPushConstantRange pushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0U,
				.size = sizeof(LambertianSampleRange)
			};
			pipelineLayoutInfo.setLayoutCount = 1U;
			pipelineLayoutInfo.pSetLayouts = &lambertianDescriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1U;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
			JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(device.get(), &pipelineLayoutInfo, nullptr, &lambertianPipelineLayout));
		}
		if (argParser.prefilteredenv) {
			VkPushConstantRange pushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0U,
				.size = sizeof(PrefilteredenvSampleRange)
			};
			pipelineLayoutInfo.setLayoutCount = 1U;
			pipelineLayoutInfo.pSetLayouts = &prefilteredenvDescriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1U;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
			JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(device.get(), &pipelineLayoutInfo, nullptr, &prefilteredenvPipelineLayout));
		}
		if (argParser.envbrdf) {
			VkPushConstantRange pushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0U,
				.size = sizeof(EnvBRDFSampleRange)
			};
			pipelineLayoutInfo.setLayoutCount = 1U;
			pipelineLayoutInfo.pSetLayouts = &envBRDFDescriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1U;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
			JJYOU_VK_UTILS_CHECK(vkCreatePipelineLayout(device.get(), &pipelineLayoutInfo, nullptr, &envBRDFPipelineLayout));
		}
	}
	// Create compute pipeline
	{
		VkComputePipelineCreateInfo pipelineInfo{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pNext = nullptr,
				.flags = 0,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = nullptr, // To set
				.pName = "main",
				.pSpecializationInfo = nullptr
			},
			.layout = nullptr, // To set
			.basePipelineHandle = nullptr,
			.basePipelineIndex = 0
		};
		if (argParser.lambertian) {
			VkShaderModule compShaderModule = createShaderModule(device, "../spv/cube/shader/lambertian.comp.spv");
			pipelineInfo.stage.module = compShaderModule;
			pipelineInfo.layout = lambertianPipelineLayout;
			JJYOU_VK_UTILS_CHECK(vkCreateComputePipelines(device.get(), nullptr, 1, &pipelineInfo, nullptr, &lambertianPipeline));
			vkDestroyShaderModule(device.get(), compShaderModule, nullptr);
		}
		if (argParser.prefilteredenv) {
			VkShaderModule compShaderModule = createShaderModule(device, "../spv/cube/shader/prefilteredenv.comp.spv");
			pipelineInfo.stage.module = compShaderModule;
			pipelineInfo.layout = prefilteredenvPipelineLayout;
			JJYOU_VK_UTILS_CHECK(vkCreateComputePipelines(device.get(), nullptr, 1, &pipelineInfo, nullptr, &prefilteredenvPipeline));
			vkDestroyShaderModule(device.get(), compShaderModule, nullptr);
		}
		if (argParser.envbrdf) {
			VkShaderModule compShaderModule = createShaderModule(device, "../spv/cube/shader/envbrdf.comp.spv");
			pipelineInfo.stage.module = compShaderModule;
			pipelineInfo.layout = envBRDFPipelineLayout;
			JJYOU_VK_UTILS_CHECK(vkCreateComputePipelines(device.get(), nullptr, 1, &pipelineInfo, nullptr, &envBRDFPipeline));
			vkDestroyShaderModule(device.get(), compShaderModule, nullptr);
		}
	}

	// Compute!
	if (argParser.lambertian) {
		// Create descriptor sets
		{
			VkDescriptorSetAllocateInfo allocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = descriptorPool,
				.descriptorSetCount = 1U,
				.pSetLayouts = &lambertianDescriptorSetLayout
			};
			JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(device.get(), &allocInfo, &lambertianDescriptorSet));
		}
		// Create temporary and output images
		{
			std::vector<float> dataVec(argParser.lambertianOutputSize.height * argParser.lambertianOutputSize.width * 6 * 4, 0.0f);
			sumLight.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				dataVec.data(),
				VK_FORMAT_R32G32B32A32_SFLOAT,
				argParser.lambertianOutputSize,
				6
			);
			sumWeight.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				dataVec.data(),
				VK_FORMAT_R32_SFLOAT,
				argParser.lambertianOutputSize,
				6
			);
			outputImage.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				nullptr,
				VK_FORMAT_R8G8B8A8_UNORM,
				argParser.lambertianOutputSize,
				6
			);
		}
		// Update descriptor sets
		{
			std::vector<VkDescriptorImageInfo> inputImageInfos;
			std::vector<VkDescriptorImageInfo> sumLightInfos;
			std::vector<VkDescriptorImageInfo> sumAreaInfos;
			std::vector<VkDescriptorImageInfo> outputImageInfos;
			for (int i = 0; i < 6; ++i) {
				inputImageInfos.push_back(VkDescriptorImageInfo{
					.sampler = nullptr,
					.imageView = inputImage.imageView(i),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					});
				sumLightInfos.push_back(VkDescriptorImageInfo{
					.sampler = nullptr,
					.imageView = sumLight.imageView(i),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					});
				sumAreaInfos.push_back(VkDescriptorImageInfo{
					.sampler = nullptr,
					.imageView = sumWeight.imageView(i),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					});
				outputImageInfos.push_back(VkDescriptorImageInfo{
					.sampler = nullptr,
					.imageView = outputImage.imageView(i),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					});
			}
			std::vector<VkWriteDescriptorSet> descriptorWrites = {
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = lambertianDescriptorSet,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 6,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = inputImageInfos.data(),
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = lambertianDescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 6,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = sumLightInfos.data(),
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = lambertianDescriptorSet,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 6,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = sumAreaInfos.data(),
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = lambertianDescriptorSet,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 6,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = outputImageInfos.data(),
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
			};
			vkUpdateDescriptorSets(device.get(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
		// Render
		std::vector<LambertianSampleRange> sampleRanges;
		for (int x = 0; x < (static_cast<int>(inputImage.extent().width) + argParser.lambertianSampleBatch.x - 1) / argParser.lambertianSampleBatch.x; ++x)
			for (int y = 0; y < (static_cast<int>(inputImage.extent().height) + argParser.lambertianSampleBatch.y - 1) / argParser.lambertianSampleBatch.y; ++y) {
				sampleRanges.push_back(
					LambertianSampleRange{
						.fRange = jjyou::glsl::ivec2(0, 6),
						.xRange = jjyou::glsl::ivec2(x * argParser.lambertianSampleBatch.x, std::min<int>((x + 1) * argParser.lambertianSampleBatch.x, inputImage.extent().width)),
						.yRange = jjyou::glsl::ivec2(y * argParser.lambertianSampleBatch.y, std::min<int>((y + 1) * argParser.lambertianSampleBatch.y, inputImage.extent().height))
					}
				);
			}
		sampleRanges.push_back(
			LambertianSampleRange{
				.fRange = jjyou::glsl::ivec2(7),
				.xRange{},
				.yRange{}
			}
		);
		for (const auto& sampleRange : sampleRanges) {
			// Prepare
			std::cout << "Lambertian sample "
				<< "[" << sampleRange.fRange[0] << ", " << sampleRange.fRange[1] << "]"
				<< " x "
				<< "[" << sampleRange.xRange[0] << ", " << sampleRange.xRange[1] << "]"
				<< " x "
				<< "[" << sampleRange.yRange[0] << ", " << sampleRange.yRange[1] << "]"
				<< std::endl;
			vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			vkResetFences(device.get(), 1, &computeFinishFence);
			vkResetCommandBuffer(computeCommandBuffer, 0);
			// Begin command buffer
			VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.pNext = nullptr,
				.flags = 0,
				.pInheritanceInfo = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
			// Record
			vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lambertianPipeline);
			vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lambertianPipelineLayout, 0, 1, &lambertianDescriptorSet, 0, nullptr);
			vkCmdPushConstants(computeCommandBuffer, lambertianPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LambertianSampleRange), &sampleRange);
			vkCmdDispatch(computeCommandBuffer, (outputImage.extent().width + 31) / 32, (outputImage.extent().height * 6 + 31) / 32, 1);
			// End command buffer
			JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
			// Submit
			VkSubmitInfo submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.pNext = nullptr,
				.waitSemaphoreCount = 0U,
				.pWaitSemaphores = nullptr,
				.pWaitDstStageMask = nullptr,
				.commandBufferCount = 1,
				.pCommandBuffers = &computeCommandBuffer,
				.signalSemaphoreCount = 0U,
				.pSignalSemaphores = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, computeFinishFence));
		}
		// Save to file
		{
			vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			HostImage hostImage = downloadDeviceImageToHostImage(outputImage, physicalDevice, device, allocator, computeCommandPool);
			std::filesystem::path imagePath = *argParser.inputImage;
			imagePath.replace_filename(imagePath.stem().string() + ".lambertian" + imagePath.extension().string());
			std::cout << "Writing to " << imagePath << std::endl;
			stbi_write_png(imagePath.string().c_str(), hostImage.width(), hostImage.height(), 4, hostImage.data(), hostImage.width() * 4);
		}
		// Cleanup
		vkFreeDescriptorSets(device.get(), descriptorPool, 1, &lambertianDescriptorSet);
		vkDestroyDescriptorSetLayout(device.get(), lambertianDescriptorSetLayout, nullptr);
		vkDestroyPipelineLayout(device.get(), lambertianPipelineLayout, nullptr);
		vkDestroyPipeline(device.get(), lambertianPipeline, nullptr);
		inputImage.destroy();
		sumLight.destroy();
		sumWeight.destroy();
		outputImage.destroy();
	}
	if (argParser.prefilteredenv) {
		// Create descriptor sets
		{
			VkDescriptorSetAllocateInfo allocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = descriptorPool,
				.descriptorSetCount = 1U,
				.pSetLayouts = &prefilteredenvDescriptorSetLayout
			};
			JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(device.get(), &allocInfo, &prefilteredenvDescriptorSet));
		}
		VkExtent2D outputSize = argParser.prefilteredenvOutputSize;
		for (int i = 1; i <= argParser.prefilteredenvOutputLevel; ++i) {
			float roughness = static_cast<float>(i) / argParser.prefilteredenvOutputLevel;
			outputSize.width = std::max(1U, outputSize.width / 2U);
			outputSize.height = std::max(1U, outputSize.height / 2U);
			std::cout << "Pbr sample "
				<< "roughness " << roughness << " "
				<< "output size " << outputSize.width << "x" << outputSize.height
				<< std::endl;
			std::vector<PrefilteredenvSampleRange> sampleRanges;
			for (int j = 0; j < (argParser.prefilteredenvNumSamples + argParser.prefilteredenvSampleBatch - 1) / argParser.prefilteredenvSampleBatch; ++j)
				sampleRanges.push_back(
					PrefilteredenvSampleRange{
						.range = jjyou::glsl::ivec2(j * argParser.prefilteredenvSampleBatch, std::min((j + 1) * argParser.prefilteredenvSampleBatch, argParser.prefilteredenvNumSamples)),
						.numSamples = argParser.prefilteredenvNumSamples,
						.roughness = roughness
					}
				);
			sampleRanges.push_back(
				PrefilteredenvSampleRange{
					.range = jjyou::glsl::ivec2(argParser.prefilteredenvNumSamples + 1),
					.numSamples = argParser.prefilteredenvNumSamples,
					.roughness = roughness
				}
			);
			// Create temporary and output images
			{
				std::vector<float> dataVec(outputSize.height * outputSize.width * 6 * 4, 0.0f);
				sumLight.create(
					physicalDevice,
					device,
					allocator,
					computeCommandPool,
					transferCommandPool,
					dataVec.data(),
					VK_FORMAT_R32G32B32A32_SFLOAT,
					outputSize,
					6
				);
				sumWeight.create(
					physicalDevice,
					device,
					allocator,
					computeCommandPool,
					transferCommandPool,
					dataVec.data(),
					VK_FORMAT_R32_SFLOAT,
					outputSize,
					6
				);
				outputImage.create(
					physicalDevice,
					device,
					allocator,
					computeCommandPool,
					transferCommandPool,
					nullptr,
					VK_FORMAT_R8G8B8A8_UNORM,
					outputSize,
					6
				);
			}
			// Update descriptor sets
			{
				VkDescriptorImageInfo inputCubeMapInfo{
					.sampler = inputCubeMap.sampler(),
					.imageView = inputCubeMap.imageView(),
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				};
				std::vector<VkDescriptorImageInfo> sumLightInfos;
				std::vector<VkDescriptorImageInfo> sumAreaInfos;
				std::vector<VkDescriptorImageInfo> outputImageInfos;
				for (int j = 0; j < 6; ++j) {
					sumLightInfos.push_back(VkDescriptorImageInfo{
						.sampler = nullptr,
						.imageView = sumLight.imageView(j),
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
						});
					sumAreaInfos.push_back(VkDescriptorImageInfo{
						.sampler = nullptr,
						.imageView = sumWeight.imageView(j),
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
						});
					outputImageInfos.push_back(VkDescriptorImageInfo{
						.sampler = nullptr,
						.imageView = outputImage.imageView(j),
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
						});
				}
				std::vector<VkWriteDescriptorSet> descriptorWrites = {
					VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.pNext = nullptr,
						.dstSet = prefilteredenvDescriptorSet,
						.dstBinding = 0,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						.pImageInfo = &inputCubeMapInfo,
						.pBufferInfo = nullptr,
						.pTexelBufferView = nullptr
					},
					VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.pNext = nullptr,
						.dstSet = prefilteredenvDescriptorSet,
						.dstBinding = 1,
						.dstArrayElement = 0,
						.descriptorCount = 6,
						.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						.pImageInfo = sumLightInfos.data(),
						.pBufferInfo = nullptr,
						.pTexelBufferView = nullptr
					},
					VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.pNext = nullptr,
						.dstSet = prefilteredenvDescriptorSet,
						.dstBinding = 2,
						.dstArrayElement = 0,
						.descriptorCount = 6,
						.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						.pImageInfo = sumAreaInfos.data(),
						.pBufferInfo = nullptr,
						.pTexelBufferView = nullptr
					},
					VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.pNext = nullptr,
						.dstSet = prefilteredenvDescriptorSet,
						.dstBinding = 3,
						.dstArrayElement = 0,
						.descriptorCount = 6,
						.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						.pImageInfo = outputImageInfos.data(),
						.pBufferInfo = nullptr,
						.pTexelBufferView = nullptr
					},
				};
				vkUpdateDescriptorSets(device.get(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
			}
			// Render
			for (const auto& sampleRange : sampleRanges) {
				// Prepare
				std::cout << "Pre filtered environment sample "
					<< "roughness " << roughness << " "
					<< "[" << sampleRange.range[0] << ", " << sampleRange.range[1] << "]"
					<< std::endl;
				vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
				vkResetFences(device.get(), 1, &computeFinishFence);
				vkResetCommandBuffer(computeCommandBuffer, 0);
				// Begin command buffer
				VkCommandBufferBeginInfo beginInfo{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.pNext = nullptr,
					.flags = 0,
					.pInheritanceInfo = nullptr
				};
				JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
				// Record
				vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, prefilteredenvPipeline);
				vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, prefilteredenvPipelineLayout, 0, 1, &prefilteredenvDescriptorSet, 0, nullptr);
				vkCmdPushConstants(computeCommandBuffer, prefilteredenvPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefilteredenvSampleRange), &sampleRange);
				vkCmdDispatch(computeCommandBuffer, (outputImage.extent().width + 31) / 32, (outputImage.extent().height * 6 + 31) / 32, 1);
				// End command buffer
				JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
				// Submit
				VkSubmitInfo submitInfo{
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.pNext = nullptr,
					.waitSemaphoreCount = 0U,
					.pWaitSemaphores = nullptr,
					.pWaitDstStageMask = nullptr,
					.commandBufferCount = 1,
					.pCommandBuffers = &computeCommandBuffer,
					.signalSemaphoreCount = 0U,
					.pSignalSemaphores = nullptr
				};
				JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, computeFinishFence));
			}
			// Save to file
			{
				vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
				HostImage hostImage = downloadDeviceImageToHostImage(outputImage, physicalDevice, device, allocator, computeCommandPool);
				std::filesystem::path imagePath = *argParser.inputImage;
				imagePath.replace_filename(imagePath.stem().string() + ".prefilteredenv." + std::to_string(i) + imagePath.extension().string());
				std::cout << "Writing to " << imagePath << std::endl;
				stbi_write_png(imagePath.string().c_str(), hostImage.width(), hostImage.height(), 4, hostImage.data(), hostImage.width() * 4);
			}
			// Cleanup
			{
				sumLight.destroy();
				sumWeight.destroy();
				outputImage.destroy();
			}
		}
		inputCubeMap.destroy();
		vkFreeDescriptorSets(device.get(), descriptorPool, 1, &prefilteredenvDescriptorSet);
		vkDestroyDescriptorSetLayout(device.get(), prefilteredenvDescriptorSetLayout, nullptr);
		vkDestroyPipelineLayout(device.get(), prefilteredenvPipelineLayout, nullptr);
		vkDestroyPipeline(device.get(), prefilteredenvPipeline, nullptr);
	}
	if (argParser.envbrdf) {
		// Create descriptor sets
		{
			VkDescriptorSetAllocateInfo allocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = descriptorPool,
				.descriptorSetCount = 1U,
				.pSetLayouts = &envBRDFDescriptorSetLayout
			};
			JJYOU_VK_UTILS_CHECK(vkAllocateDescriptorSets(device.get(), &allocInfo, &envBRDFDescriptorSet));
		}
		// Create temporary and output images
		{
			std::vector<float> dataVec(argParser.envbrdfOutputSize.height * argParser.envbrdfOutputSize.width * 2, 0.0f);
			sumLight.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				dataVec.data(),
				VK_FORMAT_R32G32_SFLOAT,
				argParser.envbrdfOutputSize,
				1
			);
			outputImage.create(
				physicalDevice,
				device,
				allocator,
				computeCommandPool,
				transferCommandPool,
				nullptr,
				VK_FORMAT_R32G32_SFLOAT,
				argParser.envbrdfOutputSize,
				1
			);
		}
		// Update descriptor sets
		{
			VkDescriptorImageInfo sumLightInfo{
				.sampler = nullptr,
				.imageView = sumLight.imageView(0),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			};
			VkDescriptorImageInfo outputImageInfo{
				.sampler = nullptr,
				.imageView = outputImage.imageView(0),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			};
			std::vector<VkWriteDescriptorSet> descriptorWrites = {
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = envBRDFDescriptorSet,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &sumLightInfo,
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = nullptr,
					.dstSet = envBRDFDescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &outputImageInfo,
					.pBufferInfo = nullptr,
					.pTexelBufferView = nullptr
				},
			};
			vkUpdateDescriptorSets(device.get(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
		// Render
		std::vector<EnvBRDFSampleRange> sampleRanges;
		for (int j = 0; j < (argParser.envbrdfNumSamples + argParser.envbrdfSampleBatch - 1) / argParser.envbrdfSampleBatch; ++j)
			sampleRanges.push_back(
				EnvBRDFSampleRange{
					.range = jjyou::glsl::ivec2(j * argParser.envbrdfSampleBatch, std::min((j + 1) * argParser.envbrdfSampleBatch, argParser.envbrdfNumSamples)),
					.numSamples = argParser.envbrdfNumSamples
				}
		);
		sampleRanges.push_back(
			EnvBRDFSampleRange{
				.range = jjyou::glsl::ivec2(argParser.envbrdfNumSamples + 1),
				.numSamples = argParser.envbrdfNumSamples
			}
		);
		for (const auto& sampleRange : sampleRanges) {
			// Prepare
			std::cout << "Environment BRDF sample "
				<< "[" << sampleRange.range[0] << ", " << sampleRange.range[1] << "]"
				<< std::endl;
			vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			vkResetFences(device.get(), 1, &computeFinishFence);
			vkResetCommandBuffer(computeCommandBuffer, 0);
			// Begin command buffer
			VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.pNext = nullptr,
				.flags = 0,
				.pInheritanceInfo = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkBeginCommandBuffer(computeCommandBuffer, &beginInfo));
			// Record
			vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envBRDFPipeline);
			vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envBRDFPipelineLayout, 0, 1, &envBRDFDescriptorSet, 0, nullptr);
			vkCmdPushConstants(computeCommandBuffer, envBRDFPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EnvBRDFSampleRange), &sampleRange);
			vkCmdDispatch(computeCommandBuffer, (outputImage.extent().width + 31) / 32, (outputImage.extent().height + 31) / 32, 1);
			// End command buffer
			JJYOU_VK_UTILS_CHECK(vkEndCommandBuffer(computeCommandBuffer));
			// Submit
			VkSubmitInfo submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.pNext = nullptr,
				.waitSemaphoreCount = 0U,
				.pWaitSemaphores = nullptr,
				.pWaitDstStageMask = nullptr,
				.commandBufferCount = 1,
				.pCommandBuffers = &computeCommandBuffer,
				.signalSemaphoreCount = 0U,
				.pSignalSemaphores = nullptr
			};
			JJYOU_VK_UTILS_CHECK(vkQueueSubmit(*device.computeQueues(), 1, &submitInfo, computeFinishFence));
		}
		// Save to file
		{
			vkWaitForFences(device.get(), 1, &computeFinishFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			auto [hostVisibleImage, hostVisibleImageMemory] = downloadDeviceImage(outputImage, physicalDevice, device, allocator, computeCommandPool);
			// copy image to cpu memory
			// Get layout of the image (including row pitch)
			VkImageSubresource subResource{};
			subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			VkSubresourceLayout subResourceLayout;
			vkGetImageSubresourceLayout(device.get(), hostVisibleImage, &subResource, &subResourceLayout);
			allocator.map(hostVisibleImageMemory);
			std::filesystem::path imagePath = "envbrdf.bin";
			std::cout << "Writing to " << imagePath << std::endl;
			std::ofstream fout(imagePath, std::ios::out | std::ios::binary);
			std::uint32_t height = outputImage.extent().height;
			fout.write(reinterpret_cast<const char*>(&height), sizeof(height));
			for (std::uint32_t r = 0; r < outputImage.extent().height; ++r) {
				const char* pData = reinterpret_cast<const char*>(hostVisibleImageMemory.mappedAddress()) + subResourceLayout.offset + r * subResourceLayout.rowPitch;
				fout.write(pData, sizeof(float) * 2 * outputImage.extent().width);
			}
			fout.close();
			allocator.unmap(hostVisibleImageMemory);
			allocator.free(hostVisibleImageMemory);
			vkDestroyImage(device.get(), hostVisibleImage, nullptr);
		}
		// Cleanup
		{
			vkFreeDescriptorSets(device.get(), descriptorPool, 1, &envBRDFDescriptorSet);
			sumLight.destroy();
			outputImage.destroy();
			vkDestroyDescriptorSetLayout(device.get(), envBRDFDescriptorSetLayout, nullptr);
			vkDestroyPipelineLayout(device.get(), envBRDFPipelineLayout, nullptr);
			vkDestroyPipeline(device.get(), envBRDFPipeline, nullptr);
		}
	}

	// Clean up
	vkDeviceWaitIdle(device.get());
	vkDestroyDescriptorPool(device.get(), descriptorPool, nullptr);
	allocator.destory();
	vkDestroyCommandPool(device.get(), transferCommandPool, nullptr);
	vkDestroyCommandPool(device.get(), computeCommandPool, nullptr);
	vkDestroyFence(device.get(), computeFinishFence, nullptr);
	device.destroy();
	instance.destroy();
	exit(0);
}