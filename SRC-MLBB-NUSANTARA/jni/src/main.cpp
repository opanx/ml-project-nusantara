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
#include <linux/input.h>
#include <vector>
#include <functional>
#include "Memory/Memory.h"
#include "Memory/PatternScanner.h"

#include "Quaternion.hpp"
#include "Vector2.hpp"
#include "Vector3.hpp"

#include "Includes/Offset.h"
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
float RadiusCir = 50.0f;
long libbase = 0;
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
    auto main_cam = Read<uintptr_t>(libbase + 0x75DC470);
    if (!main_cam)
        return 0;
    auto main_cam2 = Read<uintptr_t>(main_cam + 0xb8);
    if (!main_cam2)
        return 0;
    auto main_cam3 = Read<uintptr_t>(main_cam2 + 0x8);
    if (!main_cam3)
        return 0;
    return main_cam3;
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

void Touch_Tap(int x, int y) {
     Touch_Down((float)x, (float)y);
     usleep(80000);
     Touch_Up();
}

bool lastRetriTriggered[20] = {false};
bool autoRetribution = false;
bool AutoRetributionRed = false;
bool AutoRetributionBlue = false;
bool AutoRetributionLord = false;
bool AutoRetributionTurtle = false;
bool AutoRetributionCrab = false;
bool AutoRetributionLito = false;

float retriTouchX = RETRI_TOUCH_X_DEFAULT;
float retriTouchY = RETRI_TOUCH_Y_DEFAULT;

// === NEW FEATURES ===
bool killStealAlert = false;
bool showSpellCooldown = false;
bool showKDA = false;
bool autoRetriPriority = false;  // Lord > Turtle > Buff priority
int retriPriorityMode = 0;       // 0=all, 1=lord only, 2=lord+turtle, 3=buffs only
float killStealThreshold = 0.15f; // 15% HP threshold for KS alert
char alertMessage[256] = {0};
float alertTimer = 0.0f;

struct SpellInfo {
    int spellId;
    const char* name;
    float cooldown;
    float remaining;
};

void DrawMonster(ImDrawList *Draw) {
    if (autoRetribution) {
        ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(retriTouchX, retriTouchY), 18.0f, IM_COL32(255, 255, 255, 180), 16);
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(retriTouchX, retriTouchY), 18.0f, IM_COL32(0, 0, 0, 255), 16, 2.5f);
    }
    if (abs_ScreenX < abs_ScreenY) return;
    
    float lineSize = abs_ScreenY / 432;
    long a1 = getPtr641(libbase + OFF_BATTLE_MANAGER);
    long a2 = getPtr641((a1 + ((0x100 | 0xB8) & 0xFF)));
    long a32 = getPtr641((a2 << 1) >> 1);

    /**
    class BattleManager
    perlu update dari dump.cs*
    **/
    // Offsets now from Offset.h
    
    /**
    class ShowEntity 
    perlu update dari dump.cs*
    **/

    /**
    class ShowPlayer 
    perlu update dari dump.cs*
    **/
    size_t m_HeroName = OFF_PLAYER_HERO_NAME;
    
    long selfp = getPtr641(a32 + OFF_LOCAL_PLAYER_SHOW);
    
    auto main_cam = GetMainCamera();

    auto camera = Read<uintptr_t>(main_cam + 0x10);
    
    auto ViewMatrix = Read<Camera>(camera + 0x5C);
    _vMatrix = ViewMatrix.projectionMatrix * ViewMatrix.worldToCameraMatrix;

    long player = getPtr641(getPtr641(a32+OFF_SHOW_PLAYERS)+0x10)+0x20;
    uint stop_player = Read<uint>(getPtr641(a32+OFF_SHOW_PLAYERS)+0x18);
    
    for (int i = 0; i < stop_player; i++) {
        auto Objaddr = getPtr641(player + ((i << 3) / 1));

        if ((Objaddr ^ 0x0) == 0x0) {
            continue;
        }

        auto is_team = Read<bool>(Objaddr + OFF_ENTITY_SAME_CAMP);
        if (is_team) {
            continue;
        }
        auto HeroID = Read<int>(Objaddr + OFF_ENTITY_ID);     

        auto death = Read<bool>(Objaddr + OFF_ENTITY_DEATH);
        if (death) {
            continue;
        }

        int Health = Read<int>(Objaddr + OFF_ENTITY_HP);
        if(Health <= 0)
        {
            continue;
        }

        int maxHealth = Read<int>(Objaddr + OFF_ENTITY_HP_MAX);
        if(maxHealth <= 0)
        {
            continue;
        }

        Vector3 Z;
        vm_readv(selfp + OFF_ENTITY_POSITION, &Z, sizeof(Z));
      
        Vector3 D;
        vm_readv(Objaddr + OFF_ENTITY_POSITION, &D, sizeof(D));
        
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
    
        if (drawMHealth) {
            ImGui::GetForegroundDrawList()->AddLine({loc_posSc.X,loc_posSc.Y}, {en_posSc.X, en_posSc.Y}, ImColor(255, 255, 255), lineSize);
        }
        
        if (iconhero) {
            ImVec2 iconPos(HeroPos.X, HeroPos.Y);
        DrawHeroIcon(ImGui::GetBackgroundDrawList(), iconPos, HeroID, Health, maxHealth);
        }

        if (drawMDistance) {
            std::string s;
            s += std::to_string((int)Distance);
            s += "m | ";
            s += "Health: "+ std::to_string((int)Health);
            s += " | " + fshy(Read<uintptr_t>(Objaddr + m_HeroName));

            auto textSize1 = ImGui::CalcTextSize(s.c_str(), 0, 29);
            绘制字体描边(22.5,HeroPos.X - (textSize1.x / 2), HeroPos.Y,ImColor(248,248,255),s.c_str());
        }

    }
    long monster = getPtr641(getPtr641(a32+OFF_SHOW_MONSTERS)+0x10)+0x20;
    uint stop_monster = Read<uint>(getPtr641(a32+OFF_SHOW_MONSTERS)+0x18);
    
    for (int i = 0; i < stop_monster; i++) {
        auto Objaddr = getPtr641(monster + ((i << 3) / 1));

        if ((Objaddr ^ 0x0) == 0x0) {
            continue;
        }

        auto is_team = Read<bool>(Objaddr + OFF_ENTITY_SAME_CAMP); // m_bSameCampType
        if (is_team) {
            continue;
        }

        auto mHeroID = Read<int>(Objaddr + OFF_ENTITY_ID);        
        auto type = Read<int>(Objaddr + OFF_ENTITY_TYPE);
        
        auto death = Read<bool>(Objaddr + OFF_ENTITY_DEATH);
        if (death) {
            continue;
        }

        int Health = Read<int>(Objaddr + OFF_ENTITY_HP);
        if(Health <= 0)
        {
            continue;
        }
        
        int maxHealth = Read<int>(Objaddr + OFF_ENTITY_HP_MAX);
        if(maxHealth <= 0)
        {
            continue;
        }     
        
        Vector3 ZL;
        vm_readv(selfp + OFF_ENTITY_POSITION, &ZL, sizeof(ZL));
      
        Vector3 Dm;
        vm_readv(Objaddr + OFF_ENTITY_POSITION, &Dm, sizeof(Dm));
        
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
    uintptr_t BattleManager = getPtr641(libbase + OFF_BATTLE_MANAGER);
    BattleManager = getPtr641(BattleManager + 0xB8);
    BattleManager = getPtr641(BattleManager);

    if(!BattleManager) return;
    Oneself = getPtr641(BattleManager + OFF_LOCAL_PLAYER_SHOW);
    if(!Oneself) return;

    Vector3 MyPosition;
    vm_readv(Oneself + OFF_ENTITY_POSITION, &MyPosition, sizeof(MyPosition));
    
    MonsterCount = 0;
    uintptr_t Showmonster = getPtr641(BattleManager + OFF_SHOW_MONSTERS);
    if (Showmonster != 0) {
        int monsterCount = Read<int>(Showmonster + 0x18);
        uintptr_t monsterDataPtr = ReadPtr(Showmonster + 0x10);
        if (monsterCount >= 0 && monsterCount <= 100 && monsterDataPtr != 0) {
            uintptr_t monsterDataArray = monsterDataPtr + 0x20;
            int monsterfound = 0;
            for (int i = 0; i < monsterCount && monsterfound < 20; i++) {
                uintptr_t currentMonsterPtr = ReadPtr(monsterDataArray + (i * 8));
                if (currentMonsterPtr == 0) continue;
            int monsterID = Read<int>(currentMonsterPtr + OFF_ENTITY_ID);
            int monsterHP = Read<int>(currentMonsterPtr + OFF_ENTITY_HP);
            int monsterMaxHP = Read<int>(currentMonsterPtr + OFF_ENTITY_HP_MAX);
            Vector3 monsterPos = Read<Vector3>(currentMonsterPtr + OFF_ENTITY_POSITION);
            uint8_t deadFlag = Read<uint8_t>(currentMonsterPtr + OFF_ENTITY_DEATH);
                bool mDead = (deadFlag != 0);
                std::string mName = MonsterToString(monsterID);
                if (mName.empty()) {
                    if (monsterID == 2002) mName = "Lord";
                    else if (monsterID == 2003) mName = "Turtle";
                    else continue;
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

int CalculateRetriDamage(int Level, int KillWild) {
    int baseDamage = RETRI_BASE_DAMAGE + (Level - 1) * RETRI_PER_LEVEL;
    if (KillWild >= RETRI_KILL_THRESHOLD) {
        baseDamage += RETRI_BONUS_DAMAGE + (Level - 1) * RETRI_BONUS_PER_LEVEL;
    }
    return baseDamage;
}

bool isRetriTarget(int monsterId) {
    switch (retriPriorityMode) {
        case 1: return (monsterId == MONSTER_LORD);
        case 2: return (monsterId == MONSTER_LORD || monsterId == MONSTER_TURTLE);
        case 3: return (monsterId == MONSTER_RED_BUFF || monsterId == MONSTER_BLUE_BUFF);
        default: // mode 0 = all targets
            return (AutoRetributionLord && monsterId == MONSTER_LORD) ||
                   (AutoRetributionTurtle && monsterId == MONSTER_TURTLE) ||
                   (AutoRetributionBlue && monsterId == MONSTER_BLUE_BUFF) ||
                   (AutoRetributionRed && monsterId == MONSTER_RED_BUFF) ||
                   (AutoRetributionCrab && monsterId == MONSTER_CRAB) ||
                   (AutoRetributionLito && monsterId == MONSTER_LITO);
    }
}

int getRetriPriority(int monsterId) {
    // Higher = higher priority for auto retri
    if (monsterId == MONSTER_LORD) return 100;
    if (monsterId == MONSTER_TURTLE) return 80;
    if (monsterId == MONSTER_RED_BUFF || monsterId == MONSTER_BLUE_BUFF) return 60;
    if (monsterId >= MONSTER_EVENT_START && monsterId <= MONSTER_EVENT_END) return 50;
    if (monsterId == MONSTER_CRAB) return 40;
    if (monsterId == MONSTER_LITO) return 30;
    return 0;
}

void CheckAndTriggerRetribution() {
    if (!autoRetribution || !Oneself || MonsterCount <= 0) return;
    int myLevel = Read<int>(Oneself + OFF_ENTITY_LEVEL);
    int killWild = Read<int>(Oneself + OFF_PLAYER_KILL_WILD);
    int retriDmg = CalculateRetriDamage(myLevel, killWild);
    
    int bestIdx = -1;
    int bestPriority = -1;
    
    for (int i = 0; i < MonsterCount; i++) {
        if (!monster[i].isValid || monster[i].isDead) {
            lastRetriTriggered[i] = false;
            continue;
        }
        if (monster[i].distance > 5.0f) {
            lastRetriTriggered[i] = false;
            continue;
        }
        int id = Read<int>(monster[i].address + OFF_ENTITY_ID);
        
        if (!isRetriTarget(id)) {
            lastRetriTriggered[i] = false;
            continue;
        }
        
        int priority = getRetriPriority(id);
        if (monster[i].health <= retriDmg && priority > bestPriority) {
            bestPriority = priority;
            bestIdx = i;
        }
    }
    
    // Trigger retri on highest priority target
    for (int i = 0; i < MonsterCount; i++) {
        if (i == bestIdx) {
            if (!lastRetriTriggered[i]) {
                Touch_Tap(retriTouchX, retriTouchY);
                lastRetriTriggered[i] = true;
                // Kill Steal Alert
                if (killStealAlert) {
                    snprintf(alertMessage, sizeof(alertMessage), "RETRI: %s (%d HP)",
                             monster[i].name, monster[i].health);
                    alertTimer = 3.0f;
                }
            }
        } else {
            lastRetriTriggered[i] = false;
        }
    }
}

void RoomInfoList() {
    uintptr_t LogicBattleManager = getPtr641(libbase + OFF_LOGIC_BATTLE);
    if (!LogicBattleManager) return;

    long playersList = getPtr641(getPtr641((uintptr_t)LogicBattleManager + OFF_SHOW_PLAYERS) + 0x10);
    int playerCount = Read<int>(getPtr641((uintptr_t)LogicBattleManager + OFF_SHOW_PLAYERS) + 0x18);
    if (playerCount <= 0 || !playersList) return;

    long a1 = getPtr641(libbase + OFF_BATTLE_MANAGER);
    long a2 = getPtr641((a1 + ((0x100 | 0xB8) & 0xFF)));
    long a32 = getPtr641((a2 << 1) >> 1);

    long selfp = getPtr641(a32 + OFF_LOCAL_PLAYER_SHOW);

    if (!selfp) return;

    uint32_t myTeamCamp = Read<uint32_t>(selfp + OFF_PLAYER_POS);

    int playerB = 0;
    int playerR = 0;

    for (int i = 0; i < playerCount; i++) {
        long obj = getPtr641(playersList + i * 8);
        if (!obj) continue;

        auto nameObj = *(String**)(obj + OFF_PLAYER_HERO_NAME);
        std::string name = (nameObj && nameObj->CString()) ? nameObj->CString() : "Unknown";

        uint64_t lUid = Read<uint64_t>(obj + OFF_PLAYER_CERTIFY);
        uint32_t zoneId = Read<uint32_t>(obj + OFF_PLAYER_ZONE_ID);
        std::string uid = std::to_string(lUid) + " (" + std::to_string(zoneId) + ")";

        uint32_t heroid = Read<uint32_t>(obj + OFF_ENTITY_ID);
        int spellId = Read<int>(obj + OFF_PLAYER_CERTIFY + 0x4);
        uint32_t rank = Read<uint32_t>(obj + OFF_PLAYER_RANK_LEVEL);
        uint32_t myth = Read<uint32_t>(obj + OFF_PLAYER_ROLE_LEVEL);
        uint32_t camp = Read<uint32_t>(obj + OFF_PLAYER_POS);

        std::string hero = HeroToString(heroid);
        std::string spell = SpellToString(spellId);
        std::string rankStr = RankToString(rank, myth);

        if (camp == myTeamCamp && playerB < 5) {
            PlayerB[playerB++] = { name, uid, hero, spell, rankStr };
        } else if (playerR < 5) {
            PlayerR[playerR++] = { name, uid, hero, spell, rankStr };
        }
    }
}

int MinimapSize = 342;
int MinimapPos = 76;
bool MinimapIcon = true;
bool HideLine = false;

float g_MinimapScale = 74.11f;
float g_Res0_MultX = 1.0f;
float g_Res0_MultY = 1.0f;
float g_Res1_OffsetX = 0.0f;
float g_Res1_OffsetY = 0.0f;
int g_ICSize = 38;

Vector2 WorldToMinimap(Vector3 HeroPosition) {
    float angle = 314.60f * 0.017453292519943295f;
    float angleCos = std::cos(angle);
    float angleSin = std::sin(angle);

    Vector2 Res0;
    Res0.X = ((angleCos * HeroPosition.X - angleSin * (-HeroPosition.Z)) / g_MinimapScale) * g_Res0_MultX;
    Res0.Y = ((angleSin * HeroPosition.Y + angleCos * (-HeroPosition.Z)) / g_MinimapScale) * g_Res0_MultY;

    Vector2 Res1;
    Res1.X = (Res0.X * MinimapSize) + MinimapPos + MinimapSize / 2.0f + g_Res1_OffsetX;
    Res1.Y = (Res0.Y * MinimapSize) + MinimapSize / 2.0f + g_Res1_OffsetY;

    return Res1;
}

void DrawMinimapESP(ImDrawList* draw) {
    if (!MinimapIcon) return;

    long a1 = getPtr641(libbase + OFF_BATTLE_MANAGER);
    if (!a1) return;

    long a2 = getPtr641(a1 + 0xB8);
    if (!a2) return;

    long a32 = getPtr641(a2);
    if (!a32) return;

    long showList = getPtr641(a32 + OFF_SHOW_PLAYERS);
    if (!showList) return;

    long playerList = getPtr641(showList + 0x10);
    if (!playerList) return;
    playerList += 0x20;

    uint playerCount = Read<uint>(showList + 0x18);
    for (int i = 0; i < playerCount; i++) {
        long Objaddr = getPtr641(playerList + (i << 3));
        if (!Objaddr) continue;

        if (Read<bool>(Objaddr + OFF_ENTITY_SAME_CAMP)) continue;
        if (Read<bool>(Objaddr + OFF_ENTITY_DEATH)) continue;

        Vector3A pos{};
        vm_readv(Objaddr + OFF_ENTITY_POSITION, &pos, sizeof(pos));
        if (pos.X == 0 && pos.Y == 0 && pos.Z == 0) continue;

        Vector2 minimapPos = WorldToMinimap({ pos.X, pos.Y, pos.Z });
        draw->AddCircleFilled(ImVec2(minimapPos.X, minimapPos.Y), g_ICSize / 2.0f, IM_COL32(255, 0, 0, 255));
    }

    if (!HideLine) {
        draw->AddRect(
            ImVec2(MinimapPos, 0),
            ImVec2(MinimapPos + MinimapSize, MinimapSize),
            IM_COL32(255, 255, 255, 255)
        );
    }
}
void Layout_tick_UI() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize;
ImGui::SetNextWindowSizeConstraints(ImVec2(800, 0), ImVec2(820, FLT_MAX));

ImGui::Begin(oxorany("             Panxcz v0.1 - MLBB Tool"), nullptr, window_flags);


    if (ImGui::BeginTabBar("####")) {

        if (ImGui::BeginTabItem(oxorany("ESP"))) {
            ImGui::Checkbox(oxorany("Line"), &drawMHealth);
            ImGui::Checkbox(oxorany("IconHero"), &iconhero);
            ImGui::Checkbox(oxorany("Distance & Hero Name"), &drawMDistance);
            ImGui::Checkbox(oxorany("Alert Lord Under Attack"), &drawAlertUnderAttack);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Retri"))) {
            ImGui::Checkbox(oxorany("Auto Retri"), &autoRetribution);
            ImGui::SliderFloat(oxorany("Adjust X"), &retriTouchX, 0.0f, 3000.0f, "%.0f");
            ImGui::SliderFloat(oxorany("Adjust Y"), &retriTouchY, 0.0f, 1500.0f, "%.0f");

            ImGui::Separator();
            ImGui::Text(oxorany("Targets:"));
            ImGui::Checkbox(oxorany("Lord"), &AutoRetributionLord);
            ImGui::SameLine();
            ImGui::Checkbox(oxorany("Turtle"), &AutoRetributionTurtle);
            ImGui::Checkbox(oxorany("Buff Red"), &AutoRetributionRed);
            ImGui::SameLine();
            ImGui::Checkbox(oxorany("Buff Blue"), &AutoRetributionBlue);
            ImGui::Checkbox(oxorany("Crab"), &AutoRetributionCrab);
            ImGui::SameLine();
            ImGui::Checkbox(oxorany("Lito"), &AutoRetributionLito);

            ImGui::Separator();
            ImGui::Text(oxorany("Priority Mode:"));
            ImGui::RadioButton(oxorany("All Targets"), &retriPriorityMode, 0);
            ImGui::RadioButton(oxorany("Lord Only"), &retriPriorityMode, 1);
            ImGui::RadioButton(oxorany("Lord + Turtle"), &retriPriorityMode, 2);
            ImGui::RadioButton(oxorany("Buffs Only"), &retriPriorityMode, 3);

            if (Oneself) {
                int myLevel = Read<int>(Oneself + OFF_ENTITY_LEVEL);
                int killWild = Read<int>(Oneself + OFF_PLAYER_KILL_WILD);
                int retriDmg = CalculateRetriDamage(myLevel, killWild);
                ImGui::Separator();
                ImGui::Text(oxorany("Level: %d | Wild: %d"), myLevel, killWild);
                ImGui::Text(oxorany("Retri Damage: %d"), retriDmg);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Combat"))) {
            ImGui::Checkbox(oxorany("Kill Steal Alert"), &killStealAlert);
            ImGui::SliderFloat(oxorany("KS Threshold %%"), &killStealThreshold, 0.05f, 0.50f, "%.0f%%");
            ImGui::Checkbox(oxorany("Show KDA"), &showKDA);
            ImGui::Checkbox(oxorany("Show Spell CD"), &showSpellCooldown);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Minimap"))) {
            if (ImGui::CollapsingHeader("Minimap Setting", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Minimap", &MinimapIcon);
                ImGui::SameLine();
                ImGui::Checkbox("Hide Line", &HideLine);
                ImGui::SliderInt("Minimap Size", &MinimapSize, 100, 600);
                ImGui::SliderInt("Minimap Pos X", &MinimapPos, 0, 800);
                ImGui::SliderInt("Size", &g_ICSize, 1, 100);
                ImGui::Text("WorldToMinimap Tweak:");
                ImGui::SliderFloat("Res0 X Mult", &g_Res0_MultX, 0.1f, 3.0f);
                ImGui::SliderFloat("Res0 Y Mult", &g_Res0_MultY, 0.1f, 3.0f);
                ImGui::SliderFloat("Res1 Offset X", &g_Res1_OffsetX, -200.0f, 200.0f);
                ImGui::SliderFloat("Res1 Offset Y", &g_Res1_OffsetY, -200.0f, 200.0f);
                ImGui::SliderFloat("Minimap Scale", &g_MinimapScale, 10.0f, 150.0f);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Settings"))) {
            static int theme = 0;
            const char* themes[] = { "Dark", "Light", "Classic" };
            if (ImGui::Combo(oxorany("Theme Gui"), &theme, themes, IM_ARRAYSIZE(themes))) {
                if (theme == 0) ImGui::StyleColorsDark();
                if (theme == 1) ImGui::StyleColorsLight();
                if (theme == 2) ImGui::StyleColorsClassic();
            }
            static float opacity = 1.0f;
            ImGui::SliderFloat(oxorany("UI Opacity"), &opacity, 0.1f, 1.0f);
            ImGui::GetStyle().Alpha = opacity;
            ImGui::Text(oxorany("Current FPS: %.1f"), ImGui::GetIO().Framerate);
            if (ImGui::Button(oxorany("Exit Cheat"))) {
                main_thread_flag = false;
            }
            if (ImGui::Button(oxorany("Unload Cheat"))) {
                exit(0);
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

 
    // Kill Steal Alert overlay
    if (killStealAlert && alertTimer > 0.0f) {
        auto drawList = ImGui::GetForegroundDrawList();
        auto textSize = ImGui::CalcTextSize(alertMessage);
        ImVec2 pos((abs_ScreenX - textSize.x) / 2.0f, abs_ScreenY * 0.15f);
        drawList->AddRectFilled(ImVec2(pos.x - 10, pos.y - 5),
                                ImVec2(pos.x + textSize.x + 10, pos.y + textSize.y + 5),
                                IM_COL32(255, 50, 50, (int)(200 * alertTimer / 3.0f)));
        drawList->AddText(pos, IM_COL32(255, 255, 255, 255), alertMessage);
        alertTimer -= ImGui::GetIO().DeltaTime;
    }

    // KDA Display
    if (showKDA && Oneself) {
        int kills = Read<int>(Oneself + OFF_PLAYER_KILL);
        int deaths = Read<int>(Oneself + OFF_PLAYER_DEAD);
        int assists = Read<int>(Oneself + OFF_PLAYER_ASSIST);
        char kdaBuf[64];
        snprintf(kdaBuf, sizeof(kdaBuf), "K/D/A: %d/%d/%d", kills, deaths, assists);
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(10, abs_ScreenY * 0.05f), IM_COL32(255, 255, 100, 255), kdaBuf);
    }

    if (MinimapIcon) DrawMinimapESP(ImGui::GetForegroundDrawList());
    DrawMonster(ImGui::GetForegroundDrawList());
    g_window = ImGui::GetCurrentWindow();
    ImGui::End();
}

__attribute__((visibility("default"))) int main(int argc, char *argv[]) {
    pid = pidof(oxorany("com.mobile.legends:UnityKillsMe"));
    g_pid = pid;
    libbase = GetBase(oxorany("libcsharp.so"));
    printf("Lib: %p \n", libbase);
    screen_config();
    ::abs_ScreenX = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::abs_ScreenY = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_x = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_y = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        return -1;
    }
    Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, false);
    ImGui::GetStyle().WindowRounding = 25.0f;
    while (main_thread_flag) {
        MonsterRetribution();
        CheckAndTriggerRetribution();
        RoomInfoList();
        drawBegin();
        Layout_tick_UI();
        drawEnd();
        usleep(1000);
    }
    shutdown();
    Touch_Close();
    return 0;
}

