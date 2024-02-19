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
int main(int argc, char* argv[]) {
	try {
		// Parse arguments.
		TinyArgParser argParser;
		argParser.parseArgs(argc, argv);
		// List the physical devices and exit, if "--list-physical-devices" is inputted.
		if (argParser.listPhysicalDevices) {
			jjyou::vk::InstanceBuilder builder;
			jjyou::vk::Instance instance = builder.offscreen(true).build();
			jjyou::vk::PhysicalDeviceSelector selector(instance, nullptr);
			std::vector<jjyou::vk::PhysicalDevice> physicalDevices = selector.listAllPhysicalDevices();
			for (const auto& physicalDevice : physicalDevices) {
				std::cout << "===================================================" << std::endl;
				std::cout << "Device name: " << physicalDevice.deviceProperties().deviceName << std::endl;
				std::cout << "API version: " << physicalDevice.deviceProperties().apiVersion << std::endl;
				std::cout << "Driver version: " << physicalDevice.deviceProperties().driverVersion << std::endl;
				std::cout << "Vendor ID: " << physicalDevice.deviceProperties().vendorID << std::endl;
				std::cout << "Device ID: " << physicalDevice.deviceProperties().deviceID << std::endl;
				std::cout << "Device type: " << string_VkPhysicalDeviceType(physicalDevice.deviceProperties().deviceType) << std::endl;
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
				Engine::CameraMode::USER,
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
	}
	catch (const std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	}
	return 0;
}