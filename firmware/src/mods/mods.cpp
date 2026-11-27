#include "mods.h"

#include "espnow_bus.h"
#include "settings.h"

#ifndef DASH_MOD_ATLAS
#define DASH_MOD_ATLAS 1
#endif
#if DASH_MOD_ATLAS
#include "atlas_mod.h"
#endif

namespace mods {

namespace {
#if DASH_MOD_ATLAS
AtlasMod atlas;
#endif

void add(dash::ModList& list, dash::Registry& links, dash::Mod* m) {
  m->enabled = settings::linkEnabled(m->name(), true);
  for (int i = 0; i < m->settingCount(); i++) {
    dash::ModSetting* s = m->setting(i);
    s->set(settings::modValue(m->id(), s->key, s->value));
  }
  list.add(m);
  links.add(m);
}
}  // namespace

void addAll(dash::ModList& list, dash::Registry& links) {
#if DASH_MOD_ATLAS
  add(list, links, &atlas);
#endif
  // Next: Home Assistant, Overseer, Argos (docs/mods/README.md).
}

void settingChanged(dash::Mod* m, int i) {
  dash::ModSetting* s = m->setting(i);
  if (!s) return;
  settings::setModValue(m->id(), s->key, s->value);
  m->settingChanged(i);
}

void pump(dash::ModList& list, uint32_t now) {
  if (!espnow::up()) return;
  char buf[dash::kFrameMax + 1];
  size_t len;
  uint8_t from[6];
  // A few per pass, so a chatty sender can't hold up the screen.
  for (int i = 0; i < 4 && espnow::next(buf, sizeof buf, len, from); i++) list.route(buf, len, dash::Via::EspNow, now);
}

}  // namespace mods
