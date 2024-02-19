#pragma once
#include "fwd.hpp"

#include <string>
#include <array>
#include <filesystem>
#include <optional>

#include "Engine.hpp"

// This class is only used for 15-472 assignment.
// It will be replaced by a more generic parser jjyou::io::ArgParser in the future.
// (If I have time.)
class TinyArgParser {

public:

	TinyArgParser() {}

	void parseArgs(int argc, char* argv[]);

public:

	// Assignment required arguments.
	std::filesystem::path scene;
	std::optional<std::string> camera = std::nullopt;
	std::optional<std::string> physicalDevice = std::nullopt;
	bool listPhysicalDevices = false;
	std::optional <std::array<int, 2>> drawingSize = std::nullopt;
	Engine::CullingMode culling = Engine::CullingMode::NONE;
	std::optional<std::filesystem::path> headless = std::nullopt;

	// Additional arguments.
	bool enableValidation = false;
};