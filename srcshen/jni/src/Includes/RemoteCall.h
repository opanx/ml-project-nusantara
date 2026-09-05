/*
 * ============================================================================
 * RemoteCall.h  -  Panxcz Direct-Call Bypass Engine (aarch64, external ELF)
 * ============================================================================
 * By Panxcz & Freebuff. Educational use only.
 *
 * WHAT THIS DOES
 * --------------
 * Lets an EXTERNAL root binary (like this tool) call an arbitrary function
 * INSIDE the game process, without:
 *   - patching a single byte of the game's .so  (code-integrity hashes pass)
 *   - keeping ptrace attached during gameplay    (TracerPid = 0 in a match)
 *   - creating new threads                       (no /proc/pid/task changes)
 *   - injecting a .so / Frida agent / Xposed     (no foreign libraries)
 *
 * HOW IT WORKS (3 phases, all inside a few milliseconds, ONCE per match)
 * ---------------------------------------------------------------------
 *  1. ATTACH  : PTRACE_SEIZE + PTRACE_INTERRUPT (no SIGSTOP storm, minimal
 *               signal noise). Find the game's existing RWX region (ART JIT
 *               code cache - present on every Android 8+ device).
 *  2. STUB    : Write a tiny syscall-only stub into that JIT region (original
 *               bytes saved), run it once to:
 *                  mmap(NULL, 0x2000, RWX)           -> trampoline region
 *               then run phase 2 to:
 *                  mprotect(page0 -> RX)             (page1 stays RW = scratch)
 *                  rt_sigaction(SIGRTMIN+8, handler = trampoline, oldact saved)
 *               Restore the JIT bytes, DETACH. TracerPid returns to 0.
 *  3. FIRE    : retri cast = plain tgkill(pid, main_tid, SIGRTMIN+8).
 *               The game's OWN main thread runs the trampoline, which reads
 *               m_iSummonSkillId from the player object and calls the game's
 *               own cast function. No ptrace, no write, no thread at fire time.
 *
 * WHY THIS DIFFERS FROM OTHER TOOLS  (see BYPASS.md for full detail)
 * -------------------------------------------------------------------
 *  - GameGuardian-style tools  : keep ptrace attached the whole session ->
 *                                TracerPid visible -> instant anti-cheat flag.
 *  - Inline-hook tools         : modify .text of the game -> hash check bans.
 *  - pthread_create injectors  : visible new thread + foreign stack.
 *  - Frida/LSPosed             : huge footprint (agent .so, /proc scans).
 *  - Ours                       : zero footprint AFTER a 5ms one-time install,
 *                                trigger = one signal, call = game's own code.
 *
 * IMPORTANT (honest note)
 * -----------------------
 * The BYPASS itself is complete. The exact cast-function RVA + 'this' context
 * need ONE round of on-device verification (see RETRI_CAST_RVA in main.cpp).
 * If the cast RVA is wrong the trampoline calls garbage -> game crash, so
 * Direct Call mode defaults to OFF: enable it in the Auto Retri tab after the
 * RVA is confirmed, otherwise the proven touch path stays active.
 * ============================================================================
 */
#ifndef REMOTECALL_H
#define REMOTECALL_H

#if defined(__aarch64__)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/syscall.h>

namespace RC {

// ---------------------------------------------------------------------------
// Config: everything the trampoline needs, baked at install time
// ---------------------------------------------------------------------------
struct Cfg {
    uintptr_t playerAddr = 0;   // local hero ShowPlayer (skill-id source)
    uintptr_t castFunc   = 0;   // absolute cast function = libbase + RVA
    uintptr_t context    = 0;   // 'this' for instance methods (0 = none)
    uint32_t  skillIdOff = 0x95c; // m_iSummonSkillId on player
    uint32_t  sig        = 0;   // 0 = auto (40 = SIGRTMIN+8)
    int       timeoutMs  = 2000;
};

// return codes from Install()
enum {
    RC_OK      = 1,   // installed + self-test fired OK
    RC_FAIL    = 0,   // generic failure (see ErrStr())
    RC_NO_RWX  = -1   // no writable+executable region -> caller falls back
};

// ---------------------------------------------------------------------------
// aarch64 syscall numbers (kernel UAPI, stable)
// ---------------------------------------------------------------------------
enum {
    RC_NR_MMAP          = 222,
    RC_NR_MPROTECT      = 226,
    RC_NR_RT_SIGACTION  = 134,
    RC_NR_MUNMAP        = 215,
    RC_NR_TGKILL        = 131
};

// SIGRTMIN on bionic = 32; 32+8 = 40. ART keeps its RT signals at both
// extremes, 40 sits clear of both. Literal on purpose (stub bakes it).
enum { RC_SIG = 40 };
enum { SA_RESTART_ARM64 = 0x10000000ULL };

enum {
    RC_REGION  = 0x2000,          // trampoline region size (2 pages)
    RC_PAGE0   = 0x1000,          // page0 = RX (code+literals)
    RC_SCRATCH = 0x1000,          // page1 offset = RW scratch (fire ack)
    RC_OLDACT  = 0x1100,          // saved sigaction struct (uninstall restore)
    RC_STUB_TOTAL = 0x200         // stub area inside JIT region (restored after)
};

// minimal kernel user_pt_regs (aarch64) - avoids <elf.h> portability issues
struct RC_Regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

// ---------------------------------------------------------------------------
// static state
// ---------------------------------------------------------------------------
static int         s_pid       = 0;
static bool        s_installed = false;
static int         s_sig       = RC_SIG;
static bool        s_usePoke   = false;  // stub region not writable -> ptrace poke
static uintptr_t   s_base      = 0;   // trampoline region base (in game)
static uintptr_t   s_jit       = 0;   // game region we borrowed (rwxp or r-xp)
static uint8_t     s_orig[RC_STUB_TOTAL];
static char        s_err[192]  = "idle";

static void SetErr(const char* msg) {
    snprintf(s_err, sizeof(s_err), "%s", msg);
}

// ---------------------------------------------------------------------------
// low-level helpers (all self-contained, raw syscalls)
// ---------------------------------------------------------------------------
static bool RC_PidAlive(int pid) {
    return pid > 0 && kill(pid, 0) == 0;
}

static bool RC_VmRead(int pid, uintptr_t addr, void* buf, size_t len) {
    struct iovec l, r;
    l.iov_base = buf;       l.iov_len = len;
    r.iov_base = (void*)(addr & 0x00FFFFFFFFFFFFFFULL); // strip MTE tag
    r.iov_len  = len;
    return syscall(SYS_process_vm_readv, pid, &l, 1, &r, 1, 0) == (ssize_t)len;
}

static bool RC_VmWrite(int pid, uintptr_t addr, const void* buf, size_t len) {
    struct iovec l, r;
    l.iov_base = (void*)buf; l.iov_len = len;
    r.iov_base = (void*)(addr & 0x00FFFFFFFFFFFFFFULL);
    r.iov_len  = len;
    return syscall(SYS_process_vm_writev, pid, &l, 1, &r, 1, 0) == (ssize_t)len;
}

static bool RC_GetRegs(int pid, RC_Regs* regs) {
    struct iovec io;
    io.iov_base = regs;
    io.iov_len  = sizeof(RC_Regs);
    return ptrace(PTRACE_GETREGSET, pid, (void*)1 /*NT_PRSTATUS*/, &io) == 0;
}

static bool RC_SetRegs(int pid, const RC_Regs& regs) {
    struct iovec io;
    io.iov_base = (void*)&regs;
    io.iov_len  = sizeof(RC_Regs);
    return ptrace(PTRACE_SETREGSET, pid, (void*)1 /*NT_PRSTATUS*/, &io) == 0;
}

static bool RC_WaitStop(int pid, int ms) {
    for (int i = 0; i < ms; i += 5) {
        int st = 0;
        pid_t r = waitpid(pid, &st, __WALL | WNOHANG);
        if (r == pid) return true;
        if (r < 0 && errno != EINTR) return false;
        usleep(5000);
    }
    return false;
}

static bool RC_Attach(int pid) {
    if (ptrace(PTRACE_SEIZE, pid, 0, 0) < 0) {
        SetErr("PTRACE_SEIZE failed");
        return false;
    }
    if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) < 0) {
        ptrace(PTRACE_DETACH, pid, 0, 0);
        SetErr("PTRACE_INTERRUPT failed");
        return false;
    }
    if (!RC_WaitStop(pid, 1500)) {
        ptrace(PTRACE_DETACH, pid, 0, 0);
        SetErr("attach wait timeout");
        return false;
    }
    return true;
}

static void RC_Detach(int pid) {
    ptrace(PTRACE_DETACH, pid, 0, 0);
}

// run a stub until its brk; returns the post-stub register file
static bool RC_RunStub(int pid, const RC_Regs& in, RC_Regs* out, int ms) {
    if (!RC_SetRegs(pid, in)) { SetErr("SETREGS failed"); return false; }
    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) { SetErr("CONT failed"); return false; }
    if (!RC_WaitStop(pid, ms)) { SetErr("stub run timeout"); return false; }
    if (!RC_GetRegs(pid, out)) { SetErr("GETREGS failed"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// find a usable executable region in the game:
//   pass 1: rwxp (ART JIT code cache - writable, process_vm_writev works)
//   pass 2: r-xp (read+exec, anonymous or JIT-named) -> written via PTRACE poke
//   pass 3: any r-xp (last resort, experimental)
// ---------------------------------------------------------------------------
static bool RC_FindExecRegion(int pid, uintptr_t* outStart, size_t* outSize, bool* outWritable) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* fp = fopen(path, "r");
    if (!fp) { SetErr("cannot open maps"); return false; }
    char line[512];
    uintptr_t candW[3] = {0}; size_t candWSz[3] = {0}; int nW = 0;   // rwxp
    uintptr_t candR[3] = {0}; size_t candRSz[3] = {0}; int nR = 0;   // r-xp anonymous/jit
    uintptr_t candA[3] = {0}; size_t candASz[3] = {0}; int nA = 0;   // any r-xp
    while (fgets(line, sizeof(line), fp)) {
        if (!(line[0] >= '0' && line[0] <= '9')) continue;
        const char* p = strchr(line, ' ');
        if (!p) continue;
        bool r = p[1] == 'r', w = p[2] == 'w', x = p[3] == 'x';
        if (!r || !x) continue;
        uintptr_t a = 0, b = 0;
        if (sscanf(line, "%lx-%lx", &a, &b) != 2 || b <= a + RC_STUB_TOTAL) continue;
        if (w) {
            if (nW < 3) { candW[nW] = a; candWSz[nW] = (size_t)(b - a); nW++; }
            continue;
        }
        // r-xp: prefer anonymous or JIT-ish, else any
        const char* name = strchr(line + 5, '/');
        bool anon = (name == nullptr) || strstr(line, "[anon:") || strstr(line, "jit") ||
                    strstr(line, "code-cache") || strstr(line, "dalvik");
        if (anon && nR < 3) { candR[nR] = a; candRSz[nR] = (size_t)(b - a); nR++; }
        else if (nA < 3) { candA[nA] = a; candASz[nA] = (size_t)(b - a); nA++; }
    }
    fclose(fp);
    if (nW > 0) { *outStart = candW[0]; *outSize = candWSz[0]; *outWritable = true; return true; }
    if (nR > 0) { *outStart = candR[0]; *outSize = candRSz[0]; *outWritable = false; return true; }
    if (nA > 0) { *outStart = candA[0]; *outSize = candASz[0]; *outWritable = false; return true; }
    SetErr("no executable region found");
    return false;
}

// ptrace word access for RX regions (PEEKDATA/POKEDATA bypass page permissions)
static bool RC_PokeRead(int pid, uintptr_t addr, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    for (size_t off = 0; off < len; off += 8) {
        errno = 0;
        long v = ptrace(PTRACE_PEEKDATA, pid, (void*)(addr + off), 0);
        if (v == -1 && errno != 0) return false;
        memcpy(out + off, &v, 8);
    }
    return true;
}

static bool RC_PokeWrite(int pid, uintptr_t addr, const void* buf, size_t len) {
    const uint8_t* in = (const uint8_t*)buf;
    for (size_t off = 0; off < len; off += 8) {
        long v = 0;
        memcpy(&v, in + off, 8);
        if (ptrace(PTRACE_POKEDATA, pid, (void*)(addr + off), (void*)v) < 0) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// one-shot stub bytes (built once, written into the JIT region)
// ---------------------------------------------------------------------------
// All instruction encodings below were verified with aarch64-linux-gnu-as.
static void RC_BuildStubs(uint32_t* s1, uint32_t* s2, uint32_t* s3) {
    // phase 1: mmap(NULL, 0x2000, RWX, PRIVATE|ANON, -1, 0) -> x0; save x6
    uint32_t st1[] = {
        0xD2801BC8,  // mov x8, #222        (mmap)
        0xAA1F03E0,  // mov x0, xzr
        0xD2840001,  // mov x1, #0x2000
        0xD28000E2,  // mov x2, #7          (PROT_RWX)
        0xD2800443,  // mov x3, #0x22       (MAP_PRIVATE|MAP_ANONYMOUS)
        0xAA1F03E4,  // mov x4, xzr
        0xAA2403E4,  // mvn x4, x4          (x4 = -1)
        0xAA1F03E5,  // mov x5, xzr
        0xD4000001,  // svc #0
        0xAA0003E6,  // mov x6, x0          (save mmap result)
        0xD42009A0,  // brk #0x4d
    };
    // phase 2: mprotect(x6, 0x1000, RX) + rt_sigaction(40, &sa, &oldact, 8)
    uint32_t st2[] = {
        0xD2801C48,  // mov x8, #226        (mprotect)
        0xAA0603E0,  // mov x0, x6          (addr = mmap base)
        0xD2820001,  // mov x1, #0x1000     (page0 only)
        0xD28000A2,  // mov x2, #5          (PROT_READ|PROT_EXEC)
        0xD4000001,  // svc #0
        0xD10283FF,  // sub sp, sp, #0xa0
        0xA90027E7,  // stp x7, x9, [sp]    (handler, flags)
        0xF9000BFF,  // str xzr, [sp, #16]  (restorer)
        0xA901FFFF,  // stp xzr,xzr,[sp,#24]
        0xA902FFFF,  // stp xzr,xzr,[sp,#40]
        0xA903FFFF,  // stp xzr,xzr,[sp,#56]
        0xA904FFFF,  // stp xzr,xzr,[sp,#72]
        0xA905FFFF,  // stp xzr,xzr,[sp,#88]
        0xA906FFFF,  // stp xzr,xzr,[sp,#104]
        0xA907FFFF,  // stp xzr,xzr,[sp,#120]
        0xA908FFFF,  // stp xzr,xzr,[sp,#136]
        0xD2800500,  // mov x0, #40         (sig)
        0x910003E1,  // mov x1, sp          (act)
        0xAA1A03E2,  // mov x2, x26         (oldact = saved location)
        0xD2800103,  // mov x3, #8          (sigsetsize)
        0xD28010C8,  // mov x8, #134        (rt_sigaction)
        0xD4000001,  // svc #0
        0x910283FF,  // add sp, sp, #0xa0
        0xAA0003E7,  // mov x7, x0          (result -> x7)
        0xD42009A0,  // brk #0x4d
    };
    // phase 3: rt_sigaction(40, oldact, NULL, 8) + munmap(x6, 0x2000)
    uint32_t st3[] = {
        0xD28010C8,  // mov x8, #134        (rt_sigaction)
        0xD4000001,  // svc #0              (args set by us: x0=sig x1=old x2=0 x3=8)
        0xD2801AE8,  // mov x8, #215        (munmap)
        0xAA0603E0,  // mov x0, x6          (base, set by us)
        0xD2840001,  // mov x1, #0x2000
        0xD4000001,  // svc #0
        0xD42009A0,  // brk #0x4d
    };
    memcpy(s1, st1, sizeof(st1));
    memcpy(s2, st2, sizeof(st2));
    memcpy(s3, st3, sizeof(st3));
}

// ---------------------------------------------------------------------------
// trampoline (signal handler body) - reads skill id from player, calls cast
// ---------------------------------------------------------------------------
static void RC_BuildTrampoline(uint32_t* t, const Cfg& cfg) {
    uint32_t ldr0  = 0x58000160;                    // ldr x0,  [pc,#0x2c] (context)
    uint32_t ldr17 = 0x58000191;                    // ldr x17, [pc,#0x34] (player)
    uint32_t ldrw  = 0xB9400000
                   | (((cfg.skillIdOff >> 2) & 0xFFFu) << 10)   // LDR W: imm12 = off/4
                   | (17u << 5) | 1;                // ldr w1, [x17, #off]
    uint32_t movx2 = 0xAA1F03E2;                    // mov x2, xzr
    uint32_t movx3 = 0xD2800023;                    // mov x3, #1  (manuallySelect)
    uint32_t ldr16 = 0x58000150;                    // ldr x16, [pc,#0x3c] (cast)
    uint32_t blr   = 0xD63F0200;                    // blr x16
    uint32_t movw0 = 0x52800020;                    // mov w0, #1  (ack magic)
    uint32_t ldr17b= 0x58000131;                    // ldr x17, [pc,#0x44] (scratch)
    uint32_t strw  = 0xB9000220;                    // str w0, [x17]
    uint32_t ret   = 0xD65F03C0;                    // ret
    uint32_t code[] = { ldr0, ldr17, ldrw, movx2, movx3, ldr16, blr,
                        movw0, ldr17b, strw, ret };
    memcpy(t, code, sizeof(code));
    uint64_t* lit = (uint64_t*)((uint8_t*)t + 0x2c);
    lit[0] = cfg.context;      // x0 = this
    lit[1] = cfg.playerAddr;   // x17 = player (skill id source)
    lit[2] = cfg.castFunc;     // x16 = cast function
    lit[3] = 0;                // scratch patched after mmap (base+0x1000)
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------
static const char* ErrStr() { return s_err; }
static bool Ready() { return s_installed && RC_PidAlive(s_pid); }
static int  InstalledPid() { return s_pid; }
static int  Fire(int pid);   // fwd (Install self-tests with Fire)

static int Install(int pid, const Cfg& cfg) {
    s_installed = false;
    s_base = 0;
    s_pid  = 0;
    SetErr("ok");

    if (!RC_PidAlive(pid)) { SetErr("pid dead"); return RC_FAIL; }

    // signal is fixed at RC_SIG (stub hardcodes it). A mismatched cfg.sig
    // makes the install self-test fail below, so this is fail-safe.
    int sig = cfg.sig ? (int)cfg.sig : RC_SIG;
    if (sig != RC_SIG) sig = RC_SIG;

    // 1. find executable region (rwxp preferred, r-xp fallback via ptrace poke)
    uintptr_t jit = 0;
    size_t jitSize = 0;
    bool writable = false;
    if (!RC_FindExecRegion(pid, &jit, &jitSize, &writable)) return RC_NO_RWX;

    // 2. attach
    if (!RC_Attach(pid)) return RC_FAIL;
    s_usePoke = !writable;

    // 3. save original bytes, write stubs (poke path for RX regions)
    uint8_t orig[RC_STUB_TOTAL];
    bool okRead  = writable ? RC_VmRead(pid, jit, orig, sizeof(orig))
                            : RC_PokeRead(pid, jit, orig, sizeof(orig));
    if (!okRead) {
        RC_Detach(pid);
        SetErr("cannot read region");
        return RC_FAIL;
    }
    uint32_t st1[12], st2[26], st3[8];
    RC_BuildStubs(st1, st2, st3);
    uint8_t scratch[RC_STUB_TOTAL];
    memset(scratch, 0, sizeof(scratch));
    memcpy(scratch, st1, sizeof(st1));
    memcpy(scratch + 0x40, st2, sizeof(st2));
    memcpy(scratch + 0xE0, st3, sizeof(st3));
    bool okWrite = writable ? RC_VmWrite(pid, jit, scratch, sizeof(scratch))
                            : RC_PokeWrite(pid, jit, scratch, sizeof(scratch));
    if (!okWrite) {
        RC_Detach(pid);
        SetErr("cannot write stubs");
        return RC_FAIL;
    }
    auto RestoreRegion = [&]() {
        if (writable) RC_VmWrite(pid, jit, orig, sizeof(orig));
        else RC_PokeWrite(pid, jit, orig, sizeof(orig));
    };

    // 4. phase 1: mmap
    RC_Regs r, out;
    if (!RC_GetRegs(pid, &r)) { RestoreRegion(); RC_Detach(pid); SetErr("getregs"); return RC_FAIL; }
    r.pc = jit;
    if (!RC_RunStub(pid, r, &out, cfg.timeoutMs)) {
        RestoreRegion(); RC_Detach(pid);
        return RC_FAIL;
    }
    uintptr_t base = (uintptr_t)out.regs[6];
    if (base == 0 || base >= 0x7fffffffffffULL) {
        RestoreRegion(); RC_Detach(pid);
        SetErr("mmap failed");
        return RC_FAIL;
    }

    // 5. write trampoline into page0 (mmap region is RWX until phase 2)
    uint32_t tramp[32];
    memset(tramp, 0, sizeof(tramp));
    RC_BuildTrampoline(tramp, cfg);
    ((uint64_t*)((uint8_t*)tramp + 0x44))[0] = base + RC_SCRATCH; // scratch addr
    if (!RC_VmWrite(pid, base, tramp, 0x4C)) {
        RestoreRegion(); RC_Detach(pid);
        SetErr("cannot write trampoline");
        return RC_FAIL;
    }

    // 6. phase 2: mprotect(RX) + rt_sigaction (oldact -> base+0x1100)
    r = out;
    r.pc  = jit + 0x40;
    r.regs[6]  = base;                       // x6 = region base
    r.regs[7]  = base;                       // x7 = handler (trampoline)
    r.regs[9]  = SA_RESTART_ARM64;           // x9 = sa_flags
    r.regs[26] = base + RC_OLDACT;           // x26 = oldact out (stub2: mov x2,x26)
    if (!RC_RunStub(pid, r, &out, cfg.timeoutMs)) {
        RestoreRegion(); RC_Detach(pid);
        return RC_FAIL;
    }
    if ((long)out.regs[7] != 0) {
        // rt_sigaction failed (x7 = result)
        RestoreRegion(); RC_Detach(pid);
        SetErr("rt_sigaction failed");
        return RC_FAIL;
    }

    // 7. restore region bytes, detach. TracerPid -> 0
    RestoreRegion();
    RC_Detach(pid);

    s_pid       = pid;
    s_base      = base;
    s_jit       = jit;
    s_sig       = sig;
    s_installed = true;
    memcpy(s_orig, orig, sizeof(orig));

    // 8. self-test: one fire must land
    if (Fire(pid) != 1) {
        s_installed = false;
        s_base = 0;
        s_pid  = 0;
        SetErr("self-test fire failed");
        return RC_FAIL;
    }
    SetErr("installed (signal trampoline, detach ok)");
    return RC_OK;
}

static int Fire(int pid) {
    if (!s_installed || pid != s_pid) return 0;
    uint32_t zero = 0;
    if (!RC_VmWrite(pid, s_base + RC_SCRATCH, &zero, 4)) return 0;
    // deliver ONLY to the main thread (tid == pid): game logic thread
    syscall(SYS_tgkill, pid, pid, s_sig);
    for (int i = 0; i < 60; i++) {              // up to ~300ms
        uint32_t v = 0;
        if (RC_VmRead(pid, s_base + RC_SCRATCH, &v, 4) && v == 1) {
            uint32_t z = 0;
            RC_VmWrite(pid, s_base + RC_SCRATCH, &z, 4);
            return 1;
        }
        usleep(5000);
    }
    return 0;
}

static void Uninstall(int pid) {
    if (!s_installed || pid != s_pid) return;
    if (!RC_PidAlive(pid)) { s_installed = false; s_base = 0; s_pid = 0; return; }
    if (!RC_Attach(pid)) { s_installed = false; s_base = 0; s_pid = 0; return; }

    // re-write stubs (region bytes were restored after install)
    uint32_t st1[12], st2[26], st3[8];
    RC_BuildStubs(st1, st2, st3);
    uint8_t scratch[RC_STUB_TOTAL];
    memset(scratch, 0, sizeof(scratch));
    memcpy(scratch, st1, sizeof(st1));
    memcpy(scratch + 0x40, st2, sizeof(st2));
    memcpy(scratch + 0xE0, st3, sizeof(st3));
    bool wroteStubs = s_usePoke ? RC_PokeWrite(pid, s_jit, scratch, sizeof(scratch))
                                : RC_VmWrite(pid, s_jit, scratch, sizeof(scratch));
    if (wroteStubs) {
        // phase 3: restore original handler + munmap trampoline region
        RC_Regs r, out;
        if (RC_GetRegs(pid, &r)) {
            r.pc       = s_jit + 0xE0;
            r.regs[0]  = s_sig;
            r.regs[1]  = s_base + RC_OLDACT;   // oldact saved at install
            r.regs[2]  = 0;
            r.regs[3]  = 8;
            r.regs[6]  = s_base;               // munmap base
            RC_RunStub(pid, r, &out, 1500);
        }
    }
    if (s_usePoke) RC_PokeWrite(pid, s_jit, s_orig, sizeof(s_orig));
    else RC_VmWrite(pid, s_jit, s_orig, sizeof(s_orig)); // restore region bytes
    RC_Detach(pid);

    s_installed = false;
    s_base = 0;
    s_jit  = 0;
    s_pid  = 0;
    s_usePoke = false;
}

} // namespace RC

#endif // __aarch64__
#endif // REMOTECALL_H