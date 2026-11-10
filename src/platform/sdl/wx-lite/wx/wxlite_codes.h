#pragma once
// Engine addition (faf-neural-ai), generated from wxWindows 2.4.2's wx/defs.h and wx/event.h
// (dependencies/wxWindows-2.4.2) by the Linux client port: the same key codes and event type
// numbers, which the engine's key mapping and event tables rely on. Do not edit by hand.

enum wxKeyCode
{
    WXK_BACK    =    8,
    WXK_TAB     =    9,
    WXK_RETURN  =    13,
    WXK_ESCAPE  =    27,
    WXK_SPACE   =    32,
    WXK_DELETE  =    127,

    WXK_START   = 300,
    WXK_LBUTTON,
    WXK_RBUTTON,
    WXK_CANCEL,
    WXK_MBUTTON,
    WXK_CLEAR,
    WXK_SHIFT,
    WXK_ALT,
    WXK_CONTROL,
    WXK_MENU,
    WXK_PAUSE,
    WXK_CAPITAL,
    WXK_PRIOR,  // Page up
    WXK_NEXT,   // Page down
    WXK_END,
    WXK_HOME,
    WXK_LEFT,
    WXK_UP,
    WXK_RIGHT,
    WXK_DOWN,
    WXK_SELECT,
    WXK_PRINT,
    WXK_EXECUTE,
    WXK_SNAPSHOT,
    WXK_INSERT,
    WXK_HELP,
    WXK_NUMPAD0,
    WXK_NUMPAD1,
    WXK_NUMPAD2,
    WXK_NUMPAD3,
    WXK_NUMPAD4,
    WXK_NUMPAD5,
    WXK_NUMPAD6,
    WXK_NUMPAD7,
    WXK_NUMPAD8,
    WXK_NUMPAD9,
    WXK_MULTIPLY,
    WXK_ADD,
    WXK_SEPARATOR,
    WXK_SUBTRACT,
    WXK_DECIMAL,
    WXK_DIVIDE,
    WXK_F1,
    WXK_F2,
    WXK_F3,
    WXK_F4,
    WXK_F5,
    WXK_F6,
    WXK_F7,
    WXK_F8,
    WXK_F9,
    WXK_F10,
    WXK_F11,
    WXK_F12,
    WXK_F13,
    WXK_F14,
    WXK_F15,
    WXK_F16,
    WXK_F17,
    WXK_F18,
    WXK_F19,
    WXK_F20,
    WXK_F21,
    WXK_F22,
    WXK_F23,
    WXK_F24,
    WXK_NUMLOCK,
    WXK_SCROLL,
    WXK_PAGEUP,
    WXK_PAGEDOWN,

    WXK_NUMPAD_SPACE,
    WXK_NUMPAD_TAB,
    WXK_NUMPAD_ENTER,
    WXK_NUMPAD_F1,
    WXK_NUMPAD_F2,
    WXK_NUMPAD_F3,
    WXK_NUMPAD_F4,
    WXK_NUMPAD_HOME,
    WXK_NUMPAD_LEFT,
    WXK_NUMPAD_UP,
    WXK_NUMPAD_RIGHT,
    WXK_NUMPAD_DOWN,
    WXK_NUMPAD_PRIOR,
    WXK_NUMPAD_PAGEUP,
    WXK_NUMPAD_NEXT,
    WXK_NUMPAD_PAGEDOWN,
    WXK_NUMPAD_END,
    WXK_NUMPAD_BEGIN,
    WXK_NUMPAD_INSERT,
    WXK_NUMPAD_DELETE,
    WXK_NUMPAD_EQUAL,
    WXK_NUMPAD_MULTIPLY,
    WXK_NUMPAD_ADD,
    WXK_NUMPAD_SEPARATOR,
    WXK_NUMPAD_SUBTRACT,
    WXK_NUMPAD_DECIMAL,
    WXK_NUMPAD_DIVIDE
};

typedef int wxEventType;
const wxEventType wxEVT_NULL = 0;
const wxEventType wxEVT_FIRST = 10000;
const wxEventType wxEVT_USER_FIRST = wxEVT_FIRST + 2000;
const wxEventType wxEVT_COMMAND_BUTTON_CLICKED = wxEVT_FIRST + 1;
const wxEventType wxEVT_COMMAND_CHECKBOX_CLICKED = wxEVT_FIRST + 2;
const wxEventType wxEVT_COMMAND_CHOICE_SELECTED = wxEVT_FIRST + 3;
const wxEventType wxEVT_COMMAND_LISTBOX_SELECTED = wxEVT_FIRST + 4;
const wxEventType wxEVT_COMMAND_LISTBOX_DOUBLECLICKED = wxEVT_FIRST + 5;
const wxEventType wxEVT_COMMAND_CHECKLISTBOX_TOGGLED = wxEVT_FIRST + 6;
const wxEventType wxEVT_COMMAND_TEXT_UPDATED = wxEVT_FIRST + 7;
const wxEventType wxEVT_COMMAND_TEXT_ENTER = wxEVT_FIRST + 8;
const wxEventType wxEVT_COMMAND_TEXT_URL = wxEVT_FIRST + 13;
const wxEventType wxEVT_COMMAND_TEXT_MAXLEN = wxEVT_FIRST + 14;
const wxEventType wxEVT_COMMAND_MENU_SELECTED = wxEVT_FIRST + 9;
const wxEventType wxEVT_COMMAND_SLIDER_UPDATED = wxEVT_FIRST + 10;
const wxEventType wxEVT_COMMAND_RADIOBOX_SELECTED = wxEVT_FIRST + 11;
const wxEventType wxEVT_COMMAND_RADIOBUTTON_SELECTED = wxEVT_FIRST + 12;
const wxEventType wxEVT_COMMAND_SCROLLBAR_UPDATED = wxEVT_FIRST + 13;
const wxEventType wxEVT_COMMAND_VLBOX_SELECTED = wxEVT_FIRST + 14;
const wxEventType wxEVT_COMMAND_COMBOBOX_SELECTED = wxEVT_FIRST + 15;
const wxEventType wxEVT_COMMAND_TOOL_RCLICKED = wxEVT_FIRST + 16;
const wxEventType wxEVT_COMMAND_TOOL_ENTER = wxEVT_FIRST + 17;
const wxEventType wxEVT_COMMAND_SPINCTRL_UPDATED = wxEVT_FIRST + 18;
const wxEventType wxEVT_SOCKET = wxEVT_FIRST + 50;
const wxEventType wxEVT_LEFT_DOWN = wxEVT_FIRST + 100;
const wxEventType wxEVT_LEFT_UP = wxEVT_FIRST + 101;
const wxEventType wxEVT_MIDDLE_DOWN = wxEVT_FIRST + 102;
const wxEventType wxEVT_MIDDLE_UP = wxEVT_FIRST + 103;
const wxEventType wxEVT_RIGHT_DOWN = wxEVT_FIRST + 104;
const wxEventType wxEVT_RIGHT_UP = wxEVT_FIRST + 105;
const wxEventType wxEVT_MOTION = wxEVT_FIRST + 106;
const wxEventType wxEVT_ENTER_WINDOW = wxEVT_FIRST + 107;
const wxEventType wxEVT_LEAVE_WINDOW = wxEVT_FIRST + 108;
const wxEventType wxEVT_LEFT_DCLICK = wxEVT_FIRST + 109;
const wxEventType wxEVT_MIDDLE_DCLICK = wxEVT_FIRST + 110;
const wxEventType wxEVT_RIGHT_DCLICK = wxEVT_FIRST + 111;
const wxEventType wxEVT_SET_FOCUS = wxEVT_FIRST + 112;
const wxEventType wxEVT_KILL_FOCUS = wxEVT_FIRST + 113;
const wxEventType wxEVT_CHILD_FOCUS = wxEVT_FIRST + 114;
const wxEventType wxEVT_MOUSEWHEEL = wxEVT_FIRST + 115;
const wxEventType wxEVT_NC_LEFT_DOWN = wxEVT_FIRST + 200;
const wxEventType wxEVT_NC_LEFT_UP = wxEVT_FIRST + 201;
const wxEventType wxEVT_NC_MIDDLE_DOWN = wxEVT_FIRST + 202;
const wxEventType wxEVT_NC_MIDDLE_UP = wxEVT_FIRST + 203;
const wxEventType wxEVT_NC_RIGHT_DOWN = wxEVT_FIRST + 204;
const wxEventType wxEVT_NC_RIGHT_UP = wxEVT_FIRST + 205;
const wxEventType wxEVT_NC_MOTION = wxEVT_FIRST + 206;
const wxEventType wxEVT_NC_ENTER_WINDOW = wxEVT_FIRST + 207;
const wxEventType wxEVT_NC_LEAVE_WINDOW = wxEVT_FIRST + 208;
const wxEventType wxEVT_NC_LEFT_DCLICK = wxEVT_FIRST + 209;
const wxEventType wxEVT_NC_MIDDLE_DCLICK = wxEVT_FIRST + 210;
const wxEventType wxEVT_NC_RIGHT_DCLICK = wxEVT_FIRST + 211;
const wxEventType wxEVT_CHAR = wxEVT_FIRST + 212;
const wxEventType wxEVT_CHAR_HOOK = wxEVT_FIRST + 213;
const wxEventType wxEVT_NAVIGATION_KEY = wxEVT_FIRST + 214;
const wxEventType wxEVT_KEY_DOWN = wxEVT_FIRST + 215;
const wxEventType wxEVT_KEY_UP = wxEVT_FIRST + 216;
const wxEventType wxEVT_SET_CURSOR = wxEVT_FIRST + 230;
const wxEventType wxEVT_SCROLL_TOP = wxEVT_FIRST + 300;
const wxEventType wxEVT_SCROLL_BOTTOM = wxEVT_FIRST + 301;
const wxEventType wxEVT_SCROLL_LINEUP = wxEVT_FIRST + 302;
const wxEventType wxEVT_SCROLL_LINEDOWN = wxEVT_FIRST + 303;
const wxEventType wxEVT_SCROLL_PAGEUP = wxEVT_FIRST + 304;
const wxEventType wxEVT_SCROLL_PAGEDOWN = wxEVT_FIRST + 305;
const wxEventType wxEVT_SCROLL_THUMBTRACK = wxEVT_FIRST + 306;
const wxEventType wxEVT_SCROLL_THUMBRELEASE = wxEVT_FIRST + 307;
const wxEventType wxEVT_SCROLL_ENDSCROLL = wxEVT_FIRST + 308;
const wxEventType wxEVT_SCROLLWIN_TOP = wxEVT_FIRST + 320;
const wxEventType wxEVT_SCROLLWIN_BOTTOM = wxEVT_FIRST + 321;
const wxEventType wxEVT_SCROLLWIN_LINEUP = wxEVT_FIRST + 322;
const wxEventType wxEVT_SCROLLWIN_LINEDOWN = wxEVT_FIRST + 323;
const wxEventType wxEVT_SCROLLWIN_PAGEUP = wxEVT_FIRST + 324;
const wxEventType wxEVT_SCROLLWIN_PAGEDOWN = wxEVT_FIRST + 325;
const wxEventType wxEVT_SCROLLWIN_THUMBTRACK = wxEVT_FIRST + 326;
const wxEventType wxEVT_SCROLLWIN_THUMBRELEASE = wxEVT_FIRST + 327;
const wxEventType wxEVT_SIZE = wxEVT_FIRST + 400;
const wxEventType wxEVT_MOVE = wxEVT_FIRST + 401;
const wxEventType wxEVT_CLOSE_WINDOW = wxEVT_FIRST + 402;
const wxEventType wxEVT_END_SESSION = wxEVT_FIRST + 403;
const wxEventType wxEVT_QUERY_END_SESSION = wxEVT_FIRST + 404;
const wxEventType wxEVT_ACTIVATE_APP = wxEVT_FIRST + 405;
const wxEventType wxEVT_POWER = wxEVT_FIRST + 406;
const wxEventType wxEVT_ACTIVATE = wxEVT_FIRST + 409;
const wxEventType wxEVT_CREATE = wxEVT_FIRST + 410;
const wxEventType wxEVT_DESTROY = wxEVT_FIRST + 411;
const wxEventType wxEVT_SHOW = wxEVT_FIRST + 412;
const wxEventType wxEVT_ICONIZE = wxEVT_FIRST + 413;
const wxEventType wxEVT_MAXIMIZE = wxEVT_FIRST + 414;
const wxEventType wxEVT_MOUSE_CAPTURE_CHANGED = wxEVT_FIRST + 415;
const wxEventType wxEVT_PAINT = wxEVT_FIRST + 416;
const wxEventType wxEVT_ERASE_BACKGROUND = wxEVT_FIRST + 417;
const wxEventType wxEVT_NC_PAINT = wxEVT_FIRST + 418;
const wxEventType wxEVT_PAINT_ICON = wxEVT_FIRST + 419;
const wxEventType wxEVT_MENU_OPEN = wxEVT_FIRST + 420;
const wxEventType wxEVT_MENU_CLOSE = wxEVT_FIRST + 421;
const wxEventType wxEVT_MENU_HIGHLIGHT = wxEVT_FIRST + 422;
const wxEventType wxEVT_POPUP_MENU_INIT = wxEVT_FIRST + 423;
const wxEventType wxEVT_CONTEXT_MENU = wxEVT_FIRST + 424;
const wxEventType wxEVT_SYS_COLOUR_CHANGED = wxEVT_FIRST + 425;
const wxEventType wxEVT_DISPLAY_CHANGED = wxEVT_FIRST + 426;
const wxEventType wxEVT_SETTING_CHANGED = wxEVT_FIRST + 427;
const wxEventType wxEVT_QUERY_NEW_PALETTE = wxEVT_FIRST + 428;
const wxEventType wxEVT_PALETTE_CHANGED = wxEVT_FIRST + 429;
const wxEventType wxEVT_JOY_BUTTON_DOWN = wxEVT_FIRST + 430;
const wxEventType wxEVT_JOY_BUTTON_UP = wxEVT_FIRST + 431;
const wxEventType wxEVT_JOY_MOVE = wxEVT_FIRST + 432;
const wxEventType wxEVT_JOY_ZMOVE = wxEVT_FIRST + 433;
const wxEventType wxEVT_DROP_FILES = wxEVT_FIRST + 434;
const wxEventType wxEVT_DRAW_ITEM = wxEVT_FIRST + 435;
const wxEventType wxEVT_MEASURE_ITEM = wxEVT_FIRST + 436;
const wxEventType wxEVT_COMPARE_ITEM = wxEVT_FIRST + 437;
const wxEventType wxEVT_INIT_DIALOG = wxEVT_FIRST + 438;
const wxEventType wxEVT_IDLE = wxEVT_FIRST + 439;
const wxEventType wxEVT_UPDATE_UI = wxEVT_FIRST + 440;
const wxEventType wxEVT_COMMAND_LEFT_CLICK = wxEVT_FIRST + 500;
const wxEventType wxEVT_COMMAND_LEFT_DCLICK = wxEVT_FIRST + 501;
const wxEventType wxEVT_COMMAND_RIGHT_CLICK = wxEVT_FIRST + 502;
const wxEventType wxEVT_COMMAND_RIGHT_DCLICK = wxEVT_FIRST + 503;
const wxEventType wxEVT_COMMAND_SET_FOCUS = wxEVT_FIRST + 504;
const wxEventType wxEVT_COMMAND_KILL_FOCUS = wxEVT_FIRST + 505;
const wxEventType wxEVT_COMMAND_ENTER = wxEVT_FIRST + 506;
const wxEventType wxEVT_HELP = wxEVT_FIRST + 1050;
const wxEventType wxEVT_DETAILED_HELP = wxEVT_FIRST + 1051;
