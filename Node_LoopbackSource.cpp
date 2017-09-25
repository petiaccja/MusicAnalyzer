#include "Node_LoopbackSource.hpp"
#include "ScopeGuard.hpp"

#include <iostream>


using Microsoft::WRL::ComPtr;


LoopbackSource::LoopbackSource() {

}

LoopbackSource::~LoopbackSource() {

}

void LoopbackSource::Start(std::string deviceName) {
	ComPtr<IMMDevice> device = GetPlaybackDevice(deviceName);
	InitializeCaptureClient(device);

	std::promise<void> result;
	m_threadResult = result.get_future();

	if (!m_captureThread.joinable()) {
		m_runThread = true;
		m_captureThread = std::thread([this, device, result = std::move(result)]() mutable {
			try {
				CaptureThread(device, std::move(result));
				result.set_value();
			}
			catch (...) {
				result.set_exception(std::current_exception());
			}
		});
	}
	else {
		throw std::logic_error("You can't start capturing twice, n00b.");
	}
}
void LoopbackSource::Stop() {
	if (m_captureThread.joinable()) {
		m_runThread = false;
		m_captureThread.join();
		m_threadResult.get();
	}
}


void LoopbackSource::Update() {
	if (m_runThread) {
		GetOutput<0>().Set((int)m_waveformat.nSamplesPerSec);
	}
	else {
		GetOutput<0>().Set(0);
	}

	std::lock_guard<std::mutex> lkg(m_mtx);
	GetOutput<1>().Set(m_samples);

	for (auto& ch : m_samples) {
		ch.clear();
	}
}


// name is ignored at the moment
Microsoft::WRL::ComPtr<IMMDevice> LoopbackSource::GetPlaybackDevice(std::string name) {
	HRESULT hr = S_OK;
	ComPtr<IMMDeviceEnumerator> enumerator;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&enumerator);
	if (FAILED(hr)) {
		throw std::runtime_error("CoCreateInstance(IMMDeviceEnumerator) failed: hr = " + std::to_string(hr));
	}

	// get the default render endpoint
	ComPtr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(hr)) {
		throw std::runtime_error("IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: hr = " + std::to_string(hr));
	}

	return device;
}



void LoopbackSource::InitializeCaptureClient(Microsoft::WRL::ComPtr<IMMDevice> device) {
	HRESULT hr;

	// activate an audio client on the device
	ComPtr<IAudioClient> audioClient;
	hr = device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&audioClient
	);
	if (FAILED(hr)) {
		throw std::runtime_error("IMMDevice::Activate(IAudioClient) failed: hr = " + std::to_string(hr));
	}


	// get the default device format
	std::unique_ptr<WAVEFORMATEX, void(*)(void*)> waveformat{ nullptr, [](void* p) {CoTaskMemFree(p); } };
	{
		WAVEFORMATEX* tmp;
		hr = audioClient->GetMixFormat(&tmp);
		waveformat.reset(tmp);
		if (FAILED(hr)) {
			throw std::runtime_error("IAudioClient::GetMixFormat failed: hr = " + std::to_string(hr));
		}
	}
	if (waveformat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		auto* waveformatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(waveformat.get());
		waveformatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		waveformatExtensible->Samples.wValidBitsPerSample = 32;
		waveformat->wBitsPerSample = 32;
		waveformat->nBlockAlign = waveformat->nChannels * waveformat->wBitsPerSample / 8;
		waveformat->nAvgBytesPerSec = waveformat->nBlockAlign * waveformat->nSamplesPerSec;
	}
	else {
		waveformat->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		waveformat->wBitsPerSample = 32;
		waveformat->nBlockAlign = waveformat->nChannels * waveformat->wBitsPerSample / 8;
		waveformat->nAvgBytesPerSec = waveformat->nBlockAlign * waveformat->nSamplesPerSec;
	}
	m_waveformat = *waveformat;

	std::cout << "Capturing audio stream: " << waveformat->nSamplesPerSec << " Hz, " << waveformat->nChannels << " channels" << std::endl;


	// initialize the audio client
	// event-based waiting for packets doesn't work with loopback interfaces
	// must do polling
	hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, waveformat.get(), 0);
	if (FAILED(hr)) {
		throw std::runtime_error("IAudioClient::Initialize failed: hr = " + std::to_string(hr));
	}

	// activate an audio capture client
	ComPtr<IAudioCaptureClient> captureClient;
	hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
	if (FAILED(hr)) {
		throw std::runtime_error("IAudioClient::GetService(IAudioCaptureClient) failed: hr = " + std::to_string(hr));
	}

	m_audioClient = audioClient;
	m_captureClient = captureClient;
	m_samples.clear();
	m_samples.resize(m_waveformat.nChannels);
}



void LoopbackSource::ShutdownCaptureClient() {
	m_audioClient.Reset();
	m_captureClient.Reset();
}



void LoopbackSource::CaptureThread(Microsoft::WRL::ComPtr<IMMDevice> device, std::promise<void>&& result) {
	HRESULT hr;

	// Init COM
	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
		throw std::runtime_error("CoInitialize failed.");
	}
	auto coUninitialize = MakeScopeGuard([] {CoUninitialize(); });

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = m_audioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		throw std::runtime_error("IAudioClient::GetDevicePeriod failed: hr = " + std::to_string(hr));
	}

	// start audio client
	hr = m_audioClient->Start();
	if (FAILED(hr)) {
		throw std::runtime_error("AudioClient failed to start: hr = " + std::to_string(hr));
	}
	struct _StopGuard {
		~_StopGuard() { c->Stop(); }
		IAudioClient* c;
	} _stopGuard{ m_audioClient.Get() };


	// data processing loop
	std::vector<std::vector<float>> samples;
	std::vector<float> volume(m_waveformat.nChannels, 0.0f);
	samples.resize(m_waveformat.nChannels);
	long long numFramesCaptured = 0;
	long long numSamplesCaptured = 0;

	while (m_runThread) {
		// pop as many packets as possible
		bool hasPacket;
		unsigned packetSize;
		do {
			hr = m_captureClient->GetNextPacketSize(&packetSize);
			hasPacket = SUCCEEDED(hr) && packetSize > 0;
			if (FAILED(hr)) {
				throw std::runtime_error("Failed to GetNextPacketSize.");
			}

			uint8_t* data;
			uint32_t numFramesToRead;
			DWORD flags;
			if (hasPacket) {
				// get buffer to audio data
				hr = m_captureClient->GetBuffer(&data, &numFramesToRead, &flags, NULL, NULL);
				if (FAILED(hr)) {
					throw std::runtime_error("Failed to get buffer: hr = " + std::to_string(hr));
				}
				if (numFramesToRead == 0) {
					throw std::runtime_error("GetBuffer gave 0 frames");
				}
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
					m_captureClient->ReleaseBuffer(numFramesToRead);
					std::this_thread::sleep_for(std::chrono::nanoseconds(100 * hnsDefaultDevicePeriod / 2 + 1));
					continue;
				}
				if (flags != 0 && flags != AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
					throw std::runtime_error("GetBuffer returned invalid flags: flag = " + std::to_string(flags));
				}

				// read audio data
				int channel = 0;
				float* fData = reinterpret_cast<float*>(data);
				int numChannels = m_waveformat.nChannels;
				int numSamples = numFramesToRead * m_waveformat.nBlockAlign / sizeof(float) / numChannels;
				for (auto& channelSamples : samples) {
					channelSamples.resize(numSamples);
				}
				for (int i = 0; i < numSamples; ++i) {
					for (int ch = 0; ch < numChannels; ++ch) {
						samples[ch][i] = fData[i*numChannels + ch];
					}
					//samples[channel][i/numChannels] = fData[i + channel];
					//++channel;
					//channel %= m_waveformat.nChannels;
				}

				// release buffer
				m_captureClient->ReleaseBuffer(numFramesToRead);
				numFramesCaptured += numFramesToRead;
				numSamplesCaptured += numSamples;

				// write data to internal buffers
				std::lock_guard<std::mutex> lkg(m_mtx);
				if (m_samples[0].size() < 1048576) {
					for (int i = 0; i < m_samples.size(); ++i) {
						m_samples[i].insert(m_samples[i].end(), samples[i].begin(), samples[i].end());
					}
				}
			}
		} while (hasPacket);


		std::this_thread::sleep_for(std::chrono::nanoseconds(100 * hnsDefaultDevicePeriod / 2 + 1));
	}

	std::cout << "Captured " << (double)numSamplesCaptured / m_waveformat.nSamplesPerSec << " seconds of audio (" << numSamplesCaptured << " samples)." << std::endl;
	std::cout << "numFrames = " << numFramesCaptured << std::endl;
}