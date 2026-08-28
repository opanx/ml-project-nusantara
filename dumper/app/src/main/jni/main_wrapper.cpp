/*
 * Panxcz Tool v1.0 - External ELF Injector
 * 
 * Flow:
 *   1. List running processes from /proc
 *   2. User selects game process
 *   3. Fork + ptrace inject libPanxczOverlay.so into target
 *   4. .so has constructor → ImGui renders inside game process
 *
 * This is the INJECTOR binary. The overlay .so is loaded into the game.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <elf.h>

// Android aarch64 ptrace compat
#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS 0x40000002  // Might not exist on Android
#endif
#ifndef PTRACE_SETREGS
#define PTRACE_SETREGS 0x40000003
#endif

// Use PTRACE_GETREGSET/SETREGSET on Android
struct pt_regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static int ptrace_getregs(pid_t pid, struct pt_regs* regs) {
    struct iovec iov = { regs, sizeof(struct pt_regs) };
    return ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov);
}

static int ptrace_setregs(pid_t pid, const struct pt_regs* regs) {
    struct iovec iov = { (void*)regs, sizeof(struct pt_regs) };
    return ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov);
}
#include <dlfcn.h>
#include <vector>
#include <string>
#include <algorithm>
#include <errno.h>

// =================== Process List ===================

struct ProcessInfo {
    int pid;
    std::string name;
    std::string cmdline;
};

bool isNumeric(const std::string& s) {
    for (char c : s) {
        if (!isdigit(c)) return false;
    }
    return !s.empty();
}

std::vector<ProcessInfo> listProcesses() {
    std::vector<ProcessInfo> procs;
    DIR* dir = opendir("/proc");
    if (!dir) return procs;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!isNumeric(entry->d_name)) continue;

        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        // Read cmdline
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* f = fopen(path, "r");
        if (!f) continue;

        char cmdline[512] = {0};
        size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);

        if (n == 0) continue;

        // Parse process name from cmdline
        std::string cmd(cmdline, n);
        std::string name = cmd;
        size_t nullPos = cmd.find('\0');
        if (nullPos != std::string::npos) {
            name = cmd.substr(0, nullPos);
        }

        // Get just the filename
        std::string basename = name;
        size_t slash = name.rfind('/');
        if (slash != std::string::npos) {
            basename = name.substr(slash + 1);
        }

        ProcessInfo info;
        info.pid = pid;
        info.name = basename;
        info.cmdline = cmd;
        procs.push_back(info);
    }
    closedir(dir);
    return procs;
}

void printProcessList(const std::vector<ProcessInfo>& procs) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║           Panxcz Tool - Process Selector               ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  #   PID     Process Name                              ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");

    // Filter: show interesting processes (games, apps)
    int idx = 0;
    for (auto& p : procs) {
        // Skip kernel/system processes
        if (p.name.empty() || p.name[0] == '[') continue;

        // Show all user processes
        printf("║  %-3d %-7d %-42s ║\n", idx, p.pid, p.name.substr(0, 42).c_str());
        idx++;
    }

    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

// =================== Ptrace Injector ===================

// Shellcode for arm64: call dlopen(path, RTLD_NOW)
// x0 = pointer to library path (in target's memory)
// x1 = RTLD_NOW (2)
static const unsigned char shellcode_dlopen[] = {
    // mov x0, x19          // x0 = path (saved in x19)
    0xf3, 0x03, 0x00, 0xaa,
    // mov x1, #2            // RTLD_NOW
    0x41, 0x04, 0x80, 0xd2,
    // bl dlopen (will be patched to actual address)
    0x00, 0x00, 0x00, 0x94,
    // brk #0                // trap — signal we're done
    0x00, 0x00, 0x20, 0xd4,
};

// ARM64 NOP = 0xd503201f
#define ARM64_NOP 0xd503201f

int injectLibrary(pid_t target_pid, const char* lib_path) {
    printf("[*] Attaching to PID %d...\n", target_pid);

    // Attach
    if (ptrace(PTRACE_ATTACH, target_pid, nullptr, nullptr) < 0) {
        printf("[-] PTRACE_ATTACH failed: %s\n", strerror(errno));
        return -1;
    }

    int status;
    waitpid(target_pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        printf("[-] Target didn't stop\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }
    printf("[+] Attached!\n");

    // Save registers
    struct pt_regs orig_regs;
    if (ptrace_getregs(target_pid, &orig_regs) < 0) {
        printf("[-] PTRACE_GETREGS failed\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }
    printf("[+] Registers saved (PC=0x%llx)\n", orig_regs.pc);

    // Find a writable executable region in target
    // We'll write shellcode + path into a mmap'd region
    void* remote_buf = nullptr;

    // Use mmap in target process via syscall injection
    // syscall number for mmap on arm64 = 222
    // mmap(addr=0, length=4096, prot=RWX, flags=MAP_PRIVATE|MAP_ANONYMOUS, fd=-1, offset=0)
    struct pt_regs inject_regs = orig_regs;
    inject_regs.regs[8] = 222;  // __NR_mmap
    inject_regs.regs[0] = 0;    // addr
    inject_regs.regs[1] = 8192; // length
    inject_regs.regs[2] = 7;    // PROT_READ|PROT_WRITE|PROT_EXEC
    inject_regs.regs[3] = 0x22; // MAP_PRIVATE|MAP_ANONYMOUS
    inject_regs.regs[4] = (uint64_t)-1; // fd
    inject_regs.regs[5] = 0;    // offset

    // Set PC to a syscall instruction
    // We need to find a syscall instruction in the target
    // Alternative: use PTRACE_SYSCALL approach

    // Simpler approach: write directly to a known writable region
    // Read /proc/<pid>/maps to find a suitable region
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", target_pid);
    FILE* maps = fopen(maps_path, "r");
    if (!maps) {
        printf("[-] Cannot open maps\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }

    // Find first rw- region (stack, heap, or anonymous)
    char line[512];
    uint64_t rw_start = 0, rw_end = 0;
    while (fgets(line, sizeof(line), maps)) {
        uint64_t start, end;
        char perms[8];
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            if (perms[0] == 'r' && perms[1] == 'w') {
                rw_start = start;
                rw_end = end;
                // Prefer anonymous regions
                if (strstr(line, "00000000 00:00 0") || strstr(line, "[anon:")) {
                    break;
                }
            }
        }
    }
    fclose(maps);

    if (rw_start == 0) {
        printf("[-] No writable region found\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }

    // Use the end of the region (before guard page)
    uint64_t buf_addr = rw_end - 16384; // Leave some space
    printf("[+] Writable region: 0x%lx-0x%lx, using 0x%lx\n", rw_start, rw_end, buf_addr);

    // Find dlopen address in target
    // dlopen is in linker, we can find it from our own process
    void* dlopen_addr = dlopen(lib_path, RTLD_NOW | RTLD_NOLOAD); // This just gets the handle
    if (dlopen_addr) dlclose(dlopen_addr);

    // Get dlopen from linker64
    void* linker = dlopen("libdl.so", RTLD_NOW);
    void* (*real_dlopen)(const char*, int) = nullptr;
    if (linker) {
        real_dlopen = (decltype(real_dlopen))dlsym(linker, "android_dlopen_ext");
        if (!real_dlopen) real_dlopen = (decltype(real_dlopen))dlsym(linker, "dlopen");
        dlclose(linker);
    }
    if (!real_dlopen) {
        real_dlopen = dlopen;
    }

    // Find dlopen in target process by scanning its linker
    // Simpler: read target's /proc/<pid>/map for linker
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", target_pid);
    maps = fopen(maps_path, "r");
    uint64_t linker_base = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "linker64")) {
            sscanf(line, "%lx-", &linker_base);
            break;
        }
    }
    fclose(maps);

    if (linker_base == 0) {
        printf("[-] Cannot find linker64 in target\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }
    printf("[+] Target linker64 base: 0x%lx\n", linker_base);

    // dlopen offset from linker base (approximate, varies by Android version)
    // We'll use a more reliable approach: find it from our own linker
    void* our_linker = dlopen("libc.so", RTLD_NOW);
    uintptr_t our_dlopen = (uintptr_t)dlsym(our_linker, "android_dlopen_ext");
    dlclose(our_linker);

    if (our_dlopen == 0) {
        our_dlopen = (uintptr_t)dlopen;
    }

    // Calculate offset: dlopen_offset = our_dlopen - our_linker_base
    Dl_info info;
    dladdr((void*)our_dlopen, &info);
    uintptr_t our_linker_base = (uintptr_t)info.dli_fbase;
    uintptr_t dlopen_offset = our_dlopen - our_linker_base;

    uintptr_t target_dlopen = linker_base + dlopen_offset;
    printf("[+] dlopen offset: 0x%lx, target dlopen: 0x%lx\n", dlopen_offset, target_dlopen);

    // Prepare payload: shellcode + library path
    size_t path_len = strlen(lib_path) + 1;
    size_t path_offset = sizeof(shellcode_dlopen) + (16 - sizeof(shellcode_dlopen) % 16) % 16; // Align to 16
    size_t total_size = path_offset + path_len;

    unsigned char payload[4096] = {0};
    memcpy(payload, shellcode_dlopen, sizeof(shellcode_dlopen));

    // Patch the BL instruction to point to dlopen
    // BL offset = (target_dlopen - (buf_addr + 8)) / 4
    int64_t bl_offset = ((int64_t)target_dlopen - (int64_t)(buf_addr + 8)) / 4;
    // BL encoding: imm26 = offset[25:2], bits [31:26] = 0b100101
    uint32_t bl_insn = (0x25 << 26) | ((uint32_t)(bl_offset & 0x03FFFFFF));
    memcpy(payload + 8, &bl_insn, 4);

    // Copy library path after shellcode
    memcpy(payload + path_offset, lib_path, path_len);

    // Patch x19 load to point to path (offset in payload)
    // mov x19, #path_offset (we'll use a different approach)
    // Actually, let's use a simpler shellcode:
    // Load path address into x0 using ADRP + ADD
    // For simplicity, let's just use the raw address approach

    // Simpler shellcode that works:
    // x0 already points to path if we set it up right
    unsigned char simple_shellcode[] = {
        // adr x0, path_label (PC-relative, offset to path)
        0x00, 0x00, 0x00, 0x10,  // adr x0, #0 (will patch offset)
        // mov x1, #2 (RTLD_NOW)
        0x41, 0x04, 0x80, 0xd2,
        // bl dlopen (will patch)
        0x00, 0x00, 0x00, 0x94,
        // brk #0 (trap)
        0x00, 0x00, 0x20, 0xd4,
    };

    // Patch ADR x0 offset: imm21 = (path_addr - insn_addr) / 4
    int64_t adrp_offset = (int64_t)((buf_addr + path_offset) - buf_addr);
    uint32_t adrp_imm = (uint32_t)((adrp_offset >> 2) & 0x7FFFF);
    uint32_t adrp_lo = (uint32_t)(adrp_offset & 0x3) << 29;
    // ADR encoding: immhi:immlo format
    uint32_t adrp_insn = 0x10000000 | ((adrp_imm & 0x7FFFF) << 5) | (adrp_offset & 0x3);
    memcpy(simple_shellcode, &adrp_insn, 4);

    // Patch BL
    bl_offset = ((int64_t)target_dlopen - (int64_t)(buf_addr + 8)) / 4;
    bl_insn = (0x25 << 26) | ((uint32_t)(bl_offset & 0x03FFFFFF));
    memcpy(simple_shellcode + 8, &bl_insn, 4);

    memset(payload, 0, sizeof(payload));
    memcpy(payload, simple_shellcode, sizeof(simple_shellcode));
    memcpy(payload + path_offset, lib_path, path_len);

    printf("[+] Payload: %zu bytes (shellcode=%zu, path@%zu='%s')\n",
           total_size, sizeof(simple_shellcode), path_offset, lib_path);

    // Write payload to target's memory
    for (size_t i = 0; i < total_size; i += sizeof(long)) {
        long word = 0;
        memcpy(&word, payload + i, sizeof(long));
        ptrace(PTRACE_POKEDATA, target_pid, buf_addr + i, word);
    }
    printf("[+] Payload written to 0x%lx\n", buf_addr);

    // Set registers: PC = shellcode, x0 will be set by shellcode
    struct pt_regs inject_regs2 = orig_regs;
    inject_regs2.pc = buf_addr;  // Start of shellcode
    if (ptrace_setregs(target_pid, &inject_regs2) < 0) {
        printf("[-] PTRACE_SETREGS failed\n");
        ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
        return -1;
    }

    // Continue execution
    printf("[+] Injecting... (PC=0x%llx)\n", inject_regs2.pc);
    ptrace(PTRACE_CONT, target_pid, nullptr, nullptr);

    // Wait for brk (trap)
    waitpid(target_pid, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
        printf("[+] Injection successful! Shellcode executed.\n");

        // Check return value (x0 = dlopen result)
        struct pt_regs result_regs;
        ptrace_getregs(target_pid, &result_regs);
        printf("[+] dlopen returned: 0x%llx\n", result_regs.regs[0]);

        if (result_regs.regs[0] == 0) {
            printf("[-] dlopen failed! Check if .so path is correct.\n");
        }
    } else {
        printf("[-] Unexpected signal: %d\n", WIFSTOPPED(status) ? WSTOPSIG(status) : 0);
    }

    // Restore registers
    ptrace_setregs(target_pid, &orig_regs);
    ptrace(PTRACE_DETACH, target_pid, nullptr, nullptr);
    printf("[+] Detached from PID %d\n", target_pid);

    return 0;
}

// =================== Main ===================

void printBanner() {
    printf("\n");
    printf("┌──────────────────────────────────────────────────┐\n");
    printf("│        Panxcz Tool v1.0 - ELF Injector          │\n");
    printf("│        By Panxcz & Freebuff                     │\n");
    printf("└──────────────────────────────────────────────────┘\n");
    printf("\n");
}

void printUsage(const char* argv0) {
    printf("Usage:\n");
    printf("  %s                    — Interactive process selector\n", argv0);
    printf("  %s <PID>              — Inject into specific PID\n", argv0);
    printf("  %s <PID> <lib.so>     — Inject custom .so\n", argv0);
    printf("\n");
}

int main(int argc, char* argv[]) {
    printBanner();

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printUsage(argv[0]);
        return 0;
    }

    // Check root
    if (getuid() != 0) {
        printf("[-] Must run as root! Use: su -c %s\n", argv[0]);
        return 1;
    }

    // Default overlay library path
    const char* overlay_lib = "/data/local/tmp/libPanxczOverlay.so";
    int target_pid = 0;

    if (argc >= 2) {
        target_pid = atoi(argv[1]);
    }
    if (argc >= 3) {
        overlay_lib = argv[2];
    }

    // Interactive process selector
    if (target_pid == 0) {
        printf("[*] Scanning processes...\n");
        auto procs = listProcesses();
        printf("[+] Found %zu processes\n", procs.size());
        printProcessList(procs);

        printf("Enter PID to inject (or 'q' to quit): ");
        char input[64];
        if (!fgets(input, sizeof(input), stdin)) {
            printf("[-] No input\n");
            return 1;
        }

        // Trim newline
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0) {
            printf("Bye!\n");
            return 0;
        }

        target_pid = atoi(input);
        if (target_pid <= 0) {
            printf("[-] Invalid PID: %s\n", input);
            return 1;
        }
    }

    // Verify PID exists
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", target_pid);
    if (access(proc_path, F_OK) < 0) {
        printf("[-] PID %d does not exist!\n", target_pid);
        return 1;
    }

    // Show target info
    char cmdline_path[128];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", target_pid);
    FILE* f = fopen(cmdline_path, "r");
    if (f) {
        char cmdline[256] = {0};
        fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);
        printf("[+] Target: PID %d — %s\n", target_pid, cmdline);
    }

    // Verify overlay library exists
    if (access(overlay_lib, F_OK) < 0) {
        printf("[-] Overlay library not found: %s\n", overlay_lib);
        printf("[*] Make sure libPanxczOverlay.so is in /data/local/tmp/\n");
        return 1;
    }
    printf("[+] Overlay library: %s\n", overlay_lib);

    // Inject
    printf("\n[*] Starting injection...\n");
    int result = injectLibrary(target_pid, overlay_lib);

    if (result == 0) {
        printf("\n[+] Injection complete!\n");
        printf("[+] ImGui overlay should now appear in the game.\n");
        printf("[+] Run this tool again to re-inject if needed.\n");
    } else {
        printf("\n[-] Injection failed!\n");
    }

    return result;
}
