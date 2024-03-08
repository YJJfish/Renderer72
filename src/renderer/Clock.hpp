#pragma once
#include "fwd.hpp"
#include <chrono>
#include <memory>
#include "EventFile.hpp"

// An abstract clock. Used to play animations.
// This class is useful when you want to use a fake clock to debug the animation.
class Clock {

public:

	// We use unique_ptr instead of shared_ptr, because we don't want multiple clocks
	// to interfere with each other.
	using Ptr = std::unique_ptr<Clock>;

	Clock(void) {}
	virtual ~Clock(void) {}

	// Set current time to 0 and start timing.
	virtual void reset(void) = 0;

	// Get the current time, in seconds.
	virtual float now(void) = 0;
};

// By default we use `std::chrono::steady_clock`.
class SteadyClock : public Clock {

public:

	using Ptr = std::unique_ptr<SteadyClock>;

	SteadyClock(void) {}
	virtual ~SteadyClock(void) {}

	virtual void reset(void) override {
		this->startTime = std::chrono::steady_clock::now();
	}

	virtual float now(void) override {
		std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
		std::chrono::duration<float> duration = std::chrono::duration_cast<std::chrono::duration<float>>(now - this->startTime);
		return duration.count();
	}

private:

	std::chrono::time_point<std::chrono::steady_clock> startTime;

};

// This is a fake clock. We use the time provided by event file.
class EventClock : public Clock {

public:

	using Ptr = std::unique_ptr<EventClock>;

	EventClock(const EventFile& eventFile) : pEventFile(&eventFile), iter(0) {}
	virtual ~EventClock(void) {}

	virtual void reset(void) override {
		this->iter = 0;
	}

	virtual float now(void) override {
		std::uint32_t ts = this->pEventFile->events[this->iter].time;
		if (this->iter + 1 < this->pEventFile->events.size())
			++this->iter;
		return static_cast<float>(ts) / 1000000.0f;
	}

private:

	const EventFile* pEventFile;
	std::size_t iter;

};