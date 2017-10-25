#include "Node_Wavelet.hpp"
#include "Convolution.hpp"

#include <complex>
#include <cassert>
#include <algorithm>


void Wavelet::Update() {
	int sampleRate = GetInput<0>().Get();
	if (sampleRate != m_sampleRate) {
		m_sampleRate = sampleRate;
		RecalcWavelets();
	}

	int downsample = 1;
	std::vector<float> samples = GetInput<1>().Get();
	if (samples.size() == 0) {
		GetOutput<0>().Set(sampleRate / downsample);
		GetOutput<1>().Set(std::vector<std::vector<float>>(m_waveletReals.size()));
		return;
	}

	int delay = 0;

	// prepare working set
	m_workingSet.resize(m_buffer.GetSize() + samples.size());
	memcpy(m_workingSet.data(), m_buffer.GetSamples(), m_buffer.GetSize() * sizeof(float));
	memcpy(m_workingSet.data() + m_buffer.GetSize(), samples.data(), samples.size() * sizeof(float));
	m_buffer.AddSamples(samples.data(), samples.size());

	// calculate wavelet coefficients
	std::vector<std::vector<float>> results;
	std::vector<float> signalBuffer(m_workingSet.size());

	int downsampledCount = (samples.size() + downsample - 1) / downsample;
	for (size_t channel = 0; channel < m_waveletReals.size(); ++channel) {
		std::vector<float> result(downsampledCount);

		for (size_t sample = 0; sample < samples.size(); sample += downsample) {
			std::complex<float> c;

			float* w_re = m_waveletReals[channel].data();
			float* w_im = m_waveletImags[channel].data();
			size_t dim = m_waveletReals[channel].size();
			float* signal = m_workingSet.data() + m_delays[channel] + 1 + sample;
			memcpy(signalBuffer.data(), signal, sizeof(float)*dim);

			c.real(ScalarProduct(w_re, signalBuffer.data(), dim));
			c.imag(ScalarProduct(w_im, signalBuffer.data(), dim));

			result[sample / downsample] = std::abs(c);
		}
		results.push_back(result);
	}
	
	GetOutput<0>().Set(sampleRate / downsample);
	GetOutput<1>().Set(results);
}

void Wavelet::SetBands(int numBands, float* frequencies, float* lengths) {
	m_bands.clear();
	for (int i = 0; i < numBands; ++i) {
		m_bands.push_back({ frequencies[i], lengths[i] });
	}
	RecalcWavelets();
}

void Wavelet::RecalcWavelets() {
	m_waveletReals.clear();
	m_waveletImags.clear();

	if (m_sampleRate == 0) {
		m_waveletReals.resize(m_bands.size(), { 1.0f });
		m_waveletImags.resize(m_bands.size(), { 0.0f });
		return;
	}

	size_t maxWaveLen = 0;
	for (int i = 0; i < m_bands.size(); ++i) {
		std::vector<std::complex<float>> coeffs = MorletWavelet(m_bands[i].frequency, m_bands[i].length, m_sampleRate);
		std::vector<float> reals, imags;
		reals.resize(coeffs.size());
		imags.resize(coeffs.size());
		for (size_t j = 0; j < coeffs.size(); ++j) {
			reals[j] = coeffs[j].real();
			imags[j] = coeffs[j].imag();
		}
		m_waveletReals.push_back(reals);
		m_waveletImags.push_back(imags);
		maxWaveLen = std::max(maxWaveLen, m_waveletReals.back().size());
	}

	m_delays.resize(m_bands.size());
	for (int i = 0; i < m_bands.size(); ++i) {
		m_delays[i] = (maxWaveLen - m_waveletReals[i].size()) / 2;
	}

	m_buffer.SetSize(maxWaveLen);
}



std::vector<std::complex<float>> Wavelet::MorletWavelet(float frequency, float length, int sampleRate) {
	using namespace std::literals::complex_literals;
	constexpr float pi = 3.1415926535897932384626f;

	float samplePeriod = 1.0f / sampleRate;

	// calculate the number of taps the wavelet will occupy
	float numTapsDesired = ceil(length / samplePeriod);
	int firstTap = floor(-numTapsDesired / 2.0f);
	int lastTap = -firstTap;
	int numTaps = lastTap - firstTap + 1;
	assert(numTaps > 0);

	std::vector<std::complex<float>> coefficients(numTaps);

	// variance of the bell curve envelope
	float variance = length / 3; // practically zero ends at var = 6;
	float varianceSquared = variance * variance;

	// calculate coefficients
	double sum = 0.0f;
	for (int i = firstTap; i <= lastTap; ++i) {
		float x = samplePeriod * i;
		std::complex<float> c = exp(-x*x / varianceSquared) * exp(1if * (frequency * 2.0f * pi * x));
		sum += abs(c);
		coefficients[i - firstTap] = c;
	}

	// normalize coefficients so that their real part sums up to 1.0
	sum = 2.0 / sum;
	for (auto& c : coefficients) {
		c *= sum;
	}

	return coefficients;
}
