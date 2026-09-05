#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <linux/input.h>
#include <linux/uinput.h>

#include "imgui.h"

#define maxE 16
#define maxF 10
#define UNGRAB 0
#define GRAB 1


bool other_touch; //其他触摸

// Kalibrasi retri: di-capture dari sentuhan asli di thread TypeA
bool g_retriCapture = false;
int  g_retriNativeX = -1;
int  g_retriNativeY = -1;
float g_retriLogicalX = -1.0f;
float g_retriLogicalY = -1.0f;

// Ukuran panel sentuh asli (native max X/Y). Dipakai main.cpp utk mapping
// dot -> native (Touch_PanelMaxX/Y).
static int s_panelMaxX = 0, s_panelMaxY = 0;
int Touch_PanelMaxX() { return s_panelMaxX; }
int Touch_PanelMaxY() { return s_panelMaxY; }

// Telemetri sentuhan asli (real finger -> ImGui). Dipakai diagnosa kalau
// menu ga bisa di-klik: kalau counter ini nambah = input nyampe, masalahnya
// di sisi ImGui/kordinat; kalau 0 = jalur grab/read mati.
long long g_realTouchDowns = 0;
bool  g_realTouchLogged = false;

// mode grabless: device asli cuma bisa dibuka READ-ONLY (SELinux/perm nolak
// O_RDWR). Real touch langsung ke game (ga di-grab), kita baca utk ImGui aja,
// tap sintetik tetap lewat uinput. g_touchOpenErrno = errno open O_RDWR terakhir.
static bool s_fdRw[maxE];
bool g_touchGrabless = false;
int  g_touchOpenErrno = 0;

// Diagnostics touch (dibaca UI utk debug)
bool g_touchDebugLog = true;
int  g_touchInitOk = 0;
int  g_touchFdCount = 0;
long long g_touchMirrorWrites = 0;
long long g_touchMirrorBytes = 0;
long long g_touchTapWrites = 0;
long long g_touchTapBytes = 0;
long long g_touchTapFails = 0;    // berapa kali write tap sintetik ditolak
int  g_touchLastErr = 0;
char g_touchDevName[64] = "";bool  g_touchWatchdogOn = true;    // watchdog re-disable block_untrusted_touches

// ===== v1.14: axis pelengkap dari device asli (hasil studi herz/ther) =====
// Driver sentuh asli (mtk-tpd dkk) selalu kirim ABS_MT_PRESSURE + TOUCH_MAJOR
// pas jari nempel. Tap sintetik kita harus nyaruain bentuk stream itu, kalau
// nggak InputReader/anti-cheat bisa anggap event aneh & buang. Di-copy dari
// device asli di FASE 2 (hanya kalau device punya axis-nya) -> guard aman.
static bool s_hasPressure = false;
static int  s_pressureMin = 0, s_pressureMax = 255;
static bool s_hasTouchMajor = false;
static int  s_touchMajorMax = 0;


static uint32_t orientation = 0;
static float screenHeight = 0, screenWidth = 0;

//TODO 触摸穿透

struct touchObj {
    bool isDown = false;
    int x = 0;
    int y = 0;
    int id = 0;
};

struct targ {
    int fdNum;
    float S2TX;
    float S2TY;
};

static targ targF[maxE];

static touchObj Finger[maxE][maxF];

static int fdNum = 0, origfd[maxE], nowfd;

static float scale_x, scale_y;

static bool Touch_initialized = false;

static bool Touch_readOnly = false;

// ===== Injector sentuhan sintetik (auto-retri) =====
// Penting: pakai SLOT sendiri (12) yg TIDAK dipakai driver asli, jadi tap sintetik
// TIDAK nyatu sama jari asli (mis. jempol kiri di joystick). Dulu digabung ke slot 0
// -> tap retri malah kaya gerak joystick / nyentuh tombol lain.
static const int SYN_SLOT     = 12;
static const int SYN_TRACKING = 0x4000;
static bool g_synDown = false;
static volatile int g_realContacts = 0;   // jari asli yg lagi nempel (buat BTN_TOUCH up)

// write helper dengan 1x retry (kernel/MTK kadang nolak sementara pas game
// baru re-enable block_untrusted_touches; retry 15ms kemudian sering lolos)
static ssize_t TapWrite(int fd, void* ev, size_t len, const char* tag) {
    ssize_t wrc = write(fd, ev, len);
    if (wrc < 0) {
        int e1 = errno;
        usleep(15000);
        wrc = write(fd, ev, len);
        if (wrc < 0) {
            g_touchTapFails++;
            if (g_touchDebugLog)
                printf("[TOUCH] %s write FAILED errno=%d (retry jg gagal, total fail %lld)\n",
                       tag, e1, (long long) g_touchTapFails);
            g_touchLastErr = e1;
        } else {
            if (g_touchDebugLog)
                printf("[TOUCH] %s write ok setelah retry (errno pertama=%d)\n", tag, e1);
            g_touchLastErr = 0;
        }
    } else {
        g_touchLastErr = 0;
    }
    return wrc;
}

static void SendTapUp();   // forward decl (dipakai SendTapDown)

static void SendTapDown(int nx, int ny) {
    if (!Touch_initialized || nowfd <= 0) return;
    if (g_synDown) SendTapUp();
    // v1.14: nyaruain stream driver asli (slot -> tracking -> pressure/major -> XY
    // -> SYN_MT_REPORT -> BTN -> SYN_REPORT). Urutan & axis pelengkap cuma dikirim
    // kalau device asli punya axis itu (guard -> ga ada risiko -EINVAL kayak v1.9).
    struct input_event ev[16];
    int c = 0;
    ev[c++] = {EV_ABS, ABS_MT_SLOT, SYN_SLOT};
    ev[c++] = {EV_ABS, ABS_MT_TRACKING_ID, SYN_TRACKING};
    if (s_hasPressure) {
        int pv = s_pressureMin + (s_pressureMax - s_pressureMin) * 3 / 5;
        ev[c++] = {EV_ABS, ABS_MT_PRESSURE, pv};
    }
    if (s_hasTouchMajor && s_touchMajorMax > 0) {
        ev[c++] = {EV_ABS, ABS_MT_TOUCH_MAJOR, s_touchMajorMax / 3 > 0 ? s_touchMajorMax / 3 : 1};
    }
    ev[c++] = {EV_ABS, ABS_MT_POSITION_X, nx};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_Y, ny};
    ev[c++] = {EV_ABS, ABS_X, nx};
    ev[c++] = {EV_ABS, ABS_Y, ny};
    ev[c++] = {EV_SYN, SYN_MT_REPORT, 0};
    ev[c++] = {EV_KEY, BTN_TOUCH, 1};
    ev[c++] = {EV_KEY, BTN_TOOL_FINGER, 1};
    ev[c++] = {EV_SYN, SYN_REPORT, 0};
    ssize_t wrc = TapWrite(nowfd, ev, (size_t)c * sizeof(struct input_event), "tap DOWN");
    if (wrc > 0) { g_touchTapWrites++; g_touchTapBytes += wrc; }
    if (g_touchDebugLog && wrc > 0)
        printf("[TOUCH] tap DOWN (%d,%d) fd=%d bytes=%zd\n", nx, ny, nowfd, wrc);
    g_synDown = true;
}

static void SendTapMove(int nx, int ny) {
    if (!Touch_initialized || nowfd <= 0 || !g_synDown) return;
    struct input_event ev[8];
    int c = 0;
    ev[c++] = {EV_ABS, ABS_MT_SLOT, SYN_SLOT};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_X, nx};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_Y, ny};
    ev[c++] = {EV_ABS, ABS_X, nx};
    ev[c++] = {EV_ABS, ABS_Y, ny};
    ev[c++] = {EV_SYN, SYN_REPORT, 0};
    ssize_t wrc = write(nowfd, ev, (size_t)c * sizeof(struct input_event));
    if (wrc > 0) { g_touchTapWrites++; g_touchTapBytes += wrc; }
    else if (wrc < 0) g_touchLastErr = errno;
}

static void SendTapUp() {
    if (!Touch_initialized || nowfd <= 0) return;
    if (!g_synDown) return;
    struct input_event ev[6];
    int c = 0;
    ev[c++] = {EV_ABS, ABS_MT_SLOT, SYN_SLOT};
    ev[c++] = {EV_ABS, ABS_MT_TRACKING_ID, -1};
    ev[c++] = {EV_SYN, SYN_MT_REPORT, 0};
    if (g_realContacts <= 0) {
        ev[c++] = {EV_KEY, BTN_TOUCH, 0};
        ev[c++] = {EV_KEY, BTN_TOOL_FINGER, 0};
    }
    ev[c++] = {EV_SYN, SYN_REPORT, 0};
    ssize_t wrc = TapWrite(nowfd, ev, (size_t)c * sizeof(struct input_event), "tap UP");
    if (wrc > 0) { g_touchTapWrites++; g_touchTapBytes += wrc; }
    if (g_touchDebugLog && wrc > 0)
        printf("[TOUCH] tap UP fd=%d bytes=%zd\n", nowfd, wrc);
    g_synDown = false;
}

bool Touch_Busy() {
    return g_realContacts > 0 || g_synDown;
}

// logical (layar overlay) -> native device coordinate
static void LogicalToNative(float xt, float yt, int *ox, int *oy) {
    float x = xt, y = yt;
    switch (orientation) {
        case 1:  x = screenHeight - yt; y = xt; break;
        case 2:  x = screenWidth - xt;  y = screenHeight - yt; break;
        case 3:  x = yt;                y = screenWidth - xt;  break;
        default: x = xt;                y = yt; break;
    }
    *ox = (int) (x * scale_x);
    *oy = (int) (y * scale_y);
}

// Caps device sentuh. mtXY = punya ABS_MT_POSITION_X&Y (EVIOCGABS sukses).
// slot = punya ABS_MT_SLOT (protocol type-B). Driver type-A (banyak mtk-tpd)
// ga punya slot -> jangan ditolak, cuma prioritasnya di bawah yg punya slot.
struct TouchCaps { bool slot, mtXY; };
static TouchCaps probeTouchCaps(int fd);
static void genRandomString(char *string, int length) {
    int flag, i;
    srand((unsigned) time(NULL) + length);
    for (i = 0; i < length - 1; i++) {
        flag = rand() % 3;
        switch (flag) {
            case 0:
                string[i] = 'A' + rand() % 26;
                break;
            case 1:
                string[i] = 'a' + rand() % 26;
                break;
            case 2:
                string[i] = '0' + rand() % 10;
                break;
            default:
                string[i] = 'x';
                break;
        }
    }
    string[length - 1] = '\0';
}


static void *TypeA(void *arg) {
    targ tmp = *(targ *) arg;
    int i = tmp.fdNum;
    float S2TX = tmp.S2TX;
    float S2TY = tmp.S2TY;
    int latest = 0;
    input_event inputEvent[64]{0};

    while (Touch_initialized) {
        auto readSize = (int32_t) read(origfd[i], inputEvent, sizeof(inputEvent));
        if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
            continue;
        }
        size_t count = size_t(readSize) / sizeof(input_event);
        for (size_t j = 0; j < count; j++) {
            input_event &ie = inputEvent[j];
            if (ie.type == EV_ABS) {
                if (ie.code == ABS_MT_SLOT) {
                    latest = ie.value;
                    continue;
                }
                if (ie.code == ABS_MT_TRACKING_ID) {
                    bool was = Finger[i][latest].isDown;
                    bool nowDown = (ie.value != -1);
                    if (was != nowDown) {
                        g_realContacts += nowDown ? 1 : -1;
                        if (g_realContacts < 0) g_realContacts = 0;
                    }
                    if (nowDown) {
                        Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                        Finger[i][latest].isDown = true;
                    } else {
                        Finger[i][latest].isDown = false;
                    }
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X) {
                    Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                    Finger[i][latest].x = (int) (ie.value * S2TX);
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y) {
                    Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                    Finger[i][latest].y = (int) (ie.value * S2TY);
                    continue;
                }
            }
            if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
                ImGuiIO &io = ImGui::GetIO();
                if (Finger[i][latest].isDown) {
                    float x = Finger[i][latest].x, y = Finger[i][latest].y;
                    float xt = x / scale_x;
                    float yt = y / scale_y;
                    /* 
                    LOGD("orien %d orig x%.1f y%.1f\n", orientation, x, y);
                    LOGD("xt %.1f yt %.1f\n", xt, yt);
                    */
                    if (other_touch) {
                        switch (orientation) {
                            case 1:
                                x = xt;
                                y = yt;
                                break;
                            case 2:
                                y = yt;
                                x = screenHeight - xt;
                                break;
                            case 3:
                                x = screenHeight - xt;
                                y = screenWidth - yt;
                                break;
                            default:
                                y = xt;
                                x = screenHeight - yt;
                                break;
                        }
                    } else {
                        switch (orientation) {
                            case 1:
                                x = yt;
                                y = screenHeight - xt;
                                break;
                            case 2:
                                x = screenHeight - xt;
                                y = screenWidth - yt;
                                break;
                            case 3:
                                y = xt;
                                x = screenWidth - yt;
                                break;
                            default:
                                x = xt;
                                y = yt;
                                break;
                        }
                    }
                    io.MousePos = {x, y};
                    // LOGD("final %d %.1f %.1f\n", other_touch, x, y);
                    io.MouseDown[0] = true;
                    g_realTouchDowns++;
                    if (!g_realTouchLogged) {
                        g_realTouchLogged = true;
                        printf("[TOUCH] sentuhan asli diterima -> ImGui bisa di-klik (x=%.0f y=%.0f)\n", x, y);
                    }
                    // Kalibrasi 1-tap: simpan posisi native + logical sentuhan asli ini
                    if (g_retriCapture) {
                        g_retriNativeX = Finger[i][latest].x;
                        g_retriNativeY = Finger[i][latest].y;
                        g_retriLogicalX = x;
                        g_retriLogicalY = y;
                        g_retriCapture = false;
                    }
                } else {
                    io.MouseDown[0] = false;
                    //  LOGD("抬起");
                }
                continue;
            }
        }
        // Verbatim mirror: teruskan event asli apa adanya (ABS_MT_SLOT, TRACKING_ID,
        // urutan & timing driver asli) ke uinput clone. Game terima stream identik dgn
        // kondisi tanpa overlay -> gesture/flick MLBB (quick emote, slide chat) tetap
        // natural. Tap sintetik (retri) injeksi lewat slot terpisah (SendTap*).
        // Mode grabless: TIDAK mirror (device asli ga di-grab -> real touch udah
        // sampai game sendiri; kalau kita mirror bakal dobel).
        if (!Touch_readOnly && !g_touchGrabless && nowfd > 0) {
            ssize_t wrc = write(nowfd, inputEvent, (size_t) readSize);
            if (wrc > 0) { g_touchMirrorWrites++; g_touchMirrorBytes += wrc; }
            else if (wrc < 0) g_touchLastErr = errno;
        }
    }
    return nullptr;
}


// Android 12+ memblokir sentuhan dari perangkat uinput sbg "untrusted" kecuali
// setting ini dimatikan (persis yg dilakukan herz-kimmy.sh biar retri/touch jalan).
static void DisableUntrustedTouchBlock() {
    system("settings put global block_untrusted_touches 0 > /dev/null 2>&1");
    system("settings put secure block_untrusted_touches 0 > /dev/null 2>&1");
    system("settings put system block_untrusted_touches 0 > /dev/null 2>&1");
}

// ===== Discovery universal device sentuh =====
// 1) scan /dev/input/event* (standar). 2) fallback parse /proc/bus/input/devices
// (beberapa ROM/SELinux sembunyikan /dev/input atau penomoran event beda).
// Return: jumlah path event ditemukan (max maxE), isi evpath[] dgn "/dev/input/eventN".
static int DiscoverTouchDevices(char evpath[maxE][64]) {
    int found = 0;
    // --- cara 1: /dev/input/event* ---
    DIR *dir = opendir("/dev/input/");
    if (dir) {
        dirent *ptr;
        while ((ptr = readdir(dir)) != NULL && found < maxE) {
            if (strncmp(ptr->d_name, "event", 5) != 0) continue;
            char *end;
            long num = strtol(ptr->d_name + 5, &end, 10);
            if (end == ptr->d_name + 5 || *end != '\0') continue; // eventX aja, skip yg aneh
            if (num < 0 || num > 4096) continue;
            snprintf(evpath[found], 64, "/dev/input/event%ld", num);
            found++;
        }
        closedir(dir);
    } else {
        printf("[TOUCH] /dev/input/ ga bisa dibuka, coba /proc/bus/input/devices...\n");
    }
    // --- cara 2: fallback /proc/bus/input/devices ---
    if (found == 0) {
        FILE *fp = fopen("/proc/bus/input/devices", "r");
        if (fp) {
            char line[512];
            char handlers[512] = "";
            bool hasAbsMT = false;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] == 'I' || line[0] == 'N' || line[0] == 'P' ||
                    line[0] == 'S' || line[0] == 'U' || line[0] == 'B') {
                    if (strncmp(line, "H: Handlers", 11) == 0) {
                        snprintf(handlers, sizeof(handlers), "%s", line + 12);
                    } else if (strncmp(line, "B: ABS", 6) == 0) {
                        // bit ABS_MT_POSITION_X (0x35) & ABS_MT_POSITION_Y (0x36)
                        if (strstr(line, "35000000") || strstr(line, "3f000000") ||
                            strstr(line, "3f") || strstr(line, "ffffffff"))
                            hasAbsMT = true;
                    }
                } else if (line[0] == '\n' || line[0] == '\r' || line[0] == 0) {
                    // akhir blok device: cek punya eventN + abs-mt
                    if (hasAbsMT && handlers[0]) {
                        char *tok = strtok(handlers, " ");
                        while (tok && found < maxE) {
                            if (strncmp(tok, "event", 5) == 0) {
                                char *end;
                                long num = strtol(tok + 5, &end, 10);
                                if (end != tok + 5 && *end == '\0' && num >= 0 && num <= 4096) {
                                    snprintf(evpath[found], 64, "/dev/input/event%ld", num);
                                    found++;
                                }
                            }
                            tok = strtok(NULL, " ");
                        }
                    }
                    handlers[0] = '\0';
                    hasAbsMT = false;
                }
            }
            fclose(fp);
        } else {
            printf("[TOUCH] /proc/bus/input/devices juga ga bisa dibaca\n");
        }
    }
    return found;
}

// Watchdog: MLBB/Aegis suka re-enable block_untrusted_touches=1 di runtime
// (gejala: tap pertama jalan, terus ENOSYS). Thread ini re-disable tiap 1.5s
// selama fitur touch aktif biar sentuhan sintetik kita tetep diterima.
static void *UntrustedWatchdog(void *) {
    while (g_touchWatchdogOn && Touch_initialized) {
        usleep(1500000);   // 1.5s
        DisableUntrustedTouchBlock();
        // verifikasi: kalau masih ada yg re-enable, errno tap akan muncul terus.
        if (g_touchTapFails > 0 && g_touchDebugLog)
            printf("[TOUCH] watchdog: tap fails=%lld (block kemungkinan di-re-enable game, sudah di-disable lagi)\n",
                   (long long) g_touchTapFails);
    }
    return nullptr;
}

bool Touch_Init(int w, int h, uint32_t orientation_, bool readOnly) {
    if (!readOnly) DisableUntrustedTouchBlock();
    char evpath[maxE][64];
    int evCount = DiscoverTouchDevices(evpath);
    struct input_absinfo absX[maxE], absY[maxE];
    int fd, i;
    int screenX = 0, screenY = 0;
    fdNum = 0;
    // ===== FASE 1: cari & buka device sentuh (BELUM di-grab) =====
    // Prioritas O_RDWR (biar bisa di-grab + mirror). Kalau semua nolak (biasanya
    // SELinux/perm di ROM tertentu), fallback O_RDONLY -> mode grabless: real
    // touch tetap ke game langsung, kita baca buat ImGui, tap sintetik via uinput.
    // Gagal O_RDWR di-simpan errno-nya buat diagnosa.
    // Probe tiap node: buka RW dulu, kalau nolak baru RO. Klasifikasi caps per
    // node (slot type-B? punya sumbu MT X/Y?). Device type-A (banyak mtk-tpd,
    // ROM AOSP) sering TIDAK punya ABS_MT_SLOT -> jangan ditolak mentah2.
    int rwFd[maxE], roFd[maxE];
    bool rwSlot[maxE], roSlot[maxE];
    int rwCount = 0, roCount = 0;
    int rwFailErr = 0, openRwOk = 0, diagRej = 0;
    g_touchOpenErrno = 0;
    for (i = 0; i < evCount; i++) {
        errno = 0;
        fd = open(evpath[i], O_RDWR | O_NONBLOCK);
        if (fd >= 0) {
            openRwOk++;
            TouchCaps c = probeTouchCaps(fd);
            if (c.mtXY) {
                if (rwCount < maxE) { rwFd[rwCount] = fd; rwSlot[rwCount] = c.slot; rwCount++; }
                else close(fd);
            } else { diagRej++; close(fd); }
            continue;
        }
        if (!rwFailErr) rwFailErr = errno;
        int rfd = open(evpath[i], O_RDONLY | O_NONBLOCK);
        if (rfd >= 0) {
            TouchCaps c = probeTouchCaps(rfd);
            if (c.mtXY) {
                if (roCount < maxE) { roFd[roCount] = rfd; roSlot[roCount] = c.slot; roCount++; }
                else close(rfd);
            } else close(rfd);
        }
    }
    // Prioritas: RW+slot > RW+no-slot > RO+slot > RO+no-slot (grabless).
    // Thread baca (TypeA) aman utk no-slot: index slot default 0.
    bool usedRo = false;
    auto claim = [&](int cfd, bool rw) {
        if (fdNum >= maxE) return;
        if (ioctl(cfd, EVIOCGABS(ABS_MT_POSITION_X), &absX[fdNum]) != 0 ||
            ioctl(cfd, EVIOCGABS(ABS_MT_POSITION_Y), &absY[fdNum]) != 0) { close(cfd); return; }
        if (fdNum == 0) {
            char nm[64] = "";
            if (ioctl(cfd, EVIOCGNAME(sizeof(nm) - 1), nm) >= 0)
                strncpy(g_touchDevName, nm, sizeof(g_touchDevName) - 1);
            screenX = absX[0].maximum;
            screenY = absY[0].maximum;
            s_panelMaxX = screenX;
            s_panelMaxY = screenY;
        }
        s_fdRw[fdNum] = rw;
        origfd[fdNum] = cfd;
        fdNum++;
    };
    for (int pass = 0; pass < 4 && fdNum < maxE; pass++) {
        bool wantRw = (pass < 2);
        bool wantSlot = (pass % 2 == 0);
        int  *fromFd   = wantRw ? rwFd : roFd;
        bool *fromSlot = wantRw ? rwSlot : roSlot;
        int   fromN    = wantRw ? rwCount : roCount;
        for (i = 0; i < fromN && fdNum < maxE; i++) {
            if (fromSlot[i] == wantSlot && fromFd[i] > 0) {
                claim(fromFd[i], wantRw);
                fromFd[i] = -1;
                if (!wantRw) usedRo = true;
            }
        }
    }
    for (i = 0; i < rwCount; i++) if (rwFd[i] > 0) close(rwFd[i]);
    for (i = 0; i < roCount; i++) if (roFd[i] > 0) close(roFd[i]);

    if (fdNum <= 0) {
        printf("[TOUCH] FAILED: tidak ada device sentuh ditemukan (%d node, RW terbuka=%d ditolak-cap=%d errno=%d). Kirim log ini + \"getevent -pl\" utk diagnosa.\n",
               evCount, openRwOk, diagRej, rwFailErr ? rwFailErr : g_touchOpenErrno);
        return false;
    }
    if (usedRo) {
        g_touchOpenErrno = rwFailErr;
        printf("[TOUCH] WARNING: /dev/input cuma bisa dibuka READ-ONLY (errno=%d). Mode GRABLESS: real touch langsung ke game, ImGui dari baca aja, tap sintetik via uinput.\n",
               rwFailErr);
    }

    // ===== FASE 2: buat uinput DULU (baru grab device asli) =====
    // Kalau uinput gagal -> device asli TIDAK di-grab -> game tetap bisa disentuh.
    if (!readOnly) {
        struct uinput_user_dev ui_dev;
        memset(&ui_dev, 0, sizeof(ui_dev));
        nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (nowfd <= 0) {
            printf("[TOUCH] FAILED: /dev/uinput ga bisa dibuka (errno=%d). Touch asli TIDAK di-grab, game tetap normal. Fitur tap sintetik nonaktif.\n", errno);
            for (i = 0; i < fdNum; i++) {
                close(origfd[i]);
                origfd[i] = 0;
            }
            fdNum = 0;
            return false;
        }

        int string_len = rand() % 10 + 5;
        char string[string_len];
        memset(&ui_dev, 0, sizeof(ui_dev));

        genRandomString(string, string_len);
        strncpy(ui_dev.name, string, UINPUT_MAX_NAME_SIZE);

        ui_dev.id.bustype = 0;
        ui_dev.id.vendor = rand() % 10 + 5;
        ui_dev.id.product = rand() % 10 + 5;
        ui_dev.id.version = rand() % 10 + 5;

        ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

        ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
        ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
        ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
        ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
        ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);

        // BASE ABS yang selalu kita inject (tap sintetik slot 12 + koordinat).
        // Wajib terdaftar di uinput, kalau tidak kernel tolak batch write -> -EINVAL.
        ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_SLOT);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
        ui_dev.absmin[ABS_X] = 0; ui_dev.absmax[ABS_X] = screenX;
        ui_dev.absmin[ABS_Y] = 0; ui_dev.absmax[ABS_Y] = screenY;
        ui_dev.absmin[ABS_MT_SLOT] = 0; ui_dev.absmax[ABS_MT_SLOT] = 15;
        ui_dev.absmin[ABS_MT_POSITION_X] = 0; ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
        ui_dev.absmin[ABS_MT_POSITION_Y] = 0; ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
        ui_dev.absmin[ABS_MT_TRACKING_ID] = 0; ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;

        genRandomString(string, string_len);
        ioctl(nowfd, UI_SET_PHYS, string);

        // salin id + abs/key bits dari device sentuh asli (pakai fd yg sudah terbuka)
        fd = origfd[0];
        if (fd > 0) {
            struct input_id id;
            if (!ioctl(fd, EVIOCGID, &id)) {
                ui_dev.id.bustype = id.bustype;
                ui_dev.id.vendor = id.vendor;
                ui_dev.id.product = id.product;
                ui_dev.id.version = id.version;
            }
            // Salin SEMUA kode ABS tambahan + range dari device asli (ABS_MT_PRESSURE,
            // ABS_MT_TOUCH_MAJOR, dll). Penting: mirror verbatim meneruskan event apa
            // adanya - kalau kode itu ga didaftarkan, kernel nolak SELURUH batch write
            // -> game ga nerima sentuhan sama sekali (regresi touch di v0.5+).
            // Buffer tetap cukup (ABS_CNT=64 -> 8 bytes; kita kasih 16).
            uint8_t abits[16];
            memset(abits, 0, sizeof(abits));
            int res2 = (int) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abits)), abits);
            if (res2 > 0) {
                struct input_absinfo ai;
                for (int j = 0; j < res2 && j < (int) sizeof(abits); j++) {
                    for (int k = 0; k < 8; k++) {
                        if (!(abits[j] & (1 << k))) continue;
                        int code = j * 8 + k;
                        if (code >= ABS_MAX) continue;
                        if (code == ABS_X || code == ABS_Y || code == ABS_MT_SLOT ||
                            code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y ||
                            code == ABS_MT_TRACKING_ID) continue; // sudah di-set di atas
                        if (ioctl(nowfd, UI_SET_ABSBIT, code) != 0) continue;
                        if (ioctl(fd, EVIOCGABS(code), &ai) == 0) {
                            ui_dev.absmin[code] = ai.minimum;
                            ui_dev.absmax[code] = ai.maximum;
                            ui_dev.absfuzz[code] = ai.fuzz;
                            ui_dev.absflat[code] = ai.flat;
                            if (code == ABS_MT_PRESSURE) {
                                s_hasPressure = true;
                                s_pressureMin = ai.minimum;
                                s_pressureMax = ai.maximum;
                            } else if (code == ABS_MT_TOUCH_MAJOR) {
                                s_hasTouchMajor = true;
                                s_touchMajorMax = ai.maximum;
                            }
                        }
                    }
                }
            }

            uint8_t *bits = NULL;
            ssize_t bits_size = 0;
            int res, j, k;
            while (1) {
                res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
                if (res < bits_size)
                    break;
                bits_size = res + 16;
                bits = (uint8_t *) realloc(bits, bits_size * 2);
            }
            for (j = 0; j < res; j++) {
                for (k = 0; k < 8; k++)
                    if (bits[j] & 1 << k) {
                        if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
                            continue;
                        ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
                    }
            }
            free(bits);
        }
        ui_dev.absmin[ABS_MT_POSITION_X] = 0;
        ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
        ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
        ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
        ui_dev.absmin[ABS_X] = 0;
        ui_dev.absmax[ABS_X] = screenX;
        ui_dev.absmin[ABS_Y] = 0;
        ui_dev.absmax[ABS_Y] = screenY;
        ui_dev.absmin[ABS_MT_TRACKING_ID] = 0;
        ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;
        ui_dev.absmin[ABS_MT_SLOT] = 0;
        ui_dev.absmax[ABS_MT_SLOT] = 15;
        write(nowfd, &ui_dev, sizeof(ui_dev));

        if (ioctl(nowfd, UI_DEV_CREATE)) {
            return false;
        }
    }
    // ===== FASE 3: grab device asli (cuma yg kebuka R/W) + start thread =====
    g_touchGrabless = true;
    if (!readOnly) {
        for (i = 0; i < fdNum; i++) {
            if (!s_fdRw[i]) continue;
            g_touchGrabless = false;
            ioctl(origfd[i], EVIOCGRAB, GRAB);
        }
    } else {
        g_touchGrabless = true;
    }
    Touch_initialized = true;
    Touch_readOnly = readOnly;

    pthread_t t;
    for (i = 0; i < fdNum; i++) {
        targF[i].fdNum = i;
        targF[i].S2TX = (float) screenX / (float) absX[i].maximum;
        targF[i].S2TY = (float) screenY / (float) absY[i].maximum;
        pthread_create(&t, NULL, TypeA, &targF[i]);
    }
    //LOGD("fdNum %d", fdNum);

    ::screenWidth = w;
    ::screenHeight = h, 
    ::orientation = orientation_;
    if (::orientation == 1 || ::orientation == 3) {
        ::scale_x = (float) screenX / h;
        ::scale_y = (float) screenY / w;
    } else {
        ::scale_x = (float) screenX / w;
        ::scale_y = (float) screenY / h;    
    }

    g_touchInitOk = 1;
    g_touchFdCount = fdNum;
    printf("[TOUCH] init OK: %d device mode=%s [%s], uinput fd=%d, screen %dx%d orient=%u scale=%.2f/%.2f\n",
           fdNum, g_touchGrabless ? "GRABLESS" : "GRAB",
           g_touchDevName[0] ? g_touchDevName : "?", nowfd, screenX, screenY,
           (unsigned) orientation_, scale_x, scale_y);
    if (!g_touchGrabless)
        system("chmod 000 -R /proc/bus/input/*");

    // watchdog re-disable block_untrusted_touches (counter anti-cheat re-enable)
    if (!readOnly) {
        g_touchWatchdogOn = true;
        pthread_t wt;
        if (pthread_create(&wt, nullptr, UntrustedWatchdog, nullptr) == 0)
            pthread_detach(wt);
    }
    return true;
}
void UpdateScreenData(int w, int h, uint32_t orientation_) {
    ::screenWidth = w;
    ::screenHeight = h, 
    ::orientation = orientation_;
}

static TouchCaps probeTouchCaps(int fd) {
    TouchCaps c{false, false};
    uint8_t *bits = NULL;
    ssize_t bits_size = 0;
    int res, j, k;
    struct input_absinfo abs{};
    bool hx = false, hy = false;
    while (true) {
        res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
        if (res < bits_size)
            break;
        bits_size = res + 16;
        bits = (uint8_t *) realloc(bits, bits_size * 2);
    }
    for (j = 0; j < res; j++) {
        for (k = 0; k < 8; k++) {
            if (!(bits[j] & (1 << k)))
                continue;
            int code = j * 8 + k;
            if (code == ABS_MT_SLOT) {
                c.slot = true;
                continue;
            }
            if (code != ABS_MT_POSITION_X && code != ABS_MT_POSITION_Y)
                continue;
            if (ioctl(fd, EVIOCGABS(code), &abs) == 0) {
                if (code == ABS_MT_POSITION_X) hx = true;
                else hy = true;
            }
        }
    }
    free(bits);
    c.mtXY = hx && hy;
    return c;
}

void Touch_Close() {
    g_touchWatchdogOn = false;
    if (Touch_initialized) {
        for (int i = 0; i < maxE; ++i) {
            if (origfd[i] > 0) {
                if (!Touch_readOnly && s_fdRw[i])
                    ioctl(origfd[i], EVIOCGRAB, UNGRAB);
                close(origfd[i]);
                origfd[i] = 0;
            }
        }
        if (nowfd > 0) {
            ioctl(nowfd, UI_DEV_DESTROY);
            close(nowfd);
            nowfd = 0;
        }
        fdNum = 0;
        memset(Finger, 0, sizeof(Finger));
        Touch_initialized = false;
    }
}

void Touch_Down(float xt, float yt) {
    int x, y;
    LogicalToNative(xt, yt, &x, &y);
    SendTapDown(x, y);
}

void Touch_Move(float xt, float yt) {
    int x, y;
    LogicalToNative(xt, yt, &x, &y);
    SendTapMove(x, y);
}

void Touch_Up() {
    SendTapUp();
}

void Touch_TapNative(int x, int y, int holdMs) {
    SendTapDown(x, y);
    usleep((useconds_t)(holdMs > 0 ? holdMs : 80) * 1000);
    SendTapUp();
}
