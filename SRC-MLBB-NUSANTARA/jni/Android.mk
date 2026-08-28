LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
#[[使用opengl绘制，否则使用vulkan绘制]] (1=opengl)(0=vulkan)
OPENGL_DRAW = 1

LOCAL_MODULE := rsa

LOCAL_CFLAGS := -std=c++17
LOCAL_CFLAGS += -fvisibility=hidden
LOCAL_CPPFLAGS := -std=c++17
LOCAL_CPPFLAGS += -fvisibility=hidden

LOCAL_CFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR
LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR


ifeq ($(OPENGL_DRAW), 1)
   LOCAL_CFLAGS += -DUSE_OPENGL
   LOCAL_CPPFLAGS += -DUSE_OPENGL
endif

LOCAL_CPPFLAGS += -w -s -Wno-error=format-security -fvisibility=hidden -Werror -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fpermissive -Wall -fexceptions
#引入头文件到全局#
#LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_draw/
#LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_touch/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_vulkan
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_C_INCLUDES +=$(LOCAL_PATH)/include/Memory


LOCAL_SRC_FILES := \
    src/main.cpp \
    src/Android_draw/draw.cpp \
    src/Android_touch/TouchHelperA.cpp \
    src/ImGui/imgui.cpp \
    src/ImGui/imgui_demo.cpp \
    src/ImGui/imgui_draw.cpp \
    src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_widgets.cpp \
    src/ImGui/stb_image.cpp \
    src/ImGui/backends/imgui_impl_android.cpp \
    src/oxorany.cpp \
    src/Memory/Memory.cpp \
    src/Memory/MemoryTools.cpp
    

ifeq ($(OPENGL_DRAW), 1)
    LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_opengl3.cpp
else
    LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_vulkan.cpp 
    LOCAL_SRC_FILES += src/Android_vulkan/vulkan_wrapper.cpp 
    LOCAL_SRC_FILES += src/Android_vulkan/VulkanUtils.cpp
endif


LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3

include $(BUILD_EXECUTABLE)
