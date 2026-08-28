/*
 * External ELF binary entry point
 * Creates overlay window + EGL, then starts hack_thread.
 * hack_thread hooks eglSwapBuffers via Dobby → ImGui renders.
 *
 * Key: on_init() is non-blocking (10s timeout) so ImGui appears immediately.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// From Android-LibTool-New
#include "Menu/ImGui.h"
#include "Includes/Utils.h"
#include "Includes/Logger.h"
#include "Dobby/include/dobby.h"

// ANativeWindowCreator
#include "Includes/ANativeWindowCreator.h"

// Forward declarations
extern void setupMenu();
extern void internalDrawMenu(int width, int height);
extern void handleInputEvent(int action, float x, float y);
extern void setNativeWindow(struct ANativeWindow* window);
extern void* hack_thread(void*);
extern int glWidth, glHeight;

// Flag to tell on_init() we're external (non-blocking wait)
bool g_isExternalBinary = true;

static ANativeWindow* g_window = nullptr;
static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static volatile bool g_running = true;
static int g_screenW = 0, g_screenH = 0;

void sig_handler(int sig) { g_running = false; }

// =================== Touch Input Hook ===================
typedef void (*orig_initMotionEvent)(void* thiz, void* motionEvent, const void* inputMessage);
static orig_initMotionEvent orig_InitializeMotionEvent = nullptr;

void my_InitializeMotionEvent(void* thiz, void* motionEvent, const void* inputMessage) {
    orig_InitializeMotionEvent(thiz, motionEvent, inputMessage);
    if (!motionEvent) return;
    // MotionEvent: offset 0x08=action(int), 0x14=x(float), 0x18=y(float)
    int32_t action = *(int32_t*)((char*)motionEvent + 0x08);
    float x = *(float*)((char*)motionEvent + 0x14);
    float y = *(float*)((char*)motionEvent + 0x18);
    handleInputEvent(action & 0xFF, x, y);
}

void hookTouchInput() {
    const char* symbols[] = {
        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE",
        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventERKNS_12InputMessageE",
        nullptr
    };
    for (int i = 0; symbols[i]; i++) {
        void* sym = DobbySymbolResolver("/system/lib64/libinput.so", symbols[i]);
        if (!sym) sym = DobbySymbolResolver("/system/lib/libinput.so", symbols[i]);
        if (sym) {
            DobbyHook(sym, (void*)my_InitializeMotionEvent, (void**)&orig_InitializeMotionEvent);
            printf("[+] Touch hook: OK\n");
            return;
        }
    }
    printf("[-] Touch hook: failed (no InputConsumer symbol)\n");
}

// =================== Overlay ===================
bool createOverlay() {
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

    printf("[+] Creating overlay window...\n");
    g_window = android::ANativeWindowCreator::Create("Panxcz Tool v1.0", g_screenW, g_screenH, true);
    if (!g_window) {
        printf("[-] ANativeWindowCreator::Create failed!\n");
        return false;
    }
    ANativeWindow_acquire(g_window);
    printf("[+] Window: %p\n", g_window);

    printf("[+] EGL init...\n");
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) { printf("[-] eglGetDisplay\n"); return false; }
    EGLint major, minor;
    if (!eglInitialize(g_display, &major, &minor)) { printf("[-] eglInitialize\n"); return false; }
    printf("[+] EGL %d.%d\n", major, minor);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8, EGL_NONE
    };
    EGLConfig config; EGLint numConfigs;
    if (!eglChooseConfig(g_display, attribs, &config, 1, &numConfigs) || !numConfigs) {
        printf("[-] eglChooseConfig\n"); return false;
    }
    EGLint format;
    eglGetConfigAttrib(g_display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(g_window, 0, 0, format);

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, ctxAttribs);
    g_surface = eglCreateWindowSurface(g_display, config, g_window, nullptr);
    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        printf("[-] eglMakeCurrent\n"); return false;
    }
    printf("[+] EGL ready\n");

    setNativeWindow(g_window);
    glViewport(0, 0, g_screenW, g_screenH);
    return true;
}

int main(int argc, char* argv[]) {
    printf("============================================\n");
    printf("  Panxcz Tool v1.0 - External ELF\n");
    printf("  By Panxcz & Freebuff\n");
    printf("============================================\n");
    printf("[+] PID: %d\n", getpid());

    // Auto-detect game library
    const char* libs[] = {"liblogic.so", "libil2cpp.so", "libunity.so", "libUE4.so", "libtersafe.so", "libgame.so"};
    for (auto lib : libs) {
        void* h = dlopen(lib, RTLD_NOW);
        if (h) { setTargetLibName(lib); printf("[+] Found: %s\n", lib); dlclose(h); break; }
    }
    printf("[+] Target: %s\n", targetLibName);
    chmod(argv[0], 0777);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Step 1: Create overlay
    if (!createOverlay()) { printf("[-] Overlay failed!\n"); return 1; }

    // Step 2: Hook touch
    hookTouchInput();

    // Step 3: Start hack_thread (hooks eglSwapBuffers via Dobby, inits IL2CPP non-blocking)
    printf("[+] Starting hack_thread...\n");
    pthread_t hack_tid;
    pthread_create(&hack_tid, nullptr, hack_thread, nullptr);

    // Step 4: Wait for Dobby hook to install
    usleep(500000); // 500ms

    // Step 5: Render loop — eglSwapBuffers triggers Dobby hook → ImGui renders
    printf("[+] Render loop started\n");
    printf("[+] ImGui menu should appear now\n\n");
    int frame = 0;
    while (g_running) {
        EGLint w, h;
        eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
        eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);

        // This call triggers Dobby hook → setupMenu() + internalDrawMenu()
        eglSwapBuffers(g_display, g_surface);

        if (++frame % 300 == 0) printf("[+] Frame %d\n", frame);
        usleep(16666); // ~60fps
    }

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
