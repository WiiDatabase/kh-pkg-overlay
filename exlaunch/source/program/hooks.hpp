#pragma once

#include "common.hpp"

#include <cstring>

inline bool JoinPath(char *dst, size_t cap, const char *a, const char *b) {
    size_t n = std::strlen(a), m = std::strlen(b);
    if (n + m + 1 > cap) {
        return false;
    }
    std::memcpy(dst, a, n);
    std::memcpy(dst + n, b, m + 1);
    return true;
}

void InstallDeltaHooks();
void InstallLooseHooks();

uintptr_t FindUniquePattern(const char *pattern);

constexpr char ModDir[]  = "rom:/kh-mods";
constexpr char ModRoot[] = "rom:/kh-mods/";

const char *ModIndexFind(const char *key, long *size_out);
