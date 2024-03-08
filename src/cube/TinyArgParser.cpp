#include "TinyArgParser.hpp"

#include <exception>

void TinyArgParser::parseArgs(int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--lambertian") == 0) {
			this->lambertian = true;
		}
		else if (std::strcmp(argv[i], "--prefiltered-env") == 0) {
			this->prefilteredenv = true;
		}
		else if (std::strcmp(argv[i], "--env-brdf") == 0) {
			this->envbrdf = true;
		}
		else if (std::strcmp(argv[i], "--lambertian-output-size") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the lambertian output image size (per face) using \"--lambertian-output-size w.");
			std::uint32_t length = std::stoul(argv[i + 1]);
			this->lambertianOutputSize = VkExtent2D{ .width = length, .height = length };
			i += 1;
		}
		else if (std::strcmp(argv[i], "--lambertian-sample-batch") == 0) {
			if (i >= argc - 2)
				throw std::runtime_error("Please specify the batch size of lambertian sampling using \"--lambertian-sample-batch w h.");
			this->lambertianSampleBatch = jjyou::glsl::ivec2(std::stoi(argv[i + 1]), std::stoi(argv[i + 2]));
			i += 2;
		}
		else if (std::strcmp(argv[i], "--prefiltered-env-output-size") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the prefiltered environment output image size (per face) using \"--prefiltered-env-output-size w.");
			std::uint32_t length = std::stoul(argv[i + 1]);
			this->prefilteredenvOutputSize = VkExtent2D{ .width = length, .height = length };
			i += 1;
		}
		else if (std::strcmp(argv[i], "--prefiltered-env-output-level") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the prefiltered environment output level using \"--prefiltered-env-output-level l.");
			this->prefilteredenvOutputLevel = std::stoi(argv[i + 1]);
			i += 1;
		}
		else if (std::strcmp(argv[i], "--prefiltered-env-num-samples") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the number of samples of prefiltered environment sampling using \"--prefiltered-env-num-samples n.");
			this->prefilteredenvNumSamples = std::stoi(argv[i + 1]);
			i += 1;
		}
		else if (std::strcmp(argv[i], "--prefiltered-env-sample-batch") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the batch size of prefiltered environment sampling using \"--prefiltered-env-sample-batch b.");
			this->prefilteredenvSampleBatch = std::stoi(argv[i + 1]);
			i += 1;
		}
		else if (std::strcmp(argv[i], "--env-brdf-output-size") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the environment BRDF output image size (per face) using \"--env-brdf-output-size w.");
			std::uint32_t length = std::stoul(argv[i + 1]);
			this->envbrdfOutputSize = VkExtent2D{ .width = length, .height = length };
			i += 1;
		}
		else if (std::strcmp(argv[i], "--env-brdf-num-samples") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the number of samples of environment BRDF sampling using \"--env-brdf-num-samples n.");
			this->envbrdfNumSamples = std::stoi(argv[i + 1]);
			i += 1;
		}
		else if (std::strcmp(argv[i], "--env-brdf-sample-batch") == 0) {
			if (i >= argc - 1)
				throw std::runtime_error("Please specify the batch size of environment BRDF sampling using \"--env-brdf-sample-batch b.");
			this->envbrdfSampleBatch = std::stoi(argv[i + 1]);
			i += 1;
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
		else if (std::strcmp(argv[i], "--enable-validation") == 0) {
			this->enableValidation = true;
		}
		else if (std::strcmp(argv[i], "--input") == 0) {
			if (i == argc - 1)
				throw std::runtime_error("Please specify the input cube map using \"--input /path/to/input\".");
			this->inputImage = argv[i + 1];
			++i;
		}
	}
	if (!this->inputImage.has_value() && (this->lambertian || this->prefilteredenv)) {
		throw std::runtime_error("The input cube map is required for precomputing lambertian LUT and prefiltered environment map LUT.");
	}
}