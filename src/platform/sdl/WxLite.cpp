// Engine addition (faf-neural-ai): wx-lite's core for the native Linux client - strings, the globals,
// wx 2.4.2's event machinery (event tables, the pushed-handler chain, dynamic entries, pending events),
// the application's idle processing and wxWindows/MSW's key-code tables. The windows are in
// WxLiteWindows.cpp, SDL's side (initialisation, the event pump) in SdlApp.cpp.

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

#include "platform/sdl/SdlPrivate.h"

#include "gpg/core/utils/Logging.h"

#include <Windows.h>

// --- globals --------------------------------------------------------------------------------------
const wxPoint wxDefaultPosition(-1, -1);
const wxSize wxDefaultSize(-1, -1);
const wxChar* wxFrameNameStr = L"frame";
const wxChar* wxDialogNameStr = L"dialog";
const wxChar* wxPanelNameStr = L"panel";
wxMBConvUTF8 wxConvUTF8;
namespace
{
  wxColourDatabase sColourDatabase;
  wxBrush sBlackBrush(wxColour(0, 0, 0));
} // namespace
wxColourDatabase* wxTheColourDatabase = &sColourDatabase;
wxBrush wxNullBrush;
wxBrush* wxBLACK_BRUSH = &sBlackBrush;
wxBitmap wxNullBitmap;
wxApp* wxTheApp = nullptr;
wxAppInitializerFunction wxAppCreator = nullptr;
wxWindowList wxTopLevelWindows;

wxAppInitializer::wxAppInitializer(const wxAppInitializerFunction fn)
{
  wxAppCreator = fn;
}

void wxlite::Log(const char* const format, ...)
{
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  gpg::Logf("%s", buffer);
}

// --- wxString -------------------------------------------------------------------------------------
namespace
{
  void AppendUtf8(std::string& out, const std::uint32_t code)
  {
    if (code < 0x80u) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800u) {
      out.push_back(static_cast<char>(0xC0u | (code >> 6)));
      out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else if (code < 0x10000u) {
      out.push_back(static_cast<char>(0xE0u | (code >> 12)));
      out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
      out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else {
      out.push_back(static_cast<char>(0xF0u | (code >> 18)));
      out.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
      out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
      out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    }
  }

  /// UTF-8 to wide; a byte that does not start a valid sequence stands for itself (Latin-1).
  std::wstring DecodeUtf8(const char* const text)
  {
    std::wstring out;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p != 0) {
      const unsigned char lead = *p;
      std::uint32_t code = lead;
      int extra = 0;
      if (lead >= 0xF0u && lead < 0xF8u) {
        code = lead & 0x07u;
        extra = 3;
      } else if (lead >= 0xE0u) {
        code = lead & 0x0Fu;
        extra = 2;
      } else if (lead >= 0xC0u) {
        code = lead & 0x1Fu;
        extra = 1;
      }
      bool valid = lead < 0x80u || extra > 0;
      for (int i = 1; valid && i <= extra; ++i) {
        if ((p[i] & 0xC0u) != 0x80u) {
          valid = false;
        } else {
          code = (code << 6) | (p[i] & 0x3Fu);
        }
      }
      if (!valid) {
        out.push_back(static_cast<wchar_t>(lead));
        ++p;
        continue;
      }
      out.push_back(static_cast<wchar_t>(code));
      p += 1 + extra;
    }
    return out;
  }

  /// MSVC's wide printf reads %s / %c as wide and %S / %C as narrow, the C library's the other way
  /// round: the engine's wx format strings are written for MSVC's.
  std::wstring ToLibcWideFormat(const wxChar* const format)
  {
    std::wstring out;
    for (const wxChar* p = format; *p != 0; ++p) {
      out.push_back(*p);
      if (*p != L'%') {
        continue;
      }
      ++p;
      if (*p == L'%') {
        out.push_back(*p);
        continue;
      }
      bool hasLength = false;
      while (*p != 0 && std::wcschr(L"-+ #0123456789.*hlLqjzt", *p) != nullptr) {
        hasLength = hasLength || std::wcschr(L"hlLqjzt", *p) != nullptr;
        out.push_back(*p);
        ++p;
      }
      if (*p == 0) {
        break;
      }
      if (!hasLength && (*p == L's' || *p == L'c')) {
        out.push_back(L'l');
        out.push_back(*p);
      } else if (!hasLength && (*p == L'S' || *p == L'C')) {
        out.push_back(static_cast<wxChar>(std::towlower(static_cast<wint_t>(*p))));
      } else {
        out.push_back(*p);
      }
    }
    return out;
  }

  std::wstring FormatWide(const wxChar* const format, va_list args)
  {
    const std::wstring libcFormat = ToLibcWideFormat(format != nullptr ? format : L"");
    std::vector<wchar_t> buffer(256);
    for (;;) {
      va_list copy;
      va_copy(copy, args);
      const int written = std::vswprintf(buffer.data(), buffer.size(), libcFormat.c_str(), copy);
      va_end(copy);
      if (written >= 0 && static_cast<std::size_t>(written) < buffer.size()) {
        return std::wstring(buffer.data(), static_cast<std::size_t>(written));
      }
      if (buffer.size() >= (1u << 22)) {
        return std::wstring(buffer.data());
      }
      buffer.resize(buffer.size() * 4);
    }
  }
} // namespace

int wxString::CmpNoCase(const wxChar* const other) const
{
  return ::wcscasecmp(mText.c_str(), other != nullptr ? other : L"");
}

wxString& wxString::MakeLower()
{
  for (wchar_t& ch : mText) {
    ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
  }
  return *this;
}

wxString& wxString::MakeUpper()
{
  for (wchar_t& ch : mText) {
    ch = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch)));
  }
  return *this;
}

std::string wxString::mb_str() const
{
  std::string out;
  out.reserve(mText.size());
  for (const wchar_t ch : mText) {
    AppendUtf8(out, static_cast<std::uint32_t>(ch));
  }
  return out;
}

wxString wxString::FromUTF8(const char* const text)
{
  return wxString(DecodeUtf8(text != nullptr ? text : ""));
}

void wxString::AssignNarrow(const char* const text)
{
  mText = DecodeUtf8(text);
}

wxString wxString::Format(const wxChar* const format, ...)
{
  va_list args;
  va_start(args, format);
  wxString out(FormatWide(format, args));
  va_end(args);
  return out;
}

int wxString::Printf(const wxChar* const format, ...)
{
  va_list args;
  va_start(args, format);
  mText = FormatWide(format, args);
  va_end(args);
  return static_cast<int>(mText.size());
}

// --- events ---------------------------------------------------------------------------------------
namespace
{
  const wxEventTableEntry sEmptyEntries[] = {{wxEVT_NULL, 0, 0, nullptr, nullptr}};

  /// The handlers with queued events (wx's wxPendingEvents) and the lock over every handler's queue
  /// (wxPendingEventsLocker): AddPendingEvent is called from the engine's other threads.
  std::recursive_mutex& PendingLock()
  {
    static std::recursive_mutex lock;
    return lock;
  }
  std::vector<wxEvtHandler*>& PendingHandlers()
  {
    static std::vector<wxEvtHandler*> handlers;
    return handlers;
  }

  /// The windows Destroy()ed since the last idle pass (wx's wxPendingDelete).
  std::vector<wxWindow*>& PendingDelete()
  {
    static std::vector<wxWindow*> windows;
    return windows;
  }

  bool MatchesEntry(const wxEventTableEntry& entry, const wxEventType type, const int id)
  {
    if (entry.m_eventType != type) {
      return false;
    }
    const int id1 = entry.m_id;
    const int id2 = entry.m_lastId;
    return id1 == -1 || (id2 == -1 && id == id1) || (id2 != -1 && id >= id1 && id <= id2);
  }
} // namespace

const wxEventTable wxEvtHandler::sm_eventTable = {nullptr, &sEmptyEntries[0]};

wxEventType wxNewEventType()
{
  static int sLastUsedEventType = wxEVT_FIRST;
  return sLastUsedEventType++;
}

wxEvtHandler::~wxEvtHandler()
{
  // takes itself out of the chain of handlers (wx 2.4.2's ~wxEvtHandler)
  if (m_previousHandler != nullptr) {
    m_previousHandler->m_nextHandler = m_nextHandler;
  }
  if (m_nextHandler != nullptr) {
    m_nextHandler->m_previousHandler = m_previousHandler;
  }

  std::lock_guard<std::recursive_mutex> guard(PendingLock());
  std::vector<wxEvtHandler*>& handlers = PendingHandlers();
  handlers.erase(std::remove(handlers.begin(), handlers.end(), this), handlers.end());
  for (wxEvent* const event : m_pendingEvents) {
    delete event;
  }
  m_pendingEvents.clear();
}

bool wxEvtHandler::ProcessEvent(wxEvent& event)
{
  if (GetEvtHandlerEnabled()) {
    // per-instance dynamic entries first, then the static tables up the class hierarchy
    if (!m_dynamicEvents.empty() && SearchDynamicEventTable(event)) {
      return true;
    }
    for (const wxEventTable* table = GetEventTable(); table != nullptr; table = table->baseTable) {
      if (SearchEventTable(*table, event)) {
        return true;
      }
    }
  }

  // then down the chain of pushed handlers
  if (GetNextHandler() != nullptr && GetNextHandler()->ProcessEvent(event)) {
    return true;
  }

  // command events go on up the parent windows
  if (m_isWindow && event.IsCommandEvent()) {
    wxWindow* const window = static_cast<wxWindow*>(this);
    wxWindow* const parent = window->GetParent();
    if (!window->IsTopLevel() && parent != nullptr && !parent->IsBeingDeleted()) {
      return parent->GetEventHandler()->ProcessEvent(event);
    }
  }

  // last, the application object (never its own idle event: wxApp gets that directly)
  if (wxTheApp != nullptr && this != wxTheApp && event.GetEventType() != wxEVT_IDLE) {
    if (wxTheApp->ProcessEvent(event)) {
      return true;
    }
  }
  return false;
}

bool wxEvtHandler::SearchEventTable(const wxEventTable& table, wxEvent& event)
{
  const wxEventType type = event.GetEventType();
  const int id = event.GetId();
  for (int i = 0; table.entries[i].m_fn != nullptr; ++i) {
    const wxEventTableEntry entry = table.entries[i];
    if (MatchesEntry(entry, type, id)) {
      event.Skip(false);
      event.m_callbackUserData = entry.m_callbackUserData;
      (this->*static_cast<wxEventFunction>(entry.m_fn))(event);
      return !event.GetSkipped();
    }
  }
  return false;
}

bool wxEvtHandler::SearchDynamicEventTable(wxEvent& event)
{
  const wxEventType type = event.GetEventType();
  const int id = event.GetId();
  // the most recent first, as wx inserts them at the front; copied, as a handler may Disconnect
  const std::vector<wxEventTableEntry> entries(m_dynamicEvents.rbegin(), m_dynamicEvents.rend());
  for (const wxEventTableEntry& entry : entries) {
    if (entry.m_fn != nullptr && MatchesEntry(entry, type, id)) {
      event.Skip(false);
      event.m_callbackUserData = entry.m_callbackUserData;
      (this->*static_cast<wxEventFunction>(entry.m_fn))(event);
      if (!event.GetSkipped()) {
        return true;
      }
    }
  }
  return false;
}

void wxEvtHandler::Connect(const int id, const int lastId, const wxEventType type, const wxObjectEventFunction fn,
                           wxObject* const userData)
{
  m_dynamicEvents.push_back(wxEventTableEntry{type, id, lastId, fn, userData});
}

bool wxEvtHandler::Disconnect(const int id, const int lastId, const wxEventType type, const wxObjectEventFunction fn,
                              wxObject* const userData)
{
  for (auto it = m_dynamicEvents.begin(); it != m_dynamicEvents.end(); ++it) {
    if (it->m_id == id && it->m_lastId == lastId && it->m_eventType == type && (fn == nullptr || it->m_fn == fn) &&
        (userData == nullptr || it->m_callbackUserData == userData)) {
      m_dynamicEvents.erase(it);
      return true;
    }
  }
  return false;
}

void wxEvtHandler::AddPendingEvent(wxEvent& event)
{
  wxEvent* const copy = event.Clone();
  {
    std::lock_guard<std::recursive_mutex> guard(PendingLock());
    m_pendingEvents.push_back(copy);
    std::vector<wxEvtHandler*>& handlers = PendingHandlers();
    if (std::find(handlers.begin(), handlers.end(), this) == handlers.end()) {
      handlers.push_back(this);
    }
  }
  wxWakeUpIdle();
}

void wxEvtHandler::ProcessPendingEvents()
{
  for (;;) {
    wxEvent* event = nullptr;
    {
      std::lock_guard<std::recursive_mutex> guard(PendingLock());
      if (m_pendingEvents.empty()) {
        return;
      }
      event = m_pendingEvents.front();
      m_pendingEvents.erase(m_pendingEvents.begin());
    }
    ProcessEvent(*event);
    delete event;
  }
}

// --- the application ------------------------------------------------------------------------------
namespace
{
  const wxEventTableEntry sAppEntries[] = {
    EVT_IDLE(wxAppBase::OnIdle)
    {wxEVT_NULL, 0, 0, nullptr, nullptr}};
} // namespace
const wxEventTable wxApp::sm_eventTable = {&wxEvtHandler::sm_eventTable, &sAppEntries[0]};

bool wxAppBase::ProcessIdle()
{
  wxIdleEvent event;
  event.SetEventObject(this);
  ProcessEvent(event);
  return event.MoreRequested();
}

void wxAppBase::OnIdle(wxIdleEvent& event)
{
  static bool sInOnIdle = false;
  if (sInOnIdle) {
    return;
  }
  sInOnIdle = true;

  ProcessPendingEvents();
  DeletePendingObjects();
  if (SendIdleEvents()) {
    event.RequestMore(true);
  }

  sInOnIdle = false;
}

bool wxAppBase::SendIdleEvents()
{
  bool needMore = false;
  const wxWindowList windows = wxTopLevelWindows;
  for (wxWindow* const window : windows) {
    if (std::find(wxTopLevelWindows.begin(), wxTopLevelWindows.end(), window) != wxTopLevelWindows.end() &&
        SendIdleEvents(window)) {
      needMore = true;
    }
  }
  return needMore;
}

bool wxAppBase::SendIdleEvents(wxWindow* const window)
{
  wxIdleEvent event;
  event.SetEventObject(window);
  window->GetEventHandler()->ProcessEvent(event);
  bool needMore = event.MoreRequested();
  const wxWindowList children = window->GetChildren();
  for (wxWindow* const child : children) {
    if (SendIdleEvents(child)) {
      needMore = true;
    }
  }
  return needMore;
}

void wxAppBase::ProcessPendingEvents()
{
  // One pass over the handlers queued now; a handler deleted meanwhile has left the list itself.
  std::size_t count = 0;
  {
    std::lock_guard<std::recursive_mutex> guard(PendingLock());
    count = PendingHandlers().size();
  }
  for (; count > 0; --count) {
    wxEvtHandler* handler = nullptr;
    {
      std::lock_guard<std::recursive_mutex> guard(PendingLock());
      std::vector<wxEvtHandler*>& handlers = PendingHandlers();
      if (handlers.empty()) {
        return;
      }
      handler = handlers.front();
      handlers.erase(handlers.begin());
    }
    handler->ProcessPendingEvents();
  }
}

void wxAppBase::DeletePendingObjects()
{
  std::vector<wxWindow*>& pending = PendingDelete();
  while (!pending.empty()) {
    wxWindow* const window = pending.front();
    pending.erase(pending.begin());
    delete window;
  }
}

void wxAppBase::SetActive(const bool isActive, wxWindow*)
{
  m_isActive = isActive;
  wxActivateEvent event(wxEVT_ACTIVATE_APP, isActive);
  event.SetEventObject(this);
  ProcessEvent(event);
}

/// wx's wxPendingDelete for the windows (WxLiteWindows.cpp).
void wxliteScheduleDelete(wxWindow* const window)
{
  std::vector<wxWindow*>& pending = PendingDelete();
  if (std::find(pending.begin(), pending.end(), window) == pending.end()) {
    pending.push_back(window);
  }
}

/// A window being deleted leaves the pending lists (WxLiteWindows.cpp).
void wxliteForgetPending(wxWindow* const window)
{
  std::vector<wxWindow*>& pending = PendingDelete();
  pending.erase(std::remove(pending.begin(), pending.end(), window), pending.end());
}

// --- wxWindows/MSW's key-code translation (src/msw/window.cpp, wxWindows 2.4.2) ---------------------

// Returns 0 if was a normal ASCII value, not a special key. This indicates that the key should be
// ignored by WM_KEYDOWN and processed by WM_CHAR instead.
int wxCharCodeMSWToWX(const int keySym)
{
  int id;
  switch (keySym) {
  case VK_CANCEL: id = WXK_CANCEL; break;
  case VK_BACK: id = WXK_BACK; break;
  case VK_TAB: id = WXK_TAB; break;
  case VK_CLEAR: id = WXK_CLEAR; break;
  case VK_RETURN: id = WXK_RETURN; break;
  case VK_SHIFT: id = WXK_SHIFT; break;
  case VK_CONTROL: id = WXK_CONTROL; break;
  case VK_MENU: id = WXK_MENU; break;
  case VK_PAUSE: id = WXK_PAUSE; break;
  case VK_CAPITAL: id = WXK_CAPITAL; break;
  case VK_SPACE: id = WXK_SPACE; break;
  case VK_ESCAPE: id = WXK_ESCAPE; break;
  case VK_PRIOR: id = WXK_PRIOR; break;
  case VK_NEXT: id = WXK_NEXT; break;
  case VK_END: id = WXK_END; break;
  case VK_HOME: id = WXK_HOME; break;
  case VK_LEFT: id = WXK_LEFT; break;
  case VK_UP: id = WXK_UP; break;
  case VK_RIGHT: id = WXK_RIGHT; break;
  case VK_DOWN: id = WXK_DOWN; break;
  case VK_SELECT: id = WXK_SELECT; break;
  case VK_PRINT: id = WXK_PRINT; break;
  case VK_EXECUTE: id = WXK_EXECUTE; break;
  case VK_INSERT: id = WXK_INSERT; break;
  case VK_DELETE: id = WXK_DELETE; break;
  case VK_HELP: id = WXK_HELP; break;
  case VK_NUMPAD0: id = WXK_NUMPAD0; break;
  case VK_NUMPAD1: id = WXK_NUMPAD1; break;
  case VK_NUMPAD2: id = WXK_NUMPAD2; break;
  case VK_NUMPAD3: id = WXK_NUMPAD3; break;
  case VK_NUMPAD4: id = WXK_NUMPAD4; break;
  case VK_NUMPAD5: id = WXK_NUMPAD5; break;
  case VK_NUMPAD6: id = WXK_NUMPAD6; break;
  case VK_NUMPAD7: id = WXK_NUMPAD7; break;
  case VK_NUMPAD8: id = WXK_NUMPAD8; break;
  case VK_NUMPAD9: id = WXK_NUMPAD9; break;
  case VK_MULTIPLY: id = WXK_NUMPAD_MULTIPLY; break;
  case VK_ADD: id = WXK_NUMPAD_ADD; break;
  case VK_SUBTRACT: id = WXK_NUMPAD_SUBTRACT; break;
  case VK_DECIMAL: id = WXK_NUMPAD_DECIMAL; break;
  case VK_DIVIDE: id = WXK_NUMPAD_DIVIDE; break;
  case VK_F1: id = WXK_F1; break;
  case VK_F2: id = WXK_F2; break;
  case VK_F3: id = WXK_F3; break;
  case VK_F4: id = WXK_F4; break;
  case VK_F5: id = WXK_F5; break;
  case VK_F6: id = WXK_F6; break;
  case VK_F7: id = WXK_F7; break;
  case VK_F8: id = WXK_F8; break;
  case VK_F9: id = WXK_F9; break;
  case VK_F10: id = WXK_F10; break;
  case VK_F11: id = WXK_F11; break;
  case VK_F12: id = WXK_F12; break;
  case VK_F13: id = WXK_F13; break;
  case VK_F14: id = WXK_F14; break;
  case VK_F15: id = WXK_F15; break;
  case VK_F16: id = WXK_F16; break;
  case VK_F17: id = WXK_F17; break;
  case VK_F18: id = WXK_F18; break;
  case VK_F19: id = WXK_F19; break;
  case VK_F20: id = WXK_F20; break;
  case VK_F21: id = WXK_F21; break;
  case VK_F22: id = WXK_F22; break;
  case VK_F23: id = WXK_F23; break;
  case VK_F24: id = WXK_F24; break;
  case VK_NUMLOCK: id = WXK_NUMLOCK; break;
  case VK_SCROLL: id = WXK_SCROLL; break;

  case VK_OEM_1: id = ';'; break;
  case VK_OEM_PLUS: id = '+'; break;
  case VK_OEM_COMMA: id = ','; break;
  case VK_OEM_MINUS: id = '-'; break;
  case VK_OEM_PERIOD: id = '.'; break;
  case VK_OEM_2: id = '/'; break;
  case VK_OEM_3: id = '~'; break;
  case VK_OEM_4: id = '['; break;
  case VK_OEM_5: id = '\\'; break;
  case VK_OEM_6: id = ']'; break;
  case VK_OEM_7: id = '\''; break;

  default: id = 0;
  }
  return id;
}

int wxCharCodeWXToMSW(const int id, bool* const isVirtual)
{
  bool virtualKey = true;
  int keySym = 0;
  switch (id) {
  case WXK_CANCEL: keySym = VK_CANCEL; break;
  case WXK_CLEAR: keySym = VK_CLEAR; break;
  case WXK_SHIFT: keySym = VK_SHIFT; break;
  case WXK_CONTROL: keySym = VK_CONTROL; break;
  case WXK_MENU: keySym = VK_MENU; break;
  case WXK_PAUSE: keySym = VK_PAUSE; break;
  case WXK_PRIOR: keySym = VK_PRIOR; break;
  case WXK_NEXT: keySym = VK_NEXT; break;
  case WXK_END: keySym = VK_END; break;
  case WXK_HOME: keySym = VK_HOME; break;
  case WXK_LEFT: keySym = VK_LEFT; break;
  case WXK_UP: keySym = VK_UP; break;
  case WXK_RIGHT: keySym = VK_RIGHT; break;
  case WXK_DOWN: keySym = VK_DOWN; break;
  case WXK_SELECT: keySym = VK_SELECT; break;
  case WXK_PRINT: keySym = VK_PRINT; break;
  case WXK_EXECUTE: keySym = VK_EXECUTE; break;
  case WXK_INSERT: keySym = VK_INSERT; break;
  case WXK_DELETE: keySym = VK_DELETE; break;
  case WXK_HELP: keySym = VK_HELP; break;
  case WXK_NUMPAD0: keySym = VK_NUMPAD0; break;
  case WXK_NUMPAD1: keySym = VK_NUMPAD1; break;
  case WXK_NUMPAD2: keySym = VK_NUMPAD2; break;
  case WXK_NUMPAD3: keySym = VK_NUMPAD3; break;
  case WXK_NUMPAD4: keySym = VK_NUMPAD4; break;
  case WXK_NUMPAD5: keySym = VK_NUMPAD5; break;
  case WXK_NUMPAD6: keySym = VK_NUMPAD6; break;
  case WXK_NUMPAD7: keySym = VK_NUMPAD7; break;
  case WXK_NUMPAD8: keySym = VK_NUMPAD8; break;
  case WXK_NUMPAD9: keySym = VK_NUMPAD9; break;
  case WXK_NUMPAD_MULTIPLY: keySym = VK_MULTIPLY; break;
  case WXK_NUMPAD_ADD: keySym = VK_ADD; break;
  case WXK_NUMPAD_SUBTRACT: keySym = VK_SUBTRACT; break;
  case WXK_NUMPAD_DECIMAL: keySym = VK_DECIMAL; break;
  case WXK_NUMPAD_DIVIDE: keySym = VK_DIVIDE; break;
  case WXK_F1: keySym = VK_F1; break;
  case WXK_F2: keySym = VK_F2; break;
  case WXK_F3: keySym = VK_F3; break;
  case WXK_F4: keySym = VK_F4; break;
  case WXK_F5: keySym = VK_F5; break;
  case WXK_F6: keySym = VK_F6; break;
  case WXK_F7: keySym = VK_F7; break;
  case WXK_F8: keySym = VK_F8; break;
  case WXK_F9: keySym = VK_F9; break;
  case WXK_F10: keySym = VK_F10; break;
  case WXK_F11: keySym = VK_F11; break;
  case WXK_F12: keySym = VK_F12; break;
  case WXK_F13: keySym = VK_F13; break;
  case WXK_F14: keySym = VK_F14; break;
  case WXK_F15: keySym = VK_F15; break;
  case WXK_F16: keySym = VK_F16; break;
  case WXK_F17: keySym = VK_F17; break;
  case WXK_F18: keySym = VK_F18; break;
  case WXK_F19: keySym = VK_F19; break;
  case WXK_F20: keySym = VK_F20; break;
  case WXK_F21: keySym = VK_F21; break;
  case WXK_F22: keySym = VK_F22; break;
  case WXK_F23: keySym = VK_F23; break;
  case WXK_F24: keySym = VK_F24; break;
  case WXK_NUMLOCK: keySym = VK_NUMLOCK; break;
  case WXK_SCROLL: keySym = VK_SCROLL; break;
  default:
    virtualKey = false;
    keySym = id;
    break;
  }
  if (isVirtual != nullptr) {
    *isVirtual = virtualKey;
  }
  return keySym;
}

bool wxIsShiftDown()
{
  return ::GetKeyState(VK_SHIFT) < 0;
}

bool wxIsCtrlDown()
{
  return ::GetKeyState(VK_CONTROL) < 0;
}
