// Build-time settings. Real values live in include/secrets.h (git-ignored);
// without it the build uses the example so CI still compiles, and warns.
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found: using secrets.example.h. Copy it and fill in your Wi-Fi."
#include "secrets.example.h"
#endif
