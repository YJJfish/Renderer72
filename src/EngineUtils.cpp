#include "Engine.hpp"

VkImageView Engine::createImageView(
	VkImage image,
	VkFormat format,
	VkImageAspectFlags aspectFlags
) const {
	VkImageViewCreateInfo viewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY
		},
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	VkImageView imageView;
	JJYOU_VK_UTILS_CHECK(vkCreateImageView(this->device.get(), &viewInfo, nullptr, &imageView));
	return imageView;
}

std::pair<VkImage, jjyou::vk::Memory> Engine::createImage(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	const std::vector<std::uint32_t>& queueFamilyIndices,
	VkMemoryPropertyFlags properties
) {
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent{
			.width = width,
			.height = height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = (queueFamilyIndices.size() >= 2) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size()),
		.pQueueFamilyIndices = queueFamilyIndices.data(),
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VkImage image;
	JJYOU_VK_UTILS_CHECK(vkCreateImage(this->device.get(), &imageInfo, nullptr, &image));
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(this->device.get(), image, &memRequirements);
	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = this->physicalDevice.findMemoryType(memRequirements.memoryTypeBits, properties).value()
	};
	jjyou::vk::Memory imageMemory;
	JJYOU_VK_UTILS_CHECK(this->allocator.allocate(&allocInfo, imageMemory));
	vkBindImageMemory(this->device.get(), image, imageMemory.memory(), imageMemory.offset());
	return std::make_pair(image, imageMemory);
}

std::pair<VkBuffer, jjyou::vk::Memory> Engine::createBuffer(
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	const std::vector<std::uint32_t>& queueFamilyIndices,
	VkMemoryPropertyFlags properties
) {
	VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = size,
		.usage = usage,
		.sharingMode = (queueFamilyIndices.size() >= 2) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size()),
		.pQueueFamilyIndices = queueFamilyIndices.data()
	};
	VkBuffer buffer;
	JJYOU_VK_UTILS_CHECK(vkCreateBuffer(this->device.get(), &bufferInfo, nullptr, &buffer));
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(this->device.get(), buffer, &memRequirements);
	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = this->physicalDevice.findMemoryType(memRequirements.memoryTypeBits, properties).value()
	};
	jjyou::vk::Memory bufferMemory;
	JJYOU_VK_UTILS_CHECK(this->allocator.allocate(&allocInfo, bufferMemory));
	vkBindBufferMemory(this->device.get(), buffer, bufferMemory.memory(), bufferMemory.offset());
	return std::make_pair(buffer, bufferMemory);
}

void Engine::copyBuffer(
	VkBuffer srcBuffer,
	VkBuffer dstBuffer,
	VkDeviceSize size
) const {

	VkCommandBuffer transferCommandBuffer;
	VkCommandBufferAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = this->transferCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(this->device.get(), &allocInfo, &transferCommandBuffer));


	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr
	};
	vkBeginCommandBuffer(transferCommandBuffer, &beginInfo);

	VkBufferCopy copyRegion{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = size
	};
	vkCmdCopyBuffer(transferCommandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	vkEndCommandBuffer(transferCommandBuffer);

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

	vkQueueSubmit(*this->device.transferQueues(), 1, &submitInfo, nullptr);
	vkQueueWaitIdle(*this->device.transferQueues());

	vkFreeCommandBuffers(this->device.get(), this->transferCommandPool, 1, &transferCommandBuffer);
}

void Engine::insertImageMemoryBarrier(
	VkCommandBuffer cmdbuffer,
	VkImage image,
	VkAccessFlags srcAccessMask,
	VkAccessFlags dstAccessMask,
	VkImageLayout oldImageLayout,
	VkImageLayout newImageLayout,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	VkImageSubresourceRange subresourceRange
) {
	VkImageMemoryBarrier imageMemoryBarrier{};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	imageMemoryBarrier.srcAccessMask = srcAccessMask;
	imageMemoryBarrier.dstAccessMask = dstAccessMask;
	imageMemoryBarrier.oldLayout = oldImageLayout;
	imageMemoryBarrier.newLayout = newImageLayout;
	imageMemoryBarrier.image = image;
	imageMemoryBarrier.subresourceRange = subresourceRange;

	vkCmdPipelineBarrier(
		cmdbuffer,
		srcStageMask,
		dstStageMask,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier);
}