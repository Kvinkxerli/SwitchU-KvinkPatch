#pragma once

#include "../SettingsScreen.hpp"
#include <string>
#include <vector>

namespace settings::tabs {

class SystemTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);

private:
    static void setToggleByLabel(SettingsScreen& screen, const std::string& label, bool enabled);
    static void disableAllClockSync(SettingsScreen& screen);
    static bool anyClockSyncEnabled(const SettingsScreen& screen);
    static void openManualDateTimeEditor(SettingsScreen& screen);
    static void refreshTimezoneRowDescription(SettingsScreen& screen, const std::string& zoneName);
    static void beginTimezoneChange(SettingsScreen& screen,
                                    const std::string& filter = std::string());
};

class AccessibilityTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AudioTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class DisplayTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class InternetTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class SteamGridDbTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class ControllersTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class BluetoothTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class SleepTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class StorageTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AboutTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

}
