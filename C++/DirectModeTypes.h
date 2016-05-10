#ifndef _DIRECTMODETYPES_H_
#define _DIRECTMODETYPES_H_

#include <d3d11.h>
#include "nvapi.h"

#define VENDOR_ID	0x6A12

#define NUM_BACKBUFFERS 2

typedef enum _DMM_Status
{
	DMM_OK					= 0,
	DMM_FAIL				= 1,
	DMM_NOSUCH_DISPLAY		= 2,
	DMM_NOSUCH_MODE			= 3,
}DMM_Status;

typedef struct _TextureSwapChain
{
	ID3D11Texture2D *textures[NUM_BACKBUFFERS];		// buffers in swapchain, default # is 2;
	int		backbufIndex;
	int		frontbufIndex;
}TextureSwapChain;



#endif