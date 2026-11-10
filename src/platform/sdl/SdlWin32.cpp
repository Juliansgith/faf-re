// Engine addition (faf-neural-ai): the Win32 window, cursor, input and shell calls the native Linux
// client's shared code makes (declared in platform/posix/win32/Windows.h), on SDL 3. An HWND is the
// SDL_Window* of a wx-lite top-level window (wxWindow::GetHandle); screen coordinates are SDL's global
// ones. Client-only: the simulation's programs never link this.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/sdl/SdlPrivate.h"

#include <Windows.h>

extern char** environ;

namespace
{
  SDL_Window* WindowOf(const HWND window)
  {
    return wxlite::IsVideoReady() ? wxlite::NativeWindow(window) : nullptr;
  }

  /// The client area's origin on the screen.
  bool ClientOrigin(SDL_Window* const window, int& x, int& y)
  {
    x = 0;
    y = 0;
    return window != nullptr && SDL_GetWindowPosition(window, &x, &y);
  }

  std::atomic<int> sCursorDisplayCount{0};
  bool sCursorHasImage = true;

  void ApplyCursorVisibility()
  {
    if (!wxlite::IsVideoReady()) {
      return;
    }
    if (sCursorDisplayCount.load() >= 0 && sCursorHasImage) {
      SDL_ShowCursor();
    } else {
      SDL_HideCursor();
    }
  }

  std::string NarrowFromWide(const wchar_t* const text)
  {
    return text != nullptr ? wxString(text).mb_str() : std::string();
  }
} // namespace

// --- windows ----------------------------------------------------------------------------------------
BOOL GetClientRect(const HWND window, RECT* const rect)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  if (sdlWindow == nullptr || rect == nullptr) {
    return FALSE;
  }
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(sdlWindow, &width, &height);
  *rect = RECT{0, 0, width, height};
  return TRUE;
}

BOOL GetWindowRect(const HWND window, RECT* const rect)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  if (sdlWindow == nullptr || rect == nullptr) {
    return FALSE; // GetWindowRect(NULL) fails on Windows too
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  (void)ClientOrigin(sdlWindow, x, y);
  SDL_GetWindowSize(sdlWindow, &width, &height);
  int top = 0;
  int left = 0;
  int bottom = 0;
  int right = 0;
  if (!SDL_GetWindowBordersSize(sdlWindow, &top, &left, &bottom, &right)) {
    top = left = bottom = right = 0;
  }
  *rect = RECT{x - left, y - top, x + width + right, y + height + bottom};
  return TRUE;
}

BOOL ClientToScreen(const HWND window, POINT* const point)
{
  int x = 0;
  int y = 0;
  if (point == nullptr || !ClientOrigin(WindowOf(window), x, y)) {
    return FALSE;
  }
  point->x += x;
  point->y += y;
  return TRUE;
}

BOOL ScreenToClient(const HWND window, POINT* const point)
{
  int x = 0;
  int y = 0;
  if (point == nullptr || !ClientOrigin(WindowOf(window), x, y)) {
    return FALSE;
  }
  point->x -= x;
  point->y -= y;
  return TRUE;
}

BOOL SetWindowPos(const HWND window, const HWND insertAfter, const int x, const int y, const int width,
                  const int height, const UINT flags)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  if (sdlWindow == nullptr) {
    return FALSE;
  }
  if ((flags & SWP_NOZORDER) == 0) {
    if (insertAfter == HWND_TOPMOST) {
      (void)SDL_SetWindowAlwaysOnTop(sdlWindow, true);
    } else if (insertAfter == HWND_NOTOPMOST) {
      (void)SDL_SetWindowAlwaysOnTop(sdlWindow, false);
    } else if (insertAfter == HWND_TOP && (flags & SWP_NOACTIVATE) == 0) {
      (void)SDL_RaiseWindow(sdlWindow);
    }
  }
  // the size and place go through the wx window, so it reports WM_MOVE / WM_SIZE as Windows did
  wxTopLevelWindow* const frame = wxlite::FindTopLevel(sdlWindow);
  const int newX = (flags & SWP_NOMOVE) != 0 ? -1 : x;
  const int newY = (flags & SWP_NOMOVE) != 0 ? -1 : y;
  const int newWidth = (flags & SWP_NOSIZE) != 0 ? -1 : width;
  const int newHeight = (flags & SWP_NOSIZE) != 0 ? -1 : height;
  if (frame != nullptr) {
    frame->SetSize(newX, newY, newWidth, newHeight);
  } else {
    if (newX != -1) {
      (void)SDL_SetWindowPosition(sdlWindow, newX, newY);
    }
    if (newWidth != -1) {
      (void)SDL_SetWindowSize(sdlWindow, newWidth, newHeight);
    }
  }
  if ((flags & SWP_SHOWWINDOW) != 0) {
    if (frame != nullptr) {
      frame->Show(true);
    } else {
      (void)SDL_ShowWindow(sdlWindow);
    }
  }
  return TRUE;
}

BOOL IsWindowVisible(const HWND window)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  if (sdlWindow == nullptr) {
    return FALSE;
  }
  const SDL_WindowFlags flags = SDL_GetWindowFlags(sdlWindow);
  return (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0 ? TRUE : FALSE;
}

BOOL IsIconic(const HWND window)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  return sdlWindow != nullptr && (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MINIMIZED) != 0 ? TRUE : FALSE;
}

HWND GetForegroundWindow()
{
  return wxlite::IsVideoReady() ? static_cast<HWND>(SDL_GetKeyboardFocus()) : nullptr;
}

HWND SetFocus(const HWND window)
{
  wxWindow* const previous = wxWindow::FindFocus();
  const HWND previousHandle = previous != nullptr ? reinterpret_cast<HWND>(previous->GetHandle()) : nullptr;
  wxTopLevelWindow* const frame = wxlite::FindTopLevel(WindowOf(window));
  if (frame == nullptr) {
    return previousHandle;
  }
  // Every wx window of a frame shares its SDL window, so the handle names the frame: the focus stays
  // where it is when it is already inside, else it goes to the child that last had it (the viewport).
  if (previous != nullptr && previous->GetTopLevelParent() == frame) {
    return previousHandle;
  }
  wxWindow* target = frame->m_lastFocus;
  if (target == nullptr) {
    for (wxWindow* const child : frame->GetChildren()) {
      if (!child->IsTopLevel() && child->AcceptsFocus()) {
        target = child;
        break;
      }
    }
  }
  (target != nullptr ? target : static_cast<wxWindow*>(frame))->SetFocus();
  return previousHandle;
}

HWND GetFocus()
{
  wxWindow* const focus = wxWindow::FindFocus();
  return focus != nullptr ? reinterpret_cast<HWND>(focus->GetHandle()) : nullptr;
}

// --- mouse and cursor ---------------------------------------------------------------------------------
BOOL GetCursorPos(POINT* const point)
{
  if (point == nullptr || !wxlite::IsVideoReady()) {
    return FALSE;
  }
  float x = 0.0f;
  float y = 0.0f;
  // through the window under the mouse where the system has no global position (Wayland)
  if (SDL_Window* const focus = SDL_GetMouseFocus(); focus != nullptr) {
    (void)SDL_GetMouseState(&x, &y);
    int originX = 0;
    int originY = 0;
    (void)ClientOrigin(focus, originX, originY);
    point->x = originX + static_cast<LONG>(std::floor(x));
    point->y = originY + static_cast<LONG>(std::floor(y));
    return TRUE;
  }
  (void)SDL_GetGlobalMouseState(&x, &y);
  point->x = static_cast<LONG>(std::floor(x));
  point->y = static_cast<LONG>(std::floor(y));
  return TRUE;
}

BOOL SetCursorPos(const int x, const int y)
{
  if (!wxlite::IsVideoReady()) {
    return FALSE;
  }
  if (SDL_Window* const focus = SDL_GetMouseFocus(); focus != nullptr) {
    int originX = 0;
    int originY = 0;
    (void)ClientOrigin(focus, originX, originY);
    SDL_WarpMouseInWindow(focus, static_cast<float>(x - originX), static_cast<float>(y - originY));
    return TRUE;
  }
  return SDL_WarpMouseGlobal(static_cast<float>(x), static_cast<float>(y)) ? TRUE : FALSE;
}

HWND SetCapture(const HWND window)
{
  const HWND previous = GetCapture();
  if (wxTopLevelWindow* const frame = wxlite::FindTopLevel(WindowOf(window)); frame != nullptr) {
    // the handle names the frame: the window under the mouse in it (or the focused one) captures
    wxWindow* target = wxlite::MouseOverWindow();
    if (target == nullptr || target->GetTopLevelParent() != frame) {
      target = wxWindow::FindFocus();
    }
    if (target == nullptr || target->GetTopLevelParent() != frame) {
      target = frame;
    }
    target->CaptureMouse();
  }
  return previous;
}

BOOL ReleaseCapture()
{
  if (wxWindow* const capture = wxWindow::GetCapture(); capture != nullptr) {
    capture->ReleaseMouse();
  }
  if (wxlite::IsVideoReady()) {
    (void)SDL_CaptureMouse(false);
  }
  return TRUE;
}

HWND GetCapture()
{
  wxWindow* const capture = wxWindow::GetCapture();
  return capture != nullptr ? reinterpret_cast<HWND>(capture->GetHandle()) : nullptr;
}

BOOL ClipCursor(const RECT* const rect)
{
  if (!wxlite::IsVideoReady()) {
    return FALSE;
  }
  // SDL confines the mouse per window: the rectangle is applied to the frame it falls in, and every
  // frame is freed by a null one
  for (wxWindow* const window : wxTopLevelWindows) {
    SDL_Window* const sdlWindow = wxlite::NativeWindowOf(window);
    if (sdlWindow == nullptr) {
      continue;
    }
    if (rect == nullptr) {
      (void)SDL_SetWindowMouseRect(sdlWindow, nullptr);
      continue;
    }
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    (void)ClientOrigin(sdlWindow, x, y);
    SDL_GetWindowSize(sdlWindow, &width, &height);
    const int left = std::max(static_cast<int>(rect->left) - x, 0);
    const int top = std::max(static_cast<int>(rect->top) - y, 0);
    const int right = std::min(static_cast<int>(rect->right) - x, width);
    const int bottom = std::min(static_cast<int>(rect->bottom) - y, height);
    if (right <= left || bottom <= top) {
      continue;
    }
    const SDL_Rect area{left, top, right - left, bottom - top};
    (void)SDL_SetWindowMouseRect(sdlWindow, &area);
  }
  return TRUE;
}

int ShowCursor(const BOOL show)
{
  const int count = show != FALSE ? ++sCursorDisplayCount : --sCursorDisplayCount;
  ApplyCursorVisibility();
  return count;
}

HCURSOR SetCursor(const HCURSOR cursor)
{
  // an HCURSOR is an SDL_Cursor* (gal/backends/common/WindowsCursor.cpp makes them); a null one
  // hides the cursor until the next one, as on Windows
  if (!wxlite::IsVideoReady()) {
    return nullptr;
  }
  HCURSOR const previous = sCursorHasImage ? static_cast<HCURSOR>(SDL_GetCursor()) : nullptr;
  sCursorHasImage = cursor != nullptr;
  if (cursor != nullptr) {
    (void)SDL_SetCursor(static_cast<SDL_Cursor*>(cursor));
  }
  ApplyCursorVisibility();
  return previous;
}

// --- keyboard -----------------------------------------------------------------------------------------
short GetKeyState(const int virtualKey)
{
  return wxlite::KeyState(virtualKey);
}

short GetAsyncKeyState(const int virtualKey)
{
  return wxlite::KeyState(virtualKey) < 0 ? static_cast<short>(0x8000) : 0;
}

// --- displays -----------------------------------------------------------------------------------------
HMONITOR MonitorFromPoint(const POINT point, const DWORD flags)
{
  if (!wxlite::IsVideoReady()) {
    return nullptr;
  }
  const SDL_Point at{static_cast<int>(point.x), static_cast<int>(point.y)};
  SDL_DisplayID display = SDL_GetDisplayForPoint(&at);
  if (display == 0 && flags != MONITOR_DEFAULTTONULL) {
    display = SDL_GetPrimaryDisplay();
  }
  return display != 0 ? reinterpret_cast<HMONITOR>(static_cast<std::uintptr_t>(display)) : nullptr;
}

HMONITOR MonitorFromWindow(const HWND window, const DWORD flags)
{
  SDL_Window* const sdlWindow = WindowOf(window);
  SDL_DisplayID display = sdlWindow != nullptr ? SDL_GetDisplayForWindow(sdlWindow) : 0;
  if (display == 0 && flags != MONITOR_DEFAULTTONULL && wxlite::IsVideoReady()) {
    display = SDL_GetPrimaryDisplay();
  }
  return display != 0 ? reinterpret_cast<HMONITOR>(static_cast<std::uintptr_t>(display)) : nullptr;
}

BOOL GetMonitorInfoW(const HMONITOR monitor, MONITORINFO* const info)
{
  if (monitor == nullptr || info == nullptr || !wxlite::IsVideoReady()) {
    return FALSE;
  }
  const auto display = static_cast<SDL_DisplayID>(reinterpret_cast<std::uintptr_t>(monitor));
  SDL_Rect bounds{};
  if (!SDL_GetDisplayBounds(display, &bounds)) {
    return FALSE;
  }
  SDL_Rect usable = bounds;
  (void)SDL_GetDisplayUsableBounds(display, &usable);
  info->rcMonitor = RECT{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
  info->rcWork = RECT{usable.x, usable.y, usable.x + usable.w, usable.y + usable.h};
  info->dwFlags = display == SDL_GetPrimaryDisplay() ? MONITORINFOF_PRIMARY : 0u;
  return TRUE;
}

BOOL SystemParametersInfoW(const UINT action, const UINT uiParam, void* const pvParam, UINT)
{
  switch (action) {
  case SPI_GETSCREENSAVEACTIVE:
    if (pvParam == nullptr) {
      return FALSE;
    }
    *static_cast<BOOL*>(pvParam) = (!wxlite::IsVideoReady() || SDL_ScreenSaverEnabled()) ? TRUE : FALSE;
    return TRUE;
  case SPI_SETSCREENSAVEACTIVE:
    if (!wxlite::IsVideoReady()) {
      return FALSE;
    }
    return (uiParam != 0 ? SDL_EnableScreenSaver() : SDL_DisableScreenSaver()) ? TRUE : FALSE;
  case SPI_GETWHEELSCROLLLINES:
    if (pvParam == nullptr) {
      return FALSE;
    }
    *static_cast<UINT*>(pvParam) = 3;
    return TRUE;
  default:
    // the accessibility keys, font smoothing...: Linux has none of these settings to read or set
    return FALSE;
  }
}

// --- the wait -------------------------------------------------------------------------------------------
DWORD MsgWaitForMultipleObjectsEx(const DWORD count, const HANDLE* const handles, const DWORD milliseconds,
                                  const DWORD, const DWORD flags)
{
  const Uint64 start = SDL_GetTicks();
  for (;;) {
    for (DWORD i = 0; i < count; ++i) {
      if (handles[i] != nullptr && ::WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
        return WAIT_OBJECT_0 + i;
      }
    }
    if ((flags & MWMO_ALERTABLE) != 0 && ::SleepEx(0, TRUE) == WAIT_IO_COMPLETION) {
      return WAIT_IO_COMPLETION;
    }
    if (wxlite::IsQuitPosted()) {
      return WAIT_OBJECT_0 + count;
    }

    const Uint64 elapsed = SDL_GetTicks() - start;
    if (milliseconds != INFINITE && elapsed >= milliseconds) {
      return WAIT_TIMEOUT;
    }
    // input wakes the wait at once; the handles are looked at again every millisecond
    if (wxlite::WaitForInput(milliseconds == 0 ? 0 : 1)) {
      return WAIT_OBJECT_0 + count;
    }
    if (milliseconds == 0) {
      return WAIT_TIMEOUT;
    }
  }
}

// --- the shell ------------------------------------------------------------------------------------------
HINSTANCE ShellExecuteA(HWND, const char* const operation, const char* const file, const char* const parameters,
                        const char* const directory, int)
{
  if (file == nullptr || file[0] == '\0') {
    return reinterpret_cast<HINSTANCE>(static_cast<std::uintptr_t>(2)); // ERROR_FILE_NOT_FOUND
  }
  // "open" (and the default verb) hands the document or URL to the desktop; anything else runs it
  const bool open = operation == nullptr || std::strcmp(operation, "open") == 0;
  const bool isUrl = std::strstr(file, "://") != nullptr;
  std::vector<std::string> arguments;
  if (open && (isUrl || ::access(file, X_OK) != 0)) {
    arguments = {"xdg-open", file};
  } else if (parameters != nullptr && parameters[0] != '\0') {
    arguments = {"/bin/sh", "-c", std::string("\"") + file + "\" " + parameters};
  } else {
    arguments = {file};
  }
  std::vector<char*> argv;
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (directory != nullptr && directory[0] != '\0') {
    (void)posix_spawn_file_actions_addchdir_np(&actions, directory);
  }
  pid_t child = 0;
  const int error = ::posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (error != 0) {
    wxlite::Log("ShellExecute(\"%s\"): %s", file, std::strerror(error));
    return reinterpret_cast<HINSTANCE>(static_cast<std::uintptr_t>(2));
  }
  // reaped on its own thread, so it leaves no zombie behind
  std::thread([child] {
    int status = 0;
    (void)::waitpid(child, &status, 0);
  }).detach();
  return reinterpret_cast<HINSTANCE>(static_cast<std::uintptr_t>(33)); // > 32: success
}

HINSTANCE ShellExecuteW(const HWND window, const wchar_t* const operation, const wchar_t* const file,
                        const wchar_t* const parameters, const wchar_t* const directory, const int showCommand)
{
  const std::string narrowOperation = NarrowFromWide(operation);
  const std::string narrowFile = NarrowFromWide(file);
  const std::string narrowParameters = NarrowFromWide(parameters);
  const std::string narrowDirectory = NarrowFromWide(directory);
  return ShellExecuteA(window, operation != nullptr ? narrowOperation.c_str() : nullptr, narrowFile.c_str(),
                       narrowParameters.c_str(), narrowDirectory.c_str(), showCommand);
}

BOOL CopyFileW(const wchar_t* const existingFile, const wchar_t* const newFile, const BOOL failIfExists)
{
  if (existingFile == nullptr || newFile == nullptr) {
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  const auto toPath = [](const wchar_t* const path) {
    std::string narrow = NarrowFromWide(path);
    std::replace(narrow.begin(), narrow.end(), '\\', '/');
    return std::filesystem::path(narrow);
  };
  std::error_code error;
  const auto options = failIfExists != FALSE ? std::filesystem::copy_options::none
                                             : std::filesystem::copy_options::overwrite_existing;
  if (!std::filesystem::copy_file(toPath(existingFile), toPath(newFile), options, error) || error) {
    ::SetLastError(error == std::errc::file_exists ? ERROR_FILE_EXISTS : ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  return TRUE;
}

UINT timeBeginPeriod(UINT)
{
  return TIMERR_NOERROR;
}

UINT timeEndPeriod(UINT)
{
  return TIMERR_NOERROR;
}
