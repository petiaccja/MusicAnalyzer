#pragma once

#include "Graph_All.hpp"

#include "ConvolutionBuffer.hpp"


class DownSample
	// sample rate, channel samples, decimation factor
	: public exc::InputPortConfig<int, std::vector<float>, int>,
	// sample rate, channel samples
	public exc::OutputPortConfig<int, std::vector<float>>
{
public:
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;

private:
	void RecomputeFilters();
	static std::vector<float> CreateLPF(int sampleRate, float cutoff, float length);

private:
	ConvolutionBuffer m_buffer;
	std::vector<float> m_lpf;
	int m_currentSampleRate = 1;
	int m_currentFactor = 1;
	int m_offsetCarry = 0;
};