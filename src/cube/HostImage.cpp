#include "HostImage.hpp"
#include <fstream>

void HostImage::write(const std::filesystem::path& filename) const {
	if (this->empty())
		throw std::runtime_error("Trying to write an empty image to file.");
	if (filename.extension() == ".ppm") {
		this->_writePPM(filename);
	}
}

void HostImage::_writePPM(const std::filesystem::path& filename) const {
	std::ofstream fout(filename, std::ios::out | std::ios::binary);
	if (!fout.is_open())
		throw std::runtime_error("Unable to open file.");
	fout << "P6" << std::endl << this->_width << std::endl << this->_height << std::endl << 255 << std::endl;
	for (std::uint32_t r = 0; r < this->_height; ++r)
		for (std::uint32_t c = 0; c < this->_width; ++c)
			fout.write(reinterpret_cast<const char*>(&(*this)(r, c)), 3);
	fout.close();
}