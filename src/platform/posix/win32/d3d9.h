#pragma once
// Engine addition (Linux): the Direct3D 9 names the engine's client code keeps outside the Direct3D 9
// backend: its types (d3d9types.h) and the device interface, which only ever appears as an opaque
// pointer there (the Linux client renders with Vulkan).

#include <d3d9types.h>

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct IDirect3DTexture9;
using LPDIRECT3DDEVICE9 = IDirect3DDevice9*;
