#include "moho/render/font/FontFaceLinux.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include "gpg/core/utils/Logging.h"
#include "moho/render/font/MsdfFontRegistry.h"

namespace moho
{
  namespace
  {
    // --- Which file a face name draws from --------------------------------------------------------------------

    /**
     * The files standing in for a face the game doesn't ship, best first: a file name searched for in the font
     * folders (Windows' own file when the Microsoft core fonts are installed, then a metric-compatible clone:
     * Liberation and the Chrome OS fonts Arimo/Tinos/Cousine match Arial, Times New Roman and Courier New advance
     * for advance, Carlito matches Calibri and Caladea Cambria), or "=Face" for another face's file. A face not
     * here, or with none of its files found, draws in Arial's place. (GDI's font mapper gives such a name, Ariel or
     * New Times Roman, its "MS Sans Serif" raster font, which has no file to use here; Arial is the game's own
     * sans face and the nearest to it.)
     */
    struct SFaceStandIns
    {
      const char* mFace; // lowercase
      std::vector<const char*> mCandidates;
    };

    const std::vector<SFaceStandIns>& FaceStandIns()
    {
      static const std::vector<SFaceStandIns> sStandIns = {
        // The game ships Arial (fonts/ARIAL*.TTF); these are for an install without that folder.
        {"arial", {"arial.ttf", "LiberationSans-Regular.ttf", "Arimo-Regular.ttf", "DejaVuSans.ttf"}},
        {"arial bold", {"arialbd.ttf", "LiberationSans-Bold.ttf", "Arimo-Bold.ttf", "DejaVuSans-Bold.ttf"}},
        {"arial gras", {"=Arial Bold"}}, // the French name, which the UI uses
        {"arial italic", {"ariali.ttf", "LiberationSans-Italic.ttf", "Arimo-Italic.ttf", "DejaVuSans-Oblique.ttf"}},
        {"arial bold italic",
         {"arialbi.ttf", "LiberationSans-BoldItalic.ttf", "Arimo-BoldItalic.ttf", "DejaVuSans-BoldOblique.ttf"}},
        {"arial narrow", {"arialn.ttf", "LiberationSansNarrow-Regular.ttf", "=Arial"}},
        {"arial narrow bold", {"arialnb.ttf", "LiberationSansNarrow-Bold.ttf", "=Arial Bold"}},
        {"arial black", {"ariblk.ttf", "=Arial Bold"}},
        {"arial rounded mt bold", {"arlrdbd.ttf", "=Arial Bold"}},
        {"microsoft sans serif", {"micross.ttf", "LiberationSans-Regular.ttf", "Arimo-Regular.ttf", "DejaVuSans.ttf"}},
        {"ms sans serif", {"=Microsoft Sans Serif"}},
        {"tahoma", {"tahoma.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf"}}, // Wine's tahoma.ttf when it's there
        {"tahoma bold", {"tahomabd.ttf", "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf"}},
        {"verdana", {"verdana.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf"}},
        {"verdana bold", {"verdanab.ttf", "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf"}},
        {"segoe ui", {"segoeui.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf"}},
        {"trebuchet ms", {"trebuc.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf"}},
        {"calibri", {"calibri.ttf", "Carlito-Regular.ttf", "LiberationSans-Regular.ttf"}},
        {"calibri bold", {"calibrib.ttf", "Carlito-Bold.ttf", "LiberationSans-Bold.ttf"}},
        {"cambria", {"cambria.ttc", "Caladea-Regular.ttf", "LiberationSerif-Regular.ttf"}},
        {"times new roman", {"times.ttf", "LiberationSerif-Regular.ttf", "Tinos-Regular.ttf", "DejaVuSerif.ttf"}},
        {"times new roman bold", {"timesbd.ttf", "LiberationSerif-Bold.ttf", "Tinos-Bold.ttf", "DejaVuSerif-Bold.ttf"}},
        {"georgia", {"georgia.ttf", "Gelasio-Regular.ttf", "LiberationSerif-Regular.ttf", "DejaVuSerif.ttf"}},
        {"courier new", {"cour.ttf", "LiberationMono-Regular.ttf", "Cousine-Regular.ttf", "DejaVuSansMono.ttf"}},
        {"courier new bold", {"courbd.ttf", "LiberationMono-Bold.ttf", "Cousine-Bold.ttf", "DejaVuSansMono-Bold.ttf"}},
        {"courier", {"=Courier New"}},
        {"lucida console", {"lucon.ttf", "LiberationMono-Regular.ttf", "DejaVuSansMono.ttf"}},
        {"consolas", {"consola.ttf", "LiberationMono-Regular.ttf", "DejaVuSansMono.ttf"}},
        // Not a Windows font (GDI maps it like any unknown name) unless the core fonts package put it there.
        {"andale mono", {"andalemo.ttf", "=Arial"}},
        {"impact", {"impact.ttf", "=Arial Black"}},
        {"comic sans ms", {"comic.ttf", "=Arial"}},
      };
      return sStandIns;
    }

    /** Faces for characters a face lacks (Chinese in a chat line, say), as GDI's font linking finds them. */
    constexpr const char* kCoverageFiles[] = {
      "DejaVuSans.ttf",           "NotoSans-Regular.ttf",       "NotoSansCJK-Regular.ttc", "NotoSansCJKsc-Regular.otf",
      "DroidSansFallbackFull.ttf", "DroidSansFallback.ttf",      "wqy-microhei.ttc",        "wqy-zenhei.ttc",
      "NanumGothic.ttf",
    };

    /** What any face falls back to when not even Arial's stand-ins are there. */
    constexpr const char* kLastResortFiles[] = {
      "DejaVuSans.ttf", "LiberationSans-Regular.ttf", "NotoSans-Regular.ttf", "FreeSans.ttf", "Vera.ttf",
    };

    [[nodiscard]] std::string Lower(std::string text)
    {
      std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
      });
      return text;
    }

    void AddSearchPath(std::vector<std::filesystem::path>& folders, const char* const list, const char* const suffix)
    {
      if (list == nullptr) {
        return;
      }
      std::string text(list);
      std::size_t start = 0u;
      while (start <= text.size()) {
        const std::size_t end = std::min(text.find(':', start), text.size());
        if (end > start) {
          std::filesystem::path folder(text.substr(start, end - start));
          if (suffix != nullptr) {
            folder /= suffix;
          }
          folders.push_back(std::move(folder));
        }
        start = end + 1u;
      }
    }

    /**
     * The font folders, searched in this order: FAF_FONT_DIRS (colon-separated, for a portable install), the
     * user's own (XDG_DATA_HOME/fonts, ~/.fonts), the XDG data folders' fonts (/usr/local/share/fonts and
     * /usr/share/fonts by default), and Wine's, whose tahoma.ttf matches Tahoma. No fontconfig: only file names
     * are matched, against the stand-in tables above.
     */
    [[nodiscard]] std::vector<std::filesystem::path> FontFolders()
    {
      std::vector<std::filesystem::path> folders;
      AddSearchPath(folders, std::getenv("FAF_FONT_DIRS"), nullptr);
      const char* const home = std::getenv("HOME");
      const char* const dataHome = std::getenv("XDG_DATA_HOME");
      if (dataHome != nullptr && *dataHome != '\0') {
        folders.push_back(std::filesystem::path(dataHome) / "fonts");
      } else if (home != nullptr && *home != '\0') {
        folders.push_back(std::filesystem::path(home) / ".local/share/fonts");
      }
      if (home != nullptr && *home != '\0') {
        folders.push_back(std::filesystem::path(home) / ".fonts");
      }
      const char* const dataDirs = std::getenv("XDG_DATA_DIRS");
      AddSearchPath(folders, dataDirs != nullptr && *dataDirs != '\0' ? dataDirs : "/usr/local/share:/usr/share", "fonts");
      folders.emplace_back("/usr/local/share/fonts");
      folders.emplace_back("/usr/share/fonts");
      folders.emplace_back("/usr/share/wine/fonts");
      return folders;
    }

    /** Every font file in the font folders by lowercase file name; the first folder to have a name keeps it. */
    [[nodiscard]] const std::map<std::string, std::string>& SystemFontFiles()
    {
      static const std::map<std::string, std::string> sFiles = [] {
        std::map<std::string, std::string> files;
        for (const std::filesystem::path& folder : FontFolders()) {
          std::error_code error;
          auto entry = std::filesystem::recursive_directory_iterator(
            folder,
            std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied,
            error
          );
          for (; !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
            if (entry.depth() > 6) {
              entry.disable_recursion_pending();
            }
            std::error_code typeError;
            if (!entry->is_regular_file(typeError)) {
              continue;
            }
            const std::string name = Lower(entry->path().filename().string());
            const std::string extension = Lower(entry->path().extension().string());
            if (extension == ".ttf" || extension == ".otf" || extension == ".ttc") {
              files.emplace(name, entry->path().string());
            }
          }
        }
        gpg::Logf("Font: %zu font files in the system font folders", files.size());
        return files;
      }();
      return sFiles;
    }

    [[nodiscard]] std::string FindSystemFont(const char* const fileName)
    {
      const auto& files = SystemFontFiles();
      const auto found = files.find(Lower(fileName));
      return found != files.end() ? found->second : std::string{};
    }

    [[nodiscard]] std::vector<char> ReadWholeFile(const std::string& path)
    {
      std::ifstream in(path, std::ios::binary);
      return std::vector<char>{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    /** `face` draws from the system file `path`. False when the file can't be read. */
    [[nodiscard]] bool AliasSystemFile(const std::string& face, const std::string& path)
    {
      const std::vector<char> bytes = ReadWholeFile(path);
      if (bytes.empty()) {
        return false;
      }
      MSDF_AddFaceAlias(face.c_str(), path.c_str(), bytes.data(), bytes.size());
      return true;
    }

    std::mutex gResolveLock; // the stand-in tables' walk; the registry has its own lock
    std::set<std::string> gWarnedFaces;

    [[nodiscard]] bool ResolveLocked(const std::string& face, SMsdfFontFileView& out, int depth);

    /** `face` draws in Arial's place, or failing that in any font there is. */
    [[nodiscard]] bool ResolveAsDefaultLocked(const std::string& face, SMsdfFontFileView& out, const int depth)
    {
      SMsdfFontFileView arial;
      if (Lower(face) != "arial" && ResolveLocked("Arial", arial, depth + 1) && MSDF_AddFaceAliasToFace(face.c_str(), "Arial")) {
        return MSDF_FindFontFile(face.c_str(), out);
      }
      for (const char* const fileName : kLastResortFiles) {
        const std::string path = FindSystemFont(fileName);
        if (!path.empty() && AliasSystemFile(face, path)) {
          return MSDF_FindFontFile(face.c_str(), out);
        }
      }
      for (const auto& [name, path] : SystemFontFiles()) {
        (void)name;
        if (AliasSystemFile(face, path)) {
          return MSDF_FindFontFile(face.c_str(), out);
        }
      }
      return false;
    }

    bool ResolveLocked(const std::string& face, SMsdfFontFileView& out, const int depth)
    {
      // The game's own fonts (and any face resolved before) by the names their files answer to.
      if (MSDF_FindFontFile(face.c_str(), out)) {
        return true;
      }
      if (depth > 4) {
        return false;
      }
      const std::string lower = Lower(face);
      const auto& standIns = FaceStandIns();
      const auto entry = std::find_if(standIns.begin(), standIns.end(), [&lower](const SFaceStandIns& standIn) {
        return lower == standIn.mFace;
      });
      if (entry != standIns.end()) {
        for (const char* const candidate : entry->mCandidates) {
          if (candidate[0] == '=') {
            SMsdfFontFileView other;
            if (ResolveLocked(candidate + 1, other, depth + 1) && MSDF_AddFaceAliasToFace(face.c_str(), candidate + 1)) {
              gpg::Logf("Font: \"%s\" draws as \"%s\" (%s)", face.c_str(), candidate + 1, other.mPath.c_str());
              return MSDF_FindFontFile(face.c_str(), out);
            }
            continue;
          }
          const std::string path = FindSystemFont(candidate);
          if (!path.empty() && AliasSystemFile(face, path)) {
            gpg::Logf("Font: \"%s\" draws from %s", face.c_str(), path.c_str());
            return MSDF_FindFontFile(face.c_str(), out);
          }
        }
      }
      if (gWarnedFaces.insert(lower).second) {
        gpg::Warnf(
          entry != standIns.end() ? "Font: no font file for \"%s\" nor its stand-ins; it draws in Arial's place"
                                  : "Font: no font file for \"%s\"; it draws in Arial's place",
          face.c_str()
        );
      }
      return ResolveAsDefaultLocked(face, out, depth);
    }

    // --- FreeType -----------------------------------------------------------------------------------------------

    // One library for every face here, used under this lock (FreeType wants one thread at a time per library).
    // The MSDF cook has its own libraries.
    std::mutex gFreeTypeLock;

    [[nodiscard]] FT_Library Library()
    {
      static FT_Library sLibrary = [] {
        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0) {
          gpg::Warnf("Font: FreeType didn't start; no text");
          return static_cast<FT_Library>(nullptr);
        }
        return library;
      }();
      return sLibrary;
    }

    [[nodiscard]] std::vector<std::uint8_t> LoadTable(const FT_Face face, const FT_ULong tag)
    {
      FT_ULong length = 0;
      if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0 || length == 0 || length > (16u << 20)) {
        return {};
      }
      std::vector<std::uint8_t> bytes(length);
      if (FT_Load_Sfnt_Table(face, tag, 0, bytes.data(), &length) != 0) {
        return {};
      }
      return bytes;
    }

    [[nodiscard]] std::uint32_t U16(const std::vector<std::uint8_t>& t, const std::size_t at)
    {
      return (static_cast<std::uint32_t>(t[at]) << 8) | t[at + 1];
    }

    [[nodiscard]] std::int32_t S16(const std::vector<std::uint8_t>& t, const std::size_t at)
    {
      return static_cast<std::int16_t>(static_cast<std::uint16_t>(U16(t, at)));
    }

    /**
     * The VDMX table's extents at `ppem` for a 1:1 device, which GDI takes for tmAscent and tmDescent over the OS/2
     * values when the font has them (Arial does: 15 and 3 at 16 pixels, where the OS/2 values round to 14 and 3).
     */
    [[nodiscard]] bool VdmxExtents(const FT_Face face, const int ppem, int& yMax, int& yMin)
    {
      const std::vector<std::uint8_t> t = LoadTable(face, TTAG_VDMX);
      if (t.size() < 6u) {
        return false;
      }
      const std::size_t ratios = U16(t, 4);
      if (6u + ratios * 6u > t.size()) {
        return false;
      }
      for (std::size_t i = 0; i < ratios; ++i) {
        const std::size_t ratio = 6u + 4u * i;
        // GDI passes over ratios for every glyph (character set 0) and takes the ANSI subset's: Tahoma's only
        // ratio is of the first kind, and GDI measures Tahoma by its OS/2 values (19 high at 16 pixels, not 21).
        if (t[ratio] == 0u) {
          continue;
        }
        const unsigned xRatio = t[ratio + 1];
        const unsigned yStart = t[ratio + 2];
        const unsigned yEnd = t[ratio + 3];
        if (!((xRatio == 0u && yStart == 0u && yEnd == 0u) || (yStart <= xRatio && xRatio <= yEnd))) {
          continue;
        }
        const std::size_t group = U16(t, 6u + 4u * ratios + 2u * i);
        if (group + 4u > t.size()) {
          return false;
        }
        const std::size_t records = U16(t, group);
        if (ppem < t[group + 2] || ppem > t[group + 3]) {
          return false;
        }
        for (std::size_t k = 0; k < records; ++k) {
          const std::size_t record = group + 4u + 6u * k;
          if (record + 6u > t.size()) {
            break;
          }
          const int height = static_cast<int>(U16(t, record));
          if (height > ppem) {
            break;
          }
          if (height == ppem) {
            yMax = S16(t, record + 2u);
            yMin = S16(t, record + 4u);
            return true;
          }
        }
        return false;
      }
      return false;
    }

    /** The hdmx table's whole-pixel advances at `ppem` by glyph index, which GDI measures with; empty without. */
    [[nodiscard]] std::vector<std::uint8_t> HdmxWidths(const FT_Face face, const int ppem)
    {
      const std::vector<std::uint8_t> t = LoadTable(face, TTAG_hdmx);
      if (t.size() < 8u || face->num_glyphs <= 0) {
        return {};
      }
      const std::int32_t records = S16(t, 2);
      const std::size_t recordSize = (static_cast<std::size_t>(U16(t, 4)) << 16) | U16(t, 6);
      const std::size_t glyphs = static_cast<std::size_t>(face->num_glyphs);
      if (records <= 0 || recordSize < glyphs + 2u) {
        return {};
      }
      for (std::int32_t i = 0; i < records; ++i) {
        const std::size_t record = 8u + static_cast<std::size_t>(i) * recordSize;
        if (record + 2u + glyphs > t.size()) {
          break;
        }
        if (t[record] == ppem) {
          return std::vector<std::uint8_t>(t.begin() + static_cast<std::ptrdiff_t>(record + 2u),
                                           t.begin() + static_cast<std::ptrdiff_t>(record + 2u + glyphs));
        }
      }
      return {};
    }

    [[nodiscard]] float RoundHalfUp(const double value)
    {
      return static_cast<float>(std::floor(value + 0.5));
    }

    struct SDesignExtents
    {
      double mWinAscent = 0.0;
      double mWinDescent = 0.0;
      double mHheaAscent = 0.0;
      double mHheaDescent = 0.0; // negative below the baseline, as in the table
      double mLineGap = 0.0;
    };

    [[nodiscard]] SDesignExtents DesignExtents(const FT_Face face)
    {
      SDesignExtents extents;
      const auto* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
      const auto* const hhea = static_cast<const TT_HoriHeader*>(FT_Get_Sfnt_Table(face, FT_SFNT_HHEA));
      extents.mHheaAscent = hhea != nullptr ? hhea->Ascender : face->ascender;
      extents.mHheaDescent = hhea != nullptr ? hhea->Descender : face->descender;
      extents.mLineGap = hhea != nullptr ? hhea->Line_Gap : 0.0;
      if (os2 != nullptr && os2->version != 0xFFFFu && os2->usWinAscent + os2->usWinDescent > 0) {
        extents.mWinAscent = os2->usWinAscent;
        extents.mWinDescent = os2->usWinDescent;
      } else {
        extents.mWinAscent = extents.mHheaAscent;
        extents.mWinDescent = -extents.mHheaDescent;
      }
      return extents;
    }

    /** GDI's TEXTMETRIC for the face at `ppem` pixels to the em (Wine's reading of it, which matches Windows'). */
    [[nodiscard]] SLinuxFontMetrics ComputeMetrics(const FT_Face face, const int ppem)
    {
      const SDesignExtents extents = DesignExtents(face);
      const double scale = static_cast<double>(ppem) / static_cast<double>(face->units_per_EM);
      float ascent = RoundHalfUp(extents.mWinAscent * scale);
      float descent = RoundHalfUp(extents.mWinDescent * scale);
      int vdmxMax = 0;
      int vdmxMin = 0;
      if (VdmxExtents(face, ppem, vdmxMax, vdmxMin)) {
        ascent = static_cast<float>(vdmxMax);
        descent = static_cast<float>(-vdmxMin);
      }
      SLinuxFontMetrics metrics;
      metrics.mAscent = ascent;
      metrics.mDescent = descent;
      metrics.mHeight = ascent + descent;
      metrics.mInternalLeading = metrics.mHeight - static_cast<float>(ppem);
      const double gap = extents.mLineGap -
        ((extents.mWinAscent + extents.mWinDescent) - (extents.mHheaAscent - extents.mHheaDescent));
      metrics.mExternalLeading = (std::max)(0.0f, RoundHalfUp(gap * scale));
      const auto* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
      const float average = os2 != nullptr && os2->version != 0xFFFFu && os2->xAvgCharWidth > 0
        ? RoundHalfUp(os2->xAvgCharWidth * scale)
        : RoundHalfUp(ppem * 0.5);
      metrics.mAveCharWidth = (std::max)(1.0f, average);
      metrics.mIsTrueType = FT_IS_SFNT(face);
      return metrics;
    }

    /** Opened on first use, shared by every face, with the lock held. */
    [[nodiscard]] const std::vector<FT_Face>& CoverageFaces()
    {
      static const std::vector<FT_Face> sFaces = [] {
        std::vector<FT_Face> faces;
        for (const char* const fileName : kCoverageFiles) {
          const std::string path = FindSystemFont(fileName);
          FT_Face face = nullptr;
          if (path.empty() || Library() == nullptr || FT_New_Face(Library(), path.c_str(), 0, &face) != 0) {
            continue;
          }
          if (!FT_IS_SCALABLE(face) || face->units_per_EM == 0) {
            FT_Done_Face(face);
            continue;
          }
          faces.push_back(face);
        }
        return faces;
      }();
      return sFaces;
    }
  } // namespace

  struct LinuxFontFace::Impl
  {
    FT_Face mFace = nullptr;
    int mPixelSize = 0;
    std::vector<std::uint8_t> mHdmx; // whole-pixel advances at mPixelSize by glyph index; empty without

    /**
     * Where a character's glyph comes from: this face, a coverage face that has it, or this face's .notdef box.
     * False for a control character no face has, which draws nothing and takes no room.
     */
    [[nodiscard]] bool FindGlyph(const std::uint32_t codepoint, FT_Face& outFace, FT_UInt& outGlyph) const
    {
      outFace = mFace;
      outGlyph = FT_Get_Char_Index(mFace, codepoint);
      if (outGlyph != 0u) {
        return true;
      }
      for (const FT_Face coverage : CoverageFaces()) {
        const FT_UInt glyph = FT_Get_Char_Index(coverage, codepoint);
        if (glyph != 0u) {
          outFace = coverage;
          outGlyph = glyph;
          return true;
        }
      }
      return codepoint >= 0x20u;
    }
  };

  LinuxFontFace::LinuxFontFace(Impl* const impl, std::string face, const SLinuxFontMetrics& metrics)
    : mImpl(impl)
    , mFace(std::move(face))
    , mMetrics(metrics)
  {}

  LinuxFontFace::~LinuxFontFace()
  {
    const std::lock_guard<std::mutex> lock(gFreeTypeLock);
    if (mImpl != nullptr) {
      if (mImpl->mFace != nullptr) {
        FT_Done_Face(mImpl->mFace);
      }
      delete mImpl;
      mImpl = nullptr;
    }
  }

  LinuxFontFace* LinuxFontFace::Open(const char* const faceName, const std::int32_t pixelSize)
  {
    const std::string face = faceName != nullptr ? faceName : "";
    SMsdfFontFileView file;
    {
      const std::lock_guard<std::mutex> lock(gResolveLock);
      if (!ResolveLocked(face, file, 0)) {
        static bool sWarned = false;
        if (!sWarned) {
          sWarned = true;
          gpg::Warnf("Font: no font file at all (neither the game's fonts/ folder nor system fonts); text won't draw");
        }
        return nullptr;
      }
    }

    const std::lock_guard<std::mutex> lock(gFreeTypeLock);
    const FT_Library library = Library();
    FT_Face ftFace = nullptr;
    if (library == nullptr ||
        FT_New_Memory_Face(library, file.mBytes, static_cast<FT_Long>(file.mByteCount), 0, &ftFace) != 0) {
      gpg::Warnf("Font: FreeType can't read %s (for \"%s\")", file.mPath.c_str(), face.c_str());
      return nullptr;
    }
    if (!FT_IS_SCALABLE(ftFace) || ftFace->units_per_EM == 0) {
      gpg::Warnf("Font: %s (for \"%s\") has no outlines", file.mPath.c_str(), face.c_str());
      FT_Done_Face(ftFace);
      return nullptr;
    }

    // CreateFont(-n): n pixels to the em. A positive GDI height (a negative size here) is the cell's height, the
    // Windows ascent and descent together; a height of 0 is GDI's default size, about this.
    int ppem = pixelSize;
    if (pixelSize < 0) {
      const SDesignExtents extents = DesignExtents(ftFace);
      const double cell = extents.mWinAscent + extents.mWinDescent;
      ppem = cell > 0.0 ? static_cast<int>(RoundHalfUp(-pixelSize * ftFace->units_per_EM / cell)) : -pixelSize;
    } else if (pixelSize == 0) {
      ppem = 16;
    }
    ppem = std::clamp(ppem, 1, 4096);
    if (FT_Set_Pixel_Sizes(ftFace, 0u, static_cast<FT_UInt>(ppem)) != 0) {
      gpg::Warnf("Font: %s (for \"%s\") can't be sized to %d pixels", file.mPath.c_str(), face.c_str(), ppem);
      FT_Done_Face(ftFace);
      return nullptr;
    }

    auto* const impl = new Impl();
    impl->mFace = ftFace;
    impl->mPixelSize = ppem;
    impl->mHdmx = HdmxWidths(ftFace, ppem);
    return new LinuxFontFace(impl, face, ComputeMetrics(ftFace, ppem));
  }

  float LinuxFontFace::Advance(const wchar_t character)
  {
    const std::lock_guard<std::mutex> lock(gFreeTypeLock);
    FT_Face face = nullptr;
    FT_UInt glyph = 0u;
    if (mImpl == nullptr || !mImpl->FindGlyph(static_cast<std::uint32_t>(character), face, glyph)) {
      return 0.0f;
    }
    if (face == mImpl->mFace && glyph < mImpl->mHdmx.size()) {
      return static_cast<float>(mImpl->mHdmx[glyph]);
    }
    FT_Fixed advance = 0;
    if (FT_Get_Advance(face, glyph, FT_LOAD_NO_SCALE, &advance) != 0) {
      return 0.0f;
    }
    return RoundHalfUp(static_cast<double>(advance) * mImpl->mPixelSize / face->units_per_EM);
  }

  void LinuxFontFace::Draw(
    const wchar_t character, std::uint32_t* const pixels, const int pitch, const int penX, const int baselineY,
    const int clipLeft, const int clipTop, const int clipRight, const int clipBottom
  )
  {
    const std::lock_guard<std::mutex> lock(gFreeTypeLock);
    FT_Face face = nullptr;
    FT_UInt glyph = 0u;
    if (mImpl == nullptr || !mImpl->FindGlyph(static_cast<std::uint32_t>(character), face, glyph)) {
      return;
    }
    if (face != mImpl->mFace && FT_Set_Pixel_Sizes(face, 0u, static_cast<FT_UInt>(mImpl->mPixelSize)) != 0) {
      return;
    }
    // Anti-aliased, with FreeType's light autohinting (vertical only, so shapes keep their widths): the bytecode
    // interpreter stays out of this FreeType (cmake/deps/freetype/config), as for a mod font's small sizes.
    if (FT_Load_Glyph(face, glyph, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
      return;
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    const int rows = static_cast<int>(bitmap.rows);
    const int width = static_cast<int>(bitmap.width);
    const int rowBytes = bitmap.pitch >= 0 ? bitmap.pitch : -bitmap.pitch;
    if (bitmap.buffer == nullptr || (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO)) {
      return;
    }
    const int grayLevels = bitmap.num_grays > 1 ? bitmap.num_grays - 1 : 255;
    for (int row = 0; row < rows; ++row) {
      const int y = baselineY - slot->bitmap_top + row;
      if (y < clipTop || y >= clipBottom) {
        continue;
      }
      const unsigned char* const source =
        bitmap.buffer + static_cast<std::ptrdiff_t>(bitmap.pitch >= 0 ? row : rows - 1 - row) * rowBytes;
      for (int column = 0; column < width; ++column) {
        const int x = penX + slot->bitmap_left + column;
        if (x < clipLeft || x >= clipRight) {
          continue;
        }
        std::uint32_t value = 0u;
        if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
          value = (source[column >> 3] & (0x80u >> (column & 7))) != 0u ? 255u : 0u;
        } else {
          value = static_cast<std::uint32_t>(source[column]) * 255u / static_cast<std::uint32_t>(grayLevels);
        }
        // A COLORREF gray, as GDI's white text on black leaves it: 0x00BBGGRR.
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch) + static_cast<std::size_t>(x)] =
          value | (value << 8) | (value << 16);
      }
    }
  }
} // namespace moho
