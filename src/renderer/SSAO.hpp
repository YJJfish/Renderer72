#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/vk/Legacy/Memory.hpp>
#include <jjyou/vk/Legacy/utils.hpp>

class SSAO {

public:

	/** @brief	Construct an empty G-buffer in invalid state.
	  */
	SSAO(std::nullptr_t) {}

	/** @brief	Copy constructor is disabled.
	  */
	SSAO(const SSAO&) = delete;

	/** @brief	Move constructor.
	  */
	SSAO(SSAO&& other_) = default;

	/** @brief	Explicitly clear the G-Buffer.
	  */
	void clear(void) {
		this->~SSAO();
	}

	/** @brief	Destructor.
	  */
	~SSAO(void) = default;

	/** @brief	Copy assignment is disabled.
	  */
	SSAO& operator=(const SSAO&) = delete;

	/** @brief	Move assignment.
	  */
	SSAO& operator=(SSAO&& other_) noexcept {
		if (this != &other_) {
			this->clear();
			this->_pContext = other_._pContext;
			this->_pAllocator = other_._pAllocator;
			this->_images = std::move(other_._images);
			this->_imageMemories = std::move(other_._imageMemories);
			this->_imageViews = std::move(other_._imageViews);
			this->_framebuffers = std::move(other_._framebuffers);
			this->_extent = other_._extent;
			this->_sampler = std::move(other_._sampler);
		}
		return *this;
	}

	/** @brief	Construct an empty SSAO.
	  */
	SSAO(
		const jjyou::vk::Context& context_,
		jjyou::vk::MemoryAllocator& allocator_
	);

	/** @brief	Create textures.
	  *
	  *			There are 2 textures in a SSAO.
	  *			1. R32SFloat ssao.
	  *			2. R32SFloat ssao blur.
	  */
	SSAO& createTextures(
		vk::Extent2D extent_,
		const vk::raii::RenderPass& ssaoRenderPass_,
		const vk::raii::RenderPass& ssaoBlurRenderPass_
	);

	/** @brief	Get the image for this texture.
	  */
	const vk::raii::Image& image(std::uint32_t index_) const { return this->_images[index_]; }

	/** @brief	Get the image view for this texture.
	  */
	const vk::raii::ImageView& imageView(std::uint32_t index_) const { return this->_imageViews[index_]; }

	/** @brief	Get the frame buffer.
	  */
	const vk::raii::Framebuffer& framebuffer(std::uint32_t index_) const { return this->_framebuffers[index_]; }
	
	/** @brief	Get the texture format.
	  */
	static constexpr vk::Format format(std::uint32_t index_) { return SSAO::_formats[index_]; }

	/** @brief	Get the texture image extent.
	  */
	constexpr vk::Extent2D extent(void) const { return this->_extent; }

	/** @brief	Get the sampler.
	  */
	const vk::raii::Sampler& sampler(void) const { return this->_sampler; }

private:

	const jjyou::vk::Context* _pContext = nullptr;
	jjyou::vk::MemoryAllocator* _pAllocator = nullptr;
	std::array<vk::raii::Image, 2> _images{ {vk::raii::Image(nullptr), vk::raii::Image(nullptr)} };
	std::array<jjyou::vk::Memory, 2> _imageMemories{};
	std::array<vk::raii::ImageView, 2> _imageViews{ {vk::raii::ImageView(nullptr), vk::raii::ImageView(nullptr)} };
	std::array<vk::raii::Framebuffer, 2> _framebuffers{ { vk::raii::Framebuffer(nullptr),vk::raii::Framebuffer(nullptr) }};
	static inline constexpr std::array<vk::Format, 2> _formats = { {
		vk::Format::eR32Sfloat, vk::Format::eR32Sfloat
	} };
	vk::Extent2D _extent{};
	vk::raii::Sampler _sampler{ nullptr }; // Nearest-interpolation clip-to-border sampler.
};