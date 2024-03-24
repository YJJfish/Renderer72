#pragma once
#include "fwd.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <set>
#include <optional>
#include <utility>
#include <tuple>
#include <filesystem>

#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include <jjyou/vis/CameraView.hpp>
#include <jjyou/io/Json.hpp>
#include "VirtualSwapchain.hpp"
#include "ShadowMap.hpp"
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

	std::shared_ptr<s72::Scene72> load(
		const jjyou::io::Json<>& json,
		const std::filesystem::path& baseDir
	);

	void destroy(s72::Scene72& scene72);

	void setScene(std::shared_ptr<s72::Scene72> pScene72);

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

public:

	struct ViewLevelUniform {
		jjyou::glsl::mat4 projection{};
		jjyou::glsl::mat4 view{};
		jjyou::glsl::vec4 viewPos{};
	};

	struct ObjectLevelUniform {
		jjyou::glsl::mat4 model{};
		jjyou::glsl::mat4 normal{};
	};

	struct SkyboxUniform {
		jjyou::glsl::mat4 model{};
	};

	struct SunLight {
		jjyou::glsl::vec3 direction{}; // Object to light, in world space
		float angle = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by strength
		float __dummy1 = 0.0f;
	};

	struct SphereLight {
		jjyou::glsl::vec3 position{}; // In world space
		float radius = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by power
		float limit = 0.0f;
	};

	struct SpotLight {
		jjyou::glsl::mat4 lightSpace{}; // Project points in world space to texture uv
		jjyou::glsl::vec3 position{}; // In world space
		float radius = 0.0f;
		jjyou::glsl::vec3 direction{}; // Object to light, in world space
		float fov = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by power
		float blend = 0.0f;
		float limit = 0.0f;
		int shadow = 0;
		float __dummy2 = 0.0f;
		float __dummy3 = 0.0f;
	};

	static constexpr inline std::uint32_t MAX_NUM_SUM_LIGHTS = 1;
	static constexpr inline std::uint32_t MAX_NUM_SUM_LIGHTS_NO_SHADOW = 16;

	static constexpr inline std::uint32_t MAX_NUM_SPHERE_LIGHTS = 4;
	static constexpr inline std::uint32_t MAX_NUM_SPHERE_LIGHTS_NO_SHADOW = 128;

	static constexpr inline std::uint32_t MAX_NUM_SPOT_LIGHTS = 4;
	static constexpr inline std::uint32_t MAX_NUM_SPOT_LIGHTS_NO_SHADOW = 128;
	struct Lights {
		int numSunLights = 0;
		int numSunLightsNoShadow = 0;
		int numSphereLights = 0;
		int numSphereLightsNoShadow = 0;
		int numSpotLights = 0;
		int numSpotLightsNoShadow = 0;
		float __dummy1 = 0.0f;
		float __dummy2 = 0.0f;
		std::array<SunLight, MAX_NUM_SUM_LIGHTS> sunLights = {};
		std::array<SunLight, MAX_NUM_SUM_LIGHTS_NO_SHADOW> sunLightsNoShadow = {};
		std::array<SphereLight, MAX_NUM_SPHERE_LIGHTS> sphereLights = {};
		std::array<SphereLight, MAX_NUM_SPHERE_LIGHTS_NO_SHADOW> sphereLightsNoShadow = {};
		std::array<SpotLight, MAX_NUM_SPOT_LIGHTS> spotLights = {};
		std::array<SpotLight, MAX_NUM_SPOT_LIGHTS_NO_SHADOW> spotLightsNoShadow = {};
	};

	struct FrameData {
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
	std::shared_ptr<s72::Scene72> pScene72{};
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

	static constexpr inline std::uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	bool offscreen;

	GLFWwindow* window;

	jjyou::vk::Context context{ nullptr };

	vk::raii::SurfaceKHR surface{ nullptr };

	VkCommandPool graphicsCommandPool, transferCommandPool;

	jjyou::vk::MemoryAllocator allocator;
	
	jjyou::vk::Swapchain swapchain{ nullptr };
	VirtualSwapchain virtualSwapchain{ nullptr };

	VkFormat depthImageFormat;
	VkImage depthImage;
	jjyou::vk::Memory depthImageMemory;
	VkImageView depthImageView;

	VkRenderPass renderPass;
	vk::raii::RenderPass shadowMappingRenderPass{ nullptr };

	std::vector<VkFramebuffer> framebuffers;

	std::array<FrameData, Engine::MAX_FRAMES_IN_FLIGHT> frameData;

	VkDescriptorSetLayout viewLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout objectLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout skyboxUniformDescriptorSetLayout;
	VkDescriptorSetLayout mirrorMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout environmentMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout lambertianMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout pbrMaterialLevelUniformDescriptorSetLayout;

	VkPipelineLayout simplePipelineLayout;
	VkPipeline simplePipeline;

	VkPipelineLayout mirrorPipelineLayout;
	VkPipeline mirrorPipeline;

	VkPipelineLayout environmentPipelineLayout;
	VkPipeline environmentPipeline;

	VkPipelineLayout lambertianPipelineLayout;
	VkPipeline lambertianPipeline;

	VkPipelineLayout pbrPipelineLayout;
	VkPipeline pbrPipeline;

	VkPipelineLayout skyboxPipelineLayout;
	VkPipeline skyboxPipeline;

	VkPipelineLayout spotlightPipelineLayout;
	VkPipeline spotlightPipeline;

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
		const std::set<std::uint32_t>& queueFamilyIndices,
		VkMemoryPropertyFlags properties
	);

	std::pair<VkBuffer, jjyou::vk::Memory> createBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		const std::set<std::uint32_t>& queueFamilyIndices,
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

	VkShaderModule createShaderModule(const std::filesystem::path& path) const;

};