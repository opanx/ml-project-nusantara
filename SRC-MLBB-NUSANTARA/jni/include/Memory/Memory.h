#pragma once
// Made By @AKNoryx28

#include <unistd.h>
#include <string>
#include <vector>
#include <inttypes.h>
//#include "macros.h"

#ifdef __LP64__
typedef uint64_t APTR;
#else
typedef uint32_t APTR;
#endif

typedef int Prot;
enum Prot_ {
    Prot_NONE = 0,
    Prot_READ = 1 << 1,
    Prot_WRITE = 1 << 2,
    Prot_EXEC = 1 << 3,
    Prot_PRIV = 1 << 4,
};

inline Prot parsePerms(const char* perms_str) {
    Prot perms = Prot_NONE;
    if (perms_str[0] == 'r') {
        perms |= Prot_READ;
    }
    if (perms_str[1] == 'w') {
        perms |= Prot_WRITE;
    }
    if (perms_str[2] == 'x') {
        perms |= Prot_EXEC;
    }
    if (perms_str[3] == 'p') {
        perms |= Prot_PRIV;
    }
    return perms;
}

struct PMap {
    pid_t pid;
    unsigned long long startAddress;
    unsigned long long endAddress;
    size_t length;
    Prot perms;
    unsigned long long offset;
    std::string dev;
    unsigned long inode;
    std::string pathname;
};

namespace Memory {
    inline pid_t g_pid = -1; // default pid
    
    pid_t FindPid(const char* procName);
    
    uintptr_t GetBase(const char *libName, pid_t pid = g_pid);
    uintptr_t GetEnd(const char *libName, pid_t pid = g_pid);
    
    uintptr_t GetBaseInSplit(const char *splitName, const char *libName, pid_t pid = g_pid);
    uintptr_t GetEndInSplit(const char *splitName, const char *libName, pid_t pid = g_pid);
    
    std::vector<PMap> GetMaps(const char *name, pid_t pid = g_pid);
    
    int ProcessRead(uintptr_t address, void *out, size_t size, pid_t pid = g_pid);
    int ProcessRead(void *address, void *out, size_t size, pid_t pid = g_pid);
    int ProcessWrite(void *address, void *data, size_t size, pid_t pid = g_pid);
    
    // Process IO R/W
    bool WriteIO(uintptr_t adresss, void *write_buffer, size_t size, pid_t pid = g_pid);
    bool ReadIO(uintptr_t adresss, void *read_buffer, size_t size, pid_t pid = g_pid);
    void CleanIO();
    
    uintptr_t FindPattern(uintptr_t start, size_t length, const unsigned char* pattern, const char* mask);
    
    std::string Exec(const char *cmd);
    bool IsInvalidAddress(uintptr_t address);
    bool IsInvalidAddress(void *address);
    
    template<typename T>
    inline T Read(uintptr_t addr);
    template<typename T>
    T Read(const std::vector<uintptr_t> &address);
    
    template<typename T>
    inline T *ReadArray(uintptr_t address, unsigned int size);
    inline uintptr_t ReadPtr(uintptr_t address);
    inline uintptr_t ReadPtr(const std::vector<uintptr_t> &address);
    
    template<typename T>
    inline bool Write(uintptr_t addr, T data);
}

template<typename T>
T Memory::Read(uintptr_t addr) {
    T data;
    int result = Memory::ProcessRead((void*)addr, (void*)&data, sizeof(T), Memory::g_pid);
    if (result != 0) {
        //LOGE("Write at %" PRIXPTR " s:%zu", addr, sizeof(T));
        return {};
    }
    return data;
}

template<typename T>
T Memory::Read(const std::vector<uintptr_t> &address) {
    uintptr_t tmpAddress = 0;
    T result{};
    for (size_t i = 0; i < address.size(); ++i) {
        auto adr = address[i];
        int rval = -1;
        if (i == address.size() - 1) {
            rval = ProcessRead((void*)(tmpAddress + adr), &result, sizeof(T));
        } else {
            rval = ProcessRead((void*)(tmpAddress + adr), &tmpAddress, sizeof(uintptr_t));
        }
        if (rval != 0) return {};
    }
    return result;
}

template<typename T>
bool Memory::Write(uintptr_t addr, T data) {
    int result = Memory::ProcessWrite((void*)addr, (void*)&data, sizeof(T), Memory::g_pid);
    if (result != 0) {
        //LOGE("Write at %" PRIXPTR " s:%zu", addr, sizeof(T));
    }
    return result == 0;
}

template<typename T>
T *Memory::ReadArray(uintptr_t address, unsigned int size) {
    T data[size];
    T *ptr = data;
    Memory::ProcessRead((void*)address, ptr, sizeof(T) * size, Memory::g_pid);
    return ptr;
}

uintptr_t Memory::ReadPtr(uintptr_t address) {
    uintptr_t data;
    int result = Memory::ProcessRead((void*)address, (void*)&data, sizeof(uintptr_t), Memory::g_pid);
    if (result != 0) {
        //LOGE("Write at %" PRIXPTR " s:%zu", addr, sizeof(T));
        return 0;
    }
    return data;
}

uintptr_t Memory::ReadPtr(const std::vector<uintptr_t> &address)
{
    uintptr_t tmpAddress = 0;
    for (const auto adr : address) {
        if (Memory::ProcessRead(tmpAddress + adr, &tmpAddress, sizeof(uintptr_t)) != 0)
            return {};
    }
    return tmpAddress;
}