#include "Node_Visualizer.hpp"


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace mathter;


Visualizer::Visualizer() {
	// Create a window
	WNDCLASSEXA wc;
	wc.cbClsExtra = 0;
	wc.cbSize = sizeof(wc);
	wc.cbWndExtra = 0;
	wc.hCursor = NULL;
	wc.hIcon = NULL;
	wc.hIconSm = NULL;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hbrBackground = NULL;
	wc.lpfnWndProc = &Visualizer::WndProc;
	wc.lpszClassName = "INL_SIMPLE_WINDOW_CLASS";
	wc.lpszMenuName = nullptr;
	wc.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;

	ATOM cres = RegisterClassExA(&wc);
	if (cres == 0) {
		DWORD error = GetLastError();
		throw std::runtime_error("Failed to register window class." + std::to_string(error));
	}

	HWND hwnd = CreateWindowExA(
		0,
		"INL_SIMPLE_WINDOW_CLASS",
		"Visualizer",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		640,
		480,
		NULL,
		NULL,
		GetModuleHandle(NULL),
		(void*)this);

	if (!hwnd) {
		throw std::runtime_error("Failed to create window.");
	}
	m_hwnd = hwnd;
	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);


	// Create directx device
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	RtlSecureZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = 3;
	swapChainDesc.BufferDesc.Width = 640;
	swapChainDesc.BufferDesc.Height = 480;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
	swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.OutputWindow = m_hwnd;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		0,
		&featureLevel,
		1,
		D3D11_SDK_VERSION,
		&swapChainDesc,
		&m_swapChain,
		&m_device,
		nullptr,
		&m_context);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create d3d11 device.");
	}

	ComPtr<ID3D11Texture2D> buf;
	ComPtr<ID3D11RenderTargetView> rtv;

	HRESULT hr1 = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
	HRESULT hr2 = m_device->CreateRenderTargetView(buf.Get(), nullptr, &rtv);
	if (FAILED(hr1) || FAILED(hr2)) {
		throw std::runtime_error("Failed to get back-buffers.");
	}
	m_swapBuffer = buf;
	m_swapRtv = rtv;


	ComPtr<IDXGIOutput> pOutput = NULL;
	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<IDXGIAdapter> dxgiAdapter;
	m_swapChain->GetDevice(IID_PPV_ARGS(&dxgiDevice));
	dxgiDevice->GetAdapter(&dxgiAdapter);
	hr = dxgiAdapter->EnumOutputs(0, &pOutput);
	UINT numModes = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	hr = pOutput->GetDisplayModeList(format, 0, &numModes, NULL);
	std::vector<DXGI_MODE_DESC> displayModes(numModes);
	hr = pOutput->GetDisplayModeList(format, 0, &numModes, displayModes.data());

	for (auto& v : displayModes) {
		std::cout << v.Width << "x" << v.Height << " " << v.RefreshRate.Numerator << "/" << v.RefreshRate.Denominator << "Hz" << std::endl;
	}
	m_fsMode = displayModes.back();


	CreateShaders();
}


Visualizer::~Visualizer() {
	DestroyWindow(m_hwnd);
}




void Visualizer::Update() {
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Get input data
	auto sampleRate = GetInput<0>().Get();
	auto fft = GetInput<1>().Get();
	auto wavelet = GetInput<2>().Get();
	auto beats = GetInput<3>().Get();

	int fftSize = fft.size();


	// Update internal resource to accept input data
	int historySize = 2 * sampleRate;
	UpdateDataResources(historySize, fft.size(), wavelet.size(), beats.size());

	if (m_numBeatTracks == 0 || m_numWaveletChannels == 0 || m_numFFtBins == 0) {
		HRESULT presentHr = m_swapChain->Present(1, 0);
		if (FAILED(presentHr)) {
			throw std::runtime_error("Present failed.");
		}
		return;
	}


	// Copy input data to internal history buffers
	for (int i = 0; i < m_beatHistories.size(); ++i) {
		m_beatHistories[i].AddSamples(beats[i].data(), beats[i].size());
	}
	for (int i = 0; i < m_waveletHistories.size(); ++i) {
		m_waveletHistories[i].AddSamples(wavelet[i].data(), wavelet[i].size());
	}

	// Upload input data to GPU buffers
	{
		D3D11_MAPPED_SUBRESOURCE mapinfo;
		m_context->Map(m_beatTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);

		for (int i = 0; i < m_beatHistories.size(); ++i) {
			void* srcData = m_beatHistories[i].GetSamples();
			UINT srcSize = m_beatHistories[i].GetSize() * sizeof(float);
			memcpy((char*)mapinfo.pData + mapinfo.RowPitch*i, srcData, std::min(srcSize, mapinfo.RowPitch));
		}

		m_context->Unmap(m_beatTexture.Get(), 0);
	}
	{
		D3D11_MAPPED_SUBRESOURCE mapinfo;
		m_context->Map(m_waveletTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);

		for (int i = 0; i < m_waveletHistories.size(); ++i) {
			void* srcData = m_waveletHistories[i].GetSamples();
			UINT srcSize = m_waveletHistories[i].GetSize() * sizeof(float);
			memcpy((char*)mapinfo.pData + mapinfo.RowPitch*i, srcData, std::min(srcSize, mapinfo.RowPitch));
		}

		m_context->Unmap(m_waveletTexture.Get(), 0);
	}
	{
		D3D11_MAPPED_SUBRESOURCE mapinfo;
		m_context->Map(m_fftTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);

		std::vector<float> amplitude(fftSize);
		std::vector<float> graph(fftSize);
		float scaler = 1.0f / fftSize;
		for (int i = 0; i < fftSize; ++i) {
			amplitude[i] = std::abs(fft[i]) * scaler;
		}
		double minx = log(50);
		double maxx = log(22050);
		float* amplitudeData = amplitude.data();
		for (int i = 0; i < fftSize; ++i) {
			double logmyfreq = minx + (maxx - minx)*i / (fftSize - 1);
			float s = exp(logmyfreq) / 22050 * (fftSize - 1);
			float a = SampleCubic(amplitudeData, fftSize, s);
			graph[i] = (20 * log10(a)) / 80 + 1;
		}
		memcpy(mapinfo.pData, graph.data(), graph.size() * sizeof(float));

		m_context->Unmap(m_fftTexture.Get(), 0);
	}

	// Initialize drawing
	{
		D3D11_VIEWPORT viewport;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.Width = m_width;
		viewport.Height = m_height;
		viewport.TopLeftX = viewport.TopLeftY = 0;
		m_context->RSSetViewports(1, &viewport);

		ID3D11RenderTargetView* rtvs = m_swapRtv.Get();
		m_context->OMSetRenderTargets(1, &rtvs, nullptr);

		Vector<float, 4> color = { 0,0,0,1 };
		m_context->ClearRenderTargetView(rtvs, (float*)&color);
	}

	SpectroColor spectroColor;
	spectroColor.min = { 0, 0, 0 };
	spectroColor.mid = { 0, 0.6, 0.6 };
	spectroColor.max = { 1, 0.5, 1 };

	DrawSpectrogram(m_beatHistories.size(), m_beatHistories[0].GetSize(), m_beatTextureView.Get(), spectroColor, { 1, 1, 1 }, true, true, 0.3f, 0.0f, 1.0f);

	spectroColor.min = { 0, 0, 0 };
	spectroColor.mid = { 0.5f, 0.5f, 0.5f };
	spectroColor.max = { 1, 1, 1 };

	DrawSpectrogram(m_waveletHistories.size(), m_waveletHistories[0].GetSize(), m_waveletTextureView.Get(), spectroColor, { 0.4, 0.6, 0.6 }, true, true, 0.5f, 0.3f, 20.0f);

	DrawSpectrogram(1, m_numFFtBins, m_fftTextureView.Get(), spectroColor, { 0.2, 1.0, 1.0 }, false, true, 0.4f, 0.8f, 0.5f);


	HRESULT presentHr = m_swapChain->Present(1, 0);
	if (FAILED(presentHr)) {
		throw std::runtime_error("Present failed.");
	}
}

void Visualizer::DrawSpectrogram(int numChannels, int numPoints, ID3D11ShaderResourceView* texture, const SpectroColor& spectroColor, const Vector<float, 3>& lineColor, bool heatmap, bool line, float height, float top, float lineScale) {
	// Draw history spectrogram
	if (heatmap) {
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_context->VSSetShader(m_vsQuad.Get(), nullptr, 0);
		m_context->PSSetShader(m_psSpectrogram.Get(), nullptr, 0);

		ID3D11ShaderResourceView* views = texture;
		m_context->PSSetShaderResources(0, 1, &views);

		VsQuadCb tr;
		tr.transform.Submatrix<3, 3>(0, 0) = Matrix<float, 3, 3>::Scale(1, height) * Matrix<float, 3, 3>::Translation(0, -2 * top + (1 - height));
		tr.transform.Column(3) = Vector<float, 3>(0);

		PsSpectrogramCb sp;
		sp.min = spectroColor.min;
		sp.mid = spectroColor.mid;
		sp.max = spectroColor.max;

		D3D11_MAPPED_SUBRESOURCE mapinfo;
		m_context->Map(m_vsQuadCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);
		memcpy(mapinfo.pData, &tr, sizeof(tr));
		m_context->Unmap(m_vsQuadCb.Get(), 0);

		m_context->Map(m_psSpectrogramCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);
		memcpy(mapinfo.pData, &sp, sizeof(sp));
		m_context->Unmap(m_psSpectrogramCb.Get(), 0);

		ID3D11Buffer* cbuffer;
		cbuffer = m_vsQuadCb.Get();
		m_context->VSSetConstantBuffers(0, 1, &cbuffer);
		cbuffer = m_psSpectrogramCb.Get();
		m_context->PSSetConstantBuffers(0, 1, &cbuffer);

		m_context->Draw(4, 0);
	}

	// Draw history signal view
	if (line) {
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		m_context->VSSetShader(m_vsSignal.Get(), nullptr, 0);
		m_context->PSSetShader(m_psSimplecolor.Get(), nullptr, 0);

		ID3D11ShaderResourceView* views = texture;
		m_context->VSSetShaderResources(0, 1, &views);

		VsSignalCb vscb;
		vscb.transform.Column(3) = Vector<float, 3>(0);
		vscb.verticesPerLine = numPoints;

		PsSimplecolorCb pscb;
		pscb.color = lineColor;

		D3D11_MAPPED_SUBRESOURCE mapinfo;
		m_context->Map(m_psSimplecolorCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);
		memcpy(mapinfo.pData, &pscb, sizeof(pscb));
		m_context->Unmap(m_psSimplecolorCb.Get(), 0);

		ID3D11Buffer* cbuffer;
		cbuffer = m_vsSignalCb.Get();
		m_context->VSSetConstantBuffers(0, 1, &cbuffer);
		cbuffer = m_psSimplecolorCb.Get();
		m_context->PSSetConstantBuffers(0, 1, &cbuffer);

		for (int i = 0; i < numChannels; ++i) {
			vscb.line = i;
			vscb.transform.Submatrix<3, 3>(0, 0) = Matrix<float, 3, 3>::Scale(1, lineScale * height / numChannels) * Matrix<float, 3, 3>::Translation(0, 1 - 2 * top - 2 * (i + 0.5f)*height / numChannels);

			m_context->Map(m_vsSignalCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapinfo);
			memcpy(mapinfo.pData, &vscb, sizeof(vscb));
			m_context->Unmap(m_vsSignalCb.Get(), 0);

			cbuffer = m_vsSignalCb.Get();
			m_context->VSSetConstantBuffers(0, 1, &cbuffer);

			m_context->Draw(vscb.verticesPerLine, 0);
		}
	}
}



void Visualizer::UpdateDataResources(int historySize, int numFftBins, int numWaveletChannels, int numBeatTracks) {
	if (historySize != m_spectroHistorySize) {
		m_numBeatTracks = m_numWaveletChannels = m_numFFtBins = 0;
		m_spectroHistorySize = historySize;
	}

	if (m_numBeatTracks != numBeatTracks) {
		D3D11_TEXTURE2D_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.SampleDesc.Count = 1;
		desc.ArraySize = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.MipLevels = 1;
		desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.Width = m_spectroHistorySize;
		desc.Height = numBeatTracks;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
		memset(&viewDesc, 0, sizeof(viewDesc));
		viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipLevels = 1;
		viewDesc.Texture2D.MostDetailedMip = 0;

		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11ShaderResourceView> view;
		HRESULT hr1 = m_device->CreateTexture2D(&desc, nullptr, &tex);
		HRESULT hr2 = m_device->CreateShaderResourceView(tex.Get(), &viewDesc, &view);

		if (SUCCEEDED(hr1) && SUCCEEDED(hr2)) {
			m_numBeatTracks = numBeatTracks;
			m_beatTexture = tex;
			m_beatTextureView = view;
		}
		else {
			throw std::runtime_error("Failed to create textures and views.");
		}
	}
	if (m_beatHistories.size() != numBeatTracks) {
		m_beatHistories.resize(numBeatTracks);
		for (auto& v : m_beatHistories) {
			v.SetSize(m_spectroHistorySize);
		}
	}


	if (m_numWaveletChannels != numWaveletChannels) {
		D3D11_TEXTURE2D_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.SampleDesc.Count = 1;
		desc.ArraySize = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.MipLevels = 1;
		desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.Width = m_spectroHistorySize;
		desc.Height = numWaveletChannels;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
		memset(&viewDesc, 0, sizeof(viewDesc));
		viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipLevels = 1;
		viewDesc.Texture2D.MostDetailedMip = 0;

		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11ShaderResourceView> view;
		HRESULT hr1 = m_device->CreateTexture2D(&desc, nullptr, &tex);
		HRESULT hr2 = m_device->CreateShaderResourceView(tex.Get(), &viewDesc, &view);

		if (SUCCEEDED(hr1) && SUCCEEDED(hr2)) {
			m_numWaveletChannels = numWaveletChannels;
			m_waveletTexture = tex;
			m_waveletTextureView = view;
		}
		else {
			throw std::runtime_error("Failed to create textures and views.");
		}
	}
	if (m_waveletHistories.size() != numWaveletChannels) {
		m_waveletHistories.resize(numWaveletChannels);
		for (auto& v : m_waveletHistories) {
			v.SetSize(m_spectroHistorySize);
		}
	}

	if (m_numFFtBins != numFftBins) {
		D3D11_TEXTURE2D_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.SampleDesc.Count = 1;
		desc.ArraySize = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.MipLevels = 1;
		desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.Width = numFftBins;
		desc.Height = 1;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
		memset(&viewDesc, 0, sizeof(viewDesc));
		viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipLevels = 1;
		viewDesc.Texture2D.MostDetailedMip = 0;

		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11ShaderResourceView> view;
		HRESULT hr1 = m_device->CreateTexture2D(&desc, nullptr, &tex);
		HRESULT hr2 = m_device->CreateShaderResourceView(tex.Get(), &viewDesc, &view);

		if (SUCCEEDED(hr1) && SUCCEEDED(hr2)) {
			m_numFFtBins = numFftBins;
			m_fftTexture = tex;
			m_fftTextureView = view;
		}
		else {
			throw std::runtime_error("Failed to create textures and views.");
		}
	}
}



LRESULT __stdcall Visualizer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	Visualizer& instance = *reinterpret_cast<Visualizer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	switch (msg) {
		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;
		case WM_DESTROY:
			instance.m_isOpen = false;
			PostQuitMessage(0);
			return 0;
		case WM_SIZE: {
			if (!instance.m_swapChain) {
				return 0;
			}

			if (!instance.m_isFullscreen) {
				RECT rc;
				GetClientRect(hwnd, &rc);
				instance.m_width = rc.right - rc.left;
				instance.m_height = rc.bottom - rc.top;
			}
			else {
				instance.m_width = instance.m_fsMode.Width;
				instance.m_height = instance.m_fsMode.Height;
				instance.m_swapChain->ResizeTarget(&instance.m_fsMode);
			}
			std::cout << "resize: " << instance.m_width << "x" << instance.m_height << std::endl;

			instance.m_swapBuffer.Reset();
			instance.m_swapRtv.Reset();

			instance.m_swapChain->ResizeBuffers(3, instance.m_width, instance.m_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

			ComPtr<ID3D11Texture2D> buf;
			ComPtr<ID3D11RenderTargetView> rtv;

			HRESULT hr1 = instance.m_swapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
			HRESULT hr2 = instance.m_device->CreateRenderTargetView(buf.Get(), nullptr, &rtv);
			if (FAILED(hr1) || FAILED(hr2)) {
				throw std::runtime_error("Failed to get back-buffers.");
			}
			instance.m_swapBuffer = buf;
			instance.m_swapRtv = rtv;


			return 0;
		}
		case WM_KEYUP:
			if (wParam == VK_RETURN) {
				if (!instance.m_swapChain) {
					return 0;
				}

				instance.m_isFullscreen = !instance.m_isFullscreen;
				if (!instance.m_isFullscreen) {
					RECT rc;
					GetClientRect(hwnd, &rc);
					instance.m_width = rc.right - rc.left;
					instance.m_height = rc.bottom - rc.top;
					instance.m_swapChain->SetFullscreenState(false, nullptr);
				}
				else {
					instance.m_swapChain->SetFullscreenState(true, nullptr);
				}
			}
			return 0;
		case WM_NCCREATE:
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}


#include "Shaders/PsSpectrogram.h"
#include "Shaders/VsQuad.h"
#include "Shaders/PsSimplecolor.h"
#include "Shaders/VsSignal.h"

void Visualizer::CreateShaders() {
	if (FAILED(m_device->CreatePixelShader(PsSpectrogram, sizeof(PsSpectrogram), nullptr, &m_psSpectrogram))) {
		throw std::runtime_error("Failed to create shader.");
	}
	if (FAILED(m_device->CreateVertexShader(VsQuad, sizeof(VsQuad), nullptr, &m_vsQuad))) {
		throw std::runtime_error("Failed to create shader.");
	}
	if (FAILED(m_device->CreatePixelShader(PsSimplecolor, sizeof(PsSimplecolor), nullptr, &m_psSimplecolor))) {
		throw std::runtime_error("Failed to create shader.");
	}
	if (FAILED(m_device->CreateVertexShader(VsSignal, sizeof(VsSignal), nullptr, &m_vsSignal))) {
		throw std::runtime_error("Failed to create shader.");
	}


	D3D11_BUFFER_DESC desc;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.ByteWidth = sizeof(VsQuadCb);
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_vsQuadCb))) {
		throw std::runtime_error("Failed to create vs quad cb.");
	}

	desc.ByteWidth = sizeof(PsSpectrogramCb);
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_psSpectrogramCb))) {
		throw std::runtime_error("Failed to create ps spectro cb.");
	}

	desc.ByteWidth = sizeof(PsSimplecolorCb);
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_psSimplecolorCb))) {
		throw std::runtime_error("Failed to create ps spectro cb.");
	}

	desc.ByteWidth = sizeof(VsSignalCb);
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_vsSignalCb))) {
		throw std::runtime_error("Failed to create ps spectro cb.");
	}
}



float Visualizer::SampleCubic(float* array, int size, float position) {
	float p[4];
	int baseIndex = int(position);
	float x = position - baseIndex;
	--baseIndex;
	for (int i = 0; i < 4; ++i) {
		p[i] = array[std::max(0, std::min(size, baseIndex + i))];
	}

	return p[1] + 0.5 * x*(p[2] - p[0] + x*(2.0*p[0] - 5.0*p[1] + 4.0*p[2] - p[3] + x*(3.0*(p[1] - p[2]) + p[3] - p[0])));
}