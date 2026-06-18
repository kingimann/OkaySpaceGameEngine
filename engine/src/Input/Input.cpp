#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cctype>

#if defined(__unix__) || defined(__APPLE__)
#  include <termios.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace okay {

std::unordered_map<char, bool> Input::s_current;
std::unordered_map<char, bool> Input::s_previous;
bool Input::s_interactive = false;

Vec2     Input::s_mousePos;
unsigned Input::s_mouseCurrent = 0;
unsigned Input::s_mousePrevious = 0;

#if defined(__unix__) || defined(__APPLE__)
namespace {
    termios g_savedTermios;
    int     g_savedFlags = 0;
    bool    g_rawActive  = false;
}
#endif

void Input::BeginSession() {
#if defined(__unix__) || defined(__APPLE__)
    if (!isatty(STDIN_FILENO)) { s_interactive = false; return; }
    if (tcgetattr(STDIN_FILENO, &g_savedTermios) != 0) { s_interactive = false; return; }
    termios raw = g_savedTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_savedFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_savedFlags | O_NONBLOCK);
    g_rawActive   = true;
    s_interactive = true;
#else
    s_interactive = false;
#endif
}

void Input::EndSession() {
#if defined(__unix__) || defined(__APPLE__)
    if (g_rawActive) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTermios);
        fcntl(STDIN_FILENO, g_savedFlags, 0);
        g_rawActive = false;
    }
#endif
    s_current.clear();
    s_previous.clear();
}

void Input::Poll() {
    s_previous = s_current;
    // Keys are momentary: a key counts as "down" only on the frame it arrives.
    for (auto& kv : s_current) kv.second = false;

#if defined(__unix__) || defined(__APPLE__)
    if (!s_interactive) return;
    char buf[32];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(buf[i])));
            s_current[c] = true;
        }
    }
#endif
}

void Input::FeedKeys(const std::vector<char>& downKeys) {
    s_previous = s_current;
    s_current.clear();
    for (char k : downKeys)
        s_current[static_cast<char>(std::tolower(static_cast<unsigned char>(k)))] = true;
}

void Input::FeedMouse(const Vec2& position, unsigned buttonMask) {
    s_mousePos = position;
    s_mousePrevious = s_mouseCurrent;
    s_mouseCurrent = buttonMask;
}

Vec2 Input::MousePosition() { return s_mousePos; }

bool Input::GetMouseButton(int button) {
    if (button < 0 || button > 2) return false;
    return (s_mouseCurrent & (1u << button)) != 0;
}
bool Input::GetMouseButtonDown(int button) {
    if (button < 0 || button > 2) return false;
    unsigned bit = 1u << button;
    return (s_mouseCurrent & bit) && !(s_mousePrevious & bit);
}
bool Input::GetMouseButtonUp(int button) {
    if (button < 0 || button > 2) return false;
    unsigned bit = 1u << button;
    return !(s_mouseCurrent & bit) && (s_mousePrevious & bit);
}

bool Input::GetKey(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    auto it = s_current.find(key);
    return it != s_current.end() && it->second;
}

bool Input::GetKeyDown(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    bool now  = GetKey(key);
    auto prev = s_previous.find(key);
    bool was  = prev != s_previous.end() && prev->second;
    return now && !was;
}

bool Input::GetKeyUp(char key) {
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    bool now  = GetKey(key);
    auto prev = s_previous.find(key);
    bool was  = prev != s_previous.end() && prev->second;
    return !now && was;
}

Vec2 Input::AxisWASD() {
    Vec2 axis;
    if (GetKey('d')) axis.x += 1.0f;
    if (GetKey('a')) axis.x -= 1.0f;
    if (GetKey('w')) axis.y += 1.0f;
    if (GetKey('s')) axis.y -= 1.0f;
    return axis;
}

} // namespace okay
