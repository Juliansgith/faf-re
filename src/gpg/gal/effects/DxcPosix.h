#pragma once

// FAF addition, not in the shipped binary: the Linux client's view of DXC (libdxcompiler.so).
//
// DXC's own Linux header (dxcapi.h) brings WinAdapter.h, whose Windows types (BOOL as bool, LONG as
// long, LARGE_INTEGER, GUID ...) clash with the POSIX <Windows.h> shim every Linux translation unit
// already has (platform/PosixCompat.h). So the few interfaces ShaderCompiler uses are declared here
// with DXC 1.9's Linux ABI: the same GUIDs as on Windows, and vtables in dxcapi.h's declaration
// order (WinAdapter's IUnknown has QueryInterface, AddRef, Release and no virtual destructor).
// Slots ShaderCompiler never calls keep their position with placeholder signatures.

#if !defined(_WIN32)

#include <cstddef>
#include <cstdint>

namespace gpg::gal::effects::dxc
{
  using Hresult = std::int32_t; // WinAdapter: typedef signed int HRESULT
  using Bool = bool;            // WinAdapter: typedef bool BOOL
  using Ulong = unsigned long;  // WinAdapter: typedef unsigned long ULONG

  struct Guid
  {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint8_t data4[8];
  };

  struct IUnknown
  {
    virtual Hresult QueryInterface(const Guid& iid, void** object) = 0;
    virtual Ulong AddRef() = 0;
    virtual Ulong Release() = 0;
  };

  struct IDxcBlob : IUnknown
  {
    static constexpr Guid kIid{0x8BA5FB08U, 0x5195U, 0x40e2U, {0xAC, 0x58, 0x0D, 0x98, 0x9C, 0x3A, 0x01, 0x02}};
    virtual void* GetBufferPointer() = 0;
    virtual std::size_t GetBufferSize() = 0;
  };

  struct IDxcBlobEncoding : IDxcBlob
  {
    static constexpr Guid kIid{0x7241d424U, 0x2646U, 0x4191U, {0x97, 0xc0, 0x98, 0xe9, 0x6e, 0x42, 0xfc, 0x68}};
    virtual Hresult GetEncoding(Bool* known, std::uint32_t* codePage) = 0;
  };

  struct IDxcBlobUtf8 : IDxcBlobEncoding
  {
    static constexpr Guid kIid{0x3DA636C9U, 0xBA71U, 0x4024U, {0xA3, 0x01, 0x30, 0xCB, 0xF1, 0x25, 0x30, 0x5B}};
    virtual const char* GetStringPointer() = 0;
    virtual std::size_t GetStringLength() = 0;
  };

  enum OutKind : std::uint32_t
  {
    DXC_OUT_NONE = 0U,
    DXC_OUT_OBJECT = 1U,
    DXC_OUT_ERRORS = 2U,
    DXC_OUT_HLSL = 6U,
    DXC_OUT_FORCE_DWORD = 0xFFFFFFFFU,
  };

  struct IDxcOperationResult : IUnknown
  {
    virtual Hresult GetStatus(Hresult* status) = 0;
    virtual Hresult GetResult(IDxcBlob** result) = 0;
    virtual Hresult GetErrorBuffer(IDxcBlobEncoding** errors) = 0;
  };

  struct IDxcResult : IDxcOperationResult
  {
    static constexpr Guid kIid{0x58346CDAU, 0xDDE7U, 0x4497U, {0x94, 0x61, 0x6F, 0x87, 0xAF, 0x5E, 0x06, 0x59}};
    virtual Bool HasOutput(OutKind kind) = 0;
    virtual Hresult GetOutput(OutKind kind, const Guid& iid, void** object, IUnknown** outputName) = 0;
    virtual std::uint32_t GetNumOutputs() = 0;
    virtual OutKind GetOutputByIndex(std::uint32_t index) = 0;
    virtual OutKind PrimaryOutput() = 0;
  };

  struct DxcBuffer
  {
    const void* Ptr;
    std::size_t Size;
    unsigned int Encoding;
  };

  inline constexpr unsigned int DXC_CP_UTF8 = 65001U;

  struct IDxcCompiler3 : IUnknown
  {
    static constexpr Guid kIid{0x228B4687U, 0x5A6AU, 0x4730U, {0x90, 0x0C, 0x97, 0x02, 0xB2, 0x20, 0x3F, 0x54}};
    virtual Hresult Compile(
      const DxcBuffer* source, const wchar_t** arguments, std::uint32_t argumentCount, IUnknown* includeHandler, const Guid& iid,
      void** result
    ) = 0;
    virtual Hresult Disassemble(const DxcBuffer* object, const Guid& iid, void** result) = 0;
  };

  struct IDxcVersionInfo : IUnknown
  {
    static constexpr Guid kIid{0xb04f5b50U, 0x2059U, 0x4f12U, {0xa8, 0xff, 0xa1, 0xe0, 0xcd, 0xe1, 0xcc, 0x7e}};
    virtual Hresult GetVersion(std::uint32_t* major, std::uint32_t* minor) = 0;
    virtual Hresult GetFlags(std::uint32_t* flags) = 0;
  };

  // {73e22d93-e6ce-47f3-b5bf-f0664f39c1b0}
  inline constexpr Guid kClsidDxcCompiler{0x73e22d93U, 0xe6ceU, 0x47f3U, {0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0}};
  // {6245d6af-66e0-48fd-80b4-4d271796748c} (CLSID_DxcUtils is CLSID_DxcLibrary)
  inline constexpr Guid kClsidDxcUtils{0x6245d6afU, 0x66e0U, 0x48fdU, {0x80, 0xb4, 0x4d, 0x27, 0x17, 0x96, 0x74, 0x8c}};

  using CreateInstanceProc = Hresult (*)(const Guid& clsid, const Guid& iid, void** object);

  /** An owning interface pointer: what Microsoft::WRL::ComPtr does for ShaderCompiler on Windows. */
  template <class T>
  class Ref
  {
  public:
    Ref() noexcept = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { Reset(); }

    void Reset() noexcept
    {
      if (mPointer != nullptr) {
        mPointer->Release();
        mPointer = nullptr;
      }
    }

    [[nodiscard]] T* Get() const noexcept { return mPointer; }
    T* operator->() const noexcept { return mPointer; }
    explicit operator bool() const noexcept { return mPointer != nullptr; }

    /** For a creating call: releases the current object and returns the slot as void**. */
    [[nodiscard]] void** Put() noexcept
    {
      Reset();
      return reinterpret_cast<void**>(&mPointer);
    }

  private:
    T* mPointer = nullptr;
  };
} // namespace gpg::gal::effects::dxc

#endif
