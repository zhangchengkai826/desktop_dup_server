#include "stdafx.h"
#include "perf_timer.h"

void PerfTimer::Reset()
{
	tp = std::chrono::high_resolution_clock::now();
}

void PerfTimer::Print(const std::string& title)
{
	Print(title, false);
}

void PerfTimer::Print(const std::string& title, bool countFps)
{
	auto newTp = std::chrono::high_resolution_clock::now();
	auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(newTp - tp).count();
	OutputDebugStringA(tsf::fmt("%s: %d\n", title.c_str(), dt).c_str());
	tp = newTp;

	if (countFps)
		fpsCounter++;
	elapsed += dt;

	if (elapsed >= 1000000000) {
		OutputDebugStringA(tsf::fmt("***Fps: %f\n", (float)fpsCounter / elapsed * 1000000000).c_str());
		OutputDebugStringA(tsf::fmt("***Mspf: %f\n", (float)elapsed / fpsCounter).c_str());

		fpsCounter = 0;
		elapsed = 0;
	}
}
