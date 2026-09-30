#include "moho/agent/AgentBridge.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gpg/core/utils/Logging.h"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"
#include "lua/LuaObject.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/Sim.h"
#include "moho/sim/SimDriver.h"
#include "platform/X87Precision.h"

// Engine addition, not recovered from the binary. See AgentBridge.h for the protocol.

namespace LuaPlus
{
  // lua/LuaObject.cpp: the coroutine CLuaTask::Execute is currently resuming (cross-thread hint).
  lua_State* GetRunningTaskThreadShared() noexcept;
} // namespace LuaPlus

namespace moho
{
  namespace
  {
    constexpr int kProtocolVersion = 1;
    constexpr int kDefaultAgentSpeed = 50;
    constexpr int kDefaultConnectTimeoutSeconds = 60;
    constexpr int kMaxJsonDepth = 48;
    constexpr int kMaxWaitTicks = 1 << 20;

    // ---------------------------------------------------------------------------------------------
    // Configuration (command line). Parsed once; read from the sim, issue and main threads.

    struct AgentBridgeConfig
    {
      bool enabled = false;
      unsigned short port = 0;
      std::string scriptPath;
      int speed = kDefaultAgentSpeed;
      int connectTimeoutSeconds = kDefaultConnectTimeoutSeconds;
      bool keepVisualEffects = false;
      int watchdogSeconds = 60;
    };

    int ParseIntArgument(const msvc8::string& text, const int fallback)
    {
      const char* const begin = text.c_str();
      const char* const end = begin + std::strlen(begin);
      int value = fallback;
      const auto [ptr, ec] = std::from_chars(begin, end, value);
      return (ec == std::errc{} && ptr != begin) ? value : fallback;
    }

    AgentBridgeConfig ParseConfig()
    {
      AgentBridgeConfig config{};
      msvc8::vector<msvc8::string> args;
      if (!CFG_GetArgOption("/agentbridge", 1, &args) || args.empty()) {
        return config;
      }

      const int port = ParseIntArgument(args[0], 0);
      if (port <= 0 || port > 65535) {
        gpg::Warnf("AgentBridge: invalid port \"%s\"; the bridge is disabled.", args[0].c_str());
        return config;
      }
      config.port = static_cast<unsigned short>(port);

      if (CFG_GetArgOption("/agentscript", 1, &args) && !args.empty()) {
        config.scriptPath = args[0].c_str();
      }
      if (config.scriptPath.empty()) {
        gpg::Warnf("AgentBridge: /agentbridge needs /agentscript <file>; the bridge is disabled.");
        return config;
      }

      if (CFG_GetArgOption("/agentspeed", 1, &args) && !args.empty()) {
        config.speed = std::clamp(ParseIntArgument(args[0], kDefaultAgentSpeed), -10, 50);
      }
      if (CFG_GetArgOption("/agenttimeout", 1, &args) && !args.empty()) {
        config.connectTimeoutSeconds = std::max(1, ParseIntArgument(args[0], kDefaultConnectTimeoutSeconds));
      }
      config.keepVisualEffects = CFG_GetArgOption("/agentvfx", 0, nullptr);
      if (CFG_GetArgOption("/agentwatchdog", 1, &args) && !args.empty()) {
        config.watchdogSeconds = std::max(0, ParseIntArgument(args[0], config.watchdogSeconds));
      }

      config.enabled = true;
      gpg::Logf(
        "AgentBridge: enabled (port %u, script \"%s\", speed %+d).",
        static_cast<unsigned>(config.port),
        config.scriptPath.c_str(),
        config.speed
      );
      return config;
    }

    const AgentBridgeConfig& Config()
    {
      static const AgentBridgeConfig config = ParseConfig();
      return config;
    }

    [[noreturn]] void TerminateGame(const unsigned int exitCode, const char* const reason)
    {
      gpg::Logf("AgentBridge: %s; exiting (code %u).", reason, exitCode);
      // TerminateProcess rather than ExitProcess: this runs on the sim thread, and an orderly
      // shutdown from here could wait on locks other threads hold.
      ::TerminateProcess(::GetCurrentProcess(), exitCode);
      for (;;) {
        ::Sleep(1000);
      }
    }

    double MillisecondsSince(const LARGE_INTEGER start)
    {
      LARGE_INTEGER now{};
      LARGE_INTEGER frequency{};
      ::QueryPerformanceCounter(&now);
      ::QueryPerformanceFrequency(&frequency);
      return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    }

    LARGE_INTEGER Now()
    {
      LARGE_INTEGER now{};
      ::QueryPerformanceCounter(&now);
      return now;
    }

    // ---------------------------------------------------------------------------------------------
    // Connection: one blocking TCP stream to the agent, newline-delimited messages.

    class AgentConnection
    {
    public:
      ~AgentConnection() { Close(); }

      [[nodiscard]] bool IsOpen() const noexcept { return mSocket != INVALID_SOCKET; }

      bool Connect(const unsigned short port, const int timeoutSeconds)
      {
        if (!mWinsockStarted) {
          WSADATA wsaData{};
          if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
          }
          mWinsockStarted = true;
        }

        const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(timeoutSeconds) * 1000ull;
        for (;;) {
          const SOCKET candidate = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
          if (candidate == INVALID_SOCKET) {
            return false;
          }

          sockaddr_in address{};
          address.sin_family = AF_INET;
          address.sin_port = ::htons(port);
          address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
          if (::connect(candidate, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            BOOL noDelay = TRUE;
            (void)::setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
            mSocket = candidate;
            return true;
          }

          ::closesocket(candidate);
          if (::GetTickCount64() >= deadline) {
            return false;
          }
          ::Sleep(250);
        }
      }

      bool SendLine(const std::string& line)
      {
        mOutgoing.assign(line);
        mOutgoing.push_back('\n');
        std::size_t sent = 0;
        while (sent < mOutgoing.size()) {
          const int chunk = static_cast<int>(std::min<std::size_t>(mOutgoing.size() - sent, 1u << 20));
          const int written = ::send(mSocket, mOutgoing.data() + sent, chunk, 0);
          if (written <= 0) {
            return false;
          }
          sent += static_cast<std::size_t>(written);
        }
        return true;
      }

      bool ReceiveLine(std::string& line)
      {
        mReceiveBuffer.resize(1u << 16);
        for (;;) {
          const std::size_t newline = mIncoming.find('\n', mScanFrom);
          if (newline != std::string::npos) {
            std::size_t length = newline;
            if (length > 0 && mIncoming[length - 1] == '\r') {
              --length;
            }
            line.assign(mIncoming, 0, length);
            mIncoming.erase(0, newline + 1);
            mScanFrom = 0;
            return true;
          }

          mScanFrom = mIncoming.size();
          const int received = ::recv(mSocket, mReceiveBuffer.data(), static_cast<int>(mReceiveBuffer.size()), 0);
          if (received <= 0) {
            return false;
          }
          mIncoming.append(mReceiveBuffer.data(), static_cast<std::size_t>(received));
        }
      }

      void Close()
      {
        if (mSocket != INVALID_SOCKET) {
          ::closesocket(mSocket);
          mSocket = INVALID_SOCKET;
        }
      }

    private:
      SOCKET mSocket = INVALID_SOCKET;
      bool mWinsockStarted = false;
      std::string mIncoming;
      std::size_t mScanFrom = 0;
      std::string mOutgoing;
      std::vector<char> mReceiveBuffer;
    };

    // ---------------------------------------------------------------------------------------------
    // Lua -> JSON. Tables whose keys are exactly 1..n become arrays (an empty table is `[]`); other
    // tables become objects with string keys. Functions, userdata and threads become null.

    void AppendJsonString(std::string& out, const char* const text, const std::size_t length)
    {
      out.push_back('"');
      for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
          case '"':
            out += "\\\"";
            break;
          case '\\':
            out += "\\\\";
            break;
          case '\n':
            out += "\\n";
            break;
          case '\r':
            out += "\\r";
            break;
          case '\t':
            out += "\\t";
            break;
          default:
            if (c < 0x20) {
              char escaped[8];
              std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
              out += escaped;
            } else {
              out.push_back(static_cast<char>(c));
            }
            break;
        }
      }
      out.push_back('"');
    }

    void AppendJsonString(std::string& out, const std::string_view text)
    {
      AppendJsonString(out, text.data(), text.size());
    }

    // std::to_chars is locale-independent: the game may run under a locale with a decimal comma.
    void AppendJsonNumber(std::string& out, const double value)
    {
      if (!std::isfinite(value)) {
        out += "null";
        return;
      }

      char buffer[64];
      std::to_chars_result result{};
      if (std::floor(value) == value && std::fabs(value) < 9.0e15) {
        result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
      } else {
        // Sim numbers are single precision; the shortest float form round-trips exactly.
        result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value));
      }
      out.append(buffer, result.ptr);
    }

    bool IsFunctionType(const int type) noexcept
    {
      return type == LUA_TFUNCTION || type == LUA_CFUNCTION;
    }

    int AbsoluteIndex(lua_State* const L, const int index)
    {
      return (index < 0 && index > LUA_REGISTRYINDEX) ? lua_gettop(L) + index + 1 : index;
    }

    bool TableIsArray(lua_State* const L, const int tableIndex, int& count)
    {
      count = 0;
      double maxKey = 0.0;
      bool array = true;
      lua_pushnil(L);
      while (lua_next(L, tableIndex) != 0) {
        ++count;
        if (array) {
          if (lua_type(L, -2) == LUA_TNUMBER) {
            const double key = static_cast<double>(lua_tonumber(L, -2));
            if (key >= 1.0 && std::floor(key) == key) {
              maxKey = std::max(maxKey, key);
            } else {
              array = false;
            }
          } else {
            array = false;
          }
        }
        lua_settop(L, -2); // pop the value, keep the key for lua_next
      }
      return array && maxKey == static_cast<double>(count);
    }

    void EncodeLuaValue(lua_State* const L, int index, std::string& out, const int depth)
    {
      index = AbsoluteIndex(L, index);
      switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
          out += lua_toboolean(L, index) != 0 ? "true" : "false";
          return;
        case LUA_TNUMBER:
          AppendJsonNumber(out, static_cast<double>(lua_tonumber(L, index)));
          return;
        case LUA_TSTRING:
          AppendJsonString(out, lua_tostring(L, index), lua_strlen(L, index));
          return;
        case LUA_TTABLE:
          break;
        default:
          out += "null";
          return;
      }

      if (depth >= kMaxJsonDepth || lua_checkstack(L, 4) == 0) {
        out += "null";
        return;
      }

      int count = 0;
      if (TableIsArray(L, index, count)) {
        out.push_back('[');
        for (int i = 1; i <= count; ++i) {
          if (i > 1) {
            out.push_back(',');
          }
          lua_rawgeti(L, index, i);
          EncodeLuaValue(L, -1, out, depth + 1);
          lua_settop(L, -2);
        }
        out.push_back(']');
        return;
      }

      out.push_back('{');
      bool first = true;
      lua_pushnil(L);
      while (lua_next(L, index) != 0) {
        const int keyType = lua_type(L, -2);
        if (keyType == LUA_TSTRING || keyType == LUA_TNUMBER) {
          if (!first) {
            out.push_back(',');
          }
          first = false;
          if (keyType == LUA_TSTRING) {
            AppendJsonString(out, lua_tostring(L, -2), lua_strlen(L, -2));
          } else {
            // Not lua_tostring: converting the key in place would break lua_next.
            std::string key;
            AppendJsonNumber(key, static_cast<double>(lua_tonumber(L, -2)));
            AppendJsonString(out, key);
          }
          out.push_back(':');
          EncodeLuaValue(L, -1, out, depth + 1);
        }
        lua_settop(L, -2);
      }
      out.push_back('}');
    }

    // ---------------------------------------------------------------------------------------------
    // JSON -> Lua. Objects and arrays become tables (arrays 1-based); null becomes nil, which drops
    // the member (or leaves a hole in an array).

    class JsonToLua
    {
    public:
      JsonToLua(lua_State* const L, const std::string_view text) : mState(L), mCursor(text.data()), mEnd(text.data() + text.size())
      {}

      // Pushes one value and returns true, or returns false with the stack in an unspecified
      // state above its original top (the caller restores it).
      bool Parse()
      {
        if (!ParseValue(0)) {
          return false;
        }
        SkipWhitespace();
        return mCursor == mEnd || Fail("trailing characters after the JSON value");
      }

      [[nodiscard]] const std::string& Error() const noexcept { return mError; }

    private:
      bool Fail(const char* const what)
      {
        if (mError.empty()) {
          mError = what;
        }
        return false;
      }

      void SkipWhitespace()
      {
        while (mCursor < mEnd && (*mCursor == ' ' || *mCursor == '\t' || *mCursor == '\n' || *mCursor == '\r')) {
          ++mCursor;
        }
      }

      [[nodiscard]] char Peek() const noexcept { return mCursor < mEnd ? *mCursor : '\0'; }

      bool ParseValue(const int depth)
      {
        if (depth > kMaxJsonDepth || lua_checkstack(mState, 4) == 0) {
          return Fail("JSON nested too deeply");
        }

        SkipWhitespace();
        switch (Peek()) {
          case '{':
            return ParseObject(depth);
          case '[':
            return ParseArray(depth);
          case '"': {
            std::string text;
            if (!ParseString(text)) {
              return false;
            }
            lua_pushlstring(mState, text.data(), text.size());
            return true;
          }
          case 't':
            return ParseLiteral("true", [this] { lua_pushboolean(mState, 1); });
          case 'f':
            return ParseLiteral("false", [this] { lua_pushboolean(mState, 0); });
          case 'n':
            return ParseLiteral("null", [this] { lua_pushnil(mState); });
          default:
            return ParseNumber();
        }
      }

      template <class PushFn>
      bool ParseLiteral(const char* const literal, PushFn push)
      {
        const std::size_t length = std::strlen(literal);
        if (static_cast<std::size_t>(mEnd - mCursor) < length || std::memcmp(mCursor, literal, length) != 0) {
          return Fail("invalid JSON literal");
        }
        mCursor += length;
        push();
        return true;
      }

      bool ParseNumber()
      {
        const char* const begin = mCursor;
        while (mCursor < mEnd && (std::strchr("+-0123456789.eE", *mCursor) != nullptr)) {
          ++mCursor;
        }
        double value = 0.0;
        const auto [ptr, ec] = std::from_chars(begin, mCursor, value);
        if (ec != std::errc{} || ptr != mCursor || ptr == begin) {
          return Fail("invalid JSON number");
        }
        lua_pushnumber(mState, static_cast<lua_Number>(value));
        return true;
      }

      static void AppendUtf8(std::string& out, const std::uint32_t codePoint)
      {
        if (codePoint < 0x80) {
          out.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
          out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
          out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
          out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
          out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
      }

      bool ParseHex4(std::uint32_t& value)
      {
        if (mEnd - mCursor < 4) {
          return Fail("truncated \\u escape");
        }
        const auto [ptr, ec] = std::from_chars(mCursor, mCursor + 4, value, 16);
        if (ec != std::errc{} || ptr != mCursor + 4) {
          return Fail("invalid \\u escape");
        }
        mCursor += 4;
        return true;
      }

      bool ParseString(std::string& out)
      {
        ++mCursor; // opening quote
        for (;;) {
          if (mCursor >= mEnd) {
            return Fail("unterminated JSON string");
          }
          const char c = *mCursor++;
          if (c == '"') {
            return true;
          }
          if (c != '\\') {
            out.push_back(c);
            continue;
          }
          if (mCursor >= mEnd) {
            return Fail("unterminated JSON escape");
          }
          const char escape = *mCursor++;
          switch (escape) {
            case '"':
            case '\\':
            case '/':
              out.push_back(escape);
              break;
            case 'b':
              out.push_back('\b');
              break;
            case 'f':
              out.push_back('\f');
              break;
            case 'n':
              out.push_back('\n');
              break;
            case 'r':
              out.push_back('\r');
              break;
            case 't':
              out.push_back('\t');
              break;
            case 'u': {
              std::uint32_t codePoint = 0;
              if (!ParseHex4(codePoint)) {
                return false;
              }
              if (codePoint >= 0xD800 && codePoint <= 0xDBFF && mEnd - mCursor >= 6 && mCursor[0] == '\\' &&
                  mCursor[1] == 'u') {
                mCursor += 2;
                std::uint32_t low = 0;
                if (!ParseHex4(low)) {
                  return false;
                }
                if (low >= 0xDC00 && low <= 0xDFFF) {
                  codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                }
              }
              AppendUtf8(out, codePoint);
              break;
            }
            default:
              return Fail("invalid JSON escape");
          }
        }
      }

      bool ParseObject(const int depth)
      {
        ++mCursor; // '{'
        lua_newtable(mState);
        SkipWhitespace();
        if (Peek() == '}') {
          ++mCursor;
          return true;
        }
        for (;;) {
          SkipWhitespace();
          if (Peek() != '"') {
            return Fail("expected a JSON object key");
          }
          std::string key;
          if (!ParseString(key)) {
            return false;
          }
          SkipWhitespace();
          if (Peek() != ':') {
            return Fail("expected ':' in JSON object");
          }
          ++mCursor;

          lua_pushlstring(mState, key.data(), key.size());
          if (!ParseValue(depth + 1)) {
            return false;
          }
          if (lua_type(mState, -1) == LUA_TNIL) {
            lua_settop(mState, -3);
          } else {
            lua_rawset(mState, -3);
          }

          SkipWhitespace();
          const char next = Peek();
          ++mCursor;
          if (next == ',') {
            continue;
          }
          if (next == '}') {
            return true;
          }
          return Fail("expected ',' or '}' in JSON object");
        }
      }

      bool ParseArray(const int depth)
      {
        ++mCursor; // '['
        lua_newtable(mState);
        SkipWhitespace();
        if (Peek() == ']') {
          ++mCursor;
          return true;
        }
        for (int index = 1;; ++index) {
          if (!ParseValue(depth + 1)) {
            return false;
          }
          if (lua_type(mState, -1) == LUA_TNIL) {
            lua_settop(mState, -2);
          } else {
            lua_rawseti(mState, -2, index);
          }

          SkipWhitespace();
          const char next = Peek();
          ++mCursor;
          if (next == ',') {
            continue;
          }
          if (next == ']') {
            return true;
          }
          return Fail("expected ',' or ']' in JSON array");
        }
      }

      lua_State* mState;
      const char* mCursor;
      const char* mEnd;
      std::string mError;
    };

    // ---------------------------------------------------------------------------------------------
    // Script calls. Everything goes through the protected lua_call, and every C++ exception is
    // caught as well, so a script or protocol error is reported to the agent, never raised into
    // the sim.

    std::string LuaErrorText(lua_State* const L)
    {
      const char* const text = (lua_type(L, -1) == LUA_TSTRING) ? lua_tostring(L, -1) : nullptr;
      return text != nullptr ? std::string(text) : std::string("(error object is not a string)");
    }

    // Leaves one result on the stack and returns true, or leaves nothing (the arguments are consumed
    // either way) and appends to `error`. Globals are read raw: FA's _G raises on undefined names.
    bool CallScriptFunction(lua_State* const L, const char* const name, const int argCount, std::string& error)
    {
      const int base = lua_gettop(L) - argCount;
      try {
        lua_pushstring(L, name);
        lua_rawget(L, LUA_GLOBALSINDEX);
        if (!IsFunctionType(lua_type(L, -1))) {
          lua_settop(L, base);
          error = std::string("the agent script does not define ") + name + "()";
          return false;
        }
        lua_insert(L, base + 1);
        if (LuaCallProtected(L, argCount, 1) != 0) { // upstream's lua_call is the unprotected call
          error = std::string(name) + ": " + LuaErrorText(L);
          lua_settop(L, base);
          return false;
        }
        return true;
      } catch (const std::exception& exception) {
        error = std::string(name) + ": " + exception.what();
      } catch (...) {
        error = std::string(name) + ": unknown exception";
      }
      lua_settop(L, base);
      return false;
    }

    // AgentBridge_LoadChunk(code [, name]) -> function | nil, message. The sim sandbox has no
    // loadstring; the script's opt-in `eval` query compiles through this.
    int AgentLoadChunk(lua_State* const L)
    {
      if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L, "AgentBridge_LoadChunk: expected a string");
        return 2;
      }
      const char* const code = lua_tostring(L, 1);
      const std::size_t length = lua_strlen(L, 1);
      const char* const name = (lua_type(L, 2) == LUA_TSTRING) ? lua_tostring(L, 2) : "=agent";
      if (luaL_loadbuffer(L, code, length, name) != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
      }
      return 1;
    }

    void RegisterGlobalFunction(lua_State* const L, const char* const name, lua_CFunction function)
    {
      lua_pushstring(L, name);
      lua_pushcclosure(L, function, 0);
      lua_rawset(L, LUA_GLOBALSINDEX);
    }

    int AgentLap(lua_State* L);

    bool LoadScript(lua_State* const L, const std::string& path, std::string& error)
    {
      RegisterGlobalFunction(L, "AgentBridge_LoadChunk", &AgentLoadChunk);
      RegisterGlobalFunction(L, "AgentBridge_Lap", &AgentLap);

      std::ifstream file(path, std::ios::binary);
      if (!file) {
        error = "cannot open the agent script \"" + path + "\"";
        return false;
      }
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      if (source.size() >= 3 && std::memcmp(source.data(), "\xEF\xBB\xBF", 3) == 0) {
        source.erase(0, 3);
      }

      const std::string chunkName = "@" + path;
      const int top = lua_gettop(L);
      try {
        int status = luaL_loadbuffer(L, source.data(), source.size(), chunkName.c_str());
        if (status == 0) {
          status = LuaCallProtected(L, 0, 0);
        }
        if (status != 0) {
          error = "loading the agent script: " + LuaErrorText(L);
          lua_settop(L, top);
          return false;
        }
      } catch (const std::exception& exception) {
        error = std::string("loading the agent script: ") + exception.what();
        lua_settop(L, top);
        return false;
      } catch (...) {
        error = "loading the agent script: unknown exception";
        lua_settop(L, top);
        return false;
      }
      lua_settop(L, top);
      return true;
    }

    // Reads a field of the table at `tableIndex` without metamethods; pushes it.
    void PushRawField(lua_State* const L, const int tableIndex, const char* const key)
    {
      lua_pushstring(L, key);
      lua_rawget(L, tableIndex);
    }

    // ---------------------------------------------------------------------------------------------
    // Load-phase marks (AGENT_LoadMark): milliseconds since the process was created, from any thread.

    struct LoadMarks
    {
      std::mutex lock;
      std::vector<std::pair<const char*, double>> marks;
    };

    LoadMarks& Marks()
    {
      static LoadMarks marks;
      return marks;
    }

    double MillisecondsSinceProcessStart()
    {
      FILETIME created{}, exited{}, kernel{}, user{};
      FILETIME now{};
      ::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user);
      ::GetSystemTimePreciseAsFileTime(&now);
      const auto value = [](const FILETIME& t) {
        return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
      };
      return static_cast<double>(value(now) - value(created)) / 10000.0;
    }

    // ,"load":[["phase",ms],...] -- and forgets them (a later session in this process starts afresh)
    void AppendLoadMarks(std::string& message)
    {
      LoadMarks& marks = Marks();
      std::lock_guard<std::mutex> guard(marks.lock);
      message += ",\"load\":[";
      for (std::size_t i = 0; i < marks.marks.size(); ++i) {
        if (i > 0) {
          message.push_back(',');
        }
        message.push_back('[');
        AppendJsonString(message, marks.marks[i].first, std::strlen(marks.marks[i].first));
        message.push_back(',');
        AppendJsonNumber(message, marks.marks[i].second);
        message.push_back(']');
      }
      message.push_back(']');
      marks.marks.clear();
    }

    // ---------------------------------------------------------------------------------------------
    // Per-process runtime state. Only the sim thread touches it.

    struct AgentBridgeRuntime
    {
      AgentConnection connection;
      lua_State* scriptState = nullptr; // sim Lua state the script was loaded into
      bool helloSent = false;
      std::uint32_t lastTick = UINT32_MAX;
      std::uint32_t nextTick = 0;
      bool gameOverReported = false;
      std::string lastResultJson = "null";
      std::vector<std::string> pendingErrors;
      LARGE_INTEGER lastReplyTime{};
      double lastActMs = 0.0;

      // Time spent inside Sim::AdvanceBeat since the last step, for the timing report.
      LARGE_INTEGER beatStart{};
      double beatMs = 0.0;
      int beats = 0;

      // Sim-thread CPU time (user + kernel, 100 ns units) at the last reply: the CPU the ticks since
      // then took, as opposed to their wall time (which includes waiting for a CPU).
      std::uint64_t cpuAtLastReply = 0;

      // Named sections the script times with AgentBridge_Lap(name) during one step.
      LARGE_INTEGER lapMark{};
      std::vector<std::pair<std::string, double>> laps;
    };

    AgentBridgeRuntime& Runtime()
    {
      static AgentBridgeRuntime runtime;
      return runtime;
    }

    std::uint64_t ThreadCpu100ns()
    {
      FILETIME created{}, exited{}, kernel{}, user{};
      if (::GetThreadTimes(::GetCurrentThread(), &created, &exited, &kernel, &user) == 0) {
        return 0;
      }
      const auto value = [](const FILETIME& t) {
        return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
      };
      return value(kernel) + value(user);
    }

    // AgentBridge_Lap([name]): adds the time since the previous lap (or the start of the step) to the
    // section `name`, reported in the step's timing as "laps"; without a name it only restarts the
    // clock. For profiling the script; costs a clock read.
    int AgentLap(lua_State* const L)
    {
      AgentBridgeRuntime& runtime = Runtime();
      const LARGE_INTEGER now = Now();
      if (lua_type(L, 1) == LUA_TSTRING && runtime.lapMark.QuadPart != 0) {
        LARGE_INTEGER frequency{};
        ::QueryPerformanceFrequency(&frequency);
        const double ms =
          static_cast<double>(now.QuadPart - runtime.lapMark.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
        const char* const name = lua_tostring(L, 1);
        auto it = std::find_if(runtime.laps.begin(), runtime.laps.end(), [name](const auto& lap) {
          return lap.first == name;
        });
        if (it == runtime.laps.end()) {
          runtime.laps.emplace_back(name, ms);
        } else {
          it->second += ms;
        }
      }
      runtime.lapMark = now;
      return 0;
    }

    // ---------------------------------------------------------------------------------------------
    // Watchdog. A sim that stops making progress (a script stuck in an endless loop) would otherwise
    // leave the agent waiting forever. When the sim thread has been busy for /agentwatchdog seconds,
    // a Lua count hook is planted on the main sim state and on the coroutine being resumed; it runs
    // on the sim thread at the next Lua instruction, reports the Lua traceback to the log and to the
    // agent, and ends the process.

    std::atomic<std::int64_t> gBusySince{0};        // QueryPerformanceCounter value, 0 while idle
    std::atomic<lua_State*> gMainLuaState{nullptr};
    std::atomic<bool> gWatchdogFired{false};

    void MarkBusy(const bool busy) noexcept
    {
      gBusySince.store(busy ? Now().QuadPart : 0, std::memory_order_relaxed);
    }

    void WatchdogHook(lua_State* const L, lua_Debug* const)
    {
      lua_sethook(L, nullptr, 0, 0);
      std::string trace = "the sim made no progress for too long; Lua traceback:";
      lua_Debug ar{};
      for (int level = 0; level < 32 && lua_getstack(L, level, &ar) != 0; ++level) {
        if (lua_getinfo(L, "Sln", &ar) == 0) {
          break;
        }
        char line[600];
        std::snprintf(line, sizeof(line), "\n  %s:%d %s", ar.short_src, ar.currentline, ar.name != nullptr ? ar.name : "?");
        trace += line;
      }
      gpg::Warnf("AgentBridge watchdog: %s", trace.c_str());

      std::string message = "{\"type\":\"error\",\"errors\":[";
      AppendJsonString(message, trace);
      message += "]}";
      (void)Runtime().connection.SendLine(message);
      TerminateGame(5, "watchdog: the sim was stuck in Lua");
    }

    void WatchdogLoop(const int timeoutSeconds)
    {
      LARGE_INTEGER frequency{};
      ::QueryPerformanceFrequency(&frequency);
      for (;;) {
        ::Sleep(1000);
        const std::int64_t since = gBusySince.load(std::memory_order_relaxed);
        if (since == 0 || gWatchdogFired.load()) {
          continue;
        }
        const double busySeconds = static_cast<double>(Now().QuadPart - since) / static_cast<double>(frequency.QuadPart);
        if (busySeconds < timeoutSeconds) {
          continue;
        }

        gWatchdogFired = true;
        gpg::Warnf("AgentBridge watchdog: the sim thread has been busy for %.0f s; requesting a Lua traceback", busySeconds);
        for (lua_State* const L : {gMainLuaState.load(), LuaPlus::GetRunningTaskThreadShared()}) {
          if (L != nullptr) {
            lua_sethook(L, &WatchdogHook, LUA_MASKCOUNT, 1);
          }
        }
        // The hook never fired: stuck outside Lua (or in a coroutine we did not hook).
        ::Sleep(15000);
        TerminateGame(6, "watchdog: the sim was stuck and no Lua traceback could be taken");
      }
    }

    void StartWatchdogOnce(const int timeoutSeconds)
    {
      static std::atomic<bool> started{false};
      if (timeoutSeconds > 0 && !started.exchange(true)) {
        std::thread(WatchdogLoop, timeoutSeconds).detach();
      }
    }

    void AppendErrors(std::string& message, std::vector<std::string>& errors)
    {
      if (errors.empty()) {
        return;
      }
      message += ",\"errors\":[";
      for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) {
          message.push_back(',');
        }
        AppendJsonString(message, errors[i]);
      }
      message.push_back(']');
      for (const std::string& error : errors) {
        gpg::Warnf("AgentBridge: %s", error.c_str());
      }
      errors.clear();
    }

    void RunStep(Sim* const sim, lua_State* const L, AgentBridgeRuntime& runtime, const bool gameOver)
    {
      const std::uint32_t tick = sim->mCurTick;
      const LARGE_INTEGER stepStart = Now();
      MarkBusy(true);
      const double simMs = runtime.lastReplyTime.QuadPart != 0 ? MillisecondsSince(runtime.lastReplyTime) : 0.0;
      const std::uint64_t cpuAtStep = ThreadCpu100ns();
      const double simCpuMs =
        runtime.cpuAtLastReply != 0 ? static_cast<double>(cpuAtStep - runtime.cpuAtLastReply) / 10000.0 : 0.0;
      runtime.laps.clear();
      runtime.lapMark = stepStart;
      double observeLuaMs = 0.0;

      // ---- build and send the message
      std::string message;
      message.reserve(1u << 16);
      std::string error;
      if (!runtime.helloSent) {
        message += "{\"type\":\"hello\",\"protocol\":";
        AppendJsonNumber(message, kProtocolVersion);
        message += ",\"tick\":";
        AppendJsonNumber(message, static_cast<double>(tick));
        message += ",\"info\":";
        lua_pushstring(L, "AgentBridge_Hello");
        lua_rawget(L, LUA_GLOBALSINDEX);
        const bool hasHello = IsFunctionType(lua_type(L, -1));
        lua_settop(L, -2);
        if (hasHello && CallScriptFunction(L, "AgentBridge_Hello", 0, error)) {
          EncodeLuaValue(L, -1, message, 0);
          lua_settop(L, -2);
        } else {
          message += "null";
          if (hasHello) {
            runtime.pendingErrors.push_back(error);
          }
        }
        runtime.helloSent = true;
        AGENT_LoadMark("hello");
        AppendLoadMarks(message);
      } else {
        message += "{\"type\":\"obs\",\"tick\":";
        AppendJsonNumber(message, static_cast<double>(tick));
        message += gameOver ? ",\"gameOver\":true" : ",\"gameOver\":false";
        message += ",\"result\":";
        message += runtime.lastResultJson;
        message += ",\"obs\":";
        const bool observed = CallScriptFunction(L, "AgentBridge_Observe", 0, error);
        observeLuaMs = MillisecondsSince(stepStart);
        if (observed) {
          EncodeLuaValue(L, -1, message, 0);
          lua_settop(L, -2);
        } else {
          message += "null";
          runtime.pendingErrors.push_back(error);
        }
      }
      const double observeMs = MillisecondsSince(stepStart);
      message += ",\"timing\":{\"simMs\":";
      AppendJsonNumber(message, simMs);
      message += ",\"simCpuMs\":";
      AppendJsonNumber(message, simCpuMs);
      message += ",\"observeMs\":";
      AppendJsonNumber(message, observeMs);
      message += ",\"observeLuaMs\":";
      AppendJsonNumber(message, observeLuaMs);
      message += ",\"obsBytes\":";
      AppendJsonNumber(message, static_cast<double>(message.size()));
      message += ",\"luaKB\":";
      AppendJsonNumber(message, static_cast<double>(lua_getgccount(L)));
      if (!runtime.laps.empty()) {
        message += ",\"laps\":{";
        for (std::size_t i = 0; i < runtime.laps.size(); ++i) {
          if (i > 0) {
            message.push_back(',');
          }
          AppendJsonString(message, runtime.laps[i].first);
          message.push_back(':');
          AppendJsonNumber(message, runtime.laps[i].second);
        }
        message.push_back('}');
      }
      message += ",\"actMs\":";
      AppendJsonNumber(message, runtime.lastActMs);
      message += ",\"beatMs\":";
      AppendJsonNumber(message, runtime.beatMs);
      message += ",\"beats\":";
      AppendJsonNumber(message, runtime.beats);
      message += "}";
      runtime.beatMs = 0.0;
      runtime.beats = 0;
      AppendErrors(message, runtime.pendingErrors);
      message += "}";

      MarkBusy(false); // waiting on the agent is not the sim being stuck
      if (!runtime.connection.SendLine(message)) {
        TerminateGame(0, "the agent connection closed while sending");
      }

      // ---- wait for the agent's reply (the sim thread blocks here)
      std::string reply;
      const bool received = runtime.connection.ReceiveLine(reply);
      platform::SetX87PrecisionControl(_PC_24);
      if (!received) {
        TerminateGame(0, "the agent disconnected");
      }
      MarkBusy(true);

      // ---- apply it
      const LARGE_INTEGER actStart = Now();
      const int top = lua_gettop(L);
      int waitTicks = 1;
      runtime.lastResultJson = "null";

      JsonToLua parser(L, reply);
      bool parsed = false;
      try {
        parsed = parser.Parse();
      } catch (const std::exception& exception) {
        runtime.pendingErrors.push_back(std::string("parsing the agent reply: ") + exception.what());
      } catch (...) {
        runtime.pendingErrors.push_back("parsing the agent reply: unknown exception");
      }

      if (!parsed) {
        if (!parser.Error().empty()) {
          runtime.pendingErrors.push_back("the agent reply is not valid JSON: " + parser.Error());
        }
        lua_settop(L, top);
      } else {
        const int replyIndex = lua_gettop(L);
        if (lua_type(L, replyIndex) == LUA_TTABLE) {
          PushRawField(L, replyIndex, "cmd");
          const bool quit = lua_type(L, -1) == LUA_TSTRING && std::strcmp(lua_tostring(L, -1), "quit") == 0;
          lua_settop(L, replyIndex);
          if (quit) {
            TerminateGame(0, "the agent asked to quit");
          }

          PushRawField(L, replyIndex, "wait");
          if (lua_type(L, -1) == LUA_TNUMBER) {
            const double requested = static_cast<double>(lua_tonumber(L, -1));
            waitTicks = static_cast<int>(std::clamp(requested, 1.0, static_cast<double>(kMaxWaitTicks)));
          }
          lua_settop(L, replyIndex);
        }

        if (CallScriptFunction(L, "AgentBridge_Act", 1, error)) {
          std::string result;
          EncodeLuaValue(L, -1, result, 0);
          runtime.lastResultJson = std::move(result);
        } else {
          runtime.pendingErrors.push_back(error);
        }
        lua_settop(L, top);
      }

      runtime.nextTick = tick + static_cast<std::uint32_t>(waitTicks);
      runtime.lastActMs = MillisecondsSince(actStart);
      runtime.lastReplyTime = Now();
      runtime.cpuAtLastReply = ThreadCpu100ns();
      MarkBusy(false);
    }
  } // namespace

  namespace
  {
    // Diagnostics (engine addition): with /netdiag, dump the client manager's beat bookkeeping (queued,
    // dispatched and available beats per client, readiness) to the log every five seconds -- for
    // finding out why a session (e.g. a replay) stops dispatching beats.
    struct NetDiagnosticsStarter
    {
      NetDiagnosticsStarter()
      {
        if (!CFG_GetArgOption("/netdiag", 0, nullptr)) {
          return;
        }
        std::thread([] {
          for (;;) {
            ::Sleep(5000);
            if (ISTIDriver* const driver = WLD_GetDriver(); driver != nullptr) {
              if (CClientManagerImpl* const clientManager = driver->GetClientManager(); clientManager != nullptr) {
                clientManager->Debug();
              }
            }
          }
        }).detach();
      }
    };
    const NetDiagnosticsStarter gNetDiagnosticsStarter{};
  } // namespace

  void AGENT_LoadMark(const char* const phase)
  {
    if (!Config().enabled) {
      return;
    }
    const double ms = MillisecondsSinceProcessStart();
    LoadMarks& marks = Marks();
    std::lock_guard<std::mutex> guard(marks.lock);
    marks.marks.emplace_back(phase, ms);
  }

  void AGENT_OnSimBeatStart()
  {
    if (Config().enabled) {
      Runtime().beatStart = Now();
      MarkBusy(true);
    }
  }

  void AGENT_OnSimBeat(Sim* const sim)
  {
    const AgentBridgeConfig& config = Config();
    if (!config.enabled || sim == nullptr || sim->mLuaState == nullptr || sim->mLuaState->m_state == nullptr) {
      return;
    }

    AgentBridgeRuntime& runtime = Runtime();
    if (runtime.beatStart.QuadPart != 0) {
      runtime.beatMs += MillisecondsSince(runtime.beatStart);
      ++runtime.beats;
    }
    MarkBusy(false);
    gMainLuaState.store(sim->mLuaState->m_state, std::memory_order_relaxed);
    lua_State* const L = sim->mLuaState->m_state;
    const std::uint32_t tick = sim->mCurTick;
    const bool gameOver = sim->mGameOver;

    // A new session in this process (or the first one): reload the script into its Lua state and
    // restart the handshake.
    if (runtime.scriptState != L) {
      AGENT_LoadMark("first_beat");
      runtime.helloSent = false;
      runtime.nextTick = 0;
      runtime.lastTick = UINT32_MAX;
      runtime.gameOverReported = false;
      runtime.lastResultJson = "null";
      runtime.lastReplyTime = LARGE_INTEGER{};
    }

    // Step when a tick has run and is due, and once more when the game ends.
    const bool gameOverStep = gameOver && !runtime.gameOverReported && runtime.scriptState == L;
    if (!gameOverStep) {
      if (gameOver || tick == runtime.lastTick) {
        return;
      }
      runtime.lastTick = tick;
      if (runtime.scriptState == L && tick < runtime.nextTick) {
        return;
      }
    }
    runtime.lastTick = tick;
    if (gameOverStep) {
      runtime.gameOverReported = true;
    }

    if (!runtime.connection.IsOpen()) {
      gpg::Logf("AgentBridge: connecting to 127.0.0.1:%u", static_cast<unsigned>(config.port));
      const bool connected = runtime.connection.Connect(config.port, config.connectTimeoutSeconds);
      platform::SetX87PrecisionControl(_PC_24);
      if (!connected) {
        TerminateGame(3, "could not connect to the agent");
      }
      gpg::Logf("AgentBridge: connected");
      StartWatchdogOnce(config.watchdogSeconds);
    }

    const int savedTop = lua_gettop(L);
    if (runtime.scriptState != L) {
      std::string error;
      if (!LoadScript(L, config.scriptPath, error)) {
        std::string message = "{\"type\":\"error\",\"tick\":";
        AppendJsonNumber(message, static_cast<double>(tick));
        message += ",\"errors\":[";
        AppendJsonString(message, error);
        message += "]}";
        gpg::Warnf("AgentBridge: %s", error.c_str());
        (void)runtime.connection.SendLine(message);
        TerminateGame(4, "the agent script failed to load");
      }
      runtime.scriptState = L;
      gpg::Logf("AgentBridge: loaded %s", config.scriptPath.c_str());
    }

    RunStep(sim, L, runtime, gameOver);
    lua_settop(L, savedTop);
  }

  int AGENT_ResolvePacingSimRate(const int requestedSimRate)
  {
    const AgentBridgeConfig& config = Config();
    return config.enabled ? config.speed : requestedSimRate;
  }

  bool AGENT_UserDrainsAllBeats()
  {
    const AgentBridgeConfig& config = Config();
    return config.enabled && config.speed > 10;
  }

  int AGENT_ScaleRunAheadLimit(const int limit)
  {
    return AGENT_UserDrainsAllBeats() ? limit * 10 : limit;
  }

  int AGENT_ForcedGcPeriod(const int stockPeriodTicks)
  {
    return AGENT_UserDrainsAllBeats() ? stockPeriodTicks * 10 : stockPeriodTicks;
  }

  bool AGENT_SkipParticleEmission()
  {
    return AGENT_UserDrainsAllBeats() && !Config().keepVisualEffects;
  }
} // namespace moho
