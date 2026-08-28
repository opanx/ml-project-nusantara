/*
 * External ELF binary entry point
 * Creates overlay window + EGL + ImGui (no Dobby hook needed)
 * Then runs IL2CPP dumper + menu from Android-LibTool-New
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// From Android-LibTool-New
#include "Menu/ImGui.h"
#include "Il2cpp/Il2cpp.h"
#include "Includes/Utils.h"
#include "Includes/Logger.h"
#include "imgui/imgui.h"

// ANativeWindowCreator
#include "Includes/ANativeWindowCreator.h"

// Forward declarations from existing code
extern void on_init();
extern void draw_thread();

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

    // Create overlay window
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

    // Set window for ImGui
    setNativeWindow(g_window);
    glViewport(0, 0, g_screenW, g_screenH);

    return true;
}

// Render loop - calls ImGui draw directly (no Dobby hook)
void* render_loop(void*) {
    printf("[+] Render loop starting...\n");

    while (g_running) {
        EGLint w, h;
        eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
        eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);

        if (w > 0 && h > 0) {
            internalDrawMenu(w, h);
        }

        eglSwapBuffers(g_display, g_surface);
        usleep(16000); // ~60fps
    }
    return nullptr;
}

// IL2CPP init thread (from on_init in Main.cpp)
void* init_thread(void*) {
    printf("[+] Waiting for %s...\n", targetLibName);
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    printf("[+] %s loaded!\n", targetLibName);

    on_init();
    printf("[+] IL2CPP initialized\n");
    return nullptr;
}

int main(int argc, char* argv[]) {
    printf("============================================\n");
    printf("  Android-LibTool-New External v4.0\n");
    printf("  By Panxcz & Freebuff\n");
    printf("============================================\n");
    printf("[+] PID: %d\n", getpid());
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

    // Step 2: Setup ImGui (context, font, style)
    printf("[+] Setup ImGui...\n");
    setupMenu();
    printf("[+] ImGui ready\n");

    // Step 3: Start IL2CPP init in background
    pthread_t init_tid;
    pthread_create(&init_tid, nullptr, init_thread, nullptr);

    // Step 4: Start render loop in background
    pthread_t render_tid;
    pthread_create(&render_tid, nullptr, render_loop, nullptr);

    // Step 5: Main loop - keep alive
    printf("[+] Running... Ctrl+C to exit\n");
    while (g_running) sleep(1);

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
