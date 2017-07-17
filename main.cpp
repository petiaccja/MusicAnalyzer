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
// Exception classes
//------------------------------------------------------------------------------

// We will be using STD classes





//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------

ComPtr<IMMDevice> GetPlaybackDevice() {
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



float CalculateVolume(float* samples, int numSamples) {
	double rms = 0;
	for (int i = 0; i < numSamples; ++i) {
		rms += samples[i] * samples[i];
	}
	rms /= numSamples;
	rms = sqrt(rms);
	return (float)rms;
}



void CaptureAudio(ComPtr<IMMDevice> device, std::atomic_bool& run) {
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


	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = audioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		throw std::runtime_error("IAudioClient::GetDevicePeriod failed: hr = " + std::to_string(hr));
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

	// register with MMCSS
	// this is going to give this media application higher thread priority
	DWORD taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristicsA("Audio", &taskIndex);
	if (hTask == NULL) {
		throw std::runtime_error("AvSetMmThreadCharacteristics failed: last error = " + std::to_string(GetLastError()));
	}


	// start audio client
	hr = audioClient->Start();
	if (FAILED(hr)) {
		throw std::runtime_error("AudioClient failed to start: hr = " + std::to_string(hr));
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
					throw std::runtime_error("Failed to get buffer: hr = " + std::to_string(hr));
				}
				if (numFramesToRead == 0) {
					throw std::runtime_error("GetBuffer gave 0 frames");
				}
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
					captureClient->ReleaseBuffer(numFramesToRead);
					std::this_thread::sleep_for(std::chrono::nanoseconds(100 * hnsDefaultDevicePeriod / 2 + 1));
					continue;
				}
				if (flags != 0 && flags != AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
					throw std::runtime_error("GetBuffer returned invalid flags: flag = " + std::to_string(flags));
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

int main() {
	try {
		if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
			throw std::runtime_error("CoInitialize failed.");
		}
		auto coUninitialize = MakeScopeGuard([]	{CoUninitialize(); });

		std::signal(SIGINT, InterruptSignalHandler);

		ComPtr<IMMDevice> device;
		device = GetPlaybackDevice();

		std::future<bool> threadResult;
		std::thread captureThread([&] {
			std::promise<bool> threadResultWriter;
			threadResult = threadResultWriter.get_future();

			try {
				if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
					throw std::runtime_error("CoInitialize capture thread failed.");
				}
				auto coUninitialize = MakeScopeGuard([] {CoUninitialize(); });

				CaptureAudio(device, run);
				threadResultWriter.set_value(true);
			}
			catch (...) {
				threadResultWriter.set_exception(std::current_exception());
			}
		});

		captureThread.join();
		threadResult.get();
	}
	catch (std::exception& ex) {
		std::cout << "Your code did not work, lel.";
		std::cout << "Inline engine would give you a nice stack trace, but I wont, n00b.";
		std::cout << ex.what() << std::endl;
	}

	std::cout << "Press any key to exit..." << std::endl;
	_getch();
}
