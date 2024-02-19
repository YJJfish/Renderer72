#pragma once
#include "fwd.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <optional>
#include <utility>
#include <tuple>
#include <filesystem>

#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include <jjyou/vis/CameraView.hpp>
#include <jjyou/io/Json.hpp>
#include "Scene72.hpp"
#include "VirtualSwapchain.hpp"
#include "HostImage.hpp"
#include "Clock.hpp"

class Engine {

public:

	enum class PlayMode {
		CYCLE = 0,
		SINGLE = 1,
		REVERSE = 2,
	};

	enum class CullingMode {
		NONE = 0,
		FRUSTUM = 1,
	};

	enum class CameraMode {
		SCENE = 0,
		USER = 1,
		DEBUG = 2
	};

	Engine(
		std::optional<std::string> physicalDeviceName,
		bool enableValidation,
		bool offscreen,
		int winWidth,
		int winHeight
	);

	~Engine(void);

	s72::Scene72::Ptr load(
		const jjyou::io::Json<>& json,
		const std::filesystem::path& baseDir
	);

	void destroy(s72::Scene72& scene72);

	void setScene(s72::Scene72::Ptr pScene72);

	void drawFrame();

	// Only available in offscreen mode
	HostImage getLastRenderedFrame(void);

	void setPlayRate(float playRate) { this->playRate = playRate; }
	void setPlayTime(float playTime) { this->currPlayTime = playTime; }
	void pause(bool whether) { this->paused = whether; }
	void switchPauseState() { this->paused = !this->paused; }
	void setPlayMode(PlayMode mode) { this->playMode = mode; }
	void setCullingMode(CullingMode mode) { this->cullingMode = mode; }
	void setCameraMode(CameraMode cameraMode, std::optional<std::string> camera = std::nullopt);
	void resetClockTime(void) { this->clock->reset(); this->currClockTime = 0.0f; }
	void setClock(Clock::Ptr&& clock) { this->clock = std::move(clock); }

private:

	struct ViewLevelUniform {
		jjyou::glsl::mat4 projection;
		jjyou::glsl::mat4 view;
	};

	struct ObjectLevelUniform {
		jjyou::glsl::mat4 model;
		jjyou::glsl::mat4 normal;
	};

	struct FrameData {
		VkBuffer viewLevelUniformBuffer, objectLevelUniformBuffer = nullptr;
		jjyou::vk::Memory viewLevelUniformBufferMemory, objectLevelUniformBufferMemory;
		VkDescriptorSet viewLevelUniformDescriptorSet = nullptr, objectLevelUniformDescriptorSet = nullptr;
		VkCommandBuffer graphicsCommandBuffer = nullptr;
		VkSemaphore imageAvailableSemaphore = nullptr;
		VkSemaphore renderFinishedSemaphore = nullptr;
		VkFence inFlightFence = nullptr;
	};

	/** @brief	Framebuffer resize callback for glfw.
	  */
	bool framebufferResized = false;
	static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

	/** @brief	Mouse button callback.
	  */
	double cursorX = 0.0;
	double cursorY = 0.0;
	std::chrono::steady_clock::time_point mouseButtonLeftPressTime{};
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	
	/** @brief	Cursor position callback.
	  */
	static void cursorPosCallback(GLFWwindow* window, double xPos, double yPos);

	/** @brief	Scroll callback.
	  */
	static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);

	/** @brief	Key callback.
	  */
	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	void handleFramebufferResizing(void);
	void createSwapchain(void);
	void createFramebuffers(void);
	void createDepthImage(void);


public:
	
	/** @name	Rendering Options
	  */
	//@{
	s72::Scene72::Ptr pScene72{};
	float currPlayTime = 0.0f;
	float playRate = 1.0f;
	bool paused = false;
	PlayMode playMode = PlayMode::CYCLE;
	CullingMode cullingMode = CullingMode::NONE;
	CameraMode cameraMode = CameraMode::USER;
	std::string cameraName;
	Clock::Ptr clock{ new SteadyClock()};
	//@}

	int currentFrame = 0;
	float currClockTime = 0.0f;
	jjyou::vis::SceneView sceneViewer{};

	static constexpr inline int MAX_FRAMES_IN_FLIGHT = 2;

	bool offscreen;

	jjyou::vk::Loader loader;

	GLFWwindow* window;

	VkSurfaceKHR surface;

	jjyou::vk::Instance instance;

	jjyou::vk::PhysicalDevice physicalDevice;

	jjyou::vk::Device device;

	VkCommandPool graphicsCommandPool, transferCommandPool;

	jjyou::vk::MemoryAllocator allocator;
	
	jjyou::vk::Swapchain swapchain;
	VirtualSwapchain virtualSwapchain;

	VkFormat depthImageFormat;
	VkImage depthImage;
	jjyou::vk::Memory depthImageMemory;
	VkImageView depthImageView;

	VkRenderPass renderPass;

	std::vector<VkFramebuffer> framebuffers;

	std::array<FrameData, Engine::MAX_FRAMES_IN_FLIGHT> frameData;

	VkDescriptorSetLayout viewLevelUniformDescriptorSetLayout, objectLevelUniformDescriptorSetLayout;
	VkDescriptorPool descriptorPool;

	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;

public:

	VkImageView createImageView(
		VkImage image,
		VkFormat format,
		VkImageAspectFlags aspectFlags
	) const;
	std::pair<VkImage, jjyou::vk::Memory> createImage(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		const std::vector<std::uint32_t>& queueFamilyIndices,
		VkMemoryPropertyFlags properties
	);

	std::pair<VkBuffer, jjyou::vk::Memory> createBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		const std::vector<std::uint32_t>& queueFamilyIndices,
		VkMemoryPropertyFlags properties
	);

	void copyBuffer(
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		VkDeviceSize size
	) const;

	static void insertImageMemoryBarrier(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange
	);

};