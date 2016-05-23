// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "OutputManager.h"
using namespace DirectX;

#ifdef VR_DESKTOP
typedef bool(*VRDesktop_SZVR_GETData)(float inputs[], float outputs[]);
VRDesktop_SZVR_GETData SZVR_GetData = NULL;

void UpdateCameraPosition(XMVECTOR & camPos);
void UpdateRadiusAndAngle(float &radius, float &halfAngle);
#endif // VR_DESKTOP

//#define DEBUG_VERTEX

//#define DEBUG_LIB
#define DEBUG_MAP
//
// Constructor NULLs out all pointers & sets appropriate var vals
//
OUTPUTMANAGER::OUTPUTMANAGER() : m_SwapChain(nullptr),
                                 m_Device(nullptr),
                                 m_Factory(nullptr),
                                 m_DeviceContext(nullptr),
                                 m_RTV(nullptr),
                                 m_SamplerLinear(nullptr),
                                 m_BlendState(nullptr),
                                 m_VertexShader(nullptr),
                                 m_PixelShader(nullptr),
                                 m_InputLayout(nullptr),
                                 m_SharedSurf(nullptr),
                                 m_KeyMutex(nullptr),
                                 m_WindowHandle(nullptr),
                                 m_NeedsResize(false),
#ifdef VR_DESKTOP
								 m_ScreenTex(nullptr),
#endif // VR_DESKTOP
                                 m_OcclusionCookie(0)
{
	for (int i = 0; i < MAX_WINDOWS; ++i)
	{
		m_windows[i] = nullptr;
	}
}

//
// Destructor which calls CleanRefs to release all references and memory.
//
OUTPUTMANAGER::~OUTPUTMANAGER()
{
    CleanRefs();
}

//
// Indicates that window has been resized.
//
void OUTPUTMANAGER::WindowResize()
{
    m_NeedsResize = true;
}

//
// Initialize all state
//
DUPL_RETURN OUTPUTMANAGER::InitOutput(HWND Window, INT SingleOutput, _Out_ UINT* OutCount, _Out_ RECT* DeskBounds)
{
    HRESULT hr;

    // Store window handle
    m_WindowHandle = Window;

    // Driver types supported
    D3D_DRIVER_TYPE DriverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    // Feature levels supported
    D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);
    D3D_FEATURE_LEVEL FeatureLevel;

    // Create device
    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
        D3D11_SDK_VERSION, &m_Device, &FeatureLevel, &m_DeviceContext);
        if (SUCCEEDED(hr))
        {
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Device creation in OUTPUTMANAGER failed", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Get DXGI factory
    IDXGIDevice* DxgiDevice = nullptr;
    hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DXGI Device", L"Error", hr, nullptr);
    }

    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get parent DXGI Adapter", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    hr = DxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&m_Factory));
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get parent DXGI Factory", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Register for occlusion status windows message
    hr = m_Factory->RegisterOcclusionStatusWindow(Window, OCCLUSION_STATUS_MSG, &m_OcclusionCookie);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to register for occlusion message", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Get window size
    RECT WindowRect;
    GetClientRect(m_WindowHandle, &WindowRect);
    UINT Width = WindowRect.right - WindowRect.left;
    UINT Height = WindowRect.bottom - WindowRect.top;

    // Create swapchain for window
    DXGI_SWAP_CHAIN_DESC1 SwapChainDesc;
    RtlZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));

    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    SwapChainDesc.BufferCount = 2;
    SwapChainDesc.Width = Width;
    SwapChainDesc.Height = Height;
    SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.SampleDesc.Quality = 0;

    hr = m_Factory->CreateSwapChainForHwnd(m_Device, Window, &SwapChainDesc, nullptr, nullptr, &m_SwapChain);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create window swapchain", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Disable the ALT-ENTER shortcut for entering full-screen mode
    hr = m_Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to make window association", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Create shared texture
    DUPL_RETURN Return = CreateSharedSurf(SingleOutput, OutCount, DeskBounds);
    if (Return != DUPL_RETURN_SUCCESS)
    {
        return Return;
    }

    // Make new render target view
    Return = MakeRTV();
    if (Return != DUPL_RETURN_SUCCESS)
    {
        return Return;
    }

    // Set view port
    SetViewPort(Width, Height);

    // Create the sample state
    D3D11_SAMPLER_DESC SampDesc;
    RtlZeroMemory(&SampDesc, sizeof(SampDesc));
    SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    SampDesc.MinLOD = 0;
    SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_Device->CreateSamplerState(&SampDesc, &m_SamplerLinear);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create sampler state in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Create the blend state
    D3D11_BLEND_DESC BlendStateDesc;
    BlendStateDesc.AlphaToCoverageEnable = FALSE;
    BlendStateDesc.IndependentBlendEnable = FALSE;
    BlendStateDesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    BlendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    BlendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    BlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    BlendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_Device->CreateBlendState(&BlendStateDesc, &m_BlendState);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create blend state in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Initialize shaders
    Return = InitShaders();
    if (Return != DUPL_RETURN_SUCCESS)
    {
        return Return;
    }

    GetWindowRect(m_WindowHandle, &WindowRect);
#ifdef VR_DESKTOP
	MoveWindow(m_WindowHandle, WindowRect.left, WindowRect.top, WindowRect.right, WindowRect.bottom, TRUE);
#else
	MoveWindow(m_WindowHandle, WindowRect.left, WindowRect.top, (DeskBounds->right - DeskBounds->left) / 2, (DeskBounds->bottom - DeskBounds->top) / 2, TRUE);
#endif // VR_DESKTOP

	
#ifndef DEBUG_LIB
#ifdef VR_DESKTOP

	if (SZVR_GetData == NULL)
	{
		HINSTANCE gInstLibrary = NULL;

		if (gInstLibrary == NULL)
		{
			gInstLibrary = ::LoadLibrary(L"SZVRPlugin.dll");
		}

		if (gInstLibrary)
		{
			SZVR_GetData = (VRDesktop_SZVR_GETData)GetProcAddress(gInstLibrary, "SZVR_GetData");
		}
	}

	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"back.png", NULL, &m_BackSky[BACK], NULL);
	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"front.png", NULL, &m_BackSky[FRONT], NULL);
	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"left.png", NULL, &m_BackSky[RIGHT], NULL);
	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"right.png", NULL, &m_BackSky[LEFT], NULL);
	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"top.png", NULL, &m_BackSky[TOP], NULL);
	hr = CreateWICTextureFromFile(m_Device, m_DeviceContext, L"bottom.png", NULL, &m_BackSky[BOTTOM], NULL);
#endif // VR_DESKTOP
#endif // !DEBUG_LIB


    return Return;
}

//
// Recreate shared texture
//
DUPL_RETURN OUTPUTMANAGER::CreateSharedSurf(INT SingleOutput, _Out_ UINT* OutCount, _Out_ RECT* DeskBounds)
{
    HRESULT hr;

    // Get DXGI resources
    IDXGIDevice* DxgiDevice = nullptr;
    hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DXGI Device", L"Error", hr);
    }

    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get parent DXGI Adapter", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Set initial values so that we always catch the right coordinates
    DeskBounds->left = INT_MAX;
    DeskBounds->right = INT_MIN;
    DeskBounds->top = INT_MAX;
    DeskBounds->bottom = INT_MIN;

    IDXGIOutput* DxgiOutput = nullptr;

    // Figure out right dimensions for full size desktop texture and # of outputs to duplicate
    UINT OutputCount;
    if (SingleOutput < 0)
    {
        hr = S_OK;
        for (OutputCount = 0; SUCCEEDED(hr); ++OutputCount)
        {
            if (DxgiOutput)
            {
                DxgiOutput->Release();
                DxgiOutput = nullptr;
            }
            hr = DxgiAdapter->EnumOutputs(OutputCount, &DxgiOutput);
            if (DxgiOutput && (hr != DXGI_ERROR_NOT_FOUND))
            {
                DXGI_OUTPUT_DESC DesktopDesc;
                DxgiOutput->GetDesc(&DesktopDesc);

                DeskBounds->left = min(DesktopDesc.DesktopCoordinates.left, DeskBounds->left);
                DeskBounds->top = min(DesktopDesc.DesktopCoordinates.top, DeskBounds->top);
                DeskBounds->right = max(DesktopDesc.DesktopCoordinates.right, DeskBounds->right);
                DeskBounds->bottom = max(DesktopDesc.DesktopCoordinates.bottom, DeskBounds->bottom);
            }
        }

        --OutputCount;
    }
    else
    {
        hr = DxgiAdapter->EnumOutputs(SingleOutput, &DxgiOutput);
        if (FAILED(hr))
        {
            DxgiAdapter->Release();
            DxgiAdapter = nullptr;
            return ProcessFailure(m_Device, L"Output specified to be duplicated does not exist", L"Error", hr);
        }
        DXGI_OUTPUT_DESC DesktopDesc;
        DxgiOutput->GetDesc(&DesktopDesc);
        *DeskBounds = DesktopDesc.DesktopCoordinates;

        DxgiOutput->Release();
        DxgiOutput = nullptr;

        OutputCount = 1;
    }

    DxgiAdapter->Release();
    DxgiAdapter = nullptr;

    // Set passed in output count variable
    *OutCount = OutputCount;

    if (OutputCount == 0)
    {
        // We could not find any outputs, the system must be in a transition so return expected error
        // so we will attempt to recreate
        return DUPL_RETURN_ERROR_EXPECTED;
    }

    // Create shared texture for all duplication threads to draw into
    D3D11_TEXTURE2D_DESC DeskTexD;
    RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
    DeskTexD.Width = DeskBounds->right - DeskBounds->left;
    DeskTexD.Height = DeskBounds->bottom - DeskBounds->top;
    DeskTexD.MipLevels = 1;
    DeskTexD.ArraySize = 1;
    DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    DeskTexD.SampleDesc.Count = 1;
    DeskTexD.Usage = D3D11_USAGE_DEFAULT;
    DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DeskTexD.CPUAccessFlags = 0;
    DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedSurf);
    if (FAILED(hr))
    {
        if (OutputCount != 1)
        {
            // If we are duplicating the complete desktop we try to create a single texture to hold the
            // complete desktop image and blit updates from the per output DDA interface.  The GPU can
            // always support a texture size of the maximum resolution of any single output but there is no
            // guarantee that it can support a texture size of the desktop.
            // The sample only use this large texture to display the desktop image in a single window using DX
            // we could revert back to using GDI to update the window in this failure case.
            return ProcessFailure(m_Device, L"Failed to create DirectX shared texture - we are attempting to create a texture the size of the complete desktop and this may be larger than the maximum texture size of your GPU.  Please try again using the -output command line parameter to duplicate only 1 monitor or configure your computer to a single monitor configuration", L"Error", hr, SystemTransitionsExpectedErrors);
        }
        else
        {
            return ProcessFailure(m_Device, L"Failed to create shared texture", L"Error", hr, SystemTransitionsExpectedErrors);
        }
    }

    // Get keyed mutex
    hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&m_KeyMutex));
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to query for keyed mutex in OUTPUTMANAGER", L"Error", hr);
    }

    return DUPL_RETURN_SUCCESS;
}

//
// Present to the application window
//
DUPL_RETURN OUTPUTMANAGER::UpdateApplicationWindow(_In_ PTR_INFO* PointerInfo, _Inout_ bool* Occluded)
{
    // In a typical desktop duplication application there would be an application running on one system collecting the desktop images
    // and another application running on a different system that receives the desktop images via a network and display the image. This
    // sample contains both these aspects into a single application.
    // This routine is the part of the sample that displays the desktop image onto the display

    // Try and acquire sync on common display buffer
    HRESULT hr = m_KeyMutex->AcquireSync(1, 100);
    if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
    {
        // Another thread has the keyed mutex so try again later
        return DUPL_RETURN_SUCCESS;
    }
    else if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to acquire Keyed mutex in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Got mutex, so draw
    DUPL_RETURN Ret = DrawFrame();
    if (Ret == DUPL_RETURN_SUCCESS)
    {
        // We have keyed mutex so we can access the mouse info
        if (PointerInfo->Visible)
        {
            // Draw mouse into texture
            Ret = DrawMouse(PointerInfo);
        }
    }
#ifdef VR_DESKTOP
	Ret = DrawToScreen();
#endif // VR_DESKTOP

    // Release keyed mutex
    hr = m_KeyMutex->ReleaseSync(0);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to Release Keyed mutex in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Present to window if all worked
    if (Ret == DUPL_RETURN_SUCCESS)
    {
        // Present to window
        hr = m_SwapChain->Present(1, 0);
        if (FAILED(hr))
        {
            return ProcessFailure(m_Device, L"Failed to present", L"Error", hr, SystemTransitionsExpectedErrors);
        }
        else if (hr == DXGI_STATUS_OCCLUDED)
        {
            *Occluded = true;
        }
    }

    return Ret;
}

//
// Returns shared handle
//
HANDLE OUTPUTMANAGER::GetSharedHandle()
{
    HANDLE Hnd = nullptr;

    // QI IDXGIResource interface to synchronized shared surface.
    IDXGIResource* DXGIResource = nullptr;
    HRESULT hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&DXGIResource));
    if (SUCCEEDED(hr))
    {
        // Obtain handle to IDXGIResource object.
        DXGIResource->GetSharedHandle(&Hnd);
        DXGIResource->Release();
        DXGIResource = nullptr;
    }

    return Hnd;
}

//
// Draw frame into backbuffer
//
DUPL_RETURN OUTPUTMANAGER::DrawFrame()
{
#ifdef VR_DESKTOP
	m_DeviceContext->IASetInputLayout(m_InputLayout);
#endif // VR_DESKTOP

	HRESULT hr;

    // If window was resized, resize swapchain
    if (m_NeedsResize)
    {
        DUPL_RETURN Ret = ResizeSwapChain();
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            return Ret;
        }
        m_NeedsResize = false;
    }

    // Vertices for drawing whole texture
    VERTEX Vertices[NUMVERTICES] =
    {
        {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
        {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
    };

    D3D11_TEXTURE2D_DESC FrameDesc;
    m_SharedSurf->GetDesc(&FrameDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
    ShaderDesc.Format = FrameDesc.Format;
    ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ShaderDesc.Texture2D.MostDetailedMip = FrameDesc.MipLevels - 1;
    ShaderDesc.Texture2D.MipLevels = FrameDesc.MipLevels;

    // Create new shader resource view
    ID3D11ShaderResourceView* ShaderResource = nullptr;
    hr = m_Device->CreateShaderResourceView(m_SharedSurf, &ShaderDesc, &ShaderResource);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Set resources
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    FLOAT blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
    m_DeviceContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    m_DeviceContext->OMSetRenderTargets(1, &m_RTV, nullptr);
    m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
    m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
    m_DeviceContext->PSSetShaderResources(0, 1, &ShaderResource);
    m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
    m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_BUFFER_DESC BufferDesc;
    RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
    BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = Vertices;

    ID3D11Buffer* VertexBuffer = nullptr;

    // Create vertex buffer
    hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
    if (FAILED(hr))
    {
        ShaderResource->Release();
        ShaderResource = nullptr;
        return ProcessFailure(m_Device, L"Failed to create vertex buffer when drawing a frame", L"Error", hr, SystemTransitionsExpectedErrors);
    }
    m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

	//m_DeviceContext->ClearState();
    // Draw textured quad onto render target
    m_DeviceContext->Draw(NUMVERTICES, 0);

	//m_SwapChain->Present(0, 0);

    VertexBuffer->Release();
    VertexBuffer = nullptr;

    // Release shader resource
    ShaderResource->Release();
    ShaderResource = nullptr;

    return DUPL_RETURN_SUCCESS;
}

//
// Process both masked and monochrome pointers
//
DUPL_RETURN OUTPUTMANAGER::ProcessMonoMask(bool IsMono, _Inout_ PTR_INFO* PtrInfo, _Out_ INT* PtrWidth, _Out_ INT* PtrHeight, _Out_ INT* PtrLeft, _Out_ INT* PtrTop, _Outptr_result_bytebuffer_(*PtrHeight * *PtrWidth * BPP) BYTE** InitBuffer, _Out_ D3D11_BOX* Box)
{
    // Desktop dimensions
    D3D11_TEXTURE2D_DESC FullDesc;
    m_SharedSurf->GetDesc(&FullDesc);
    INT DesktopWidth = FullDesc.Width;
    INT DesktopHeight = FullDesc.Height;

    // Pointer position
    INT GivenLeft = PtrInfo->Position.x;
    INT GivenTop = PtrInfo->Position.y;

    // Figure out if any adjustment is needed for out of bound positions
    if (GivenLeft < 0)
    {
        *PtrWidth = GivenLeft + static_cast<INT>(PtrInfo->ShapeInfo.Width);
    }
    else if ((GivenLeft + static_cast<INT>(PtrInfo->ShapeInfo.Width)) > DesktopWidth)
    {
        *PtrWidth = DesktopWidth - GivenLeft;
    }
    else
    {
        *PtrWidth = static_cast<INT>(PtrInfo->ShapeInfo.Width);
    }

    if (IsMono)
    {
        PtrInfo->ShapeInfo.Height = PtrInfo->ShapeInfo.Height / 2;
    }

    if (GivenTop < 0)
    {
        *PtrHeight = GivenTop + static_cast<INT>(PtrInfo->ShapeInfo.Height);
    }
    else if ((GivenTop + static_cast<INT>(PtrInfo->ShapeInfo.Height)) > DesktopHeight)
    {
        *PtrHeight = DesktopHeight - GivenTop;
    }
    else
    {
        *PtrHeight = static_cast<INT>(PtrInfo->ShapeInfo.Height);
    }

    if (IsMono)
    {
        PtrInfo->ShapeInfo.Height = PtrInfo->ShapeInfo.Height * 2;
    }

    *PtrLeft = (GivenLeft < 0) ? 0 : GivenLeft;
    *PtrTop = (GivenTop < 0) ? 0 : GivenTop;

    // Staging buffer/texture
    D3D11_TEXTURE2D_DESC CopyBufferDesc;
    CopyBufferDesc.Width = *PtrWidth;
    CopyBufferDesc.Height = *PtrHeight;
    CopyBufferDesc.MipLevels = 1;
    CopyBufferDesc.ArraySize = 1;
    CopyBufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    CopyBufferDesc.SampleDesc.Count = 1;
    CopyBufferDesc.SampleDesc.Quality = 0;
    CopyBufferDesc.Usage = D3D11_USAGE_STAGING;
    CopyBufferDesc.BindFlags = 0;
    CopyBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CopyBufferDesc.MiscFlags = 0;

    ID3D11Texture2D* CopyBuffer = nullptr;
    HRESULT hr = m_Device->CreateTexture2D(&CopyBufferDesc, nullptr, &CopyBuffer);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed creating staging texture for pointer", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Copy needed part of desktop image
    Box->left = *PtrLeft;
    Box->top = *PtrTop;
    Box->right = *PtrLeft + *PtrWidth;
    Box->bottom = *PtrTop + *PtrHeight;
    m_DeviceContext->CopySubresourceRegion(CopyBuffer, 0, 0, 0, 0, m_SharedSurf, 0, Box);

    // QI for IDXGISurface
    IDXGISurface* CopySurface = nullptr;
    hr = CopyBuffer->QueryInterface(__uuidof(IDXGISurface), (void **)&CopySurface);
    CopyBuffer->Release();
    CopyBuffer = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI staging texture into IDXGISurface for pointer", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Map pixels
    DXGI_MAPPED_RECT MappedSurface;
    hr = CopySurface->Map(&MappedSurface, DXGI_MAP_READ);
    if (FAILED(hr))
    {
        CopySurface->Release();
        CopySurface = nullptr;
        return ProcessFailure(m_Device, L"Failed to map surface for pointer", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // New mouseshape buffer
    *InitBuffer = new (std::nothrow) BYTE[*PtrWidth * *PtrHeight * BPP];
    if (!(*InitBuffer))
    {
        return ProcessFailure(nullptr, L"Failed to allocate memory for new mouse shape buffer.", L"Error", E_OUTOFMEMORY);
    }

    UINT* InitBuffer32 = reinterpret_cast<UINT*>(*InitBuffer);
    UINT* Desktop32 = reinterpret_cast<UINT*>(MappedSurface.pBits);
    UINT  DesktopPitchInPixels = MappedSurface.Pitch / sizeof(UINT);

    // What to skip (pixel offset)
    UINT SkipX = (GivenLeft < 0) ? (-1 * GivenLeft) : (0);
    UINT SkipY = (GivenTop < 0) ? (-1 * GivenTop) : (0);

    if (IsMono)
    {
        for (INT Row = 0; Row < *PtrHeight; ++Row)
        {
            // Set mask
            BYTE Mask = 0x80;
            Mask = Mask >> (SkipX % 8);
            for (INT Col = 0; Col < *PtrWidth; ++Col)
            {
                // Get masks using appropriate offsets
                BYTE AndMask = PtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch))] & Mask;
                BYTE XorMask = PtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY + (PtrInfo->ShapeInfo.Height / 2)) * (PtrInfo->ShapeInfo.Pitch))] & Mask;
                UINT AndMask32 = (AndMask) ? 0xFFFFFFFF : 0xFF000000;
                UINT XorMask32 = (XorMask) ? 0x00FFFFFF : 0x00000000;

                // Set new pixel
                InitBuffer32[(Row * *PtrWidth) + Col] = (Desktop32[(Row * DesktopPitchInPixels) + Col] & AndMask32) ^ XorMask32;

                // Adjust mask
                if (Mask == 0x01)
                {
                    Mask = 0x80;
                }
                else
                {
                    Mask = Mask >> 1;
                }
            }
        }
    }
    else
    {
        UINT* Buffer32 = reinterpret_cast<UINT*>(PtrInfo->PtrShapeBuffer);

        // Iterate through pixels
        for (INT Row = 0; Row < *PtrHeight; ++Row)
        {
            for (INT Col = 0; Col < *PtrWidth; ++Col)
            {
                // Set up mask
                UINT MaskVal = 0xFF000000 & Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))];
                if (MaskVal)
                {
                    // Mask was 0xFF
                    InitBuffer32[(Row * *PtrWidth) + Col] = (Desktop32[(Row * DesktopPitchInPixels) + Col] ^ Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))]) | 0xFF000000;
                }
                else
                {
                    // Mask was 0x00
                    InitBuffer32[(Row * *PtrWidth) + Col] = Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))] | 0xFF000000;
                }
            }
        }
    }

    // Done with resource
    hr = CopySurface->Unmap();
    CopySurface->Release();
    CopySurface = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to unmap surface for pointer", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    return DUPL_RETURN_SUCCESS;
}

//
// Draw mouse provided in buffer to backbuffer
//
DUPL_RETURN OUTPUTMANAGER::DrawMouse(_In_ PTR_INFO* PtrInfo)
{
    // Vars to be used
    ID3D11Texture2D* MouseTex = nullptr;
    ID3D11ShaderResourceView* ShaderRes = nullptr;
    ID3D11Buffer* VertexBufferMouse = nullptr;
    D3D11_SUBRESOURCE_DATA InitData;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC SDesc;

    // Position will be changed based on mouse position
    VERTEX Vertices[NUMVERTICES] =
    {
        {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
        {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
    };

    D3D11_TEXTURE2D_DESC FullDesc;
    m_SharedSurf->GetDesc(&FullDesc);
    INT DesktopWidth = FullDesc.Width;
    INT DesktopHeight = FullDesc.Height;

    // Center of desktop dimensions
    INT CenterX = (DesktopWidth / 2);
    INT CenterY = (DesktopHeight / 2);

    // Clipping adjusted coordinates / dimensions
    INT PtrWidth = 0;
    INT PtrHeight = 0;
    INT PtrLeft = 0;
    INT PtrTop = 0;

    // Buffer used if necessary (in case of monochrome or masked pointer)
    BYTE* InitBuffer = nullptr;

    // Used for copying pixels
    D3D11_BOX Box;
    Box.front = 0;
    Box.back = 1;

    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.CPUAccessFlags = 0;
    Desc.MiscFlags = 0;

    // Set shader resource properties
    SDesc.Format = Desc.Format;
    SDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SDesc.Texture2D.MostDetailedMip = Desc.MipLevels - 1;
    SDesc.Texture2D.MipLevels = Desc.MipLevels;

    switch (PtrInfo->ShapeInfo.Type)
    {
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
        {
            PtrLeft = PtrInfo->Position.x;
            PtrTop = PtrInfo->Position.y;

            PtrWidth = static_cast<INT>(PtrInfo->ShapeInfo.Width);
            PtrHeight = static_cast<INT>(PtrInfo->ShapeInfo.Height);

            break;
        }

        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
        {
            ProcessMonoMask(true, PtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer, &Box);
            break;
        }

        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
        {
            ProcessMonoMask(false, PtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer, &Box);
            break;
        }

        default:
            break;
    }

    // VERTEX creation
    Vertices[0].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
    Vertices[0].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
    Vertices[1].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
    Vertices[1].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
    Vertices[2].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
    Vertices[2].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
    Vertices[3].Pos.x = Vertices[2].Pos.x;
    Vertices[3].Pos.y = Vertices[2].Pos.y;
    Vertices[4].Pos.x = Vertices[1].Pos.x;
    Vertices[4].Pos.y = Vertices[1].Pos.y;
    Vertices[5].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
    Vertices[5].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;

    // Set texture properties
    Desc.Width = PtrWidth;
    Desc.Height = PtrHeight;

    // Set up init data
    InitData.pSysMem = (PtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? PtrInfo->PtrShapeBuffer : InitBuffer;
    InitData.SysMemPitch = (PtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? PtrInfo->ShapeInfo.Pitch : PtrWidth * BPP;
    InitData.SysMemSlicePitch = 0;

    // Create mouseshape as texture
    HRESULT hr = m_Device->CreateTexture2D(&Desc, &InitData, &MouseTex);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create mouse pointer texture", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Create shader resource from texture
    hr = m_Device->CreateShaderResourceView(MouseTex, &SDesc, &ShaderRes);
    if (FAILED(hr))
    {
        MouseTex->Release();
        MouseTex = nullptr;
        return ProcessFailure(m_Device, L"Failed to create shader resource from mouse pointer texture", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    D3D11_BUFFER_DESC BDesc;
    ZeroMemory(&BDesc, sizeof(D3D11_BUFFER_DESC));
    BDesc.Usage = D3D11_USAGE_DEFAULT;
    BDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
    BDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BDesc.CPUAccessFlags = 0;

    ZeroMemory(&InitData, sizeof(D3D11_SUBRESOURCE_DATA));
    InitData.pSysMem = Vertices;

    // Create vertex buffer
    hr = m_Device->CreateBuffer(&BDesc, &InitData, &VertexBufferMouse);
    if (FAILED(hr))
    {
        ShaderRes->Release();
        ShaderRes = nullptr;
        MouseTex->Release();
        MouseTex = nullptr;
        return ProcessFailure(m_Device, L"Failed to create mouse pointer vertex buffer in OutputManager", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Set resources
    FLOAT BlendFactor[4] = {0.f, 0.f, 0.f, 0.f};
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBufferMouse, &Stride, &Offset);
    m_DeviceContext->OMSetBlendState(m_BlendState, BlendFactor, 0xFFFFFFFF);
    m_DeviceContext->OMSetRenderTargets(1, &m_RTV, nullptr);
    m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
    m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
    m_DeviceContext->PSSetShaderResources(0, 1, &ShaderRes);
    m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);

    // Draw
    m_DeviceContext->Draw(NUMVERTICES, 0);

    // Clean
    if (VertexBufferMouse)
    {
        VertexBufferMouse->Release();
        VertexBufferMouse = nullptr;
    }
    if (ShaderRes)
    {
        ShaderRes->Release();
        ShaderRes = nullptr;
    }
    if (MouseTex)
    {
        MouseTex->Release();
        MouseTex = nullptr;
    }
    if (InitBuffer)
    {
        delete [] InitBuffer;
        InitBuffer = nullptr;
    }

    return DUPL_RETURN_SUCCESS;
}

#ifdef VR_DESKTOP
DUPL_RETURN OUTPUTMANAGER::DrawWindows(std::vector<HWND> windows)
{
	int totalWindow = windows.size();


	if (totalWindow > 0)
	{
		for (int i = 0; i < totalWindow && i < MAX_WINDOWS; ++i)
		{
			RECT rc;
			HWND hwnd = windows.at(i);

			GetWindowRect(hwnd, &rc);
			int winWidth = rc.right - rc.left;
			int winHeight = rc.bottom - rc.top;

			HDC hdcScreen = GetWindowDC(hwnd);
			HDC hdc = CreateCompatibleDC(hdcScreen);
			HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, winWidth, winHeight);
			SelectObject(hdc, hbmp);
			PrintWindow(hwnd, hdc, NULL);
			//BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hdcScreen, 0, 0, SRCCOPY);

			BITMAPINFOHEADER bmih;
			ZeroMemory(&bmih, sizeof(BITMAPINFOHEADER));
			bmih.biSize = sizeof(BITMAPINFOHEADER);
			bmih.biPlanes = 1;
			bmih.biBitCount = 32;
			bmih.biWidth = winWidth;
			bmih.biHeight = -winHeight;
			bmih.biCompression = BI_RGB;
			bmih.biSizeImage = 0;

			int bytes_per_pixel = bmih.biBitCount / 8;
			BYTE *pixels = (BYTE*)malloc(bytes_per_pixel * winWidth * winHeight);

			BITMAPINFO bmi = { 0 };
			bmi.bmiHeader = bmih;

			int row_count;
			row_count = GetDIBits(hdc, hbmp, 0, winHeight, pixels, &bmi, DIB_RGB_COLORS);

			DeleteDC(hdc);
			DeleteObject(hbmp);
			ReleaseDC(NULL, hdcScreen);

			D3D11_TEXTURE2D_DESC desc = CD3D11_TEXTURE2D_DESC(
				DXGI_FORMAT_B8G8R8A8_UNORM,
				winWidth,
				winHeight,
				1,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE,
				1,
				0,
				0
				);

			D3D11_SUBRESOURCE_DATA data;
			RtlZeroMemory(&data, sizeof(data));
			data.pSysMem = pixels;
			data.SysMemPitch = bytes_per_pixel*winWidth;
			data.SysMemSlicePitch = bytes_per_pixel*winWidth*winHeight;

			ID3D11Texture2D* windowTexture = nullptr;
			ID3D11ShaderResourceView* windowSRV = nullptr;

			HRESULT hr;
			hr = m_Device->CreateTexture2D(&desc, &data, &windowTexture);
			if (FAILED(hr))
			{
				return DUPL_RETURN_ERROR_EXPECTED;
			}

			data.pSysMem = nullptr;
			free(pixels);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			hr = m_Device->CreateShaderResourceView(windowTexture, &srvDesc, &windowSRV);
			if (FAILED(hr))
			{
				return DUPL_RETURN_ERROR_EXPECTED;
			}

			m_widthSteps[i] = (float)winWidth / (float)winHeight;

			if (m_windows[i])
			{
				m_windows[i]->Release();
				m_windows[i] = nullptr;
			}

			if (windowSRV)
			{
				m_windows[i] = windowSRV;
				m_windows[i]->AddRef();
				windowSRV->Release();
				windowSRV = nullptr;
			}

			if (windowTexture)
			{
				windowTexture->Release();
				windowTexture = nullptr;
			}
		}
	}
	
		for (int i = 0; i < MAX_WINDOWS && m_windows[i] != nullptr; i++)
		{
			float width = m_widthSteps[i];

// 			const int n = 10;
// 
// 			float startAngle = -30;
// 			float endAngle = -25;
// 			static float r = 10;							
// 
// 			float sita = XMConvertToRadians(-startAngle);	// sita range from -60 to 60
// 			float delta = XMConvertToRadians((endAngle - startAngle) / (float)n);	// draw texture every 5 degree
// 
// 			float centerZ = 8.0f;	// circle center z-axis offset
// 			float centerX = 0.0f;	// circle center x-axis offset
// 			XMFLOAT3 center = XMFLOAT3(0.0f, 0.0f, -centerZ); // center of circle
// 
// 			const int numVer = n * 4;
// 			VERTEX vertices[numVer];
// 
// 			float startHeight = -1.0f + 0.3f*(float)i;
// 			float endHeight = startHeight + 0.3f*(float)i;
// 			for (int j = 0; j < n; ++j)
// 			{
// 				float nextSita = sita + delta;
// 				vertices[j * 4] = { XMFLOAT3(r*sin(sita) + centerX, startHeight, r*cos(sita) - centerZ), XMFLOAT2((float(j) / float(n)), 1.0f) };
// 				vertices[j * 4 + 1] = { XMFLOAT3(r*sin(sita) + centerX, endHeight, r*cos(sita) - centerZ), XMFLOAT2((float(j) / float(n)), 0.0f) };
// 				vertices[j * 4 + 2] = { XMFLOAT3(r*sin(nextSita) + centerX, startHeight, r*cos(nextSita) - centerZ), XMFLOAT2((float(j + 1) / float(n)), 1.0f) };
// 				vertices[j * 4 + 3] = { XMFLOAT3(r*sin(nextSita) + centerX, endHeight, r*cos(nextSita) - centerZ), XMFLOAT2((float(j + 1) / float(n)), 0.0f) };
// 				sita = nextSita;
// 			}
// 
// 			D3D11_BUFFER_DESC BufferDesc;
// 			RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
// 			BufferDesc.Usage = D3D11_USAGE_DEFAULT;
// 			BufferDesc.ByteWidth = sizeof(VERTEX)* numVer;
// 			BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
// 			BufferDesc.CPUAccessFlags = 0;
// 			D3D11_SUBRESOURCE_DATA InitData;
// 			RtlZeroMemory(&InitData, sizeof(InitData));
// 			InitData.pSysMem = vertices;
// 
// 			ID3D11Buffer *vertexBuf = nullptr;
// 			m_Device->CreateBuffer(&BufferDesc, &InitData, &vertexBuf);       // create the buffer
// 
// 			const int numInd = n * 6;	// n*6 indices for screen, 4*6 for background
// 			DWORD OurIndices[numInd];
// 
// 			for (int j = 0; j < n; ++j)
// 			{
// 				int base = j * 4;
// 				OurIndices[j * 6] = base;
// 				OurIndices[j * 6 + 1] = base + 1;
// 				OurIndices[j * 6 + 2] = base + 2;
// 				OurIndices[j * 6 + 3] = base + 3;
// 				OurIndices[j * 6 + 4] = base + 2;
// 				OurIndices[j * 6 + 5] = base + 1;
// 			}
// 
// 			BufferDesc.Usage = D3D11_USAGE_DEFAULT;
// 			BufferDesc.ByteWidth = sizeof(DWORD)* numInd;
// 			BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
// 			BufferDesc.CPUAccessFlags = 0;
// 
// 			RtlZeroMemory(&InitData, sizeof(InitData));
// 			InitData.pSysMem = OurIndices;
// 
// 			ID3D11Buffer *indexBuf = nullptr;
// 			m_Device->CreateBuffer(&BufferDesc, &InitData, &indexBuf);
// 
// 			UINT stride = sizeof(VERTEX);
// 			UINT offset = 0;
// 			m_DeviceContext->IASetVertexBuffers(0, 1, &vertexBuf, &stride, &offset);
// 			m_DeviceContext->OMSetBlendState(m_BlendState, NULL, 0xFFFFFFFF);
// 			m_DeviceContext->PSSetShaderResources(0, 1, &m_windows[i]);
// 			m_DeviceContext->DrawIndexed(numInd, 0, 0);
// 
// 			if (vertexBuf)
// 			{
// 				vertexBuf->Release();
// 				vertexBuf = nullptr;
// 			}
// 
// 			if (indexBuf)
// 			{
// 				indexBuf->Release();
// 				indexBuf = nullptr;
// 			}

			VERTEX vertices[6];
			switch (i)
			{
			case 0:
				vertices[0] = { XMFLOAT3(-4.0f - width, 0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) };
				vertices[1] = { XMFLOAT3(-4.0f - width, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				vertices[2] = { XMFLOAT3(-4.0f, 0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[3] = { XMFLOAT3(-4.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) };
				vertices[4] = { XMFLOAT3(-4.0f, 0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[5] = { XMFLOAT3(-4.0f - width, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				break;
			case 1:
				vertices[0] = { XMFLOAT3(-4.0f - width, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) };
				vertices[1] = { XMFLOAT3(-4.0f - width, -0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				vertices[2] = { XMFLOAT3(-4.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[3] = { XMFLOAT3(-4.0f, -0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) };
				vertices[4] = { XMFLOAT3(-4.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[5] = { XMFLOAT3(-4.0f - width, -0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				break;
			case 2:
				vertices[0] = { XMFLOAT3(4.0f, 0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) };
				vertices[1] = { XMFLOAT3(4.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				vertices[2] = { XMFLOAT3(4.0f + width, 0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[3] = { XMFLOAT3(4.0f + width, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) };
				vertices[4] = { XMFLOAT3(4.0f + width, 0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[5] = { XMFLOAT3(4.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				break;
			case 3:
				vertices[0] = { XMFLOAT3(4.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) };
				vertices[1] = { XMFLOAT3(4.0f, -0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				vertices[2] = { XMFLOAT3(4.0f + width, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[3] = { XMFLOAT3(4.0f + width, -0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) };
				vertices[4] = { XMFLOAT3(4.0f + width, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
				vertices[5] = { XMFLOAT3(4.0f, -0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
				break;
			default:
				return DUPL_RETURN_ERROR_UNEXPECTED;
			}

			D3D11_BUFFER_DESC BufferDesc;
			RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
			BufferDesc.Usage = D3D11_USAGE_DEFAULT;
			BufferDesc.ByteWidth = sizeof(VERTEX)* 6;
			BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			BufferDesc.CPUAccessFlags = 0;
			D3D11_SUBRESOURCE_DATA InitData;
			RtlZeroMemory(&InitData, sizeof(InitData));
			InitData.pSysMem = vertices;

			ID3D11Buffer *vertexBuf = nullptr;
			m_Device->CreateBuffer(&BufferDesc, &InitData, &vertexBuf);       // create the buffer

			UINT stride = sizeof(VERTEX);
			UINT offset = 0;
			m_DeviceContext->IASetVertexBuffers(0, 1, &vertexBuf, &stride, &offset);
			m_DeviceContext->OMSetBlendState(m_BlendState, NULL, 0xFFFFFFFF);
			m_DeviceContext->PSSetShaderResources(0, 1, &m_windows[i]);
			m_DeviceContext->Draw(6, 0);

			if (vertexBuf)
			{
				vertexBuf->Release();
				vertexBuf = nullptr;
			}
		}
 	return DUPL_RETURN_SUCCESS;
}



BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam)
{
	std::vector<HWND> *pvec = (std::vector<HWND>*)lparam;

	if (IsWindowVisible(hwnd) && !GetParent(hwnd) && !GetWindow(hwnd, GW_OWNER))
	{
		RECT rc;

		GetWindowRect(hwnd, &rc);
		// if the window size is too small, discard it 
		if ((rc.bottom - rc.top) < 100 || (rc.right - rc.left) < 100)
		{
			return TRUE;
		}
		char windowName[128];
		char className[128];
		GetWindowTextA(hwnd, windowName, 128);
		GetClassNameA(hwnd, className, 128);
		if (strlen(windowName) != 0 && strcmp(windowName, "3Desktop") != 0 && strstr(windowName, "Chrome") == NULL)
		{
			pvec->push_back(hwnd);
		}
	}
	return TRUE;
}


// Draw the duplicated desktop to a distant screen
DUPL_RETURN OUTPUTMANAGER::DrawToScreen()
{
	// Get desktop texture
	ID3D11Texture2D *pSurface = nullptr;
	m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pSurface));

	D3D11_TEXTURE2D_DESC desc;
	pSurface->GetDesc(&desc);

	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	//desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	HRESULT hr = m_Device->CreateTexture2D(&desc, NULL, &m_ScreenTex);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	m_DeviceContext->CopyResource(m_ScreenTex, pSurface);

	if (pSurface)
	{
		pSurface->Release();
		pSurface = nullptr;
	}

	// Get all visible windows in desktop
	std::vector<HWND> winHandles;
	EnumWindows(EnumProc, (LPARAM)&winHandles);

	// create shader resource view
	D3D11_TEXTURE2D_DESC FrameDesc;
	m_ScreenTex->GetDesc(&FrameDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = FrameDesc.Format;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = FrameDesc.MipLevels - 1;
	ShaderDesc.Texture2D.MipLevels = FrameDesc.MipLevels;

	// Create new shader resource view
	ID3D11ShaderResourceView* ScreenShaderResource = nullptr;	// TODO: Dont forget to release
	
	//hr = m_Device->CreateShaderResourceView(m_ScreenTex, &ShaderDesc, &ScreenShaderResource);
	hr = m_Device->CreateShaderResourceView(m_ScreenTex, &ShaderDesc, &ScreenShaderResource);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	if (m_ScreenTex)
	{
		m_ScreenTex->Release();
		m_ScreenTex = nullptr;
	}

	// create the vertex buffer
#ifdef DEBUG_VERTEX
	// Vertices for drawing whole texture
	VERTEX Vertices[NUMVERTICES] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX)* NUMVERTICES;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	ID3D11Buffer* VertexBuffer = nullptr;

	// Create vertex buffer
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
#else

	ID3D11Buffer *pVBuffer = nullptr;		// TODO: Dont forget to release
	ID3D11Buffer *pIBuffer = nullptr;


	const int n = 400;
	
	static float halfDegree = 25;
	static float r = 10;							// radius
	UpdateRadiusAndAngle(r, halfDegree);

	float sita = XMConvertToRadians(-halfDegree);	// sita range from -60 to 60
	float delta = XMConvertToRadians(2*halfDegree/(float)n);	// draw texture every 5 degree
	

	float centerZ = 8.0f;	// circle center z-axis offset
	float centerX = 0.0f;	// circle center x-axis offset
	XMFLOAT3 center = XMFLOAT3(0.0f, 0.0f, -centerZ); // center of circle

	const int numVer = n * 4 + 6 * 4;	// n*4 vertices for screen, 4*4 vertices for background
	VERTEX OurVertices[numVer];	

	for (int i = 0; i < n; ++i)
	{
		float nextSita = sita + delta;
		OurVertices[i * 4] = { XMFLOAT3(r*sin(sita)+centerX, -1.0f, r*cos(sita) - centerZ), XMFLOAT2((float(i)/float(n)), 1.0f) };
		OurVertices[i * 4 + 1] = { XMFLOAT3(r*sin(sita)+centerX, 1.0f, r*cos(sita) - centerZ), XMFLOAT2((float(i) / float(n)), 0.0f) };
		OurVertices[i * 4 + 2] = { XMFLOAT3(r*sin(nextSita)+centerX, -1.0f, r*cos(nextSita) - centerZ), XMFLOAT2((float(i+1) / float(n)), 1.0f) };
		OurVertices[i * 4 + 3] = { XMFLOAT3(r*sin(nextSita)+centerX, 1.0f, r*cos(nextSita) - centerZ), XMFLOAT2((float(i+1) / float(n)), 0.0f) };
		sita = nextSita;
	}

	const float len = 50.0f;	// half length of wall in a box

	int startInd;

	startInd = 4 * n;
	OurVertices[startInd] = { XMFLOAT3(-len, -len, -len), XMFLOAT2(1.0f, 1.0f) };    // BACK
	OurVertices[startInd+1] = { XMFLOAT3(-len, len, -len), XMFLOAT2(1.0f, 0.0f) };
	OurVertices[startInd+2] = { XMFLOAT3(len, -len, -len), XMFLOAT2(0.0f, 1.0f) };
	OurVertices[startInd+3] = { XMFLOAT3(len, len, -len), XMFLOAT2(0.0f, 0.0f) };

	startInd = 4 * (n + 1);
	OurVertices[startInd] = { XMFLOAT3(-len, -len, len), XMFLOAT2(0.0f, 1.0f) };    // FRONT
	OurVertices[startInd+1] = { XMFLOAT3(-len, len, len), XMFLOAT2(0.0f, 0.0f) };
	OurVertices[startInd+2] = { XMFLOAT3(len, -len, len), XMFLOAT2(1.0f, 1.0f) };
	OurVertices[startInd+3] = { XMFLOAT3(len, len, len), XMFLOAT2(1.0f, 0.0f) };

	startInd = 4 * (n + 2);
	OurVertices[startInd] = { XMFLOAT3(-len, -len, -len), XMFLOAT2(0.0f, 1.0f) };    // LEFT
	OurVertices[startInd+1] = { XMFLOAT3(-len, len, -len), XMFLOAT2(0.0f, 0.0f) };
	OurVertices[startInd+2] = { XMFLOAT3(-len, -len, len), XMFLOAT2(1.0f, 1.0f) };
	OurVertices[startInd+3] = { XMFLOAT3(-len, len, len), XMFLOAT2(1.0f, 0.0f) };

	startInd = 4 * (n + 3);
	OurVertices[startInd] = { XMFLOAT3(len, -len, -len), XMFLOAT2(1.0f, 1.0f) };    // RIGHT
	OurVertices[startInd+1] = { XMFLOAT3(len, len, -len), XMFLOAT2(1.0f, 0.0f) };
	OurVertices[startInd+2] = {XMFLOAT3(len, -len, len), XMFLOAT2(0.0f, 1.0f) };
	OurVertices[startInd+3] = { XMFLOAT3(len, len, len), XMFLOAT2(0.0f, 0.0f) };

	startInd = 4 * (n + 4);
	OurVertices[startInd] = { XMFLOAT3(-len, len, -len), XMFLOAT2(0.0f, 0.0f) };    // TOP
	OurVertices[startInd + 1] = { XMFLOAT3(-len, len, len), XMFLOAT2(0.0f, 1.0f) };
	OurVertices[startInd + 2] = { XMFLOAT3(len, len, -len), XMFLOAT2(1.0f, 0.0f) };
	OurVertices[startInd + 3] = { XMFLOAT3(len, len, len), XMFLOAT2(1.0f, 1.0f) };

	startInd = 4 * (n + 5);
	OurVertices[startInd] = { XMFLOAT3(-len, -len, -len), XMFLOAT2(0.0f, 1.0f) };    // BOTTOM
	OurVertices[startInd + 1] = { XMFLOAT3(-len, -len, len), XMFLOAT2(0.0f, 0.0f) };
	OurVertices[startInd + 2] = { XMFLOAT3(len, -len, -len), XMFLOAT2(1.0f, 1.0f) };
	OurVertices[startInd + 3] = { XMFLOAT3(len, -len, len), XMFLOAT2(1.0f, 0.0f) };

// 	VERTEX OurVertices[] =
// 	{
// 		{ XMFLOAT3(-2.5f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
// 		{ XMFLOAT3(-2.5f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
// 		{ XMFLOAT3(2.5f, -1.0f, 0.0f),XMFLOAT2(1.0f, 1.0f) },
// 		{ XMFLOAT3(2.5f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
// 	};

#ifndef DEBUG_MAP
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));

	bd.Usage = D3D11_USAGE_DYNAMIC;                // write access access by CPU and GPU
	bd.ByteWidth = sizeof(VERTEX)* 4;              // size is the VERTEX struct * 4
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;       // use as a vertex buffer
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;    // allow CPU to write in buffer

	hr = m_Device->CreateBuffer(&bd, NULL, &pVBuffer);       // create the buffer
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	// copy the vertices into the buffer
	D3D11_MAPPED_SUBRESOURCE ms;
	m_DeviceContext->Map(pVBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);    // map the buffer
	memcpy(ms.pData, OurVertices, sizeof(OurVertices));                 // copy the data
	m_DeviceContext->Unmap(pVBuffer, NULL);                                      // unmap the buffer

#else
	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX)* numVer;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = OurVertices;
	
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &pVBuffer);       // create the buffer
#endif
	
	const int numInd = n * 6 + 6 * 6;	// n*6 indices for screen, 4*6 for background
	DWORD OurIndices[numInd];

	for (int i = 0; i < n; ++i)
	{
		int base = i * 4;
		OurIndices[i*6] = base;
		OurIndices[i*6 + 1] = base + 1;
		OurIndices[i*6 + 2] = base + 2;
		OurIndices[i*6 + 3] = base + 3;
		OurIndices[i*6 + 4] = base + 2;
		OurIndices[i*6 + 5] = base + 1;
	}

	int base;
	
	// Back indices
	base = 4 * n;
	startInd = 6 * n;
	OurIndices[startInd] = base + 2;
	OurIndices[startInd+ 1] = base + 1;
	OurIndices[startInd + 2] = base;
	OurIndices[startInd + 3] = base + 1;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 3;

	// Front indices
	base = 4 * (n+1);
	startInd = 6 * (n + 1);
	OurIndices[startInd] = base;
	OurIndices[startInd + 1] = base + 1;
	OurIndices[startInd + 2] = base + 2;
	OurIndices[startInd + 3] = base + 3;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 1;

	// Left indices
	base = 4 * (n+2);
	startInd = 6 * (n + 2);
	OurIndices[startInd] = base;
	OurIndices[startInd + 1] = base + 1;
	OurIndices[startInd + 2] = base + 2;
	OurIndices[startInd + 3] = base + 3;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 1;

	// Right indices
	base = 4 * (n+3);
	startInd = 6 * (n + 3);
	OurIndices[startInd] = base + 2;
	OurIndices[startInd + 1] = base + 1;
	OurIndices[startInd + 2] = base;
	OurIndices[startInd + 3] = base + 1;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 3;

	// Top indices
	base = 4 * (n + 4);
	startInd = 6 * (n + 4);
	OurIndices[startInd] = base + 2;
	OurIndices[startInd + 1] = base + 1;
	OurIndices[startInd + 2] = base + 0;
	OurIndices[startInd + 3] = base + 1;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 3;

	// Bottom indices
	base = 4 * (n + 5);
	startInd = 6 * (n + 5);
	OurIndices[startInd] = base;
	OurIndices[startInd + 1] = base + 1;
	OurIndices[startInd + 2] = base + 2;
	OurIndices[startInd + 3] = base + 3;
	OurIndices[startInd + 4] = base + 2;
	OurIndices[startInd + 5] = base + 1;

// 	DWORD OurIndices[] =
// 	{
// 		0, 1, 2,
// 		3, 2, 1,
// 	};

#ifndef DEBUG_MAP
	// create the index buffer
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = sizeof(DWORD)* 6;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;

	hr = m_Device->CreateBuffer(&bd, NULL, &pIBuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	m_DeviceContext->Map(pIBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);    // map the buffer
	memcpy(ms.pData, OurIndices, sizeof(OurIndices));                   // copy the data
	m_DeviceContext->Unmap(pIBuffer, NULL);
#else
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(DWORD)* numInd;
	BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;

	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = OurIndices;
	
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &pIBuffer);
#endif
#endif
	// Get window size
	RECT WindowRect;
	GetClientRect(m_WindowHandle, &WindowRect);
	UINT Width = WindowRect.right - WindowRect.left;
	UINT Height = WindowRect.bottom - WindowRect.top;

	// Initialize zbuffer
	ID3D11DepthStencilView *zbuffer = nullptr;	// TODO: Dont forget to release

	D3D11_TEXTURE2D_DESC texd;
	ZeroMemory(&texd, sizeof(texd));

	texd.Width = Width;
	texd.Height = Height;
	texd.ArraySize = 1;
	texd.MipLevels = 1;
	texd.SampleDesc.Count = 1;
	texd.Format = DXGI_FORMAT_D32_FLOAT;
	texd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	ID3D11Texture2D *pDepthBuffer = nullptr;
	hr = m_Device->CreateTexture2D(&texd, NULL, &pDepthBuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	// create the depth buffer
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
	ZeroMemory(&dsvd, sizeof(dsvd));

	dsvd.Format = DXGI_FORMAT_D32_FLOAT;
	dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;

	hr = m_Device->CreateDepthStencilView(pDepthBuffer, &dsvd, &zbuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	if (pDepthBuffer)
	{
		pDepthBuffer->Release();
		pDepthBuffer = nullptr;
	}
	
	// create constant buffer
	ID3D11Buffer *pCBuffer = nullptr;		// TODO: Dont forget to release

	D3D11_BUFFER_DESC consBufferDesc;
	ZeroMemory(&consBufferDesc, sizeof(consBufferDesc));

	consBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	consBufferDesc.ByteWidth = sizeof(float) * 16;
	consBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	hr = m_Device->CreateBuffer(&consBufferDesc, NULL, &pCBuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}
////////////////////////////////////////////////////////////////////////////
	// Begin to render texture for two eyes
//////////////////////////////////////////////////////////////////////////////

	ID3D11ShaderResourceView *pEyeShaderResource[2];	// Dont forget to release
	Eye_Type eyes[2] = { LEFT_EYE, RIGHT_EYE };
	
	// Outside of for loop, in case of directions of two eye are not the same
	float inputOpt[1] = { 1 };
	float resultDir[4] = { 0, 0, 0, 0 };

#ifdef DEBUG_LIB
	XMVECTOR lookAt = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
#else
#ifdef VR_DESKTOP
	
	XMMATRIX matRot;

	if (SZVR_GetData(inputOpt, resultDir))
	{
		FLOAT x = -resultDir[0];
		FLOAT y = -resultDir[1];
		FLOAT z = -resultDir[2];			// ignore the z-axis rotation
		FLOAT w = resultDir[3];
		
		// Tranform Quaterion to Matrix (Headset rotation)
// 		matRot = XMMATRIX(1.0f-2.0f*y*y-2.0f*z*z, 2.0f*x*y+2.0f*w*z, 2.0f*x*z-2.0f*w*y, 0, 
// 					      2.0f*x*y-2.0f*w*z, 1.0f-2.0f*x*x-2.0f*z*z, 2.0f*y*z+2.0f*w*x, 0, 
// 						  2.0f*x*z+2.0f*w*y, 2.0f*y*z-2.0f*w*x, 1.0f-2.0f*x*x-2.0f*y*y, 0, 
// 						  0, 0, 0, 1.0f);

		matRot = XMMATRIX(-(1.0f - 2.0f*y*y - 2.0f*z*z), -(2.0f*x*y + 2.0f*w*z), -(2.0f*x*z - 2.0f*w*y), 0,
			-(2.0f*x*y - 2.0f*w*z), -(1.0f - 2.0f*x*x - 2.0f*z*z), -(2.0f*y*z + 2.0f*w*x), 0,
			-(2.0f*x*z + 2.0f*w*y), -(2.0f*y*z - 2.0f*w*x), -(1.0f - 2.0f*x*x - 2.0f*y*y), 0,
			0, 0, 0, 1.0f);
	}
#else
	XMVECTOR lookAt = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
#endif // VR_DESKTOP
#endif // DEBUG_LIB

	for (int index = 0; index < 2; ++index)
	{
		// Initialization finished, begin to set position
		CBUFFER cBuffer;

		DirectX::XMMATRIX matView, matPorj;

		static XMVECTOR camPos = XMVectorSet(-0.01f, 0.0f, -3.0f, 0.0f);
		if (eyes[index] == LEFT_EYE)	// left eye
			camPos += XMVectorSet(0.02f, 0.0f, 0.0f, 0.0f);		// start at (0.1, 0.0, 0.0)
		else                            // right eye
			camPos += XMVectorSet(-0.02f, 0.0f, 0.0f, 0.0f);		// start at (-0.1, 0.0, 0.0)

		UpdateCameraPosition(camPos);
		
		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

		XMVECTOR lookAt = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
		lookAt = XMVector3Transform(lookAt, matRot);

		matView = XMMatrixLookAtLH(camPos, camPos+lookAt, up);

		//matView = matView;	// get the camera position after rotation

		matPorj = XMMatrixPerspectiveFovLH(XMConvertToRadians(110), (FLOAT)Width / (FLOAT)Height, 0.03f, 100.0f);

		cBuffer.Final = matView * matPorj;

		// Begin to set buffers
		m_DeviceContext->ClearState();

		m_DeviceContext->OMSetRenderTargets(1, &m_RTV, zbuffer);

		FLOAT color[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_DeviceContext->ClearRenderTargetView(m_RTV, color);
		m_DeviceContext->ClearDepthStencilView(zbuffer, D3D11_CLEAR_DEPTH, 1.0f, 0);

		SetViewPort(Width, Height);

		m_DeviceContext->IASetInputLayout(m_ScreenInputLayout);

		UINT stride = sizeof(VERTEX);
		UINT offset = 0;
		m_DeviceContext->IASetVertexBuffers(0, 1, &pVBuffer, &stride, &offset);
		m_DeviceContext->IASetIndexBuffer(pIBuffer, DXGI_FORMAT_R32_UINT, 0);

		m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		m_DeviceContext->VSSetConstantBuffers(0, 1, &pCBuffer);
		m_DeviceContext->UpdateSubresource(pCBuffer, 0, 0, &cBuffer, 0, 0);
		m_DeviceContext->VSSetShader(m_ScreenVertexShader, NULL, NULL);
		m_DeviceContext->PSSetShader(m_PixelShader, NULL, NULL);
		m_DeviceContext->PSSetShaderResources(0, 1, &ScreenShaderResource);
		m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);

		//m_DeviceContext->ClearState();
		m_DeviceContext->DrawIndexed(6*n, 0, 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[BACK]);		// Draw back 
		m_DeviceContext->DrawIndexed(6, 6 * n, 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[FRONT]);		// Draw front
		m_DeviceContext->DrawIndexed(6, 6 * (n + 1) , 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[LEFT]);		// Draw left
		m_DeviceContext->DrawIndexed(6, 6 * (n + 2), 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[RIGHT]);		// Draw right
		m_DeviceContext->DrawIndexed(6, 6 * (n + 3), 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[TOP]);		// Draw top
		m_DeviceContext->DrawIndexed(6, 6 * (n + 4), 0);

		m_DeviceContext->PSSetShaderResources(0, 1, &m_BackSky[BOTTOM]);		// Draw bottom
		m_DeviceContext->DrawIndexed(6, 6 * (n + 5), 0);

// 		// Draw other windows
// 		DrawWindows(winHandles);
// 		winHandles.clear();

		// Prepare for screen texture, store in pResource
		ID3D11Texture2D *pEyeScreen = nullptr;
		m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pEyeScreen));

		ID3D11Texture2D* pEyeTexture = nullptr;

		D3D11_TEXTURE2D_DESC description;

		pEyeScreen->GetDesc(&description);

		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.CPUAccessFlags = 0;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.SampleDesc.Count = 1;
		description.SampleDesc.Quality = 0;

		hr = m_Device->CreateTexture2D(&description, NULL, &pEyeTexture);
		if (FAILED(hr))
		{
			return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
		}
		m_DeviceContext->CopyResource(pEyeTexture, pEyeScreen);

		hr = m_Device->CreateShaderResourceView(pEyeTexture, NULL, &pEyeShaderResource[index]);
		if (FAILED(hr))
		{
			return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
		}

		if (pEyeScreen)
		{
			pEyeScreen->Release();
			pEyeScreen = nullptr;
		}

		if (pEyeTexture)
		{
			pEyeTexture->Release();
			pEyeTexture = nullptr;
		}
	}

//////////////////////////////////////////////////////////
	// Begin to render seperate-screen
////////////////////////////////////////////////////////

	ID3D11Buffer *pVEyeBuffer;
	ID3D11Buffer *pIEyeBuffer;

	VERTEX EyeVertices[] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
	};
#ifndef DEBUG_MAP
	// create the vertex buffer
	D3D11_BUFFER_DESC BufferDes;
	ZeroMemory(&BufferDes, sizeof(BufferDes));

	BufferDes.Usage = D3D11_USAGE_DYNAMIC;                // write access access by CPU and GPU
	BufferDes.ByteWidth = sizeof(VERTEX)* 8;              // size is the VERTEX struct * 3
	BufferDes.BindFlags = D3D11_BIND_VERTEX_BUFFER;       // use as a vertex buffer
	BufferDes.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;    // allow CPU to write in buffer

	m_Device->CreateBuffer(&BufferDes, NULL, &pVEyeBuffer);     // create the buffer

	// copy the vertices into the buffer
	D3D11_MAPPED_SUBRESOURCE maps;
	m_DeviceContext->Map(pVEyeBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &maps);    // map the buffer
	memcpy(maps.pData, EyeVertices, sizeof(EyeVertices));                 // copy the data
	m_DeviceContext->Unmap(pVEyeBuffer, NULL);                                      // unmap the buffer

#else
	D3D11_BUFFER_DESC BufferDes;
	ZeroMemory(&BufferDes, sizeof(BufferDes));

	BufferDes.Usage = D3D11_USAGE_DEFAULT;                
	BufferDes.ByteWidth = sizeof(VERTEX)* 8;              
	BufferDes.BindFlags = D3D11_BIND_VERTEX_BUFFER;       
	BufferDes.CPUAccessFlags = 0;    

	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = EyeVertices;

	hr = m_Device->CreateBuffer(&BufferDes, &InitData, &pVEyeBuffer);
#endif

	DWORD EyeIndices[] =
	{
		0, 1, 2,
		3, 2, 1,
		4, 5, 6,
		7, 6, 5,
	};

#ifndef DEBUG_MAP
	// create the index buffer
	BufferDes.Usage = D3D11_USAGE_DYNAMIC;
	BufferDes.ByteWidth = sizeof(DWORD)* 12;
	BufferDes.BindFlags = D3D11_BIND_INDEX_BUFFER;
	BufferDes.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	BufferDes.MiscFlags = 0;

	hr = m_Device->CreateBuffer(&BufferDes, NULL, &pIEyeBuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame(in screen)", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	m_DeviceContext->Map(pIEyeBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &maps);    // map the buffer
	memcpy(maps.pData, EyeIndices, sizeof(EyeIndices));                   // copy the data
	m_DeviceContext->Unmap(pIEyeBuffer, NULL);
#else

	BufferDes.Usage = D3D11_USAGE_DEFAULT;
	BufferDes.ByteWidth = sizeof(DWORD)* 12;
	BufferDes.BindFlags = D3D11_BIND_INDEX_BUFFER;
	BufferDes.CPUAccessFlags = 0;
	BufferDes.MiscFlags = 0;

	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = EyeIndices;

	hr = m_Device->CreateBuffer(&BufferDes, &InitData, &pIEyeBuffer);

#endif
	m_DeviceContext->ClearState();

	m_DeviceContext->OMSetRenderTargets(1, &m_RTV, NULL);
	

	// Set View Port according to screen resolution
	float inputs[1] = { 0 };
	float result[4];

#ifndef DEBUG_LIB
	if (SZVR_GetData(inputs, result))
	{
		SetViewPort(result[2], result[3]);
		//SetViewPort(800, 600);
	}

#else
	SetViewPort(800, 600);
#endif // !DEBUG_LIB

	m_DeviceContext->IASetInputLayout(m_InputLayout);

	m_DeviceContext->VSSetShader(m_VertexShader, 0, 0);
	m_DeviceContext->PSSetShader(m_ScreenPixelShader, 0, 0);

	UINT stride = sizeof(VERTEX);
	UINT offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &pVEyeBuffer, &stride, &offset);
	m_DeviceContext->IASetIndexBuffer(pIEyeBuffer, DXGI_FORMAT_R32_UINT, 0);

	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);

	// left eye
	m_DeviceContext->PSSetShaderResources(0, 1, &pEyeShaderResource[0]);
	// right eye
	m_DeviceContext->PSSetShaderResources(1, 1, &pEyeShaderResource[1]);
	m_DeviceContext->DrawIndexed(12, 0, 0);

	//m_SwapChain->SetFullscreenState(TRUE, NULL);

	if (pEyeShaderResource[0])
	{
		pEyeShaderResource[0]->Release();
		pEyeShaderResource[0] = nullptr;
	}

	if (pEyeShaderResource[1])
	{
		pEyeShaderResource[1]->Release();
		pEyeShaderResource[1] = nullptr;
	}

	if (pVEyeBuffer)
	{
		pVEyeBuffer->Release();
		pVEyeBuffer = nullptr;
	}

	if (pIEyeBuffer)
	{
		pIEyeBuffer->Release();
		pIEyeBuffer = nullptr;
	}

	if (pVBuffer)
	{
		pVBuffer->Release();
		pVBuffer = nullptr;
	}

	if (pIBuffer)
	{
		pIBuffer->Release();
		pIBuffer = nullptr;
	}

	if (ScreenShaderResource)
	{
		ScreenShaderResource->Release();
		ScreenShaderResource = nullptr;
	}

	if (pCBuffer)
	{
		pCBuffer->Release();
		pCBuffer = nullptr;
	}

	if (zbuffer)
	{
		zbuffer->Release();
		zbuffer = nullptr;
	}

	return DUPL_RETURN_SUCCESS;
}
#endif // VR_DESKTOP

//
// Initialize shaders for drawing to screen
//
DUPL_RETURN OUTPUTMANAGER::InitShaders()
{
    HRESULT hr;

    UINT Size = ARRAYSIZE(g_VS);
    hr = m_Device->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create vertex shader in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT NumElements = ARRAYSIZE(Layout);
	hr = m_Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create input layout in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
	}
	m_DeviceContext->IASetInputLayout(m_InputLayout);

#ifdef VR_DESKTOP
	Size = ARRAYSIZE(g_VS1);
	hr = m_Device->CreateVertexShader(g_VS1, Size, nullptr, &m_ScreenVertexShader);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create vertex shader in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
	}

	D3D11_INPUT_ELEMENT_DESC Layout1[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	
	NumElements = ARRAYSIZE(Layout1);
	hr = m_Device->CreateInputLayout(Layout, NumElements, g_VS1, Size, &m_ScreenInputLayout);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create input layout in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
	}
#endif // VR_DESKTOP

    Size = ARRAYSIZE(g_PS);
    hr = m_Device->CreatePixelShader(g_PS, Size, nullptr, &m_PixelShader);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create pixel shader in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

#ifdef VR_DESKTOP
	Size = ARRAYSIZE(g_PS1);
	hr = m_Device->CreatePixelShader(g_PS1, Size, nullptr, &m_ScreenPixelShader);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create pixel shader in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
	}
#endif // VR_DESKTOP

    return DUPL_RETURN_SUCCESS;
}

//
// Reset render target view
//
DUPL_RETURN OUTPUTMANAGER::MakeRTV()
{
    // Get backbuffer
    ID3D11Texture2D* BackBuffer = nullptr;
    HRESULT hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&BackBuffer));
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get backbuffer for making render target view in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Create a render target view
    hr = m_Device->CreateRenderTargetView(BackBuffer, nullptr, &m_RTV);
    BackBuffer->Release();
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to create render target view in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Set new render target
    m_DeviceContext->OMSetRenderTargets(1, &m_RTV, nullptr);

    return DUPL_RETURN_SUCCESS;
}

//
// Set new viewport
//
void OUTPUTMANAGER::SetViewPort(UINT Width, UINT Height)
{
    D3D11_VIEWPORT VP;
    VP.Width = static_cast<FLOAT>(Width);
    VP.Height = static_cast<FLOAT>(Height);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    m_DeviceContext->RSSetViewports(1, &VP);
}

//
// Resize swapchain
//
DUPL_RETURN OUTPUTMANAGER::ResizeSwapChain()
{
    if (m_RTV)
    {
        m_RTV->Release();
        m_RTV = nullptr;
    }

    RECT WindowRect;
    GetClientRect(m_WindowHandle, &WindowRect);
    UINT Width = WindowRect.right - WindowRect.left;
    UINT Height = WindowRect.bottom - WindowRect.top;

    // Resize swapchain
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    m_SwapChain->GetDesc(&SwapChainDesc);
    HRESULT hr = m_SwapChain->ResizeBuffers(SwapChainDesc.BufferCount, Width, Height, SwapChainDesc.BufferDesc.Format, SwapChainDesc.Flags);
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to resize swapchain buffers in OUTPUTMANAGER", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Make new render target view
    DUPL_RETURN Ret = MakeRTV();
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        return Ret;
    }

    // Set new viewport
    SetViewPort(Width, Height);

    return Ret;
}

//
// Releases all references
//
void OUTPUTMANAGER::CleanRefs()
{
    if (m_VertexShader)
    {
        m_VertexShader->Release();
        m_VertexShader = nullptr;
    }

    if (m_PixelShader)
    {
        m_PixelShader->Release();
        m_PixelShader = nullptr;
    }

    if (m_InputLayout)
    {
        m_InputLayout->Release();
        m_InputLayout = nullptr;
    }

    if (m_RTV)
    {
        m_RTV->Release();
        m_RTV = nullptr;
    }

    if (m_SamplerLinear)
    {
        m_SamplerLinear->Release();
        m_SamplerLinear = nullptr;
    }

    if (m_BlendState)
    {
        m_BlendState->Release();
        m_BlendState = nullptr;
    }

    if (m_DeviceContext)
    {
        m_DeviceContext->Release();
        m_DeviceContext = nullptr;
    }

    if (m_Device)
    {
        m_Device->Release();
        m_Device = nullptr;
    }

    if (m_SwapChain)
    {
        m_SwapChain->Release();
        m_SwapChain = nullptr;
    }

    if (m_SharedSurf)
    {
        m_SharedSurf->Release();
        m_SharedSurf = nullptr;
    }

    if (m_KeyMutex)
    {
        m_KeyMutex->Release();
        m_KeyMutex = nullptr;
    }

    if (m_Factory)
    {
        if (m_OcclusionCookie)
        {
            m_Factory->UnregisterOcclusionStatus(m_OcclusionCookie);
            m_OcclusionCookie = 0;
        }
        m_Factory->Release();
        m_Factory = nullptr;
    }

#ifdef VR_DESKTOP
	if (m_ScreenTex)
	{
		m_ScreenTex->Release();
		m_ScreenTex = nullptr;
	}

	if (m_ScreenVertexShader)
	{
		m_ScreenVertexShader->Release();
		m_ScreenVertexShader = nullptr;
	}

	if (m_ScreenPixelShader)
	{
		m_ScreenPixelShader->Release();
		m_ScreenPixelShader = nullptr;
	}

	if (m_ScreenInputLayout)
	{
		m_ScreenInputLayout->Release();
		m_ScreenInputLayout = nullptr;
	}

	if (m_BackSky[BACK])
	{
		m_BackSky[BACK]->Release();
		m_BackSky[BACK] = nullptr;
	}

	if (m_BackSky[FRONT])
	{
		m_BackSky[FRONT]->Release();
		m_BackSky[FRONT] = nullptr;
	}

	if (m_BackSky[LEFT])
	{
		m_BackSky[LEFT]->Release();
		m_BackSky[LEFT] = nullptr;
	}

	if (m_BackSky[RIGHT])
	{
		m_BackSky[RIGHT]->Release();
		m_BackSky[RIGHT] = nullptr;
	}

	if (m_BackSky[TOP])
	{
		m_BackSky[TOP]->Release();
		m_BackSky[TOP] = nullptr;
	}

	if (m_BackSky[BOTTOM])
	{
		m_BackSky[BOTTOM]->Release();
		m_BackSky[BOTTOM] = nullptr;
	}
#endif // VR_DESKTOP
}
