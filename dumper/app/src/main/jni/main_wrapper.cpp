/*
 * External ELF binary entry point
 * Creates overlay window, then lets existing hack_thread() do its thing
 * (Dobby hooks eglSwapBuffers → ImGui renders on top)
 *
 * Flow:
 *   main() → createWindow() → hack_thread() → Dobby hooks eglSwapBuffers
 *   render loop → eglSwapBuffers() → Dobby hook fires → ImGui draws
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <dlfcn.h>

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// From Android-LibTool-New
#include "Menu/ImGui.h"
#include "Includes/Utils.h"
#include "Includes/Logger.h"

// ANativeWindowCreator for overlay
#include "Includes/ANativeWindowCreator.h"

// Forward: hack_thread from Main.cpp
extern void* hack_thread(void*);

static ANativeWindow* g_window = nullptr;
static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static volatile bool g_running = true;
static int g_screenW = 0, g_screenH = 0;

void sig_handler(int sig) { g_running = false; }

bool createOverlay() {
    // Get screen size
    FILE* fp = popen("wm size", "r");
    if (fp) {
        char buf[128];
        while (fgets(buf, sizeof(buf), fp)) {
            if (sscanf(buf, "Physical size: %dx%d", &g_screenW, &g_screenH) == 2) break;
        }
        pclose(fp);
    }
    if (g_screenW == 0) { g_screenW = 2400; g_screenH = 1080; }
    printf("[+] Screen: %dx%d\n", g_screenW, g_screenH);

    // Create overlay window (hide=true for security)
    printf("[+] Creating overlay...\n");
    g_window = android::ANativeWindowCreator::Create("Panxcz Tool", g_screenW, g_screenH, true);
    if (!g_window) {
        printf("[-] Create window failed!\n");
        return false;
    }
    ANativeWindow_acquire(g_window);
    printf("[+] Window: %p\n", g_window);

    // EGL init
    printf("[+] EGL init...\n");
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) return false;
    eglInitialize(g_display, 0, 0);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8, EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(g_display, attribs, &config, 1, &numConfigs);

    EGLint format;
    eglGetConfigAttrib(g_display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(g_window, 0, 0, format);

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, ctxAttribs);
    g_surface = eglCreateWindowSurface(g_display, config, g_window, nullptr);
    eglMakeCurrent(g_display, g_surface, g_surface, g_context);
    printf("[+] EGL ready\n");

    // Set native window for ImGui backend
    setNativeWindow(g_window);
    glViewport(0, 0, g_screenW, g_screenH);

    return true;
}

// Render loop: eglSwapBuffers triggers Dobby hook → ImGui draws
void* render_loop(void*) {
    printf("[+] Render loop started\n");
    while (g_running) {
        eglSwapBuffers(g_display, g_surface);
        usleep(16000); // ~60fps
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    printf("============================================\n");
    printf("  Android-LibTool-New External v4.0\n");
    printf("  By Panxcz & Freebuff\n");
    printf("============================================\n");
    printf("[+] PID: %d\n", getpid());
    // Auto-detect: try common game libraries
    const char* libs[] = {"liblogic.so", "libil2cpp.so", "libunity.so", "libUE4.so", "libtersafe.so", "libgame.so"};
    for (auto lib : libs) {
        void* h = dlopen(lib, RTLD_NOW);
        if (h) { setTargetLibName(lib); dlclose(h); break; }
    }
    printf("[+] Target: %s\n", targetLibName);
    printf("[+] Ctrl+C to exit\n\n");

    chmod(argv[0], 0777);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Step 1: Create overlay window + EGL
    if (!createOverlay()) {
        printf("[-] Overlay failed!\n");
        return 1;
    }

    // Step 2: Start hack_thread (hooks eglSwapBuffers via Dobby, inits IL2CPP)
    printf("[+] Starting hack_thread (Dobby hook + IL2CPP init)...\n");
    pthread_t hack_tid;
    pthread_create(&hack_tid, nullptr, hack_thread, nullptr);

    // Step 3: Render loop (eglSwapBuffers → Dobby hook fires → ImGui draws)
    printf("[+] Render loop (waiting for Dobby hook)...\n");
    render_loop(nullptr);

    // Cleanup
    printf("[+] Cleanup...\n");
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_display, g_surface);
    eglDestroyContext(g_display, g_context);
    eglTerminate(g_display);
    ANativeWindow_release(g_window);
    android::ANativeWindowCreator::Destroy(g_window);
    printf("[+] Done\n");
    return 0;
}
