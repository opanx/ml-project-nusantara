/*
 * PanxczTool v1.0 - ELF Executable Injector
 *
 * Flow:
 *   1. List running processes from /proc
 *   2. User selects game
 *   3. Fork + inject libPanxczOverlay.so into game via /proc/pid/mem
 *   4. Game loads .so → ImGui overlay appears
 *
 * Open in MT Manager to see process list + inject.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <elf.h>
#include <dlfcn.h>
#include <vector>
#include <string>

// ========================================
// Process listing
// ========================================

struct ProcInfo {
    int pid;
    char name[256];
    char cmdline[256];
};

static int list_procs(ProcInfo* out, int max) {
    DIR* d = opendir("/proc");
    if (!d) return 0;
    int count = 0;
    struct dirent* e;
    while ((e = readdir(d)) && count < max) {
        // Only numeric dirs (PIDs)
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;

        // Read cmdline
        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char buf[256] = {0};
        fread(buf, 1, sizeof(buf)-1, f);
        fclose(f);
        if (buf[0] == 0) continue;

        // Get basename
        char* base = strrchr(buf, '/');
        base = base ? base + 1 : buf;

        out[count].pid = pid;
        strncpy(out[count].name, base, sizeof(out[count].name)-1);
        strncpy(out[count].cmdline, buf, sizeof(out[count].cmdline)-1);
        count++;
    }
    closedir(d);
    return count;
}

static void print_header(void) {
    printf("\n");
    printf("  ╔═══════════════════════════════════════╗\n");
    printf("  ║     PanxczTool v1.0 - ELF Injector   ║\n");
    printf("  ║     By Panxcz & Freebuff             ║\n");
    printf("  ╚═══════════════════════════════════════╝\n\n");
}

static void print_usage(const char* name) {
    printf("Usage:\n");
    printf("  %s              Interactive mode\n", name);
    printf("  %s <PID>        Inject to specific PID\n", name);
    printf("  %s -h           This help\n\n", name);
}

// ========================================
// Ptrace inject (arm64 Android)
// ========================================

struct arm64_regs {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static int get_regs(pid_t pid, struct arm64_regs* r) {
    struct iovec iov = { r, sizeof(*r) };
    return ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov);
}

static int set_regs(pid_t pid, const struct arm64_regs* r) {
    struct iovec iov = { (void*)r, sizeof(*r) };
    return ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov);
}

// Simple arm64 shellcode to call: mmap(0, 8192, 7, 0x22, -1, 0)
// Returns buffer address in x0
static const uint32_t sc_mmap[] = {
    0xD2800020,  // mov x0, #1 (length page)
    0xD2800080,  // nop placeholder
    0xD2800041,  // mov x1, #2
    0xD2800000,  // mov x0, #0 (addr=0)
    0xD2810001,  // mov x1, #0x2000 (8192)
    0xD28000E2,  // mov x2, #7 (RWX)
    0xD2800443,  // mov x3, #0x22 (PRIVATE|ANON)
    0x92800004,  // mov x4, #-1
    0xD2800005,  // mov x5, #0
    0xD2805C08,  // mov x8, #222 (__NR_mmap)
    0xD4000001,  // svc #0
    0xD4200000,  // brk #0 (trap = done)
};

static int inject_so(pid_t target_pid, const char* so_path) {
    // Attach
    if (ptrace(PTRACE_ATTACH, target_pid, 0, 0) < 0) {
        printf("[-] PTRACE_ATTACH failed: %s\n", strerror(errno));
        return -1;
    }
    int status;
    waitpid(target_pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        printf("[-] Target not stopped\n");
        ptrace(PTRACE_DETACH, target_pid, 0, 0);
        return -1;
    }
    printf("[+] Attached to PID %d\n", target_pid);

    // Save regs
    struct arm64_regs saved;
    if (get_regs(target_pid, &saved) < 0) {
        printf("[-] GETREGS failed\n");
        ptrace(PTRACE_DETACH, target_pid, 0, 0);
        return -1;
    }
    printf("[+] Saved registers (PC=0x%llx)\n", saved.pc);

    // Find a place to put our data (target's stack area)
    char maps[256];
    snprintf(maps, sizeof(maps), "/proc/%d/maps", target_pid);
    FILE* f = fopen(maps, "r");
    if (!f) { ptrace(PTRACE_DETACH, target_pid, 0, 0); return -1; }

    uint64_t stack_start = 0, stack_end = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "[stack")) {
            sscanf(line, "%lx-%lx", &stack_start, &stack_end);
            break;
        }
    }
    fclose(f);

    if (!stack_start) {
        // Fallback: find any rw- region
        f = fopen(maps, "r");
        while (fgets(line, sizeof(line), f)) {
            uint64_t s, e;
            char perms[8];
            if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) == 3) {
                if (perms[0] == 'r' && perms[1] == 'w' && strstr(line, "[anon")) {
                    stack_start = s;
                    stack_end = e;
                    break;
                }
            }
        }
        fclose(f);
    }

    if (!stack_start) {
        printf("[-] No writable region found\n");
        ptrace(PTRACE_DETACH, target_pid, 0, 0);
        return -1;
    }

    uint64_t buf = stack_end - 4096;
    printf("[+] Write target: 0x%llx\n", buf);

    // Write shellcode + path to target
    uint64_t path_addr = buf + 256;
    uint32_t sc[32] = {0};
    memcpy(sc, sc_mmap, sizeof(sc_mmap));
    size_t sc_count = sizeof(sc_mmap) / 4;

    // Patch: after mmap, load x0 with path_addr
    // mov x0, path_addr (split into movz + movk)
    sc[sc_count] = 0xD2800000 | ((path_addr >> 0) & 0xFFFF);   // movz x0, #lo
    sc[sc_count + 1] = 0xF2A00000 | ((path_addr >> 16) & 0xFFFF); // movk x0, #mid, lsl #16
    sc[sc_count + 2] = 0xF2C00000 | ((path_addr >> 32) & 0xFFFF); // movk x0, #hi, lsl #32
    sc_count += 3;

    // mov x1, #2 (RTLD_NOW)
    sc[sc_count++] = 0xD2800041;

    // We need dlopen address. Use: dlopen = linker_base + offset
    // Find linker in target
    f = fopen(maps, "r");
    uint64_t linker_base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "linker64") && !linker_base) {
            sscanf(line, "%lx-", &linker_base);
        }
    }
    fclose(f);

    // Find our own dlopen offset from linker
    Dl_info di;
    void* h = dlopen("libdl.so", RTLD_NOW);
    void* sym = dlsym(h, "android_dlopen_ext");
    if (!sym) sym = dlsym(h, "dlopen");
    dladdr(sym, &di);
    uint64_t our_base = (uint64_t)di.dli_fbase;
    uint64_t dlopen_off = (uint64_t)sym - our_base;
    dlclose(h);

    uint64_t target_dlopen = linker_base + dlopen_off;
    printf("[+] dlopen in target: 0x%llx\n", target_dlopen);

    // bl dlopen (relative branch)
    int64_t bl_off = ((int64_t)target_dlopen - (int64_t)(buf + sc_count * 4)) / 4;
    uint32_t bl = (0x25u << 26) | ((uint32_t)(bl_off & 0x03FFFFFF));
    sc[sc_count++] = bl;

    // brk #0
    sc[sc_count++] = 0xD4200000;

    // Write sc to target
    for (unsigned i = 0; i < sc_count; i++) {
        ptrace(PTRACE_POKEDATA, target_pid, buf + i * 4, (long)sc[i]);
    }

    // Write path
    int path_len = strlen(so_path) + 1;
    long* path_words = (long*)so_path;
    for (int i = 0; i < (path_len + 7) / 8; i++) {
        ptrace(PTRACE_POKEDATA, target_pid, path_addr + i * 8, path_words[i]);
    }

    // Set PC to shellcode
    struct arm64_regs inject_regs = saved;
    inject_regs.pc = buf;
    if (set_regs(target_pid, &inject_regs) < 0) {
        printf("[-] SETREGS failed\n");
        ptrace(PTRACE_DETACH, target_pid, 0, 0);
        return -1;
    }

    printf("[+] Injecting shellcode at 0x%llx...\n", buf);
    ptrace(PTRACE_CONT, target_pid, 0, 0);
    waitpid(target_pid, &status, 0);

    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
        struct arm64_regs result;
        get_regs(target_pid, &result);
        printf("[+] Shellcode executed! dlopen returned: 0x%llx\n", result.x[0]);
        if (result.x[0] != 0) {
            printf("[+] libPanxczOverlay.so loaded into game!\n");
        } else {
            printf("[-] dlopen failed — check .so path and permissions\n");
        }
    } else {
        printf("[-] Unexpected stop (signal=%d)\n", WIFSTOPPED(status) ? WSTOPSIG(status) : 0);
    }

    // Restore and detach
    set_regs(target_pid, &saved);
    ptrace(PTRACE_DETACH, target_pid, 0, 0);
    printf("[+] Detached from PID %d\n", target_pid);
    return 0;
}

// ========================================
// Main
// ========================================

int main(int argc, char* argv[]) {
    print_header();

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    if (getuid() != 0) {
        printf("[-] Must run as root!\n");
        printf("    su -c %s\n\n", argv[0]);
        return 1;
    }

    const char* so_path = "/data/local/tmp/libPanxczOverlay.so";
    int target_pid = 0;

    if (argc >= 2) target_pid = atoi(argv[1]);
    if (argc >= 3) so_path = argv[2];

    // Interactive: show process list
    if (target_pid == 0) {
        ProcInfo procs[512];
        int count = list_procs(procs, 512);

        printf("  #   PID     Process\n");
        printf("  ─── ─────── ─────────────────────────────────────\n");

        // Filter: show only interesting processes
        int shown = 0;
        for (int i = 0; i < count; i++) {
            if (procs[i].name[0] == '[' || procs[i].name[0] == 0) continue;
            printf("  %-3d %-7d %s\n", shown, procs[i].pid, procs[i].name);
            shown++;
        }
        printf("\n  Total: %d processes\n\n", shown);
        printf("  Enter PID to inject: ");

        char input[64];
        if (!fgets(input, sizeof(input), stdin)) return 1;
        input[strcspn(input, "\n")] = 0;

        if (input[0] == 'q' || input[0] == 'Q') return 0;

        target_pid = atoi(input);
        if (target_pid <= 0) {
            printf("[-] Invalid PID\n");
            return 1;
        }
    }

    // Verify PID
    char ppath[64];
    snprintf(ppath, sizeof(ppath), "/proc/%d", target_pid);
    if (access(ppath, F_OK) < 0) {
        printf("[-] PID %d not found!\n", target_pid);
        return 1;
    }

    // Verify .so
    if (access(so_path, F_OK) < 0) {
        printf("[-] %s not found!\n", so_path);
        printf("[*] Push it first: adb push libPanxczOverlay.so %s\n", so_path);
        return 1;
    }

    // Show target info
    snprintf(ppath, sizeof(ppath), "/proc/%d/cmdline", target_pid);
    FILE* f = fopen(ppath, "r");
    if (f) {
        char cmd[256] = {0};
        fread(cmd, 1, sizeof(cmd)-1, f);
        fclose(f);
        printf("[+] Target: PID %d — %s\n", target_pid, cmd);
    }
    printf("[+] Library: %s\n\n", so_path);

    // Inject!
    printf("[*] Injecting into PID %d...\n\n", target_pid);
    int r = inject_so(target_pid, so_path);

    if (r == 0) {
        printf("\n[+] DONE! ImGui overlay should appear in the game.\n");
        printf("[+] If not, check logcat: adb logcat | grep Panxcz\n");
    } else {
        printf("\n[-] Injection failed!\n");
    }

    return r;
}
