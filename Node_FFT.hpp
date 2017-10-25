#pragma once

#include "Graph_All.hpp"
#include "ConvolutionBuffer.hpp"

#include <ffft/FFTReal.h>
#include <complex>
#include <memory>
#include <vector>


class FFT
	// sample rate, channel samples
	: public exc::InputPortConfig<int, std::vector<float>>,
	// max frequency, fourier transform
	public exc::OutputPortConfig<float, std::vector<std::complex<float>>>
{
public:
	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;
	
	void SetBinCount(int sampleCount, int fftBins);
	int GetSampleCount() const;
	int GetBinCount() const;
private:
	ConvolutionBuffer m_buffer;
	std::unique_ptr<ffft::FFTReal<float>> m_fft;
	int m_sampleCount = 1;
};