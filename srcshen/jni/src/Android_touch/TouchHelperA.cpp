#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cmath>
#include <linux/input.h>
#include <linux/uinput.h>

#include "imgui.h"

#define maxE 5
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

static void SendTapUp();   // forward decl (dipakai SendTapDown)

static void SendTapDown(int nx, int ny) {
    if (!Touch_initialized || nowfd <= 0) return;
    if (g_synDown) SendTapUp();
    struct input_event ev[10];
    int c = 0;
    ev[c++] = {EV_ABS, ABS_MT_SLOT, SYN_SLOT};
    ev[c++] = {EV_ABS, ABS_MT_TRACKING_ID, SYN_TRACKING};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_X, nx};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_Y, ny};
    ev[c++] = {EV_ABS, ABS_X, nx};
    ev[c++] = {EV_ABS, ABS_Y, ny};
    ev[c++] = {EV_SYN, SYN_MT_REPORT, 0};
    ev[c++] = {EV_KEY, BTN_TOUCH, 1};
    ev[c++] = {EV_KEY, BTN_TOOL_FINGER, 1};
    ev[c++] = {EV_SYN, SYN_REPORT, 0};
    (void)!write(nowfd, ev, (size_t)c * sizeof(struct input_event));
    g_synDown = true;
}

static void SendTapMove(int nx, int ny) {
    if (!Touch_initialized || nowfd <= 0 || !g_synDown) return;
    struct input_event ev[5];
    int c = 0;
    ev[c++] = {EV_ABS, ABS_MT_SLOT, SYN_SLOT};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_X, nx};
    ev[c++] = {EV_ABS, ABS_MT_POSITION_Y, ny};
    ev[c++] = {EV_ABS, ABS_X, nx};
    ev[c++] = {EV_ABS, ABS_Y, ny};
    ev[c++] = {EV_SYN, SYN_REPORT, 0};
    (void)!write(nowfd, ev, (size_t)c * sizeof(struct input_event));
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
    (void)!write(nowfd, ev, (size_t)c * sizeof(struct input_event));
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

static bool checkDeviceIsTouch(int fd);
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
        if (!Touch_readOnly && nowfd > 0) {
            ssize_t wrc = write(nowfd, inputEvent, (size_t) readSize);
            (void) wrc;
        }
    }
    return nullptr;
}


bool Touch_Init(int w, int h, uint32_t orientation_, bool readOnly) {
    char temp[128];
    DIR *dir = opendir("/dev/input/");
    dirent *ptr = NULL;
    int eventCount = 0;
    while ((ptr = readdir(dir)) != NULL) {
        if (strstr(ptr->d_name, "event"))
            eventCount++;
    }
    struct input_absinfo abs, absX[maxE], absY[maxE];
    int fd, i, tmp1, tmp2;
    int screenX, screenY, minCnt = eventCount + 1;
    fdNum = 0;
    for (i = 0; i <= eventCount; i++) {
        sprintf(temp, "/dev/input/event%d", i);
        fd = open(temp, O_RDWR);
        if (fd < 0) {
            continue;
        }
        if (checkDeviceIsTouch(fd)) {
            tmp1 = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX[fdNum]);
            tmp2 = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY[fdNum]);
            if (tmp1 == 0 && tmp2 == 0) {
                origfd[fdNum] = fd;
                if (!readOnly) {
                    ioctl(fd, EVIOCGRAB, GRAB);
                }
                if (i < minCnt) {
                    screenX = absX[fdNum].maximum;
                    screenY = absY[fdNum].maximum;
                    minCnt = i;
                }
                fdNum++;
                if (fdNum >= maxE)
                    break;
            }
        } else {
            close(fd);
        }
    }

    if (minCnt > eventCount) {
        puts("Failed init touch!");
        return false;
    }

    if (!readOnly) {
        struct uinput_user_dev ui_dev;
        nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (nowfd <= 0) {
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
        ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_SLOT);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
        ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
        ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
        ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
        ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
        ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);

        genRandomString(string, string_len);
        ioctl(nowfd, UI_SET_PHYS, string);

        sprintf(temp, "/dev/input/event%d", minCnt);
        fd = open(temp, O_RDWR);
        if (fd) {
            struct input_id id;
            if (!ioctl(fd, EVIOCGID, &id)) {
                ui_dev.id.bustype = id.bustype;
                ui_dev.id.vendor = id.vendor;
                ui_dev.id.product = id.product;
                ui_dev.id.version = id.version;
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

    system("chmod 000 -R /proc/bus/input/*");
    return true;
}
void UpdateScreenData(int w, int h, uint32_t orientation_) {
    ::screenWidth = w;
    ::screenHeight = h, 
    ::orientation = orientation_;
}

static bool checkDeviceIsTouch(int fd) {
    uint8_t *bits = NULL;
    ssize_t bits_size = 0;
    int res, j, k;
    bool itmp = false, itmp2 = false, itmp3 = false;
    struct input_absinfo abs{};
    while (true) {
        res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
        if (res < bits_size)
            break;
        bits_size = res + 16;
        bits = (uint8_t *) realloc(bits, bits_size * 2);
    }
    for (j = 0; j < res; j++) {
        for (k = 0; k < 8; k++)
            if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0) {
                if (j * 8 + k == ABS_MT_SLOT) {
                    itmp = true;
                    continue;
                }
                if (j * 8 + k == ABS_MT_POSITION_X) {
                    itmp2 = true;
                    continue;
                }
                if (j * 8 + k == ABS_MT_POSITION_Y) {
                    itmp3 = true;
                    continue;
                }
            }
    }
    free(bits);
    return itmp && itmp2 && itmp3;
}

void Touch_Close() {
    if (Touch_initialized) {
        for (int i = 0; i < maxE; ++i) {
            if (origfd[i] > 0) {
                if (!Touch_readOnly)
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
