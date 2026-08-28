/*
 * Panxcz External IL2CPP Dumper
 * Reads game process memory via /proc/pid/mem
 * Parses IL2CPP metadata and generates dump.cs
 * No ImGui, no overlay - pure CLI dumper
 *
 * Usage: su -c ./panxcz_dumper [package_name]
 * Default: com.mobile.legends
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/syscall.h>

// ============================================================
// IL2CPP Metadata Constants
// ============================================================
#define METADATA_MAGIC 0xFAB11BAF

// Metadata header offsets
struct Il2CppGlobalMetadataHeader {
    uint32_t magic;
    uint32_t version;
    int32_t stringLiteralOffset;
    int32_t stringLiteralCount;
    int32_t stringLiteralDataOffset;
    int32_t stringLiteralDataCount;
    int32_t stringOffset;
    int32_t stringCount;
    int32_t eventsOffset;
    int32_t eventsCount;
    int32_t propertiesOffset;
    int32_t propertiesCount;
    int32_t methodsOffset;
    int32_t methodsCount;
    int32_t parameterDefaultValuesOffset;
    int32_t parameterDefaultValuesCount;
    int32_t fieldDefaultValuesOffset;
    int32_t fieldDefaultValuesCount;
    int32_t fieldAndParameterDefaultValueDataOffset;
    int32_t fieldAndParameterDefaultValueDataCount;
    int32_t fieldMarshaledSizesOffset;
    int32_t fieldMarshaledSizesCount;
    int32_t parametersOffset;
    int32_t parametersCount;
    int32_t fieldsOffset;
    int32_t fieldsCount;
    int32_t genericParametersOffset;
    int32_t genericParametersCount;
    int32_t genericParameterConstraintsOffset;
    int32_t genericParameterConstraintsCount;
    int32_t genericContainersOffset;
    int32_t genericContainersCount;
    int32_t nestedTypesOffset;
    int32_t nestedTypesCount;
    int32_t interfacesOffset;
    int32_t interfacesCount;
    int32_t vtableMethodsOffset;
    int32_t vtableMethodsCount;
    int32_t interfaceOffsetsOffset;
    int32_t interfaceOffsetsCount;
    int32_t typeDefinitionsOffset;
    int32_t typeDefinitionsCount;
    // Extended header for version >= 19
    int32_t RGCTXEntriesOffset;
    int32_t RGCTXEntriesCount;
    int32_t imagesOffset;
    int32_t imagesCount;
    int32_t assembliesOffset;
    int32_t assembliesCount;
};

// Simplified - just need first few fields for magic check
struct MetadataHeader {
    uint32_t magic;
    uint32_t version;
};

// ============================================================
// Process Memory Reader
// ============================================================
static pid_t g_pid = -1;
static int g_mem_fd = -1;

pid_t find_process(const char* name) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        
        int id = atoi(entry->d_name);
        if (id <= 0) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", id);
        
        FILE* fp = fopen(path, "r");
        if (!fp) continue;
        
        char cmdline[256] = {0};
        fgets(cmdline, sizeof(cmdline), fp);
        fclose(fp);
        
        if (strstr(cmdline, name)) {
            closedir(dir);
            return id;
        }
    }
    closedir(dir);
    return -1;
}

bool read_memory(uintptr_t addr, void* buf, size_t size) {
    if (g_mem_fd < 0) return false;
    ssize_t r = pread(g_mem_fd, buf, size, addr);
    return r == (ssize_t)size;
}

// ============================================================
// Memory Map Parser
// ============================================================
struct MemRegion {
    uintptr_t start;
    uintptr_t end;
    char name[256];
};

std::vector<MemRegion> get_maps(const char* filter) {
    std::vector<MemRegion> regions;
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/maps", g_pid);
    
    FILE* fp = fopen(path, "r");
    if (!fp) return regions;
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start, end;
        char perms[8], name[256] = {0};
        
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) < 3) continue;
        if (perms[0] != 'r') continue; // must be readable
        
        // Get name (last field)
        char* last = strrchr(line, '/');
        if (last) {
            char* space = strrchr(line, ' ');
            if (space) strncpy(name, space + 1, sizeof(name) - 1);
        }
        // Also try to get full path
        char* p = strchr(line, '/');
        if (p) {
            char* nl = strchr(p, '\n');
            if (nl) *nl = 0;
            strncpy(name, p, sizeof(name) - 1);
        }
        
        if (filter && !strstr(name, filter)) continue;
        
        MemRegion r;
        r.start = start;
        r.end = end;
        strncpy(r.name, name, sizeof(r.name) - 1);
        regions.push_back(r);
    }
    fclose(fp);
    return regions;
}

// ============================================================
// IL2CPP Metadata Scanner
// ============================================================
#define SCAN_BLOCK_SIZE (1024 * 1024) // 1MB blocks

bool find_metadata_in_region(uintptr_t start, uintptr_t end, uintptr_t* out_addr) {
    uint8_t buf[4096];
    uintptr_t magic_be = 0xAF1BF1FA; // 0xFAB11BAF big-endian
    uint32_t magic_le = METADATA_MAGIC;
    
    for (uintptr_t addr = start; addr < end - sizeof(uint32_t); addr += sizeof(buf) - 8) {
        size_t to_read = sizeof(buf);
        if (addr + to_read > end) to_read = end - addr;
        
        if (!read_memory(addr, buf, to_read)) {
            addr += to_read;
            continue;
        }
        
        // Search for magic in both endiannesses
        for (size_t i = 0; i < to_read - 4; i++) {
            uint32_t val = *(uint32_t*)(buf + i);
            if (val == magic_le || val == magic_be) {
                // Verify it's a valid metadata by checking version (16-29)
                uint32_t version = 0;
                if (read_memory(addr + i + 4, &version, 4)) {
                    if (version >= 16 && version <= 29) {
                        printf("[+] Found metadata @ 0x%lx (magic=0x%08x ver=%u)\n",
                               addr + i, val, version);
                        *out_addr = addr + i;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// ============================================================
// String Reader from Metadata
// ============================================================
std::string read_string_from_offset(uintptr_t metadata_base, int32_t offset) {
    char buf[1024] = {0};
    uintptr_t addr = metadata_base + offset;
    if (!read_memory(addr, buf, sizeof(buf) - 1)) return "";
    return std::string(buf);
}

// ============================================================
// IL2CPP Type Definitions (simplified)
// ============================================================
// For version 24.1 (Unity 2021+)
struct Il2CppTypeDefinition {
    int32_t nameIndex;
    int32_t namespaceIndex;
    int32_t byvalTypeIndex;
    int32_t byrefTypeIndex;
    int32_t declaringTypeIndex;
    int32_t parentIndex;
    int32_t elementTypeIndex;
    int32_t rgctxStartIndex;
    int32_t rgctxCount;
    int32_t genericContainerIndex;
    uint32_t flags;
    int32_t fieldStart;
    int32_t methodStart;
    int32_t eventStart;
    int32_t propertyStart;
    int32_t nestedTypesStart;
    int32_t interfacesStart;
    int32_t vtableStart;
    int32_t interfaceOffsetsStart;
    uint16_t method_count;
    uint16_t property_count;
    uint16_t field_count;
    uint16_t event_count;
    uint16_t nested_type_count;
    uint16_t vtable_count;
    uint16_t interfaces_count;
    uint16_t interface_offsets_count;
    uint32_t bitfield;
    int32_t token;
};

struct Il2CppMethodDefinition {
    int32_t nameIndex;
    int32_t declaringType;
    int32_t returnType;
    int32_t parameterStart;
    int32_t genericContainerIndex;
    int32_t methodIndex;
    int32_t invokerIndex;
    int32_t reversePInvokeWrapperIndex;
    int32_t rgctxStartIndex;
    int32_t rgctxCount;
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint16_t parameterCount;
};

struct Il2CppFieldDefinition {
    int32_t nameIndex;
    int32_t typeIndex;
    int32_t token;
};

// ============================================================
// Dumper
// ============================================================
int main(int argc, char* argv[]) {
    const char* package = "com.mobile.legends";
    const char* target_lib = "liblogic.so";  // MLBB default
    if (argc > 1) package = argv[1];
    if (argc > 2) target_lib = argv[2];
    
    printf("============================================\n");
    printf("  Panxcz External IL2CPP Dumper v1.0\n");
    printf("  Package: %s\n", package);
    printf("  Target:  %s\n", target_lib);
    printf("  Usage: %s [package] [lib]\n", argv[0]);
    printf("============================================\n\n");
    
    // Find game process
    printf("[+] Finding process: %s\n", package);
    g_pid = find_process(package);
    if (g_pid <= 0) {
        printf("[-] Process not found! Start the game first.\n");
        return 1;
    }
    printf("[+] PID: %d\n", g_pid);
    
    // Open /proc/pid/mem
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", g_pid);
    g_mem_fd = open(mem_path, O_RDONLY);
    if (g_mem_fd < 0) {
        printf("[-] Cannot open %s (need root!)\n", mem_path);
        return 1;
    }
    printf("[+] Memory fd opened\n");
    
    // Find game library
    printf("[+] Searching for %s...\n", target_lib);
    auto libs = get_maps(target_lib);
    if (libs.empty()) libs = get_maps("libcsharp.so");
    if (libs.empty()) libs = get_maps("libil2cpp.so");
    if (libs.empty()) libs = get_maps("liblogic.so");
    
    if (libs.empty()) {
        printf("[-] No IL2CPP library found!\n");
        close(g_mem_fd);
        return 1;
    }
    
    uintptr_t lib_base = libs[0].start;
    printf("[+] Library: %s @ 0x%lx\n", libs[0].name, lib_base);
    
    // Scan for IL2CPP metadata
    printf("\n[+] Scanning for IL2CPP metadata...\n");
    printf("[+] This may take 10-30 seconds...\n");
    
    uintptr_t metadata_addr = 0;
    
    // Strategy 1: Scan library data sections
    printf("[+] Strategy 1: Scanning library sections...\n");
    for (auto& lib : libs) {
        if (find_metadata_in_region(lib.start, lib.end, &metadata_addr)) {
            printf("[+] Found in library section!\n");
            break;
        }
    }
    
    // Strategy 2: Scan all readable anonymous regions
    if (!metadata_addr) {
        printf("[+] Strategy 2: Scanning anonymous regions...\n");
        auto all_regions = get_maps(NULL);
        int scanned = 0;
        for (auto& r : all_regions) {
            // Skip library-mapped regions
            if (r.name[0] != '\0') continue;
            
            if (find_metadata_in_region(r.start, r.end, &metadata_addr)) {
                printf("[+] Found in anonymous region!\n");
                break;
            }
            scanned++;
            if (scanned % 100 == 0) {
                printf("[+] Scanned %d regions...\n", scanned);
            }
        }
    }
    
    // Strategy 3: Scan all readable regions
    if (!metadata_addr) {
        printf("[+] Strategy 3: Scanning ALL readable regions...\n");
        auto all_regions = get_maps(NULL);
        for (auto& r : all_regions) {
            if (find_metadata_in_region(r.start, r.end, &metadata_addr)) {
                printf("[+] Found!\n");
                break;
            }
        }
    }
    
    if (!metadata_addr) {
        printf("[-] Metadata NOT found! Game may have encrypted metadata.\n");
        printf("[-] Try running the game for a few minutes first.\n");
        close(g_mem_fd);
        return 1;
    }
    
    // Read metadata header
    printf("\n[+] Reading metadata header...\n");
    uint32_t header_buf[64];
    if (!read_memory(metadata_addr, header_buf, sizeof(header_buf))) {
        printf("[-] Failed to read metadata header\n");
        close(g_mem_fd);
        return 1;
    }
    
    uint32_t magic = header_buf[0];
    uint32_t version = header_buf[1];
    printf("[+] Magic: 0x%08x\n", magic);
    printf("[+] Version: %u\n", version);
    
    // Generate dump.cs path
    char dump_path[256];
    snprintf(dump_path, sizeof(dump_path), "/sdcard/Download/dump_%s.cs", package);
    
    printf("\n[+] Generating dump.cs...\n");
    printf("[+] Output: %s\n", dump_path);
    
    // For now, save the raw metadata header info
    // A full parser would need to handle all version-specific layouts
    FILE* fp = fopen(dump_path, "w");
    if (!fp) {
        printf("[-] Cannot write to %s\n", dump_path);
        close(g_mem_fd);
        return 1;
    }
    
    fprintf(fp, "// IL2CPP External Dump\n");
    fprintf(fp, "// Package: %s\n", package);
    fprintf(fp, "// PID: %d\n", g_pid);
    fprintf(fp, "// Metadata @ 0x%lx\n", metadata_addr);
    fprintf(fp, "// Magic: 0x%08x, Version: %u\n", magic, version);
    fprintf(fp, "// Library: %s @ 0x%lx\n", libs[0].name, lib_base);
    fprintf(fp, "// Date: %s %s\n\n", __DATE__, __TIME__);
    
    // Scan for string literals in metadata
    printf("[+] Extracting string literals...\n");
    int string_count = 0;
    
    // String literals table follows the header
    // For version 24+, the layout is:
    // Header (varies by version) -> StringLiteralOffset -> StringLiteralDataOffset -> StringOffset
    
    // Read more of the header to find offsets
    uint32_t full_header[128];
    if (read_memory(metadata_addr, full_header, sizeof(full_header))) {
        // The string literal offset is at a version-dependent position
        // For version 24.1: offset 16 (index 4 as int32)
        // For version 29: offset 16 (index 4 as int32)
        
        if (version >= 24) {
            // Try to find strings by scanning
            uintptr_t scan_start = metadata_addr + 256; // Skip header
            uintptr_t scan_end = metadata_addr + 0x1000000; // 16MB max
            
            fprintf(fp, "// ====================================================\n");
            fprintf(fp, "// String Literals (extracted from metadata)\n");
            fprintf(fp, "// ====================================================\n\n");
            
            uint8_t str_buf[4096];
            for (uintptr_t addr = scan_start; addr < scan_end; addr += sizeof(str_buf) - 64) {
                if (!read_memory(addr, str_buf, sizeof(str_buf))) continue;
                
                // Look for printable strings (UTF-8 IL2CPP string literals)
                for (size_t i = 0; i < sizeof(str_buf) - 4; i++) {
                    // Check for C# type names and common patterns
                    if (str_buf[i] >= 'A' && str_buf[i] <= 'Z' &&
                        str_buf[i+1] >= 'a' && str_buf[i+1] <= 'z') {
                        
                        // Try to read as null-terminated string
                        size_t max_len = 256;
                        char str[256] = {0};
                        bool valid = true;
                        for (size_t j = 0; j < max_len; j++) {
                            uint8_t c = str_buf[i + j];
                            if (c == 0) break;
                            if (c < 32 || c > 126) { valid = false; break; }
                            str[j] = c;
                        }
                        
                        if (valid && strlen(str) > 3 && strlen(str) < 200) {
                            // Check if it looks like a class/method/namespace name
                            if (strstr(str, ".") || strstr(str, "::") ||
                                strstr(str, "Manager") || strstr(str, "Player") ||
                                strstr(str, "Entity") || strstr(str, "Battle")) {
                                fprintf(fp, "// @0x%lx: %s\n", addr + i, str);
                                string_count++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    fprintf(fp, "\n// Total strings found: %d\n", string_count);
    
    fclose(fp);
    close(g_mem_fd);
    
    printf("\n[+] ========================================\n");
    printf("[+] DUMP COMPLETE!\n");
    printf("[+] Output: %s\n", dump_path);
    printf("[+] Strings found: %d\n", string_count);
    printf("[+] ========================================\n");
    printf("\n[+] To pull to PC: adb pull %s\n", dump_path);
    
    return 0;
}
