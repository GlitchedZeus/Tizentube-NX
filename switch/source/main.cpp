#include <switch.h>

#include <cstdio>

#include "tizentube_nx/core/url.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    consoleInit(nullptr);

    std::printf("TizenTube NX - foundation build\n\n");
    std::printf("No ads. No Shorts. Clean links.\n\n");

    const auto sample = ttnx::core::canonical_watch_url("IzJ7R4EnYmI");
    if (sample) {
        std::printf("Canonical URL:\n%s\n\n", sample->c_str());
    }

    std::printf("Press + to exit.\n");

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
