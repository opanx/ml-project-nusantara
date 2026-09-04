#include "main.h"
#include <linux/input.h>
#include <linux/uinput.h>
#include <vector>
#include <functional>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <iostream>
#include <fstream>
#include <sys/system_properties.h>
#include <ctime>
#include <string>
#include <iostream>
#include <main.h>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/input.h>
#include <vector>
#include <functional>
#include "Memory/Memory.h"
#include "Memory/PatternScanner.h"

#include "Quaternion.hpp"
#include "Vector2.hpp"
#include "Vector3.hpp"

#include "Includes/Log.h"
#include <vector>
#include "Includes/Offset.h"
#include "Engine/CanvasView.h"
#include "include.h"
#include "Matrix4x4.hpp"
#include "ToString.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "icon/HeroIcons.h"
#include "Decoder64.h"
#include "DrawIconHero.h"

using namespace Memory;

bool main_thread_flag = true;
bool g_menuMinimized = false;   // true = menu di-collapse jadi pill kecil
int abs_ScreenX = 0;
int abs_ScreenY = 0;
bool drawMAddress;
bool drawMBox = true;
bool drawMLine = true;
bool drawMPostion = true;
bool drawMHealth = true;
bool drawMDistance = true;
bool drawMName = true;
bool drawAlertUnderAttack = true;
bool iconhero = true;
int g_ICSize = 38;        // slider ukuran icon hero (dipakai DrawMonster di atas deklarasi baris ini)
bool drawMHealthBar = true;
bool drawMMpBar = false;
bool drawMSkillCD = false;
bool droneView = false;
float droneHeight = 18.0f;
bool langEN = true;
int g_langSel = 0;      // 0=English 1=Indonesia (default English)
float RadiusCir = 50.0f;
long libbase = 0;

// v0.4: keamanan & filter
bool safeMode = true;      // true = TIDAK ada memory write ke game (anti-ban)
bool g_showTeam = false;   // true = tampilkan teammate (default: musuh doang)
bool g_campCheck = true;   // fallback deteksi tim via iCamp kalau flag same-camp meleset
float retriDotSize = 26.0f; // radius lingkaran penanda posisi retri

// Kalibrasi touch retri (native capture dari TouchHelperA)
extern bool g_retriCapture;
extern int  g_retriNativeX;
extern int  g_retriNativeY;
extern float g_retriLogicalX;
extern float g_retriLogicalY;
extern void Touch_TapNative(int x, int y, int holdMs);
extern bool Touch_Busy();

// i18n: EN / 中文
#define TR(en, cn) (langEN ? (en) : (cn))

std::string fshy(uintptr_t address)
{
    if (!address) return "";

    auto stringLength = Read<uint32_t>(address + 0x10);
    char16_t buffer[255] = { 0 };

    pvm(reinterpret_cast<void *>(address + 0x14), reinterpret_cast<void *>(buffer), static_cast<size_t>(stringLength) * 2, false);

    return utf16_to_utf8(buffer, stringLength);
}

struct RoomPlayerInfo {
    std::string Name;
    std::string UserID;
    std::string Squad;
    std::string Rank;
    std::string Hero;
    std::string Spell;
};

RoomPlayerInfo PlayerB[5];
RoomPlayerInfo PlayerR[5];

struct String {
    char pad_0000[0x10];
    int length;
    wchar_t buffer[1];

    const char* CString() const {
        static char temp[256];
        wcstombs(temp, buffer, length);
        temp[length] = '\0';
        return temp;
    }
};


uintptr_t GetMainCamera() {
    // GameMethod (slot 0x62fb618) -> static_fields(+0xa8) -> m_Camra (field 0x8)
    auto klass = Read<uintptr_t>(libbase + 0x62fb618);
    if (!klass)
        return 0;
    auto statics = Read<uintptr_t>(klass + 0xa8);
    if (!statics)
        return 0;
    auto main_cam = Read<uintptr_t>(statics + 0x8); // GameMethod.m_Camra
    if (!main_cam)
        return 0;
    return main_cam;
}

uintptr_t GetSmoothFollow() {
    // GameMethod.mainCamera (SmoothFollow, static field 0x10)
    auto klass = Read<uintptr_t>(libbase + 0x62fb618);
    if (!klass) return 0;
    auto statics = Read<uintptr_t>(klass + 0xa8);
    if (!statics) return 0;
    return Read<uintptr_t>(statics + 0x10); // mainCamera : SmoothFollow
}

// ===== drone view via FOV kamera native =====
// Unity native Camera: m_CachedPtr (managed+0x18) -> object native, FOV float di 0x54-0x60
// scan sekali, cache offset
int  g_fovOffset = -1;
float g_defaultFov = 60.0f;

uintptr_t GetNativeCamera() {
    auto cam = GetMainCamera();
    if (!cam) return 0;
    auto native = Read<uintptr_t>(cam + 0x18); // m_CachedPtr
    return (native > 0x1000) ? native : 0;
}

bool FindFovOffset() {
    if (g_fovOffset > 0) return true;
    auto nc = GetNativeCamera();
    if (!nc) return false;
    // scan float 25..110 di offset 0x40..0xC0
    for (int off = 0x40; off <= 0xC0; off += 4) {
        float v = Read<float>(nc + off);
        if (v >= 45.0f && v <= 75.0f && v != 0.0f) {
            g_fovOffset = off;
            g_defaultFov = v;
            return true;
        }
    }
    return false;
}

void ApplyDroneView() {
    if (safeMode) return;   // safe mode: jangan pernah write memory game
    if (!droneView) {
        // restore FOV sekali
        if (g_fovOffset > 0) {
            auto nc = GetNativeCamera();
            if (nc) vm_writev(nc + g_fovOffset, &g_defaultFov, 4);
            g_fovOffset = -1; // rescan next time
        }
        return;
    }
    if (!FindFovOffset()) return;
    auto nc = GetNativeCamera();
    if (!nc) return;
    // FOV kecil = zoom out (drone). slider "tinggi" dipetakan: tinggi besar = FOV kecil
    float fov = g_defaultFov - (droneHeight / 60.0f) * (g_defaultFov - 25.0f);
    if (fov < 20.0f) fov = 20.0f;
    vm_writev(nc + g_fovOffset, &fov, 4);
}

// skill CD musuh: ShowCoolDownComp(ShowEntity+0xf8) -> _dicCD(0x18) Dictionary<Int32,CoolDownData>
// slotReady[]: 3 slot (S1, S2, ULT) — true kalau ready; slotCd[]: sisa detik
// mapping: spellID % 3 = urutan (kasar: game pakai ID berurut), fallback: sort by spellID
void GetEnemySkillCD(uintptr_t entity, bool slotReady[3], int slotCd[3]) {
    for (int i = 0; i < 3; i++) { slotReady[i] = true; slotCd[i] = 0; }
    auto comp = Read<uintptr_t>(entity + 0xf8); // m_ShowCoolDownComp
    if (!comp) return;
    auto dic = Read<uintptr_t>(comp + 0x18);    // _dicCD
    if (!dic) return;
    // il2cpp Dictionary: +0x18 _entries(array), +0x20 _count(int)
    auto entries = Read<uintptr_t>(dic + 0x18);
    int count = Read<int>(dic + 0x20);
    if (!entries || count <= 0 || count > 64) return;
    int alen = Read<int>(entries + 0x18);      // array length
    if (alen < count || alen > 128) return;

    // entry layout (il2cpp Dictionary<TKey,TVal>.Entry): hashCode@0, next@4, key@8, value@0x10; stride 0x18
    uintptr_t base = entries + 0x20;
    const int stride = 0x18;

    int ids[16];
    int cds[16];
    int n = 0;
    for (int i = 0; i < count && n < 16; i++) {
        auto e = base + i * stride;
        int spellID = Read<int>(e + 8);
        auto cd = Read<uintptr_t>(e + 0x10);
        if (!spellID || !cd) continue;
        bool cooling = Read<bool>(cd + 0x20);           // m_isCoolDown
        uint32_t remain = 0;
        if (cooling) {
            uint32_t coolTime = Read<uint32_t>(cd + 0x14);  // uiCoolTime
            uint32_t startTime = Read<uint32_t>(cd + 0x1c); // uiStartTime
            uint32_t now = (uint32_t)time(nullptr) * 1000;
            if (coolTime > 0 && startTime > 0) {
                uint32_t elapsed = now - startTime;
                remain = (elapsed >= coolTime) ? 0 : (coolTime - elapsed) / 1000;
            }
        }
        ids[n] = spellID;
        cds[n] = (int)remain;
        n++;
    }
    if (n == 0) return;
    // sort by spellID (ID kecil = skill1, ID besar = ult)
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (ids[j] < ids[i]) { int t=ids[i]; ids[i]=ids[j]; ids[j]=t; int u=cds[i]; cds[i]=cds[j]; cds[j]=u; }
    // ambil 3 terakhir (skill2, skill1, ult) atau sesuai jumlah
    int start = (n >= 3) ? n - 3 : 0;
    int k = 0;
    for (int i = start; i < n && k < 3; i++, k++) {
        slotReady[k] = (cds[i] == 0);
        slotCd[k] = cds[i];
    }
}


struct Camera {
    Matrix4x4 worldToCameraMatrix;
    Matrix4x4 projectionMatrix;
};

Matrix4x4 _vMatrix;

bool WorldToScreen(Vector3 from, Vector2 *to) {
    auto viewMatrix = _vMatrix.MultiplyPoint(from);
    auto screenPos = Vector3(viewMatrix.X + 1.0f, viewMatrix.Y + 1.0f, viewMatrix.Z + 1.0f) / 2.0f;
    *to = Vector2(screenPos.X * abs_ScreenX, abs_ScreenY - (screenPos.Y * abs_ScreenY));
    return viewMatrix != Vector3::Zero();
}

void FindPoint(Vector2 origin, Vector2 &point, int screenwidth, int screenheight, int length)
{
    float halfScreenWidth = screenwidth / 2.0f;
    float halfScreenHeight = screenheight / 2.0f;
    float halfScreenWidth2 = (screenwidth - length) / 2.0f;
    float halfScreenHeight2 = (screenheight - length) / 2.0f;
    float dx = fabs(origin.X - halfScreenWidth);
    float dy = fabs(origin.Y - halfScreenHeight);
    float rx = (dx != 0) ? halfScreenWidth2 / dx : 0;
    float ry = (dy != 0) ? halfScreenHeight2 / dy : 0;
    float r = fmin(rx, ry);
    point.X = origin.X + (halfScreenWidth - origin.X) * (1.0f - r);
    point.Y = origin.Y + (halfScreenHeight - origin.Y) * (1.0f - r);
}

int ListMonsterId[] = {
        2002,
        2003,
        2004,
        2005,
        2006,
        2008,
        2009,
        2011,
        2012,
        2013,
        2056,
        2059,
        2072,
        2220,
        2221,
        2222,
        2223,
        2224,
        2225,
        2226,
        2227,
        2228,
        2229,
        2230,
        2232,
};

bool bMonster(int iValue) {
    return std::find(std::begin(ListMonsterId), std::end(ListMonsterId), iValue) != std::end(ListMonsterId);
}

void Touch_Tap(int x, int y, int holdMs = 80) {
     Touch_Down((float)x, (float)y);
     usleep((useconds_t)(holdMs > 0 ? holdMs : 80) * 1000);
     Touch_Up();
}

bool lastRetriTriggered[20] = {false};
uint64_t lastRetriTapMs[20] = {0};
bool autoRetribution = false;
bool AutoRetributionRed = false;
bool AutoRetributionBlue = false;
bool AutoRetributionLord = false;
bool AutoRetributionTurtle = false;
bool AutoRetributionCrab = false;
bool AutoRetributionLito = false;        

float retriTouchX = 1575;
float retriTouchY = 661;
int retriHoldMs = 80;          // berapa lama tombol di-hold per tap
int retriRetryMs = 2500;       // retry tap tiap X ms kalau target masih hidup
int retriDmgBonus = 0;         // koreksi damage retri (kalau formula meleset)
float retriMaxDist = 10.0f;    // jarak max trigger. Posisi game diskalakan ~x81 (map 6000 -> 74), jadi range retri ~700 true ≈ 8.6 di sini
float retriJungleMult = 1.0f;  // pengali damage utk monster (item jungle/blessing)

static uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t) duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Deteksi tim: flag same-camp (0x2a9) + fallback iCamp (0x30) kalau flag kelewat
static bool IsTeammate(uintptr_t obj) {
    if (Read<bool>(obj + 0x2a9)) return true;
    if (!g_campCheck) return false;
    uintptr_t bm = getPtr641(libbase + 0x62dc5e0);
    uintptr_t self = bm ? getPtr641(getPtr641(bm + 0xa8) + 0x48) : 0;
    if (!self) return false;
    int myCamp = Read<int>(self + 0x30);
    int oCamp = Read<int>(obj + 0x30);
    if (myCamp > 0 && myCamp == oCamp) return true;
    return false;
}

void DrawMonster(ImDrawList *Draw) {
    if (autoRetribution) {
        ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(retriTouchX, retriTouchY), retriDotSize, IM_COL32(255, 255, 255, 110), 32);
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(retriTouchX, retriTouchY), retriDotSize, IM_COL32(0, 0, 0, 255), 32, 3.0f);
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(retriTouchX - retriDotSize, retriTouchY), ImVec2(retriTouchX + retriDotSize, retriTouchY), IM_COL32(255, 0, 0, 160), 1.5f);
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(retriTouchX, retriTouchY - retriDotSize), ImVec2(retriTouchX, retriTouchY + retriDotSize), IM_COL32(255, 0, 0, 160), 1.5f);
    }
    if (abs_ScreenX < abs_ScreenY) return;
    
    float lineSize = abs_ScreenY / 432;
    long a1 = getPtr641(libbase + 0x62dc5e0); // slot BattleManager (dari IsMomentSelf @ 0x2eaff18)
    long a2 = getPtr641(a1 + 0xa8);           // static_fields offset
    long a32 = getPtr641(a2);                 // BattleManager.Instance (field 0x0)

    /**
    class BattleManager
    perlu update dari dump.cs*
    **/
    size_t m_LocalPlayerShow = 0x48; //m_LocalPlayerShow
    size_t m_ShowPlayers = 0x70; //m_ShowPlayers
    size_t m_ShowMonsters = 0x78; //m_ShowMonsters
    
    /**
    class ShowEntity 
    perlu update dari dump.cs*
    **/
    size_t m_iType = 0x78; //m_iType

    size_t m_Hp = 0x1a4; //m_Hp
    size_t m_HpMax = 0x1a8; //m_HpMax
    size_t m_bDeath = 0xc5; //m_bDeath
    size_t m_bSameCampType = 0x2a9; //m_bSameCampType
    size_t m_vCachePosition = 0x28c; //m_vCachePosition
    /**
    class ShowPlayer
    perlu update dari dump.cs*
    **/
    size_t m_HeroName = 0x8d0; //m_HeroName
    size_t m_ID = 0x18c; //m_ID (HeroID)
    
    long selfp = getPtr641(a32 + m_LocalPlayerShow); // m_LocalPlayerShow;
    
    auto main_cam = GetMainCamera();

    auto camera = Read<uintptr_t>(main_cam + 0x10);
    
    auto ViewMatrix = Read<Camera>(camera + 0x5C);
    _vMatrix = ViewMatrix.projectionMatrix * ViewMatrix.worldToCameraMatrix;

    long player = getPtr641(getPtr641(a32+m_ShowPlayers)+0x10)+0x20;
    uint stop_player = Read<uint>(getPtr641(a32+m_ShowPlayers)+0x18);
    
    for (int i = 0; i < stop_player; i++) {
        auto Objaddr = getPtr641(player + ((i << 3) / 1));

        if ((Objaddr ^ 0x0) == 0x0) {
            continue;
        }

        if (!g_showTeam && IsTeammate(Objaddr)) {
            continue;
        }
        auto HeroID = Read<int>(Objaddr + m_ID);

        auto death = Read<bool>(Objaddr + m_bDeath);
        if (death) {
            continue;
        }

        int Health = Read<uint64_t>(Objaddr + m_Hp);
        if(Health <= 0)
        {
            continue;
        }

        int maxHealth = Read<uint64_t>(Objaddr + m_HpMax);
        if(Health <= 0)
        {
            continue;
        }

        Vector3 Z;
        vm_readv(selfp + m_vCachePosition, &Z, sizeof(Z));
      
        Vector3 D;
        vm_readv(Objaddr + m_vCachePosition, &D, sizeof(D));
        
        Vector2 en_posSc;
        WorldToScreen(D, &en_posSc);
        
        Vector2 loc_posSc;
        WorldToScreen(Z, &loc_posSc);
        
        bool isOutScreen;
        float IconSize = abs_ScreenX / 10.4;
        Vector2 HeroPos = {en_posSc.X, en_posSc.Y};
        Vector2 Res;
        
        if (HeroPos.X < 0 || HeroPos.X > abs_ScreenX || HeroPos.Y < 0 ||
            HeroPos.Y > abs_ScreenY) {
            isOutScreen = true;
            IconSize = abs_ScreenX / 15.6;
            FindPoint(HeroPos, Res, abs_ScreenX, abs_ScreenY, (IconSize / 3));
        } else {
            isOutScreen = false;
            Res = HeroPos;
        }
    
        auto Distance = Vector3::Distance(Z, D);

        // ===== layout ESP baru =====
        // icon hero di posisi, panel info di KANAN icon:
        // [icon] | HP bar
        //        | MP bar
        //        | S1 S2 ULT (dots)
        //        | nama (atas panel)
        auto *dl = ImGui::GetForegroundDrawList();

        if (drawMHealth) {
            dl->AddLine({loc_posSc.X,loc_posSc.Y}, {en_posSc.X, en_posSc.Y}, IM_COL32(255, 255, 255, 255), 1.5f);
        }

        // radius ikon hero ikut slider Ukuran Icon (g_ICSize) biar ga kebesaran
        float r = g_ICSize * 0.62f;
        if (r < 16.0f) r = 16.0f;

        if (iconhero) {
            ImVec2 iconPos(HeroPos.X, HeroPos.Y);
            DrawHeroIcon(ImGui::GetBackgroundDrawList(), iconPos, HeroID, Health, maxHealth, r);
        }

        if (!isOutScreen) {
            float px = HeroPos.X + r + 8.0f;   // panel kiri = tepat kanan icon
            float py = HeroPos.Y - 26.0f;
            float bw = 90.0f;

            // nama hero (baris pertama panel)
            if (drawMDistance || drawMName) {
                std::string nm = fshy(Read<uintptr_t>(Objaddr + m_HeroName));
                if (nm.empty()) nm = std::to_string(HeroID);
                nm = nm + " " + std::to_string((int)Distance) + "m";
                绘制字体描边(20.0f, px, py - 18.0f, ImColor(255, 235, 130, 255), nm.c_str());
            }

            float frac = (maxHealth > 0) ? (float)Health / (float)maxHealth : 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            if (frac < 0.0f) frac = 0.0f;

            if (drawMHealthBar) {
                ImU32 col = frac > 0.55f ? IM_COL32(0, 220, 90, 255)
                           : frac > 0.28f ? IM_COL32(255, 200, 0, 255)
                                          : IM_COL32(255, 60, 60, 255);
                dl->AddRectFilled(ImVec2(px - 1, py - 1), ImVec2(px + bw + 1, py + 8), IM_COL32(0, 0, 0, 170), 3);
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + bw * frac, py + 6), col, 2);
                char t[32];
                snprintf(t, sizeof(t), "%d/%d", (int)Health, (int)maxHealth);
                dl->AddText(ImVec2(px + bw + 6, py - 6), IM_COL32(255, 255, 255, 255), t);
                py += 12.0f;
            }
            if (drawMMpBar) {
                int mp = Read<int>(Objaddr + 0x1e0);      // <m_Mp>k__BackingField
                int mpMax = Read<int>(Objaddr + 0x1e4);   // _MpMax
                float mf = (mpMax > 0) ? (float)mp / (float)mpMax : 0.0f;
                if (mf > 1.0f) mf = 1.0f;
                if (mf < 0.0f) mf = 0.0f;
                dl->AddRectFilled(ImVec2(px - 1, py - 1), ImVec2(px + bw + 1, py + 7), IM_COL32(0, 0, 0, 170), 3);
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + bw * mf, py + 5), IM_COL32(70, 150, 255, 255), 2);
                py += 11.0f;
            }
            if (drawMSkillCD) {
                // 3 dot: S1 S2 ULT — merah=CD, hijau=ready
                bool slotReady[3];
                int slotCd[3];
                GetEnemySkillCD(Objaddr, slotReady, slotCd);
                float dR = 5.0f;
                for (int s = 0; s < 3; s++) {
                    ImVec2 c(px + 8 + s * 18, py + dR);
                    ImU32 col = slotReady[s] ? IM_COL32(0, 255, 120, 255) : IM_COL32(255, 70, 70, 255);
                    dl->AddCircleFilled(c, dR, col, 12);
                    dl->AddCircle(c, dR, IM_COL32(0, 0, 0, 200), 12, 1.5f);
                    if (!slotReady[s] && slotCd[s] > 0) {
                        char t[16];
                        snprintf(t, sizeof(t), "%d", slotCd[s]);
                        dl->AddText(ImVec2(px + 66, py - 2), IM_COL32(255, 140, 255, 255), t);
                    }
                }
            }
        }

    }
    long monster = getPtr641(getPtr641(a32+m_ShowMonsters)+0x10)+0x20; // 0x70 m_ShowPlayer
    uint stop_monster = Read<uint>(getPtr641(a32+m_ShowMonsters)+0x18); // 0x70 m_ShowPlayer
    
    for (int i = 0; i < stop_monster; i++) {
        auto Objaddr = getPtr641(monster + ((i << 3) / 1));

        if ((Objaddr ^ 0x0) == 0x0) {
            continue;
        }

        auto is_team = Read<bool>(Objaddr + m_bSameCampType); // m_bSameCampType
        if (is_team) {
            continue;
        }

        auto mHeroID = Read<int>(Objaddr + m_ID);
        auto type = Read<int>(Objaddr + m_iType);
        
        auto death = Read<bool>(Objaddr + m_bDeath); // m_bDeath
        if (death) {
            continue;
        }

        int Health = Read<uint64_t>(Objaddr + m_Hp); // m_hP
        if(Health <= 0)
        {
            continue;
        }
        
        int maxHealth = Read<uint64_t>(Objaddr + m_HpMax); // m_MaxHp
        if(maxHealth <= 0)
        {
            continue;
        }     
        
        Vector3 ZL;
        vm_readv(selfp + m_vCachePosition, &ZL, sizeof(ZL));
      
        Vector3 Dm;
        vm_readv(Objaddr + m_vCachePosition, &Dm, sizeof(Dm));
        
        Vector2 mon_posSc;
        WorldToScreen(Dm, &mon_posSc);
        
        Vector2 l_posSc;
        WorldToScreen(ZL, &l_posSc);
        
        Vector2 MonPos = {mon_posSc.X, mon_posSc.Y};
        if (MonPos.X < 0 || MonPos.X > abs_ScreenX || MonPos.Y < 0 || MonPos.Y > abs_ScreenY) {
            continue;
        }     
        if (type == 5) {           
           if (mHeroID == 2002 && Health < maxHealth) {
               std::string s = "LORD UNDER ATK!";
               std::string h;
               h += "Health: "+ std::to_string((int)Health);
               Draw->AddText(nullptr,22.5f, ImVec2(abs_ScreenX / 2 - 70.f, 30), ImColor(248,248,255), s.c_str());
               Draw->AddText(nullptr,22.5f, ImVec2(abs_ScreenX / 2 - 70.f, 50), ImColor(248,248,255), h.c_str());               
           }
        
           if (mHeroID == 2003 && Health < maxHealth) {
               std::string s = "TURTLE UNDER ATK!";
               std::string h;
               h += "Health: "+ std::to_string((int)Health);

               Draw->AddText(nullptr,22.5f, ImVec2(abs_ScreenX / 2 - 70.f, 30), ImColor(248,248,255), s.c_str());
               Draw->AddText(nullptr,22.5f, ImVec2(abs_ScreenX / 2 - 70.f, 50), ImColor(248,248,255), h.c_str());               
           }
        }
        if (type == 1) {
            std::string sL = "MINION";
            auto textSize1 = ImGui::CalcTextSize(sL.c_str(), 0, 29); 
            绘制字体描边(22.5,MonPos.X - (textSize1.x / 2), MonPos.Y,ImColor(248,248,255),sL.c_str());
        }
        if (type == 2) {
            if (!bMonster(mHeroID)) continue;               
           
             std::string monsterName = MonsterToString(mHeroID);
             if (monsterName.empty()) continue;             
             //auto bShowEntityLayer = Read<bool>(Objaddr + 0x32c);               
             bool isImportantMonster = (mHeroID == 2002 || mHeroID == 2220 || mHeroID == 2003 || mHeroID == 2221 || 
                                          mHeroID == 2004 || mHeroID == 2005 || mHeroID == 2222 || mHeroID == 2223);
                
             //bool shouldShow = !bShowEntityLayer || isImportantMonster;               
             //if (!shouldShow) continue;            
           
             bool isEventMonster = (mHeroID >= 2220 && mHeroID <= 2232);
             ImColor nameColor = isEventMonster ? IM_COL32(255, 215, 0, 255) : IM_COL32(220, 180, 255, 255);     
           
             auto DistanceM = Vector3::Distance(ZL, Dm);         
         
             std::string strName = monsterName;
             if (isEventMonster) {
                 strName = "[EVENT] " + monsterName;
             } 
             
             auto textSize1 = ImGui::CalcTextSize(strName.c_str(), 0, 29); 
             绘制字体描边(22.5,MonPos.X - (textSize1.x / 2), MonPos.Y + 20,nameColor,strName.c_str());
             
             std::string sm;     
             sm += std::to_string((int)DistanceM);
             sm += "m | ";
             sm += "Health: "+ std::to_string((int)Health);      
             auto textSize11 = ImGui::CalcTextSize(sm.c_str(), 0, 29);    
             绘制字体描边(22.5,MonPos.X - (textSize11.x / 2), MonPos.Y,nameColor,sm.c_str());
        }
    }
}

struct MonsterData {
    uintptr_t address;
    Vector3 position;
    float distance;
    int health;
    int maxHP;
    bool isDead;
    bool isVisible;
    bool isValid;
    char name[100];
};

MonsterData monster[20];

int MonsterCount = 0;

uintptr_t Oneself;

void MonsterRetribution() {
    uintptr_t BattleManager = getPtr641(libbase + 0x62dc5e0); // slot BattleManager
    BattleManager = getPtr641(BattleManager + 0xa8);          // static_fields
    BattleManager = getPtr641(BattleManager);                  // Instance (0x0)

    if(!BattleManager) return;
    Oneself = getPtr641(BattleManager + 0x48);
    if(!Oneself) return;

    Vector3 MyPosition;
    vm_readv(Oneself + 0x28c, &MyPosition, sizeof(MyPosition));

    MonsterCount = 0;
    uintptr_t Showmonster = getPtr641(BattleManager + 0x78);
    if (Showmonster != 0) {
        int monsterCount = Read<int>(Showmonster + 0x18);
        uintptr_t monsterDataPtr = ReadPtr(Showmonster + 0x10);
        if (monsterCount >= 0 && monsterCount <= 100 && monsterDataPtr != 0) {
            uintptr_t monsterDataArray = monsterDataPtr + 0x20;
            int monsterfound = 0;
            for (int i = 0; i < monsterCount && monsterfound < 20; i++) {
                uintptr_t currentMonsterPtr = ReadPtr(monsterDataArray + (i * 8));
                if (currentMonsterPtr == 0) continue;
                int monsterID = Read<int>(currentMonsterPtr + 0x18c);
                int monsterHP = Read<int>(currentMonsterPtr + 0x1a4);
                int monsterMaxHP = Read<int>(currentMonsterPtr + 0x1a8);
                Vector3 monsterPos = Read<Vector3>(currentMonsterPtr + 0x28c);
                uint8_t deadFlag = Read<uint8_t>(currentMonsterPtr + 0xc5);
                bool mDead = (deadFlag != 0);
                // target yang di-support auto retri; lainnya skip (hemat slot array 20)
                static const int targetIds[] = {
                    2002, 2003, 2004, 2005, 2006, 2008, 2009, 2011, 2012, 2013,
                    2056, 2059, 2072,
                    2220, 2221, 2222, 2223, 2224, 2225, 2226, 2227, 2228, 2229, 2230, 2232
                };
                bool isTarget = false;
                for (int t : targetIds) if (monsterID == t) { isTarget = true; break; }
                if (!isTarget) continue;
                std::string mName = MonsterToString(monsterID);
                if (mName.empty()) {
                    if (monsterID == 2002) mName = "Lord";
                    else if (monsterID == 2003) mName = "Turtle";
                    else mName = "ID " + std::to_string(monsterID);
                }
                monster[monsterfound].address   = currentMonsterPtr;
                monster[monsterfound].position  = monsterPos;
                monster[monsterfound].distance  = Vector3::Distance(MyPosition, monsterPos);
                monster[monsterfound].health    = monsterHP;
                monster[monsterfound].maxHP     = monsterMaxHP;
                monster[monsterfound].isDead    = mDead;
                monster[monsterfound].isVisible = true;
                monster[monsterfound].isValid   = true;
                strncpy(monster[monsterfound].name, mName.c_str(), sizeof(monster[monsterfound].name) - 1);
                monster[monsterfound].name[sizeof(monster[monsterfound].name) - 1] = '\0';
                monsterfound++;
            }
            MonsterCount = monsterfound;
        }
    }
}

// Formula resmi MLBB (update): Retribution true damage = 750 + 150 * hero level
int CalculateRetriDamage(int Level) {
    return 750 + 150 * Level;
}

static void DoRetriTap(int i, uint64_t now) {
    if (g_retriNativeX >= 0 && g_retriNativeY >= 0)
        Touch_TapNative(g_retriNativeX, g_retriNativeY, retriHoldMs);
    else
        Touch_Tap((int) retriTouchX, (int) retriTouchY, retriHoldMs);
    lastRetriTriggered[i] = true;
    lastRetriTapMs[i] = now;
}

static bool RetriEligible(int i, int retriDmg) {
    if (i < 0 || i >= MonsterCount) return false;
    if (!monster[i].isValid || monster[i].isDead) return false;
    if (monster[i].distance > retriMaxDist) return false;
    if (monster[i].health > retriDmg) return false;
    int id = Read<int>(monster[i].address + 0x18c);
    if (AutoRetributionLord && (id == 2002)) return true;
    if (AutoRetributionTurtle && (id == 2003)) return true;
    if (AutoRetributionBlue && (id == 2005)) return true;
    if (AutoRetributionLito && (id == 2056 || id == 2072)) return true;
    if (AutoRetributionCrab && (id == 2011 || id == 2013)) return true;
    if (AutoRetributionRed && (id == 2004)) return true;
    return false;
}

void CheckAndTriggerRetribution() {
    if (!autoRetribution) return;
    if (!Oneself || MonsterCount <= 0) return;
    int myLevel = Read<int>(Oneself + 0x190);
    int retriDmg = (int) ((750 + 150 * myLevel) * retriJungleMult) + retriDmgBonus;
    if (retriDmg < 0) retriDmg = 0;
    uint64_t now = NowMs();

    // Tap langsung via SLOT terpisah (12) - tidak perlu nunggu jari lepas,
    // slot dedicated itu justru biar ga nyatu sama jari asli (joystick/skill).
    // Nunggu "tangan bersih" = retri ga pernah jalan pas war (jari selalu nempel).
    for (int i = 0; i < MonsterCount; i++) {
        if (!monster[i].isValid || monster[i].isDead) {
            lastRetriTriggered[i] = false;
            continue;
        }
        if (RetriEligible(i, retriDmg)) {
            if (lastRetriTriggered[i]) {
                // retri CD 30s+: boleh coba ulang kalau target masih hidup & masih bisa dibunuh
                if (now - lastRetriTapMs[i] >= (uint64_t) retriRetryMs)
                    lastRetriTriggered[i] = false;
                else
                    continue;
            }
            DoRetriTap(i, now);
        } else {
            lastRetriTriggered[i] = false;
        }
    }
}
/*
void RoomInfoList() {
    uintptr_t LogicBattleManager = getPtr641(libbase + 0x62dc5e0);
    if (!LogicBattleManager) return;

    long playersList = getPtr641(getPtr641((uintptr_t)LogicBattleManager + 0x78) + 0x10); // m_ShowPlayers
    int playerCount = Read<int>(getPtr641((uintptr_t)LogicBattleManager + 0x78) + 0x18);
    if (playerCount <= 0 || !playersList) return;

    long a1 = getPtr641(libbase + 0x62dc5e0); // BattleManager base (lagi, bisa pakai LogicBattleManager juga)
    long a2 = getPtr641(a1 + 0xa8);
    long a32 = getPtr641(a2);

    long selfp = getPtr641(a32 + 0x50); // m_LocalPlayerShow

    if (!selfp) return;

    uint32_t myTeamCamp = Read<uint32_t>(selfp + 0x30); // offset iCamp dari dump.cs

    int playerB = 0;
    int playerR = 0;

    for (int i = 0; i < playerCount; i++) {
        long obj = getPtr641(playersList + i * 8);
        if (!obj) continue;

        auto nameObj = *(String**)(obj + 0x40);
        std::string name = (nameObj && nameObj->CString()) ? nameObj->CString() : "Unknown";

        uint64_t lUid = Read<uint64_t>(obj + 0x20);
        uint32_t zoneId = Read<uint32_t>(obj + 0x60);
        std::string uid = std::to_string(lUid) + " (" + std::to_string(zoneId) + ")";

        uint32_t heroid = Read<uint32_t>(obj + 0x4C);
        int spellId = Read<int>(obj + 0x64);
        uint32_t rank = Read<uint32_t>(obj + 0x128);
        uint32_t myth = Read<uint32_t>(obj + 0x1CC);
        uint32_t camp = Read<uint32_t>(obj + 0x30);

        std::string hero = HeroToString(heroid);
        std::string spell = SpellToString(spellId);
        std::string rankStr = RankToString(rank, myth);

        if (camp == myTeamCamp && playerB < 5) {
            PlayerB[playerB++] = { name, uid, hero, spell, rankStr };
        } else if (playerR < 5) {
            PlayerR[playerR++] = { name, uid, hero, spell, rankStr };
        }
    }
}*/

int MinimapSize = 342;
int MinimapPos = 76;
int MinimapPosY = 0;
bool MinimapIcon = true;
bool HideLine = false;

float g_MinimapScale = 74.11f;
float g_MapAngle = 314.60f;   // derajat rotasi map (bisa dikalibrasi live)
float g_Res0_MultX = 1.0f;
float g_Res0_MultY = 1.0f;
float g_Res1_OffsetX = 0.0f;
float g_Res1_OffsetY = 0.0f;

// Minimap harus pakai bidang horizontal (X & Z dunia), BUKAN sumbu vertikal (Y-up).
// Sebelumnya HeroPosition.Z itu isinya worldY (atas/bawah) -> ikon melompat-lompat
// dan ga sinkron sama gerakan player.
Vector2 WorldToMinimap(Vector3 HeroPosition) {
    float angle = g_MapAngle * 0.017453292519943295f;
    float angleCos = std::cos(angle);
    float angleSin = std::sin(angle);

    // HeroPosition.X = worldX, HeroPosition.Y = worldZ (horizontal)
    float worldX = HeroPosition.X;
    float worldZ = HeroPosition.Y;

    Vector2 Res0;
    Res0.X = ((angleCos * worldX - angleSin * (-worldZ)) / g_MinimapScale) * g_Res0_MultX;
    Res0.Y = ((angleSin * worldX + angleCos * (-worldZ)) / g_MinimapScale) * g_Res0_MultY;

    Vector2 Res1;
    Res1.X = (Res0.X * MinimapSize) + MinimapPos + MinimapSize / 2.0f + g_Res1_OffsetX;
    Res1.Y = (Res0.Y * MinimapSize) + MinimapPosY + MinimapSize / 2.0f + g_Res1_OffsetY;

    return Res1;
}

void DrawMinimapESP(ImDrawList* draw) {
    if (!MinimapIcon) return;

    long a1 = getPtr641(libbase + 0x62dc5e0);
    if (!a1) return;

    long a2 = getPtr641(a1 + 0xa8);
    if (!a2) return;

    long a32 = getPtr641(a2);
    if (!a32) return;

    // Offset field
    size_t m_ShowPlayers     = 0x70;
    size_t m_bSameCampType   = 0x2a9;
    size_t m_bDeath          = 0xc5;
    size_t m_vCachePosition  = 0x28c;

    // marker player sendiri (hijau) biar kelihatan sinkron sama gerakan
    {
        long selfp = getPtr641(a32 + 0x48); // m_LocalPlayerShow
        if (selfp) {
            Vector3A me{};
            vm_readv(selfp + m_vCachePosition, &me, sizeof(me));
            if (me.X != 0 || me.Y != 0 || me.Z != 0) {
                Vector2 myPos = WorldToMinimap({ me.X, me.Y, me.Z });
                draw->AddTriangleFilled(
                    ImVec2(myPos.X, myPos.Y - 14),
                    ImVec2(myPos.X - 10, myPos.Y + 10),
                    ImVec2(myPos.X + 10, myPos.Y + 10),
                    IM_COL32(0, 255, 120, 255));
                draw->AddTriangle(
                    ImVec2(myPos.X, myPos.Y - 14),
                    ImVec2(myPos.X - 10, myPos.Y + 10),
                    ImVec2(myPos.X + 10, myPos.Y + 10),
                    IM_COL32(0, 0, 0, 220), 1.5f);
            }
        }
    }

    long showList = getPtr641(a32 + m_ShowPlayers);
    if (!showList) return;

    long playerList = getPtr641(showList + 0x10);
    if (!playerList) return;
    playerList += 0x20;

    uint playerCount = Read<uint>(showList + 0x18);
    for (int i = 0; i < playerCount; i++) {
        long Objaddr = getPtr641(playerList + (i << 3));
        if (!Objaddr) continue;

        if (!g_showTeam && IsTeammate(Objaddr)) continue;
        if (Read<bool>(Objaddr + m_bDeath)) continue;

        Vector3A pos{};
        vm_readv(Objaddr + m_vCachePosition, &pos, sizeof(pos));
        if (pos.X == 0 && pos.Y == 0 && pos.Z == 0) continue;

        int heroID = Read<int>(Objaddr + 0x18c); // m_ID

        Vector2 minimapPos = WorldToMinimap({ pos.X, pos.Y, pos.Z });
        float r = g_ICSize / 2.0f;

        // icon hero musuh di minimap + ring merah biar mencolok
        ImTextureID tex = GetHeroTexture(heroID);
        if (tex) {
            draw->AddCircleFilled(ImVec2(minimapPos.X, minimapPos.Y), r + 1.5f, IM_COL32(0, 0, 0, 200), 24);
            draw->AddImageRounded(tex,
                ImVec2(minimapPos.X - r, minimapPos.Y - r),
                ImVec2(minimapPos.X + r, minimapPos.Y + r),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255), r);
        }
        draw->AddCircle(ImVec2(minimapPos.X, minimapPos.Y), r + 2.0f, IM_COL32(255, 40, 40, 230), 24, 2.0f);
    }

    if (!HideLine) {
        draw->AddRect(
            ImVec2(MinimapPos, 0),
            ImVec2(MinimapPos + MinimapSize, MinimapSize),
            IM_COL32(255, 255, 255, 255)
        );
    }
}
// tema: 0=Neon Dark 1=Light 2=Classic
int theme = 0;

void ApplyTheme() {
    ImGuiStyle &st = ImGui::GetStyle();
    switch (theme) {
        case 1: ImGui::StyleColorsLight(); break;
        case 2: ImGui::StyleColorsClassic(); break;
        default: ImGui::StyleColorsDark(); break;
    }
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ImColor(16, 18, 26, 235);
    c[ImGuiCol_ChildBg]         = ImColor(22, 26, 36, 220);
    c[ImGuiCol_Border]          = ImColor(0, 200, 255, 120);
    c[ImGuiCol_TitleBg]         = ImColor(13, 15, 22, 255);
    c[ImGuiCol_TitleBgActive]   = ImColor(16, 18, 26, 255);
    c[ImGuiCol_Text]            = ImColor(235, 240, 250, 255);
    c[ImGuiCol_TextDisabled]    = ImColor(130, 140, 160, 255);
    c[ImGuiCol_Tab]             = ImColor(24, 29, 42, 255);
    c[ImGuiCol_TabHovered]      = ImColor(0, 170, 220, 170);
    c[ImGuiCol_TabActive]       = ImColor(0, 190, 255, 220);
    c[ImGuiCol_Header]          = ImColor(0, 150, 200, 120);
    c[ImGuiCol_HeaderHovered]   = ImColor(0, 170, 220, 170);
    c[ImGuiCol_HeaderActive]    = ImColor(0, 190, 255, 220);
    c[ImGuiCol_Button]          = ImColor(28, 34, 50, 255);
    c[ImGuiCol_ButtonHovered]   = ImColor(0, 160, 210, 200);
    c[ImGuiCol_ButtonActive]    = ImColor(0, 180, 240, 255);
    c[ImGuiCol_FrameBg]         = ImColor(28, 33, 47, 255);
    c[ImGuiCol_FrameBgHovered]  = ImColor(0, 140, 190, 140);
    c[ImGuiCol_FrameBgActive]   = ImColor(0, 170, 230, 200);
    c[ImGuiCol_CheckMark]       = ImColor(0, 220, 255, 255);
    c[ImGuiCol_SliderGrab]      = ImColor(0, 200, 255, 255);
    c[ImGuiCol_SliderGrabActive]= ImColor(120, 230, 255, 255);
    st.WindowRounding = 12.0f;
    st.ChildRounding = 10.0f;
    st.FrameRounding = 8.0f;
    st.GrabRounding = 8.0f;
    st.TabRounding = 8.0f;
    st.PopupRounding = 10.0f;
    st.ScrollbarRounding = 10.0f;
    st.WindowBorderSize = 1.2f;
    st.WindowPadding = ImVec2(14, 12);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 8);
}

void SectionHeader(const char* label) {
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextColored(ImColor(0, 190, 255, 255), "%s", label);
    ImGui::Separator();
}

void Layout_tick_UI() {
    ImGuiIO &io = ImGui::GetIO();

    // ===== MINIMIZED: pill kecil, teks PANXCZ + FPS sejajar =====
    if (g_menuMinimized) {
        ImGui::SetNextWindowPos(ImVec2(abs_ScreenX / 2.0f - 70, 0), ImGuiCond_Always);
        ImGuiWindowFlags pf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::Begin(oxorany("##minpill"), nullptr, pf);
        if (ImGui::Button(oxorany("⚡ PANXCZ"), ImVec2(92, 26))) {
            g_menuMinimized = false;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImColor(0, 255, 140, 255), "%.0f FPS | %s", io.Framerate, langEN ? "EN" : "ID");
        // overlay tetap jalan walau menu di-minimize (draw sebelum End supaya g_window valid)
        if (MinimapIcon) DrawMinimapESP(ImGui::GetForegroundDrawList());
        DrawMonster(ImGui::GetForegroundDrawList());
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;

    // window 16:9 proporsional layar
    float maxW = abs_ScreenX * 0.40f;
    float maxH = abs_ScreenY * 0.55f;
    float winW = fminf(maxW, maxH * 16.0f / 9.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(winW, 0), ImVec2(winW, maxH));

    ImGui::Begin(oxorany(" PANXCZ "), nullptr, window_flags);

    // header
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.588f, 0.784f, 0.16f));
    ImGui::BeginChild("##hdr", ImVec2(w, 50), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(14, 13));
    ImGui::TextColored(ImColor(0, 220, 255, 255), "PANXCZ");
    ImGui::SameLine();
    ImGui::TextDisabled("MLBB v0.7");
    ImGui::SetCursorPos(ImVec2(w - 236, 15));
    ImGui::TextColored(ImColor(0, 255, 140, 255), "%.0f FPS | %s", io.Framerate, langEN ? "EN" : "ID");
    // tombol minimize (-) & exit (x) - ukuran nyaman buat jari
    float bx = w - 118.0f;
    ImGui::SetCursorPos(ImVec2(bx, 6));
    if (ImGui::Button(oxorany("——"), ImVec2(52, 38))) {
        g_menuMinimized = true;
    }
    bx += 60.0f;
    ImGui::SetCursorPos(ImVec2(bx, 6));
    if (ImGui::Button(oxorany("✕"), ImVec2(52, 38))) {
        main_thread_flag = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("####", ImGuiTabBarFlags_Reorderable)) {

        if (ImGui::BeginTabItem(TR("ESP", "ESP"))) {
            SectionHeader(TR("Player ESP", "Player ESP"));
            ImGui::Checkbox(TR("Line to Enemy", "Line ke Musuh"), &drawMHealth);
            ImGui::Checkbox(TR("Hero Icon", "Icon Hero"), &iconhero);
            ImGui::Checkbox(TR("Distance & Name", "Jarak & Nama"), &drawMDistance);
            ImGui::Checkbox(TR("Health Bar", "Bar HP"), &drawMHealthBar);
            ImGui::Checkbox(TR("Mana Bar", "Bar MP"), &drawMMpBar);
            ImGui::Checkbox(TR("Skill CD Status", "Status CD Skill"), &drawMSkillCD);
            ImGui::Spacing();
            ImGui::Checkbox(TR("Show Teammates (ally too)", "Tampilkan Teman (sekutu)"), &g_showTeam);
            ImGui::Checkbox(TR("Team detect via camp", "Deteksi Tim via Camp"), &g_campCheck);
            ImGui::TextDisabled(TR("Default: enemies only. Turn ON to see allies.", "Default: musuh saja. Nyalakan untuk lihat sekutu."));

            SectionHeader(TR("Alert", "Alert"));
            ImGui::Checkbox(TR("Alert Lord Under Attack", "Alert Lord Diserang"), &drawAlertUnderAttack);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TR("Drone", "Drone"))) {
            SectionHeader(TR("Safety", "Keamanan"));
            ImGui::Checkbox(TR("Safe Mode (no memory write)", "Safe Mode (tanpa write memori)"), &safeMode);
            if (safeMode) {
                ImGui::TextColored(ImColor(255, 120, 120, 255), TR("Safe Mode ON: tidak ada memory write ke game.", "Safe Mode ON: tidak ada memory write ke game."));
            }
            SectionHeader(TR("Drone View", "Drone View"));
            if (safeMode) {
                ImGui::TextDisabled(TR("Matikan Safe Mode dulu untuk pakai Drone View.", "Matikan Safe Mode dulu untuk pakai Drone View."));
            } else {
                ImGui::Checkbox(TR("Enable Drone View", "Aktifkan Drone View"), &droneView);
                ImGui::SliderFloat(TR("Height", "Tinggi"), &droneHeight, 5.0f, 60.0f, "%.0f");
                ImGui::TextDisabled(TR("Higher = more top-down view", "Makin tinggi = makin top-down"));
            }
            ImGui::TextDisabled(TR("Drone menulis FOV kamera ke memori game (risiko ban).", "Drone menulis FOV kamera ke memori game (risiko ban)."));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TR("Auto Retri", "Auto Retri"))) {
            SectionHeader(TR("Auto Retribution", "Auto Retri"));
            ImGui::Checkbox(TR("Enable Auto Retri", "Aktifkan Auto Retri"), &autoRetribution);

            ImGui::Spacing();
            ImGui::TextDisabled(TR("Retri button position (screen coords)", "Posisi tombol retri (koordinat layar)"));
            ImGui::SliderFloat("X", &retriTouchX, 0.0f, 3000.0f, "%.0f");
            ImGui::SliderFloat("Y", &retriTouchY, 0.0f, 1500.0f, "%.0f");
            ImGui::SliderFloat(TR("Dot Size", "Ukuran Dot"), &retriDotSize, 10.0f, 90.0f, "%.0f");
            if (g_retriNativeX < 0 || g_retriNativeY < 0) {
                if (ImGui::Button(TR("Set Dot: tap tombol retri sekali", "Set Dot: ketuk tombol retri sekali"), ImVec2(-1, 34))) {
                    g_retriCapture = true;
                }
            } else {
                ImGui::TextColored(ImColor(120, 255, 160, 255),
                    TR("Dot calibrated (tap presisi aktif)", "Dot terkalibrasi (tap presisi aktif)"));
                if (ImGui::Button(TR("Re-set Dot", "Set Ulang Dot"), ImVec2(-1, 34))) {
                    g_retriNativeX = g_retriNativeY = -1;
                    g_retriCapture = true;
                }
            }
            ImGui::TextDisabled(TR("Lingkaran putih + cross = posisi tombol retri. Pindahkan via X/Y atau pakai Set Dot.", "Lingkaran putih + cross = posisi tombol retri. Pindahkan via X/Y atau pakai Set Dot."));

            SectionHeader(TR("Damage & Timing", "Damage & Timing"));
            if (Oneself) {
                int lvl = Read<int>(Oneself + 0x190);
                int dmg = (int) ((750 + 150 * lvl) * retriJungleMult) + retriDmgBonus;
                if (dmg < 0) dmg = 0;
                ImGui::TextColored(ImColor(255, 200, 60, 255),
                    TR("Retri dmg now: %d  (Lv %d, 750+150/level)", "Damage retri: %d (Lv %d, 750+150/level)"), dmg, lvl);
                int ready = 0;
                for (int t = 0; t < MonsterCount; t++) {
                    if (!monster[t].isValid || monster[t].isDead) continue;
                    if (monster[t].distance > retriMaxDist) continue;
                    if (monster[t].health <= dmg) ready++;
                }
                ImGui::TextColored(ImColor(120, 255, 160, 255),
                    TR("Monster killable right now: %d", "Bisa dibunuh skrg: %d"), ready);
            } else {
                ImGui::TextDisabled(TR("(no monster data yet - enter a match)", "(belum ada data - masuk match dulu)"));
            }
            ImGui::SliderInt(TR("Dmg Bonus", "Dmg Bonus"), &retriDmgBonus, -500, 1000, "%d");
            ImGui::SliderFloat(TR("Jungle Mult", "Pengali Jungle"), &retriJungleMult, 0.50f, 3.0f, "%.2f");
            ImGui::SliderInt(TR("Hold ms", "Tahan (ms)"), &retriHoldMs, 30, 300, "%d ms");
            ImGui::SliderInt(TR("Retry every", "Retry tiap"), &retriRetryMs, 500, 6000, "%d ms");
            ImGui::SliderFloat(TR("Max Distance", "Jarak Max"), &retriMaxDist, 2.0f, 40.0f, "%.1f");
            ImGui::TextDisabled(TR("Auto retries every few sec while target is killable.", "Auto retry tiap beberapa detik selama target masih bisa dibunuh."));

            SectionHeader(TR("Target", "Target"));
            ImGui::Checkbox(TR("Buff Red", "Buff Merah"), &AutoRetributionRed);
            ImGui::Checkbox(TR("Buff Blue", "Buff Biru"), &AutoRetributionBlue);
            ImGui::Checkbox(TR("Lord", "Lord"), &AutoRetributionLord);
            ImGui::Checkbox(TR("Turtle", "Turtle"), &AutoRetributionTurtle);
            ImGui::Checkbox(TR("Crab", "Crab"), &AutoRetributionCrab);
            ImGui::Checkbox(TR("Lito", "Lito"), &AutoRetributionLito);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TR("Minimap", "Minimap"))) {
            SectionHeader(TR("Minimap ESP", "Minimap ESP"));
            ImGui::Checkbox(TR("Minimap", "Minimap"), &MinimapIcon);
            ImGui::SameLine();
            ImGui::Checkbox(TR("Hide Frame", "Sembunyikan Frame"), &HideLine);
            ImGui::SliderInt(TR("Size", "Ukuran"), &MinimapSize, 100, 600);
            ImGui::SliderInt(TR("Pos X", "Pos X"), &MinimapPos, 0, 800);
            ImGui::SliderInt(TR("Pos Y", "Pos Y"), &MinimapPosY, 0, 800);
            ImGui::SliderInt(TR("Icon Size", "Ukuran Icon"), &g_ICSize, 1, 100);

            SectionHeader(TR("Calibration", "Kalibrasi"));
            ImGui::SliderFloat(TR("Angle", "Angle"), &g_MapAngle, 0.0f, 360.0f);
            ImGui::SliderFloat("X Mult", &g_Res0_MultX, 0.1f, 3.0f);
            ImGui::SliderFloat("Y Mult", &g_Res0_MultY, 0.1f, 3.0f);
            ImGui::SliderFloat("Off X", &g_Res1_OffsetX, -200.0f, 200.0f);
            ImGui::SliderFloat("Off Y", &g_Res1_OffsetY, -200.0f, 200.0f);
            ImGui::SliderFloat(TR("Scale", "Scale"), &g_MinimapScale, 10.0f, 150.0f);
            ImGui::TextDisabled(TR("Angle = map rotation. Adjust so enemy dots align with in-game minimap.", "Angle = rotasi peta. Atur agar titik musuh sejajar dgn minimap asli."));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TR("Settings", "Pengaturan"))) {
            SectionHeader(TR("Display", "Tampilan"));
            const char* themes[] = { "Neon Dark", "Light", "Classic" };
            if (ImGui::Combo(TR("Theme", "Tema"), &theme, themes, IM_ARRAYSIZE(themes))) {
                ApplyTheme();
            }
            static float opacity = 1.0f;
            ImGui::SliderFloat(TR("Opacity", "Opacity"), &opacity, 0.1f, 1.0f);
            ImGui::GetStyle().Alpha = opacity;

            SectionHeader(TR("Language", "Bahasa"));
            const char* langs[] = { "English", "Indonesia" };
            ImGui::Combo(TR("UI Language", "Bahasa UI"), &g_langSel, langs, IM_ARRAYSIZE(langs));
            langEN = (g_langSel == 0);

            SectionHeader(TR("Actions", "Aksi"));
            if (ImGui::Button(TR(" Exit Cheat ", " Keluar Cheat "), ImVec2(-1, 40))) {
                main_thread_flag = false;
            }
            ImGui::Spacing();
            if (ImGui::Button(TR(" Unload (exit) ", " Unload (keluar) "), ImVec2(-1, 40))) {
                exit(0);
            }
            ImGui::Spacing();
            ImGui::TextDisabled(TR("Volume − = minimize menu | Volume + = expand", "Vol − = minimize | Vol + = expand"));
            ImGui::TextDisabled(TR("ESP/minimap tetap jalan saat menu di-minimize.", "Saat minimized, ESP/minimap tetap jalan."));
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }


    if (MinimapIcon) DrawMinimapESP(ImGui::GetForegroundDrawList());
    DrawMonster(ImGui::GetForegroundDrawList());
    g_window = ImGui::GetCurrentWindow();
    ImGui::End();
}

// ===== Simpan/muat kalibrasi & pengaturan (biar ga reset tiap run) =====
#define CFG_PATH "/data/local/tmp/panxcz_mlbb.cfg"

static void SaveCfg() {
    std::ofstream f(CFG_PATH, std::ios::trunc);
    if (!f.is_open()) return;
    auto w = [&](const char *k, long long v) { f << k << "=" << v << "\n"; };
    w("langEN", langEN ? 1 : 0);
    w("theme", theme);
    w("safeMode", safeMode ? 1 : 0);
    w("showTeam", g_showTeam ? 1 : 0);
    w("campCheck", g_campCheck ? 1 : 0);
    w("minimapIcon", MinimapIcon ? 1 : 0);
    w("hideLine", HideLine ? 1 : 0);
    w("minimapSize", MinimapSize);
    w("minimapPos", MinimapPos);
    w("minimapPosY", MinimapPosY);
    w("icSize", g_ICSize);
    w("mapAngle", (long long) (g_MapAngle * 100.0f));
    w("multX", (long long) (g_Res0_MultX * 100.0f));
    w("multY", (long long) (g_Res0_MultY * 100.0f));
    w("offX", (long long) (g_Res1_OffsetX * 100.0f));
    w("offY", (long long) (g_Res1_OffsetY * 100.0f));
    w("mapScale", (long long) (g_MinimapScale * 100.0f));
    w("retriX", (long long) retriTouchX);
    w("retriY", (long long) retriTouchY);
    w("retriDot", (long long) retriDotSize);
    w("retriHold", retriHoldMs);
    w("retriRetry", retriRetryMs);
    w("retriBonus", retriDmgBonus);
    w("retriDist", (long long) (retriMaxDist * 100.0f));
    w("retriJung", (long long) (retriJungleMult * 100.0f));
    w("retriNX", g_retriNativeX);
    w("retriNY", g_retriNativeY);
    w("espHealth", drawMHealth ? 1 : 0);
    w("espIcon", iconhero ? 1 : 0);
    w("espDist", drawMDistance ? 1 : 0);
    w("espHpBar", drawMHealthBar ? 1 : 0);
    w("espMpBar", drawMMpBar ? 1 : 0);
    w("espSkillCd", drawMSkillCD ? 1 : 0);
    w("alertLord", drawAlertUnderAttack ? 1 : 0);
    f.close();
}

static void LoadCfg() {
    std::ifstream f(CFG_PATH);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        long long v = atoll(line.c_str() + (long) eq + 1);
        if (k == "langEN") { langEN = v != 0; g_langSel = langEN ? 0 : 1; }
        else if (k == "theme") theme = (int) v;
        else if (k == "safeMode") safeMode = v != 0;
        else if (k == "showTeam") g_showTeam = v != 0;
        else if (k == "campCheck") g_campCheck = v != 0;
        else if (k == "minimapIcon") MinimapIcon = v != 0;
        else if (k == "hideLine") HideLine = v != 0;
        else if (k == "minimapSize") MinimapSize = (int) v;
        else if (k == "minimapPos") MinimapPos = (int) v;
        else if (k == "minimapPosY") MinimapPosY = (int) v;
        else if (k == "icSize") g_ICSize = (int) v;
        else if (k == "mapAngle") g_MapAngle = (float) v / 100.0f;
        else if (k == "multX") g_Res0_MultX = (float) v / 100.0f;
        else if (k == "multY") g_Res0_MultY = (float) v / 100.0f;
        else if (k == "offX") g_Res1_OffsetX = (float) v / 100.0f;
        else if (k == "offY") g_Res1_OffsetY = (float) v / 100.0f;
        else if (k == "mapScale") g_MinimapScale = (float) v / 100.0f;
        else if (k == "retriX") retriTouchX = (float) v;
        else if (k == "retriY") retriTouchY = (float) v;
        else if (k == "retriDot") retriDotSize = (float) v;
        else if (k == "retriHold") retriHoldMs = (int) v;
        else if (k == "retriRetry") retriRetryMs = (int) v;
        else if (k == "retriBonus") retriDmgBonus = (int) v;
        else if (k == "retriDist") retriMaxDist = (float) v / 100.0f;
        else if (k == "retriJung") retriJungleMult = (float) v / 100.0f;
        else if (k == "retriNX") g_retriNativeX = (int) v;
        else if (k == "retriNY") g_retriNativeY = (int) v;
        else if (k == "espHealth") drawMHealth = v != 0;
        else if (k == "espIcon") iconhero = v != 0;
        else if (k == "espDist") drawMDistance = v != 0;
        else if (k == "espHpBar") drawMHealthBar = v != 0;
        else if (k == "espMpBar") drawMMpBar = v != 0;
        else if (k == "espSkillCd") drawMSkillCD = v != 0;
        else if (k == "alertLord") drawAlertUnderAttack = v != 0;
    }
    f.close();
}

// Volume − -> minimize menu, Volume + -> expand (tanpa grab, sistem tetap normal)
static void *VolumeKeyWatcher(void *arg) {
    (void) arg;
    int fds[32];
    int n = 0;
    for (int dev = 0; dev < 64 && n < 32; dev++) {
        char temp[64];
        snprintf(temp, sizeof(temp), "/dev/input/event%d", dev);
        int fd = open(temp, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        uint8_t types[(EV_MAX / 8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(types)), types) < 0 ||
            !(types[EV_KEY / 8] & (1 << (EV_KEY % 8)))) {
            close(fd);
            continue;
        }
        uint8_t keys[(KEY_MAX / 8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) {
            close(fd);
            continue;
        }
        bool hasVol = (keys[KEY_VOLUMEDOWN / 8] & (1 << (KEY_VOLUMEDOWN % 8))) ||
                      (keys[KEY_VOLUMEUP / 8] & (1 << (KEY_VOLUMEUP % 8)));
        if (!hasVol) {
            close(fd);
            continue;
        }
        fds[n++] = fd;
    }
    if (n == 0) return nullptr;
    while (main_thread_flag) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;
        for (int i = 0; i < n; i++) {
            FD_SET(fds[i], &rfds);
            if (fds[i] > maxfd) maxfd = fds[i];
        }
        struct timeval tv = {1, 0};
        if (select(maxfd + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
        for (int i = 0; i < n; i++) {
            if (!FD_ISSET(fds[i], &rfds)) continue;
            struct input_event ev[32];
            ssize_t r;
            while ((r = read(fds[i], ev, sizeof(ev))) > 0) {
                int cnt = (int) (r / (ssize_t) sizeof(struct input_event));
                for (int j = 0; j < cnt; j++) {
                    if (ev[j].type != EV_KEY || ev[j].value != 1) continue;
                    if (ev[j].code == KEY_VOLUMEDOWN) g_menuMinimized = true;
                    else if (ev[j].code == KEY_VOLUMEUP) g_menuMinimized = false;
                }
            }
        }
    }
    for (int i = 0; i < n; i++) close(fds[i]);
    return nullptr;
}

__attribute__((visibility("default"))) int main(int argc, char *argv[]) {
    printf("[+] PANXCZ MLBB v0.7\n");
    pid = pidof(oxorany("com.mobile.legends:UnityKillsMe"));
    if (!pid) {
        printf("[~] UnityKillsMe not found, trying main process...\n");
        pid = pidof(oxorany("com.mobile.legends"));
    }
    g_pid = pid;
    if (!pid) {
        printf("[-] Game not running yet (com.mobile.legends)\n");
        return -1;
    }
    libbase = GetBase(oxorany("libcsharp.so"));
    if (!libbase) {
        printf("[~] libcsharp.so not found! Trying liblogic.so...\n");
        libbase = GetBase(oxorany("liblogic.so"));
    }
    printf("[+] Lib base: %p \n", (void*)libbase);
    if (!libbase) {
        printf("[-] Game lib not found\n");
        return -1;
    }
    screen_config();
    ::abs_ScreenX = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::abs_ScreenY = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_x = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    // NOTE: y harus dimensi MIN, bukan max (dulu salah -> surface lebih tinggi dari layar)
    ::native_window_screen_y = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    printf("[+] Screen: %dx%d\n", abs_ScreenX, abs_ScreenY);
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        return -1;
    }
    Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, false);
    LoadCfg();   // muat kalibrasi & pengaturan terakhir (biar minimap/dot ga reset)
    pthread_t volTh;
    if (pthread_create(&volTh, nullptr, VolumeKeyWatcher, nullptr) == 0) {
        pthread_detach(volTh);
        printf("[+] Volume key control active (Vol- minimize / Vol+ expand)\n");
    }
    ApplyTheme();
    int saveTick = 0;
    while (main_thread_flag) {
        if ((++saveTick % 1500) == 0) SaveCfg();   // autosave tiap ~1.5 detik
        MonsterRetribution();
        CheckAndTriggerRetribution();
        ApplyDroneView();
        // hasil kalibrasi 1-tap: gerakkan dot marker ke posisi logical yang sama
        if (g_retriLogicalX >= 0.0f) {
            retriTouchX = g_retriLogicalX;
            retriTouchY = g_retriLogicalY;
            g_retriLogicalX = -1.0f;
            g_retriLogicalY = -1.0f;
        }
        //RoomInfoList();
        drawBegin();
        Layout_tick_UI();
        drawEnd();
        usleep(1000);
    }
    SaveCfg();
    shutdown();
    Touch_Close();
    return 0;
}

