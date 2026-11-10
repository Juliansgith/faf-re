#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "moho/render/font/MsdfFontCook.h"

namespace moho
{
  /**
   * FAF addition, not in the shipped binary (issue #306).
   *
   * Which font files the engine has, and their MSDF atlases. A font is cooked the first time text asks
   * for it, on one background thread at below-normal priority, and the atlas is kept in
   * `<local data>/fontcache` under a hash of the file and the cook settings, so a later start reads it
   * back. The folder sits beside the engine's cache folder, which `/purgecache` empties.
   *
   * Asking never waits: until an atlas is ready the caller draws the way it did before.
   */
  void MSDF_RegisterFontFile(const char* path, const void* fontBytes, std::size_t byteCount);

  /** `ren_ModFonts`: whether fonts from mods load at all. See docs/SWITCHES.md. */
  extern bool ren_ModFonts;

  /**
   * A font from a mod, which isn't trusted (issue #305): it has to pass `MSDF_CheckFontStructure`, it is
   * cooked within the mod limits of `MSDF_MakeModCookOptions`, its names are never read and it never reaches
   * GDI. A font that closed the game while cooking on two starts is refused from then on. Answers with
   * the private family name to draw it with (`mod:<hash>`), the same one for the same bytes, so a mod
   * can't take over `Arial` or any name the game uses. Empty, with one warning logged, when refused.
   */
  [[nodiscard]] std::string MSDF_RegisterModFont(const char* path, const void* fontBytes, std::size_t byteCount);

  /**
   * The atlas for a face name, as `CD3DFont::Create` gets it (any case). Null while it is cooking, and
   * for names no registered file answers to, such as a system font.
   */
  [[nodiscard]] std::shared_ptr<const SMsdfFontAtlas> MSDF_FindAtlas(const char* faceName);

#if !defined(_WIN32)
  /**
   * Linux client (no GDI): the registry also holds the files that `CD3DFont` measures and rasterizes through
   * FreeType (moho/render/font/FontFaceLinux.h). A registered file's bytes never change and live until the
   * process ends, so the pointer stays good.
   */
  struct SMsdfFontFileView
  {
    const std::uint8_t* mBytes = nullptr;
    std::size_t mByteCount = 0u;
    std::string mPath;
  };

  /**
   * The file a face name answers to (any case), its own names first, then the aliases below. Never a mod's
   * font: those are not trusted, and only the cook (with its limits) opens them.
   */
  [[nodiscard]] bool MSDF_FindFontFile(const char* faceName, SMsdfFontFileView& out);

  /**
   * `alias`, a face name no registered file answers to, draws from the file at `path` (registered now with
   * `fontBytes` unless a file of that path already is). A file's own names, even registered later, come first.
   */
  void MSDF_AddFaceAlias(const char* alias, const char* path, const void* fontBytes, std::size_t byteCount);

  /** `alias` draws from the file `faceName` answers to. False when none does. */
  [[nodiscard]] bool MSDF_AddFaceAliasToFace(const char* alias, const char* faceName);
#endif

  /** Lets the background thread finish its current font and stop. Safe to call more than once. */
  void MSDF_ShutdownFontRegistry();
} // namespace moho
