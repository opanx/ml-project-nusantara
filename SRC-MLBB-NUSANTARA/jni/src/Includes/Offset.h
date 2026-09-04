#ifndef OFFSET_H
#define OFFSET_H

// ============================================
// MLBB Offsets - Dari dump.cs (com.mobile.legends v22.1.97.12061)
// Diverifikasi jalan di srcshen (v1.0)
// Updated: 2026-09-04
// ============================================

// ---- BattleManager ----
// Chain: libbase+0x62dc5e0 (slot) -> +0xa8 (static_fields) -> deref (Instance @0x0)
#define OFF_BATTLE_MANAGER           0x62dc5e0  // Static instance slot
#define OFF_BM_STATIC_FIELDS         0xa8       // static_fields offset
#define OFF_LOCAL_PLAYER_SHOW        0x48       // ShowPlayer (m_LocalPlayerShow)
#define OFF_SHOW_PLAYERS             0x70       // List<ShowPlayer>
#define OFF_SHOW_MONSTERS            0x78       // List<ShowEntity>
#define OFF_MAIN_TOWER_DEAD          0x88       // bool

// ---- ShowEntity (base class) ----
#define OFF_ENTITY_TYPE              0x78       // m_iType (int)
#define OFF_ENTITY_CAMP              0xd0       // m_EntityCampType (int)
#define OFF_ENTITY_DEATH             0xc5       // m_bDeath (bool)
#define OFF_ENTITY_ID                0x18c      // m_ID (int)
#define OFF_ENTITY_LEVEL             0x190      // m_Level (int)
#define OFF_ENTITY_HP                0x1a4      // m_Hp (int)
#define OFF_ENTITY_HP_MAX            0x1a8      // m_HpMax (int)
#define OFF_ENTITY_POSITION          0x28c      // m_vCachePosition (Vector3)
#define OFF_ENTITY_SAME_CAMP         0x2a9      // m_bSameCampType (bool)
#define OFF_ENTITY_SHOW_LAYER        0x32c      // bShowEntityLayer (bool)
#define OFF_ENTITY_SKIN_ID           0x2b8      // m_SkinId (int)
#define OFF_ENTITY_MOVE_SPEED        0x230      // m_dMoveSpeed (double)
#define OFF_ENTITY_COOLDOWN_COMP     0xf8       // m_ShowCoolDownComp

// ---- ShowPlayer (extends ShowEntity) ----
#define OFF_PLAYER_HERO_NAME         0x8d0      // m_HeroName (string)
#define OFF_PLAYER_HERO_MAP          0x8d8      // m_HeroSmallMap (string)
#define OFF_PLAYER_POS               0x8e0      // m_iPos (int)
#define OFF_PLAYER_TEAM_ID           0x8e8      // m_iTeamId (ulong)
#define OFF_PLAYER_TEAM_LEVEL        0x8f0      // m_uTeamLevel (uint)
#define OFF_PLAYER_TEAM_NAME         0x8f8      // m_sTeamName (string)
#define OFF_PLAYER_CERTIFY           0x900      // m_iCertify (uint)
#define OFF_PLAYER_RANK_LEVEL        0x90c      // m_uiRankLevel (uint)
#define OFF_PLAYER_DEFENCE_RANK      0x910      // m_uiDefenceRankLevel (uint)
#define OFF_PLAYER_ROLE_LEVEL        0x914      // m_uiRoleLevel (uint)
#define OFF_PLAYER_SUMMON_SKILL      0x95c      // m_iSummonSkillId (int)
#define OFF_PLAYER_SEX               0x9b8      // m_Sex (uint)
#define OFF_PLAYER_GOLD              0xa24      // _iGold (int)
#define OFF_PLAYER_KILL              0x9e0      // m_killNum (int)
#define OFF_PLAYER_ASSIST            0x9e4      // m_assistNum (int)
#define OFF_PLAYER_DEAD              0x9e8      // m_deadNum (int)
#define OFF_PLAYER_KILL_WILD         0xa28      // m_KillWildTimes (int)
#define OFF_PLAYER_KILL_TOWER        0xa20      // m_KillTowerTimes (int)
#define OFF_PLAYER_KILL_SOLDIER      0xa24      // m_KillSoldierTimes (int)
#define OFF_PLAYER_CONT_KILL         0xa00      // continueKill (int)
#define OFF_PLAYER_MULTI_KILL        0xa04      // mutiKill (int)
#define OFF_PLAYER_DOUBLE_KILL       0xa10      // m_DoubleKillTimes (int)
#define OFF_PLAYER_TRIPLE_KILL       0xa14      // m_TripleKillTimes (int)
#define OFF_PLAYER_QUADRA_KILL       0xa18      // m_QuadraKillTimes (int)
#define OFF_PLAYER_PENTA_KILL        0xa1c      // m_PentaKillTimes (int)
#define OFF_PLAYER_ZONE_ID           0x938      // m_uZoneID (uint)
#define OFF_PLAYER_ROOM_ID           0x940      // m_ulRoomID (ulong)

// ---- Monster IDs ----
#define MONSTER_LORD                 2002
#define MONSTER_TURTLE               2003
#define MONSTER_RED_BUFF             2004
#define MONSTER_BLUE_BUFF            2005
#define MONSTER_CRAB                 2006
#define MONSTER_LITO                 2056
#define MONSTER_POKA                 2059
#define MONSTER_CRAMMER              2008
#define MONSTER_FIREFROG             2009
#define MONSTER_SPIDER               2011
#define MONSTER_LITHO                2012
#define MONSTER_BEAR                 2013

// ---- Event Monsters ----
#define MONSTER_EVENT_START          2220
#define MONSTER_EVENT_END            2232

// ---- ShowEntity Types ----
#define ENTITY_TYPE_MINION           1
#define ENTITY_TYPE_MONSTER          2
#define ENTITY_TYPE_TOWER            5

// ---- Camera ----
#define OFF_CAMERA_MAIN              0x75DC470  // Static Camera.main (belum diverifikasi di versi baru, fallback scanner)
#define OFF_CAMERA_TRANSFORM         0x10       // m_Transform
#define OFF_CAMERA_COMPONENT         0xb8       // Component base
#define OFF_CAMERA_VIEW_MATRIX       0x5C       // worldToCameraMatrix + projectionMatrix

// ---- LogicBattleManager ----
#define OFF_LOGIC_BATTLE             0x7680928  // Static instance (lama, fallback)

// ---- Retribution Spell ----
// Formula resmi MLBB: true damage = 750 + 150 * level
#define RETRI_BASE_DAMAGE            750
#define RETRI_PER_LEVEL              150
#define RETRI_BONUS_DAMAGE           0
#define RETRI_BONUS_PER_LEVEL        0
#define RETRI_KILL_THRESHOLD         5          // Wild monster kills (legacy, ga dipakai)
#define RETRI_MAX_DISTANCE           700.0f     // range retri asli MLBB (unit dunia)

// ---- Touch Calibration ----
// Default proporsional ke layar di main() (0.64W, 0.61H). -1 = belum di-set
#define RETRI_TOUCH_X_DEFAULT        -1.0f
#define RETRI_TOUCH_Y_DEFAULT        -1.0f

// ---- Minimap ----
#define MINIMAP_DEFAULT_SIZE         342
#define MINIMAP_DEFAULT_POS          76
#define MINIMAP_SCALE_DEFAULT        74.11f

#endif //OFFSET_H