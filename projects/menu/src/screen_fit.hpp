#pragma once
#include <cstdint>

namespace switchu::menu {

// Size SwitchU's managed display layer to the TV output and apply the console's
// "underscan" TV setting (0..100 — the value qlaunch's "Adjust Screen Size"
// writes). 100 = full screen; smaller values shrink and center the image with
// black borders, so the menu's corners fit inside a TV that overscans.
//
// initScreenFitLayer stores the layer created in __appInit and applies the
// current setting once. applyScreenUnderscan re-reads setsys and resizes the
// layer, so the settings UI gets an immediate visual result.

// Returns true when the layer was initialized successfully.
bool initScreenFitLayer(std::uint64_t managedLayerId);

// Re-reads tv.underscan and resizes the layer. Returns true on success, or when
// there is nothing to adjust (handheld mode / standalone homebrew / no layer).
bool applyScreenUnderscan();

} // namespace switchu::menu
