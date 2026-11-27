// ESP-NOW for mods: one receive queue shared by every mod. Frames are
// dash-mod JSON (docs/mods/README.md); main.cpp hands each one to the mod
// named by its "m" key. Works on the channel of the Wi-Fi network DASH is
// on (Atlas pins its truck network to channel 6).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace espnow {

// key: 16 characters, or empty for no encryption (then anyone in range can
// send). Safe to call again; it only starts once.
bool begin(const char* key);
bool up();

// Adds a peer by "AA:BB:CC:DD:EE:FF". Encrypted when begin() had a key.
// Once any peer is added, frames from anyone else are dropped.
bool addPeer(const char* mac);

// The next received frame, copied into buf (0-terminated). False if none.
// `from` gets the sender's MAC.
bool next(char* buf, size_t bufSize, size_t& len, uint8_t from[6]);

// Sends to one peer (added with addPeer) or, with mac null, to every peer.
bool send(const uint8_t* mac, const char* json, size_t len);

uint32_t received();  // frames since boot
uint32_t dropped();   // queue full or too big

bool parseMac(const char* text, uint8_t out[6]);

}  // namespace espnow
