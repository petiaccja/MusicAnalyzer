#include "Node_BeatFinder.hpp"

using namespace mathter;

BeatFinder::BeatFinder() {
	m_buffers.resize(NumKickBands + NumSnareBands);
}


void BeatFinder::Update() {
	int sampleRate = GetInput<0>().Get();
	auto signal = GetInput<1>().Get();
	auto wavelet = GetInput<2>().Get();
	int downsample = 1;

	if (sampleRate != m_sampleRate) {
		m_sampleRate = sampleRate;
		ResizeBuffers();
	}
	if (wavelet.size() == 0) {
		GetOutput<0>().Set(sampleRate / downsample);
		return;
	}
	if (wavelet.size() != NumKickBands + NumSnareBands) {
		throw std::logic_error("Wavelet band count does not match with expected band counts.");
	}
	
	// Create working sets
	std::vector<float> signalSet;
	std::vector<std::vector<float>> workingSet(m_buffers.size());

	signalSet.resize(m_signalBuffer.GetSize() + signal.size());
	memcpy(signalSet.data(), m_signalBuffer.GetSamples(), m_signalBuffer.GetSize() * sizeof(float));
	memcpy(signalSet.data() + m_signalBuffer.GetSize(), signal.data(), signal.size() * sizeof(float));
	m_signalBuffer.AddSamples(signal.data(), signal.size());

	for (int i = 0; i < m_buffers.size(); ++i) {
		workingSet[i].resize(m_buffers[i].GetSize() + wavelet[i].size());
		memcpy(workingSet[i].data(), m_buffers[i].GetSamples(), m_buffers[i].GetSize() * sizeof(float));
		memcpy(workingSet[i].data() + m_buffers[i].GetSize(), wavelet[i].data(), wavelet[i].size() * sizeof(float));
		m_buffers[i].AddSamples(wavelet[i].data(), wavelet[i].size());
	}
	if (signal.size() != wavelet[0].size()) {
		__debugbreak();
	}
	int numSamples = signal.size();


	// Helper function for statistical stuff
	auto CalcMeanKick = [&](int numPoints, int sample) -> Vector<float, NumKickBands> {
		Vector<float, NumKickBands> ret;
		for (int i = 0; i < NumKickBands; ++i) {
			float* psignal = &workingSet[i].back() - numSamples - numPoints + sample + 1;
			ret[i] = Mean(psignal, numPoints);
		}
		return ret;
	};
	auto CalcCovmatKick = [&](int numPoints, int sample, const Vector<float, NumKickBands>& means) -> Matrix<float, NumKickBands, NumKickBands> {
		Matrix<float, NumKickBands, NumKickBands> ret;
		for (int i = 0; i < NumKickBands; ++i) {
			for (int j = 0; j < NumKickBands; ++j) {
				float* psignal1 = &workingSet[i].back() - numSamples - numPoints + sample + 1;
				float* psignal2 = &workingSet[j].back() - numSamples - numPoints + sample + 1;
				ret(i, j) = Covariance(psignal1, psignal2, means[i], means[j], numPoints);
			}
		}
		return ret;
	};


	std::vector<std::vector<float>> output(2);

	// Loop through each input sample
	static float kickProbabilityPrev = 0;
	for (int sample = 0; sample < signal.size(); sample += downsample) {
		// IDEA:
		// try to least-squares fit a second degree polynomial to the kick wavelet tracks
		// kick beat spectrum looks like this:
		//   xxxxx
		// xx     xxxxxx
		//              xxxxxxx
		//
		// - polynomial fits well
		// bass guitar looks like this
		//   xx       x
		//  x  x     x x
		// x    x   x  x
		// x     xxx    xxxxxxxxx
		// - huge holes under polynomial

		float volume;

		// calculate volume
		float* psignal = signalSet.data() + sample + 10;
		volume = Volume(psignal, m_sampleRate);
		

		// calculate probability
		float kickProbability;
		float snareProbability = 0.0f;

		auto kickMeanLong = CalcMeanKick(m_sampleRate * 2, sample);
		auto kickMean = CalcMeanKick(m_sampleRate * 1.5, sample);
		auto kickCov = CalcCovmatKick(m_sampleRate * 0.07, sample, kickMean);
		auto kickMeanShort = CalcMeanKick(m_sampleRate * 0.05, sample);
		auto kickVol = 0.0f;
		for (auto v : kickMeanShort) {
			kickVol += v;
		}

		auto kickCovMod = kickCov;
		for (int i = 0; i < kickCovMod.RowCount(); ++i) {
			kickCovMod(i, i) = 0.0f;
		}
		kickProbability = kickCovMod.Norm() * 100 * kickVol;
		float derivative = (kickProbability - kickProbabilityPrev)*m_sampleRate;
		kickProbabilityPrev = kickProbability;

		output[0].push_back(derivative / 20);
		output[1].push_back(kickProbability);
	}

	GetOutput<0>().Set(sampleRate / downsample);
	GetOutput<1>().Set(output);
}




void BeatFinder::ResizeBuffers() {
	m_historySize = m_sampleRate * 2;

	for (auto& buffer : m_buffers) {
		buffer.SetSize(m_historySize - 1);
	}
	m_signalBuffer.SetSize(m_historySize - 1);
}


float BeatFinder::Volume(const float* signal, int numSamples) {
	float volume = 0.0f;
	for (int i = 0; i < numSamples; ++i) {
		volume += signal[i] * signal[i];
	}
	volume = sqrt(volume / numSamples);
	return volume;
}

float BeatFinder::Mean(const float* signal, int numSamples) {
	double sum = 0.0;
	for (int i = 0; i < numSamples; ++i) {
		sum += signal[i];
	}
	return float(sum / numSamples);
}

float BeatFinder::Variance(const float* signal, float mean, int numSamples) {
	double sum = 0.0;
	for (int i = 0; i < numSamples; ++i) {
		float d = signal[i] - mean;
		sum += d*d;
	}
	return float(sum / numSamples);
}

float BeatFinder::Variance(const float* signal, int numSamples) {
	return Variance(signal, Mean(signal, numSamples), numSamples);
}

float BeatFinder::Covariance(const float* signal1, const float* signal2, float mean1, float mean2, int numSamples) {
	double sum = 0.0;
	for (int i = 0; i < numSamples; ++i) {
		sum += signal1[i] * signal2[i];
	}
	float Exy = float(sum / numSamples);
	float ExEy = mean1*mean2;
	return Exy - ExEy;
}

float BeatFinder::Covariance(const float* signal1, const float* signal2, int numSamples) {
	return Covariance(signal1, signal2, Mean(signal1, numSamples), Mean(signal2, numSamples), numSamples);
}