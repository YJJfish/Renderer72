#include "GBuffer.hpp"

GBuffer::GBuffer(
	const jjyou::vk::Context& context_,
	jjyou::vk::MemoryAllocator& allocator_
) : _pContext(&context_), _pAllocator(&allocator_) {}

GBuffer& GBuffer::createTextures(
	vk::Extent2D extent_,
	const vk::raii::RenderPass& renderPass_
) {
	this->clear();
	this->_extent = extent_;
	vk::ImageCreateInfo imageCreateInfo = vk::ImageCreateInfo()
		.setFlags(vk::ImageCreateFlags(0))
		.setImageType(vk::ImageType::e2D)
		//.setFormat()
		.setExtent(vk::Extent3D(this->_extent, 1))
		.setMipLevels(1)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setQueueFamilyIndices(nullptr)
		.setInitialLayout(vk::ImageLayout::eUndefined);
	vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
		.setFlags(vk::ImageViewCreateFlags(0))
		//.setImage()
		.setViewType(vk::ImageViewType::e2D)
		//.setFormat()
		.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity))
		.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
	for (std::uint32_t i = 0; i < 4; ++i) {
		imageCreateInfo.setFormat(GBuffer::_formats[i]);
		this->_images[i] = vk::raii::Image(this->_pContext->device(), imageCreateInfo);
		vk::MemoryRequirements imageMemoryRequirements = this->_images[i].getMemoryRequirements();
		vk::MemoryAllocateInfo imageMemoryAllocInfo(
			imageMemoryRequirements.size,
			this->_pContext->findMemoryType(imageMemoryRequirements.memoryTypeBits, ::vk::MemoryPropertyFlagBits::eDeviceLocal).value()
		);
		JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(reinterpret_cast<VkMemoryAllocateInfo*>(&imageMemoryAllocInfo), this->_imageMemories[i]));
		this->_images[i].bindMemory(this->_imageMemories[i].memory(), this->_imageMemories[i].offset());
		imageViewCreateInfo.setImage(*this->_images[i]).setFormat(GBuffer::_formats[i]);
		this->_imageViews[i] = vk::raii::ImageView(
			this->_pContext->device(),
			imageViewCreateInfo
		);
	}
	{
		imageCreateInfo.setFormat(GBuffer::_depthFormat).setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
		this->_depthImage = vk::raii::Image(this->_pContext->device(), imageCreateInfo);
		vk::MemoryRequirements imageMemoryRequirements = this->_depthImage.getMemoryRequirements();
		vk::MemoryAllocateInfo imageMemoryAllocInfo(
			imageMemoryRequirements.size,
			this->_pContext->findMemoryType(imageMemoryRequirements.memoryTypeBits, ::vk::MemoryPropertyFlagBits::eDeviceLocal).value()
		);
		JJYOU_VK_UTILS_CHECK(this->_pAllocator->allocate(reinterpret_cast<VkMemoryAllocateInfo*>(&imageMemoryAllocInfo), this->_depthImageMemory));
		this->_depthImage.bindMemory(this->_depthImageMemory.memory(), this->_depthImageMemory.offset());
		imageViewCreateInfo
			.setImage(*this->_depthImage)
			.setFormat(GBuffer::_depthFormat)
			.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1));
		this->_depthImageView = vk::raii::ImageView(
			this->_pContext->device(),
			imageViewCreateInfo
		);
	}
	std::vector<vk::ImageView> attachments{ *this->_imageViews[0],*this->_imageViews[1],*this->_imageViews[2],*this->_imageViews[3],*this->_depthImageView };
	vk::FramebufferCreateInfo framebufferCreateInfo = vk::FramebufferCreateInfo()
		.setFlags(vk::FramebufferCreateFlags(0U))
		.setRenderPass(*renderPass_)
		.setAttachments(attachments)
		.setWidth(this->_extent.width)
		.setHeight(this->_extent.height)
		.setLayers(1U);
	this->_framebuffer = vk::raii::Framebuffer(
		this->_pContext->device(),
		framebufferCreateInfo
	);
	vk::SamplerCreateInfo samplerCreateInfo = vk::SamplerCreateInfo()
		.setFlags(vk::SamplerCreateFlags(0))
		//.setMagFilter()
		//.setMinFilter()
		.setMipmapMode(vk::SamplerMipmapMode::eNearest)
		.setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
		.setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
		.setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
		.setMipLodBias(0.0f)
		.setAnisotropyEnable(VK_FALSE) // G-buffer textures won't be viewed at oblique angles
		.setMaxAnisotropy(0.0f)
		.setCompareEnable(VK_FALSE)
		.setCompareOp(vk::CompareOp::eNever)
		.setMinLod(0.0f)
		.setMaxLod(0.0f)
		.setBorderColor(vk::BorderColor::eFloatOpaqueBlack)
		.setUnnormalizedCoordinates(VK_FALSE);
	this->_nearestSampler = vk::raii::Sampler(this->_pContext->device(), samplerCreateInfo.setMagFilter(vk::Filter::eNearest).setMinFilter(vk::Filter::eNearest));
	this->_linearSampler = vk::raii::Sampler(this->_pContext->device(), samplerCreateInfo.setMagFilter(vk::Filter::eLinear).setMinFilter(vk::Filter::eLinear));
	return *this;
}