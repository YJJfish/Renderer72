#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/vk/Legacy/Memory.hpp>
#include <jjyou/vk/Legacy/utils.hpp>

class GBuffer {

public:

	/** @brief	Construct an empty G-buffer in invalid state.
	  */
	GBuffer(std::nullptr_t) {}

	/** @brief	Copy constructor is disabled.
	  */
	GBuffer(const GBuffer&) = delete;

	/** @brief	Move constructor.
	  */
	GBuffer(GBuffer&& other_) = default;

	/** @brief	Explicitly clear the G-Buffer.
	  */
	void clear(void) {
		this->~GBuffer();
	}

	/** @brief	Destructor.
	  */
	~GBuffer(void) = default;

	/** @brief	Copy assignment is disabled.
	  */
	GBuffer& operator=(const GBuffer&) = delete;

	/** @brief	Move assignment.
	  */
	GBuffer& operator=(GBuffer&& other_) noexcept {
		if (this != &other_) {
			this->clear();
			this->_pContext = other_._pContext;
			this->_pAllocator = other_._pAllocator;
			this->_images = std::move(other_._images);
			this->_depthImage = std::move(other_._depthImage);
			this->_imageMemories = std::move(other_._imageMemories);
			this->_depthImageMemory = std::move(other_._depthImageMemory);
			this->_imageViews = std::move(other_._imageViews);
			this->_depthImageView = std::move(other_._depthImageView);
			this->_framebuffer = std::move(other_._framebuffer);
			this->_extent = other_._extent;
			this->_nearestSampler = std::move(other_._nearestSampler);
			this->_linearSampler = std::move(other_._linearSampler);
		}
		return *this;
	}

	/** @brief	Construct an empty G-buffer.
	  */
	GBuffer(
		const jjyou::vk::Context& context_,
		jjyou::vk::MemoryAllocator& allocator_
	);

	/** @brief	Create textures.
	  *
	  *			There are 4 textures in a G-buffer.
	  *			1. R32G32B32A32Sfloat
	  *			2. R8G8B8A8Unorm.
	  *			3. R8G8B8A8Unorm.
	  *			4. R8G8B8A8Unorm.
	  */
	GBuffer& createTextures(
		vk::Extent2D extent_,
		const vk::raii::RenderPass& renderPass_
	);

	/** @brief	Get the image for this texture.
	  */
	const vk::raii::Image& image(std::uint32_t index_) const { return this->_images[index_]; }

	/** @brief	Get the depth image for this texture.
	  */
	const vk::raii::Image& depthImage(void) const { return this->_depthImage; }

	/** @brief	Get the image view for this texture.
	  */
	const vk::raii::ImageView& imageView(std::uint32_t index_) const { return this->_imageViews[index_]; }

	/** @brief	Get the depth image view for this texture.
	  */
	const vk::raii::ImageView& depthImageView(void) const { return this->_depthImageView; }
	
	/** @brief	Get the framebuffer for G-buffer
	  */
	const vk::raii::Framebuffer& framebuffer(void) const { return this->_framebuffer; }

	/** @brief	Get the texture format.
	  */
	static constexpr vk::Format format(std::uint32_t index_) { return GBuffer::_formats[index_]; }

	/** @brief	Get the depth texture format.
	  */
	static constexpr vk::Format depthFormat(void) { return GBuffer::_depthFormat; }

	/** @brief	Get the texture image extent.
	  */
	constexpr vk::Extent2D extent(void) const { return this->_extent; }

	/** @brief	Get the sampler.
	  */
	const vk::raii::Sampler& nearestSampler(void) const { return this->_nearestSampler; }

	/** @brief	Get the sampler.
	  */
	const vk::raii::Sampler& linearSampler(void) const { return this->_linearSampler; }

private:

	const jjyou::vk::Context* _pContext = nullptr;
	jjyou::vk::MemoryAllocator* _pAllocator = nullptr;
	std::array<vk::raii::Image, 4> _images{ {vk::raii::Image(nullptr), vk::raii::Image(nullptr), vk::raii::Image(nullptr), vk::raii::Image(nullptr)} };
	vk::raii::Image _depthImage{ nullptr };
	std::array<jjyou::vk::Memory, 4> _imageMemories{};
	jjyou::vk::Memory _depthImageMemory{};
	std::array<vk::raii::ImageView, 4> _imageViews{ {vk::raii::ImageView(nullptr), vk::raii::ImageView(nullptr), vk::raii::ImageView(nullptr), vk::raii::ImageView(nullptr)} };
	vk::raii::ImageView _depthImageView{ nullptr };
	vk::raii::Framebuffer _framebuffer{ nullptr };
	static inline constexpr std::array<vk::Format, 4> _formats = { {
		vk::Format::eR32G32B32A32Sfloat, vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Unorm
	} };
	static inline constexpr vk::Format _depthFormat = vk::Format::eD32Sfloat;
	vk::Extent2D _extent{};
	vk::raii::Sampler _nearestSampler{ nullptr }; // Nearest-interpolation clip-to-border sampler. Ued for composition.
	vk::raii::Sampler _linearSampler{ nullptr }; // Linear-interpolation clip-to-border sampler. Used for SSAO.
};