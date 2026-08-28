#include "Utils.h"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "obfuscate.h"
#include "Logger.h"
#include "KittyMemory/MemoryPatch.h"

// === Runtime Configurable Target Lib ===
char g_targetLibName[256] = "libil2cpp.so";  // Universal default
int selectedGamePreset = 0;

GamePreset gamePresets[] = {
    {"MLBB (Mobile Legends)", "liblogic.so"},
    {"IL2CPP (Default)",      "libil2cpp.so"},
    {"Unity (Generic)",       "libunity.so"},
    {"Unreal Engine 4/5",     "libUE4.so"},
    {"Free Fire / MAX",       "libil2cpp.so"},
    {"PUBG Mobile",           "libtersafe.so"},
    {"Genshin Impact",        "libil2cpp.so"},
    {"COD Mobile",            "libil2cpp.so"},
    {"Brawl Stars",           "libgame.so"},
    {"Blood Strike",          "libil2cpp.so"},
    {"Standoff 2",            "lib standoff 2.so"},
    {"Custom (Manual)",       ""},
};
int numGamePresets = sizeof(gamePresets) / sizeof(gamePresets[0]);

void setTargetLibName(const char* name) {
    if (name && strlen(name) > 0 && strlen(name) < 255) {
        strncpy(g_targetLibName, name, 255);
        g_targetLibName[255] = '\0';
    }
}

const char* getTargetLibName() {
    return g_targetLibName;
}

DWORD libBase = 0;
bool libLoaded = false;
std::vector<MemoryPatch> memoryPatches;
std::vector<uint64_t> offsetVector;

// Patching a offset without switch.
void patchOffset(const char *fileName, uint64_t offset, std::string hexBytes, bool isOn) {

    MemoryPatch patch = MemoryPatch::createWithHex(fileName, offset, hexBytes);

    //Check if offset exists in the offsetVector
    if (std::find(offsetVector.begin(), offsetVector.end(), offset) != offsetVector.end()) {
        //LOGE(OBFUSCATE("Already exists"));
        std::vector<uint64_t>::iterator itr = std::find(offsetVector.begin(), offsetVector.end(), offset);
        patch = memoryPatches[std::distance(offsetVector.begin(), itr)]; //Get index of memoryPatches vector
    } else {
        memoryPatches.push_back(patch);
        offsetVector.push_back(offset);
        //LOGI(OBFUSCATE("Added"));
    }

    if (!patch.isValid()) {
        LOGE(OBFUSCATE("Failing offset: 0x%llu, please re-check the hex"), offset);
        return;
    }
    if (isOn) {
        if (!patch.Modify()) {
            LOGE(OBFUSCATE("Something went wrong while patching this offset: 0x%llu"), offset);
        }
    } else {
        if (!patch.Restore()) {
            LOGE(OBFUSCATE("Something went wrong while restoring this offset: 0x%llu"), offset);
        }
    }
}

void patchOffsetSym(uintptr_t absolute_address, std::string hexBytes, bool isOn) {

    MemoryPatch patch = MemoryPatch::createWithHex(absolute_address, hexBytes);

    //Check if offset exists in the offsetVector
    if (std::find(offsetVector.begin(), offsetVector.end(), absolute_address) != offsetVector.end()) {
        //LOGE(OBFUSCATE("Already exists"));
        std::vector<uint64_t>::iterator itr = std::find(offsetVector.begin(), offsetVector.end(), absolute_address);
        patch = memoryPatches[std::distance(offsetVector.begin(), itr)]; //Get index of memoryPatches vector
    } else {
        memoryPatches.push_back(patch);
        offsetVector.push_back(absolute_address);
        //LOGI(OBFUSCATE("Added"));
    }

    if (!patch.isValid()) {
        LOGE(OBFUSCATE("Failing offset: 0x%llu, please re-check the hex"), absolute_address);
        return;
    }
    if (isOn) {
        if (!patch.Modify()) {
            LOGE(OBFUSCATE("Something went wrong while patching this offset: 0x%llu"), absolute_address);
        }
    } else {
        if (!patch.Restore()) {
            LOGE(OBFUSCATE("Something went wrong while restoring this offset: 0x%llu"), absolute_address);
        }
    }
}

DWORD findLibrary(const char *library)
{
    char filename[0xFF] = {0}, buffer[1024] = {0};
    FILE *fp = NULL;
    DWORD address = 0;

    sprintf(filename, "%s", (char*)OBFUSCATE("/proc/self/maps"));

    fp = fopen(filename, OBFUSCATE("rt"));
    if (fp == NULL)
    {
        perror(OBFUSCATE("fopen"));
        goto done;
    }

    while (fgets(buffer, sizeof(buffer), fp))
    {
        if (strstr(buffer, library))
        {
            address = (DWORD)strtoul(buffer, NULL, 16);
            goto done;
        }
    }

done:

    if (fp)
    {
        fclose(fp);
    }

    return address;
}

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr)
{
    libBase = findLibrary(libraryName);
    if (libBase == 0)
        return 0;
    return (reinterpret_cast<DWORD>(libBase + relativeAddr));
}

jboolean isGameLibLoaded(JNIEnv *env, jobject thiz)
{
    return libLoaded;
}

bool isLibraryLoaded(const char *libraryName)
{
    // libLoaded = true;
    char line[512] = {0};
    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp != NULL)
    {
        while (fgets(line, sizeof(line), fp))
        {
            std::string a = line;
            if (strstr(line, libraryName))
            {
                libLoaded = true;
                return true;
            }
        }
        fclose(fp);
    }
    return false;
}

uintptr_t string2Offset(const char *c)
{
    int base = 16;
    // See if this function catches all possibilities.
    // If it doesn't, the function would have to be amended
    // whenever you add a combination of architecture and
    // compiler that is not yet addressed.
    static_assert(sizeof(uintptr_t) == sizeof(unsigned long) || sizeof(uintptr_t) == sizeof(unsigned long long),
                  "Please add string to handle conversion for this architecture.");

    // Now choose the correct function ...
    if (sizeof(uintptr_t) == sizeof(unsigned long))
    {
        return strtoul(c, nullptr, base);
    }

    // All other options exhausted, sizeof(uintptr_t) == sizeof(unsigned long long))
    return strtoull(c, nullptr, base);
}
