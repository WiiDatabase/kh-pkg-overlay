#include "hooks.hpp"
#include "lib.hpp"
#include "lock.hpp"
#include "nn/fs.hpp"

#include <cstring>

namespace nn::fs {
    Result ReadFile(FileHandle handle, long position, void *buffer, ulong size, const ReadOption &option);
    Result ReadFile(ulong *bytes_read, FileHandle handle, long position, void *buffer, ulong size);
    Result ReadFile(ulong *bytes_read, FileHandle handle, long position, void *buffer, ulong size,
                    const ReadOption &option);
} // namespace nn::fs

namespace {

    constexpr const char DeltaSuffix[] = ".delta";
    constexpr size_t MaxPathLength     = 0x300;
    constexpr size_t MaxOverlays       = 16;

    struct Overlay {
        u64 pkg;
        nn::fs::FileHandle delta;
        long base_size;
        long delta_size;
    };

    Overlay g_overlays[MaxOverlays];
    SpinLock g_lock;

    bool FindOverlay(nn::fs::FileHandle handle, Overlay *out) {
        LockGuard lock(g_lock);
        for (auto &o : g_overlays) {
            if (o.pkg != 0 && o.pkg == handle._internal) {
                *out = o;
                return true;
            }
        }
        return false;
    }

    bool EndsWith(const char *s, const char *suffix) {
        size_t n = std::strlen(s), m = std::strlen(suffix);
        return n >= m && std::memcmp(s + n - m, suffix, m) == 0;
    }

} // namespace

HOOK_DEFINE_TRAMPOLINE(OpenFileHook){
    static Result Callback(nn::fs::FileHandle *out, const char *path, int mode) {
        Result rc = Orig(out, path, mode);
        if (rc != 0 || !EndsWith(path, ".pkg")) {
            return rc;
        }
        size_t len = std::strlen(path);
        if (len + sizeof(DeltaSuffix) > MaxPathLength) {
            return rc;
        }
        char delta_path[MaxPathLength];
        std::memcpy(delta_path, path, len);
        std::memcpy(delta_path + len, DeltaSuffix, sizeof(DeltaSuffix));

        nn::fs::FileHandle delta;
        if (Orig(&delta, delta_path, mode) != 0) {
            return rc;
        }
        long base_size = 0, delta_size = 0;
        if (nn::fs::GetFileSize(&base_size, *out) != 0 || nn::fs::GetFileSize(&delta_size, delta) != 0) {
            nn::fs::CloseFile(delta);
            return rc;
        }

        LockGuard lock(g_lock);
        for (auto &o : g_overlays) {
            if (o.pkg == 0) {
                o = {out->_internal, delta, base_size, delta_size};
                return rc;
            }
        }
        nn::fs::CloseFile(delta);
        return rc;
    }
};

HOOK_DEFINE_TRAMPOLINE(CloseFileHook){
    static void Callback(nn::fs::FileHandle handle) {
        nn::fs::FileHandle delta{};
        bool had_delta = false;
        {
            LockGuard lock(g_lock);
            for (auto &o : g_overlays) {
                if (o.pkg != 0 && o.pkg == handle._internal) {
                    delta     = o.delta;
                    had_delta = true;
                    o         = {};
                    break;
                }
            }
        }
        if (had_delta) {
            Orig(delta);
        }
        Orig(handle);
    }
};

HOOK_DEFINE_TRAMPOLINE(GetFileSizeHook){
    static Result Callback(long *out, nn::fs::FileHandle handle) {
        Overlay o;
        if (FindOverlay(handle, &o)) {
            *out = o.base_size + o.delta_size;
            return 0;
        }
        return Orig(out, handle);
    }
};

template<typename BaseRead>
Result OverlayRead(const Overlay &o, long position, void *buffer, ulong size, ulong *bytes_read, BaseRead base_read) {
    ulong done = 0;
    auto *dst  = static_cast<u8 *>(buffer);
    if (position < o.base_size) {
        ulong head = static_cast<ulong>(o.base_size - position);
        if (head > size) {
            head = size;
        }
        Result rc = base_read(position, dst, head);
        if (rc != 0) {
            return rc;
        }
        done = head;
    }
    if (done < size) {
        long delta_pos = position + static_cast<long>(done) - o.base_size;
        long left      = o.delta_size - delta_pos;
        ulong tail     = size - done;
        if (left < static_cast<long>(tail)) {
            tail = left > 0 ? static_cast<ulong>(left) : 0;
        }
        if (tail > 0) {
            ulong got = 0;
            Result rc = nn::fs::ReadFile(&got, o.delta, delta_pos, dst + done, tail);
            if (rc != 0) {
                return rc;
            }
            done += got;
        }
    }
    if (bytes_read != nullptr) {
        *bytes_read = done;
    }
    return 0;
}

HOOK_DEFINE_TRAMPOLINE(ReadFileHook){
    static Result Callback(nn::fs::FileHandle handle, long position, void *buffer, ulong size) {
        Overlay o;
        if (!FindOverlay(handle, &o) || position + static_cast<long>(size) <= o.base_size) {
            return Orig(handle, position, buffer, size);
        }
        return OverlayRead(o, position, buffer, size, nullptr,
                           [&](long pos, void *dst, ulong n) { return Orig(handle, pos, dst, n); });
    }
};

HOOK_DEFINE_TRAMPOLINE(ReadFileOptionHook){
    static Result Callback(nn::fs::FileHandle handle, long position, void *buffer, ulong size,
                           const nn::fs::ReadOption &option) {
        Overlay o;
        if (!FindOverlay(handle, &o) || position + static_cast<long>(size) <= o.base_size) {
            return Orig(handle, position, buffer, size, option);
        }
        return OverlayRead(o, position, buffer, size, nullptr,
                           [&](long pos, void *dst, ulong n) { return Orig(handle, pos, dst, n, option); });
    }
};

HOOK_DEFINE_TRAMPOLINE(ReadFileSizeHook){
    static Result Callback(ulong *bytes_read, nn::fs::FileHandle handle, long position, void *buffer, ulong size) {
        Overlay o;
        if (!FindOverlay(handle, &o) || position + static_cast<long>(size) <= o.base_size) {
            return Orig(bytes_read, handle, position, buffer, size);
        }
        return OverlayRead(o, position, buffer, size, bytes_read, [&](long pos, void *dst, ulong n) {
            ulong got = 0;
            return Orig(&got, handle, pos, dst, n);
        });
    }
};

HOOK_DEFINE_TRAMPOLINE(ReadFileSizeOptionHook){
    static Result Callback(ulong *bytes_read, nn::fs::FileHandle handle, long position, void *buffer, ulong size,
                           const nn::fs::ReadOption &option) {
        Overlay o;
        if (!FindOverlay(handle, &o) || position + static_cast<long>(size) <= o.base_size) {
            return Orig(bytes_read, handle, position, buffer, size, option);
        }
        return OverlayRead(o, position, buffer, size, bytes_read, [&](long pos, void *dst, ulong n) {
            ulong got = 0;
            return Orig(&got, handle, pos, dst, n, option);
        });
    }
};

void InstallDeltaHooks() {
    using ReadPlain   = Result (*)(nn::fs::FileHandle, long, void *, ulong);
    using ReadOpt     = Result (*)(nn::fs::FileHandle, long, void *, ulong, const nn::fs::ReadOption &);
    using ReadSize    = Result (*)(ulong *, nn::fs::FileHandle, long, void *, ulong);
    using ReadSizeOpt = Result (*)(ulong *, nn::fs::FileHandle, long, void *, ulong, const nn::fs::ReadOption &);

    OpenFileHook::InstallAtFuncPtr(nn::fs::OpenFile);
    CloseFileHook::InstallAtFuncPtr(nn::fs::CloseFile);
    GetFileSizeHook::InstallAtFuncPtr(nn::fs::GetFileSize);
    ReadFileHook::InstallAtFuncPtr(static_cast<ReadPlain>(nn::fs::ReadFile));
    ReadFileOptionHook::InstallAtFuncPtr(static_cast<ReadOpt>(nn::fs::ReadFile));
    ReadFileSizeHook::InstallAtFuncPtr(static_cast<ReadSize>(nn::fs::ReadFile));
    ReadFileSizeOptionHook::InstallAtFuncPtr(static_cast<ReadSizeOpt>(nn::fs::ReadFile));
}
