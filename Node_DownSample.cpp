#include "Node_DownSample.hpp"
#include "Convolution.hpp"

#include <Mathter\Utility.hpp>
#include <numeric>

using namespace mathter;
constexpr float Pi = Constants<float>::Pi;


inline void DownSample::Update() {
	int sampleRate = GetInput<0>().Get();
	std::vector<float> inSamples = GetInput<1>().Get();
	int factor = GetInput<2>().Get();
	std::vector<float> workingSet;
	std::vector<float> outSamples;

	if (m_currentSampleRate != sampleRate || m_currentFactor != factor) {
		m_currentSampleRate = sampleRate;
		m_currentFactor = factor;
		RecomputeFilters();
		m_offsetCarry = 0;
	}


	// prepare working set
	workingSet.resize(m_buffer.GetSize() + inSamples.size());
	memcpy(workingSet.data(), m_buffer.GetSamples(), m_buffer.GetSize() * sizeof(float));
	memcpy(workingSet.data() + m_buffer.GetSize(), inSamples.data(), inSamples.size() * sizeof(float));
	m_buffer.AddSamples(inSamples.data(), inSamples.size());

	// apply convolution filter
	std::vector<float> signalBuffer(workingSet.size());
	for (size_t i = m_offsetCarry; i < inSamples.size(); i+=m_currentFactor) {
		size_t dim = m_lpf.size();
		float* signal = workingSet.data() + m_buffer.GetSize() - dim + i;
		memcpy(signalBuffer.data(), signal, sizeof(float)*dim);

		outSamples.push_back(ScalarProduct(m_lpf.data(), signalBuffer.data(), dim));
	}
	m_offsetCarry = (inSamples.size() - m_offsetCarry) % m_currentFactor; // this formula might be totally wrong

	GetOutput<0>().Set(m_currentSampleRate / m_currentFactor);
	GetOutput<1>().Set(outSamples);
}


void DownSample::RecomputeFilters() {
	float nyquistLimit = m_currentSampleRate / 2.0f;
	float newNyquistLimit = nyquistLimit / (float)m_currentFactor;

	float cutoff = newNyquistLimit * 0.85f;

	m_lpf = CreateLPF(m_currentSampleRate, cutoff, 0.002f);
}


std::vector<float> DownSample::CreateLPF(int sampleRate, float cutoff, float length) {
	int numSamples = int(sampleRate*length / 2) * 2 + 1;
	std::vector<float> samples(numSamples);

	for (int i = 0; i < numSamples; ++i) {
		float n = 2.0f*cutoff;
		float t = length*(i - 1) - length / 2;
		float y = sin(Pi*n*t) / (Pi*n*t);
		float w = 0.54f + 0.46f*cos(2.0f*t / length*Pi);
		samples[i] = y*w;
	}
	samples[numSamples / 2 + 1] = 1.0f;

	double sum = 0.0f;
	for (auto v : samples) {
		sum += v;
	}

	float corr = float(1 / sum);
	for (auto& v : samples) {
		v *= corr;
	}

	return samples;
}