#include <iostream>
#include <fstream>
#include <filesystem>

#include <jjyou/vk/Vulkan.hpp>
#include <jjyou/glsl/glsl.hpp>
#include <jjyou/io/Json.hpp>
#include "TinyArgParser.hpp"
#include "Engine.hpp"
#include "Scene72.hpp"
#include "HostImage.hpp"
#include "EventFile.hpp"

#include <stb/stb_image.h>
int main(int argc, char* argv[]) {
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	try {
		// Parse arguments.
		TinyArgParser argParser;
		argParser.parseArgs(argc, argv);
		// List the physical devices and exit, if "--list-physical-devices" is inputted.
		if (argParser.listPhysicalDevices) {
			jjyou::vk::ContextBuilder builder;
			jjyou::vk::Context context{ nullptr };
			// Instance
			builder
				.headless(true)
				.enableValidation(false)
				.applicationName("Renderer72")
				.applicationVersion(0, 1, 0, 0)
				.engineName("Engine72")
				.engineVersion(0, 1, 0, 0)
				.apiVersion(0, 1, 0, 0);
			builder.buildInstance(context);
			// Physical device
			builder.requirePhysicalDeviceFeatures(
				VkPhysicalDeviceFeatures{
					.geometryShader = true,
					.samplerAnisotropy = true,
				}
			);
			std::vector<jjyou::vk::PhysicalDeviceInfo> physicalDeviceInfos = builder.listPhysicalDevices(context);
			for (const auto& physicalDeviceInfo : physicalDeviceInfos) {
				vk::PhysicalDeviceProperties properties = physicalDeviceInfo.physicalDevice.getProperties();
				std::cout << "===================================================" << std::endl;
				std::cout << "Device name: " << properties.deviceName << std::endl;
				std::cout << "API version: " << properties.apiVersion << std::endl;
				std::cout << "Driver version: " << properties.driverVersion << std::endl;
				std::cout << "Vendor ID: " << properties.vendorID << std::endl;
				std::cout << "Device ID: " << properties.deviceID << std::endl;
				std::cout << "Device type: " << string_VkPhysicalDeviceType(static_cast<VkPhysicalDeviceType>(properties.deviceType)) << std::endl;
				std::cout << "Available for this application : " << std::boolalpha << (physicalDeviceInfo.requiredCriteria == jjyou::vk::PhysicalDeviceInfo::Support::AllSupported) << std::endl;
			}
			std::cout << "===================================================" << std::endl;
			exit(0);
		} 

		// Initialize the engine.
		Engine engine(
			argParser.physicalDevice,
			argParser.enableValidation,
			argParser.headless.has_value(),
			argParser.drawingSize.has_value() ? (*argParser.drawingSize)[0] : 800,
			argParser.drawingSize.has_value() ? (*argParser.drawingSize)[1] : 600
		);

		// Load the scene.
		std::filesystem::path sceneBasePath = argParser.scene.parent_path();
		const jjyou::io::Json<> s72Json = jjyou::io::Json<>::parse(argParser.scene);
		s72::Scene72::Ptr pScene72 = engine.load(
			s72Json,
			sceneBasePath // We need base path because the json uses relative path to reference b72 file.
		);
		engine.setScene(pScene72);

		// Set culling mode
		engine.setCullingMode(argParser.culling);

		// Set camera
		if (argParser.camera.has_value())
			engine.setCameraMode(
				Engine::CameraMode::SCENE,
				*argParser.camera
			);

		// Main loop.
		if (!argParser.headless.has_value()) {
			engine.resetClockTime();
			engine.setPlayTime(0.0f);
			engine.setPlayRate(1.0f);
			while (!glfwWindowShouldClose(engine.window)) {
				engine.drawFrame();
				glfwPollEvents();
				
			}
		}
		else {
			std::filesystem::path eventBasePath = (*argParser.headless).parent_path();
			EventFile eventFile(*argParser.headless);
			engine.setClock(Clock::Ptr(new EventClock(eventFile))); // Set a fake clock
			engine.resetClockTime();
			engine.setPlayTime(0.0f);
			engine.setPlayRate(1.0f);
			for (const Event& event : eventFile.events) {
				switch (event.type) {
				case EventType::Available:
				{
					engine.drawFrame();
					break;
				}
				case EventType::Play:
				{
					engine.setPlayTime(std::any_cast<float>(event.arguments[0]));
					engine.setPlayRate(std::any_cast<float>(event.arguments[1]));
					break;
				}
				case EventType::Save:
				{
					HostImage image = engine.getLastRenderedFrame();
					image.write(eventBasePath / std::any_cast<std::string>(event.arguments[0]));
					break;
				}
				case EventType::Mark:
				{
					std::cout << std::any_cast<std::string>(event.arguments[0]) << std::endl;
					break;
				}
				default:
					break;
				}
			}
		}
		engine.destroy(*pScene72);
		pScene72 = nullptr;
	}
	catch (const std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "Program run for " << ms << "ms" << std::endl;
	return 0;
}