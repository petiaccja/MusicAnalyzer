#pragma once

#include "Graph_All.hpp"

#include <vector>
#include <algorithm>

#define NOMINMAX
#include <Windows.h>


class VolumeDisplay
	: public exc::InputPortConfig<int, std::vector<std::vector<float>>>,
	// sample rate, channel samples
	public exc::OutputPortConfig<>
{
public:
	VolumeDisplay() {
		// ugly console hack
		COORD topLeft = { 0, 0 };
		HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO screen;
		DWORD written;
		GetConsoleScreenBufferInfo(console, &screen);
		width = screen.dwSize.X;
		height = screen.dwSize.Y;
		hConsole = console;
	}

	void Notify(exc::InputPortBase* sender) override {}
	void Update() override {
		int inputSampleRate = GetInput<0>().Get();
		std::vector<std::vector<float>> samples = GetInput<1>().Get();


		// show volume indicators
		DWORD written;
		FillConsoleOutputCharacterA(hConsole, ' ', width * samples.size(), { 0, 1 }, &written);
		SetConsoleCursorPosition(hConsole, { 0, 1 });
		for (int ch = 0; ch < samples.size(); ++ch) {
			float volf = !samples[ch].empty() ? samples[ch][0] : 0.0f;
			float voldb = 20 * log10(volf);
			voldb = std::max(-60.f, voldb);
			voldb /= 60.f;
			voldb += 1.0f;

			int numChars = std::min(int(width), int(width*voldb));
			numChars = std::max(0, numChars);
			FillConsoleOutputCharacterA(hConsole, '#', numChars, { 0, SHORT(1 + ch) }, &written);
		}
	}

private:
	HANDLE hConsole;
	int width = 0, height = 0;
};