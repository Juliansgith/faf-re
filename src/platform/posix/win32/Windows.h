#pragma once

// Engine addition (dedicated-server plan WPL.5): the subset of <Windows.h> (and of the
// MSVC CRT) that the shared simulation code uses, for the Linux build. cmake/FafLinux.cmake
// puts this directory on the include path and platform/PosixCompat.h includes it in every
// translation unit; Windows builds never see it. Types and constants have their Windows
// sizes and values. The functions are implemented on POSIX in src/platform/posix/Win32Shim.cpp
// with the Windows semantics the engine relies on: events (manual and auto reset),
// semaphores, mutexes and waits with timeouts on one kind of handle, files opened by
// CreateFile, interlocked operations on 32-bit values, the performance counter, the calling
// thread's id.

#if defined(_WIN32)
#error "platform/posix/win32/Windows.h is for builds without the Windows SDK"
#endif

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <cerrno>

#include <pthread.h>

#define WINAPI
#define CALLBACK
#define APIENTRY
#define WINBASEAPI

using VOID = void;
using BOOL = int;
using BYTE = std::uint8_t;
using UCHAR = unsigned char;
using CHAR = char;
using WCHAR = wchar_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using INT = int;
using LONG = std::int32_t;
using ULONG = std::uint32_t;
using HRESULT = std::int32_t;
using DWORD_PTR = std::uintptr_t;
using ULONG_PTR = std::uintptr_t;
using UINT_PTR = std::uintptr_t;
using INT_PTR = std::intptr_t;
using LONG_PTR = std::intptr_t;
using SIZE_T = std::size_t;
using HANDLE = void*;
using HMODULE = void*;
using HINSTANCE = void*;
using HLOCAL = void*;
using HWND = void*;
using HDC = void*;
using HFONT = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using DWORD64 = std::uint64_t;
using ULONGLONG = std::uint64_t;
using LONGLONG = std::int64_t;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define S_OK (static_cast<HRESULT>(0))
#define E_FAIL (static_cast<HRESULT>(0x80004005u))
#define E_INVALIDARG (static_cast<HRESULT>(0x80070057u))
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)

#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0x00000000u
#define WAIT_ABANDONED 0x00000080u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu
#define WAIT_IO_COMPLETION 0x000000C0u
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)))
#define MAX_PATH 260

#define ERROR_SUCCESS 0u
#define NO_ERROR 0u
#define ERROR_FILE_INVALID 1006u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_SUPPORTED 50u
#define ERROR_FILE_EXISTS 80u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_ENVVAR_NOT_FOUND 203u

struct tagRECT
{
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};
using RECT = tagRECT;
struct tagPOINT
{
  LONG x;
  LONG y;
};
using POINT = tagPOINT;
struct tagSIZE
{
  LONG cx;
  LONG cy;
};
using SIZE = tagSIZE;
using HBITMAP = void*;

struct FILETIME
{
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
};

union LARGE_INTEGER
{
  struct
  {
    DWORD LowPart;
    LONG HighPart;
  };
  std::int64_t QuadPart;
};

struct SYSTEM_INFO
{
  WORD wProcessorArchitecture;
  WORD wReserved;
  DWORD dwPageSize;
  LPVOID lpMinimumApplicationAddress;
  LPVOID lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask;
  DWORD dwNumberOfProcessors;
  DWORD dwProcessorType;
  DWORD dwAllocationGranularity;
  WORD wProcessorLevel;
  WORD wProcessorRevision;
};

struct OSVERSIONINFOW
{
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  WCHAR szCSDVersion[128];
};
#define VER_PLATFORM_WIN32_NT 2u
BOOL GetVersionExW(OSVERSIONINFOW* info);

// --- Synchronisation ---------------------------------------------------------------------
HANDLE CreateEventW(void* attributes, BOOL manualReset, BOOL initialState, const wchar_t* name);
HANDLE CreateEventA(void* attributes, BOOL manualReset, BOOL initialState, const char* name);
#define CreateEvent CreateEventW
HANDLE CreateSemaphoreW(void* attributes, LONG initialCount, LONG maximumCount, const wchar_t* name);
BOOL ReleaseSemaphore(HANDLE semaphore, LONG releaseCount, LONG* previousCount);
HANDLE CreateMutexW(void* attributes, BOOL initialOwner, const wchar_t* name);
HANDLE CreateMutexA(void* attributes, BOOL initialOwner, const char* name);
#define CreateMutex CreateMutexW
BOOL ReleaseMutex(HANDLE mutex);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds);
BOOL CloseHandle(HANDLE handle);

/// A recursive lock, as Windows' critical section.
struct CRITICAL_SECTION
{
  alignas(16) unsigned char storage[64];
  bool initialized;
};
void InitializeCriticalSection(CRITICAL_SECTION* section);
BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION* section, DWORD spinCount);
void EnterCriticalSection(CRITICAL_SECTION* section);
BOOL TryEnterCriticalSection(CRITICAL_SECTION* section);
void LeaveCriticalSection(CRITICAL_SECTION* section);
void DeleteCriticalSection(CRITICAL_SECTION* section);

using PVOID = void*;

/// One-time initialisation (InitOnceExecuteOnce's synchronous form).
struct INIT_ONCE
{
  int state; // 0 not run, 1 running, 2 done
};
#define INIT_ONCE_STATIC_INIT {0}
using PINIT_ONCE = INIT_ONCE*;
using PINIT_ONCE_FN = BOOL (*)(PINIT_ONCE initOnce, PVOID parameter, PVOID* context);
BOOL InitOnceExecuteOnce(PINIT_ONCE initOnce, PINIT_ONCE_FN function, PVOID parameter, PVOID* context);

/// The process's command line (/proc/self/cmdline, arguments joined by spaces).
const wchar_t* GetCommandLineW();

/// A slim reader/writer lock, as a POSIX rwlock (statically initialisable as SRWLOCK_INIT).
struct SRWLOCK
{
  pthread_rwlock_t lock;
};
#define SRWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER}
inline void InitializeSRWLock(SRWLOCK* const lock) noexcept
{
  (void)::pthread_rwlock_init(&lock->lock, nullptr);
}
inline void AcquireSRWLockExclusive(SRWLOCK* const lock) noexcept
{
  (void)::pthread_rwlock_wrlock(&lock->lock);
}
inline void ReleaseSRWLockExclusive(SRWLOCK* const lock) noexcept
{
  (void)::pthread_rwlock_unlock(&lock->lock);
}
inline void AcquireSRWLockShared(SRWLOCK* const lock) noexcept
{
  (void)::pthread_rwlock_rdlock(&lock->lock);
}
inline void ReleaseSRWLockShared(SRWLOCK* const lock) noexcept
{
  (void)::pthread_rwlock_unlock(&lock->lock);
}

// --- Threads, processes and time ---------------------------------------------------------
DWORD GetCurrentThreadId();
HANDLE GetCurrentThread();
HANDLE GetCurrentProcess();
DWORD GetCurrentProcessId();
void Sleep(DWORD milliseconds);
DWORD SleepEx(DWORD milliseconds, BOOL alertable);

// Asynchronous procedure calls: QueueUserAPC queues the call to the thread OpenThread named;
// that thread runs its queued calls, in order, in its next alertable wait (SleepEx,
// WaitForSingleObjectEx with `alertable`), which then returns WAIT_IO_COMPLETION.
#define STANDARD_RIGHTS_REQUIRED 0x000F0000u
#define SYNCHRONIZE 0x00100000u
#define THREAD_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFFu)
using PAPCFUNC = void (*)(ULONG_PTR parameter);
HANDLE OpenThread(DWORD desiredAccess, BOOL inheritHandle, DWORD threadId);
DWORD QueueUserAPC(PAPCFUNC function, HANDLE thread, ULONG_PTR data);

// Shared objects, through the dynamic loader.
HMODULE GetModuleHandleA(const char* moduleName);
HMODULE GetModuleHandleW(const wchar_t* moduleName); // null: the executable's load address
HMODULE LoadLibraryA(const char* fileName);
void* GetProcAddress(HMODULE module, const char* procName);
BOOL FreeLibrary(HMODULE module);
DWORD GetTickCount();
BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void GetSystemInfo(SYSTEM_INFO* info);
DWORD GetLastError();
void SetLastError(DWORD error);
BOOL GetProcessAffinityMask(HANDLE process, DWORD_PTR* processMask, DWORD_PTR* systemMask);
DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size);
void OutputDebugStringA(const char* text);
void OutputDebugStringW(const wchar_t* text);

#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_IDLE (-15)
BOOL SetThreadPriority(HANDLE thread, int priority);
DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);

enum LOGICAL_PROCESSOR_RELATIONSHIP
{
  RelationProcessorCore = 0,
  RelationNumaNode = 1,
  RelationCache = 2,
  RelationProcessorPackage = 3,
  RelationGroup = 4,
  RelationAll = 0xFFFF,
};
struct SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
/// Physical cores, NUMA nodes and caches (engine addition: RelationCache, one record per distinct cache)
/// from sysfs, as fixed-size records in group 0. With no topology to report: FALSE with
/// ERROR_NOT_SUPPORTED and a length of 0.
BOOL GetLogicalProcessorInformationEx(
  LOGICAL_PROCESSOR_RELATIONSHIP relation, SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* buffer, DWORD* length
);

// --- Interlocked operations on 32- and 64-bit values -------------------------------------
inline LONG InterlockedIncrement(volatile LONG* value) noexcept
{
  return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedDecrement(volatile LONG* value) noexcept
{
  return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedExchange(volatile LONG* target, const LONG value) noexcept
{
  return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedExchangeAdd(volatile LONG* target, const LONG value) noexcept
{
  return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedCompareExchange(volatile LONG* target, const LONG exchange, LONG comparand) noexcept
{
  __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return comparand;
}
inline std::int64_t InterlockedCompareExchange64(
  volatile std::int64_t* target, const std::int64_t exchange, std::int64_t comparand
) noexcept
{
  __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return comparand;
}

// --- Memory ------------------------------------------------------------------------------
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_TOP_DOWN 0x00100000u
#define MEM_FREE 0x00010000u
#define MEM_RELEASE 0x00008000u
#define MEM_PRIVATE 0x00020000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect);
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD freeType);

struct MEMORY_BASIC_INFORMATION
{
  LPVOID BaseAddress;
  LPVOID AllocationBase;
  DWORD AllocationProtect;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
};
/// Describes the page holding `address` from /proc/self/maps: committed private
/// read-write memory, read-only, or free.
SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);

HLOCAL LocalFree(HLOCAL memory);

// --- Files -------------------------------------------------------------------------------
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u
#define FILE_ATTRIBUTE_SYSTEM 0x00000004u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_FLAG_RANDOM_ACCESS 0x10000000u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define MOVEFILE_REPLACE_EXISTING 0x00000001u
BOOL ReplaceFileW(const wchar_t* replacedFile, const wchar_t* replacementFile, const wchar_t* backupFile, DWORD flags, void* exclude, void* reserved);
BOOL PathFileExistsW(const wchar_t* path);

// Heaps: every heap is the C library's (HeapSize is malloc_usable_size, at least the
// requested size).
#define HEAP_ZERO_MEMORY 0x00000008u
HANDLE GetProcessHeap();
HANDLE HeapCreate(DWORD options, SIZE_T initialSize, SIZE_T maximumSize);
void* HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes);
BOOL HeapFree(HANDLE heap, DWORD flags, void* memory);
SIZE_T HeapSize(HANDLE heap, DWORD flags, const void* memory);
BOOL CreateDirectoryA(const char* path, void* attributes);
/// `_fullpath`: the absolute, lexically normalised form of `path` (which need not exist).
char* _fullpath(char* absolutePath, const char* relativePath, std::size_t maxLength);
/// Plan WPR.23 (headless `--path-spelling`): from now on `file` gets one `requested<TAB>on disk` line
/// for each path found on disk only by ignoring case, each once.
void FAF_SetPathSpellingTrace(std::FILE* file);

// Code pages: the ANSI and OEM code pages are UTF-8 here.
#define CP_ACP 0u
#define CP_OEMCP 1u
#define CP_UTF8 65001u
int WideCharToMultiByte(UINT codePage, DWORD flags, const wchar_t* wide, int wideLength, char* multiByte, int multiByteLength, const char* defaultChar, BOOL* usedDefaultChar);
int MultiByteToWideChar(UINT codePage, DWORD flags, const char* multiByte, int multiByteLength, wchar_t* wide, int wideLength);
// The secure scanf takes a buffer size after each %s, %c and %[ argument; the shared code
// scans numbers only, where it is sscanf.
#define sscanf_s sscanf
DWORD GetModuleFileNameW(HMODULE module, wchar_t* fileName, DWORD size);
BOOL SetProcessAffinityMask(HANDLE process, DWORD_PTR processMask);

// The shell folders, as the XDG directories: CSIDL_LOCAL_APPDATA is $XDG_DATA_HOME
// (~/.local/share), CSIDL_APPDATA $XDG_CONFIG_HOME (~/.config), CSIDL_PERSONAL ~/Documents,
// CSIDL_DESKTOPDIRECTORY ~/Desktop; other folders fail with E_FAIL.
#define CSIDL_DESKTOP 0x0000
#define CSIDL_PERSONAL 0x0005
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_APPDATA 0x001A
#define CSIDL_LOCAL_APPDATA 0x001C
#define CSIDL_FLAG_CREATE 0x8000
HRESULT SHGetFolderPathW(HWND owner, int folder, HANDLE token, DWORD flags, wchar_t* path);

// Shell file operations are not available: SHFileOperation fails with ERROR_NOT_SUPPORTED and
// touches nothing.
#define FO_MOVE 0x0001u
#define FO_COPY 0x0002u
#define FO_DELETE 0x0003u
#define FO_RENAME 0x0004u
#define FOF_SILENT 0x0004u
#define FOF_NOCONFIRMATION 0x0010u
#define FOF_ALLOWUNDO 0x0040u
#define FOF_NOERRORUI 0x0400u
struct SHFILEOPSTRUCTA
{
  HWND hwnd;
  UINT wFunc;
  const char* pFrom;
  const char* pTo;
  WORD fFlags;
  BOOL fAnyOperationsAborted;
  void* hNameMappings;
  const char* lpszProgressTitle;
};
struct SHFILEOPSTRUCTW
{
  HWND hwnd;
  UINT wFunc;
  const wchar_t* pFrom;
  const wchar_t* pTo;
  WORD fFlags;
  BOOL fAnyOperationsAborted;
  void* hNameMappings;
  const wchar_t* lpszProgressTitle;
};
int SHFileOperationA(SHFILEOPSTRUCTA* operation);
int SHFileOperationW(SHFILEOPSTRUCTW* operation);
#define MOVEFILE_COPY_ALLOWED 0x00000002u
#define MOVEFILE_WRITE_THROUGH 0x00000008u

HANDLE CreateFileW(
  const wchar_t* path, DWORD desiredAccess, DWORD shareMode, void* security, DWORD creation, DWORD flags, HANDLE templateFile
);
HANDLE CreateFileA(
  const char* path, DWORD desiredAccess, DWORD shareMode, void* security, DWORD creation, DWORD flags, HANDLE templateFile
);
BOOL ReadFile(HANDLE file, void* buffer, DWORD bytesToRead, DWORD* bytesRead, void* overlapped);
BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytesToWrite, DWORD* bytesWritten, void* overlapped);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* newPosition, DWORD moveMethod);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG* distanceHigh, DWORD moveMethod);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size);
DWORD GetFileSize(HANDLE file, DWORD* sizeHigh);
BOOL FlushFileBuffers(HANDLE file);
// Read-only views of a whole file (the only mapping the shared code makes).
#define FILE_MAP_READ 0x0004u
HANDLE CreateFileMappingW(HANDLE file, void* attributes, DWORD protect, DWORD maximumSizeHigh, DWORD maximumSizeLow, const wchar_t* name);
void* MapViewOfFile(HANDLE mapping, DWORD desiredAccess, DWORD offsetHigh, DWORD offsetLow, std::size_t bytes);
BOOL UnmapViewOfFile(const void* view);
BOOL SetEndOfFile(HANDLE file);
DWORD GetFileAttributesW(const wchar_t* path);
DWORD GetFileAttributesA(const char* path);
BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags);
BOOL MoveFileW(const wchar_t* from, const wchar_t* to);
BOOL DeleteFileW(const wchar_t* path);
BOOL DeleteFileA(const char* path);
BOOL CreateDirectoryW(const wchar_t* path, void* security);
BOOL DosDateTimeToFileTime(WORD fatDate, WORD fatTime, FILETIME* fileTime);

// --- Messages ----------------------------------------------------------------------------
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100u
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200u
#define FORMAT_MESSAGE_FROM_STRING 0x00000400u
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800u
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000u
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000FFu
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((static_cast<WORD>(s) << 10) | static_cast<WORD>(p))
/// FORMAT_MESSAGE_FROM_SYSTEM: the text of `messageId` read as an errno value (strerror),
/// the only source the engine formats from.
DWORD FormatMessageW(DWORD flags, const void* source, DWORD messageId, DWORD languageId, wchar_t* buffer, DWORD size, va_list* arguments);
DWORD FormatMessageA(DWORD flags, const void* source, DWORD messageId, DWORD languageId, char* buffer, DWORD size, va_list* arguments);

// --- MSVC CRT names ----------------------------------------------------------------------
using __time64_t = std::int64_t;
inline __time64_t _time64(__time64_t* out) noexcept
{
  const __time64_t now = static_cast<__time64_t>(std::time(nullptr));
  if (out != nullptr) {
    *out = now;
  }
  return now;
}
char* _ctime64(const __time64_t* time) noexcept;

struct __timeb64
{
  __time64_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};
void _ftime64(__timeb64* out) noexcept;
inline int _ftime64_s(__timeb64* out) noexcept
{
  _ftime64(out);
  return 0;
}

inline int memcpy_s(void* const dest, const std::size_t destSize, const void* const src, const std::size_t count) noexcept
{
  if (count == 0) {
    return 0;
  }
  if (dest == nullptr || src == nullptr || destSize < count) {
    return 22; // EINVAL / ERANGE, as the CRT's checks
  }
  std::memcpy(dest, src, count);
  return 0;
}

inline int memmove_s(void* const dest, const std::size_t destSize, const void* const src, const std::size_t count) noexcept
{
  if (count == 0) {
    return 0;
  }
  if (dest == nullptr || src == nullptr || destSize < count) {
    return 22;
  }
  std::memmove(dest, src, count);
  return 0;
}

int fopen_s(std::FILE** file, const char* path, const char* mode) noexcept;
int _wfopen_s(std::FILE** file, const wchar_t* path, const wchar_t* mode) noexcept;

int strncpy_s(char* dest, std::size_t destSize, const char* src, std::size_t count) noexcept;
template <std::size_t N>
inline int strncpy_s(char (&dest)[N], const char* const src, const std::size_t count) noexcept
{
  return strncpy_s(dest, N, src, count);
}
int strcpy_s(char* dest, std::size_t destSize, const char* src) noexcept;
template <std::size_t N>
inline int strcpy_s(char (&dest)[N], const char* const src) noexcept
{
  return strcpy_s(dest, N, src);
}
int sprintf_s(char* buffer, std::size_t size, const char* format, ...) noexcept;
template <std::size_t N, class... Args>
inline int sprintf_s(char (&buffer)[N], const char* const format, Args... args) noexcept
{
  return sprintf_s(buffer, N, format, args...);
}
int vsprintf_s(char* buffer, std::size_t size, const char* format, va_list arguments) noexcept;
#define _TRUNCATE (static_cast<std::size_t>(-1))

int _memicmp(const void* lhs, const void* rhs, std::size_t count) noexcept;

using _locale_t = void*;
inline wint_t _towlower_l(const wint_t c, _locale_t) noexcept
{
  return std::towlower(c);
}

// x87 control word (float.h on Windows).
#define _MCW_PC 0x00030000u
#define _PC_64 0x00000000u
#define _PC_53 0x00010000u
#define _PC_24 0x00020000u
#define _MCW_RC 0x00000300u
#define _RC_NEAR 0x00000000u
#define _MCW_EM 0x0008001Fu
/// The x87 and SSE control state in the CRT's encoding; changes the x87 precision and
/// rounding fields of the calling thread's control word.
unsigned int _controlfp(unsigned int newValue, unsigned int mask) noexcept;

// --- More of the same, found by the Linux build ----------------------------------------
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_EXECUTE_WRITECOPY 0x80u
#define PAGE_GUARD 0x100u
#define MEM_DECOMMIT 0x00004000u
#define MEM_MAPPED 0x00040000u
#define MEM_IMAGE 0x01000000u

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

struct SYSTEMTIME
{
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
};
void GetSystemTimeAsFileTime(FILETIME* fileTime);
BOOL FileTimeToLocalFileTime(const FILETIME* fileTime, FILETIME* localFileTime);
BOOL FileTimeToSystemTime(const FILETIME* fileTime, SYSTEMTIME* systemTime);
void GetLocalTime(SYSTEMTIME* systemTime);
void GetSystemTime(SYSTEMTIME* systemTime);

enum GET_FILEEX_INFO_LEVELS
{
  GetFileExInfoStandard = 0,
  GetFileExMaxInfoLevel = 1,
};
struct WIN32_FILE_ATTRIBUTE_DATA
{
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
};
BOOL GetFileAttributesExW(const wchar_t* path, GET_FILEEX_INFO_LEVELS level, void* information);
BOOL GetFileAttributesExA(const char* path, GET_FILEEX_INFO_LEVELS level, void* information);

using KAFFINITY = std::uintptr_t;
struct GROUP_AFFINITY
{
  KAFFINITY Mask;
  WORD Group;
  WORD Reserved[3];
};
struct PROCESSOR_NUMBER
{
  WORD Group;
  BYTE Number;
  BYTE Reserved;
};
struct PROCESSOR_RELATIONSHIP
{
  BYTE Flags;
  BYTE EfficiencyClass;
  BYTE Reserved[20];
  WORD GroupCount;
  GROUP_AFFINITY GroupMask[1];
};
struct NUMA_NODE_RELATIONSHIP
{
  DWORD NodeNumber;
  BYTE Reserved[18];
  WORD GroupCount;
  GROUP_AFFINITY GroupMask;
};
enum PROCESSOR_CACHE_TYPE
{
  CacheUnified = 0,
  CacheInstruction = 1,
  CacheData = 2,
  CacheTrace = 3,
};
struct CACHE_RELATIONSHIP
{
  BYTE Level;
  BYTE Associativity;
  WORD LineSize;
  DWORD CacheSize;
  PROCESSOR_CACHE_TYPE Type;
  BYTE Reserved[18];
  WORD GroupCount;
  GROUP_AFFINITY GroupMask;
};
struct SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
{
  LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
  DWORD Size;
  union
  {
    PROCESSOR_RELATIONSHIP Processor;
    NUMA_NODE_RELATIONSHIP NumaNode;
    CACHE_RELATIONSHIP Cache;
  };
};
void GetCurrentProcessorNumberEx(PROCESSOR_NUMBER* number);
// One processor group (group 0, the CPUs the kernel numbers 0..63): the group affinity is
// the thread's affinity mask, and the ideal processor is a hint Linux has no call for.
BOOL SetThreadGroupAffinity(HANDLE thread, const GROUP_AFFINITY* affinity, GROUP_AFFINITY* previous);
BOOL SetThreadIdealProcessorEx(HANDLE thread, PROCESSOR_NUMBER* ideal, PROCESSOR_NUMBER* previous);
BOOL GetNumaProcessorNode(UCHAR processor, UCHAR* nodeNumber);
BOOL GetNumaNodeProcessorMask(UCHAR node, ULONGLONG* processorMask);

int _vsnprintf(char* buffer, std::size_t count, const char* format, va_list arguments) noexcept;
int _vsnwprintf(wchar_t* buffer, std::size_t count, const wchar_t* format, va_list arguments) noexcept;
int _snprintf(char* buffer, std::size_t count, const char* format, ...) noexcept;

// --- Engine addition (faf-neural-ai): what the native Linux client's shared code still calls --------
// Process, string and environment helpers (Win32Shim.cpp).
using errno_t = int;
using USHORT = unsigned short;
using COLORREF = DWORD;
ULONGLONG GetTickCount64();
errno_t getenv_s(std::size_t* requiredSize, char* buffer, std::size_t bufferSize, const char* name);
int _wcsicmp(const wchar_t* left, const wchar_t* right) noexcept;
DWORD GetModuleFileNameA(HMODULE module, char* fileName, DWORD size);
char* GetCommandLineA();
BOOL IsDebuggerPresent();
void DebugBreak();
// Windows, cursors and the screen: the handle is the client's SDL_Window* (platform/sdl/SdlWin32.cpp, in
// the Linux client only; the simulation's programs never call these).
using HCURSOR = void*;
using HICON = void*;
using HMONITOR = void*;
BOOL GetWindowRect(HWND window, RECT* rect);
BOOL GetClientRect(HWND window, RECT* rect);
BOOL SetCursorPos(int x, int y);
BOOL GetCursorPos(POINT* point);
BOOL ClientToScreen(HWND window, POINT* point);
BOOL ScreenToClient(HWND window, POINT* point);
BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags);
BOOL IsWindowVisible(HWND window);
#define HWND_TOP (reinterpret_cast<HWND>(0))
#define HWND_TOPMOST (reinterpret_cast<HWND>(-1))
#define HWND_NOTOPMOST (reinterpret_cast<HWND>(-2))
#define SWP_NOSIZE 0x0001u
#define SWP_NOMOVE 0x0002u
#define SWP_NOZORDER 0x0004u
#define SWP_NOACTIVATE 0x0010u
#define SWP_SHOWWINDOW 0x0040u
HWND SetCapture(HWND window);
BOOL ReleaseCapture();
HWND GetCapture();
BOOL ClipCursor(const RECT* rect);
int ShowCursor(BOOL show);
HCURSOR SetCursor(HCURSOR cursor);
BOOL IsIconic(HWND window);
#define HWND_BOTTOM (reinterpret_cast<HWND>(1))
#define SWP_NOOWNERZORDER 0x0200u
// Displays (SDL's): an HMONITOR holds an SDL_DisplayID.
struct MONITORINFO
{
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
};
#define MONITOR_DEFAULTTONULL 0x0u
#define MONITOR_DEFAULTTOPRIMARY 0x1u
#define MONITOR_DEFAULTTONEAREST 0x2u
#define MONITORINFOF_PRIMARY 0x1u
HMONITOR MonitorFromPoint(POINT point, DWORD flags);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);
// The system settings the client reads or sets: the screen saver (SDL's), the wheel's lines; every
// other one answers FALSE (nothing to read on Linux).
BOOL SystemParametersInfoW(UINT action, UINT uiParam, void* pvParam, UINT winIni);
#define SPI_GETSCREENSAVEACTIVE 0x0010u
#define SPI_SETSCREENSAVEACTIVE 0x0011u
#define SPI_GETFONTSMOOTHING 0x004Au
#define SPI_GETWHEELSCROLLLINES 0x0068u
#define SPI_GETFONTSMOOTHINGTYPE 0x200Au
#define SPI_GETFONTSMOOTHINGCONTRAST 0x200Cu
#define SPI_GETFONTSMOOTHINGORIENTATION 0x2012u
// Keyboard focus and key state: SetFocus on the SDL window focuses the wx window that last had the focus
// in it; GetKeyState / GetAsyncKeyState read SDL's keyboard and mouse state by virtual-key code.
HWND SetFocus(HWND window);
HWND GetFocus();
HWND GetForegroundWindow();
short GetKeyState(int virtualKey);
short GetAsyncKeyState(int virtualKey);
// The window messages platform/sdl builds from SDL's events (wx-lite hands them to the windows'
// MSWWindowProc, as wxWindows/MSW's window procedure did), with their Win32 values.
using WPARAM = UINT_PTR;
using LPARAM = LONG_PTR;
using LRESULT = LONG_PTR;
#define LOWORD(l) (static_cast<WORD>(static_cast<std::uintptr_t>(l) & 0xFFFFu))
#define HIWORD(l) (static_cast<WORD>((static_cast<std::uintptr_t>(l) >> 16) & 0xFFFFu))
#define MAKELONG(a, b)                                                                                     \
  (static_cast<LONG>(static_cast<std::uint32_t>(static_cast<WORD>(a)) |                                    \
                     (static_cast<std::uint32_t>(static_cast<WORD>(b)) << 16)))
#define MAKELPARAM(l, h) (static_cast<LPARAM>(static_cast<std::uint32_t>(MAKELONG(l, h))))
#define MAKEWPARAM(l, h) (static_cast<WPARAM>(static_cast<std::uint32_t>(MAKELONG(l, h))))
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<short>(LOWORD(lp))))
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<short>(HIWORD(lp))))
#define GET_WHEEL_DELTA_WPARAM(wp) (static_cast<short>(HIWORD(wp)))
#define GET_XBUTTON_WPARAM(wp) (HIWORD(wp))
#define WM_NULL 0x0000u
#define WM_CREATE 0x0001u
#define WM_DESTROY 0x0002u
#define WM_MOVE 0x0003u
#define WM_SIZE 0x0005u
#define WM_ACTIVATE 0x0006u
#define WM_SETFOCUS 0x0007u
#define WM_KILLFOCUS 0x0008u
#define WM_ENABLE 0x000Au
#define WM_PAINT 0x000Fu
#define WM_CLOSE 0x0010u
#define WM_QUIT 0x0012u
#define WM_ERASEBKGND 0x0014u
#define WM_SHOWWINDOW 0x0018u
#define WM_ACTIVATEAPP 0x001Cu
#define WM_SETCURSOR 0x0020u
#define WM_GETMINMAXINFO 0x0024u
#define WM_WINDOWPOSCHANGED 0x0047u
#define WM_DISPLAYCHANGE 0x007Eu
#define WM_NCHITTEST 0x0084u
#define WM_KEYFIRST 0x0100u
#define WM_KEYDOWN 0x0100u
#define WM_KEYUP 0x0101u
#define WM_CHAR 0x0102u
#define WM_DEADCHAR 0x0103u
#define WM_SYSKEYDOWN 0x0104u
#define WM_SYSKEYUP 0x0105u
#define WM_SYSCHAR 0x0106u
#define WM_KEYLAST 0x0109u
#define WM_COMMAND 0x0111u
#define WM_SYSCOMMAND 0x0112u
#define WM_TIMER 0x0113u
#define WM_MOUSEFIRST 0x0200u
#define WM_MOUSEMOVE 0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP 0x0202u
#define WM_LBUTTONDBLCLK 0x0203u
#define WM_RBUTTONDOWN 0x0204u
#define WM_RBUTTONUP 0x0205u
#define WM_RBUTTONDBLCLK 0x0206u
#define WM_MBUTTONDOWN 0x0207u
#define WM_MBUTTONUP 0x0208u
#define WM_MBUTTONDBLCLK 0x0209u
#define WM_MOUSEWHEEL 0x020Au
#define WM_XBUTTONDOWN 0x020Bu
#define WM_XBUTTONUP 0x020Cu
#define WM_XBUTTONDBLCLK 0x020Du
#define WM_MOUSEHWHEEL 0x020Eu
#define WM_MOUSELAST 0x020Eu
#define WM_CAPTURECHANGED 0x0215u
#define WM_ENTERSIZEMOVE 0x0231u
#define WM_EXITSIZEMOVE 0x0232u
#define WM_MOUSELEAVE 0x02A3u
#define WM_USER 0x0400u
#define WM_APP 0x8000u
#define SIZE_RESTORED 0u
#define SIZE_MINIMIZED 1u
#define SIZE_MAXIMIZED 2u
#define SIZE_MAXSHOW 3u
#define SIZE_MAXHIDE 4u
#define WA_INACTIVE 0u
#define WA_ACTIVE 1u
#define WA_CLICKACTIVE 2u
#define SC_SIZE 0xF000u
#define SC_MOVE 0xF010u
#define SC_MINIMIZE 0xF020u
#define SC_MAXIMIZE 0xF030u
#define SC_CLOSE 0xF060u
#define SC_KEYMENU 0xF100u
#define SC_RESTORE 0xF120u
#define SC_SCREENSAVE 0xF140u
#define SC_MONITORPOWER 0xF170u
#define HTCLIENT 1
#define MK_LBUTTON 0x0001u
#define MK_RBUTTON 0x0002u
#define MK_SHIFT 0x0004u
#define MK_CONTROL 0x0008u
#define MK_MBUTTON 0x0010u
#define MK_XBUTTON1 0x0020u
#define MK_XBUTTON2 0x0040u
#define XBUTTON1 0x0001u
#define XBUTTON2 0x0002u
#define WHEEL_DELTA 120
#define KF_EXTENDED 0x0100u
#define KF_ALTDOWN 0x2000u
#define KF_REPEAT 0x4000u
#define KF_UP 0x8000u
// Virtual-key codes (WinUser.h): the engine's key map and wx's key translation are keyed on them.
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_SLEEP 0x5F
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
// Shell and timer entry points the client's startup calls: ShellExecute opens documents and URLs with
// xdg-open; the multimedia timer resolution is a no-op (Linux timers are already fine-grained).
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_SHOWDEFAULT 10
HINSTANCE ShellExecuteW(HWND window, const wchar_t* operation, const wchar_t* file, const wchar_t* parameters,
                        const wchar_t* directory, int showCommand);
HINSTANCE ShellExecuteA(HWND window, const char* operation, const char* file, const char* parameters,
                        const char* directory, int showCommand);
#define TIMERR_NOERROR 0u
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);
union ULARGE_INTEGER
{
  struct
  {
    DWORD LowPart;
    DWORD HighPart;
  };
  std::uint64_t QuadPart;
};
/// Copies a file (backslashes read as slashes); `failIfExists` keeps an existing destination.
BOOL CopyFileW(const wchar_t* existingFile, const wchar_t* newFile, BOOL failIfExists);
/// Waits for one of `handles` or for window input (SDL events) at most `milliseconds`: WAIT_OBJECT_0 + i,
/// WAIT_OBJECT_0 + count for input, or WAIT_TIMEOUT.
DWORD MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, DWORD milliseconds, DWORD wakeMask, DWORD flags);
#define QS_ALLINPUT 0x04FFu
#define MWMO_ALERTABLE 0x0002u
#define MWMO_INPUTAVAILABLE 0x0004u
LONG CompareFileTime(const FILETIME* first, const FILETIME* second);
inline std::tm* _localtime64(const __time64_t* const time) noexcept
{
  const std::time_t value = static_cast<std::time_t>(*time);
  return std::localtime(&value);
}
inline std::tm* _gmtime64(const __time64_t* const time) noexcept
{
  const std::time_t value = static_cast<std::time_t>(*time);
  return std::gmtime(&value);
}
inline __time64_t _mktime64(std::tm* const brokenDown) noexcept
{
  return static_cast<__time64_t>(std::mktime(brokenDown));
}
inline std::FILE* _popen(const char* const command, const char* const mode) noexcept
{
  return ::popen(command, mode);
}
inline int _pclose(std::FILE* const stream) noexcept
{
  return ::pclose(stream);
}
inline int* _errno() noexcept
{
  return &errno;
}
