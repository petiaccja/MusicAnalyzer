#include "Node_FFT.hpp"



void FFT::Update() {
	if (!m_fft) {
		SetBinCount(1024, 1024);
	}

	int sampleRate = GetInput<0>().Get();
	std::vector<float> signal = GetInput<1>().Get();
	m_buffer.AddSamples(signal.data(), signal.size());

	float maxFreq = sampleRate / 2.0f;
	int fftSize = m_fft->get_length();

	std::vector<std::complex<float>> fourierTransform(m_fft->get_length()/2);
	std::vector<float> inputBuffer(fftSize);
	std::vector<float> outputBuffer(fftSize);

	// apply window function
	memcpy(inputBuffer.data(), m_buffer.GetSamples(), m_sampleCount * sizeof(float));
	memset(inputBuffer.data() + m_sampleCount, 0, (fftSize - m_sampleCount) * sizeof(float));
	int N = m_sampleCount - 1;
	for (int i = 0; i < m_sampleCount; ++i) {
		float w = 0.54f - 0.46*cos(2.f*3.1415926f*i / N);
		inputBuffer[i] *= w;
	}

	m_fft->do_fft(outputBuffer.data(), inputBuffer.data());

	for (int i = 0; i < fourierTransform.size(); ++i) {
		fourierTransform[i].real(outputBuffer[i]);
		fourierTransform[i].imag(outputBuffer[i + fftSize/2]);
	}

	GetOutput<0>().Set(maxFreq);
	GetOutput<1>().Set(fourierTransform);
}


void FFT::SetBinCount(int sampleCount, int fftBins) {
	if (sampleCount > fftBins) {
		throw std::logic_error("Sample count must be less or equal to bin count.");
	}

	int power = 2;
	while (power < fftBins) {
		power *= 2;
	}
	m_fft = std::make_unique<ffft::FFTReal<float>>( power );
	m_buffer.SetSize(sampleCount);
	m_sampleCount = sampleCount;
}

int FFT::GetBinCount() const {
	return m_fft ? m_fft->get_length() : 1024;
}

int FFT::GetSampleCount() const {
	return m_sampleCount;
}