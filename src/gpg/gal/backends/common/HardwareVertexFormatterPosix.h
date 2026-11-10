#pragma once

// FAF addition, not in the shipped binary: what the D3D10-dialect hardware vertex formatters
// (backends/d3d10/HardwareVertexFormatterD3D10.cpp, Float16HardwareVertexFormatterD3D10.cpp) take from
// D3D10Private.h, without the Direct3D 10 SDK. The Linux client compiles those two formatters for the
// Vulkan backend (gpg/gal/MeshVertex.cpp hands them out for every API but Direct3D 9); Windows builds
// keep including D3D10Private.h and never see this header.

#if !defined(_WIN32)

#include <cstdint>

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Matrix.h"
#include "gpg/gal/MeshVertex.h"
#include "gpg/gal/backends/d3d10/Float16HardwareVertexFormatterD3D10.hpp"
#include "gpg/gal/backends/d3d10/HardwareVertexFormatterD3D10.hpp"

namespace gpg::gal
{
  namespace
  {
    /**
     * D3D10Private.h's copy of 0x0094D490: rows `(0,1,2)`, `(4,5,6)`, `(8,9,10)`, `(12,13,14)` of a
     * 4x4 matrix into four contiguous 3-float rows.
     */
    [[maybe_unused]] const Matrix* CopyMatrix4x3Rows(
      float* const outRow0, float* const outRow1, float* const outRow2, float* const outRow3, const Matrix& matrix
    )
    {
      float* const outRows[4] = {outRow0, outRow1, outRow2, outRow3};
      for (int row = 0; row < 4; ++row) {
        outRows[row][0] = matrix.r[row].x;
        outRows[row][1] = matrix.r[row].y;
        outRows[row][2] = matrix.r[row].z;
      }
      return &matrix;
    }
  } // namespace
} // namespace gpg::gal

#endif
