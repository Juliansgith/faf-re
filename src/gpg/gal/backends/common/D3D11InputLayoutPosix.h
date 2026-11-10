#pragma once

// FAF addition, not in the shipped binary: the Linux client has no <d3d11.h>, but the Vulkan backend
// reads the gal vertex formats from the Direct3D 11 input-layout tables (backends/d3d11/
// D3D11VertexLayouts.cpp, D3D-API-free data). These are the two <d3d11.h> names those tables use,
// with d3d11.h's layout and values. Windows builds include <d3d11.h> instead and never see this.

#if !defined(_WIN32)

#include <dxgiformat.h>

typedef enum D3D11_INPUT_CLASSIFICATION
{
  D3D11_INPUT_PER_VERTEX_DATA = 0,
  D3D11_INPUT_PER_INSTANCE_DATA = 1
} D3D11_INPUT_CLASSIFICATION;

typedef struct D3D11_INPUT_ELEMENT_DESC
{
  const char* SemanticName;
  unsigned int SemanticIndex;
  DXGI_FORMAT Format;
  unsigned int InputSlot;
  unsigned int AlignedByteOffset;
  D3D11_INPUT_CLASSIFICATION InputSlotClass;
  unsigned int InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;

#define D3D11_APPEND_ALIGNED_ELEMENT (0xffffffff)

#endif
