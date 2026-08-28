#ifndef UTILS
#define UTILS

#include <jni.h>
#include <string>
#include "obfuscate.h"

// === Runtime Configurable Target Lib ===
// Select from ImGui Settings tab or change via setTargetLibName()
struct GamePreset {
    const char* displayName;
    const char* libName;
};

extern GamePreset gamePresets[];
extern int numGamePresets;
extern int selectedGamePreset;
extern char g_targetLibName[256];

void setTargetLibName(const char* name);
const char* getTargetLibName();

// Legacy macro — now uses runtime global
#define targetLibName getTargetLibName()

//Ok now let's build it

typedef unsigned long DWORD;

DWORD findLibrary(const char *library);

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr);

jboolean isGameLibLoaded(JNIEnv *env, jobject thiz);

bool isLibraryLoaded(const char *libraryName);

uintptr_t string2Offset(const char *c);

void patchOffsetSym(uintptr_t absolute_address, std::string hexBytes, bool isOn);

void patchOffset(const char *fileName, uint64_t offset, std::string hexBytes, bool isOn);

namespace ToastLength
{
    inline const int LENGTH_LONG = 1;
    inline const int LENGTH_SHORT = 0;
} // namespace ToastLength

#endif
