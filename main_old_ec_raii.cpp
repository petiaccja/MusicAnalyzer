#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include <wrl/client.h>

#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
#include <csignal>
#include <algorithm>
#include <future>

#include <conio.h>

//------------------------------------------------------------------------------
// RAII helpers
//------------------------------------------------------------------------------

template <class Func>
struct ScopeGuard {
public:
	~ScopeGuard() {
		if (enabled) {
			func();
		}
	}
	Func func;
	bool enabled = false;
};

template <class Func>
ScopeGuard<Func> MakeScopeGuard(Func func) {
	return ScopeGuard<Func>{ func, true };
}


using Microsoft::WRL::ComPtr;


//------------------------------------------------------------------------------
// Error codes
//------------------------------------------------------------------------------

enum class eErrorCode {
	SUCCESS = 0,
	UNKNOWN_ERROR,
};


//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------

eErrorCode GetPlaybackDevice(ComPtr<IMMDevice>& result) {
	HRESULT hr = S_OK;
	ComPtr<IMMDeviceEnumerator> enumerator;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&enumerator);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}

	// get the default render endpoint
	ComPtr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}

	result = device;
}



float CalculateVolume(float* samples, int numSamples) {
	double rms = 0;
	for (int i = 0; i < numSamples; ++i) {
		rms += samples[i] * samples[i];
	}
	rms /= numSamples;
	rms = sqrt(rms);
	return (float)rms;
}



eErrorCode CaptureAudio(ComPtr<IMMDevice> device, std::atomic_bool& run) {
	HRESULT hr;

	// activate an audio client on the device
	ComPtr<IAudioClient> audioClient;
	hr = device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&audioClient
	);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}


	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = audioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}

	// get the default device format
	std::unique_ptr<WAVEFORMATEX, void(*)(void*)> waveformat{ nullptr, [](void* p) {CoTaskMemFree(p); } };
	{
		WAVEFORMATEX* tmp;
		hr = audioClient->GetMixFormat(&tmp);
		waveformat.reset(tmp);
		if (FAILED(hr)) {
			return eErrorCode::UNKNOWN_ERROR;
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

	std::cout << "Capturing audio stream: " << waveformat->nSamplesPerSec << " Hz, " << waveformat->nChannels << " channels" << std::endl;


	// initialize the audio client
	// event-based waiting for packets doesn't work with loopback interfaces
	// must do polling
	hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, waveformat.get(), 0);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}

	// activate an audio capture client
	ComPtr<IAudioCaptureClient> captureClient;
	hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}

	// register with MMCSS
	// this is going to give this media application higher thread priority
	DWORD taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristicsA("Audio", &taskIndex);
	if (hTask == NULL) {
		return eErrorCode::UNKNOWN_ERROR;
	}


	// start audio client
	hr = audioClient->Start();
	if (FAILED(hr)) {
		return eErrorCode::UNKNOWN_ERROR;
	}
	struct _StopGuard {
		~_StopGuard() { c->Stop(); }
		IAudioClient* c;
	} _stopGuard{ audioClient.Get() };


	// ugly console hack
	COORD topLeft = { 0, 0 };
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO screen;
	DWORD written;
	GetConsoleScreenBufferInfo(console, &screen);


	// data processing loop
	std::vector<std::vector<float>> samples;
	std::vector<float> volume(waveformat->nChannels, 0.0f);
	samples.resize(waveformat->nChannels);
	long long numFramesCaptured = 0;
	long long numSamplesCaptured = 0;

	while (run) {
		// pop as many packets as possible
		bool hasPacket;
		unsigned packetSize;
		do {
			hr = captureClient->GetNextPacketSize(&packetSize);
			hasPacket = SUCCEEDED(hr) && packetSize > 0;
			if (FAILED(hr)) {
				run = false;
			}

			uint8_t* data;
			uint32_t numFramesToRead;
			DWORD flags;
			if (hasPacket) {
				// get buffer to audio data
				hr = captureClient->GetBuffer(&data, &numFramesToRead, &flags, NULL, NULL);
				if (FAILED(hr)) {
					return eErrorCode::UNKNOWN_ERROR;
				}
				if (numFramesToRead == 0) {
					return eErrorCode::UNKNOWN_ERROR;
				}
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
					captureClient->ReleaseBuffer(numFramesToRead);
					std::this_thread::sleep_for(std::chrono::nanoseconds(100 * hnsDefaultDevicePeriod / 2 + 1));
					continue;
				}
				if (flags != 0 && flags != AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
					return eErrorCode::UNKNOWN_ERROR;
				}

				// read audio data
				int channel = 0;
				float* fData = reinterpret_cast<float*>(data);
				int numChannels = waveformat->nChannels;
				int numSamples = numFramesToRead * waveformat->nBlockAlign / sizeof(float) / numChannels;
				for (auto& channelSamples : samples) {
					channelSamples.resize(numSamples);
				}
				for (int i = 0; i < numSamples; ++i) {
					samples[channel][i] = fData[i*numChannels + channel];
					++channel;
					channel %= waveformat->nChannels;
				}

				// release buffer
				captureClient->ReleaseBuffer(numFramesToRead);
				numFramesCaptured += numFramesToRead;
				numSamplesCaptured += numSamples;

				// calculate volume
				for (int ch = 0; ch < numChannels; ++ch) {
					float volInst = CalculateVolume(samples[ch].data(), numSamples);
					float t = ((float)numSamples / (float)waveformat->nSamplesPerSec) / 0.1f;
					t = 1.0f - std::max(t, 1.0f);
					volume[ch] = t*volume[ch] + (1 - t)*volInst;
				}
			}

		} while (hasPacket);

		// show volume indicators
		FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * waveformat->nChannels, { 0, 1 }, &written);
		SetConsoleCursorPosition(console, { 0, 1 });
		for (int ch = 0; ch < waveformat->nChannels; ++ch) {
			float voldb = 20 * log10(volume[ch]);
			voldb = std::max(-60.f, voldb);
			voldb /= 60.f;
			voldb += 1.0f;

			int numChars = std::min(int(screen.dwSize.X), int(screen.dwSize.X*voldb));
			numChars = std::max(0, numChars);
			FillConsoleOutputCharacterA(console, '#', numChars, { 0, SHORT(1 + ch) }, &written);
		}

		std::this_thread::sleep_for(std::chrono::nanoseconds(100 * hnsDefaultDevicePeriod / 2 + 1));
	}

	std::cout << "Captured " << (double)numSamplesCaptured / waveformat->nSamplesPerSec << " seconds of audio (" << numSamplesCaptured << " samples)." << std::endl;
	std::cout << "numFrames = " << numFramesCaptured << std::endl;
}


std::atomic_bool run = true;
void InterruptSignalHandler(int signal) {
	run = false;
}

int main_ec_raii() {
	eErrorCode error;

	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
		std::cout << "CoInitialize failed." << std::endl;
		return (int)error;
	}
	auto coUninitialize = MakeScopeGuard([] {CoUninitialize(); });

	std::signal(SIGINT, InterruptSignalHandler);

	ComPtr<IMMDevice> device;
	error = GetPlaybackDevice(device);
	if (error != eErrorCode::SUCCESS) {
		std::cout << "Failed to get device." << std::endl;
		return 1;
	}

	std::future<eErrorCode> threadResult;
	std::thread captureThread([&device, &threadResult] {
		std::promise<eErrorCode> threadResultWriter;
		eErrorCode error;
		threadResult = threadResultWriter.get_future();
		
		if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
			threadResultWriter.set_value(eErrorCode::UNKNOWN_ERROR);
			return;
		}
		auto coUninitialize = MakeScopeGuard([] {CoUninitialize(); });

		error = CaptureAudio(device, run);
		if (error != eErrorCode::SUCCESS) {
			threadResultWriter.set_value(eErrorCode::UNKNOWN_ERROR);
			return;
		}
		
		threadResultWriter.set_value(eErrorCode::SUCCESS);
	});

	captureThread.join();
	error = threadResult.get();
	if (error != eErrorCode::SUCCESS) {
		std::cout << "Capture thread failed for unknown reasons at an unknown place." << std::endl;
		return (int)error;
	}
	
	std::cout << "Press any key to exit..." << std::endl;
	_getch();
}
