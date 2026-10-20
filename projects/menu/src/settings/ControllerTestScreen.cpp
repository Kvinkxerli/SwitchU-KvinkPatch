#include "ControllerTestScreen.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr float kStickSelectThreshold = 0.55f;
constexpr float kStickSelectDelay = 0.45f;
constexpr float kDeadzone = 0.12f;

void drawPanel(nxui::Renderer& ren, const nxui::Theme& theme, const nxui::Rect& rect, float alpha) {
    ren.drawRoundedRect(rect, theme.panelBase.withAlpha(0.93f * alpha), 22.f);
    ren.drawRoundedRectOutline(rect, theme.panelBorder.withAlpha(0.32f * alpha), 22.f, 1.2f);
    ren.drawRoundedRectOutline(rect.shrunk(1.5f), theme.panelHighlight.withAlpha(0.08f * alpha), 20.5f, 1.f);
}
}

ControllerTestScreen::ControllerTestScreen() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
}

namespace {
constexpr HidNpadIdType kPlayerIds[] = {
    HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
    HidNpadIdType_Handheld,
};

std::string styleName(u32 style) {
    auto& i18n = nxui::I18n::instance();
    if (style == 0)
        return i18n.tr("controllers.disconnected", "Not connected");
    if (style & HidNpadStyleTag_NpadHandheld)
        return i18n.tr("controllers.handheld", "Handheld mode");
    if (style & HidNpadStyleTag_NpadFullKey)
        return i18n.tr("controllers.pro", "Pro Controller");
    if (style & HidNpadStyleTag_NpadJoyDual)
        return i18n.tr("controllers.joycon_pair", "Joy-Con (L + R)");
    if (style & HidNpadStyleTag_NpadJoyLeft)
        return i18n.tr("controllers.joycon_l", "Joy-Con (L)");
    if (style & HidNpadStyleTag_NpadJoyRight)
        return i18n.tr("controllers.joycon_r", "Joy-Con (R)");
    if (style & HidNpadStyleTag_NpadGc)
        return i18n.tr("controllers.gamecube", "GameCube controller");
    if (style & HidNpadStyleTag_NpadPalma)
        return i18n.tr("controllers.poke_ball", "Poke Ball Plus");
    return i18n.tr("controllers.other", "Other controller");
}
} // namespace

void ControllerTestScreen::showManager() {
    if (m_active)
        return;
    show();                 // base activation: animations, visibility, sfx hooks
    m_mode = Mode::Manager;
    clearActions();         // the Test-mode X action must not leak into Manager
    m_managerSel = 0;
    m_status.clear();
    m_statusTime = 0.f;
    m_batteryTimer = 1.f; // force a fresh battery poll on the first frame
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr("controllers.opened",
            "Controllers. Up and Down select a player, Y swaps with the next player, X changes grip, Plus pairs a new controller, Minus tests input, B closes."));
}

// Battery state comes straight from libnx's public HID API - no raw IPC, no
// private symbols (verified against switchbrew/libnx master hid.h):
//   u32  hidGetNpadStyleSet(HidNpadIdType id)        -> connection/style bits
//   void hidGetNpadPowerInfoSingle(id, HidPowerInfo*)      -> main device
//   void hidGetNpadPowerInfoSplit(id, left, right)         -> Joy-Con pair
// HidPowerInfo { bool is_powered; bool is_charging; u8 pad[6];
//                u32 battery_level /* always 0-4 */ }.
// When a pad has no battery data hidd leaves battery_level at its default and
// is_powered false; we treat "no power info" as valid=false only when the
// style itself can report batteries but the level is out of range.
namespace {
constexpr float kBatteryPollInterval = 0.5f;

// prop = 0 -> single/main device of the pad; nonzero -> left/right halves of
// a detached Joy-Con pair via hidGetNpadPowerInfoSplit. Returns false when
// the reported level is out of the documented 0..4 range.
bool readDevicePowerInfo(HidNpadIdType id, u64 wantRight, s16& outLevel, bool& outCharging) {
    outLevel = -1;
    outCharging = false;
    if (wantRight == 0ULL) {
        HidPowerInfo info{};
        hidGetNpadPowerInfoSingle(id, &info);
        if (info.battery_level > 4u)
            return false;
        outLevel = static_cast<s16>(info.battery_level);
        outCharging = info.is_charging != false && info.is_powered != false;
        return true;
    }
    HidPowerInfo left{}, right{};
    hidGetNpadPowerInfoSplit(id, &left, &right);
    const HidPowerInfo& info = (wantRight == 2ULL) ? right : left;
    if (info.battery_level > 4u)
        return false;
    outLevel = static_cast<s16>(info.battery_level);
    outCharging = info.is_charging != false && info.is_powered != false;
    return true;
}
} // namespace

// Styles whose devices report no battery at all (wired GC, Poké Ball Plus,
// virtual/debug pads).
bool styleHasNoBattery(u32 style) {
    return (style & (HidNpadStyleTag_NpadGc | HidNpadStyleTag_NpadPalma |
                     HidNpadStyleTag_NpadSystem | HidNpadStyleTag_NpadSystemExt)) != 0;
}
} // namespace

void ControllerTestScreen::pollBatteries() {
    auto clearSlot = [&](int slot) {
        m_battery[slot] = {};
        m_batteryType[slot] = 0;
    };
    auto applyInfo = [&](int slot, s16 level, bool charging, bool valid) {
        DeviceBattery& b = m_battery[slot];
        b.level = level;
        b.charging = charging;
        b.valid = valid;
    };
    // Slots 0..7 are Players 1-8, slot 8 is Handheld, slot 9 is the right
    // half of Player 1's detached Joy-Con pair (if any).
    for (int i = 0; i < 8; ++i) {
        const HidNpadIdType id = kPlayerIds[i];
        const u32 style = hidGetNpadStyleSet(id);
        if (style == 0 || styleHasNoBattery(style)) {
            clearSlot(i);
            if (i == 0) clearSlot(9);
            continue;
        }
        m_batteryType[i] = static_cast<u8>(style);
        s16 level = -1;
        bool charging = false;
        const bool ok = readDevicePowerInfo(id, 0ULL, level, charging);
        applyInfo(i, ok ? level : -1, ok && charging, ok);
        if (i == 0 && (style & HidNpadStyleTag_NpadJoyDual)) {
            // Show both halves separately for a detached Joy-Con pair.
            s16 rlevel = -1;
            bool rcharging = false;
            const bool rok = readDevicePowerInfo(id, 2ULL, rlevel, rcharging);
            applyInfo(9, rok ? rlevel : -1, rok && rcharging, rok);
            m_batteryType[9] = static_cast<u8>(HidNpadStyleTag_NpadJoyRight);
        } else if (i == 0) {
            clearSlot(9);
        }
    }
    const u32 handheldStyle = hidGetNpadStyleSet(HidNpadIdType_Handheld);
    m_batteryType[8] = static_cast<u8>(handheldStyle);
    if (handheldStyle == 0) {
        clearSlot(8);
    } else {
        s16 level = -1;
        bool charging = false;
        const bool ok = readDevicePowerInfo(HidNpadIdType_Handheld, 0ULL, level, charging);
        applyInfo(8, ok ? level : -1, ok && charging, ok);
    }
}

void ControllerTestScreen::updateManager(float dt) {
    if (m_statusTime > 0.f)
        m_statusTime -= dt;
    m_batteryTimer += dt;
    if (m_batteryTimer >= kBatteryPollInterval) {
        m_batteryTimer = 0.f;
        pollBatteries();
    }
    if (!m_input)
        return;
    auto& i18n = nxui::I18n::instance();

    if (m_input->isDown(nxui::Button::DUp))
        m_managerSel = (m_managerSel + kPlayerRows - 1) % kPlayerRows;
    if (m_input->isDown(nxui::Button::DDown))
        m_managerSel = (m_managerSel + 1) % kPlayerRows;

    if (m_input->isDown(nxui::Button::Y) && m_managerSel < 7) {
        Result rc = hidSwapNpadAssignment(kPlayerIds[m_managerSel], kPlayerIds[m_managerSel + 1]);
        if (R_SUCCEEDED(rc)) {
            ++m_managerSel;
            setStatus(i18n.tr("controllers.swapped", "Player order changed"));
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "0x%X", (unsigned)rc);
            setStatus(i18n.tr("controllers.swap_failed", "Could not change order") + " " + buf);
        }
        if (m_accessibilityCb) m_accessibilityCb(m_status);
    }

    if (m_input->isDown(nxui::Button::X)) {
        HidNpadJoyHoldType hold = HidNpadJoyHoldType_Vertical;
        hidGetNpadJoyHoldType(&hold);
        hold = hold == HidNpadJoyHoldType_Vertical ? HidNpadJoyHoldType_Horizontal
                                                   : HidNpadJoyHoldType_Vertical;
        hidSetNpadJoyHoldType(hold);
        setStatus(hold == HidNpadJoyHoldType_Horizontal
            ? i18n.tr("controllers.grip_horizontal", "Joy-Con grip: horizontal (sideways)")
            : i18n.tr("controllers.grip_vertical", "Joy-Con grip: vertical (single Joy-Con)"));
        if (m_accessibilityCb) m_accessibilityCb(m_status);
    }

    if (m_input->isDown(nxui::Button::Plus) && m_pairingCb)
        m_pairingCb();

    if (m_input->isDown(nxui::Button::Minus)) {
        m_mode = Mode::Test;
        setupActions();
        return;
    }

    if (m_input->isDown(nxui::Button::B))
        hide();
}

// Battery glyph: a small horizontal cell with 4 bars (like the system UI),
// plus an optional bolt-ish "+" marker while charging. Unknown state renders
// as an outlined empty cell so rows never jump around. `level` is the raw
// hidd value 0..4 (-1 = no data).
void ControllerTestScreen::drawBatteryIcon(nxui::Renderer& ren, nxui::Vec2 pos,
                                           const std::string& name, s16 level,
                                           bool charging, bool connected,
                                           const nxui::Color& fg, const nxui::Color& dim,
                                           float alpha) {
    constexpr float kW = 30.f;
    constexpr float kH = 15.f;
    nxui::Rect body{pos.x, pos.y, kW, kH};
    const nxui::Color outline = connected ? fg.withAlpha(0.85f * alpha)
                                          : dim.withAlpha(0.55f * alpha);
    ren.drawRoundedRectOutline(body, outline, 3.5f, 1.4f);
    // Positive terminal nub on the right.
    ren.drawRect({body.right() + 1.f, pos.y + kH * 0.30f, 2.5f, kH * 0.40f}, outline);

    if (connected && level >= 0) {
        const int filled = std::clamp(static_cast<int>(level), 0, 4);
        // Red tint for critical battery (hidd reports at most one bar left),
        // cursor accent otherwise - both work on light and dark themes.
        const nxui::Color low{0.90f, 0.30f, 0.25f, 1.f};
        const nxui::Color fillC = (filled <= 1 ? low : m_theme->cursorNormal)
                                      .withAlpha(0.95f * alpha);
        constexpr float kPad = 2.5f;
        constexpr float kGap = 1.6f;
        const float usableW = kW - 2.f * kPad - 3.f * kGap;
        const float barW = usableW / 4.f;
        for (int b = 0; b < filled; ++b)
            ren.drawRect({body.x + kPad + b * (barW + kGap), body.y + kPad,
                          barW, kH - 2.f * kPad}, fillC);
    }

    char pct[24];
    if (!connected)
        std::snprintf(pct, sizeof(pct), "%s", "--");
    else if (level >= 0)
        std::snprintf(pct, sizeof(pct), "%u%s", static_cast<unsigned>(level),
                      charging ? " +" : "");
    else {
        const std::string noData =
            nxui::I18n::instance().tr("controllers.battery_unavailable", "no data");
        std::snprintf(pct, sizeof(pct), "%s", noData.c_str());
    }
    nxui::Vec2 sz = m_smallFont->measure(pct);
    ren.drawText(pct, {pos.x + (kW - sz.x * 0.72f) * 0.5f, pos.y + kH + 3.f}, m_smallFont,
                 (connected ? fg : dim).withAlpha((connected ? 0.9f : 0.55f) * alpha), 0.72f);
}

void ControllerTestScreen::drawManager(nxui::Renderer& ren, const nxui::Rect& p, float alpha) {
    auto& i18n = nxui::I18n::instance();
    nxui::Rect list{p.x + 30.f, p.y + 82.f, p.width - 60.f, 452.f};
    drawPanel(ren, *m_theme, list, alpha);

    HidNpadJoyHoldType hold = HidNpadJoyHoldType_Vertical;
    hidGetNpadJoyHoldType(&hold);

    const float rowH = 46.f;
    for (int i = 0; i < kPlayerRows; ++i) {
        nxui::Rect row{list.x + 14.f, list.y + 12.f + i * (rowH + 2.f), list.width - 28.f, rowH};
        const u32 style = hidGetNpadStyleSet(kPlayerIds[i]);
        const bool connected = style != 0;
        const bool sel = i == m_managerSel;
        if (sel) {
            ren.drawRoundedRect(row, m_theme->cursorNormal.withAlpha(0.22f * alpha), 14.f);
            ren.drawRoundedRectOutline(row, m_theme->cursorNormal.withAlpha(0.85f * alpha), 14.f, 2.f);
        }
        std::string name = i == 8
            ? i18n.tr("controllers.handheld_slot", "Handheld")
            : i18n.tr("controllers.player", "Player") + " " + std::to_string(i + 1);
        ren.drawText(name, {row.x + 18.f, row.y + 10.f}, m_font,
                     m_theme->textPrimary.withAlpha(alpha), 0.80f);
        ren.drawText(styleName(style), {row.x + 250.f, row.y + 12.f}, m_smallFont,
                     (connected ? m_theme->textPrimary : m_theme->textSecondary)
                         .withAlpha((connected ? 1.f : 0.6f) * alpha), 0.80f);

        // Battery column: split Joy-Con pairs show L and R glyphs side by side.
        const DeviceBattery& main = m_battery[i];
        const bool dualPair = (style & HidNpadStyleTag_NpadJoyDual) != 0 && i == 0;
        float iconX = row.right() - (dualPair ? 150.f : 112.f);
        drawBatteryIcon(ren, {iconX, row.y + 12.f}, name, main.level, main.charging,
                        connected && main.valid, m_theme->textPrimary, m_theme->textSecondary, alpha);
        if (dualPair) {
            const DeviceBattery& rhalf = m_battery[9];
            drawBatteryIcon(ren, {iconX + 46.f, row.y + 12.f}, name, rhalf.level,
                            rhalf.charging, connected && rhalf.valid,
                            m_theme->textPrimary, m_theme->textSecondary, alpha);
        }
        ren.drawCircle({row.right() - 28.f, row.y + rowH * 0.5f}, 8.f,
                       (connected ? m_theme->cursorNormal : m_theme->panelBorder)
                           .withAlpha((connected ? 0.95f : 0.35f) * alpha), 20);
    }

    std::string grip = hold == HidNpadJoyHoldType_Horizontal
        ? i18n.tr("controllers.grip_horizontal_short", "Grip: horizontal")
        : i18n.tr("controllers.grip_vertical_short", "Grip: vertical");
    ren.drawText(grip, {p.x + 36.f, p.y + 548.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(alpha), 0.76f);
    if (m_statusTime > 0.f && !m_status.empty())
        ren.drawText(m_status, {p.x + 36.f, p.y + 578.f}, m_smallFont,
                     m_theme->textPrimary.withAlpha(alpha), 0.76f);
    ren.drawText(i18n.tr("controllers.footer",
                         "↑↓ Select   Y: Swap with next   X: Grip   +: Pair new   −: Test   B: Close"),
                 {p.x + 36.f, p.y + 614.f}, m_smallFont, m_theme->textSecondary.withAlpha(alpha), 0.72f);
}

void ControllerTestScreen::show() {
    if (m_active)
        return;
    m_mode = Mode::Test;
    m_active = true;
    m_animatingOut = false;
    m_activeStick = ActiveStick::None;
    m_candidateStick = ActiveStick::None;
    m_candidateTime = 0.f;
    m_stickX = m_stickY = 0.f;
    m_recordingTouch = false;
    m_touchFullscreen = false;
    m_bHoldTime = 0.f;
    m_touchTrace.clear();
    m_touchFullscreenAnim.setImmediate(0.f);
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, 0.22f, nxui::Easing::outCubic);
    m_scale.setImmediate(0.92f);
    m_scale.set(1.f, 0.24f, nxui::Easing::outCubic);
    setVisible(true);
    setupActions();
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr("controller_test.opened", "Controller test. Tilt a stick briefly to select it. Hold B for one second or press HOME to close."));
}

void ControllerTestScreen::hide() {
    if (!m_active || m_animatingOut)
        return;
    m_animatingOut = true;
    if (m_closeSfxCb) m_closeSfxCb();
    m_alpha.set(0.f, 0.18f, nxui::Easing::outCubic);
    m_scale.set(0.96f, 0.18f, nxui::Easing::outCubic);
    clearActions();
}

void ControllerTestScreen::setupActions() {
    clearActions();
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() {
        if (m_touchFullscreen)
            exitTouchFullscreen();
    });
}

nxui::Rect ControllerTestScreen::panelRect(float scale) const {
    nxui::Rect rect{32.f, 32.f, 1216.f, 656.f};
    float w = rect.width * scale;
    float h = rect.height * scale;
    rect.x += (rect.width - w) * 0.5f;
    rect.y += (rect.height - h) * 0.5f;
    rect.width = w;
    rect.height = h;
    return rect;
}

nxui::Rect ControllerTestScreen::touchPanelRect(float scale) const {
    nxui::Rect p = panelRect(scale);
    return {p.x + 516.f * scale, p.y + 360.f * scale, 668.f * scale, 264.f * scale};
}

void ControllerTestScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut || m_mode == Mode::Manager)
        return;

    nxui::Rect zone{0.f, 0.f, 1280.f, 720.f};
    if (!m_touchFullscreen) {
        zone = touchPanelRect(m_scale.value()).shrunk(18.f * m_scale.value());
        zone.y += 42.f * m_scale.value();
        zone.height -= 42.f * m_scale.value();
    }

    if (input.touchDown() && zone.contains(input.touchX(), input.touchY())) {
        if (!m_touchFullscreen) {
            enterTouchFullscreen();
            zone = {0.f, 0.f, 1280.f, 720.f};
        }
        m_touchTrace.clear();
        recordTouchPoint(input.touchX(), input.touchY(), zone);
        m_recordingTouch = true;
    }
    if (input.isTouching() && m_recordingTouch) {
        recordTouchPoint(input.touchX(), input.touchY(), zone);
    }
    if (input.touchUp())
        m_recordingTouch = false;
}

void ControllerTestScreen::enterTouchFullscreen() {
    m_touchFullscreen = true;
    m_touchFullscreenAnim.set(1.f, 0.24f, nxui::Easing::outCubic);
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr(
            "controller_test.touch_fullscreen_opened",
            "Full-screen touch test. Press X to return. Hold B for one second or press HOME to close."));
}

void ControllerTestScreen::exitTouchFullscreen() {
    if (!m_touchFullscreen)
        return;
    m_touchFullscreen = false;
    m_recordingTouch = false;
    m_touchFullscreenAnim.set(0.f, 0.20f, nxui::Easing::outCubic);
}

void ControllerTestScreen::recordTouchPoint(float x, float y, const nxui::Rect& surface) {
    if (surface.width <= 0.f || surface.height <= 0.f)
        return;
    nxui::Vec2 p{
        std::clamp((x - surface.x) / surface.width, 0.f, 1.f),
        std::clamp((y - surface.y) / surface.height, 0.f, 1.f),
    };
    const float minNormalizedDistance = 2.f / std::max(surface.width, surface.height);
    if (m_touchTrace.empty() || (p - m_touchTrace.back()).lengthSq() >= minNormalizedDistance * minNormalizedDistance)
        m_touchTrace.push_back(p);
    if (m_touchTrace.size() > 512)
        m_touchTrace.erase(m_touchTrace.begin(), m_touchTrace.begin() + 128);
}

void ControllerTestScreen::drawTouchTrace(nxui::Renderer& ren, const nxui::Rect& surface,
                                          float alpha, float thickness) {
    auto mapPoint = [&surface](const nxui::Vec2& p) {
        return nxui::Vec2{surface.x + p.x * surface.width, surface.y + p.y * surface.height};
    };
    for (std::size_t i = 1; i < m_touchTrace.size(); ++i)
        ren.drawLine(mapPoint(m_touchTrace[i - 1]), mapPoint(m_touchTrace[i]),
                     m_theme->cursorNormal.withAlpha(0.88f * alpha), thickness);
    if (!m_touchTrace.empty())
        ren.drawCircle(mapPoint(m_touchTrace.back()), thickness + 2.f,
                       m_theme->cursorNormal.withAlpha(alpha), 20);
}

void ControllerTestScreen::update(float dt) {
    if (!isActive())
        return;
    m_alpha.update(dt);
    m_scale.update(dt);
    m_touchFullscreenAnim.update(dt);
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_active = false;
        m_animatingOut = false;
        setVisible(false);
        if (m_closedCb) m_closedCb();
        return;
    }
    if (!m_active || !m_input)
        return;

    if (m_mode == Mode::Manager) {
        updateManager(dt);
        return;
    }

    if (m_input->isHeld(nxui::Button::B)) {
        m_bHoldTime += dt;
        if (m_bHoldTime >= 1.f) {
            m_bHoldTime = 0.f;
            hide();
            return;
        }
    } else {
        m_bHoldTime = 0.f;
    }

    const float lm = std::hypot(m_input->leftStickX(), m_input->leftStickY());
    const float rm = std::hypot(m_input->rightStickX(), m_input->rightStickY());
    ActiveStick candidate = ActiveStick::None;
    if (lm >= kStickSelectThreshold || rm >= kStickSelectThreshold)
        candidate = lm >= rm ? ActiveStick::Left : ActiveStick::Right;
    if (candidate == ActiveStick::None) {
        m_candidateStick = ActiveStick::None;
        m_candidateTime = 0.f;
    } else if (candidate != m_candidateStick) {
        m_candidateStick = candidate;
        m_candidateTime = 0.f;
    } else {
        m_candidateTime += dt;
        if (m_candidateTime >= kStickSelectDelay && m_activeStick != candidate) {
            m_activeStick = candidate;
            m_candidateTime = 0.f;
            if (m_accessibilityCb) {
                m_accessibilityCb(candidate == ActiveStick::Left
                    ? nxui::I18n::instance().tr("controller_test.left_selected", "Left stick selected")
                    : nxui::I18n::instance().tr("controller_test.right_selected", "Right stick selected"));
            }
        }
    }

    float x = 0.f, y = 0.f;
    if (m_activeStick == ActiveStick::Left) {
        x = m_input->leftStickX(); y = m_input->leftStickY();
    } else if (m_activeStick == ActiveStick::Right) {
        x = m_input->rightStickX(); y = m_input->rightStickY();
    }
    // A diagnostic view must display the hardware sample exactly. Filtering
    // made fast circular movements visibly lag behind and miss the edge.
    m_stickX = x;
    m_stickY = y;
}

std::string ControllerTestScreen::directionLabel(float x, float y) const {
    auto& i18n = nxui::I18n::instance();
    if (std::hypot(x, y) <= kDeadzone)
        return i18n.tr("controller_test.center", "Center");
    const bool horizontal = std::abs(x) > 0.38f;
    const bool vertical = std::abs(y) > 0.38f;
    if (vertical && horizontal)
        return (y > 0 ? i18n.tr("controller_test.up", "Up") : i18n.tr("controller_test.down", "Down"))
            + std::string(" · ")
            + (x > 0 ? i18n.tr("controller_test.right", "Right") : i18n.tr("controller_test.left", "Left"));
    if (horizontal) return x > 0 ? i18n.tr("controller_test.right", "Right") : i18n.tr("controller_test.left", "Left");
    return y > 0 ? i18n.tr("controller_test.up", "Up") : i18n.tr("controller_test.down", "Down");
}

void ControllerTestScreen::drawStickPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    auto& i18n = nxui::I18n::instance();
    ren.drawText(i18n.tr("controller_test.joysticks", "Joysticks"), {r.x + 24.f, r.y + 20.f},
                 m_font, m_theme->textPrimary.withAlpha(alpha), 0.92f);
    std::string active = m_activeStick == ActiveStick::Left
        ? i18n.tr("controller_test.left_stick", "Left stick")
        : m_activeStick == ActiveStick::Right
            ? i18n.tr("controller_test.right_stick", "Right stick")
            : i18n.tr("controller_test.tilt_to_select", "Tilt a stick briefly to select it");
    ren.drawText(active, {r.x + 24.f, r.y + 62.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(alpha), 0.76f);

    nxui::Vec2 center{r.x + r.width * 0.5f, r.y + 214.f};
    const float outer = 91.f;
    ren.drawCircle(center, outer, m_theme->panelBorder.withAlpha(0.24f * alpha), 48);
    ren.drawCircle(center, outer * kDeadzone, m_theme->cursorNormal.withAlpha(0.13f * alpha), 32);
    ren.drawCircle({center.x + m_stickX * (outer - 19.f), center.y - m_stickY * (outer - 19.f)},
                   19.f, m_theme->cursorNormal.withAlpha(0.92f * alpha), 32);

    char values[80]{};
    std::snprintf(values, sizeof(values), "X %+0.3f     Y %+0.3f", m_stickX, m_stickY);
    ren.drawText(values, {r.x + 24.f, r.bottom() - 72.f}, m_smallFont,
                 m_theme->textPrimary.withAlpha(alpha), 0.78f);
    std::string direction = i18n.tr("controller_test.direction", "Direction") + ": " + directionLabel(m_stickX, m_stickY);
    ren.drawText(direction, {r.x + 24.f, r.bottom() - 42.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(alpha), 0.74f);
}

void ControllerTestScreen::drawButtonsPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    ren.drawText(nxui::I18n::instance().tr("controller_test.buttons", "Buttons"),
                 {r.x + 24.f, r.y + 20.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 0.92f);
    static constexpr ButtonVisual buttons[] = {
        {nxui::Button::A,"A"},{nxui::Button::B,"B"},{nxui::Button::X,"X"},{nxui::Button::Y,"Y"},
        {nxui::Button::L,"L"},{nxui::Button::R,"R"},{nxui::Button::ZL,"ZL"},{nxui::Button::ZR,"ZR"},
        {nxui::Button::Plus,"+"},{nxui::Button::Minus,"−"},{nxui::Button::LStick,"L3"},{nxui::Button::RStick,"R3"},
        {nxui::Button::DUp,"↑"},{nxui::Button::DDown,"↓"},{nxui::Button::DLeft,"←"},{nxui::Button::DRight,"→"},
    };
    constexpr float gapX = 10.f;
    constexpr float gapY = 8.f;
    constexpr float cellH = 34.f;
    const float cellW = (r.width - 48.f - 3.f * gapX) / 4.f;
    for (int i = 0; i < 16; ++i) {
        int row = i / 4, col = i % 4;
        nxui::Rect cell{r.x + 24.f + col * (cellW + gapX),
                        r.y + 64.f + row * (cellH + gapY), cellW, cellH};
        const bool held = m_input && m_input->isHeld(buttons[i].button);
        ren.drawRoundedRect(cell, (held ? m_theme->cursorNormal : m_theme->panelBorder)
                                  .withAlpha((held ? 0.88f : 0.18f) * alpha), 13.f);
        ren.drawRoundedRectOutline(cell, (held ? m_theme->textPrimary : m_theme->panelBorder)
                                         .withAlpha((held ? 0.62f : 0.30f) * alpha), 13.f, 1.f);
        nxui::Vec2 sz = m_smallFont->measure(buttons[i].label);
        ren.drawText(buttons[i].label, {cell.x + (cell.width - sz.x * 0.68f) * 0.5f, cell.y + 6.f},
                     m_smallFont, (held ? m_theme->textPrimary : m_theme->textSecondary).withAlpha(alpha), 0.68f);
    }
}

void ControllerTestScreen::drawTouchPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    ren.drawText(nxui::I18n::instance().tr("controller_test.touchscreen", "Touch screen"),
                 {r.x + 24.f, r.y + 18.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 0.88f);
    nxui::Rect zone = r.shrunk(18.f);
    zone.y += 42.f; zone.height -= 42.f;
    ren.drawRoundedRect(zone, m_theme->background.withAlpha(0.52f * alpha), 15.f);
    ren.drawRoundedRectOutline(zone, m_theme->panelBorder.withAlpha(0.34f * alpha), 15.f, 1.f);
    drawTouchTrace(ren, zone, alpha, 5.f);
    if (m_touchTrace.empty())
        ren.drawText(nxui::I18n::instance().tr("controller_test.touch_hint", "Touch or click and drag here"),
                     {zone.x + 18.f, zone.y + 18.f}, m_smallFont, m_theme->textSecondary.withAlpha(0.72f * alpha), 0.74f);
}

void ControllerTestScreen::drawFullscreenTouch(nxui::Renderer& ren, float alpha) {
    const float t = std::clamp(m_touchFullscreenAnim.value(), 0.f, 1.f);
    if (t <= 0.01f)
        return;
    const float a = alpha * t;
    nxui::Rect screen{0.f, 0.f, 1280.f, 720.f};
    ren.drawRect(screen, m_theme->background.withAlpha(0.98f * a));

    // Sparse guide grid makes edge and corner coverage easy to inspect.
    const nxui::Color guide = m_theme->panelBorder.withAlpha(0.18f * a);
    for (int x = 160; x < 1280; x += 160)
        ren.drawLine({(float)x, 0.f}, {(float)x, 720.f}, guide, 1.f);
    for (int y = 120; y < 720; y += 120)
        ren.drawLine({0.f, (float)y}, {1280.f, (float)y}, guide, 1.f);

    drawTouchTrace(ren, screen, a, 6.f);
    nxui::Rect hint{24.f, 20.f, 700.f, 48.f};
    ren.drawRoundedRect(hint, m_theme->panelBase.withAlpha(0.95f * a), 18.f);
    ren.drawText(nxui::I18n::instance().tr(
                     "controller_test.touch_fullscreen_hint",
                     "Touch test · X: return · Hold B 1 s or HOME: close"),
                 {hint.x + 18.f, hint.y + 12.f}, m_smallFont,
                 m_theme->textPrimary.withAlpha(a), 0.74f);
}

void ControllerTestScreen::render(nxui::Renderer& ren) {
    if (!isActive() || !m_theme || !m_font || !m_smallFont)
        return;
    const float alpha = m_alpha.value();
    const float scale = m_scale.value();
    nxui::Rect p = panelRect(scale);
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color::black().withAlpha(0.12f * alpha));
    ren.drawFrostedInset(p, m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f),
                        m_theme->panelBorder.withAlpha(0.32f), m_theme->panelHighlight.withAlpha(0.10f), 26.f, alpha);
    if (m_mode == Mode::Manager) {
        ren.drawText(nxui::I18n::instance().tr("controllers.title", "Controllers"),
                     {p.x + 30.f, p.y + 24.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 1.f);
        drawManager(ren, p, alpha);
        return;
    }
    ren.drawText(nxui::I18n::instance().tr("controller_test.title", "Controller Test"),
                 {p.x + 30.f, p.y + 24.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 1.f);
    const std::string closeHint = nxui::I18n::instance().tr(
        "controller_test.close_hint", "Hold B 1 s or press HOME to close");
    nxui::Vec2 closeSize = m_smallFont->measure(closeHint);
    ren.drawText(closeHint,
                 {p.right() - 30.f - closeSize.x * 0.68f, p.y + 30.f},
                 m_smallFont, m_theme->textSecondary.withAlpha(alpha), 0.68f);
    nxui::Rect stick{p.x + 30.f * scale, p.y + 82.f * scale,
                     456.f * scale, 542.f * scale};
    nxui::Rect buttons{p.x + 516.f * scale, p.y + 82.f * scale,
                       668.f * scale, 254.f * scale};
    nxui::Rect touch{p.x + 516.f * scale, p.y + 360.f * scale,
                     668.f * scale, 264.f * scale};
    drawStickPanel(ren, stick, alpha);
    drawButtonsPanel(ren, buttons, alpha);
    drawTouchPanel(ren, touch, alpha);
    drawFullscreenTouch(ren, alpha);
}
