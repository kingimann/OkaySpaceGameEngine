#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Core/Prefs.hpp"

#include <algorithm>
#include <cctype>

namespace okay {

namespace {

float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

struct State {
    AccessibilitySettings settings;
    std::vector<std::pair<int, Accessibility::Listener>> listeners;
    int nextId = 1;
    Accessibility::Announcer announcer;
};
State& S() {
    static State s;
    return s;
}

std::string JoinActions(const std::set<std::string>& a) {
    std::string out;
    for (const auto& n : a) { if (!out.empty()) out += ';'; out += n; }
    return out;
}

std::set<std::string> SplitActions(const std::string& s) {
    std::set<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ';') {
            if (i > start) out.insert(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

} // namespace

// =====================================================================================
const char* AimAssistName(AimAssist level) {
    switch (level) {
        case AimAssist::Low: return "low";
        case AimAssist::Medium: return "medium";
        case AimAssist::High: return "high";
        case AimAssist::Off: break;
    }
    return "off";
}

bool AimAssistFromName(const std::string& nameIn, AimAssist& level) {
    const std::string n = Lower(nameIn);
    if (n == "off" || n == "none") level = AimAssist::Off;
    else if (n == "low") level = AimAssist::Low;
    else if (n == "medium" || n == "mid") level = AimAssist::Medium;
    else if (n == "high") level = AimAssist::High;
    else return false;
    return true;
}

float AimAssistStrength(AimAssist level) {
    switch (level) {
        case AimAssist::Low: return 0.35f;
        case AimAssist::Medium: return 0.65f;
        case AimAssist::High: return 1.0f;
        case AimAssist::Off: break;
    }
    return 0.0f;
}

// =====================================================================================
void AccessibilitySettings::Clamp() {
    textScale = okay::Clamp(textScale, 0.75f, 2.0f);
    colorblindStrength = okay::Clamp(colorblindStrength, 0.0f, 1.0f);
    subtitles.Clamp();
    for (auto it = toggleActions.begin(); it != toggleActions.end();)
        it = it->empty() ? toggleActions.erase(it) : std::next(it);
}

std::uint32_t AccessibilitySettings::Diff(const AccessibilitySettings& o) const {
    std::uint32_t d = 0;
    if (textScale != o.textScale) d |= kA11yTextScale;
    if (highContrast != o.highContrast) d |= kA11yHighContrast;
    if (reduceMotion != o.reduceMotion) d |= kA11yReduceMotion;
    if (reduceFlashing != o.reduceFlashing) d |= kA11yReduceFlashing;
    if (colorblind != o.colorblind || colorblindStrength != o.colorblindStrength) d |= kA11yColorblind;
    if (subtitles != o.subtitles) d |= kA11ySubtitles;
    if (toggleAllHolds != o.toggleAllHolds || toggleActions != o.toggleActions) d |= kA11yHoldToggle;
    if (aimAssist != o.aimAssist) d |= kA11yAimAssist;
    if (screenReaderHints != o.screenReaderHints) d |= kA11yScreenReader;
    return d;
}

void AccessibilitySettings::Load() {
    if (Prefs::Has("a11y.textScale")) textScale = Prefs::GetFloat("a11y.textScale");
    if (Prefs::Has("a11y.highContrast")) highContrast = Prefs::GetInt("a11y.highContrast") != 0;
    if (Prefs::Has("a11y.reduceMotion")) reduceMotion = Prefs::GetInt("a11y.reduceMotion") != 0;
    if (Prefs::Has("a11y.reduceFlashing")) reduceFlashing = Prefs::GetInt("a11y.reduceFlashing") != 0;
    if (Prefs::Has("a11y.colorblind")) { ColorblindMode m; if (ColorblindFromName(Prefs::GetString("a11y.colorblind"), m)) colorblind = m; }
    if (Prefs::Has("a11y.colorblindStrength")) colorblindStrength = Prefs::GetFloat("a11y.colorblindStrength");
    if (Prefs::Has("a11y.toggleAll")) toggleAllHolds = Prefs::GetInt("a11y.toggleAll") != 0;
    if (Prefs::Has("a11y.toggle")) toggleActions = SplitActions(Prefs::GetString("a11y.toggle"));
    if (Prefs::Has("a11y.aimAssist")) { AimAssist a; if (AimAssistFromName(Prefs::GetString("a11y.aimAssist"), a)) aimAssist = a; }
    if (Prefs::Has("a11y.screenReader")) screenReaderHints = Prefs::GetInt("a11y.screenReader") != 0;
    subtitles.Load();
    Clamp();
}

void AccessibilitySettings::Save() const {
    Prefs::SetFloat("a11y.textScale", textScale);
    Prefs::SetInt("a11y.highContrast", highContrast ? 1 : 0);
    Prefs::SetInt("a11y.reduceMotion", reduceMotion ? 1 : 0);
    Prefs::SetInt("a11y.reduceFlashing", reduceFlashing ? 1 : 0);
    Prefs::SetString("a11y.colorblind", ColorblindName(colorblind));
    Prefs::SetFloat("a11y.colorblindStrength", colorblindStrength);
    Prefs::SetInt("a11y.toggleAll", toggleAllHolds ? 1 : 0);
    Prefs::SetString("a11y.toggle", JoinActions(toggleActions));
    Prefs::SetString("a11y.aimAssist", AimAssistName(aimAssist));
    Prefs::SetInt("a11y.screenReader", screenReaderHints ? 1 : 0);
    subtitles.Save();
}

// =====================================================================================
const AccessibilitySettings& Accessibility::Get() { return S().settings; }

void Accessibility::Set(const AccessibilitySettings& in) {
    AccessibilitySettings next = in;
    next.Clamp();
    const std::uint32_t changed = next.Diff(S().settings);
    if (changed == 0) return;
    S().settings = next;
    if (changed & kA11ySubtitles) CaptionManager::Main().settings = next.subtitles;
    const auto listeners = S().listeners;   // a listener may remove itself
    for (const auto& l : listeners) if (l.second) l.second(S().settings, changed);
}

void Accessibility::Load() {
    AccessibilitySettings s = S().settings;
    s.Load();
    Set(s);
}

void Accessibility::Save() { S().settings.Save(); }

void Accessibility::Reset() {
    S().settings = AccessibilitySettings{};
    S().listeners.clear();
    S().announcer = nullptr;
    CaptionManager::Main().settings = S().settings.subtitles;
}

int Accessibility::AddListener(Listener fn) {
    const int id = S().nextId++;
    S().listeners.emplace_back(id, std::move(fn));
    return id;
}

void Accessibility::RemoveListener(int id) {
    auto& ls = S().listeners;
    ls.erase(std::remove_if(ls.begin(), ls.end(), [id](const auto& l) { return l.first == id; }), ls.end());
}

#define OKAY_A11Y_EDIT(expr) do { AccessibilitySettings s = S().settings; expr; Set(s); } while (0)
void Accessibility::SetTextScale(float scale) { OKAY_A11Y_EDIT(s.textScale = scale); }
void Accessibility::SetHighContrast(bool on) { OKAY_A11Y_EDIT(s.highContrast = on); }
void Accessibility::SetReduceMotion(bool on) { OKAY_A11Y_EDIT(s.reduceMotion = on); }
void Accessibility::SetReduceFlashing(bool on) { OKAY_A11Y_EDIT(s.reduceFlashing = on); }
void Accessibility::SetColorblind(ColorblindMode mode, float strength) { OKAY_A11Y_EDIT((s.colorblind = mode, s.colorblindStrength = strength)); }
void Accessibility::SetSubtitles(const SubtitleSettings& sub) { OKAY_A11Y_EDIT(s.subtitles = sub); }
void Accessibility::SetToggleAllHolds(bool on) { OKAY_A11Y_EDIT(s.toggleAllHolds = on); }
void Accessibility::SetToggleAction(const std::string& action, bool toggle) {
    OKAY_A11Y_EDIT((toggle ? (void)s.toggleActions.insert(action) : (void)s.toggleActions.erase(action)));
}
void Accessibility::SetAimAssist(AimAssist level) { OKAY_A11Y_EDIT(s.aimAssist = level); }
void Accessibility::SetScreenReaderHints(bool on) { OKAY_A11Y_EDIT(s.screenReaderHints = on); }
#undef OKAY_A11Y_EDIT

float Accessibility::ScaleText(float pixels) { return pixels * S().settings.textScale; }
float Accessibility::MotionScale() { return S().settings.reduceMotion ? 0.0f : 1.0f; }
float Accessibility::TransitionSeconds(float seconds) { return S().settings.reduceMotion ? seconds * 0.25f : seconds; }
float Accessibility::FlashScale() { return S().settings.reduceFlashing ? 0.0f : 1.0f; }

Color Accessibility::FilterColor(const Color& c) {
    const AccessibilitySettings& s = S().settings;
    return DaltonizeColor(c, s.colorblind, s.colorblindStrength);
}

void Accessibility::FilterFrame(std::uint8_t* rgba, std::size_t pixels) {
    const AccessibilitySettings& s = S().settings;
    DaltonizeRgba8(rgba, pixels, s.colorblind, s.colorblindStrength);
}

Color Accessibility::TextColor(const Color& foreground, const Color& background) {
    return S().settings.highContrast ? EnsureContrast(foreground, background, 7.0f) : foreground;
}

bool Accessibility::ToggleFor(const std::string& action) { return S().settings.ToggleFor(action); }
float Accessibility::AimAssistStrength() { return okay::AimAssistStrength(S().settings.aimAssist); }

void Accessibility::SetAnnouncer(Announcer announcer) { S().announcer = std::move(announcer); }

bool Accessibility::Announce(const std::string& text) {
    if (!S().settings.screenReaderHints || !S().announcer || text.empty()) return false;
    S().announcer(text);
    return true;
}

// =====================================================================================
bool HoldToggle::Update(bool held, bool toggleMode) {
    if (toggleMode) {
        if (held && !m_held) m_active = !m_active;   // each press flips it
    } else {
        m_active = held;
    }
    m_held = held;
    return m_active;
}

} // namespace okay
