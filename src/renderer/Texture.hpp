/***********************************************************************
 * @file	Memory.hpp
 * @author	jjyou
 * @date	2024-2-6
 * @brief	This file implements Texture2D class.
***********************************************************************/
#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <jjyou/vk/Context.hpp>
#include <jjyou/vk/Legacy/Memory.hpp>
#include "utils.hpp"

namespace jjyou {

	namespace vk {

		class Texture2D {

		public:

			/** @brief	Default constructor.
			  */
			Texture2D(void) {}

			/** @brief	Destructor.
			  */
			~Texture2D(void) {
				this->destroy();
			}

			Texture2D(const Texture2D&) = delete;

			Texture2D(Texture2D&& other) noexcept :
				_pContext(other._pContext),
				_extent(other._extent),
				_numLayers(other._numLayers),
				_mipLevels(other._mipLevels),
				_format(other._format),
				_pAllocator(other._pAllocator),
				_image(other._image),
				_imageMemory(std::move(other._imageMemory)),
				_imageView(other._imageView),
				_sampler(other._sampler)
			{
				other._pContext = nullptr;
			}

			/** @brief	Check whether the texture is in a valid state
			  */
			bool has_value(void) const { return (this->_pContext != nullptr); }

			/** @brief	Create a texture.
			  */
			void create(
				const Context& context,
				MemoryAllocator& allocator,
				VkCommandPool graphicsCommandPool,
				VkCommandPool transferCommandPool,
				const void* data,
				VkFormat format,
				VkExtent2D extent,
				int mipLevels = 1,
				const std::vector<void*>& mipData = {},
				bool cubeMap = false,
				VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
			);

			/** @brief	Call the corresponding vkDestroyXXX function to destroy the wrapped instance.
			  */
			void destroy(void) {
				if (this->_pContext != nullptr) {
					this->_pAllocator->free(this->_imageMemory);
					this->_pAllocator = nullptr;
					vkDestroySampler(*this->_pContext->device(), this->_sampler, nullptr);
					this->_sampler = nullptr;
					vkDestroyImageView(*this->_pContext->device(), this->_imageView, nullptr);
					this->_imageView = nullptr;
					vkDestroyImage(*this->_pContext->device(), this->_image, nullptr);
					this->_image = nullptr;
					this->_extent = { .width = 0, .height = 0 };
					this->_mipLevels = 0;
					this->_format = VK_FORMAT_UNDEFINED;
					this->_pContext = nullptr;
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

			/** @brief	Get the number of texture layers.
			  * @return Number of texture image layers.
			  */
			std::uint32_t numLayers(void) const { return this->_numLayers; }

			/** @brief	Get the texture image mipmap.
			  * @return Texture image mipmap levels.
			  */
			std::uint32_t mipLevels(void) const { return this->_mipLevels; }

			/** @brief	Get the texture format.
			  * @return Texture format.
			  */
			VkFormat format(void) const { return this->_format; }

		private:

			const Context* _pContext = nullptr;
			VkExtent2D _extent{};
			std::uint32_t _numLayers = 0;
			std::uint32_t _mipLevels = 0;
			VkFormat _format = VK_FORMAT_UNDEFINED;
			MemoryAllocator* _pAllocator = nullptr;
			VkImage _image = nullptr;
			Memory _imageMemory{};
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

	}

}