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
	JJYOU_VK_UTILS_CHECK(vkCreateImageView(*this->context.device(), &viewInfo, nullptr, &imageView));
	return imageView;
}

std::pair<VkImage, jjyou::vk::Memory> Engine::createImage(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	const std::set<std::uint32_t>& queueFamilyIndices,
	VkMemoryPropertyFlags properties
) {
	std::vector<std::uint32_t> _queueFamilyIndices(queueFamilyIndices.begin(), queueFamilyIndices.end());
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
		.sharingMode = (_queueFamilyIndices.size() >= 2) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = static_cast<std::uint32_t>(_queueFamilyIndices.size()),
		.pQueueFamilyIndices = _queueFamilyIndices.data(),
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VkImage image;
	JJYOU_VK_UTILS_CHECK(vkCreateImage(*this->context.device(), &imageInfo, nullptr, &image));
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(*this->context.device(), image, &memRequirements);
	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = this->context.findMemoryType(memRequirements.memoryTypeBits, static_cast<vk::MemoryPropertyFlags>(properties)).value()
	};
	jjyou::vk::Memory imageMemory;
	JJYOU_VK_UTILS_CHECK(this->allocator.allocate(&allocInfo, imageMemory));
	vkBindImageMemory(*this->context.device(), image, imageMemory.memory(), imageMemory.offset());
	return std::make_pair(image, std::move(imageMemory));
}

std::pair<VkBuffer, jjyou::vk::Memory> Engine::createBuffer(
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	const std::set<std::uint32_t>& queueFamilyIndices,
	VkMemoryPropertyFlags properties
) {
	std::vector<std::uint32_t> _queueFamilyIndices(queueFamilyIndices.begin(), queueFamilyIndices.end());
	VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = size,
		.usage = usage,
		.sharingMode = (_queueFamilyIndices.size() >= 2) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = static_cast<std::uint32_t>(_queueFamilyIndices.size()),
		.pQueueFamilyIndices = _queueFamilyIndices.data()
	};
	VkBuffer buffer;
	JJYOU_VK_UTILS_CHECK(vkCreateBuffer(*this->context.device(), &bufferInfo, nullptr, &buffer));
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(*this->context.device(), buffer, &memRequirements);
	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = this->context.findMemoryType(memRequirements.memoryTypeBits, static_cast<vk::MemoryPropertyFlags>(properties)).value()
	};
	jjyou::vk::Memory bufferMemory;
	JJYOU_VK_UTILS_CHECK(this->allocator.allocate(&allocInfo, bufferMemory));
	vkBindBufferMemory(*this->context.device(), buffer, bufferMemory.memory(), bufferMemory.offset());
	return std::make_pair(buffer, std::move(bufferMemory));
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
	JJYOU_VK_UTILS_CHECK(vkAllocateCommandBuffers(*this->context.device(), &allocInfo, &transferCommandBuffer));


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

	vkQueueSubmit(**this->context.queue(jjyou::vk::Context::QueueType::Transfer), 1, &submitInfo, nullptr);
	vkQueueWaitIdle(**this->context.queue(jjyou::vk::Context::QueueType::Transfer));

	vkFreeCommandBuffers(*this->context.device(), this->transferCommandPool, 1, &transferCommandBuffer);
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

vk::raii::ShaderModule Engine::createShaderModule(const std::filesystem::path& path) const {
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
	JJYOU_VK_UTILS_CHECK(vkCreateShaderModule(*this->context.device(), &createInfo, nullptr, &shaderModule));
	return vk::raii::ShaderModule(this->context.device(), shaderModule);
}