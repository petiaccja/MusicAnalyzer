#pragma once

#include "Graph_All.hpp"

#include "ConvolutionBuffer.hpp"

#include <vector>
#include <Mathter/Matrix.hpp>


// algorithm:



class BeatFinder
	// sample rate, signal, wavelet
	: public exc::InputPortConfig<int, std::vector<float>, std::vector<std::vector<float>>>,
	// samnple rate, beat probability
	public exc::OutputPortConfig<int, std::vector<std::vector<float>>>
{
	static constexpr int NumKickBands = 8;
	static constexpr int NumSnareBands = 6;
public:
	BeatFinder();
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;
	void ResizeBuffers();

	static float Volume(const float* signal, int numSamples);
	static float Mean(const float* signal, int numSamples);
	static float Variance(const float* signal, float mean, int numSamples);
	static float Variance(const float* signal, int numSamples);
	static float Covariance(const float* signal1, const float* signal2, float mean1, float mean2, int numSamples);
	static float Covariance(const float* signal1, const float* signal2, int numSamples);
private:
	ConvolutionBuffer m_signalBuffer;
	std::vector<ConvolutionBuffer> m_buffers;
	ConvolutionBuffer m_kickBuffer;
	std::vector<float> m_kickFilter;
	int m_sampleRate = 1;
	int m_historySize = 1;
	float m_kickProbabiltiyPrev = 0.0f;
};