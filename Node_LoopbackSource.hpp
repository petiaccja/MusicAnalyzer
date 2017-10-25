#pragma once

#include "Graph_All.hpp"

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>


#define NOMINMAX
#include <wrl/client.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>


class LoopbackSource 
	: public exc::InputPortConfig<>,
	// sample rate, channel samples
	public exc::OutputPortConfig<int, std::vector<std::vector<float>>>
{
public:
	LoopbackSource();
	~LoopbackSource();

	void Start(std::string deviceName);
	void Stop();

	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;

protected:
	static Microsoft::WRL::ComPtr<IMMDevice> GetPlaybackDevice(std::string name);
	void InitializeCaptureClient(Microsoft::WRL::ComPtr<IMMDevice> device);
	void ShutdownCaptureClient();
	void CaptureThread(Microsoft::WRL::ComPtr<IMMDevice> device, std::promise<void>&& result);

protected:
	std::thread m_captureThread;
	std::atomic_bool m_runThread;
	std::future<void> m_threadResult;
	std::mutex m_mtx;
	std::vector<std::vector<float>> m_samples;

	Microsoft::WRL::ComPtr<IAudioCaptureClient> m_captureClient;
	Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
	WAVEFORMATEX m_waveformat;
};