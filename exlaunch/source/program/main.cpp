#include "hooks.hpp"
#include "lib.hpp"

extern "C" void exl_main(void *x0, void *x1) {
    exl::hook::Initialize();
    InstallDeltaHooks();
    InstallLooseHooks();
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
