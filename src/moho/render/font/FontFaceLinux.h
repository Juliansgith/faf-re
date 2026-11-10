#pragma once

// Engine addition (faf-neural-ai), Linux client only: what CD3DFont takes from GDI on Windows, from FreeType.
//
// A face name ("Arial", "Arial Bold", "Zeroes Three", "Tahoma") resolves to a font file: the game's own fonts
// first (the retail fonts/ folder, mounted at /fonts/ and registered by REN_Init), then system fonts that stand
// in for Windows' own (Liberation, DejaVu, Carlito; found by searching the usual font folders, no fontconfig).
// The file is registered with the MSDF registry under that face name too, so the face's text draws from the
// file's atlas (ren_MsdfText) exactly as on Windows. Metrics follow GDI's rules for CreateFont(-pixels): the
// OS/2 Windows ascent and descent (or the VDMX table's values, which GDI prefers), the hhea line gap as GDI
// turns it into external leading, and whole-pixel advances (the hdmx table's where it has the size).

#include <cstdint>
#include <string>

#if defined(_WIN32)
#error "FontFaceLinux.h is for the Linux client; Windows measures text through GDI"
#endif

namespace moho
{
  /** The TEXTMETRIC fields CD3DFont keeps, as GDI would report them. */
  struct SLinuxFontMetrics
  {
    float mHeight = 0.0f;
    float mAscent = 0.0f;
    float mDescent = 0.0f;
    float mInternalLeading = 0.0f;
    float mExternalLeading = 0.0f;
    float mAveCharWidth = 0.0f;
    bool mIsTrueType = false;
  };

  /** One face at one size: what an HFONT and its memory DC are to CD3DFont on Windows. */
  class LinuxFontFace
  {
  public:
    /**
     * `faceName` at `pixelSize` pixels to the em, as GDI's CreateFont(-pixelSize, ..., faceName). Never fails for
     * a name no file answers to (that draws in Arial's place, with one warning per face); null only when there is
     * no font file at all.
     */
    [[nodiscard]] static LinuxFontFace* Open(const char* faceName, std::int32_t pixelSize);

    ~LinuxFontFace();
    LinuxFontFace(const LinuxFontFace&) = delete;
    LinuxFontFace& operator=(const LinuxFontFace&) = delete;

    [[nodiscard]] const SLinuxFontMetrics& Metrics() const noexcept { return mMetrics; }

    /** The face name the file was found for (what the MSDF registry knows it by). */
    [[nodiscard]] const std::string& Face() const noexcept { return mFace; }

    /** What GDI's DrawText(DT_CALCRECT) measures for one character: whole pixels. */
    [[nodiscard]] float Advance(wchar_t character);

    /**
     * Draws `character` white on black into a 32-bit scratch image of GDI COLORREFs (0x00BBGGRR, gray), its pen
     * at (`penX`, `baselineY`), writing only inside [clipLeft, clipRight) x [clipTop, clipBottom).
     */
    void Draw(
      wchar_t character, std::uint32_t* pixels, int pitch, int penX, int baselineY, int clipLeft, int clipTop,
      int clipRight, int clipBottom
    );

    struct Impl;

  private:
    LinuxFontFace(Impl* impl, std::string face, const SLinuxFontMetrics& metrics);

    Impl* mImpl = nullptr;
    std::string mFace;
    SLinuxFontMetrics mMetrics;
  };
} // namespace moho
