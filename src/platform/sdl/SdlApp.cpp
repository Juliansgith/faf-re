// Engine addition (faf-neural-ai): the native Linux client's event pump. wxApp::Pending / Dispatch,
// which the engine's main loop (WIN_AppExecute, moho/app/WinApp.cpp) calls where Windows' message
// pump was, take SDL's events and hand each to the window it is for as the Windows messages it stood
// for there (WM_KEYDOWN / WM_CHAR / WM_MOUSEMOVE / WM_SIZE / WM_CLOSE / WM_ACTIVATE...): the window's
// MSWWindowProc - the engine's overrides first, then wx-lite's wxWindowMSW procedure - turns them into
// the same wx events wxWindows/MSW did. Also wx's entry (wxEntry, wxApp::Initialize / CleanUp) and the
// key state GetKeyState reads.

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "platform/sdl/SdlPrivate.h"

#include <Windows.h>

extern int __argc;    // platform/posix/Win32Shim.cpp
extern char** __argv; // platform/posix/Win32Shim.cpp

namespace
{
  bool sVideoReady = false;
  bool sVideoFailed = false;
  Uint32 sWakeEventType = 0;
  std::atomic<bool> sWakePending{false};
  std::atomic<bool> sQuitPosted{false};
  bool sAppActive = false;
  float sWheelRemainder = 0.0f;

  // GetKeyState's table, by virtual-key code: 0x80 down, 0x01 toggled (as Windows keeps it)
  std::atomic<std::uint8_t> sKeyState[256];

  // --- SDL key -> Windows virtual key ---------------------------------------------------------------
  struct VirtualKey
  {
    int code = 0;          // VK_*, 0 for a key Windows has no code for
    int specific = 0;      // VK_LSHIFT / VK_RCONTROL... for the modifiers
    bool extended = false; // lParam bit 24
  };

  int OemFromCharacter(const SDL_Keycode key)
  {
    switch (key) {
    case ';': return VK_OEM_1;
    case '=': return VK_OEM_PLUS;
    case ',': return VK_OEM_COMMA;
    case '-': return VK_OEM_MINUS;
    case '.': return VK_OEM_PERIOD;
    case '/': return VK_OEM_2;
    case '`': return VK_OEM_3;
    case '[': return VK_OEM_4;
    case '\\': return VK_OEM_5;
    case ']': return VK_OEM_6;
    case '\'': return VK_OEM_7;
    default: return 0;
    }
  }

  VirtualKey TranslateKey(const SDL_Scancode scancode, const SDL_Keycode key, const SDL_Keymod mod)
  {
    VirtualKey out;
    // letters follow the layout (Windows' VK_A is where the layout puts A); a non-Latin layout's
    // letters keep the US position, as Windows' do
    if (key >= 'a' && key <= 'z') {
      out.code = static_cast<int>(key - 'a' + 'A');
      return out;
    }
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z && (key & SDLK_SCANCODE_MASK) == 0 &&
        OemFromCharacter(key) == 0 && !(key >= '0' && key <= '9')) {
      out.code = 'A' + (scancode - SDL_SCANCODE_A);
      return out;
    }
    // the digit row is VK_0..VK_9 by position on every layout
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
      out.code = '1' + (scancode - SDL_SCANCODE_1);
      return out;
    }
    if (scancode == SDL_SCANCODE_0) {
      out.code = '0';
      return out;
    }
    if (const int oem = OemFromCharacter(key); oem != 0) {
      out.code = oem;
      return out;
    }

    const bool numLock = (mod & SDL_KMOD_NUM) != 0;
    switch (scancode) {
    case SDL_SCANCODE_RETURN: out.code = VK_RETURN; break;
    case SDL_SCANCODE_ESCAPE: out.code = VK_ESCAPE; break;
    case SDL_SCANCODE_BACKSPACE: out.code = VK_BACK; break;
    case SDL_SCANCODE_TAB: out.code = VK_TAB; break;
    case SDL_SCANCODE_SPACE: out.code = VK_SPACE; break;
    case SDL_SCANCODE_MINUS: out.code = VK_OEM_MINUS; break;
    case SDL_SCANCODE_EQUALS: out.code = VK_OEM_PLUS; break;
    case SDL_SCANCODE_LEFTBRACKET: out.code = VK_OEM_4; break;
    case SDL_SCANCODE_RIGHTBRACKET: out.code = VK_OEM_6; break;
    case SDL_SCANCODE_BACKSLASH: out.code = VK_OEM_5; break;
    case SDL_SCANCODE_NONUSHASH: out.code = VK_OEM_5; break;
    case SDL_SCANCODE_SEMICOLON: out.code = VK_OEM_1; break;
    case SDL_SCANCODE_APOSTROPHE: out.code = VK_OEM_7; break;
    case SDL_SCANCODE_GRAVE: out.code = VK_OEM_3; break;
    case SDL_SCANCODE_COMMA: out.code = VK_OEM_COMMA; break;
    case SDL_SCANCODE_PERIOD: out.code = VK_OEM_PERIOD; break;
    case SDL_SCANCODE_SLASH: out.code = VK_OEM_2; break;
    case SDL_SCANCODE_NONUSBACKSLASH: out.code = VK_OEM_102; break;
    case SDL_SCANCODE_CAPSLOCK: out.code = VK_CAPITAL; break;
    case SDL_SCANCODE_PRINTSCREEN: out.code = VK_SNAPSHOT; out.extended = true; break;
    case SDL_SCANCODE_SCROLLLOCK: out.code = VK_SCROLL; break;
    case SDL_SCANCODE_PAUSE: out.code = VK_PAUSE; break;
    case SDL_SCANCODE_INSERT: out.code = VK_INSERT; out.extended = true; break;
    case SDL_SCANCODE_HOME: out.code = VK_HOME; out.extended = true; break;
    case SDL_SCANCODE_PAGEUP: out.code = VK_PRIOR; out.extended = true; break;
    case SDL_SCANCODE_DELETE: out.code = VK_DELETE; out.extended = true; break;
    case SDL_SCANCODE_END: out.code = VK_END; out.extended = true; break;
    case SDL_SCANCODE_PAGEDOWN: out.code = VK_NEXT; out.extended = true; break;
    case SDL_SCANCODE_RIGHT: out.code = VK_RIGHT; out.extended = true; break;
    case SDL_SCANCODE_LEFT: out.code = VK_LEFT; out.extended = true; break;
    case SDL_SCANCODE_DOWN: out.code = VK_DOWN; out.extended = true; break;
    case SDL_SCANCODE_UP: out.code = VK_UP; out.extended = true; break;
    case SDL_SCANCODE_NUMLOCKCLEAR: out.code = VK_NUMLOCK; out.extended = true; break;
    case SDL_SCANCODE_KP_DIVIDE: out.code = VK_DIVIDE; out.extended = true; break;
    case SDL_SCANCODE_KP_MULTIPLY: out.code = VK_MULTIPLY; break;
    case SDL_SCANCODE_KP_MINUS: out.code = VK_SUBTRACT; break;
    case SDL_SCANCODE_KP_PLUS: out.code = VK_ADD; break;
    case SDL_SCANCODE_KP_ENTER: out.code = VK_RETURN; out.extended = true; break;
    // the keypad without Num Lock is the navigation block (not extended), as on Windows
    case SDL_SCANCODE_KP_1: out.code = numLock ? VK_NUMPAD1 : VK_END; break;
    case SDL_SCANCODE_KP_2: out.code = numLock ? VK_NUMPAD2 : VK_DOWN; break;
    case SDL_SCANCODE_KP_3: out.code = numLock ? VK_NUMPAD3 : VK_NEXT; break;
    case SDL_SCANCODE_KP_4: out.code = numLock ? VK_NUMPAD4 : VK_LEFT; break;
    case SDL_SCANCODE_KP_5: out.code = numLock ? VK_NUMPAD5 : VK_CLEAR; break;
    case SDL_SCANCODE_KP_6: out.code = numLock ? VK_NUMPAD6 : VK_RIGHT; break;
    case SDL_SCANCODE_KP_7: out.code = numLock ? VK_NUMPAD7 : VK_HOME; break;
    case SDL_SCANCODE_KP_8: out.code = numLock ? VK_NUMPAD8 : VK_UP; break;
    case SDL_SCANCODE_KP_9: out.code = numLock ? VK_NUMPAD9 : VK_PRIOR; break;
    case SDL_SCANCODE_KP_0: out.code = numLock ? VK_NUMPAD0 : VK_INSERT; break;
    case SDL_SCANCODE_KP_PERIOD: out.code = numLock ? VK_DECIMAL : VK_DELETE; break;
    case SDL_SCANCODE_KP_EQUALS: out.code = VK_OEM_PLUS; break;
    case SDL_SCANCODE_APPLICATION: out.code = VK_APPS; out.extended = true; break;
    case SDL_SCANCODE_MENU: out.code = VK_APPS; out.extended = true; break;
    case SDL_SCANCODE_HELP: out.code = VK_HELP; break;
    case SDL_SCANCODE_SELECT: out.code = VK_SELECT; break;
    case SDL_SCANCODE_EXECUTE: out.code = VK_EXECUTE; break;
    case SDL_SCANCODE_CLEAR: out.code = VK_CLEAR; break;
    case SDL_SCANCODE_SLEEP: out.code = VK_SLEEP; break;
    case SDL_SCANCODE_LCTRL: out.code = VK_CONTROL; out.specific = VK_LCONTROL; break;
    case SDL_SCANCODE_LSHIFT: out.code = VK_SHIFT; out.specific = VK_LSHIFT; break;
    case SDL_SCANCODE_LALT: out.code = VK_MENU; out.specific = VK_LMENU; break;
    case SDL_SCANCODE_LGUI: out.code = VK_LWIN; out.extended = true; break;
    case SDL_SCANCODE_RCTRL: out.code = VK_CONTROL; out.specific = VK_RCONTROL; out.extended = true; break;
    case SDL_SCANCODE_RSHIFT: out.code = VK_SHIFT; out.specific = VK_RSHIFT; break;
    case SDL_SCANCODE_RALT: out.code = VK_MENU; out.specific = VK_RMENU; out.extended = true; break;
    case SDL_SCANCODE_RGUI: out.code = VK_RWIN; out.extended = true; break;
    default:
      if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        out.code = VK_F1 + (scancode - SDL_SCANCODE_F1);
      } else if (scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24) {
        out.code = VK_F13 + (scancode - SDL_SCANCODE_F13);
      }
      break;
    }
    return out;
  }

  // --- the windows the events are for --------------------------------------------------------------
  wxTopLevelWindow* TopLevelFor(const SDL_WindowID id)
  {
    return wxlite::FindTopLevel(SDL_GetWindowFromID(id));
  }

  /// The window keyboard messages go to: the focused one when it is in `topLevel`, else `topLevel`.
  wxWindow* KeyboardTarget(wxTopLevelWindow* const topLevel)
  {
    wxWindow* const focus = wxWindow::FindFocus();
    if (focus != nullptr && focus->GetTopLevelParent() == topLevel) {
      return focus;
    }
    return topLevel;
  }

  /// The window mouse messages go to, with (x, y) - top-level client coordinates - turned into its
  /// client coordinates: the capturing window, else the deepest one under the mouse.
  wxWindow* MouseTarget(wxTopLevelWindow* const topLevel, int& x, int& y)
  {
    if (wxWindow* const capture = wxWindow::GetCapture(); capture != nullptr) {
      int offsetX = 0;
      int offsetY = 0;
      wxlite::OffsetInTopLevel(capture, offsetX, offsetY);
      if (capture->GetTopLevelParent() != topLevel) {
        // another frame captured the mouse: through the screen into its coordinates
        topLevel->ClientToScreen(&x, &y);
        capture->ScreenToClient(&x, &y);
        return capture;
      }
      x -= offsetX;
      y -= offsetY;
      return capture;
    }
    return wxlite::HitTest(topLevel, x, y);
  }

  void UpdateMouseOver(wxWindow* const target)
  {
    wxWindow*& over = wxlite::MouseOverWindow();
    if (over != nullptr && over != target) {
      wxWindow* const previous = over;
      over = nullptr;
      previous->GenerateMouseLeave(-1, -1, wxlite::MouseKeyFlags());
    }
    over = target;
  }

  void SetVirtualKey(const VirtualKey& key, const bool down)
  {
    if (key.specific != 0) {
      wxlite::SetKeyDown(key.specific, down);
      int left = 0;
      int right = 0;
      switch (key.code) {
      case VK_SHIFT: left = VK_LSHIFT; right = VK_RSHIFT; break;
      case VK_CONTROL: left = VK_LCONTROL; right = VK_RCONTROL; break;
      default: left = VK_LMENU; right = VK_RMENU; break;
      }
      wxlite::SetKeyDown(key.code, wxlite::KeyState(left) < 0 || wxlite::KeyState(right) < 0);
    } else {
      wxlite::SetKeyDown(key.code, down);
    }
  }

  // --- event translation ----------------------------------------------------------------------------
  void OnKey(const SDL_KeyboardEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    if (topLevel == nullptr) {
      return;
    }
    const VirtualKey key = TranslateKey(event.scancode, event.key, event.mod);
    if (key.code == 0) {
      return;
    }
    const bool wasDown = wxlite::KeyState(key.specific != 0 ? key.specific : key.code) < 0;
    SetVirtualKey(key, event.down);

    const bool altDown = wxlite::KeyState(VK_MENU) < 0;
    const bool ctrlDown = wxlite::KeyState(VK_CONTROL) < 0;
    // lParam: repeat count 1, extended (24), context / Alt held (29), previous state (30), transition (31)
    std::uint32_t lParam = 1u;
    if (key.extended) {
      lParam |= 1u << 24;
    }
    if (altDown) {
      lParam |= 1u << 29;
    }
    if (event.down ? (event.repeat || wasDown) : true) {
      lParam |= 1u << 30;
    }
    if (!event.down) {
      lParam |= 1u << 31;
    }
    // Alt without Ctrl (and F10) make the system keys
    const bool system = (altDown && !ctrlDown) || key.code == VK_F10;
    const WXUINT message =
      event.down ? (system ? WM_SYSKEYDOWN : WM_KEYDOWN) : (system ? WM_SYSKEYUP : WM_KEYUP);

    wxWindow* const target = KeyboardTarget(topLevel);
    (void)target->MSWWindowProc(message, static_cast<WXWPARAM>(key.code), static_cast<WXLPARAM>(lParam));

    if (!event.down) {
      return;
    }
    // TranslateMessage's WM_CHAR for the keys SDL's text input leaves out: the control characters
    int character = 0;
    switch (key.code) {
    case VK_RETURN: character = '\r'; break;
    case VK_BACK: character = '\b'; break;
    case VK_TAB: character = '\t'; break;
    case VK_ESCAPE: character = 0x1B; break;
    default:
      if (ctrlDown && !altDown && key.code >= 'A' && key.code <= 'Z') {
        character = key.code - 'A' + 1;
      }
      break;
    }
    if (character != 0) {
      wxWindow* const charTarget = KeyboardTarget(topLevel);
      (void)charTarget->MSWWindowProc(system ? WM_SYSCHAR : WM_CHAR, static_cast<WXWPARAM>(character),
                                      static_cast<WXLPARAM>(lParam & 0x01FFFFFFu));
    }
  }

  void OnTextInput(const SDL_TextInputEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    if (topLevel == nullptr || event.text == nullptr) {
      return;
    }
    const wxString text = wxString::FromUTF8(event.text);
    const bool altDown = wxlite::KeyState(VK_MENU) < 0 && wxlite::KeyState(VK_CONTROL) >= 0;
    for (std::size_t i = 0; i < text.Len(); ++i) {
      const wxChar character = text[i];
      if (character < 0x20 || character == 0x7F) {
        continue; // sent from the key itself
      }
      wxWindow* const target = KeyboardTarget(topLevel);
      (void)target->MSWWindowProc(altDown ? WM_SYSCHAR : WM_CHAR, static_cast<WXWPARAM>(character), 1);
    }
  }

  void OnMouseMotion(const SDL_MouseMotionEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    if (topLevel == nullptr) {
      return;
    }
    int x = static_cast<int>(std::floor(event.x));
    int y = static_cast<int>(std::floor(event.y));
    wxWindow* const target = MouseTarget(topLevel, x, y);
    UpdateMouseOver(target);
    if (wxWindow::GetCapture() == nullptr) {
      (void)target->MSWWindowProc(WM_SETCURSOR, static_cast<WXWPARAM>(target->GetHandle()),
                                  MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }
    (void)target->MSWWindowProc(WM_MOUSEMOVE, wxlite::MouseKeyFlags(), MAKELPARAM(x, y));
  }

  void OnMouseButton(const SDL_MouseButtonEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    int virtualKey = 0;
    WXUINT message = 0;
    // an even click count is the double click (Windows: DOWN, UP, DBLCLK, UP)
    const bool doubleClick = event.down && event.clicks >= 2 && (event.clicks % 2) == 0;
    switch (event.button) {
    case SDL_BUTTON_LEFT:
      virtualKey = VK_LBUTTON;
      message = event.down ? (doubleClick ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN) : WM_LBUTTONUP;
      break;
    case SDL_BUTTON_RIGHT:
      virtualKey = VK_RBUTTON;
      message = event.down ? (doubleClick ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN) : WM_RBUTTONUP;
      break;
    case SDL_BUTTON_MIDDLE:
      virtualKey = VK_MBUTTON;
      message = event.down ? (doubleClick ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN) : WM_MBUTTONUP;
      break;
    case SDL_BUTTON_X1:
      virtualKey = VK_XBUTTON1;
      message = event.down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
      break;
    case SDL_BUTTON_X2:
      virtualKey = VK_XBUTTON2;
      message = event.down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
      break;
    default:
      return;
    }
    wxlite::SetKeyDown(virtualKey, event.down);
    if (topLevel == nullptr) {
      return;
    }
    int x = static_cast<int>(std::floor(event.x));
    int y = static_cast<int>(std::floor(event.y));
    wxWindow* const target = MouseTarget(topLevel, x, y);
    UpdateMouseOver(target);
    WXWPARAM wParam = wxlite::MouseKeyFlags();
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
      wParam = MAKEWPARAM(wParam, event.button == SDL_BUTTON_X1 ? XBUTTON1 : XBUTTON2);
    }
    if (event.down && wxWindow::GetCapture() == nullptr) {
      (void)target->MSWWindowProc(WM_SETCURSOR, static_cast<WXWPARAM>(target->GetHandle()), MAKELPARAM(HTCLIENT, message));
    }
    (void)target->MSWWindowProc(message, wParam, MAKELPARAM(x, y));
  }

  void OnMouseWheel(const SDL_MouseWheelEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    if (topLevel == nullptr) {
      return;
    }
    float amount = event.y;
    if (event.direction == SDL_MOUSEWHEEL_FLIPPED) {
      amount = -amount;
    }
    sWheelRemainder += amount * static_cast<float>(WHEEL_DELTA);
    const int delta = static_cast<int>(sWheelRemainder);
    if (delta == 0) {
      return;
    }
    sWheelRemainder -= static_cast<float>(delta);

    // Windows sends the wheel to the focused window, with the position on the screen
    int x = static_cast<int>(std::floor(event.mouse_x));
    int y = static_cast<int>(std::floor(event.mouse_y));
    topLevel->ClientToScreen(&x, &y);
    wxWindow* target = KeyboardTarget(topLevel);
    if (target == topLevel) {
      int localX = static_cast<int>(std::floor(event.mouse_x));
      int localY = static_cast<int>(std::floor(event.mouse_y));
      target = MouseTarget(topLevel, localX, localY);
    }
    const WXWPARAM wParam = MAKEWPARAM(wxlite::MouseKeyFlags(), static_cast<WORD>(static_cast<short>(delta)));
    (void)target->MSWWindowProc(WM_MOUSEWHEEL, wParam, MAKELPARAM(x, y));
  }

  void SendToAllTopLevel(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
  {
    const wxWindowList windows = wxTopLevelWindows;
    for (wxWindow* const window : windows) {
      (void)window->MSWWindowProc(message, wParam, lParam);
    }
  }

  void RefreshTree(wxWindow* const window)
  {
    window->Refresh(false);
    for (wxWindow* const child : window->GetChildren()) {
      if (!child->IsTopLevel()) {
        RefreshTree(child);
      }
    }
  }

  void SendSize(wxTopLevelWindow* const topLevel, const WXWPARAM kind)
  {
    (void)topLevel->MSWWindowProc(WM_SIZE, kind, MAKELPARAM(topLevel->m_width, topLevel->m_height));
  }

  void OnWindowEvent(const SDL_WindowEvent& event)
  {
    wxTopLevelWindow* const topLevel = TopLevelFor(event.windowID);
    if (topLevel == nullptr) {
      return;
    }
    SDL_Window* const window = static_cast<SDL_Window*>(topLevel->m_nativeWindow);
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      (void)topLevel->MSWWindowProc(WM_CLOSE, 0, 0);
      break;

    case SDL_EVENT_WINDOW_MOVED:
      topLevel->m_x = event.data1;
      topLevel->m_y = event.data2;
      (void)topLevel->MSWWindowProc(WM_MOVE, 0, MAKELPARAM(event.data1, event.data2));
      break;

    case SDL_EVENT_WINDOW_RESIZED: {
      const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
      if ((flags & SDL_WINDOW_MINIMIZED) != 0) {
        break;
      }
      const bool maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
      if (event.data1 == topLevel->m_width && event.data2 == topLevel->m_height &&
          maximized == topLevel->m_maximized) {
        break; // our own SetSize, already reported
      }
      topLevel->m_width = event.data1;
      topLevel->m_height = event.data2;
      if (maximized) {
        SendSize(topLevel, SIZE_MAXIMIZED);
      } else if (topLevel->m_maximized) {
        SendSize(topLevel, SIZE_RESTORED);
      } else {
        // a resize by the user: the frame's sizing loop, as Windows runs it around a border drag
        (void)topLevel->MSWWindowProc(WM_SYSCOMMAND, SC_SIZE, 0);
        (void)topLevel->MSWWindowProc(WM_ENTERSIZEMOVE, 0, 0);
        SendSize(topLevel, SIZE_RESTORED);
        (void)topLevel->MSWWindowProc(WM_EXITSIZEMOVE, 0, 0);
      }
      RefreshTree(topLevel);
      break;
    }

    case SDL_EVENT_WINDOW_MAXIMIZED:
      SDL_GetWindowSize(window, &topLevel->m_width, &topLevel->m_height);
      SendSize(topLevel, SIZE_MAXIMIZED);
      RefreshTree(topLevel);
      break;

    case SDL_EVENT_WINDOW_MINIMIZED:
      (void)topLevel->MSWWindowProc(WM_SIZE, SIZE_MINIMIZED, 0);
      break;

    case SDL_EVENT_WINDOW_RESTORED:
      if (topLevel->m_iconized || topLevel->m_maximized) {
        SDL_GetWindowSize(window, &topLevel->m_width, &topLevel->m_height);
        SendSize(topLevel, SIZE_RESTORED);
        RefreshTree(topLevel);
      }
      break;

    case SDL_EVENT_WINDOW_EXPOSED:
      RefreshTree(topLevel);
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      if (!sAppActive) {
        sAppActive = true;
        SendToAllTopLevel(WM_ACTIVATEAPP, TRUE, 0);
      }
      (void)topLevel->MSWWindowProc(WM_ACTIVATE, WA_ACTIVE, 0);
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST: {
      (void)topLevel->MSWWindowProc(WM_ACTIVATE, WA_INACTIVE, 0);
      const bool stillOurs = SDL_GetKeyboardFocus() != nullptr && SDL_GetKeyboardFocus() != window;
      if (!stillOurs && sAppActive) {
        sAppActive = false;
        SendToAllTopLevel(WM_ACTIVATEAPP, FALSE, 0);
      }
      // the keys released elsewhere never come back here
      wxlite::ClearKeyState();
      if (wxWindow* const focus = wxWindow::FindFocus(); focus != nullptr && focus->GetTopLevelParent() == topLevel) {
        topLevel->m_lastFocus = focus;
        wxlite::FocusWindow() = nullptr;
        (void)focus->MSWWindowProc(WM_KILLFOCUS, 0, 0);
      }
      break;
    }

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      UpdateMouseOver(nullptr);
      break;

    default:
      break;
    }
  }

  void Translate(const SDL_Event& event)
  {
    if (event.type == sWakeEventType && sWakeEventType != 0) {
      sWakePending = false;
      return;
    }
    switch (event.type) {
    case SDL_EVENT_QUIT:
      // only a signal (SIGINT / SIGTERM) sends it: closing the last window does not
      // (SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE is off; WM_CLOSE asks the game first)
      wxlite::Log("wx-lite: quit requested (signal)");
      if (wxTheApp != nullptr) {
        wxTheApp->ExitMainLoop();
      }
      break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      OnKey(event.key);
      break;
    case SDL_EVENT_TEXT_INPUT:
      OnTextInput(event.text);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      OnMouseMotion(event.motion);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      OnMouseButton(event.button);
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      OnMouseWheel(event.wheel);
      break;
    default:
      if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        OnWindowEvent(event.window);
      }
      break;
    }
  }
} // namespace

// --- key state ----------------------------------------------------------------------------------------
void wxlite::SetKeyDown(const int virtualKey, const bool down)
{
  if (virtualKey <= 0 || virtualKey > 255) {
    return;
  }
  std::uint8_t value = sKeyState[virtualKey].load(std::memory_order_relaxed);
  if (down && (value & 0x80u) == 0) {
    value ^= 0x01u;
  }
  value = down ? static_cast<std::uint8_t>(value | 0x80u) : static_cast<std::uint8_t>(value & ~0x80u);
  sKeyState[virtualKey].store(value, std::memory_order_relaxed);
}

short wxlite::KeyState(const int virtualKey)
{
  if (virtualKey <= 0 || virtualKey > 255) {
    return 0;
  }
  const std::uint8_t value = sKeyState[virtualKey].load(std::memory_order_relaxed);
  int toggled = value & 0x01u;
  if (sVideoReady && (virtualKey == VK_CAPITAL || virtualKey == VK_NUMLOCK || virtualKey == VK_SCROLL)) {
    const SDL_Keymod mod = SDL_GetModState();
    const SDL_Keymod bit = virtualKey == VK_CAPITAL ? SDL_KMOD_CAPS
                           : virtualKey == VK_NUMLOCK ? SDL_KMOD_NUM
                                                      : SDL_KMOD_SCROLL;
    toggled = (mod & bit) != 0 ? 1 : 0;
  }
  return static_cast<short>(((value & 0x80u) != 0 ? 0xFF80 : 0) | toggled);
}

void wxlite::ClearKeyState()
{
  for (auto& key : sKeyState) {
    key.store(static_cast<std::uint8_t>(key.load(std::memory_order_relaxed) & 0x01u), std::memory_order_relaxed);
  }
}

std::uint32_t wxlite::MouseKeyFlags()
{
  std::uint32_t flags = 0;
  if (KeyState(VK_LBUTTON) < 0) {
    flags |= MK_LBUTTON;
  }
  if (KeyState(VK_RBUTTON) < 0) {
    flags |= MK_RBUTTON;
  }
  if (KeyState(VK_MBUTTON) < 0) {
    flags |= MK_MBUTTON;
  }
  if (KeyState(VK_XBUTTON1) < 0) {
    flags |= MK_XBUTTON1;
  }
  if (KeyState(VK_XBUTTON2) < 0) {
    flags |= MK_XBUTTON2;
  }
  if (KeyState(VK_SHIFT) < 0) {
    flags |= MK_SHIFT;
  }
  if (KeyState(VK_CONTROL) < 0) {
    flags |= MK_CONTROL;
  }
  return flags;
}

// --- the main loop ------------------------------------------------------------------------------------
bool wxlite::IsVideoReady()
{
  return sVideoReady;
}

void wxlite::WakeMainLoop()
{
  if (!sVideoReady || sWakeEventType == 0) {
    return;
  }
  bool expected = false;
  if (!sWakePending.compare_exchange_strong(expected, true)) {
    return; // one wake-up in the queue is enough
  }
  SDL_Event event{};
  event.type = sWakeEventType;
  if (!SDL_PushEvent(&event)) {
    sWakePending = false;
  }
}

void wxlite::PostQuit()
{
  sQuitPosted = true;
  WakeMainLoop();
}

void wxlite::CancelQuit()
{
  sQuitPosted = false;
}

bool wxlite::IsQuitPosted()
{
  return sQuitPosted;
}

bool wxlite::WaitForInput(const int milliseconds)
{
  if (IsPaintRequested()) {
    return true;
  }
  if (!sVideoReady) {
    SDL_Delay(static_cast<Uint32>(milliseconds > 0 ? milliseconds : 0));
    return false;
  }
  return SDL_WaitEventTimeout(nullptr, milliseconds);
}

bool wxApp::Initialize()
{
  if (sVideoReady) {
    return true;
  }
  if (sVideoFailed) {
    return false;
  }
  // WM_CLOSE asks the game; a closed last window must not quit behind its back
  SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
  SDL_SetHint(SDL_HINT_APP_NAME, "Supreme Commander: Forged Alliance");
  // the game draws its own cursor and keeps the screen awake itself (CScApp)
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    sVideoFailed = true;
    std::fprintf(stderr, "wx-lite: SDL video failed to start: %s\n", SDL_GetError());
    wxlite::Log("wx-lite: SDL video failed to start: %s", SDL_GetError());
    return false;
  }
  sWakeEventType = SDL_RegisterEvents(1);
  sVideoReady = true;
  wxlite::Log("wx-lite: SDL %d.%d.%d, video driver \"%s\"", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
              SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "?");
  return true;
}

void wxApp::CleanUp()
{
  if (wxTheApp != nullptr) {
    wxTheApp->DeletePendingObjects();
  }
  const wxWindowList windows = wxTopLevelWindows;
  for (wxWindow* const window : windows) {
    delete window;
  }
  delete wxTheApp;
  wxTheApp = nullptr;
  if (sVideoReady) {
    sVideoReady = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  }
}

bool wxAppBase::Pending()
{
  if (sQuitPosted) {
    return true;
  }
  if (!sVideoReady) {
    return false;
  }
  SDL_PumpEvents();
  return SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) || wxlite::IsPaintRequested();
}

void wxAppBase::Dispatch()
{
  if (sQuitPosted.exchange(false)) {
    ExitMainLoop(); // WM_QUIT
    return;
  }
  if (!sVideoReady) {
    return;
  }
  SDL_Event event;
  if (SDL_PollEvent(&event)) {
    Translate(event);
    return;
  }
  // WM_PAINT comes when nothing else is queued
  (void)wxlite::DispatchPaint();
}

int wxAppBase::MainLoop()
{
  m_keepGoing = true;
  while (m_keepGoing) {
    while (m_keepGoing && Pending()) {
      Dispatch();
    }
    if (!m_keepGoing) {
      break;
    }
    if (!ProcessIdle()) {
      (void)wxlite::WaitForInput(10);
    }
  }
  return 0;
}

int wxEntry(WXHINSTANCE, WXHINSTANCE, char*, int, const bool enterLoop)
{
  if (!wxApp::Initialize()) {
    // the shell goes on without windows: the render device fails and the game says why
    wxlite::Log("wx-lite: running without a display");
  }
  if (wxTheApp == nullptr) {
    if (wxAppCreator == nullptr) {
      wxlite::Log("wx-lite: no application (IMPLEMENT_APP missing)");
      return -1;
    }
    wxTheApp = wxAppCreator();
  }

  // the wide command line, as wx keeps it
  static std::vector<std::wstring> sArguments;
  static std::vector<wxChar*> sArgumentPointers;
  sArguments.clear();
  for (int i = 0; i < __argc; ++i) {
    sArguments.emplace_back(wxString::FromUTF8(__argv[i]).Native());
  }
  sArgumentPointers.clear();
  for (std::wstring& argument : sArguments) {
    sArgumentPointers.push_back(argument.data());
  }
  sArgumentPointers.push_back(nullptr);
  wxTheApp->argc = static_cast<int>(sArguments.size());
  wxTheApp->argv = sArgumentPointers.data();

  if (!wxTheApp->OnInit()) {
    return -1;
  }
  if (!enterLoop) {
    return 0;
  }
  const int exitCode = wxTheApp->OnRun();
  (void)wxTheApp->OnExit();
  wxApp::CleanUp();
  return exitCode;
}

int wxEntry(const int argc, char** const argv)
{
  (void)argc;
  (void)argv;
  return wxEntry(nullptr, nullptr, nullptr, 0, true);
}

wxPoint wxGetMousePosition()
{
  POINT point{};
  (void)::GetCursorPos(&point);
  return wxPoint(point.x, point.y);
}

void wxGetMousePosition(int* const x, int* const y)
{
  const wxPoint point = wxGetMousePosition();
  if (x != nullptr) {
    *x = point.x;
  }
  if (y != nullptr) {
    *y = point.y;
  }
}

bool wxYield()
{
  if (wxTheApp == nullptr) {
    return false;
  }
  while (wxTheApp->Pending()) {
    wxTheApp->Dispatch();
  }
  wxTheApp->ProcessPendingEvents();
  return true;
}

void wxWakeUpIdle()
{
  wxlite::WakeMainLoop();
}
