#pragma once

#include "Graph_All.hpp"

#include <vector>


class SplitStereo
	// sample rate, channel samples
	: public exc::InputPortConfig<std::vector<std::vector<float>>>,
	// sample rate, channel samples
	public exc::OutputPortConfig<std::vector<float>, std::vector<float>>
{
public:
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override {
		std::vector<std::vector<float>> channels = GetInput<0>().Get();

		std::vector<float> left;
		std::vector<float> right;

		if (channels.size() > 0) {
			left = channels[0];
		}
		if (channels.size() > 1) {
			right = channels[1];
		}
		else {
			right = left;
		}

		GetOutput<0>().Set(left);
		GetOutput<1>().Set(right);		
	}
private:
	std::vector<float> m_values;
};