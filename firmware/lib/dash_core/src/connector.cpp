#include "connector.h"

namespace dash {

const char* linkStateText(LinkState s) {
  switch (s) {
    case LinkState::Off: return "off";
    case LinkState::Waiting: return "waiting for Wi-Fi";
    case LinkState::Connecting: return "connecting";
    case LinkState::Up: return "connected";
    case LinkState::Error: return "problem";
  }
  return "?";
}

bool Registry::add(Connector* c) {
  if (!c || n_ >= kMax) return false;
  list_[n_] = c;
  last_[n_] = c->enabled ? c->state() : LinkState::Off;
  n_++;
  rev_++;
  return true;
}

void Registry::begin() {
  for (int i = 0; i < n_; i++)
    if (list_[i]->enabled) list_[i]->begin();
}

void Registry::loop(uint32_t now, bool netUp) {
  for (int i = 0; i < n_; i++) {
    Connector* c = list_[i];
    if (c->enabled) c->loop(now, netUp);
    LinkState s = c->enabled ? c->state() : LinkState::Off;
    if (s != last_[i]) {
      last_[i] = s;
      rev_++;
    }
  }
}

int Registry::problems() const {
  int p = 0;
  for (int i = 0; i < n_; i++)
    if (list_[i]->enabled && list_[i]->state() != LinkState::Up) p++;
  return p;
}

}  // namespace dash
