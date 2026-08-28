/*
 * Panxcz Overlay v1.0 - Shared Library for Injection
 *
 * This .so gets injected into the game process via ptrace.
 * __attribute__((constructor)) runs when dlopen loads it.
 *
 * Flow:
 *   1. panxcz_tool injects this .so into game process
 *   2. Constructor runs, finds game's EGL context
 *   3. Hooks eglSwapBuffers via Dobby
 *   4. ImGui renders on top of the game
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
#include "Dobby/include/dobby.h"

extern void setupMenu();
extern void internalDrawMenu(int width, int height);
extern void handleInputEvent(int action, float x, float y);
extern void setNativeWindow(struct ANativeWindow* window);
extern void* hack_thread(void*);
extern int glWidth, glHeight;

static volatile bool g_overlay_active = false;

// Thread that waits for game EGL, then hooks eglSwapBuffers
static void* overlay_thread(void* arg) {
    LOGI("Overlay thread started");

    // Wait for libEGL.so to load
    int wait = 0;
    while (!isLibraryLoaded("libEGL.so")) {
        usleep(100000); // 100ms
        if (++wait > 100) { // 10s timeout
            LOGE("libEGL.so not found, giving up");
            return nullptr;
        }
    }
    LOGI("libEGL.so loaded");

    // Wait a bit more for game to initialize EGL
    usleep(1000000); // 1s

    // Now hook eglSwapBuffers via Dobby
    void* eglSwap = DobbySymbolResolver("libEGL.so", "eglSwapBuffers");
    if (!eglSwap) {
        LOGE("eglSwapBuffers not found");
        return nullptr;
    }
    LOGI("eglSwapBuffers @ %p", eglSwap);

    // The existing swapbuffers_hook in ImGui.cpp handles the rest
    // initModMenu hooks eglSwapBuffers → calls setupMenu() → ImGui renders

    // Start hack_thread which calls initModMenu
    hack_thread(nullptr);

    g_overlay_active = true;
    LOGI("Overlay active! ImGui should appear.");
    return nullptr;
}

// Constructor — runs when .so is loaded via dlopen
__attribute__((constructor))
void panxcz_overlay_init() {
    LOGI("========================================");
    LOGI("  Panxcz Overlay v1.0 - Loaded!");
    LOGI("  PID: %d", getpid());
    LOGI("========================================");

    // Mark as external
    extern bool g_isExternalBinary;
    g_isExternalBinary = true;

    // Start overlay in a new thread (don't block the game)
    pthread_t tid;
    pthread_create(&tid, nullptr, overlay_thread, nullptr);
    pthread_detach(tid);
}
