#pragma once
#include "fwd.hpp"

#include <vulkan/vulkan.h>
#include <vector>

#include <jjyou/vk/Vulkan.hpp>

class VirtualSwapchain {

public:

	/** @brief	Default constructor.
	  */
	VirtualSwapchain(void) {}

	/** @brief	Destructor.
	  */
	~VirtualSwapchain(void) {}

	/** @brief	Check whether the class contains images.
	  * @return `true` if not empty.
	  */
	bool has_value() const {
		return (this->device != nullptr);
	}

	/**	@brief	Create images and imageviews for the virtual swapchain
	  */
	void create(
		const jjyou::vk::PhysicalDevice& physicalDevice,
		const jjyou::vk::Device& device,
		jjyou::vk::MemoryAllocator& allocator,
		std::uint32_t imageCount,
		VkFormat format,
		VkExtent2D imageExtent
	);

	/** @brief	Call the corresponding vkDestroyXXX function to destroy the wrapped instance.
	  */
	void destroy(void);

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

	const jjyou::vk::Device* device = nullptr;
	jjyou::vk::MemoryAllocator* allocator = nullptr;
	std::vector<VkImage> swapchainImages = {};
	std::vector<jjyou::vk::Memory> swapchainImageMemories = {};
	std::vector<VkImageView> swapchainImageViews = {};
	VkFormat _format = VK_FORMAT_UNDEFINED;
	VkExtent2D _extent = {};
	std::uint32_t currentImageIdx = 0;

};