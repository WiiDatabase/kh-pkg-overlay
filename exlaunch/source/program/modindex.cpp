#include "hooks.hpp"
#include "lib.hpp"
#include "lock.hpp"
#include "nn/fs.hpp"

#include <algorithm>
#include <cstring>

namespace {

    constexpr size_t MaxEntries   = 0x8000;
    constexpr size_t NamePoolSize = 0x100000;
    constexpr size_t DirPoolSize  = 0x20000;
    constexpr size_t MaxPath      = 0x300;

    struct Entry {
        u32 name;
        u32 size;
    };

    Entry g_Entries[MaxEntries];
    size_t g_EntryCount;
    char g_Names[NamePoolSize];
    size_t g_NamesUsed;
    char g_Dirs[DirPoolSize];
    nn::fs::DirectoryEntry g_DirEntry;

    SpinLock g_ScanLock;
    bool g_Ready;

    char Lower(char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    int CompareNoCase(const char *a, const char *b) {
        for (;; a++, b++) {
            char x = Lower(*a), y = Lower(*b);
            if (x != y || x == '\0') {
                return static_cast<unsigned char>(x) - static_cast<unsigned char>(y);
            }
        }
    }

    bool JoinChild(char *dst, size_t cap, const char *dir, const char *name) {
        if (*dir == '\0') {
            return JoinPath(dst, cap, "", name);
        }
        if (*name == '\0') {
            return JoinPath(dst, cap, dir, "");
        }
        size_t n = std::strlen(dir);
        return n + 1 < cap && JoinPath(dst, cap, dir, "/") && JoinPath(dst + n + 1, cap - n - 1, "", name);
    }

    const char *StoreString(char *pool, size_t pool_size, size_t *used, const char *prefix, const char *name) {
        char *dst = pool + *used;
        if (!JoinPath(dst, pool_size - *used, prefix, name)) {
            return nullptr;
        }
        *used += std::strlen(dst) + 1;
        return dst;
    }

    void Scan() {
        size_t dirs_used = 0;
        StoreString(g_Dirs, DirPoolSize, &dirs_used, "", "");
        char path[MaxPath];
        char rel[MaxPath];

        for (size_t next = 0; next < dirs_used;) {
            const char *dir = g_Dirs + next;
            next += std::strlen(dir) + 1;

            if (!JoinChild(path, sizeof(path), ModDir, dir)) {
                continue;
            }
            nn::fs::DirectoryHandle handle;
            if (nn::fs::OpenDirectory(&handle, path, nn::fs::OpenDirectoryMode_All) != 0) {
                continue;
            }
            for (;;) {
                s64 count = 0;
                if (nn::fs::ReadDirectory(&count, &g_DirEntry, handle, 1) != 0 || count != 1) {
                    break;
                }
                if (!JoinChild(rel, sizeof(rel), dir, g_DirEntry.m_Name)) {
                    continue;
                }
                if (g_DirEntry.m_Type == nn::fs::DirectoryEntryType_Directory) {
                    StoreString(g_Dirs, DirPoolSize, &dirs_used, "", rel);
                    continue;
                }
                if (g_EntryCount == MaxEntries || g_DirEntry.m_FileSize < 0 || g_DirEntry.m_FileSize > INT32_MAX) {
                    continue;
                }
                const char *name = StoreString(g_Names, NamePoolSize, &g_NamesUsed, "", rel);
                if (name == nullptr) {
                    continue;
                }
                g_Entries[g_EntryCount++] = {static_cast<u32>(name - g_Names), static_cast<u32>(g_DirEntry.m_FileSize)};
            }
            nn::fs::CloseDirectory(handle);
        }

        std::sort(g_Entries, g_Entries + g_EntryCount, [](const Entry &a, const Entry &b) {
            return CompareNoCase(g_Names + a.name, g_Names + b.name) < 0;
        });
    }

    void EnsureScanned() {
        if (__atomic_load_n(&g_Ready, __ATOMIC_ACQUIRE)) {
            return;
        }
        LockGuard lock(g_ScanLock);
        if (!g_Ready) {
            Scan();
            __atomic_store_n(&g_Ready, true, __ATOMIC_RELEASE);
        }
    }

} // namespace

const char *ModIndexFind(const char *key, long *size_out) {
    EnsureScanned();
    size_t lo = 0, hi = g_EntryCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c      = CompareNoCase(g_Names + g_Entries[mid].name, key);
        if (c == 0) {
            *size_out = g_Entries[mid].size;
            return g_Names + g_Entries[mid].name;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return nullptr;
}
