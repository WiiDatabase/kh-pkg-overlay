#include "hooks.hpp"
#include "lib.hpp"

namespace {

    constexpr size_t MaxPatternBytes = 128;

    int HexDigit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    size_t ParsePattern(const char *pattern, u8 *bytes, bool *wild) {
        size_t n = 0;
        for (const char *p = pattern; *p != '\0' && n < MaxPatternBytes;) {
            if (*p == ' ') {
                p++;
                continue;
            }
            if (p[0] == '?' && p[1] == '?') {
                wild[n]    = true;
                bytes[n++] = 0;
                p += 2;
                continue;
            }
            int hi = HexDigit(p[0]);
            int lo = p[1] != '\0' ? HexDigit(p[1]) : -1;
            if (hi < 0 || lo < 0) {
                return 0;
            }
            wild[n]    = false;
            bytes[n++] = static_cast<u8>(hi << 4 | lo);
            p += 2;
        }
        return n;
    }

} // namespace

uintptr_t FindUniquePattern(const char *pattern) {
    u8 bytes[MaxPatternBytes];
    bool wild[MaxPatternBytes];
    size_t len = ParsePattern(pattern, bytes, wild);
    if (len == 0) {
        return 0;
    }

    const auto &text  = exl::util::GetMainModuleInfo().m_Text;
    const auto *start = reinterpret_cast<const u8 *>(text.m_Start);
    uintptr_t found   = 0;
    for (size_t off = 0; off + len <= text.m_Size; off += 4) {
        bool match = true;
        for (size_t i = 0; i < len; i++) {
            if (!wild[i] && start[off + i] != bytes[i]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        if (found != 0) {
            return 0;
        }
        found = text.m_Start + off;
    }
    return found;
}
