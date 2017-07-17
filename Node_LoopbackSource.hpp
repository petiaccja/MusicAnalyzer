#pragma once


#include "Graph_All.hpp"

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>


class LoopbackSource 
	: public exc::InputPortConfig<>,
	public exc::OutputPortConfig<std::vector<float>>
{
public:
	LoopbackSource();
	~LoopbackSource();

	void Start(std::string deviceName);
	void Stop();

	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;
protected:
	std::thread m_sourceThread;
	std::mutex m_mtx;
	std::vector<float> m_samples;
};