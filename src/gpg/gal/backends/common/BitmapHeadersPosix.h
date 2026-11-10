#pragma once

// FAF addition, not in the shipped binary: the two BMP file headers of <wingdi.h>, with their Windows
// layout (packed to 2 bytes, as the file has them), for the Linux client's frame dumps
// (DeviceVulkan.cpp's WriteBmp, FAF_GAL_DUMP_DIR). In a namespace of their own, so they never meet a
// global declaration of the same names; users bring them in with `using`.

#if !defined(_WIN32)

#include <cstdint>

namespace gpg::gal::posix
{
#pragma pack(push, 2)
  struct BITMAPFILEHEADER
  {
    std::uint16_t bfType;
    std::uint32_t bfSize;
    std::uint16_t bfReserved1;
    std::uint16_t bfReserved2;
    std::uint32_t bfOffBits;
  };
#pragma pack(pop)

  struct BITMAPINFOHEADER
  {
    std::uint32_t biSize;
    std::int32_t biWidth;
    std::int32_t biHeight;
    std::uint16_t biPlanes;
    std::uint16_t biBitCount;
    std::uint32_t biCompression;
    std::uint32_t biSizeImage;
    std::int32_t biXPelsPerMeter;
    std::int32_t biYPelsPerMeter;
    std::uint32_t biClrUsed;
    std::uint32_t biClrImportant;
  };

  static_assert(sizeof(BITMAPFILEHEADER) == 14U && sizeof(BITMAPINFOHEADER) == 40U, "the BMP headers' file layout");

  inline constexpr std::uint32_t kBiRgb = 0U; // BI_RGB
} // namespace gpg::gal::posix

#endif
