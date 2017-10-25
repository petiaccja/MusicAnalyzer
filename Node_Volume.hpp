#pragma once

#include "Graph_All.hpp"

#include <vector>


class Volume
	// sample rate, channel samples
	: public exc::InputPortConfig<int, std::vector<std::vector<float>>>,
	// sample rate, channel samples
	public exc::OutputPortConfig<int, std::vector<std::vector<float>>>
{
public:
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override {
		int inputSampleRate = GetInput<0>().Get();
		std::vector<std::vector<float>> samples = GetInput<1>().Get();

		if (samples.size() != m_values.size()) {
			m_values.resize(samples.size(), 0.0f);
		}

		float decayFactor = 0.98f;
		for (int ch = 0; ch < samples.size(); ++ch) {
			for (int s = 0; s < samples[ch].size(); ++s) {
				float sq = samples[ch][s] * samples[ch][s];
				float volume = m_values[ch]*m_values[ch] * decayFactor + sq*(1.0f - decayFactor);
				m_values[ch] = sqrt(volume);
				samples[ch][s] = sqrt(volume);
			}
		}

		GetOutput<0>().Set(inputSampleRate);
		GetOutput<1>().Set(samples);
	}

private:
	std::vector<float> m_values;
};