#include "stdafx.h"
#include "WinDesktopDup.h"

WinDesktopDup::~WinDesktopDup() {
	Close();
}

Error WinDesktopDup::Initialize() {
	// Get desktop
	HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
	if (!hDesk)
		return "Failed to open desktop";

	// Attach desktop to this thread (presumably for cases where this is not the main/UI thread)
	bool deskAttached = SetThreadDesktop(hDesk) != 0;
	CloseDesktop(hDesk);
	hDesk = nullptr;
	if (!deskAttached)
		return "Failed to attach recording thread to desktop";

	// Initialize DirectX
	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE driverTypes[] = {
	    D3D_DRIVER_TYPE_HARDWARE,
	    D3D_DRIVER_TYPE_WARP,
	    D3D_DRIVER_TYPE_REFERENCE,
	};
	auto numDriverTypes = ARRAYSIZE(driverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL featureLevels[] = {
	    D3D_FEATURE_LEVEL_11_0,
	    D3D_FEATURE_LEVEL_10_1,
	    D3D_FEATURE_LEVEL_10_0,
	    D3D_FEATURE_LEVEL_9_1};
	auto numFeatureLevels = ARRAYSIZE(featureLevels);

	D3D_FEATURE_LEVEL featureLevel;

	// Create device
	for (size_t i = 0; i < numDriverTypes; i++) {
		hr = D3D11CreateDevice(nullptr, driverTypes[i], nullptr, 0, featureLevels, (UINT) numFeatureLevels,
		                       D3D11_SDK_VERSION, &D3DDevice, &featureLevel, &D3DDeviceContext);
		if (SUCCEEDED(hr))
			break;
	}
	if (FAILED(hr))
		return tsf::fmt("D3D11CreateDevice failed: %v", hr);

	// Get DXGI device
	IDXGIDevice* dxgiDevice = nullptr;
	hr = D3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**) &dxgiDevice);
	if (FAILED(hr))
		return tsf::fmt("D3DDevice->QueryInterface failed: %v", hr);

	// Get DXGI adapter
	IDXGIAdapter* dxgiAdapter = nullptr;
	hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**) &dxgiAdapter);
	dxgiDevice->Release();
	dxgiDevice = nullptr;
	if (FAILED(hr)) {
		return tsf::fmt("dxgiDevice->GetParent failed: %v", hr);
	}

	// Get output
	IDXGIOutput* dxgiOutput = nullptr;
	hr = dxgiAdapter->EnumOutputs(OutputNumber, &dxgiOutput);
	dxgiAdapter->Release();
	dxgiAdapter = nullptr;
	if (FAILED(hr)) {
		return tsf::fmt("dxgiAdapter->EnumOutputs failed: %v", hr);
	}

	// QI for Output 1
	IDXGIOutput1* dxgiOutput1 = nullptr;
	hr = dxgiOutput->QueryInterface(__uuidof(dxgiOutput1), (void**) &dxgiOutput1);
	dxgiOutput->Release();
	dxgiOutput = nullptr;
	if (FAILED(hr))
		return tsf::fmt("dxgiOutput->QueryInterface failed: %v", hr);

	// Create desktop duplication
	hr = dxgiOutput1->DuplicateOutput(D3DDevice, &DeskDupl);
	dxgiOutput1->Release();
	dxgiOutput1 = nullptr;
	if (FAILED(hr)) {
		if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
			return "Too many desktop recorders already active";
		}
		return tsf::fmt("DuplicateOutput failed: %v", hr);
	}

	return "";
}

void WinDesktopDup::Close() {
	if (DeskDupl)
		DeskDupl->Release();

	if (D3DDeviceContext)
		D3DDeviceContext->Release();

	if (D3DDevice)
		D3DDevice->Release();

	DeskDupl         = nullptr;
	D3DDeviceContext = nullptr;
	D3DDevice        = nullptr;
	HaveFrameLock    = false;
}

void WinDesktopDup::Reset() {
	OutputDebugStringA("LOST IDXGIOutputDuplication, resetting...");
	// shutdown and reinitialize
	Close();
	auto err = Initialize();
	if (err != "") {
		throw std::runtime_error(err);
	}
}

void WinDesktopDup::CaptureNext() {
	if (!DeskDupl)
		return;

	HRESULT hr;

	// according to the docs, it's best for performance if we hang onto the frame for as long as possible,
	// and only release the previous frame immediately before acquiring the next one. Something about
	// the OS coalescing updates, so that it doesn't have to store them as distinct things.
	if (HaveFrameLock) {
		HaveFrameLock = false;
		hr = DeskDupl->ReleaseFrame();
		if (hr == DXGI_ERROR_ACCESS_LOST) {
			Reset();
			return;
		}
		else if (FAILED(hr)) {
			throw std::runtime_error(tsf::fmt("ReleaseFrame error, code: %d\n", hr));
		}
	}

	IDXGIResource *deskRes = nullptr;
	DXGI_OUTDUPL_FRAME_INFO frameInfo;
	hr = DeskDupl->AcquireNextFrame(0, &frameInfo, &deskRes);
	if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		OutputDebugStringA("Nothing changed, happy\n");
		return;
	}
	if (hr == DXGI_ERROR_ACCESS_LOST) {
		Reset();
		return;
	}
	else if (FAILED(hr)) {
		throw std::runtime_error(tsf::fmt("AcquireNextFrame error, code: %d\n", hr));
	}

	HaveFrameLock = true;

	DXGI_OUTDUPL_DESC deskDuplDesc;
	DeskDupl->GetDesc(&deskDuplDesc);
	if (deskDuplDesc.ModeDesc.Width != 2736 || deskDuplDesc.ModeDesc.Height != 1824)
	//if (deskDuplDesc.ModeDesc.Width != 1920 || deskDuplDesc.ModeDesc.Height != 1080)
		throw std::runtime_error("NotImplementError: Currently only support server resolution 1920x1080");
	if (Latest.Width != deskDuplDesc.ModeDesc.Width || Latest.Height != deskDuplDesc.ModeDesc.Height) {
		Latest.Width = deskDuplDesc.ModeDesc.Width;
		Latest.Height = deskDuplDesc.ModeDesc.Height;
		Latest.Buf.resize((long long)Latest.Width * Latest.Height * 4);

		OutputDebugStringA(tsf::fmt("Resize Latest Bitmap to (%d, %d)\n", Latest.Width, Latest.Height).c_str());
	}

	// the desktop image was updated
	if (frameInfo.LastPresentTime.QuadPart != 0) {
		// image already in system memory
		if (deskDuplDesc.DesktopImageInSystemMemory) {
			DXGI_MAPPED_RECT mappedRect;
			DeskDupl->MapDesktopSurface(&mappedRect);
			if (hr == DXGI_ERROR_ACCESS_LOST) {
				Reset();
				return;
			}
			else if (FAILED(hr)) {
				throw std::runtime_error(tsf::fmt("MapDesktopSurface error, code: %d\n", hr));
			}

			for (int y = 0; y < Latest.Height; y++)
				memcpy(Latest.Buf.data() + (long long)y * Latest.Width * 4, (uint8_t*)mappedRect.pBits + (long long)mappedRect.Pitch * y, (long long)Latest.Width * 4);

			DeskDupl->UnMapDesktopSurface();
			if (FAILED(hr)) {
				throw std::runtime_error(tsf::fmt("MapDesktopSurface error, code: %d\n", hr));
			}

			deskRes->Release();
		}
		else {
			ID3D11Texture2D* gpuTex = nullptr;
			hr = deskRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)& gpuTex);
			deskRes->Release();
			deskRes = nullptr;
			if (FAILED(hr)) {
				throw std::runtime_error(tsf::fmt("IDXGIResource.QueryInterface error, code: %d\n", hr));
			}

			D3D11_TEXTURE2D_DESC desc;
			gpuTex->GetDesc(&desc);
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.MiscFlags = 0;
			ID3D11Texture2D* cpuTex = nullptr;
			hr = D3DDevice->CreateTexture2D(&desc, nullptr, &cpuTex);
			if (SUCCEEDED(hr)) {
				D3DDeviceContext->CopyResource(cpuTex, gpuTex);
			}
			else {
				throw std::runtime_error(tsf::fmt("ID3D11Device.CreateTexture2D error, code: %d\n", hr));
			}
			gpuTex->Release();

			D3D11_MAPPED_SUBRESOURCE sr;
			hr = D3DDeviceContext->Map(cpuTex, 0, D3D11_MAP_READ, 0, &sr);
			if (SUCCEEDED(hr)) {
				for (int y = 0; y < (int)desc.Height; y++)
					memcpy(Latest.Buf.data() + (long long)y * Latest.Width * 4, (uint8_t*)sr.pData + (long long)sr.RowPitch * y, (long long)Latest.Width * 4);
				D3DDeviceContext->Unmap(cpuTex, 0);
			}
			else {
				throw std::runtime_error(tsf::fmt("ID3D11DeviceContext.Map error, code: %d\n", hr));
			}
			cpuTex->Release();
		}
		SendNBytes(Latest.Buf.data(), (int)Latest.Buf.size());
	}
}
