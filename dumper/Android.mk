LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := panxcz_dumper
LOCAL_SRC_FILES := dumper.cpp
LOCAL_LDLIBS := -llog -landroid
LOCAL_CFLAGS := -O2 -Wall -Werror
include $(BUILD_EXECUTABLE)
