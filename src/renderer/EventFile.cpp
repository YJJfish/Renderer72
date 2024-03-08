#include "EventFile.hpp"
#include <fstream>
#include <sstream>

EventFile::EventFile(const std::filesystem::path& filename) {
	std::ifstream fin(filename, std::ios::in);
	if (!fin.is_open())
		throw std::runtime_error("Unable to open file.");
	std::string buffer;
	std::stringstream sstream;
	while (true) {
		std::getline(fin, buffer);
		sstream.clear(); sstream << buffer;
		std::uint32_t time;
		if (!(sstream >> time)) return;
		std::string eventTypeString;
		sstream >> eventTypeString;
		if (eventTypeString == "AVAILABLE") {
			this->events.emplace_back(time, EventType::Available);
		}
		else if (eventTypeString == "PLAY") {
			float t, rate;
			sstream >> t >> rate;
			this->events.emplace_back(time, EventType::Play, t, rate);
		}
		else if (eventTypeString == "SAVE") {
			std::string ppmFile;
			sstream >> ppmFile;
			this->events.emplace_back(time, EventType::Save, ppmFile);
		}
		else if (eventTypeString == "MARK") {
			std::string description;
			std::getline(sstream, description);
			int i = 0;
			while (description[i] == ' ' || description[i] == '\t') ++i;
			if (i > 0) description = description.substr(i);
			this->events.emplace_back(time, EventType::Mark, description);
		}
		else {
			this->events.emplace_back(time, EventType::Undefined);
		}
	}
}