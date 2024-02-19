#include "TinyArgParser.hpp"

#include <exception>

void TinyArgParser::parseArgs(int argc, char* argv[]) {
	std::optional<std::filesystem::path> scene;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--scene") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the path to the scene using \"--scene \\path\\to\\scene_file\".");
			scene = argv[i + 1];
			++i;
		}
		else if (std::strcmp(argv[i], "--camera") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the camera name using \"--camera camera_name\".");
			this->camera = argv[i + 1];
			++i;
		}
		else if (std::strcmp(argv[i], "--physical-device") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the physical device name using \"--physical-device device_name\".");
			this->physicalDevice = argv[i + 1];
			++i;
		}
		else if (std::strcmp(argv[i], "--list-physical-devices") == 0) {
			this->listPhysicalDevices = true;
		}
		else if (std::strcmp(argv[i], "--drawing-size") == 0) {
			if (i >= argc - 2)
				throw std::runtime_error("Please specify the drawing size using \"--drawing-size width height\".");
			this->drawingSize.emplace(std::array<int, 2>{ {std::stoi(argv[i + 1]), std::stoi(argv[i + 2])} });
			i += 2;
		}
		else if (std::strcmp(argv[i], "--culling") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the culling mode using \"--culling culling_mode\".");
			if (std::strcmp(argv[i + 1], "none") == 0)
				this->culling = Engine::CullingMode::NONE;
			else if (std::strcmp(argv[i + 1], "frustum") == 0)
				this->culling = Engine::CullingMode::FRUSTUM;
			else
				throw std::runtime_error("Unsupported culling mode.");
			++i;
		}
		else if (std::strcmp(argv[i], "--headless") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the headless mode using \"--headless \\path\\to\\events_file\".");
			this->headless = argv[i + 1];
			++i;
		}
		else if (std::strcmp(argv[i], "--enable-validation") == 0) {
			this->enableValidation = true;
		}
	}
	if (this->listPhysicalDevices == true)
		return;
	if (!scene.has_value())
		throw std::runtime_error("Argument \"--scene \\path\\to\\scene_file\" is REQUIRED.");
	this->scene = *scene;
	if (this->headless.has_value() && !this->drawingSize.has_value())
		throw std::runtime_error("Argument \"--drawing-size width height\" is REQUIRED in headless mode.");
}