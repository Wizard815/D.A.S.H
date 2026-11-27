// Every compiled-in mod, in one place. To add a mod: write it (see
// docs/mods/README.md), give it a DASH_MOD_<NAME> flag in platformio.ini,
// and add it in mods.cpp.
#pragma once
#include "connector.h"
#include "mod.h"

namespace mods {

// Adds the mods this build has, loads their switches and settings, and puts
// each in the links registry so it shows in Links.
void addAll(dash::ModList& list, dash::Registry& links);

// Saves a mod's setting after it changed on screen, and tells the mod.
void settingChanged(dash::Mod* m, int index);

// Hands frames waiting on ESP-NOW to the mods. Call every loop pass.
void pump(dash::ModList& list, uint32_t nowMs);

}  // namespace mods
