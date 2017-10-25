#pragma once

#include "Graph_All.hpp"
#include "ConvolutionBuffer.hpp"

#include <vector>
#include <algorithm>
#include <complex>

#include <Mathter/Vector.hpp>

#define NOMINMAX
#include <Windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>


class Visualizer
	// sample rate, fft, wavelet, beats
	: public exc::InputPortConfig<int, std::vector<std::complex<float>>, std::vector<std::vector<float>>, std::vector<std::vector<float>>>,
	public exc::OutputPortConfig<>
{
public:
	Visualizer();
	~Visualizer();

	void Notify(exc::InputPortBase* sender) override {}
	void Update() override;
	bool IsOpen() const { return m_isOpen; }
private:
	struct SpectroColor {
		mathter::Vector<float, 3> min, mid, max;
	};

	static LRESULT __stdcall WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void UpdateDataResources(int historySize, int numFftBins, int numWaveletChannels, int numBeatTracks);
	void CreateShaders();
	void DrawSpectrogram(int numChannels, int numPoints, ID3D11ShaderResourceView* texture, const SpectroColor& spectroColor, const mathter::Vector<float, 3>& lineColor, bool heatmap, bool line, float height, float top, float lineScale);
	float SampleCubic(float* array, int size, float position);
private:
	HWND m_hwnd;
	bool m_isOpen = true;
	int m_width = 640, m_height = 480;
	Microsoft::WRL::ComPtr<ID3D11Device> m_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
	Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_beatTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_beatTextureView;
	std::vector<ConvolutionBuffer> m_beatHistories;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_waveletTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_waveletTextureView;
	std::vector<ConvolutionBuffer> m_waveletHistories;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_fftTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_fftTextureView;

	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psSpectrogram;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vsQuad;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psSimplecolor;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vsSignal;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_psSpectrogramCb;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_vsQuadCb;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_psSimplecolorCb;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_vsSignalCb;

	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_swapRtv;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_swapBuffer;

	int m_numFFtBins = 0, m_numWaveletChannels = 0, m_numBeatTracks = 0;
	int m_spectroHistorySize = 2048;

	bool m_isFullscreen = false;
	DXGI_MODE_DESC m_fsMode;

	struct alignas(16) PsSpectrogramCb {
		alignas(16) mathter::Vector<float, 3, true> min, mid, max;
	};
	struct alignas(16) VsQuadCb {
		mathter::Matrix<float, 3, 4, mathter::eMatrixOrder::FOLLOW_VECTOR, mathter::eMatrixLayout::ROW_MAJOR, true> transform;
	};
	struct alignas(16) PsSimplecolorCb {
		mathter::Vector<float, 3, true> color;
	};
	struct alignas(16) VsSignalCb {
		mathter::Matrix<float, 3, 4, mathter::eMatrixOrder::FOLLOW_VECTOR, mathter::eMatrixLayout::ROW_MAJOR, true> transform;
		uint32_t verticesPerLine;
		uint32_t line;
	};
};