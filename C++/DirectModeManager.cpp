#include "DirectModeManager.h"


DirectModeManager::DirectModeManager()
{

}


DirectModeManager::~DirectModeManager()
{

}

DMM_Status DirectModeManager::Init(unsigned int displaySelection, unsigned int modeSelection, IDXGIDevice* dxgiDevice)
{
	NvU32 numDisplays = 4;
	NV_DIRECT_MODE_DISPLAY_HANDLE displays[4] = { 0 };

	auto status = NvAPI_DISP_EnumerateDirectModeDisplays(VENDOR_ID, &numDisplays, displays, NV_ENUM_DIRECTMODE_DISPLAY_ENABLED);
	if (status != NVAPI_OK)
	{
		return DMM_FAIL;
	}

	if (numDisplays <= displaySelection)
	{
		return DMM_NOSUCH_DISPLAY;
	}
	
	m_display = displays[displaySelection];

	NvU32 numModes = 0;
	status = NvAPI_D3D_DirectModeGetDisplayModes(&m_display, &numModes, nullptr, NV_DIRECTMODE_GETMODES_FLAG_SUPPORTED);
	if (status != NVAPI_OK)
	{
		return DMM_FAIL;
	}
	NV_DIRECT_MODE_INFO* modeDescs = new NV_DIRECT_MODE_INFO[numModes];
	for (NvU32 i = 0; i < numModes; ++i)
	{
		memset(&modeDescs[i], 0, sizeof(NV_DIRECT_MODE_INFO));
		modeDescs[i].version = NV_DIRECT_MODE_INFO_VER;
	}
	status = NvAPI_D3D_DirectModeGetDisplayModes(&m_display, &numModes, modeDescs, NV_DIRECTMODE_GETMODES_FLAG_SUPPORTED);
	if (status != NVAPI_OK)
	{
		delete[] modeDescs;
		return DMM_FAIL;
	}

	if (numModes <= modeSelection)
	{
		return DMM_NOSUCH_MODE;
	}

	m_modeDesc = modeDescs[modeSelection];
	delete[] modeDescs;

	switch (m_modeDesc.format)
	{
	case NV_FORMAT_A8B8G8R8:
		m_dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	case NV_FORMAT_A8R8G8B8:
		m_dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case NV_FORMAT_X8R8G8B8:
		m_dxgiFormat = DXGI_FORMAT_B8G8R8X8_UNORM;
		break;
	case NV_FORMAT_A2B10G10R10:
		m_dxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
		break;
	case NV_FORMAT_A16B16G16R16F:
		m_dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		break;
	case NV_FORMAT_UNKNOWN:
	default:
		// Add other desired formats
		m_dxgiFormat = DXGI_FORMAT_UNKNOWN;
		break;
	}

	status = NvAPI_D3D_AcquireDirectModeDisplay(VENDOR_ID, dxgiDevice, &m_display);
	if (status != NVAPI_OK && status != NVAPI_HDCP_DISABLED)
	{
		return DMM_FAIL;
	}

	for (int i = 0; i < NUM_BACKBUFFERS; i++)
	{
		auto status = NvAPI_D3D_DirectModeCreateSurface(&m_display, &m_modeDesc, &m_surfaces[i], &m_sharedHandles[i]);
		if (status != NVAPI_OK)
		{
			return DMM_FAIL;
		}
	}

	status = NvAPI_D3D_DirectModeSetDisplayMode(&m_display, &m_modeDesc);
	if (status != NVAPI_OK)
	{
		return DMM_FAIL;
	}

	return DMM_OK;
}

DMM_Status DirectModeManager::Present(UINT frameIndex, IDXGIDevice* dxgiDevice)
{
	auto status = NvAPI_D3D_DirectModePresent(&m_display, m_surfaces[frameIndex], NV_DIRECTMODE_PRESENT_FLAG_QUEUED_VSYNC, dxgiDevice);
	if (status != NVAPI_OK)
	{
		return DMM_FAIL;
	}
	return DMM_OK;
}

DMM_Status DirectModeManager::Release()
{
	auto status = NvAPI_D3D_ReleaseDirectModeDisplay(VENDOR_ID, &m_display);
	if (status != NVAPI_OK)
	{
		return DMM_FAIL;
	}
}

DMM_Status DirectModeManager::InitBackBuffers()
{
	for (int i = 0; i < NUM_BACKBUFFERS; i++)
	{
		auto status = NvAPI_D3D_DirectModeCreateSurface(&m_display, &m_modeDesc, &m_surfaces[i], &m_sharedHandles[i]);
		if (status != NVAPI_OK)
		{
			return DMM_FAIL;
		}
	}

	return DMM_OK;
}

ID3D11Texture2D* DirectModeManager::getSwapChianBuffer()
{
	return m_texSwapChian->textures[m_texSwapChian->frontbufIndex];
}