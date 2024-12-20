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
#include "Texture.hpp"
#include <jjyou/glsl/glsl.hpp>
#include <jjyou/vis/CameraView.hpp>
#include <jjyou/io/Json.hpp>
#include "VirtualSwapchain.hpp"
#include "ShadowMap.hpp"
#include "HostImage.hpp"
#include "Clock.hpp"
#include "GBuffer.hpp"
#include "SSAO.hpp"

class Engine {

public:

	jjyou::vk::Context context{ nullptr };

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

	struct SSAOParameters {
		std::array<jjyou::glsl::vec4, 256> ssaoSamples{};
	};

	enum class DeferredShadingRenderingMode : int {
		Scene = 0,
		SSAO,
		SSAOBlur,
		Albedo,
		Normal,
		Depth,
		Metalness,
		Roughness
	};

	struct SkyboxUniform {
		jjyou::glsl::mat4 model{};
	};

	static constexpr inline std::uint32_t NUM_CASCADE_LEVELS = 4;

	struct SunLight {
		std::array<float, NUM_CASCADE_LEVELS> cascadeSplits{}; // Pay attention to the alignment requirement!
		std::array<jjyou::glsl::mat4, NUM_CASCADE_LEVELS> orthographic = {}; // Project points in world space to texture uv
		jjyou::glsl::vec3 direction{}; // Object to light, in world space
		float angle = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by strength
		int shadow; // Shadow map size
	};

	struct SunLightShadowMapUniform {
		//Why not passing 4 mat4 projection matrices?
		//Because "push_constant" is only guaranteed to have at least 128 bytes.
		jjyou::glsl::vec3 orthoX{};
		float __dummy1 = 0.0f;
		jjyou::glsl::vec3 orthoY{};
		float __dummy2 = 0.0f;
		std::array<jjyou::glsl::vec2, NUM_CASCADE_LEVELS> center = {};
		std::array<float, NUM_CASCADE_LEVELS> width = {};
		std::array<float, NUM_CASCADE_LEVELS> height = {};
		std::array<float, NUM_CASCADE_LEVELS> zNear = {};
		std::array<float, NUM_CASCADE_LEVELS> zFar = {};
	};

	struct SphereLight {
		jjyou::glsl::vec3 position{}; // In world space
		float radius = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by power
		float limit = 0.0f;
	};

	struct SphereLightShadowMapUniform {
		jjyou::glsl::vec3 position{};
		float radius = 0.0f;
		jjyou::glsl::mat4 perspective{};
		float limit = 0.0f;
		float __dummy1 = 0.0f;
		float __dummy2 = 0.0f;
		float __dummy3 = 0.0f;
	};

	struct SpotLight {
		jjyou::glsl::mat4 perspective{}; // Project points in world space to texture uv
		jjyou::glsl::vec3 position{}; // In world space
		float radius = 0.0f;
		jjyou::glsl::vec3 direction{}; // Object to light, in world space
		float fov = 0.0f;
		jjyou::glsl::vec3 tint{}; // Already multiplied by power
		float blend = 0.0f;
		float limit = 0.0f;
		int shadow = 0;
		float __dummy1 = 0.0f;
		float __dummy2 = 0.0f;
	};

	struct SpotLightShadowMapUniform {
		jjyou::glsl::mat4 perspective{};
	};

	static constexpr inline std::uint32_t MAX_NUM_SUN_LIGHTS = 1;
	static constexpr inline std::uint32_t MAX_NUM_SUN_LIGHTS_NO_SHADOW = 16;

	static constexpr inline std::uint32_t MAX_NUM_SPHERE_LIGHTS = 4;
	static constexpr inline std::uint32_t MAX_NUM_SPHERE_LIGHTS_NO_SHADOW = 1024;

	static constexpr inline std::uint32_t MAX_NUM_SPOT_LIGHTS = 4;
	static constexpr inline std::uint32_t MAX_NUM_SPOT_LIGHTS_NO_SHADOW = 1024;
	struct Lights {
		int numSunLights = 0;
		int numSunLightsNoShadow = 0;
		int numSphereLights = 0;
		int numSphereLightsNoShadow = 0;
		int numSpotLights = 0;
		int numSpotLightsNoShadow = 0;
		float __dummy1 = 0.0f;
		float __dummy2 = 0.0f;
		std::array<SunLight, MAX_NUM_SUN_LIGHTS> sunLights = {};
		std::array<SunLight, MAX_NUM_SUN_LIGHTS_NO_SHADOW> sunLightsNoShadow = {};
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

	vk::raii::SurfaceKHR surface{ nullptr };

	vk::raii::DescriptorPool descriptorPool{ nullptr };

	VkCommandPool graphicsCommandPool, transferCommandPool;

	jjyou::vk::MemoryAllocator allocator;
	
	jjyou::vk::Swapchain swapchain{ nullptr };
	VirtualSwapchain virtualSwapchain{ nullptr };

	GBuffer gBuffer{ nullptr };
	SSAO ssao{ nullptr };

	VkFormat depthImageFormat;
	VkImage depthImage;
	jjyou::vk::Memory depthImageMemory;
	VkImageView depthImageView;

	VkRenderPass outputRenderPass;// one color attachment
	vk::raii::RenderPass deferredRenderPass{ nullptr }; // write to g buffer
	vk::raii::RenderPass ssaoRenderPass{ nullptr }; // write to ssao
	vk::raii::RenderPass shadowMappingRenderPass{ nullptr }; // write to depth buffer

	std::vector<VkFramebuffer> framebuffers;

	std::array<FrameData, Engine::MAX_FRAMES_IN_FLIGHT> frameData;

	VkDescriptorSetLayout viewLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout viewLevelUniformWithSSAODescriptorSetLayout;
	VkDescriptorSetLayout objectLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout skyboxUniformDescriptorSetLayout;
	VkDescriptorSetLayout mirrorMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout environmentMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout lambertianMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout pbrMaterialLevelUniformDescriptorSetLayout;
	VkDescriptorSetLayout ssaoDescriptorSetLayout;
	VkDescriptorSetLayout ssaoBlurDescriptorSetLayout;

	VkPipelineLayout simpleForwardPipelineLayout;
	VkPipeline simpleForwardPipeline;

	VkPipelineLayout mirrorForwardPipelineLayout;
	VkPipeline mirrorForwardPipeline;

	VkPipelineLayout environmentForwardPipelineLayout;
	VkPipeline environmentForwardPipeline;

	VkPipelineLayout lambertianForwardPipelineLayout;
	VkPipeline lambertianForwardPipeline;

	VkPipelineLayout pbrDeferredPipelineLayout;
	VkPipeline pbrDeferredPipeline;

	VkPipelineLayout skyboxPipelineLayout;
	VkPipeline skyboxPipeline;

	VkPipelineLayout spotlightPipelineLayout;
	VkPipeline spotlightPipeline;

	VkPipelineLayout spherelightPipelineLayout;
	VkPipeline spherelightPipeline;

	VkPipelineLayout sunlightPipelineLayout;
	VkPipeline sunlightPipeline;

	VkPipelineLayout ssaoPipelineLayout;
	VkPipeline ssaoPipeline;

	VkPipelineLayout ssaoBlurPipelineLayout;
	VkPipeline ssaoBlurPipeline;

	VkPipelineLayout deferredShadingCompositionPipelineLayout;
	VkPipeline deferredShadingCompositionPipeline;

	jjyou::vk::Texture2D ssaoNoise{};
	SSAOParameters ssaoParameters;

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

	vk::raii::ShaderModule createShaderModule(const std::filesystem::path& path) const;

	void updateGBufferAndSSAOSampler(const s72::Scene72& scene) const;

};