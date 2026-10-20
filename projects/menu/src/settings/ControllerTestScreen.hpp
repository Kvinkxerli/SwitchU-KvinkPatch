#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <switch.h>
#include <functional>
#include <string>
#include <vector>

class ControllerTestScreen final : public nxui::GlassWidget {
public:
    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;

    ControllerTestScreen();
    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setInput(nxui::Input* input) { m_input = input; }
    void onClosed(VoidCb cb) { m_closedCb = std::move(cb); }
    void onCloseSfx(VoidCb cb) { m_closeSfxCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }

    // Controller manager (SwitchU's own replacement for the system
    // "Controllers" applet): lists players, reorders them, switches grip.
    void showManager();
    void onSystemPairing(VoidCb cb) { m_pairingCb = std::move(cb); }
    void show();
    void hide();
    bool isActive() const { return m_active || m_animatingOut; }
    void handleTouch(nxui::Input& input);

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    enum class Mode { Test, Manager };
    static constexpr int kPlayerRows = 9; // Player 1-8 + handheld
    void updateManager(float dt);
    void pollBatteries();
    void drawBatteryIcon(nxui::Renderer& ren, nxui::Vec2 pos, const std::string& name,
                         s16 charge, bool charging, bool connected,
                         const nxui::Color& fg, const nxui::Color& dim, float alpha);
    void drawManager(nxui::Renderer& ren, const nxui::Rect& panel, float alpha);
    void setStatus(std::string text) { m_status = std::move(text); m_statusTime = 3.f; }

    enum class ActiveStick { None, Left, Right };
    struct ButtonVisual { nxui::Button button; const char* label; };

    nxui::Rect panelRect(float scale = 1.f) const;
    nxui::Rect touchPanelRect(float scale = 1.f) const;
    void setupActions();
    void drawStickPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawButtonsPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawTouchPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawFullscreenTouch(nxui::Renderer& ren, float alpha);
    void enterTouchFullscreen();
    void exitTouchFullscreen();
    void recordTouchPoint(float x, float y, const nxui::Rect& surface);
    void drawTouchTrace(nxui::Renderer& ren, const nxui::Rect& surface, float alpha, float thickness);
    std::string directionLabel(float x, float y) const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::Input* m_input = nullptr;
    bool m_active = false;
    bool m_animatingOut = false;
    nxui::AnimatedFloat m_alpha;
    nxui::AnimatedFloat m_scale;
    ActiveStick m_activeStick = ActiveStick::None;
    ActiveStick m_candidateStick = ActiveStick::None;
    float m_candidateTime = 0.f;
    float m_stickX = 0.f;
    float m_stickY = 0.f;
    bool m_recordingTouch = false;
    bool m_touchFullscreen = false;
    float m_bHoldTime = 0.f;
    nxui::AnimatedFloat m_touchFullscreenAnim;
    std::vector<nxui::Vec2> m_touchTrace;
    Mode m_mode = Mode::Test;
    int m_managerSel = 0;
    std::string m_status;
    float m_statusTime = 0.f;
    // Battery polling cache: hidd samples arrive at a few Hz, so re-query
    // the service at most twice per second and keep the last good values.
    struct DeviceBattery {
        s16 level = -1;    // hidd battery level, 0..4; -1 = unknown/no data
        bool charging = false;
        bool valid = false;
    };
    static constexpr int kMaxDevices = 12; // 10 npad ids + L/R split pair extras
    DeviceBattery m_battery[kMaxDevices];
    u8 m_batteryType[kMaxDevices] = {};
    float m_batteryTimer = 1.f; // poll immediately on first frame
    VoidCb m_pairingCb;
    VoidCb m_closedCb;
    VoidCb m_closeSfxCb;
    StringCb m_accessibilityCb;
};
