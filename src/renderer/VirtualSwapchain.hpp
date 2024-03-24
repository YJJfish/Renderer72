#pragma once
#include "fwd.hpp"

#include <vector>

#include <jjyou/vk/Vulkan.hpp>

class VirtualSwapchain {

public:

	/** @brief	Construct an empty swapchain.
	  */
	VirtualSwapchain(std::nullptr_t) {}

	/** @brief	Copy constructor is disabled.
	  */
	VirtualSwapchain(const VirtualSwapchain&) = delete;

	/** @brief	Move constructor.
	  */
	VirtualSwapchain(VirtualSwapchain&& other) = default;

	/** @brief	Destructor.
	  */
	~VirtualSwapchain(void) {
		if (this->_pContext != nullptr) {
			for (std::uint32_t i = 0; i < this->swapchainImages.size(); ++i) {
				vkDestroyImageView(*this->_pContext->device(), this->swapchainImageViews[i], nullptr);
				this->allocator->free(this->swapchainImageMemories[i]);
				vkDestroyImage(*this->_pContext->device(), this->swapchainImages[i], nullptr);
			}
			this->swapchainImages.clear();
			this->swapchainImageMemories.clear();
			this->swapchainImageViews.clear();
			this->allocator = nullptr;
			this->_format = VK_FORMAT_UNDEFINED;
			this->_extent = { .width = 0, .height = 0 };
		}
	}

	/** @brief	Copy assignment is disabled.
	  */
	VirtualSwapchain& operator=(const VirtualSwapchain&) = delete;

	/** @brief	Move assignment.
	  */
	VirtualSwapchain& operator=(VirtualSwapchain&& other) noexcept {
		if (this != &other) {
			this->_pContext = other._pContext;
			other._pContext = nullptr;
			this->allocator = allocator;
			this->swapchainImages = std::move(other.swapchainImages);
			this->swapchainImageMemories = std::move(other.swapchainImageMemories);
			this->swapchainImageViews = std::move(other.swapchainImageViews);
			this->_format = other._format;
			this->_extent = other._extent;
			this->currentImageIdx = other.currentImageIdx;
		}
		return *this;
	}

	/**	@brief	Create images and image views for the virtual swapchain
	  */
	VirtualSwapchain(
		const jjyou::vk::Context& context,
		jjyou::vk::MemoryAllocator& allocator,
		std::uint32_t imageCount,
		VkFormat format,
		VkExtent2D imageExtent
	);

	/** @brief	Similar to `vkAcquireNextImageKHR`.
	  */
	VkResult acquireNextImage(
		std::uint32_t* pImageIndex
	);

	/** @brief	Aquire last image. This is useful when you want the last rendered image.
	  */
	VkResult acquireLastImage(
		std::uint32_t* pImageIndex
	) const;

	/** @brief	Get the number of images in the swapchain.
	  * @return The number of images in the swapchain.
	  */
	std::uint32_t imageCount(void) const { return static_cast<std::uint32_t>(this->swapchainImages.size()); }

	/** @brief	Get the swapchain images.
	  * @return Vector of VkImage.
	  */
	const std::vector<VkImage>& images(void) const { return this->swapchainImages; }

	/** @brief	Get the swapchain image views.
	  * @return Vector of VkImageView.
	  */
	const std::vector<VkImageView>& imageViews(void) const { return this->swapchainImageViews; }

	/** @brief	Get the swapchain format.
	  * @return Swapchain format.
	  */
	VkFormat format(void) const { return this->_format; }

	/** @brief	Get the swapchain extent.
	  * @return Swapchain extent.
	  */
	VkExtent2D extent(void) const { return this->_extent; }

private:

	const jjyou::vk::Context* _pContext = nullptr;
	jjyou::vk::MemoryAllocator* allocator = nullptr;
	std::vector<VkImage> swapchainImages = {};
	std::vector<jjyou::vk::Memory> swapchainImageMemories = {};
	std::vector<VkImageView> swapchainImageViews = {};
	VkFormat _format = VK_FORMAT_UNDEFINED;
	VkExtent2D _extent = {};
	std::uint32_t currentImageIdx = 0;

};