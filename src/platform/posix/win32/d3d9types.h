#pragma once
// Engine addition (Linux): the Direct3D 9 types the engine's renderer interface (gpg/gal, moho/render)
// still names, with Direct3D 9's values. Linux renders with Vulkan (gpg/gal/backends/vulkan); nothing
// here talks to Direct3D.

#include <cstdint>

#include <Windows.h>

using D3DCOLOR = DWORD;

struct D3DVIEWPORT9
{
  DWORD X;
  DWORD Y;
  DWORD Width;
  DWORD Height;
  float MinZ;
  float MaxZ;
};

struct D3DLOCKED_RECT
{
  INT Pitch;
  void* pBits;
};

enum D3DPRIMITIVETYPE
{
  D3DPT_POINTLIST = 1,
  D3DPT_LINELIST = 2,
  D3DPT_LINESTRIP = 3,
  D3DPT_TRIANGLELIST = 4,
  D3DPT_TRIANGLESTRIP = 5,
  D3DPT_TRIANGLEFAN = 6,
  D3DPT_FORCE_DWORD = 0x7fffffff
};

#define D3DLOCK_READONLY 0x00000010L
#define D3DLOCK_DISCARD 0x00002000L
#define D3DLOCK_NOOVERWRITE 0x00001000L
#define D3DLOCK_NOSYSLOCK 0x00000800L

#define D3D_OK S_OK
#define MAKE_D3DHRESULT(code) static_cast<HRESULT>(0x88760000u | static_cast<unsigned>(code))
#define D3DERR_DEVICELOST MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET MAKE_D3DHRESULT(2153)
#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
#define D3DERR_OUTOFVIDEOMEMORY MAKE_D3DHRESULT(380)
