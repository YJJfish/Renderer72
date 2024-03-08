#pragma once

#include "fwd.hpp"

#include <memory>
#include <exception>
#include <stdexcept>
#include <filesystem>
#include <jjyou/glsl/glsl.hpp>


// Reference counted host image. Similar to OpenCV's cv::Mat
class HostImage {

public:

	using Color4b = jjyou::glsl::vec<std::uint8_t, 4>;

	HostImage(void) = default;
	HostImage(std::uint32_t width, std::uint32_t height) : _width(width), _height(height), _data(new Color4b[width * height]) {};
	HostImage(const HostImage&) = default;
	HostImage(HostImage&&) = default;
	~HostImage(void) { this->release(); }
	HostImage& operator=(const HostImage&) = default;
	HostImage& operator=(HostImage&&) = default;
	void release(void) { this->_width = 0; this->_height = 0; this->_data.reset(); }
	void create(std::uint32_t width, std::uint32_t height) { this->_width = width; this->_height = height; this->_data.reset(new Color4b[width * height]); }
	bool empty(void) const { return (this->_data == nullptr); }
	Color4b& at(std::uint32_t row, std::uint32_t col) { if (row >= this->_height || col >= this->_width) throw std::out_of_range("Row or column index out of range."); return this->_data[row * this->_width + col]; }
	const Color4b& at(std::uint32_t row, std::uint32_t col) const { if (row >= this->_height || col >= this->_width) throw std::out_of_range("Row or column index out of range."); return this->_data[row * this->_width + col]; }
	Color4b& operator()(std::uint32_t row, std::uint32_t col) { return this->_data[row * this->_width + col]; }
	const Color4b& operator()(std::uint32_t row, std::uint32_t col) const { return this->_data[row * this->_width + col]; }
	Color4b* data() const { return this->_data.get(); }

	void write(const std::filesystem::path& filename) const;

private:

	std::uint32_t _width = 0;
	std::uint32_t _height = 0;
	std::shared_ptr<Color4b[]> _data{};

	void _writePPM(const std::filesystem::path& filename) const;

};