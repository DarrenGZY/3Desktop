#pragma once
#include "DirectModeTypes.h"
class DirectModeManager
{
public:
	DirectModeManager();

	void Present();
	DMM_Status Init(unsigned int displaySelection, unsigned int modeSelection, IDXGIDevice* dxgiDevice);
	DMM_Status Release();
	DMM_Status Present(UINT frameIndex, IDXGIDevice* dxgiDevice);
	ID3D11Texture2D* getSwapChianBuffer();
	~DirectModeManager();

private:
	DMM_Status InitBackBuffers();

	NV_DIRECT_MODE_DISPLAY_HANDLE m_display;
	NV_DIRECT_MODE_INFO m_modeDesc;
	NV_DIRECT_MODE_SURFACE_HANDLE m_surfaces[NUM_BACKBUFFERS];
	HANDLE m_sharedHandles[NUM_BACKBUFFERS];
	DXGI_FORMAT m_dxgiFormat;
	TextureSwapChain* m_texSwapChian;
};

