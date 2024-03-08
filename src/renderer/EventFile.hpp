#pragma once
#include "fwd.hpp"

#include <filesystem>
#include <vector>
#include <any>

enum class EventType {
	Undefined = 0,
	Available = 1,
	Play = 2,
	Save = 3,
	Mark = 4
};

struct Event {

public:

	template <class ...Args>
	Event(std::uint32_t time, EventType type, Args... args) : time(time), type(type), arguments{ args... } {}

	std::uint32_t time;

	EventType type;

	std::vector<std::any> arguments;
};

class EventFile {

public:

	EventFile(const std::filesystem::path& filename);

	std::vector<Event> events;

};