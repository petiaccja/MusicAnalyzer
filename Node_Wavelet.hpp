#pragma once

#include "Graph_All.hpp"

#include "ConvolutionBuffer.hpp"

#include <vector>
#include <complex>



class Wavelet
	// sample rate, samples
	: public exc::InputPortConfig<int, std::vector<float>>,
	// sample rate, wavelet amplitudes
	public exc::OutputPortConfig<int, std::vector<std::vector<float>>>
{
public:
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;
	void SetBands(int numBands, float* frequencies, float* lengths);
private:
	static std::vector<std::complex<float>> MorletWavelet(float frequency, float length, int sampleRate);
	void RecalcWavelets();
	
	struct Band {
		float frequency, length;
	};
private:
	std::vector<std::vector<float>> m_waveletReals;
	std::vector<std::vector<float>> m_waveletImags;
	std::vector<int> m_delays;
	std::vector<Band> m_bands;
	int m_sampleRate = 0;
	ConvolutionBuffer m_buffer;
	std::vector<float> m_workingSet;
};