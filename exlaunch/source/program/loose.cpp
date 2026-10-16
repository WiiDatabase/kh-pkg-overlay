#include "hooks.hpp"
#include "lib.hpp"
#include "nn/fs.hpp"

#include <cstring>

namespace {

    constexpr char OpenFilePattern[] =
            "ff8305d1 fd7b12a9 fc5f13a9 f65714a9 f44f15a9 fd830491 14600591 f50301aa "
            "f30300aa e00301aa e10314aa f70302aa ???????? ??????34 ??????b5 ????????";
    constexpr char ReadFilePattern[] =
            "ff4301d1 fd7b01a9 f71300f9 f65703a9 f44f04a9 fd430091 083041b9 f30300aa "
            "09068052 0a028052 001040b9 f40301aa 0829091b 690e40f9 35c1288b ????????";

    constexpr char GetRemasteredAssetPattern[] =
            "ff8301d1 fd7b01a9 fa6702a9 f85f03a9 f65704a9 f44f05a9 fd430091 08a040f9 "
            "1f0100f1 093041b9 2411427a ??????54 5a7c4093 f30301aa 49c7228b f50300aa";
    constexpr ptrdiff_t MemalignCallOffset = 0x5c;
    constexpr ptrdiff_t FreeCallOffset     = 0x270;

    constexpr ptrdiff_t PkgAssetSize       = 0x12c;
    constexpr ptrdiff_t PkgRemasteredCount = 0x130;
    constexpr ptrdiff_t PkgRemasteredTable = 0x140;
    constexpr ptrdiff_t PkgAssetPath       = 0x158;
    constexpr size_t RemasteredEntrySize   = 0x30;
    constexpr size_t RemasteredNameSize    = 0x20;
    constexpr char AssetPathMarker[]       = "ReSource/";
    constexpr char OriginalDir[]           = "original/";
    constexpr char RemasteredDir[]         = "remastered/";
    constexpr size_t MaxPath               = 0x300;

    using MemalignFn = void *(*) (size_t, size_t);
    using FreeFn     = void (*)(void *);
    MemalignFn g_GameMemalign;
    FreeFn g_GameFree;

    template<typename T>
    T &PkgField(void *pkg, ptrdiff_t offset) {
        return *reinterpret_cast<T *>(static_cast<u8 *>(pkg) + offset);
    }

    const char *AssetPath(void *pkg) {
        const char *rel = std::strstr(&PkgField<char>(pkg, PkgAssetPath), AssetPathMarker);
        return rel != nullptr ? rel + sizeof(AssetPathMarker) - 1 : nullptr;
    }

    const char *FindLooseOriginal(void *pkg, long *size) {
        const char *rel = AssetPath(pkg);
        char key[MaxPath];
        if (rel == nullptr || !JoinPath(key, sizeof(key), OriginalDir, rel)) {
            return nullptr;
        }
        return ModIndexFind(key, size);
    }

    const char *FindLooseRemastered(void *pkg, int index, long *size) {
        const u8 *table = PkgField<const u8 *>(pkg, PkgRemasteredTable);
        if (table == nullptr || index < 0 || index >= PkgField<s32>(pkg, PkgRemasteredCount)) {
            return nullptr;
        }
        const char *rel = AssetPath(pkg);
        if (rel == nullptr) {
            return nullptr;
        }
        char name[RemasteredNameSize + 1];
        std::memcpy(name, table + index * RemasteredEntrySize, RemasteredNameSize);
        name[RemasteredNameSize] = '\0';

        char key[MaxPath];
        size_t n = std::strlen(RemasteredDir) + std::strlen(rel);
        if (!JoinPath(key, sizeof(key), RemasteredDir, rel) || !JoinPath(key + n, sizeof(key) - n, "/", name)) {
            return nullptr;
        }
        return ModIndexFind(key, size);
    }

    uintptr_t BranchTarget(uintptr_t insn_addr) {
        u32 insn = *reinterpret_cast<const u32 *>(insn_addr);
        if ((insn & 0xfc000000) != 0x94000000) {
            return 0;
        }
        s64 imm = static_cast<s64>(insn & 0x03ffffff) << 38 >> 36;
        return insn_addr + imm;
    }

    s32 ReadLoose(const char *name, void *buffer, long size) {
        char path[MaxPath];
        if (!JoinPath(path, sizeof(path), ModRoot, name)) {
            return -2;
        }
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read) != 0) {
            return -2;
        }
        Result rc = nn::fs::ReadFile(handle, 0, buffer, static_cast<ulong>(size));
        nn::fs::CloseFile(handle);
        return rc == 0 ? static_cast<s32>(size) : -2;
    }

} // namespace

HOOK_DEFINE_TRAMPOLINE(PkgOpenFileHook){
    static bool Callback(void *pkg, const char *path, const char *alt_base) {
        if (!Orig(pkg, path, alt_base)) {
            return false;
        }
        long size;
        if (FindLooseOriginal(pkg, &size) != nullptr) {
            PkgField<s32>(pkg, PkgAssetSize) = static_cast<s32>(size);
        }
        return true;
    }
};

HOOK_DEFINE_TRAMPOLINE(PkgReadFileHook){
    static s32 Callback(void *pkg, void *buffer) {
        long size;
        const char *name = FindLooseOriginal(pkg, &size);
        if (name == nullptr) {
            return Orig(pkg, buffer);
        }
        return ReadLoose(name, buffer, size);
    }
};

HOOK_DEFINE_TRAMPOLINE(PkgGetRemasteredAssetHook){
    static void *Callback(void *pkg, u32 *size_out, int index) {
        long size;
        const char *name = FindLooseRemastered(pkg, index, &size);
        if (name == nullptr) {
            return Orig(pkg, size_out, index);
        }
        void *buffer = g_GameMemalign(0x10, (size + 0xff) & ~0xffl);
        if (buffer == nullptr) {
            return nullptr;
        }
        std::memset(buffer, 0, (size + 0xff) & ~0xffl);
        if (ReadLoose(name, buffer, size) < 0) {
            g_GameFree(buffer);
            return Orig(pkg, size_out, index);
        }
        *size_out = static_cast<u32>(size);
        return buffer;
    }
};

void InstallLooseHooks() {
    uintptr_t open_file = FindUniquePattern(OpenFilePattern);
    uintptr_t read_file = FindUniquePattern(ReadFilePattern);
    if (open_file != 0 && read_file != 0) {
        PkgOpenFileHook::InstallAtPtr(open_file);
        PkgReadFileHook::InstallAtPtr(read_file);
    }

    uintptr_t get_remastered = FindUniquePattern(GetRemasteredAssetPattern);
    if (get_remastered != 0) {
        g_GameMemalign = reinterpret_cast<MemalignFn>(BranchTarget(get_remastered + MemalignCallOffset));
        g_GameFree     = reinterpret_cast<FreeFn>(BranchTarget(get_remastered + FreeCallOffset));
        if (g_GameMemalign != nullptr && g_GameFree != nullptr) {
            PkgGetRemasteredAssetHook::InstallAtPtr(get_remastered);
        }
    }
}
