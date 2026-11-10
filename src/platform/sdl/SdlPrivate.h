#pragma once
// Engine addition (faf-neural-ai): what the native Linux client's SDL shell files share (WxLite.cpp,
// WxLiteWindows.cpp, SdlApp.cpp, SdlWin32.cpp): window lookup, keyboard focus and mouse capture, the
// key state GetKeyState reads, and the paint and wake-up requests of the main loop.

#include <cstdint>

#include "wx/wxlite.h"

struct SDL_Window;

namespace wxlite
{
  /// The SDL window behind a native handle (an HWND or a WXHWND holds the SDL_Window*).
  inline SDL_Window* NativeWindow(const WXHWND handle) noexcept
  {
    return reinterpret_cast<SDL_Window*>(handle);
  }
  inline SDL_Window* NativeWindow(const void* const handle) noexcept
  {
    return static_cast<SDL_Window*>(const_cast<void*>(handle));
  }

  /// The top-level wx window that owns `window`, or null.
  wxTopLevelWindow* FindTopLevel(SDL_Window* window);
  /// The SDL window `window` draws into: its top-level ancestor's.
  SDL_Window* NativeWindowOf(const wxWindow* window);

  /// The window with the keyboard focus (wxWindow::FindFocus), set without sending any message.
  wxWindow*& FocusWindow();
  /// The window that captured the mouse (wxWindow::GetCapture).
  wxWindow*& CaptureWindow();
  /// The window the mouse was last over (for its wxEVT_LEAVE_WINDOW).
  wxWindow*& MouseOverWindow();
  /// Forgets `window` everywhere the shell keeps a pointer to it (focus, capture, mouse-over).
  void ForgetWindow(wxWindow* window);

  /// The deepest shown, enabled window of `topLevel` under the client point (x, y), with (x, y)
  /// turned into that window's client coordinates.
  wxWindow* HitTest(wxWindow* topLevel, int& x, int& y);
  /// The window's origin in its top-level window's client area.
  void OffsetInTopLevel(const wxWindow* window, int& x, int& y);

  // --- key state (GetKeyState / GetAsyncKeyState), kept as the messages are dispatched ---------------
  void SetKeyDown(int virtualKey, bool down);
  short KeyState(int virtualKey);
  void ClearKeyState();
  /// MK_* flags of the mouse buttons and of Shift / Ctrl, as a mouse message's wParam carries them.
  std::uint32_t MouseKeyFlags();

  // --- the main loop -------------------------------------------------------------------------------
  /// A window called Refresh(): a WM_PAINT is due once the queue is empty.
  void RequestPaint();
  bool IsPaintRequested();
  /// Sends the WM_ERASEBKGND / WM_PAINT the windows asked for; true when one was sent.
  bool DispatchPaint();
  /// Wakes the main loop's wait (an SDL user event); callable from any thread.
  void WakeMainLoop();
  /// PostQuitMessage: the next Dispatch() ends the main loop (wxApp::ExitMainLoop), as WM_QUIT did.
  void PostQuit();
  void CancelQuit();
  bool IsQuitPosted();
  /// Whether SDL's video (and with it its event queue) is up.
  bool IsVideoReady();
  /// The engine's own wait slices (MsgWaitForMultipleObjectsEx): true when an SDL event is queued,
  /// after waiting at most `milliseconds` for one.
  bool WaitForInput(int milliseconds);

  /// Logs one shell message through the engine's log (gpg::Logf), or stderr before it is up.
  void Log(const char* format, ...);
} // namespace wxlite
