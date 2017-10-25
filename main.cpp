#include "Node_LoopbackSource.hpp"
#include "Node_Volume.hpp"
#include "Node_VolumeDisplay.hpp"
#include "Node_Wavelet.hpp"
#include "Node_SplitStereo.hpp"
#include "Node_BeatFinder.hpp"
#include "Node_DownSample.hpp"
#include "Node_BarDisplay.hpp"
#include "Node_FFT.hpp"

#include "ScopeGuard.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <csignal>
#include "Node_Visualizer.hpp"


using Microsoft::WRL::ComPtr;


std::atomic_bool run = true;
void InterruptSignalHandler(int signal) {
	run = false;
}

int main() {
	try {
		if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
			throw std::runtime_error("CoInitialize failed.");
		}
		auto coUninitialize = MakeScopeGuard([] {CoUninitialize(); });

		std::signal(SIGINT, InterruptSignalHandler);

		LoopbackSource source;
		SplitStereo split;
		Wavelet wavelet;
		BeatFinder beatFinder;
		Volume volume;
		DownSample decimate;
		VolumeDisplay volumeDisplay;
		BarDisplay barDisplay;
		FFT fft;
		Visualizer visualizer;

		//float freqs[] = { 40, 55, 75, 95, 120, 165, 180, 205, 235, 265 };
		//float lengths[] = { 0.12, 0.10, 0.09, 0.08, 0.07, 0.12, 0.10, 0.06, 0.05, 0.05 };
		float freqs[] = { 40, 55, 70, 85, 105, 125, 145, 155, 180, 200, 225, 250, 275, 300 };
		float lengths[] = { 0.12, 0.10, 0.10, 0.10, 0.09, 0.09, 0.09, 0.08, 0.10, 0.10, 0.08, 0.08, 0.08, 0.08 };
		wavelet.SetBands(sizeof(freqs) / 4, freqs, lengths);
		decimate.GetInput<2>().Set(30); // 11 for 4000 Hz
		fft.SetBinCount(4096, 16384);

		
		source.GetOutput(1)->Link(split.GetInput(0));
		source.GetOutput(0)->Link(decimate.GetInput(0));

		split.GetOutput(0)->Link(decimate.GetInput(1));

		decimate.GetOutput(0)->Link(wavelet.GetInput(0));
		decimate.GetOutput(1)->Link(wavelet.GetInput(1));
		decimate.GetOutput(1)->Link(beatFinder.GetInput(1));

		//wavelet.GetOutput(0)->Link(volume.GetInput(0));
		//wavelet.GetOutput(1)->Link(volume.GetInput(1));

		wavelet.GetOutput(0)->Link(beatFinder.GetInput(0));
		wavelet.GetOutput(1)->Link(beatFinder.GetInput(2));

		//volume.GetOutput(0)->Link(volumeDisplay.GetInput(0));
		//volume.GetOutput(1)->Link(volumeDisplay.GetInput(1));

		beatFinder.GetOutput(0)->Link(volumeDisplay.GetInput(0));
		beatFinder.GetOutput(1)->Link(volumeDisplay.GetInput(1));

		beatFinder.GetOutput(0)->Link(barDisplay.GetInput(0));
		beatFinder.GetOutput(1)->Link(barDisplay.GetInput(1));

		beatFinder.GetOutput(0)->Link(visualizer.GetInput(0));
		beatFinder.GetOutput(1)->Link(visualizer.GetInput(3));
		wavelet.GetOutput(1)->Link(visualizer.GetInput(2));

		source.GetOutput(0)->Link(fft.GetInput(0));
		split.GetOutput(1)->Link(fft.GetInput(1));

		fft.GetOutput(1)->Link(visualizer.GetInput(1));


		source.Start("default");

		while (run && visualizer.IsOpen()) {
			auto time = std::chrono::steady_clock::now();
			source.Update();
			split.Update();
			decimate.Update();
			wavelet.Update();
			beatFinder.Update();
			volume.Update();
			fft.Update();
			//volumeDisplay.Update();
			//barDisplay.Update();

			visualizer.Update();

			std::this_thread::sleep_until(time + std::chrono::milliseconds(16));
		}

		source.Stop();
	}
	catch (std::exception& ex) {
		std::cout << "Your code did not work, lel.";
		std::cout << "Inline engine would give you a nice stack trace, but I wont, n00b.";
		std::cout << ex.what() << std::endl;
	}
}
