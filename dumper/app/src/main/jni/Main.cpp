#include <jni.h>     // for JNI_ERR, JNIEnv, jclass, JNINativeM...
#include <pthread.h> // for pthread_create
#include <thread>
#include <unistd.h>              // for sleep
#include "Il2cpp/Il2cpp.h"       // for EnsureAttached, Init
#include "Il2cpp/il2cpp-class.h" // for Il2CppImage, Il2CppObject
#include "Includes/Logger.h"     // for LOGD, LOGI
#include "Includes/Utils.h"      // for isGameLibLoaded, isLibraryLoaded
#include "Includes/obfuscate.h"  // for make_obfuscator, OBFUSCATE
#include "Menu/ImGui.h"
#include "Tool/Keyboard.h"
#include "Tool/Tool.h"
#include "Tool/Util.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "sstream"
#include <android/native_window_jni.h>
#include "Tool/Unity.h"

void logcatJson(nlohmann::ordered_json &json)
{
    auto str = json.dump(4, '#');
    std::istringstream iss(str);
    std::string line;
    while (std::getline(iss, line))
    {
        usleep(100);
        LOGD("%s", line.c_str());
    }
}

template <typename T>
void ConfigSet(const char *key, T value);

Il2CppImage *g_Image = nullptr;
std::vector<MethodInfo *> g_Methods;
extern std::unordered_map<void *, HookerData> hookerMap;
extern int maxLine;

extern ImVec2 initialScreenSize;

// config
bool collapsed = false;
bool fullScreen = false;
bool resetWindow = false;
int selectedScale = 3;
int selectedTheme = 0;

bool doChangeScale = false;
bool doChangeTheme = false;


constexpr std::array<const char *, 7> possibleScale = {
    "Smallest", "Smaller", "Small", "Default", "Large", "Larger", "Largest",
};
constexpr std::array<float, 7> scaleFactors = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};

constexpr std::array<const char *, 3> possibleThemes = {
    "Dark", "Light", "Classic",
};

ImGuiStyle initialStyle;

const char *title = OBFUSCATE("IL2cpp Tool By Your Name"); // Add your name here .


void draw_thread()
{
    static ImVec2 lastSize = ImVec2(0, 0);
    static ImVec2 lastPos = ImVec2(0, 0);

    if (resetWindow)
    {
        resetWindow = false;
        if (fullScreen)
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            auto screenSize = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowSize(screenSize);
        }
        else
        {
            ImGui::SetNextWindowPos(lastPos);
            ImGui::SetNextWindowSize(lastSize);
        }
    }
    if (fullScreen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetFrameHeight()));
    }
    int i = 0;
    auto drawList = ImGui::GetBackgroundDrawList();

    for (auto &v : HookerData::visited)
    {
        if (v.name.empty())
            continue;
        char label[256]{0};
        sprintf(label, "%s", v.name.c_str());
        if (v.hitCount > 0)
        {
            sprintf(label, "%s (%dx)", label, v.hitCount);
        }
        auto labelSize = ImGui::CalcTextSize(label);
        ImVec2 labellPos{20, 150 + (labelSize.y * i)};

        auto dt = ImGui::GetIO().DeltaTime;
        constexpr ImVec4 GREEN = {0.f, 1.f, 0.f, 1.f};
        ImColor color = ImColor(1.f, 1.f, 1.f, 1.f);
        if (v.time > 0.f)
        {
            v.time -= dt;
            auto t = v.time;
            color = ImColor(ImLerp(color.Value.x, GREEN.x, t), ImLerp(color.Value.y, GREEN.y, t),
                            ImLerp(color.Value.z, GREEN.z, t), 1.f);
        }
        v.goneTime -= dt;
        if (v.goneTime > 0.f && v.goneTime <= 1.f)
        {
            auto t = v.goneTime;
            color.Value.w = ImLerp(0.f, color.Value.w, t);
        }
        if (v.goneTime <= 0.f)
        {
            v.name = "";
        }

        drawList->AddRectFilled(labellPos, {labellPos.x + labelSize.x, labellPos.y + labelSize.y},
                                IM_COL32(0, 0, 0, 100));
        drawList->AddText(labellPos, color, label);
        i++;
    }
    collapsed = !ImGui::Begin(title, nullptr, (fullScreen ? ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove : 0));
    if (fullScreen)
    {
        ImGui::PopStyleVar();
    }

#ifdef __DEBUG__
    static bool showDemoWindow = false;
#endif
    Keyboard::Update();
    static bool changeToToolsTab = false;
    if (ImGui::BeginTabBar("mainTabber"))
    {
        if (ImGui::BeginTabItem("Tools", nullptr, changeToToolsTab ? ImGuiTabItemFlags_SetSelected : 0))
        {
            changeToToolsTab = false;
            if (ImGui::Checkbox("Fullscreen", &fullScreen))
            {
                if (fullScreen)
                {
                    lastSize = ImGui::GetWindowSize();
                    lastPos = ImGui::GetWindowPos();
                }
                resetWindow = true;
            }
            Tool::Draw();
            ImGui::EndTabItem();
        }

        if (!hookerMap.empty())
        {
            if (ImGui::BeginTabItem("Tracer"))
            {
                ImGui::Text("Traced method count : %zu", hookerMap.size());
                std::vector<HookerData *> sortedHooker;
                for (auto &[name, data] : hookerMap)
                {
                    if (data.hitCount > 0)
                        sortedHooker.push_back(&data);
                }
                if (!sortedHooker.empty())
                {
                    if (ImGui::Button("Quick Restore"))
                    {
                        ImGui::OpenPopup("QuickRestorePopup");
                    }
                    std::sort(sortedHooker.begin(), sortedHooker.end(),
                              [](const HookerData *a, const HookerData *b) { return a->hitCount > b->hitCount; });
                    ImGui::BeginChild("TracerList", ImVec2(0, 0), ImGuiChildFlags_None,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    auto &tab = Tool::GetFirstTab();
                    for (auto v : sortedHooker)
                    {
                        char label[256]{0};
                        sprintf(label, "%s::%s (%dx)###%p", v->method->getClass()->getName(), v->method->getName(),
                                v->hitCount, v->method);
                        // if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                        // {
                        //     toBeErased = v;
                        // }

                        ImGui::PushID(v->method);
                        bool opened =
                            tab.MethodViewer(v->method->getClass(), v->method, tab.getCachedParams(v->method));
                        if (!opened && !changeToToolsTab && ImGui::IsItemHeld())
                        {
                            LOGD("IsItemHeld %s", v->method->getName());
                            changeToToolsTab = true;
                            Tool::OpenNewTabFromClass(v->method->getClass()).setOpenedTab = true;
                            ;
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    static auto &io = ImGui::GetIO();
                    ImGui::SetNextWindowSizeConstraints(ImVec2(io.DisplaySize.x / 1.2f, 0),
                                                        ImVec2(io.DisplaySize.x / 1.2f, io.DisplaySize.y / 2));
                    if (ImGui::BeginPopup("QuickRestorePopup",
                                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar))
                    {
                        HookerData *toBeErased = nullptr;
                        for (auto v : sortedHooker)
                        {
                            char label[256]{0};
                            sprintf(label, "%s::%s (%dx)###%p", v->method->getClass()->getName(), v->method->getName(),
                                    v->hitCount, v->method);
                            if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                            {
                                toBeErased = v;
                            }
                        }
                        if (toBeErased)
                        {
                            Tool::ToggleHooker(toBeErased->method, 0);
                        }
                        ImGui::EndPopup();
                    }
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                    ImGui::Text("No traced method has been called yet");
                    ImGui::PopStyleColor();
                }
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("Strings"))
        {
            Tool::Strings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dumper"))
        {
            Tool::Dumper();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings"))
        {
            ImGui::Separator();
            ImGui::Text("Target Library");
            {
                auto preview = gamePresets[selectedGamePreset].displayName;
                if (ImGui::BeginCombo("Game##target", preview))
                {
                    for (int i = 0; i < numGamePresets; i++)
                    {
                        bool selected = (i == selectedGamePreset);
                        if (ImGui::Selectable(gamePresets[i].displayName, selected))
                        {
                            selectedGamePreset = i;
                            if (strlen(gamePresets[i].libName) > 0) {
                                setTargetLibName(gamePresets[i].libName);
                            }
                            ConfigSet("selectedGamePreset", selectedGamePreset);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                // Custom input for last option
                if (selectedGamePreset == numGamePresets - 1) {
                    static char customLib[256] = "libil2cpp.so";
                    if (ImGui::InputText("Custom lib", customLib, sizeof(customLib))) {
                        setTargetLibName(customLib);
                    }
                    if (ImGui::Button("Apply Custom")) {
                        setTargetLibName(customLib);
                        ConfigSet("customLibName", std::string(customLib));
                    }
                }
                ImGui::Text("Current: %s", getTargetLibName());
            }
            ImGui::Separator();
            ImGui::Text("Display");
            // ImGui::SliderFloat("Scale##Global", &font->Scale, .1f, 2.0f, "%.2f");
            auto preview = possibleScale[selectedScale];
            if (ImGui::BeginCombo("Scale##Global", preview))
            {
                for (int i = 0; i < possibleScale.size(); i++)
                {
                    // bool selected = strcmp(possibleScale[i], preview) == 0;
                    bool selected = i == selectedScale;
                    if (ImGui::Selectable(possibleScale[i], selected))
                    {
                        selectedScale = i;
                        ConfigSet("selectedScale", selectedScale);
                        doChangeScale = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Enable if keyboard input is not being received", &Keyboard::check);
#ifdef __DEBUG__
            if (ImGui::Checkbox("Show Demo Window", &showDemoWindow))
            {
            }
#endif
            ImGui::Separator();
            ImGui::Text("Info");
            static auto packageName = Il2cpp::getPackageName();
            static auto unityVersion = Il2cpp::getUnityVersion();
            static auto gameVersion = Il2cpp::getGameVersion();
            ImGui::Text("Package: %s", packageName.c_str());
            ImGui::Text("Version: %s", gameVersion.c_str());
            ImGui::Text("Unity: %s", unityVersion.c_str());
#ifdef __aarch64__
            ImGui::Text("Arch: %s", "arm64-v8a");
#else
            ImGui::Text("Arch: %s", "armeabi-v7a");
#endif
            ImGui::Separator();

            // here you can add your telegram id/channel link
           
            if (ImGui::Button("Updates and stuff : https://t.me/CheatCodeRevo", ImVec2(-1, 0)))
            {
                auto Application = Il2cpp::FindClass("UnityEngine.Application");
                auto OpenURL = Application->getMethod("OpenURL", 1);
                if (OpenURL)
                {
                    OpenURL->invoke_static<void>(Il2cpp::NewString(OBFUSCATE("https://t.me/CheatCodeRevo")));
                }
                else
                {
                    Keyboard::Open(OBFUSCATE("https://t.me/CheatCodeRevo"), nullptr);
                }
            }
            ImGui::Separator();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    static bool doCalculate = false;
    if (doCalculate)
    {
        doCalculate = false;
        Tool::CalculateSomething();
    }

    if (doChangeTheme)
    {
        doChangeTheme = false;
        if (selectedTheme == 0)
            ImGui::StyleColorsDark();
        else if (selectedTheme == 1)
            ImGui::StyleColorsLight();
        else if (selectedTheme == 2)
            ImGui::StyleColorsClassic();

        ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);
        // Update initialStyle to keep new colors but we will need to re-apply scale
        initialStyle = ImGui::GetStyle();
        doChangeScale = true;
    }

    if (doChangeScale)
    {
        doChangeScale = false;
        static auto font = ImGui::GetFont();

        font->Scale = scaleFactors[selectedScale];
        auto style = initialStyle;
        style.ScaleAllSizes(font->Scale);
        ImGui::GetStyle() = style;
        doCalculate = true;
        if (fullScreen)
            resetWindow = true;
    }

#ifdef __DEBUG__
    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow();
    }
#endif
    ImGui::End();
    Tool::DrawNotifications();
}

static nlohmann::ordered_json gConf;

template <typename T>
void ConfigSet(const char *key, T value)
{
    LOGD(__FUNCTION__);
    gConf[key] = value;
    LOGD("ConfigWrite %s = %s", key, gConf[key].dump().c_str());
    Util::FileWriter fileWriter("tool_conf.json");
    fileWriter.write(gConf.dump(2).c_str());
}

template <typename T>
T ConfigGet(const char *key, T defaultValue)
{
    LOGD(__FUNCTION__);
    if (gConf.contains(key))
    {
        LOGD("ConfigGet %s = %s", key, gConf[key].dump().c_str());
        return gConf[key].get<T>();
    }
    else
    {
        ConfigSet(key, defaultValue);
    }
    return defaultValue;
}

void ConfigInit()
{
    Util::FileReader fileReader("tool_conf.json");
    if (fileReader.exists())
    {
        auto data = fileReader.read();
        try
        {
            gConf = nlohmann::json::parse(data);
            logcatJson(gConf);
        }
        catch (nlohmann::json::exception &e)
        {
            LOGE("ConfigInit error : %s", e.what());
            Util::FileWriter fileWriter("tool_conf.json");
            fileWriter.write("{}");
        }
    }
    else
    {
        Util::FileWriter fileWriter("tool_conf.json");
        fileWriter.write("{}");
    }
}

extern bool g_isExternalBinary;

void on_init()
{
    LOGD(__FUNCTION__);

    // Non-blocking wait: external binary may never have the target lib
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName))
    {
        if (g_isExternalBinary) {
            if (waitCount++ > 10) {
                LOGI("External mode: %s not found, continuing without IL2CPP", (const char *)targetLibName);
                break;
            }
        } else {
            waitCount = 0; // internal: wait forever
        }
        sleep(1);
    }

    bool libLoaded = isLibraryLoaded(targetLibName);
    if (libLoaded) {
        LOGI("%s has been loaded", (const char *)targetLibName);
    } else if (g_isExternalBinary) {
        LOGI("External mode: %s not in this process — ImGui menu only", (const char *)targetLibName);
    }

    if (libLoaded) {
        Il2cpp::Init();
        Il2cpp::EnsureAttached();

        Keyboard::Init();

        initialStyle = ImGui::GetStyle();
        ConfigInit();
        selectedScale = ConfigGet<int>("selectedScale", selectedScale);
        if (selectedScale < 0 || selectedScale >= scaleFactors.size())
        {
            selectedScale = 3;
            ConfigSet("selectedScale", selectedScale);
        }
        selectedTheme = ConfigGet<int>("selectedTheme", selectedTheme);
        if (selectedTheme < 0 || selectedTheme >= possibleThemes.size())
        {
            selectedTheme = 0;
            ConfigSet("selectedTheme", selectedTheme);
        }
        // Load target library preset from config
        selectedGamePreset = ConfigGet<int>("selectedGamePreset", 0);
        if (selectedGamePreset < 0 || selectedGamePreset >= numGamePresets)
        {
            selectedGamePreset = 0;
            ConfigSet("selectedGamePreset", 0);
        }
        if (strlen(gamePresets[selectedGamePreset].libName) > 0) {
            setTargetLibName(gamePresets[selectedGamePreset].libName);
        }
        doChangeTheme = true;
        doChangeScale = true;

        LOGD("HOOKING...");

#ifndef LIB_INPUT
        Unity::HookInput();
#endif

        g_Image = Il2cpp::GetAssembly("Assembly-CSharp")->getImage();
        auto images = Il2cpp::GetImages();
        Tool::Init(g_Image, images);

        for (auto image : images)
        {
            for (auto klass : image->getClasses())
            {
                for (auto m : klass->getMethods())
                {
                    if (!m->methodPointer)
                        continue;
                    g_Methods.emplace_back(m);
                }
            }
        }
    } else {
        LOGI("Skipping IL2CPP init — no target library in external mode");
    }
    LOGD("%zu methods", g_Methods.size());
    LOGD("SORTING");
    std::sort(g_Methods.begin(), g_Methods.end(),
              [](const auto &a, const auto &b) { return a->methodPointer < b->methodPointer; });
    LOGPTR(g_Methods.front()->methodPointer);
    LOGPTR(g_Methods.back()->methodPointer);
    LOGD("SORTED");
    LOGD("HOOKED!");
}

// we will run our hacks in a new thread so our while loop doesn't block process main thread
bool useJava = false;
void *hack_thread(void *)
{
    logger::Clear();

    LOGI("pthread created");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOGINT(useJava);
    if (!useJava)
        initModMenu((void *)draw_thread, (void *)on_init);
    return nullptr;
}

extern int glWidth;
extern int glHeight;
extern "C"
{
    JNIEXPORT void JNICALL Java_imgui_il2cpp_tool_NativeMethods_onDrawFrame(JNIEnv *env, jclass clazz)
    {
        internalDrawMenu(glWidth, glHeight);
    }

    JNIEXPORT void JNICALL Java_imgui_il2cpp_tool_NativeMethods_onSurfaceChanged(JNIEnv *env, jclass clazz, jint width,
                                                                                 jint height)
    {
        LOGD(__FUNCTION__);
        static bool once = true;
        if (once)
        {
            // once = false;
            glWidth = width;
            glHeight = height;
            initialScreenSize = ImVec2((float)width, (float)height);
            LOGD("glWidth = %d, glHeight = %d", glWidth, glHeight);
        }
        setupMenu();
    }

    JNIEXPORT void JNICALL Java_imgui_il2cpp_tool_NativeMethods_onSurfaceCreate(JNIEnv *env, jclass clazz)
    {
        LOGD(__FUNCTION__);
        initModMenu((void *)draw_thread, (void *)on_init, useJava);
    }

    JNIEXPORT void JNICALL Java_imgui_il2cpp_tool_NativeMethods_onTouchEvent(JNIEnv *env, jclass clazz, jint action,
                                                                              jfloat x, jint x_orig, jfloat y,
                                                                              jint y_orig)
    {
        handleInputEvent(action, x, y);
    }

    JNIEXPORT void JNICALL Java_imgui_il2cpp_tool_NativeMethods_onSurfaceCreated(JNIEnv *env, jclass clazz, jobject surface)
    {
        if (surface) {
            ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
            setNativeWindow(window);
        } else {
            setNativeWindow(nullptr);
        }
    }

    JNIEXPORT jstring JNICALL Java_imgui_il2cpp_tool_NativeMethods_getWindowRect(JNIEnv *env, jclass clazz)
    {
        return env->NewStringUTF(Tool::GetWindowRect().c_str());
    }
}
__attribute__((constructor)) void lib_main()
{
    // Create a new thread so it does not block the main thread, means the game would not freeze
    pthread_t ptid;
    pthread_create(&ptid, nullptr, hack_thread, nullptr);
}

JavaVM *g_vm = nullptr;
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, [[maybe_unused]] void *reserved)
{
    g_vm = vm;
    JNIEnv *env;
    vm->GetEnv((void **)&env, JNI_VERSION_1_6);
    if (env->FindClass("imgui/il2cpp/tool/NativeMethods") == nullptr)
    {
        LOGE("Could not find class imgui/il2cpp/tool/NativeMethods");
        env->ExceptionClear();
    }
    else
    {
        useJava = true;
    }

    return JNI_VERSION_1_6;
}
