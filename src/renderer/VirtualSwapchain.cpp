#include "VirtualSwapchain.hpp"
#include "utils.hpp"

VirtualSwapchain::VirtualSwapchain(
	const jjyou::vk::Context& context,
	jjyou::vk::MemoryAllocator& allocator,
	std::uint32_t imageCount,
	VkFormat format,
	VkExtent2D imageExtent
) : _pContext(&context), allocator(&allocator), _format(format), _extent(imageExtent)
{
	this->swapchainImages.resize(imageCount);
	this->swapchainImageMemories.resize(imageCount);
	this->swapchainImageViews.resize(imageCount);
	this->currentImageIdx = 0;
	std::vector<std::uint32_t> queueFamilyIndices = { *context.queueFamilyIndex(jjyou::vk::Context::QueueType::Main), *context.queueFamilyIndex(jjyou::vk::Context::QueueType::Transfer) };
	if (queueFamilyIndices[0] == queueFamilyIndices[1])
		queueFamilyIndices.pop_back();
	for (std::uint32_t i = 0; i < imageCount; ++i) {
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
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			.sharingMode = (queueFamilyIndices.size() >= 2) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size()),
			.pQueueFamilyIndices = queueFamilyIndices.data(),
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};
		JJYOU_VK_UTILS_CHECK(vkCreateImage(*this->_pContext->device(), &imageInfo, nullptr, &this->swapchainImages[i]));
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(*this->_pContext->device(), this->swapchainImages[i], &memRequirements);
		VkMemoryAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = nullptr,
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = this->_pContext->findMemoryType(memRequirements.memoryTypeBits, ::vk::MemoryPropertyFlagBits::eDeviceLocal).value()
		};
		JJYOU_VK_UTILS_CHECK(this->allocator->allocate(&allocInfo, this->swapchainImageMemories[i]));
		vkBindImageMemory(*this->_pContext->device(), this->swapchainImages[i], this->swapchainImageMemories[i].memory(), this->swapchainImageMemories[i].offset());
		VkImageViewCreateInfo viewInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.image = this->swapchainImages[i],
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
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};
		JJYOU_VK_UTILS_CHECK(vkCreateImageView(*this->_pContext->device(), &viewInfo, nullptr, &this->swapchainImageViews[i]));
	}
}

VkResult VirtualSwapchain::acquireNextImage(
	std::uint32_t* pImageIndex
) {
	if (pImageIndex != nullptr) {
		*pImageIndex = this->currentImageIdx;
	}
	this->currentImageIdx = (this->currentImageIdx + 1) % this->imageCount();
	return VK_SUCCESS;
}

VkResult VirtualSwapchain::acquireLastImage(
	std::uint32_t* pImageIndex
) const {
	if (pImageIndex != nullptr) {
		*pImageIndex = (this->currentImageIdx + this->imageCount() - 1) % this->imageCount();
	}
	return VK_SUCCESS;
}