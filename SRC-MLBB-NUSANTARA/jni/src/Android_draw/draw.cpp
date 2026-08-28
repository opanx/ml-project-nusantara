#include "Android_draw/draw.h"

EGLDisplay display = EGL_NO_DISPLAY;
EGLConfig config;
EGLSurface surface = EGL_NO_SURFACE;
EGLContext context = EGL_NO_CONTEXT;

ANativeWindow *native_window;
ImFont* verdana;
int native_window_screen_x = 0;
int native_window_screen_y = 0;
android::ANativeWindowCreator::DisplayInfo displayInfo{0};
uint32_t orientation = 0;
bool g_Initialized = false;
ImGuiWindow *g_window = nullptr;

bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    orientation = displayInfo.orientation;
    #if defined(USE_OPENGL)
        if (!init_egl(_screen_x, _screen_y, log)) {
            return false;
        }
    #else
        InitVulkan();
        SetupVulkan();
        ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, true);
        SetupVulkanWindow(::native_window, (int) _screen_x, (int) _screen_y);
    #endif
    if (!ImGui_init()) {
        return false;
    }   
    #ifndef USE_OPENGL
        UploadFonts();
    #endif
    return true;
}

bool screenHide;
bool init_egl(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    FILE *fp;
    char buffer[1024];
    fp = popen("settings put system block_untrusted_touches 0", "r");
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
     
    }
    pclose(fp);
    system ("settings put global block_untrusted_touches 0 > /dev/null 2>&1");
    system ("settings put secure block_untrusted_touches 0 > /dev/null 2>&1");

    bool sgfop;
    string sfflp;
    cout << "[+] HideScreenRecorder? 1[YES] 2[NO]: ";
    cin >> sfflp;
    if (sfflp == "1")
        sgfop = true;
    else
        sgfop = false;

    if (sgfop) {
        printf("[+] HideScreenRecorder Enabled\n");
    } else {
        printf("[+] HideScreenRecorder Disabled\n");
    }

    // ALWAYS pass false — hidden window (true) causes SIGSEGV on EGL init
    // HideScreenRecorder flag saved for future use, but window must be visible
    ::native_window = android::ANativeWindowCreator::Create("Panxcz v0.1", _screen_x, _screen_y, false);
    if (!native_window) {
        printf("ANativeWindowCreator::Create failed\n");
        return false;
    }
    ANativeWindow_acquire(native_window);

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("eglGetDisplay error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglGetDisplay ok\n");

    if (eglInitialize(display, 0, 0) != EGL_TRUE) {
        printf("eglInitialize error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglInitialize ok\n");

    EGLint num_config = 0;
    const EGLint attribList[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_RED_SIZE, 5,
        EGL_BUFFER_SIZE, 32,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint attrib_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    if (log) printf("num_config = %d\n", num_config);
    if (eglChooseConfig(display, attribList, &config, 1, &num_config) != EGL_TRUE) {
        printf("eglChooseConfig error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglChooseConfig ok\n");

    EGLint egl_format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, egl_format);

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglCreateContext ok\n");

    surface = eglCreateWindowSurface(display, config, native_window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglCreateWindowSurface ok\n");

    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("eglMakeCurrent error=%u\n", glGetError());
        return false;
    }
    if (log) printf("eglMakeCurrent ok\n");
    if (log) printf("createNativeWindow ok\n");

    return true;
}

void screen_config() {
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

bool ImGui_init() {
    if (g_Initialized) {
        return true;
    }
    printf("[+] ImGui: Creating context...\n");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    printf("[+] ImGui: Init Android backend...\n");
    ImGui_ImplAndroid_Init(native_window);
    #if defined(USE_OPENGL)
        ImGui_ImplOpenGL3_Init("#version 300 es");
    #endif

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.IniSavingRate = -1.0f; // Disable ini saving

    // Load OPPOSans font (embedded in Font.h)
    printf("[+] ImGui: Loading font...\n");
    static ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false; // Static data, don't free
    font_cfg.SizePixels = 28.0f;
    io.Fonts->AddFontFromMemoryTTF((void *)OPPOSans_H, OPPOSans_H_size, 28.0f, &font_cfg);

    // === BLACK OBSIDIAN THEME ===
    ImGuiStyle &style = ImGui::GetStyle();

    // Background: deep black
    style.Colors[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    style.Colors[ImGuiCol_ChildBg]            = ImVec4(0.08f, 0.08f, 0.08f, 0.90f);
    style.Colors[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.08f, 0.96f);

    // Text: white/light gray
    style.Colors[ImGuiCol_Text]               = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Borders: dark subtle
    style.Colors[ImGuiCol_Border]             = ImVec4(0.15f, 0.15f, 0.15f, 0.60f);
    style.Colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame (input, checkbox, etc): dark gray
    style.Colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.12f, 0.12f, 0.94f);
    style.Colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.18f, 0.18f, 0.18f, 0.94f);
    style.Colors[ImGuiCol_FrameBgActive]       = ImVec4(0.22f, 0.22f, 0.22f, 0.94f);

    // Title bar
    style.Colors[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.08f, 0.08f, 0.08f, 0.75f);

    // Menu bar
    style.Colors[ImGuiCol_MenuBarBg]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // Scrollbar
    style.Colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.08f, 0.08f, 0.08f, 0.90f);
    style.Colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);

    // Header (collapsible, tree node): cyan accent
    style.Colors[ImGuiCol_Header]             = ImVec4(0.00f, 0.45f, 0.65f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered]       = ImVec4(0.00f, 0.55f, 0.75f, 0.85f);
    style.Colors[ImGuiCol_HeaderActive]        = ImVec4(0.00f, 0.65f, 0.85f, 1.00f);

    // Separator
    style.Colors[ImGuiCol_Separator]           = ImVec4(0.18f, 0.18f, 0.18f, 0.60f);
    style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.00f, 0.45f, 0.65f, 0.78f);
    style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.00f, 0.55f, 0.75f, 1.00f);

    // Resize grip
    style.Colors[ImGuiCol_ResizeGrip]          = ImVec4(0.00f, 0.45f, 0.65f, 0.25f);
    style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.00f, 0.55f, 0.75f, 0.67f);
    style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.00f, 0.65f, 0.85f, 0.95f);

    // Tab bar
    style.Colors[ImGuiCol_Tab]                = ImVec4(0.10f, 0.10f, 0.10f, 0.80f);
    style.Colors[ImGuiCol_TabHovered]          = ImVec4(0.00f, 0.45f, 0.65f, 0.80f);
    style.Colors[ImGuiCol_TabActive]           = ImVec4(0.00f, 0.35f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocused]        = ImVec4(0.10f, 0.10f, 0.10f, 0.80f);
    style.Colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    // Buttons: cyan accent
    style.Colors[ImGuiCol_Button]             = ImVec4(0.00f, 0.40f, 0.60f, 0.80f);
    style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.00f, 0.50f, 0.70f, 0.90f);
    style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.00f, 0.60f, 0.80f, 1.00f);

    // Checkmark & Slider: cyan
    style.Colors[ImGuiCol_CheckMark]          = ImVec4(0.00f, 0.70f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]          = ImVec4(0.00f, 0.50f, 0.75f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.00f, 0.65f, 0.90f, 1.00f);

    // Tooltip (use PopupBg for older ImGui versions)
    // style.Colors[ImGuiCol_TooltipBg] is not available in this ImGui version

    // Rounded corners
    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.PopupRounding = 8.0f;
    style.ChildRounding = 8.0f;

    // Spacing
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImGui::GetStyle().ScaleAllSizes(2.5f);

    printf("[+] ImGui: Init complete!\n");
    ::g_Initialized = true;
    return true;
}

void drawBegin() {
    screen_config();
    if (::orientation != displayInfo.orientation) {
        ::orientation = displayInfo.orientation;
        UpdateScreenData(displayInfo.width, displayInfo.height, displayInfo.orientation);
        if (g_window) {
            g_window->Pos.x = 100;
            g_window->Pos.y = 125;
        }
    }
    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_NewFrame();
    #else
        ImGui_ImplVulkan_NewFrame();
    #endif        
    ImGui_ImplAndroid_NewFrame(native_window_screen_x, native_window_screen_y);
    ImGui::NewFrame();
}

void drawEnd() {
    ImGui::Render();
    #ifdef USE_OPENGL
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        eglSwapBuffers(display, surface);
    #else
        FrameRender(ImGui::GetDrawData());
        FramePresent();
    #endif
}



void shutdown() {
    if (!g_Initialized) {
        return;
    }
    // Cleanup
    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_Shutdown();
    #else
        DeviceWait();
        ImGui_ImplVulkan_Shutdown();
    #endif
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    #ifdef USE_OPENGL
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) {
                eglDestroyContext(display, context);
            }
            if (surface != EGL_NO_SURFACE) {
                eglDestroySurface(display, surface);
            }
            eglTerminate(display);
        }
        display = EGL_NO_DISPLAY;
        context = EGL_NO_CONTEXT;
        surface = EGL_NO_SURFACE;
    #else    
        CleanupVulkanWindow();
        CleanupVulkan();
    #endif
    
    ANativeWindow_release(native_window);
    android::ANativeWindowCreator::Destroy(native_window);
    ::g_Initialized = false;
}
