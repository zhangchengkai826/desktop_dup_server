#pragma once

class PerfTimer {
	std::chrono::time_point<std::chrono::high_resolution_clock> tp;
	long long elapsed = 0;
	long long fpsCounter = 0;
public:
	void Reset();
	void Print(const std::string& title);
	void Print(const std::string& title, bool countFps);
};