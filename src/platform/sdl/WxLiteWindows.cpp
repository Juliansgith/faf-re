// Engine addition (faf-neural-ai): wx-lite's windows for the native Linux client. A wxWindow keeps
// its place, size, children, handler chain and state; a top-level window owns an SDL window. The
// window procedure is wxWindows 2.4.2's (src/msw/window.cpp, frame.cpp): platform/sdl/SdlApp.cpp
// turns SDL's events into the Windows messages a window got there, MSWWindowProc turns those into
// wx events through the window's handler chain, and what nothing handles goes to MSWDefWindowProc -
// so the engine's own MSWWindowProc / MSWDefWindowProc overrides (the main frame's, the viewport's)
// see the same messages they saw on Windows.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "platform/sdl/SdlPrivate.h"

#include <Windows.h>

void wxliteScheduleDelete(wxWindow* window); // WxLite.cpp
void wxliteForgetPending(wxWindow* window);  // WxLite.cpp

namespace
{
  constexpr const char* kWindowProperty = "faf.wxlite.window";
  constexpr int kDefaultChildExtent = 20;      // wxWindowMSW's default control extent
  constexpr int kDefaultFrameWidth = 800;
  constexpr int kDefaultFrameHeight = 600;

  wxWindow* sFocusWindow = nullptr;
  wxWindow* sCaptureWindow = nullptr;
  wxWindow* sMouseOverWindow = nullptr;
  bool sPaintRequested = false;

  const wxEventTableEntry sEmptyEntries[] = {{wxEVT_NULL, 0, 0, nullptr, nullptr}};
  const wxEventTableEntry sTopLevelEntries[] = {
    EVT_CLOSE(wxTopLevelWindow::OnCloseWindow)
    EVT_SIZE(wxTopLevelWindow::OnSize)
    {wxEVT_NULL, 0, 0, nullptr, nullptr}};
  const wxEventTableEntry sDialogEntries[] = {
    EVT_CLOSE(wxDialog::OnCloseWindow)
    {wxEVT_NULL, 0, 0, nullptr, nullptr}};

  WXHWND HandleOf(const wxWindow* const window)
  {
    return window != nullptr ? window->GetHandle() : 0;
  }

  bool IsInside(const wxWindow* window, const wxWindow* const ancestor)
  {
    for (; window != nullptr; window = window->GetParent()) {
      if (window == ancestor) {
        return true;
      }
    }
    return false;
  }

  void CollectPaint(wxWindow* const window, std::vector<wxWindow*>& out)
  {
    if (!window->IsShown()) {
      return;
    }
    if (window->m_needsPaint) {
      out.push_back(window);
    }
    for (wxWindow* const child : window->GetChildren()) {
      CollectPaint(child, out);
    }
  }
} // namespace

const wxEventTable wxWindowBase::sm_eventTable = {&wxEvtHandler::sm_eventTable, &sEmptyEntries[0]};
const wxEventTable wxWindow::sm_eventTable = {&wxWindowBase::sm_eventTable, &sEmptyEntries[0]};
const wxEventTable wxTopLevelWindow::sm_eventTable = {&wxWindow::sm_eventTable, &sTopLevelEntries[0]};
const wxEventTable wxFrame::sm_eventTable = {&wxTopLevelWindow::sm_eventTable, &sEmptyEntries[0]};
const wxEventTable wxDialog::sm_eventTable = {&wxTopLevelWindow::sm_eventTable, &sDialogEntries[0]};

// --- shared state -----------------------------------------------------------------------------------
wxWindow*& wxlite::FocusWindow()
{
  return sFocusWindow;
}

wxWindow*& wxlite::CaptureWindow()
{
  return sCaptureWindow;
}

wxWindow*& wxlite::MouseOverWindow()
{
  return sMouseOverWindow;
}

wxTopLevelWindow* wxlite::FindTopLevel(SDL_Window* const window)
{
  if (window == nullptr) {
    return nullptr;
  }
  return static_cast<wxTopLevelWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, nullptr));
}

wxWindow* wxFindWinFromHandle(const WXHWND handle)
{
  return wxlite::FindTopLevel(wxlite::NativeWindow(handle));
}

SDL_Window* wxlite::NativeWindowOf(const wxWindow* const window)
{
  return NativeWindow(HandleOf(window));
}

void wxlite::OffsetInTopLevel(const wxWindow* window, int& x, int& y)
{
  for (; window != nullptr && !window->IsTopLevel(); window = window->GetParent()) {
    x += window->m_x;
    y += window->m_y;
  }
}

wxWindow* wxlite::HitTest(wxWindow* const window, int& x, int& y)
{
  // the most recently created child first (it sits on top of its siblings)
  const std::vector<wxWindow*>& children = window->GetChildren();
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    wxWindow* const child = *it;
    if (child->IsTopLevel() || !child->IsShown() || !child->IsEnabled()) {
      continue;
    }
    if (x >= child->m_x && y >= child->m_y && x < child->m_x + child->m_width && y < child->m_y + child->m_height) {
      x -= child->m_x;
      y -= child->m_y;
      return HitTest(child, x, y);
    }
  }
  return window;
}

void wxlite::RequestPaint()
{
  sPaintRequested = true;
}

bool wxlite::IsPaintRequested()
{
  return sPaintRequested;
}

bool wxlite::DispatchPaint()
{
  if (!sPaintRequested) {
    return false;
  }
  sPaintRequested = false;
  std::vector<wxWindow*> windows;
  for (wxWindow* const topLevel : wxTopLevelWindows) {
    CollectPaint(topLevel, windows);
  }
  for (wxWindow* const window : windows) {
    window->Update();
  }
  return !windows.empty();
}

// --- wxWindow -------------------------------------------------------------------------------------
bool wxWindow::Create(wxWindow* const parent, const wxWindowID id, const wxPoint& pos, const wxSize& size,
                      const long style, const wxString& name)
{
  m_isWindow = true;
  m_windowId = id;
  m_windowName = name;
  m_windowStyle = style;
  m_x = pos.x != -1 ? pos.x : 0;
  m_y = pos.y != -1 ? pos.y : 0;
  m_width = size.x != -1 ? size.x : kDefaultChildExtent;
  m_height = size.y != -1 ? size.y : kDefaultChildExtent;
  m_isShown = true; // children are created visible (WS_VISIBLE), as wxWindowMSW's are
  if (parent != nullptr) {
    parent->AddChild(this);
  }
  return true;
}

wxWindow::~wxWindow()
{
  m_isBeingDeleted = true;
  wxliteForgetPending(this);
  wxlite::ForgetWindow(this);
  DestroyChildren();
  if (m_parent != nullptr) {
    m_parent->RemoveChild(this);
  }
  delete m_sizer;
  m_sizer = nullptr;
  if (wxTheApp != nullptr && wxTheApp->GetTopWindow() == this) {
    wxTheApp->SetTopWindow(nullptr);
  }
}

void wxlite::ForgetWindow(wxWindow* const window)
{
  if (sFocusWindow == window) {
    sFocusWindow = nullptr;
  }
  if (sCaptureWindow == window) {
    sCaptureWindow = nullptr;
    SDL_CaptureMouse(false);
  }
  if (sMouseOverWindow == window) {
    sMouseOverWindow = nullptr;
  }
  for (wxWindow* const topLevel : wxTopLevelWindows) {
    auto* const frame = static_cast<wxTopLevelWindow*>(topLevel);
    if (frame->m_lastFocus == window) {
      frame->m_lastFocus = nullptr;
    }
  }
}

void wxWindow::AddChild(wxWindow* const child)
{
  if (child == nullptr) {
    return;
  }
  child->m_parent = this;
  if (std::find(m_children.begin(), m_children.end(), child) == m_children.end()) {
    m_children.push_back(child);
  }
}

void wxWindow::RemoveChild(wxWindow* const child)
{
  m_children.erase(std::remove(m_children.begin(), m_children.end(), child), m_children.end());
  if (child != nullptr && child->m_parent == this) {
    child->m_parent = nullptr;
  }
}

bool wxWindow::DestroyChildren()
{
  while (!m_children.empty()) {
    wxWindow* const child = m_children.back();
    RemoveChild(child);
    delete child;
  }
  return true;
}

bool wxWindow::Destroy()
{
  delete this;
  return true;
}

bool wxWindow::Close(const bool force)
{
  wxCloseEvent event(wxEVT_CLOSE_WINDOW, m_windowId);
  event.SetEventObject(this);
  event.SetCanVeto(!force);
  // a window with no close handler is destroyed by wxTopLevelWindow's default one
  return GetEventHandler()->ProcessEvent(event) && !event.GetVeto();
}

bool wxWindow::Show(const bool show)
{
  if (show == m_isShown) {
    return false;
  }
  m_isShown = show;
  if (!show && sFocusWindow != nullptr && IsInside(sFocusWindow, this)) {
    sFocusWindow = nullptr;
  }
  if (show) {
    Refresh();
  }
  return true;
}

wxWindow* wxWindow::GetTopLevelParent() const
{
  const wxWindow* window = this;
  while (window != nullptr && !window->IsTopLevel()) {
    window = window->GetParent();
  }
  return const_cast<wxWindow*>(window);
}

WXWidget wxWindow::GetHandle() const
{
  const wxWindow* const topLevel = GetTopLevelParent();
  return topLevel != nullptr && topLevel != this ? topLevel->GetHandle() : 0;
}

void wxWindow::SetFocus()
{
  wxWindow* const previous = sFocusWindow;
  if (wxWindow* const topLevel = GetTopLevelParent(); topLevel != nullptr) {
    static_cast<wxTopLevelWindow*>(topLevel)->m_lastFocus = this;
  }
  if (previous == this) {
    return;
  }
  sFocusWindow = this;
  if (previous != nullptr && !previous->IsBeingDeleted()) {
    previous->MSWWindowProc(WM_KILLFOCUS, static_cast<WXWPARAM>(HandleOf(this)), 0);
  }
  if (sFocusWindow == this) {
    MSWWindowProc(WM_SETFOCUS, static_cast<WXWPARAM>(HandleOf(previous)), 0);
  }
}

wxWindow* wxWindow::FindFocus()
{
  return sFocusWindow;
}

void wxWindow::CaptureMouse()
{
  sCaptureWindow = this;
  SDL_CaptureMouse(true);
}

void wxWindow::ReleaseMouse()
{
  if (sCaptureWindow == this) {
    sCaptureWindow = nullptr;
    SDL_CaptureMouse(false);
  }
}

wxWindow* wxWindow::GetCapture()
{
  return sCaptureWindow;
}

void wxWindow::Refresh(const bool eraseBackground, const wxRect*)
{
  m_needsPaint = true;
  m_needsErase = m_needsErase || eraseBackground;
  wxlite::RequestPaint();
}

void wxWindow::Update()
{
  if (!m_needsPaint) {
    return;
  }
  const bool erase = m_needsErase;
  m_needsPaint = false;
  m_needsErase = false;
  // BeginPaint sends WM_ERASEBKGND first when the update region asks for it
  if (erase) {
    (void)MSWWindowProc(WM_ERASEBKGND, 0, 0);
  }
  (void)MSWWindowProc(WM_PAINT, 0, 0);
}

void wxWindow::SetSize(int x, int y, int width, int height, int)
{
  if (x == -1) {
    x = m_x;
  }
  if (y == -1) {
    y = m_y;
  }
  if (width == -1) {
    width = m_width;
  }
  if (height == -1) {
    height = m_height;
  }
  const bool moved = x != m_x || y != m_y;
  const bool resized = width != m_width || height != m_height;
  m_x = x;
  m_y = y;
  m_width = width;
  m_height = height;
  // MoveWindow sends WM_MOVE and WM_SIZE before it returns
  if (moved) {
    (void)MSWWindowProc(WM_MOVE, 0, MAKELPARAM(x, y));
  }
  if (resized) {
    (void)MSWWindowProc(WM_SIZE, SIZE_RESTORED, MAKELPARAM(width, height));
    Refresh();
  }
}

void wxWindow::GetSize(int* const width, int* const height) const
{
  if (width != nullptr) {
    *width = m_width;
  }
  if (height != nullptr) {
    *height = m_height;
  }
}

void wxWindow::GetPosition(int* const x, int* const y) const
{
  if (x != nullptr) {
    *x = m_x;
  }
  if (y != nullptr) {
    *y = m_y;
  }
}

void wxWindow::ClientToScreen(int* const x, int* const y) const
{
  POINT point{x != nullptr ? *x : 0, y != nullptr ? *y : 0};
  int offsetX = 0;
  int offsetY = 0;
  wxlite::OffsetInTopLevel(this, offsetX, offsetY);
  point.x += offsetX;
  point.y += offsetY;
  (void)::ClientToScreen(reinterpret_cast<HWND>(GetHandle()), &point);
  if (x != nullptr) {
    *x = point.x;
  }
  if (y != nullptr) {
    *y = point.y;
  }
}

wxPoint wxWindow::ClientToScreen(const wxPoint& pt) const
{
  int x = pt.x;
  int y = pt.y;
  ClientToScreen(&x, &y);
  return wxPoint(x, y);
}

void wxWindow::ScreenToClient(int* const x, int* const y) const
{
  POINT point{x != nullptr ? *x : 0, y != nullptr ? *y : 0};
  (void)::ScreenToClient(reinterpret_cast<HWND>(GetHandle()), &point);
  int offsetX = 0;
  int offsetY = 0;
  wxlite::OffsetInTopLevel(this, offsetX, offsetY);
  if (x != nullptr) {
    *x = point.x - offsetX;
  }
  if (y != nullptr) {
    *y = point.y - offsetY;
  }
}

wxPoint wxWindow::ScreenToClient(const wxPoint& pt) const
{
  int x = pt.x;
  int y = pt.y;
  ScreenToClient(&x, &y);
  return wxPoint(x, y);
}

void wxWindow::PushEventHandler(wxEvtHandler* const handler)
{
  wxEvtHandler* const current = GetEventHandler();
  handler->SetNextHandler(current);
  current->SetPreviousHandler(handler);
  SetEventHandler(handler);
}

wxEvtHandler* wxWindow::PopEventHandler(const bool deleteHandler)
{
  wxEvtHandler* const top = GetEventHandler();
  if (top == this) {
    return nullptr;
  }
  wxEvtHandler* const next = top->GetNextHandler();
  top->SetNextHandler(nullptr);
  top->SetPreviousHandler(nullptr);
  if (next != nullptr) {
    next->SetPreviousHandler(nullptr);
  }
  SetEventHandler(next == this ? nullptr : next);
  if (deleteHandler) {
    delete top;
    return nullptr;
  }
  return top;
}

bool wxWindow::RemoveEventHandler(wxEvtHandler* const handler)
{
  for (wxEvtHandler* current = GetEventHandler(); current != nullptr && current != this;
       current = current->GetNextHandler()) {
    if (current != handler) {
      continue;
    }
    wxEvtHandler* const previous = current->GetPreviousHandler();
    wxEvtHandler* const next = current->GetNextHandler();
    if (previous != nullptr) {
      previous->SetNextHandler(next);
    } else {
      SetEventHandler(next == this ? nullptr : next);
    }
    if (next != nullptr) {
      next->SetPreviousHandler(previous);
    }
    current->SetNextHandler(nullptr);
    current->SetPreviousHandler(nullptr);
    return true;
  }
  return false;
}

void wxWindow::SetSizer(wxSizer* const sizer, const bool deleteOld)
{
  if (deleteOld && m_sizer != sizer) {
    delete m_sizer;
  }
  m_sizer = sizer;
}

// --- wxWindowMSW's window procedure -----------------------------------------------------------------
long wxWindow::MSWWindowProc(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
{
  bool processed = false;
  long result = 0;

  switch (message) {
  case WM_MOVE:
    processed = HandleMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    break;

  case WM_SIZE:
    switch (wParam) {
    case SIZE_MAXHIDE:
    case SIZE_MAXSHOW:
    case SIZE_MINIMIZED:
      // no size events for these: the client size may be negative
      break;
    default:
      processed = HandleSize(LOWORD(lParam), HIWORD(lParam), static_cast<WXUINT>(wParam));
      break;
    }
    break;

  case WM_ACTIVATEAPP:
    if (wxTheApp != nullptr) {
      wxTheApp->SetActive(wParam != 0, FindFocus());
    }
    break;

  case WM_ACTIVATE:
    processed = HandleActivate(LOWORD(wParam));
    break;

  case WM_SETFOCUS:
    processed = HandleSetFocus();
    break;

  case WM_KILLFOCUS:
    processed = HandleKillFocus();
    break;

  case WM_PAINT:
    processed = HandlePaint();
    break;

  case WM_ERASEBKGND:
    processed = HandleEraseBkgnd();
    if (processed) {
      result = TRUE;
    }
    break;

  case WM_CLOSE:
    // don't let the default procedure destroy the window: ~wxWindow does that
    processed = true;
    result = TRUE;
    break;

  case WM_MOUSEMOVE:
    processed = HandleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<WXUINT>(wParam));
    break;

  case WM_MOUSEWHEEL:
    processed = HandleMouseWheel(wParam, lParam);
    break;

  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MBUTTONDBLCLK:
    // the shell already sends these to the window under the mouse (FindWindowForMouseEvent's job);
    // our windows take the focus when clicked
    if (GetCapture() != this && message == WM_LBUTTONDOWN && AcceptsFocus()) {
      SetFocus();
    }
    processed = HandleMouseEvent(message, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<WXUINT>(wParam));
    break;

  case WM_SYSKEYDOWN:
  case WM_KEYDOWN:
    // processed by an event handler: no EVT_CHAR for this key
    m_lastKeydownProcessed = HandleKeyDown(static_cast<WORD>(wParam), lParam);
    if (m_lastKeydownProcessed) {
      processed = true;
    }
    if (!processed) {
      switch (wParam) {
      // "not interesting" to OnChar
      case VK_SHIFT:
      case VK_CONTROL:
      case VK_MENU:
      case VK_CAPITAL:
      case VK_NUMLOCK:
      case VK_SCROLL:
        processed = true;
        break;

      // these come back as WM_CHAR
      case VK_ESCAPE:
      case VK_SPACE:
      case VK_RETURN:
      case VK_BACK:
      case VK_TAB:
      case VK_ADD:
      case VK_SUBTRACT:
      case VK_MULTIPLY:
      case VK_DIVIDE:
      case VK_OEM_1:
      case VK_OEM_2:
      case VK_OEM_3:
      case VK_OEM_4:
      case VK_OEM_5:
      case VK_OEM_6:
      case VK_OEM_7:
      case VK_OEM_PLUS:
      case VK_OEM_COMMA:
      case VK_OEM_MINUS:
      case VK_OEM_PERIOD:
        processed = false;
        break;

      // the context-menu key is a right click
      case VK_APPS:
        processed = HandleMouseEvent(WM_RBUTTONDOWN, m_width / 2, m_height / 2, wxlite::MouseKeyFlags());
        break;

      default:
        // do generate a CHAR event
        processed = HandleChar(static_cast<WORD>(wParam), lParam);
        break;
      }
    }
    if (message == WM_SYSKEYDOWN) {
      processed = false; // the system keys go on to the default procedure
    }
    break;

  case WM_SYSKEYUP:
  case WM_KEYUP:
    if (wParam == VK_APPS) {
      processed = HandleMouseEvent(WM_RBUTTONUP, m_width / 2, m_height / 2, wxlite::MouseKeyFlags());
    } else {
      processed = HandleKeyUp(static_cast<WORD>(wParam), lParam);
    }
    break;

  case WM_SYSCHAR:
  case WM_CHAR:
    if (m_lastKeydownProcessed) {
      // handling a key in EVT_KEY_DOWN is meant, by design, to prevent its EVT_CHAR
      m_lastKeydownProcessed = false;
      processed = true;
    } else {
      processed = HandleChar(wParam, lParam, true);
    }
    break;

  default:
    break;
  }

  if (!processed) {
    result = MSWDefWindowProc(message, wParam, lParam);
  }
  return result;
}

long wxWindow::MSWDefWindowProc(const WXUINT message, const WXWPARAM, const WXLPARAM lParam)
{
  // DefWindowProc's part the engine relies on: the class cursor (the arrow) over the client area
  if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
    (void)::SetCursor(SDL_GetDefaultCursor());
    return TRUE;
  }
  return 0;
}

bool wxWindow::HandleMove(const int x, const int y)
{
  wxMoveEvent event(wxPoint(x, y), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleSize(int, int, WXUINT)
{
  // the total size, not the client size the message carries
  wxSizeEvent event(GetSize(), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleActivate(const int state)
{
  wxActivateEvent event(wxEVT_ACTIVATE, state != static_cast<int>(WA_INACTIVE), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleSetFocus()
{
  wxFocusEvent event(wxEVT_SET_FOCUS, m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleKillFocus()
{
  wxFocusEvent event(wxEVT_KILL_FOCUS, m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandlePaint()
{
  m_needsPaint = false;
  wxPaintEvent event(m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleEraseBkgnd()
{
  wxDC dc(this);
  wxEraseEvent event(m_windowId, &dc);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

void wxWindow::InitMouseEvent(wxMouseEvent& event, const int x, const int y, const WXUINT flags)
{
  event.m_x = x;
  event.m_y = y;
  event.m_shiftDown = (flags & MK_SHIFT) != 0;
  event.m_controlDown = (flags & MK_CONTROL) != 0;
  event.m_leftDown = (flags & MK_LBUTTON) != 0;
  event.m_middleDown = (flags & MK_MBUTTON) != 0;
  event.m_rightDown = (flags & MK_RBUTTON) != 0;
  event.m_altDown = ::GetKeyState(VK_MENU) < 0;
  event.SetTimestamp(static_cast<long>(SDL_GetTicks()));
  event.m_eventObject = this;
  event.SetId(GetId());
}

bool wxWindow::HandleMouseEvent(const WXUINT message, const int x, const int y, const WXUINT flags)
{
  static const wxEventType kEventsMouse[] = {
    wxEVT_MOTION,     wxEVT_LEFT_DOWN, wxEVT_LEFT_UP,     wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DOWN,
    wxEVT_RIGHT_UP,   wxEVT_RIGHT_DCLICK, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP, wxEVT_MIDDLE_DCLICK};
  if (message < WM_MOUSEMOVE || message > WM_MBUTTONDBLCLK) {
    return false;
  }
  wxMouseEvent event(kEventsMouse[message - WM_MOUSEMOVE]);
  InitMouseEvent(event, x, y, flags);
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMouseMove(const int x, const int y, const WXUINT flags)
{
  if (!m_mouseInWindow) {
    const bool inside = x >= 0 && y >= 0 && x < m_width && y < m_height;
    if (!HasCapture() || inside) {
      m_mouseInWindow = true;
      wxMouseEvent event(wxEVT_ENTER_WINDOW);
      InitMouseEvent(event, x, y, flags);
      (void)GetEventHandler()->ProcessEvent(event);
    }
  }
  return HandleMouseEvent(WM_MOUSEMOVE, x, y, flags);
}

void wxWindow::GenerateMouseLeave(const int x, const int y, const WXUINT flags)
{
  if (!m_mouseInWindow) {
    return;
  }
  m_mouseInWindow = false;
  wxMouseEvent event(wxEVT_LEAVE_WINDOW);
  InitMouseEvent(event, x, y, flags);
  (void)GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleMouseWheel(const WXWPARAM wParam, const WXLPARAM lParam)
{
  wxMouseEvent event(wxEVT_MOUSEWHEEL);
  // the position is on the screen, as Windows reports it
  InitMouseEvent(event, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), LOWORD(wParam));
  event.m_wheelRotation = static_cast<short>(HIWORD(wParam));
  event.m_wheelDelta = WHEEL_DELTA;
  event.m_linesPerAction = 3; // SPI_GETWHEELSCROLLLINES's default
  return GetEventHandler()->ProcessEvent(event);
}

wxKeyEvent wxWindow::CreateKeyEvent(const wxEventType type, const int id, const WXLPARAM lParam,
                                    const WXWPARAM wParam) const
{
  wxKeyEvent event(type);
  event.SetId(GetId());
  event.m_shiftDown = wxIsShiftDown();
  event.m_controlDown = wxIsCtrlDown();
  event.m_altDown = (HIWORD(lParam) & KF_ALTDOWN) == KF_ALTDOWN;
  event.m_eventObject = const_cast<wxWindow*>(this);
  event.m_keyCode = id;
  event.m_rawCode = static_cast<std::uint32_t>(wParam);
  event.m_rawFlags = static_cast<std::uint32_t>(lParam);
  event.SetTimestamp(static_cast<long>(SDL_GetTicks()));

  // the cursor, relative to the window rectangle
  POINT point{};
  (void)::GetCursorPos(&point);
  RECT rect{};
  (void)::GetWindowRect(reinterpret_cast<HWND>(GetHandle()), &rect);
  event.m_x = point.x - rect.left;
  event.m_y = point.y - rect.top;
  return event;
}

bool wxWindow::HandleChar(const WXWPARAM wParam, const WXLPARAM lParam, const bool isASCII)
{
  bool ctrlDown = false;
  int id;
  if (isASCII) {
    // 1..26 are Ctrl+letter (keycode 1..26, ControlDown) but for Return, Backspace and Tab
    id = static_cast<int>(wParam);
    if (id > 0 && id < 27) {
      switch (id) {
      case 13: id = WXK_RETURN; break;
      case 8: id = WXK_BACK; break;
      case 9: id = WXK_TAB; break;
      default: ctrlDown = true; break;
      }
    }
  } else {
    // from WM_KEYDOWN: only the keys WM_CHAR does not report
    id = wxCharCodeMSWToWX(static_cast<int>(wParam));
    if (id == 0) {
      return false;
    }
  }
  (void)ctrlDown;

  wxKeyEvent event(CreateKeyEvent(wxEVT_CHAR, id, lParam, wParam));
  // AltGr characters come with Ctrl and Alt: report them without either
  if (event.m_controlDown && event.m_altDown && id >= 32 && id < 256) {
    event.m_controlDown = false;
    event.m_altDown = false;
  }
  return GetEventHandler()->ProcessEvent(event);
}

bool wxWindow::HandleKeyDown(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (id == 0) {
    id = static_cast<int>(wParam); // a normal ASCII key
  }
  if (id != -1) {
    wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_DOWN, id, lParam, wParam));
    if (GetEventHandler()->ProcessEvent(event)) {
      return true;
    }
  }
  return false;
}

bool wxWindow::HandleKeyUp(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (id == 0) {
    id = static_cast<int>(wParam);
  }
  if (id != -1) {
    wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_UP, id, lParam, wParam));
    if (GetEventHandler()->ProcessEvent(event)) {
      return true;
    }
  }
  return false;
}

// --- wxTopLevelWindow -------------------------------------------------------------------------------
bool wxTopLevelWindow::CreateTopLevel(wxWindow* const parent, const wxWindowID id, const wxString& title,
                                      const wxPoint& pos, const wxSize& size, const long style, const wxString& name)
{
  m_isWindow = true;
  m_windowId = id;
  m_windowName = name;
  m_windowStyle = style;
  m_title = title;
  m_isShown = false;
  m_width = size.x > 0 ? size.x : kDefaultFrameWidth;
  m_height = size.y > 0 ? size.y : kDefaultFrameHeight;
  if (parent != nullptr) {
    parent->AddChild(this);
  }
  wxTopLevelWindows.push_back(this);
  // A quit posted when the last frame went (wxTopLevelWindowMSW's PostQuitMessage) is withdrawn by a
  // new one: CScApp::CreateAppFrame destroys its frames and creates new ones when a renderer fails.
  wxlite::CancelQuit();

  if (!wxApp::Initialize()) {
    wxlite::Log("wx-lite: no SDL video, the window \"%s\" stays without a native window", title.mb_str().c_str());
    return false;
  }

  SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;
  if ((style & wxCAPTION) == 0) {
    flags |= SDL_WINDOW_BORDERLESS;
  }
  if ((style & wxRESIZE_BORDER) != 0) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  if ((style & wxSTAY_ON_TOP) != 0) {
    flags |= SDL_WINDOW_ALWAYS_ON_TOP;
  }

  const std::string utf8Title = title.mb_str();
  SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, utf8Title.c_str());
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, pos.x != -1 ? pos.x : SDL_WINDOWPOS_UNDEFINED);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, pos.y != -1 ? pos.y : SDL_WINDOWPOS_UNDEFINED);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, m_width);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, m_height);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
  SDL_Window* window = SDL_CreateWindowWithProperties(properties);
  if (window == nullptr) {
    // Without a Vulkan loader SDL refuses SDL_WINDOW_VULKAN; the shell still runs without one.
    wxlite::Log("wx-lite: SDL_CreateWindow (Vulkan) failed: %s; trying without Vulkan", SDL_GetError());
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                          static_cast<Sint64>(flags & ~static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN)));
    window = SDL_CreateWindowWithProperties(properties);
  }
  SDL_DestroyProperties(properties);
  if (window == nullptr) {
    wxlite::Log("wx-lite: SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }

  m_nativeWindow = window;
  SDL_SetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, this);
  SDL_GetWindowSize(window, &m_width, &m_height);
  SDL_GetWindowPosition(window, &m_x, &m_y);
  // WM_CHAR comes from SDL's text input
  (void)SDL_StartTextInput(window);
  return true;
}

wxTopLevelWindow::~wxTopLevelWindow()
{
  m_isBeingDeleted = true;
  // the children render into this window: they go first
  DestroyChildren();
  wxTopLevelWindows.erase(std::remove(wxTopLevelWindows.begin(), wxTopLevelWindows.end(), this),
                          wxTopLevelWindows.end());
  if (m_nativeWindow != nullptr) {
    SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow);
    m_nativeWindow = nullptr;
    SDL_ClearProperty(SDL_GetWindowProperties(window), kWindowProperty);
    SDL_DestroyWindow(window);
  }
  // wxTopLevelWindowMSW: the last one gone ends the application (PostQuitMessage)
  if (wxTheApp != nullptr && wxTopLevelWindows.empty()) {
    wxTheApp->SetTopWindow(nullptr);
    if (wxTheApp->GetExitOnFrameDelete()) {
      wxlite::PostQuit();
    }
  }
}

bool wxTopLevelWindow::Show(const bool show)
{
  const bool changed = show != m_isShown;
  m_isShown = show;
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    if (show) {
      SDL_ShowWindow(window);
    } else {
      SDL_HideWindow(window);
    }
  }
  if (show) {
    Refresh();
  }
  return changed;
}

bool wxTopLevelWindow::Destroy()
{
  // deleted in the next idle pass, hidden now
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    SDL_HideWindow(window);
  }
  m_isShown = false;
  wxliteScheduleDelete(this);
  return true;
}

void wxTopLevelWindow::SetTitle(const wxString& title)
{
  m_title = title;
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    SDL_SetWindowTitle(window, title.mb_str().c_str());
  }
}

void wxTopLevelWindow::SetSize(int x, int y, int width, int height, int)
{
  if (x == -1) {
    x = m_x;
  }
  if (y == -1) {
    y = m_y;
  }
  if (width == -1) {
    width = m_width;
  }
  if (height == -1) {
    height = m_height;
  }
  if (m_minWidth > 0) {
    width = std::max(width, m_minWidth);
  }
  if (m_minHeight > 0) {
    height = std::max(height, m_minHeight);
  }
  const bool moved = x != m_x || y != m_y;
  const bool resized = width != m_width || height != m_height;
  SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow);
  m_x = x;
  m_y = y;
  m_width = width;
  m_height = height;
  if (moved) {
    if (window != nullptr) {
      SDL_SetWindowPosition(window, x, y);
    }
    (void)MSWWindowProc(WM_MOVE, 0, MAKELPARAM(x, y));
  }
  if (resized) {
    if (window != nullptr) {
      SDL_SetWindowSize(window, width, height);
    }
    (void)MSWWindowProc(WM_SIZE, IsMaximized() ? SIZE_MAXIMIZED : SIZE_RESTORED, MAKELPARAM(width, height));
    Refresh();
  }
}

void wxTopLevelWindow::Maximize(const bool maximize)
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    // on a hidden window SDL keeps the request for the first show, as wx 2.4 did (m_maximizeOnShow)
    if (maximize) {
      SDL_MaximizeWindow(window);
    } else {
      SDL_RestoreWindow(window);
    }
  }
  m_maximized = maximize;
}

void wxTopLevelWindow::Restore()
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    SDL_RestoreWindow(window);
  }
  m_maximized = false;
  m_iconized = false;
}

void wxTopLevelWindow::Iconize(const bool iconize)
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    if (iconize) {
      SDL_MinimizeWindow(window);
    } else {
      SDL_RestoreWindow(window);
    }
  }
}

bool wxTopLevelWindow::IsMaximized() const
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
  }
  return m_maximized;
}

bool wxTopLevelWindow::IsIconized() const
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
  }
  return m_iconized;
}

bool wxTopLevelWindow::ShowFullScreen(const bool show, long)
{
  if (show == m_fullScreen) {
    return false;
  }
  m_fullScreen = show;
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    // the desktop's mode: the render device sizes its swap chain to the window
    (void)SDL_SetWindowFullscreenMode(window, nullptr);
    if (!SDL_SetWindowFullscreen(window, show)) {
      wxlite::Log("wx-lite: SDL_SetWindowFullscreen failed: %s", SDL_GetError());
    }
  }
  if (!m_isShown) {
    Show(true);
  }
  return true;
}

void wxTopLevelWindow::Raise()
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    SDL_RaiseWindow(window);
  }
}

void wxTopLevelWindow::RequestUserAttention(int)
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    (void)SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);
  }
}

void wxTopLevelWindow::OnSizeHintsChanged()
{
  if (SDL_Window* const window = static_cast<SDL_Window*>(m_nativeWindow); window != nullptr) {
    SDL_SetWindowMinimumSize(window, std::max(m_minWidth, 0), std::max(m_minHeight, 0));
    if (m_maxWidth > 0 && m_maxHeight > 0) {
      SDL_SetWindowMaximumSize(window, m_maxWidth, m_maxHeight);
    }
  }
}

long wxTopLevelWindow::MSWWindowProc(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
{
  // wxFrame::MSWWindowProc's part: close, size (every kind), activation restoring the child focus
  switch (message) {
  case WM_CLOSE:
    // if it can't close, the message counts as processed - otherwise the system would close it
    (void)Close();
    return 0;

  case WM_SIZE:
    if (HandleSize(LOWORD(lParam), HIWORD(lParam), static_cast<WXUINT>(wParam))) {
      return 0;
    }
    return MSWDefWindowProc(message, wParam, lParam);

  case WM_ACTIVATE: {
    const long result = wxWindow::MSWWindowProc(message, wParam, lParam);
    if (LOWORD(wParam) != WA_INACTIVE) {
      // wxFrame::OnActivate: the focus goes back to the child that had it, else to the first one
      // that takes it
      wxWindow* target = m_lastFocus;
      if (target == nullptr) {
        for (wxWindow* const child : m_children) {
          if (!child->IsTopLevel() && child->AcceptsFocus()) {
            target = child;
            break;
          }
        }
      }
      if (target != nullptr && wxWindow::FindFocus() != target) {
        target->SetFocus();
      }
    }
    return result;
  }

  default:
    return wxWindow::MSWWindowProc(message, wParam, lParam);
  }
}

bool wxTopLevelWindow::HandleSize(const int width, const int height, const WXUINT flag)
{
  switch (flag) {
  case SIZE_RESTORED:
    m_iconized = false;
    m_maximized = false;
    break;
  case SIZE_MAXIMIZED:
    m_iconized = false;
    m_maximized = true;
    break;
  case SIZE_MINIMIZED:
    m_iconized = true;
    break;
  default:
    break;
  }
  if (m_iconized) {
    return false;
  }
  wxSizeEvent event(wxSize(width, height), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

void wxTopLevelWindow::OnCloseWindow(wxCloseEvent&)
{
  Destroy();
}

void wxTopLevelWindow::OnSize(wxSizeEvent&)
{
  if (GetAutoLayout()) {
    Layout();
    return;
  }
  // exactly one child: it fills the client area
  wxWindow* only = nullptr;
  for (wxWindow* const child : m_children) {
    if (child->IsTopLevel()) {
      continue;
    }
    if (only != nullptr) {
      return;
    }
    only = child;
  }
  if (only != nullptr) {
    int width = 0;
    int height = 0;
    GetClientSize(&width, &height);
    only->SetSize(0, 0, width, height);
  }
}

// --- wxDialog ---------------------------------------------------------------------------------------
int wxDialog::ShowModal()
{
  wxlite::Log("wx-lite: dialog \"%s\" is not shown on Linux; it answers Cancel", m_title.mb_str().c_str());
  m_returnCode = wxID_CANCEL;
  return wxID_CANCEL;
}

void wxDialog::OnCloseWindow(wxCloseEvent&)
{
  // wx simulates its Cancel button: the dialog hides, it is not destroyed
  m_returnCode = wxID_CANCEL;
  Show(false);
}

int wxMessageBox(const wxString& message, const wxString& caption, const long style, wxWindow*, int, int)
{
  wxlite::Log("wx-lite message box: %s: %s", caption.mb_str().c_str(), message.mb_str().c_str());
  if ((style & wxCANCEL) != 0) {
    return wxCANCEL;
  }
  if ((style & wxYES_NO) != 0) {
    return 0x0008; // wxNO
  }
  return wxOK;
}
