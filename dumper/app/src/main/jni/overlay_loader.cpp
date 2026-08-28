/*
 * Panxcz Overlay v1.0
 *
 * Load into game process via:
 *   su -c "LD_PRELOAD=/data/local/tmp/libPanxczOverlay.so am start -n <game>/<activity>"
 *   OR inject with any injector tool
 *
 * Constructor runs on dlopen → hooks eglSwapBuffers → ImGui renders.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>

#include "Menu/ImGui.h"
#include "Includes/Utils.h"
#include "Includes/Logger.h"

extern void setupMenu();
extern void internalDrawMenu(int width, int height);
extern void handleInputEvent(int action, float x, float y);
extern void setNativeWindow(struct ANativeWindow* window);
extern void* hack_thread(void*);
extern int glWidth, glHeight;

// Thread that initializes ImGui after game loads
static void* overlay_thread(void* arg) {
    LOGI("[Panxcz] Overlay thread started, PID=%d", getpid());

    // Wait for libEGL.so
    int wait = 0;
    while (!isLibraryLoaded("libEGL.so")) {
        usleep(200000);
        if (++wait > 50) { // 10s
            LOGE("[Panxcz] libEGL.so not found after 10s");
            return nullptr;
        }
    }

    // Wait for game to finish loading
    usleep(2000000); // 2s

    // hack_thread → initModMenu → hooks eglSwapBuffers → ImGui renders
    hack_thread(nullptr);

    LOGI("[Panxcz] ImGui overlay active!");
    return nullptr;
}

// Runs when .so is loaded via LD_PRELOAD or dlopen
__attribute__((constructor))
void panxcz_init() {
    // Skip if loaded by linker during system boot
    if (getpid() < 1000) return;

    LOGI("[Panxcz] ========================================");
    LOGI("[Panxcz]  Panxcz Overlay v1.0 — Loaded!");
    LOGI("[Panxcz]  PID: %d", getpid());
    LOGI("[Panxcz] ========================================");

    // Start overlay thread (don't block game)
    pthread_t tid;
    pthread_create(&tid, nullptr, overlay_thread, nullptr);
    pthread_detach(tid);
}
