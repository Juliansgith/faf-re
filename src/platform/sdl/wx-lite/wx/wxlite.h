#pragma once
// Engine addition (faf-neural-ai): wx-lite, the part of wxWindows 2.4.2's API the engine's client code
// names, for the native Linux client. The engine draws its whole UI itself (maui, on the render device);
// wx gave the Windows build its top-level window, its event loop and its keyboard and mouse events, plus
// a few tool windows and dialogs. Here:
//   - the event machinery is wx 2.4's: event tables (DECLARE_EVENT_TABLE, BEGIN_EVENT_TABLE, EVT_*),
//     wxEvtHandler::ProcessEvent through the table chain and the pushed-handler chain, the same key codes
//     and event types (wxlite_codes.h, generated from the real headers);
//   - windows keep position, size, parent, children, shown state and title; the top-level frame owns an
//     SDL window (platform/sdl/SdlApp.cpp), whose events become wxKeyEvent / wxMouseEvent / wxSizeEvent;
//   - dialogs, widgets, bitmaps, sizers, files and URLs exist so the engine's declarations compile, and do
//     nothing: on Linux the tool windows are not built and the startup dialogs answer as cancelled.
// Nothing here is used by the simulation.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "wx/wxlite_codes.h"

#define wxMAJOR_VERSION 2
#define wxMINOR_VERSION 4
#define wxRELEASE_NUMBER 2
#define wxUSE_UNICODE 1
#define wxUSE_GUI 1
#define WXDLLEXPORT
#define WXDLLIMPEXP_CORE
#define WXDLLIMPEXP_BASE
#define wxCALLBACK
#define wxStdCall

typedef wchar_t wxChar;
// An integer, as wxWindows/MSW's (the engine casts it with static_cast<std::uintptr_t>); on Linux it holds
// the SDL_Window* of the top-level window.
typedef std::uintptr_t WXHWND;
typedef WXHWND WXWidget;
typedef void* WXHINSTANCE;
typedef unsigned int WXUINT;
typedef std::uintptr_t WXWPARAM;
typedef std::intptr_t WXLPARAM;
typedef int wxWindowID;
typedef long wxCoord;
#define wxT(x) L##x
#define _T(x) L##x
#define wxEmptyString wxString()
#define wxASSERT(cond) ((void)0)
#define wxASSERT_MSG(cond, msg) ((void)0)
#define wxFAIL_MSG(msg) ((void)0)
#define wxCHECK_RET(cond, msg) do { if (!(cond)) return; } while (0)
#define wxCHECK_MSG(cond, rc, msg) do { if (!(cond)) return (rc); } while (0)
#define WXUNUSED(x)
inline void wxAssert(int, const wxChar*, int, const wxChar*, const wxChar* = nullptr) {}

enum
{
  wxID_ANY = -1,
  wxID_LOWEST = 4999,
  wxID_OPEN = 5000,
  wxID_CLOSE,
  wxID_NEW,
  wxID_SAVE,
  wxID_SAVEAS,
  wxID_REVERT,
  wxID_EXIT,
  wxID_UNDO,
  wxID_REDO,
  wxID_HELP,
  wxID_PRINT,
  wxID_PRINT_SETUP,
  wxID_PREVIEW,
  wxID_ABOUT,
  wxID_OK = 5100,
  wxID_CANCEL,
  wxID_APPLY,
  wxID_YES,
  wxID_NO,
  wxID_HIGHEST = 5999
};

// Styles: the values only have to be distinct; nothing reads them back on Linux.
enum : long
{
  wxCAPTION = 0x20000000,
  wxMINIMIZE_BOX = 0x0400,
  wxMAXIMIZE_BOX = 0x0200,
  wxRESIZE_BORDER = 0x0040,
  wxSYSTEM_MENU = 0x0800,
  wxCLOSE_BOX = 0x1000,
  wxSTAY_ON_TOP = 0x8000,
  wxSIMPLE_BORDER = 0x02000000,
  wxBORDER_NONE = 0x00200000,
  wxNO_BORDER = 0x00200000,
  wxCLIP_CHILDREN = 0x00400000,
  wxTAB_TRAVERSAL = 0x00080000,
  wxWANTS_CHARS = 0x00040000,
  wxDEFAULT_FRAME_STYLE = wxSYSTEM_MENU | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCAPTION | wxCLIP_CHILDREN,
  wxDEFAULT_DIALOG_STYLE = wxSYSTEM_MENU | wxCAPTION,
  wxBU_AUTODRAW = 0x0004,
  wxDD_NEW_DIR_BUTTON = 0x0080,
  wxSPLASH_CENTRE_ON_SCREEN = 0x01,
  wxSPLASH_NO_TIMEOUT = 0x00,
  wxSPLASH_TIMEOUT = 0x04,
  wxHORIZONTAL = 0x0004,
  wxVERTICAL = 0x0008,
  wxALL = 0x00f0,
  wxEXPAND = 0x2000,
  wxALIGN_CENTER = 0x0900,
  wxALIGN_CENTRE = 0x0900,
  wxTE_MULTILINE = 0x0020,
  wxTE_READONLY = 0x0010,
  wxTE_RICH = 0x0080,
  wxSIZE_USE_EXISTING = 0x0000,
  wxSIZE_AUTO = 0x0003,
  wxOK = 0x0004,
  wxYES_NO = 0x000A,
  wxCANCEL = 0x0010,
  wxICON_ERROR = 0x0200,
  wxICON_INFORMATION = 0x0800,
};

enum wxBitmapType
{
  wxBITMAP_TYPE_INVALID,
  wxBITMAP_TYPE_BMP,
  wxBITMAP_TYPE_BMP_RESOURCE,
  wxBITMAP_TYPE_ICO,
  wxBITMAP_TYPE_ICO_RESOURCE,
  wxBITMAP_TYPE_PNG,
  wxBITMAP_TYPE_ANY = 50
};

// --- strings ------------------------------------------------------------------------------
class wxString
{
public:
  wxString() = default;
  wxString(const wxChar* text) : mText(text != nullptr ? text : L"") {}
  wxString(const wxChar* text, std::size_t length) : mText(text, length) {}
  wxString(const std::wstring& text) : mText(text) {}
  wxString(const char* text) { AssignNarrow(text != nullptr ? text : ""); }
  wxString(wxChar ch, std::size_t count = 1) : mText(count, ch) {}

  const wxChar* c_str() const noexcept { return mText.c_str(); }
  const wxChar* GetData() const noexcept { return mText.c_str(); }
  operator const wxChar*() const noexcept { return mText.c_str(); }
  std::size_t Len() const noexcept { return mText.size(); }
  std::size_t length() const noexcept { return mText.size(); }
  std::size_t Length() const noexcept { return mText.size(); }
  bool IsEmpty() const noexcept { return mText.empty(); }
  bool empty() const noexcept { return mText.empty(); }
  void Clear() { mText.clear(); }
  void Empty() { mText.clear(); }
  wxChar GetChar(std::size_t index) const { return mText[index]; }
  wxChar operator[](std::size_t index) const { return mText[index]; }
  wxString& operator=(const wxChar* text) { mText = text != nullptr ? text : L""; return *this; }
  wxString& operator+=(const wxString& other) { mText += other.mText; return *this; }
  wxString& operator+=(const wxChar* other) { mText += other; return *this; }
  wxString& operator+=(wxChar ch) { mText += ch; return *this; }
  wxString& Append(const wxChar* other) { mText += other; return *this; }
  wxString& Append(wxChar ch, std::size_t count = 1) { mText.append(count, ch); return *this; }
  bool operator==(const wxString& other) const { return mText == other.mText; }
  bool operator!=(const wxString& other) const { return mText != other.mText; }
  bool operator==(const wxChar* other) const { return mText == (other != nullptr ? other : L""); }
  int Cmp(const wxChar* other) const { return mText.compare(other); }
  int CmpNoCase(const wxChar* other) const;
  int Find(wxChar ch, bool fromEnd = false) const
  {
    const std::size_t at = fromEnd ? mText.rfind(ch) : mText.find(ch);
    return at == std::wstring::npos ? -1 : static_cast<int>(at);
  }
  wxString Mid(std::size_t first, std::size_t count = std::wstring::npos) const
  {
    return first >= mText.size() ? wxString() : wxString(mText.substr(first, count));
  }
  wxString Left(std::size_t count) const { return wxString(mText.substr(0, count)); }
  wxString Right(std::size_t count) const
  {
    return count >= mText.size() ? *this : wxString(mText.substr(mText.size() - count));
  }
  wxString& MakeLower();
  wxString& MakeUpper();
  /// UTF-8, as the engine hands strings to its own code.
  std::string mb_str() const;
  std::string ToUTF8() const { return mb_str(); }
  static wxString FromUTF8(const char* text);
  static wxString Format(const wxChar* format, ...);
  int Printf(const wxChar* format, ...);
  const std::wstring& Native() const noexcept { return mText; }

private:
  void AssignNarrow(const char* text);
  std::wstring mText;
};
inline wxString operator+(wxString left, const wxString& right) { left += right; return left; }
inline wxString operator+(wxString left, const wxChar* right) { left += right; return left; }
inline wxString operator+(const wxChar* left, const wxString& right) { wxString out(left); out += right; return out; }

struct wxMBConvUTF8
{
  std::string cWC2MB(const wxChar* text) const { return wxString(text).mb_str(); }
  wxString cMB2WC(const char* text) const { return wxString::FromUTF8(text); }
};
extern wxMBConvUTF8 wxConvUTF8;

// --- geometry, colours, bitmaps -------------------------------------------------------------
struct wxPoint
{
  int x = 0;
  int y = 0;
  wxPoint() = default;
  wxPoint(int px, int py) : x(px), y(py) {}
  bool operator==(const wxPoint& o) const { return x == o.x && y == o.y; }
  bool operator!=(const wxPoint& o) const { return !(*this == o); }
};
struct wxSize
{
  int x = 0;
  int y = 0;
  wxSize() = default;
  wxSize(int w, int h) : x(w), y(h) {}
  int GetWidth() const { return x; }
  int GetHeight() const { return y; }
  bool operator==(const wxSize& o) const { return x == o.x && y == o.y; }
  bool operator!=(const wxSize& o) const { return !(*this == o); }
};
struct wxRect
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  wxRect() = default;
  wxRect(int px, int py, int w, int h) : x(px), y(py), width(w), height(h) {}
  int GetX() const { return x; }
  int GetY() const { return y; }
  int GetWidth() const { return width; }
  int GetHeight() const { return height; }
};
extern const wxPoint wxDefaultPosition;
extern const wxSize wxDefaultSize;
extern const wxChar* wxFrameNameStr;
extern const wxChar* wxDialogNameStr;
extern const wxChar* wxPanelNameStr;

class wxObject
{
public:
  virtual ~wxObject() = default;
};

class wxColour : public wxObject
{
public:
  wxColour() = default;
  wxColour(unsigned char r, unsigned char g, unsigned char b) : mRed(r), mGreen(g), mBlue(b), mOk(true) {}
  unsigned char Red() const { return mRed; }
  unsigned char Green() const { return mGreen; }
  unsigned char Blue() const { return mBlue; }
  bool Ok() const { return mOk; }

private:
  unsigned char mRed = 0, mGreen = 0, mBlue = 0;
  bool mOk = false;
};
class wxColourDatabase : public wxObject
{
public:
  wxColour Find(const wxString&) const { return wxColour(); }
};
extern wxColourDatabase* wxTheColourDatabase;

class wxBrush : public wxObject
{
public:
  wxBrush() = default;
  wxBrush(const wxColour&, int = 0) {}
};
extern wxBrush wxNullBrush;
extern wxBrush* wxBLACK_BRUSH;
class wxFont : public wxObject
{
};
class wxImage : public wxObject
{
public:
  wxImage() = default;
  wxImage(const wxString&, long = wxBITMAP_TYPE_ANY) {}
  bool Ok() const { return false; }
  static void AddHandler(wxObject*) {}
};
class wxPNGHandler : public wxObject
{
};
class wxBitmap : public wxObject
{
public:
  wxBitmap() = default;
  wxBitmap(const wxString&, long = wxBITMAP_TYPE_ANY) {}
  wxBitmap(const wxImage&) {}
  bool Ok() const { return false; }
  int GetWidth() const { return 0; }
  int GetHeight() const { return 0; }
};
extern wxBitmap wxNullBitmap;
class wxIcon : public wxBitmap
{
public:
  wxIcon() = default;
  wxIcon(const wxString&, long = wxBITMAP_TYPE_ANY, int = -1, int = -1) {}
};
class wxImageList : public wxObject
{
};

// --- events ---------------------------------------------------------------------------------
class wxEvtHandler;
class wxWindow;

class wxEvent : public wxObject
{
public:
  explicit wxEvent(int id = 0, wxEventType type = wxEVT_NULL) : m_eventType(type), m_id(id) {}
  wxEventType GetEventType() const { return m_eventType; }
  void SetEventType(wxEventType type) { m_eventType = type; }
  wxObject* GetEventObject() const { return m_eventObject; }
  void SetEventObject(wxObject* object) { m_eventObject = object; }
  int GetId() const { return m_id; }
  void SetId(int id) { m_id = id; }
  long GetTimestamp() const { return m_timeStamp; }
  void Skip(bool skip = true) { m_skipped = skip; }
  bool GetSkipped() const { return m_skipped; }
  bool IsCommandEvent() const { return m_isCommandEvent; }
  virtual wxEvent* Clone() const { return new wxEvent(*this); }

  wxObject* m_eventObject = nullptr;
  wxEventType m_eventType = wxEVT_NULL;
  long m_timeStamp = 0;
  int m_id = 0;
  wxObject* m_callbackUserData = nullptr;
  bool m_skipped = false;
  bool m_isCommandEvent = false;
};

class wxCommandEvent : public wxEvent
{
public:
  explicit wxCommandEvent(wxEventType type = wxEVT_NULL, int id = 0) : wxEvent(id, type) { m_isCommandEvent = true; }
  wxEvent* Clone() const override { return new wxCommandEvent(*this); }
  long GetInt() const { return m_commandInt; }
  void SetInt(long value) { m_commandInt = value; }
  const wxString& GetString() const { return m_commandString; }
  void SetString(const wxString& text) { m_commandString = text; }
  bool IsChecked() const { return m_commandInt != 0; }

  wxString m_commandString;
  long m_commandInt = 0;
  long m_extraLong = 0;
  void* m_clientData = nullptr;
};

class wxKeyEvent : public wxEvent
{
public:
  explicit wxKeyEvent(wxEventType type = wxEVT_NULL) : wxEvent(0, type) {}
  wxEvent* Clone() const override { return new wxKeyEvent(*this); }
  long GetKeyCode() const { return m_keyCode; }
  long KeyCode() const { return m_keyCode; }
  bool ControlDown() const { return m_controlDown; }
  bool ShiftDown() const { return m_shiftDown; }
  bool AltDown() const { return m_altDown; }
  bool MetaDown() const { return m_metaDown; }
  bool HasModifiers() const { return m_controlDown || m_altDown || m_metaDown; }
  long GetX() const { return m_x; }
  long GetY() const { return m_y; }
  unsigned long GetRawKeyCode() const { return m_rawCode; }
  unsigned long GetRawKeyFlags() const { return m_rawFlags; }

  long m_x = 0, m_y = 0;
  long m_keyCode = 0;
  bool m_controlDown = false, m_shiftDown = false, m_altDown = false, m_metaDown = false;
  bool m_scanCode = false;
  unsigned long m_rawCode = 0;
  unsigned long m_rawFlags = 0;
};

class wxMouseEvent : public wxEvent
{
public:
  explicit wxMouseEvent(wxEventType type = wxEVT_NULL) : wxEvent(0, type) {}
  wxEvent* Clone() const override { return new wxMouseEvent(*this); }
  long GetX() const { return m_x; }
  long GetY() const { return m_y; }
  wxPoint GetPosition() const { return wxPoint(static_cast<int>(m_x), static_cast<int>(m_y)); }
  bool LeftDown() const { return m_eventType == wxEVT_LEFT_DOWN; }
  bool LeftUp() const { return m_eventType == wxEVT_LEFT_UP; }
  bool RightDown() const { return m_eventType == wxEVT_RIGHT_DOWN; }
  bool RightUp() const { return m_eventType == wxEVT_RIGHT_UP; }
  bool MiddleDown() const { return m_eventType == wxEVT_MIDDLE_DOWN; }
  bool MiddleUp() const { return m_eventType == wxEVT_MIDDLE_UP; }
  bool LeftDClick() const { return m_eventType == wxEVT_LEFT_DCLICK; }
  bool RightDClick() const { return m_eventType == wxEVT_RIGHT_DCLICK; }
  bool MiddleDClick() const { return m_eventType == wxEVT_MIDDLE_DCLICK; }
  bool LeftIsDown() const { return m_leftDown; }
  bool RightIsDown() const { return m_rightDown; }
  bool MiddleIsDown() const { return m_middleDown; }
  bool Dragging() const { return m_eventType == wxEVT_MOTION && (m_leftDown || m_rightDown || m_middleDown); }
  bool Moving() const { return m_eventType == wxEVT_MOTION && !Dragging(); }
  bool Entering() const { return m_eventType == wxEVT_ENTER_WINDOW; }
  bool Leaving() const { return m_eventType == wxEVT_LEAVE_WINDOW; }
  bool ControlDown() const { return m_controlDown; }
  bool ShiftDown() const { return m_shiftDown; }
  bool AltDown() const { return m_altDown; }
  bool MetaDown() const { return m_metaDown; }
  int GetWheelRotation() const { return m_wheelRotation; }
  int GetWheelDelta() const { return m_wheelDelta; }
  int GetLinesPerAction() const { return m_linesPerAction; }
  bool ButtonDown(int = -1) const
  {
    return m_eventType == wxEVT_LEFT_DOWN || m_eventType == wxEVT_RIGHT_DOWN || m_eventType == wxEVT_MIDDLE_DOWN;
  }
  bool ButtonUp(int = -1) const
  {
    return m_eventType == wxEVT_LEFT_UP || m_eventType == wxEVT_RIGHT_UP || m_eventType == wxEVT_MIDDLE_UP;
  }

  long m_x = 0, m_y = 0;
  bool m_leftDown = false, m_middleDown = false, m_rightDown = false;
  bool m_controlDown = false, m_shiftDown = false, m_altDown = false, m_metaDown = false;
  int m_wheelRotation = 0;
  int m_wheelDelta = 120;
  int m_linesPerAction = 3;
};

class wxSizeEvent : public wxEvent
{
public:
  wxSizeEvent(const wxSize& size = wxSize(), int id = 0) : wxEvent(id, wxEVT_SIZE), m_size(size) {}
  wxSize GetSize() const { return m_size; }
  wxSize m_size;
};
class wxMoveEvent : public wxEvent
{
public:
  wxMoveEvent(const wxPoint& pos = wxPoint(), int id = 0) : wxEvent(id, wxEVT_MOVE), m_pos(pos) {}
  wxPoint GetPosition() const { return m_pos; }
  wxPoint m_pos;
};
class wxCloseEvent : public wxEvent
{
public:
  explicit wxCloseEvent(wxEventType type = wxEVT_NULL, int id = 0) : wxEvent(id, type) {}
  bool CanVeto() const { return m_canVeto; }
  void Veto(bool veto = true) { m_veto = veto; }
  bool GetVeto() const { return m_veto; }
  bool m_canVeto = true;
  bool m_veto = false;
};
class wxPaintEvent : public wxEvent
{
public:
  explicit wxPaintEvent(int id = 0) : wxEvent(id, wxEVT_PAINT) {}
};
class wxEraseEvent : public wxEvent
{
public:
  explicit wxEraseEvent(int id = 0) : wxEvent(id, wxEVT_ERASE_BACKGROUND) {}
};
class wxIdleEvent : public wxEvent
{
public:
  wxIdleEvent() : wxEvent(0, wxEVT_IDLE) {}
  void RequestMore(bool more = true) { m_requestMore = more; }
  bool MoreRequested() const { return m_requestMore; }
  bool m_requestMore = false;
};
class wxActivateEvent : public wxEvent
{
public:
  explicit wxActivateEvent(wxEventType type = wxEVT_NULL, bool active = true, int id = 0)
    : wxEvent(id, type), m_active(active) {}
  bool GetActive() const { return m_active; }
  bool m_active;
};
class wxFocusEvent : public wxEvent
{
public:
  explicit wxFocusEvent(wxEventType type = wxEVT_NULL, int id = 0) : wxEvent(id, type) {}
};
class wxScrollEvent : public wxCommandEvent
{
public:
  int GetPosition() const { return static_cast<int>(m_commandInt); }
};
class wxTreeEvent : public wxCommandEvent
{
};
class wxListEvent : public wxCommandEvent
{
};
class wxNotebookEvent : public wxCommandEvent
{
};

typedef void (wxObject::*wxObjectEventFunction)(wxEvent&);
typedef void (wxEvtHandler::*wxEventFunction)(wxEvent&);
typedef void (wxEvtHandler::*wxCommandEventFunction)(wxCommandEvent&);
typedef void (wxEvtHandler::*wxCharEventFunction)(wxKeyEvent&);
typedef void (wxEvtHandler::*wxMouseEventFunction)(wxMouseEvent&);
typedef void (wxEvtHandler::*wxSizeEventFunction)(wxSizeEvent&);
typedef void (wxEvtHandler::*wxMoveEventFunction)(wxMoveEvent&);
typedef void (wxEvtHandler::*wxCloseEventFunction)(wxCloseEvent&);
typedef void (wxEvtHandler::*wxPaintEventFunction)(wxPaintEvent&);
typedef void (wxEvtHandler::*wxEraseEventFunction)(wxEraseEvent&);
typedef void (wxEvtHandler::*wxIdleEventFunction)(wxIdleEvent&);
typedef void (wxEvtHandler::*wxActivateEventFunction)(wxActivateEvent&);
typedef void (wxEvtHandler::*wxFocusEventFunction)(wxFocusEvent&);
typedef void (wxEvtHandler::*wxScrollEventFunction)(wxScrollEvent&);
typedef void (wxEvtHandler::*wxTreeEventFunction)(wxTreeEvent&);
typedef void (wxEvtHandler::*wxListEventFunction)(wxListEvent&);
typedef void (wxEvtHandler::*wxNotebookEventFunction)(wxNotebookEvent&);

struct wxEventTableEntry
{
  int m_eventType;
  int m_id;
  int m_lastId;
  wxObjectEventFunction m_fn;
  wxObject* m_callbackUserData;
};
struct wxEventTable
{
  const wxEventTable* baseTable;
  const wxEventTableEntry* entries;
};

#define DECLARE_EVENT_TABLE()                                                                              \
private:                                                                                                   \
  static const wxEventTableEntry sm_eventTableEntries[];                                                   \
                                                                                                           \
protected:                                                                                                 \
  static const wxEventTable sm_eventTable;                                                                 \
  const wxEventTable* GetEventTable() const override;                                                      \
                                                                                                           \
private:

#define BEGIN_EVENT_TABLE(theClass, baseClass)                                                             \
  const wxEventTable theClass::sm_eventTable = {&baseClass::sm_eventTable, &theClass::sm_eventTableEntries[0]}; \
  const wxEventTable* theClass::GetEventTable() const { return &theClass::sm_eventTable; }                 \
  const wxEventTableEntry theClass::sm_eventTableEntries[] = {
#define END_EVENT_TABLE() {wxEVT_NULL, 0, 0, nullptr, nullptr}};

#define DECLARE_EVENT_TABLE_ENTRY(type, id, idLast, fn, obj) {type, id, idLast, fn, obj},
#define wxEVENT_ENTRY(type, id, fnType, fn) \
  DECLARE_EVENT_TABLE_ENTRY(type, id, -1, (wxObjectEventFunction)(wxEventFunction)(fnType)&fn, nullptr)

#define EVT_KEY_DOWN(fn) wxEVENT_ENTRY(wxEVT_KEY_DOWN, -1, wxCharEventFunction, fn)
#define EVT_KEY_UP(fn) wxEVENT_ENTRY(wxEVT_KEY_UP, -1, wxCharEventFunction, fn)
#define EVT_CHAR(fn) wxEVENT_ENTRY(wxEVT_CHAR, -1, wxCharEventFunction, fn)
#define EVT_SIZE(fn) wxEVENT_ENTRY(wxEVT_SIZE, -1, wxSizeEventFunction, fn)
#define EVT_MOVE(fn) wxEVENT_ENTRY(wxEVT_MOVE, -1, wxMoveEventFunction, fn)
#define EVT_CLOSE(fn) wxEVENT_ENTRY(wxEVT_CLOSE_WINDOW, -1, wxCloseEventFunction, fn)
#define EVT_PAINT(fn) wxEVENT_ENTRY(wxEVT_PAINT, -1, wxPaintEventFunction, fn)
#define EVT_ERASE_BACKGROUND(fn) wxEVENT_ENTRY(wxEVT_ERASE_BACKGROUND, -1, wxEraseEventFunction, fn)
#define EVT_IDLE(fn) wxEVENT_ENTRY(wxEVT_IDLE, -1, wxIdleEventFunction, fn)
#define EVT_ACTIVATE(fn) wxEVENT_ENTRY(wxEVT_ACTIVATE, -1, wxActivateEventFunction, fn)
#define EVT_ACTIVATE_APP(fn) wxEVENT_ENTRY(wxEVT_ACTIVATE_APP, -1, wxActivateEventFunction, fn)
#define EVT_SET_FOCUS(fn) wxEVENT_ENTRY(wxEVT_SET_FOCUS, -1, wxFocusEventFunction, fn)
#define EVT_KILL_FOCUS(fn) wxEVENT_ENTRY(wxEVT_KILL_FOCUS, -1, wxFocusEventFunction, fn)
#define EVT_LEFT_DOWN(fn) wxEVENT_ENTRY(wxEVT_LEFT_DOWN, -1, wxMouseEventFunction, fn)
#define EVT_LEFT_UP(fn) wxEVENT_ENTRY(wxEVT_LEFT_UP, -1, wxMouseEventFunction, fn)
#define EVT_RIGHT_DOWN(fn) wxEVENT_ENTRY(wxEVT_RIGHT_DOWN, -1, wxMouseEventFunction, fn)
#define EVT_RIGHT_UP(fn) wxEVENT_ENTRY(wxEVT_RIGHT_UP, -1, wxMouseEventFunction, fn)
#define EVT_MOTION(fn) wxEVENT_ENTRY(wxEVT_MOTION, -1, wxMouseEventFunction, fn)
#define EVT_ENTER_WINDOW(fn) wxEVENT_ENTRY(wxEVT_ENTER_WINDOW, -1, wxMouseEventFunction, fn)
#define EVT_LEAVE_WINDOW(fn) wxEVENT_ENTRY(wxEVT_LEAVE_WINDOW, -1, wxMouseEventFunction, fn)
#define EVT_MOUSEWHEEL(fn) wxEVENT_ENTRY(wxEVT_MOUSEWHEEL, -1, wxMouseEventFunction, fn)
#define EVT_MOUSE_EVENTS(fn)                                                                               \
  EVT_LEFT_DOWN(fn) EVT_LEFT_UP(fn) EVT_RIGHT_DOWN(fn) EVT_RIGHT_UP(fn)                                    \
  wxEVENT_ENTRY(wxEVT_MIDDLE_DOWN, -1, wxMouseEventFunction, fn)                                           \
  wxEVENT_ENTRY(wxEVT_MIDDLE_UP, -1, wxMouseEventFunction, fn)                                             \
  wxEVENT_ENTRY(wxEVT_LEFT_DCLICK, -1, wxMouseEventFunction, fn)                                           \
  wxEVENT_ENTRY(wxEVT_RIGHT_DCLICK, -1, wxMouseEventFunction, fn)                                          \
  wxEVENT_ENTRY(wxEVT_MIDDLE_DCLICK, -1, wxMouseEventFunction, fn)                                         \
  EVT_MOTION(fn) EVT_ENTER_WINDOW(fn) EVT_LEAVE_WINDOW(fn) EVT_MOUSEWHEEL(fn)
#define EVT_BUTTON(id, fn) wxEVENT_ENTRY(wxEVT_COMMAND_BUTTON_CLICKED, id, wxCommandEventFunction, fn)
#define EVT_CHECKBOX(id, fn) wxEVENT_ENTRY(wxEVT_COMMAND_CHECKBOX_CLICKED, id, wxCommandEventFunction, fn)
#define EVT_TEXT(id, fn) wxEVENT_ENTRY(wxEVT_COMMAND_TEXT_UPDATED, id, wxCommandEventFunction, fn)
#define EVT_TEXT_ENTER(id, fn) wxEVENT_ENTRY(wxEVT_COMMAND_TEXT_ENTER, id, wxCommandEventFunction, fn)
#define EVT_MENU(id, fn) wxEVENT_ENTRY(wxEVT_COMMAND_MENU_SELECTED, id, wxCommandEventFunction, fn)
#define EVT_COMMAND(id, type, fn) wxEVENT_ENTRY(type, id, wxCommandEventFunction, fn)
#define EVT_CUSTOM(type, id, fn) wxEVENT_ENTRY(type, id, wxEventFunction, fn)

wxEventType wxNewEventType();

class wxEvtHandler : public wxObject
{
public:
  wxEvtHandler() = default;
  ~wxEvtHandler() override;

  wxEvtHandler* GetNextHandler() const { return m_nextHandler; }
  wxEvtHandler* GetPreviousHandler() const { return m_previousHandler; }
  void SetNextHandler(wxEvtHandler* handler) { m_nextHandler = handler; }
  void SetPreviousHandler(wxEvtHandler* handler) { m_previousHandler = handler; }
  void SetEvtHandlerEnabled(bool enabled) { m_enabled = enabled; }
  bool GetEvtHandlerEnabled() const { return m_enabled; }

  /// wx 2.4's: this handler's tables, then the next handler's, until one handles the event.
  virtual bool ProcessEvent(wxEvent& event);
  void AddPendingEvent(wxEvent& event);
  void ProcessPendingEvents();
  bool SearchEventTable(const wxEventTable& table, wxEvent& event);
  void Connect(int id, int lastId, wxEventType type, wxObjectEventFunction fn, wxObject* userData = nullptr);
  void Connect(int id, wxEventType type, wxObjectEventFunction fn, wxObject* userData = nullptr)
  {
    Connect(id, -1, type, fn, userData);
  }

  static const wxEventTable sm_eventTable;
  virtual const wxEventTable* GetEventTable() const { return &sm_eventTable; }

protected:
  wxEvtHandler* m_nextHandler = nullptr;
  wxEvtHandler* m_previousHandler = nullptr;
  std::vector<wxEventTableEntry> m_dynamicEvents;
  std::vector<wxEvent*> m_pendingEvents;
  bool m_enabled = true;
};

// --- windows --------------------------------------------------------------------------------
class wxSizer;
class wxDC;
class wxLayoutConstraints;
class wxAcceleratorTable : public wxObject
{
};

class wxWindowBase : public wxEvtHandler
{
public:
  static const wxEventTable sm_eventTable;
  const wxEventTable* GetEventTable() const override { return &sm_eventTable; }
};

class wxWindow : public wxWindowBase
{
public:
  wxWindow() = default;
  wxWindow(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
           long style = 0, const wxString& name = wxPanelNameStr)
  {
    Create(parent, id, pos, size, style, name);
  }
  ~wxWindow() override;
  bool Create(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = 0, const wxString& name = wxPanelNameStr);

  wxWindow* GetParent() const { return m_parent; }
  wxWindow* GetGrandParent() const { return m_parent != nullptr ? m_parent->m_parent : nullptr; }
  const std::vector<wxWindow*>& GetChildren() const { return m_children; }
  wxWindowID GetId() const { return m_windowId; }
  void SetId(wxWindowID id) { m_windowId = id; }
  wxString GetName() const { return m_windowName; }
  void SetName(const wxString& name) { m_windowName = name; }
  virtual void SetTitle(const wxString& title) { m_title = title; }
  virtual wxString GetTitle() const { return m_title; }
  void SetLabel(const wxString& label) { m_title = label; }
  wxString GetLabel() const { return m_title; }
  long GetWindowStyleFlag() const { return m_windowStyle; }
  long GetWindowStyle() const { return m_windowStyle; }
  void SetWindowStyleFlag(long style) { m_windowStyle = style; }
  void SetWindowStyle(long style) { m_windowStyle = style; }

  virtual bool Show(bool show = true);
  bool Hide() { return Show(false); }
  bool IsShown() const { return m_isShown; }
  virtual bool Enable(bool enable = true) { m_isEnabled = enable; return true; }
  bool Disable() { return Enable(false); }
  bool IsEnabled() const { return m_isEnabled; }
  virtual bool IsTopLevel() const { return false; }
  virtual bool Close(bool force = false);
  virtual bool Destroy();
  bool DestroyChildren();
  virtual void Raise() {}
  virtual void Lower() {}
  virtual void SetFocus();
  static wxWindow* FindFocus();
  virtual void CaptureMouse();
  virtual void ReleaseMouse();
  static wxWindow* GetCapture();
  bool HasCapture() const { return GetCapture() == this; }
  virtual void Refresh(bool = true, const wxRect* = nullptr) {}
  void Update() {}
  void Freeze() {}
  void Thaw() {}
  void Layout() {}
  void Fit() {}
  void Centre(int = wxHORIZONTAL | wxVERTICAL) {}
  void Center(int = wxHORIZONTAL | wxVERTICAL) {}
  void CentreOnScreen(int = wxHORIZONTAL | wxVERTICAL) {}

  virtual void SetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);
  void SetSize(int width, int height) { SetSize(-1, -1, width, height, wxSIZE_USE_EXISTING); }
  void SetSize(const wxSize& size) { SetSize(-1, -1, size.x, size.y, wxSIZE_USE_EXISTING); }
  void SetSize(const wxRect& rect) { SetSize(rect.x, rect.y, rect.width, rect.height); }
  void Move(int x, int y) { SetSize(x, y, -1, -1, wxSIZE_USE_EXISTING); }
  void Move(const wxPoint& pos) { Move(pos.x, pos.y); }
  void SetClientSize(int width, int height) { SetSize(width, height); }
  void SetClientSize(const wxSize& size) { SetSize(size); }
  void SetSizeHints(int, int, int = -1, int = -1, int = -1, int = -1) {}
  void GetSize(int* width, int* height) const;
  wxSize GetSize() const { return wxSize(m_width, m_height); }
  void GetClientSize(int* width, int* height) const { GetSize(width, height); }
  wxSize GetClientSize() const { return GetSize(); }
  void GetPosition(int* x, int* y) const;
  wxPoint GetPosition() const { return wxPoint(m_x, m_y); }
  wxRect GetRect() const { return wxRect(m_x, m_y, m_width, m_height); }
  void ClientToScreen(int* x, int* y) const;
  wxPoint ClientToScreen(const wxPoint& pt) const;
  void ScreenToClient(int* x, int* y) const;
  wxPoint ScreenToClient(const wxPoint& pt) const;

  void PushEventHandler(wxEvtHandler* handler);
  wxEvtHandler* PopEventHandler(bool deleteHandler = false);
  wxEvtHandler* GetEventHandler() const { return m_eventHandler != nullptr ? m_eventHandler : const_cast<wxWindow*>(this); }
  void SetEventHandler(wxEvtHandler* handler) { m_eventHandler = handler; }

  void SetBackgroundColour(const wxColour&) {}
  void SetForegroundColour(const wxColour&) {}
  void SetFont(const wxFont&) {}
  void SetCursor(const wxObject&) {}
  void SetSizer(wxSizer*, bool = true) {}
  void SetAutoLayout(bool) {}
  void SetAcceleratorTable(const wxAcceleratorTable&) {}
  void SetToolTip(const wxString&) {}
  virtual void SetIcon(const wxIcon&) {}

  /// The native handle: the SDL_Window of a top-level window (platform/sdl/SdlApp.cpp), else the
  /// nearest top-level ancestor's.
  virtual WXWidget GetHandle() const;

  // wxWindows/MSW's message hooks: the engine overrides them for its frame and viewport. On Linux
  // no Windows message ever arrives; platform/sdl turns SDL's events into wx events instead.
  virtual long MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam);
  virtual long MSWDefWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam);
  virtual bool TransferDataFromWindow() { return true; }
  virtual bool TransferDataToWindow() { return true; }
  virtual bool Validate() { return true; }
  WXHWND GetHWND() const { return GetHandle(); }

  void AddChild(wxWindow* child);
  void RemoveChild(wxWindow* child);

  static const wxEventTable sm_eventTable;
  const wxEventTable* GetEventTable() const override { return &sm_eventTable; }

  wxWindow* m_parent = nullptr;
  std::vector<wxWindow*> m_children;
  wxEvtHandler* m_eventHandler = nullptr;
  wxWindowID m_windowId = -1;
  wxString m_windowName;
  wxString m_title;
  long m_windowStyle = 0;
  int m_x = 0, m_y = 0, m_width = 0, m_height = 0;
  bool m_isShown = false;
  bool m_isEnabled = true;
  bool m_isBeingDeleted = false;
};
typedef std::vector<wxWindow*> wxWindowList;

class wxControl : public wxWindow
{
public:
  using wxWindow::wxWindow;
};
class wxPanel : public wxWindow
{
public:
  wxPanel() = default;
  wxPanel(wxWindow* parent, wxWindowID id = -1, const wxPoint& pos = wxDefaultPosition,
          const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxPanelNameStr)
    : wxWindow(parent, id, pos, size, style, name) {}
  bool Create(wxWindow* parent, wxWindowID id = -1, const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxPanelNameStr)
  {
    return wxWindow::Create(parent, id, pos, size, style, name);
  }
};

class wxTopLevelWindow : public wxWindow
{
public:
  wxTopLevelWindow() = default;
  ~wxTopLevelWindow() override;
  bool CreateTopLevel(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size,
                      long style, const wxString& name);
  bool IsTopLevel() const override { return true; }
  bool Show(bool show = true) override;
  void SetTitle(const wxString& title) override;
  void SetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
  using wxWindow::SetSize;
  void Maximize(bool maximize = true);
  void Iconize(bool iconize = true);
  bool IsMaximized() const;
  bool IsIconized() const;
  bool ShowFullScreen(bool show, long = 0);
  bool IsFullScreen() const { return m_fullScreen; }
  void Raise() override;
  WXWidget GetHandle() const override { return reinterpret_cast<WXWidget>(m_nativeWindow); }

  void* m_nativeWindow = nullptr; // SDL_Window*, created on the first Show
  bool m_fullScreen = false;
};
typedef wxTopLevelWindow wxTopLevelWindowMSW;

class wxFrame : public wxTopLevelWindow
{
public:
  wxFrame() = default;
  wxFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
          const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE, const wxString& name = wxFrameNameStr)
  {
    Create(parent, id, title, pos, size, style, name);
  }
  bool Create(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE,
              const wxString& name = wxFrameNameStr)
  {
    return CreateTopLevel(parent, id, title, pos, size, style, name);
  }
  void SetMenuBar(wxObject*) {}
  void SetStatusText(const wxString&, int = 0) {}
  static const wxEventTable sm_eventTable;
  const wxEventTable* GetEventTable() const override { return &sm_eventTable; }
};

class wxDialog : public wxTopLevelWindow
{
public:
  wxDialog() = default;
  wxDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
           const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE,
           const wxString& name = wxDialogNameStr)
  {
    Create(parent, id, title, pos, size, style, name);
  }
  bool Create(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE,
              const wxString& name = wxDialogNameStr)
  {
    // a dialog stays a plain window record: on Linux it never opens a native window
    m_parent = parent;
    m_windowId = id;
    m_title = title;
    m_windowStyle = style;
    m_x = pos.x;
    m_y = pos.y;
    m_width = size.x;
    m_height = size.y;
    (void)name;
    return true;
  }
  bool Show(bool show = true) override { m_isShown = show; return true; }
  /// The Linux client has no dialogs: every modal one answers as cancelled.
  virtual int ShowModal() { return wxID_CANCEL; }
  void EndModal(int returnCode) { m_returnCode = returnCode; }
  int GetReturnCode() const { return m_returnCode; }
  bool IsModal() const { return false; }
  static const wxEventTable sm_eventTable;
  const wxEventTable* GetEventTable() const override { return &sm_eventTable; }
  int m_returnCode = 0;
};

class wxButton : public wxControl
{
public:
  wxButton() = default;
  wxButton(wxWindow* parent, wxWindowID id, const wxString& label = wxString(), const wxPoint& pos = wxDefaultPosition,
           const wxSize& size = wxDefaultSize, long style = 0)
    : wxControl(parent, id, pos, size, style)
  {
    m_title = label;
  }
  void SetDefault() {}
};
class wxBitmapButton : public wxButton
{
public:
  wxBitmapButton() = default;
  wxBitmapButton(wxWindow* parent, wxWindowID id, const wxBitmap&, const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize, long style = wxBU_AUTODRAW)
    : wxButton(parent, id, wxString(), pos, size, style) {}
  bool Create(wxWindow* parent, wxWindowID id, const wxBitmap&, const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = wxBU_AUTODRAW)
  {
    return wxWindow::Create(parent, id, pos, size, style);
  }
  void SetBitmapLabel(const wxBitmap&) {}
  void SetBitmapSelected(const wxBitmap&) {}
  void SetBitmapFocus(const wxBitmap&) {}
  void SetBitmapDisabled(const wxBitmap&) {}
};
class wxCheckBox : public wxControl
{
public:
  wxCheckBox() = default;
  wxCheckBox(wxWindow* parent, wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize, long style = 0)
    : wxControl(parent, id, pos, size, style)
  {
    m_title = label;
  }
  bool GetValue() const { return m_value; }
  void SetValue(bool value) { m_value = value; }
  bool IsChecked() const { return m_value; }
  bool m_value = false;
};
class wxStaticText : public wxControl
{
public:
  wxStaticText() = default;
  wxStaticText(wxWindow* parent, wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize, long style = 0)
    : wxControl(parent, id, pos, size, style)
  {
    m_title = label;
  }
};
class wxTextCtrl : public wxControl
{
public:
  wxTextCtrl() = default;
  wxTextCtrl(wxWindow* parent, wxWindowID id, const wxString& value = wxString(), const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize, long style = 0)
    : wxControl(parent, id, pos, size, style), m_value(value) {}
  wxString GetValue() const { return m_value; }
  void SetValue(const wxString& value) { m_value = value; }
  void AppendText(const wxString& text) { m_value += text; }
  void Clear() { m_value.Clear(); }
  void SetInsertionPointEnd() {}
  void ShowPosition(long) {}
  long GetLastPosition() const { return static_cast<long>(m_value.Len()); }
  void SetSelection(long, long) {}
  wxString m_value;
};
class wxComboBox : public wxControl
{
public:
  wxString GetValue() const { return wxString(); }
  void SetValue(const wxString&) {}
  int GetSelection() const { return -1; }
  void SetSelection(int) {}
  void Append(const wxString&) {}
  void Clear() {}
};
class wxUpdateUIEvent : public wxCommandEvent
{
public:
  void Enable(bool) {}
  void Check(bool) {}
};
class wxSlider : public wxControl
{
public:
  int GetValue() const { return 0; }
  void SetValue(int) {}
};
class wxNotebook : public wxControl
{
};
class wxSplitterWindow : public wxWindow
{
};
class wxListCtrl : public wxControl
{
};
class wxTreeItemData : public wxObject
{
};
class wxTreeItemId
{
public:
  bool IsOk() const { return m_pItem != nullptr; }
  void* m_pItem = nullptr;
};
class wxTreeCtrl : public wxControl
{
};
class wxToolBar : public wxControl
{
};
class wxMenu : public wxEvtHandler
{
};
class wxMenuBar : public wxWindow
{
};
class wxSplashScreen : public wxFrame
{
public:
  wxSplashScreen(const wxBitmap&, long, int, wxWindow*, wxWindowID, const wxPoint& = wxDefaultPosition,
                 const wxSize& = wxDefaultSize, long = 0) {}
};

class wxSizer : public wxObject
{
public:
  void Add(wxWindow*, int = 0, int = 0, int = 0) {}
  void Add(wxSizer*, int = 0, int = 0, int = 0) {}
  void Add(int, int, int = 0, int = 0, int = 0) {}
  void Fit(wxWindow*) {}
  void SetSizeHints(wxWindow*) {}
  void Layout() {}
};
class wxBoxSizer : public wxSizer
{
public:
  explicit wxBoxSizer(int) {}
};
class wxLayoutConstraints : public wxObject
{
};

class wxDC : public wxObject
{
public:
  void SetBrush(const wxBrush&) {}
  void SetPen(const wxObject&) {}
  void DrawBitmap(const wxBitmap&, int, int, bool = false) {}
  void DrawRectangle(int, int, int, int) {}
  void Clear() {}
};
class wxPaintDC : public wxDC
{
public:
  explicit wxPaintDC(wxWindow*) {}
};
class wxClientDC : public wxDC
{
public:
  explicit wxClientDC(wxWindow*) {}
};

// --- dialogs, files, URLs -------------------------------------------------------------------
class wxDirDialog : public wxDialog
{
public:
  wxDirDialog(wxWindow* parent, const wxString& message = wxString(), const wxString& path = wxString(), long style = 0,
              const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize)
    : wxDialog(parent, -1, message, pos, size, style), m_path(path) {}
  wxString GetPath() const { return m_path; }
  void SetPath(const wxString& path) { m_path = path; }
  wxString m_path;
};
class wxFileDialog : public wxDialog
{
public:
  wxFileDialog(wxWindow* parent, const wxString& message = wxString(), const wxString& = wxString(),
               const wxString& = wxString(), const wxString& = wxString(), long style = 0,
               const wxPoint& pos = wxDefaultPosition)
    : wxDialog(parent, -1, message, pos, wxDefaultSize, style) {}
  wxString GetPath() const { return wxString(); }
};
int wxMessageBox(const wxString& message, const wxString& caption = wxString(), long style = wxOK,
                 wxWindow* parent = nullptr, int x = -1, int y = -1);

class wxInputStream : public wxObject
{
public:
  virtual bool Eof() const { return true; }
  virtual std::size_t LastRead() const { return 0; }
  wxInputStream& Read(void*, std::size_t) { return *this; }
};
class wxOutputStream : public wxObject
{
};
class wxFileInputStream : public wxInputStream
{
public:
  explicit wxFileInputStream(const wxString&) {}
  bool Ok() const { return false; }
};
class wxFileOutputStream : public wxOutputStream
{
public:
  explicit wxFileOutputStream(const wxString&) {}
  bool Ok() const { return false; }
};
class wxFile : public wxObject
{
};
class wxFFile : public wxObject
{
};
class wxFileName : public wxObject
{
};
enum wxURLError
{
  wxURL_NOERR = 0,
  wxURL_SNTXERR,
  wxURL_NOPROTO,
  wxURL_NOHOST,
  wxURL_NOPATH,
  wxURL_CONNERR,
  wxURL_PROTOERR
};
class wxURL : public wxObject
{
public:
  explicit wxURL(const wxString&) {}
  wxURLError GetError() const { return wxURL_CONNERR; }
  wxInputStream* GetInputStream() { return nullptr; }
};

// --- the application ------------------------------------------------------------------------
class wxAppBase : public wxEvtHandler
{
public:
  virtual bool OnInit() { return true; }
  virtual int OnExit() { return 0; }
  virtual int OnRun() { return MainLoop(); }
  virtual int MainLoop();
  virtual void ExitMainLoop() { m_keepGoing = false; }
  virtual bool Pending();
  virtual void Dispatch();
  virtual bool ProcessIdle();
  virtual bool Initialized() { return true; }
  virtual void OnAssert(const wxChar*, int, const wxChar*) {}
  wxWindow* GetTopWindow() const { return m_topWindow; }
  void SetTopWindow(wxWindow* window) { m_topWindow = window; }
  void SetExitOnFrameDelete(bool exit) { m_exitOnFrameDelete = exit; }
  void SetAppName(const wxString& name) { m_appName = name; }
  wxString GetAppName() const { return m_appName; }
  void SetVendorName(const wxString&) {}
  bool IsActive() const { return m_isActive; }

  wxWindow* m_topWindow = nullptr;
  wxString m_appName;
  int argc = 0;
  wxChar** argv = nullptr;
  bool m_keepGoing = true;
  bool m_exitOnFrameDelete = true;
  bool m_isActive = true;
};
class wxApp : public wxAppBase
{
public:
  static const wxEventTable sm_eventTable;
  const wxEventTable* GetEventTable() const override { return &sm_eventTable; }
  static bool Initialize() { return true; }
  static void CleanUp() {}
};
extern wxApp* wxTheApp;
typedef wxApp* (*wxAppInitializerFunction)();
class wxAppInitializer
{
public:
  explicit wxAppInitializer(wxAppInitializerFunction fn);
};
wxApp* wxCreateApp();
extern wxAppInitializerFunction wxAppCreator;

#define DECLARE_APP(appname) extern appname& wxGetApp();
#define IMPLEMENT_APP_NO_MAIN(appname)                                                                     \
  wxApp* wxCreateApp() { return new appname; }                                                             \
  wxAppInitializer wxTheAppInitializer((wxAppInitializerFunction)wxCreateApp);                              \
  appname& wxGetApp() { return *static_cast<appname*>(wxTheApp); }
#define IMPLEMENT_APP(appname) IMPLEMENT_APP_NO_MAIN(appname)

/// wx's entry: creates the application object, runs OnInit, its main loop and OnExit
/// (platform/sdl/SdlApp.cpp drives the loop with SDL's events).
int wxEntry(WXHINSTANCE instance, WXHINSTANCE previous, char* commandLine, int show, bool enterLoop = true);
int wxEntry(int argc, char** argv);
wxPoint wxGetMousePosition();
void wxGetMousePosition(int* x, int* y);
bool wxYield();
void wxWakeUpIdle();
/// wxWindows/MSW's virtual-key translation (wx/msw/window.cpp), with the same table: the engine maps
/// keys to the original's Windows virtual-key codes in its keymaps.
int wxCharCodeWXToMSW(int keyCode, bool* isVirtual);
int wxCharCodeMSWToWX(int virtualKey);
