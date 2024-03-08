#pragma once
#include "fwd.hpp"

#include <string>
#include <array>
#include <filesystem>
#include <optional>
#include <jjyou/glsl/glsl.hpp>
#include <jjyou/vk/Vulkan.hpp>

// This class is only used for 15-472 assignment.
// It will be replaced by a more generic parser jjyou::io::ArgParser in the future.
// (If I have time.)
class TinyArgParser {

public:

	TinyArgParser() {}

	void parseArgs(int argc, char* argv[]);

public:

	std::optional<std::filesystem::path> inputImage;
	bool lambertian = false;
	bool prefilteredenv = false;
	bool envbrdf = false;
	VkExtent2D lambertianOutputSize = VkExtent2D{ .width = 512U, .height = 512U };
	jjyou::glsl::ivec2 lambertianSampleBatch = jjyou::glsl::ivec2(32, 32);

	VkExtent2D prefilteredenvOutputSize = VkExtent2D{ .width = 512U, .height = 512U };
	int prefilteredenvOutputLevel = 7;
	int prefilteredenvNumSamples = 1024 * 4096;
	int prefilteredenvSampleBatch = 4096;

	VkExtent2D envbrdfOutputSize = VkExtent2D{ .width = 512U, .height = 512U };
	int envbrdfNumSamples = 4096;
	int envbrdfSampleBatch = 4096;

	std::optional<std::string> physicalDevice = std::nullopt;
	bool listPhysicalDevices = false;
	bool enableValidation = false;
};