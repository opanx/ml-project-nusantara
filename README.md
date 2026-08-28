# Panxcz MLBB External Tool v1.3

> **⚠️ EDUCATIONAL PURPOSES ONLY**
> This tool is for learning about memory reading, ImGui overlays, and Android reverse engineering.
> Using cheats in online games violates Terms of Service and may result in account ban.

## What is this?

An external MLBB (Mobile Legends: Bang Bang) tool that reads game memory via `/proc/PID/mem` and renders an ImGui overlay on top of the game. It runs as a standalone ELF binary with root access.

## Features

| Feature | Status | Description |
|---------|--------|-------------|
| **Auto Retri** | ✅ Working | Automatically taps retribution when monster HP < retri damage |
| **Retri Priority** | ✅ Working | Lord > Turtle > Buff priority modes |
| **ESP Line** | ✅ Working | Draw line from you to enemy heroes |
| **Hero Icon ESP** | ✅ Working | Show hero icons + health bars on enemies |
| **Distance & Name** | ✅ Working | Show distance (meters) + hero name |
| **Minimap ESP** | ✅ Working | Red dots on minimap for enemy positions |
| **Kill Steal Alert** | ✅ Working | On-screen alert when monster is retri-able |
| **KDA Display** | ✅ Working | Live K/D/A overlay |
| **Room Info** | ✅ Working | Player info: Name, UID, Hero, Spell, Rank |
| **Hide Screen Recorder** | ✅ Working | Optional window hidden from screen recording |
| **Theme Selection** | ✅ Working | Dark / Light / Classic ImGui themes |

## Retri Priority Modes

| Mode | Targets |
|------|---------|
| **All Targets** (default) | Lord, Turtle, Red, Blue, Crab, Lito |
| **Lord Only** | Lord only |
| **Lord + Turtle** | Lord + Turtle |
| **Buffs Only** | Red Buff + Blue Buff |

## Retribution Damage Formula

```
Base Damage = 600 + (Level - 1) × 80
Bonus Damage = 300 + (Level - 1) × 40 (if Wild Kills ≥ 5)
Total = Base + Bonus
```

## Requirements

- **Rooted Android device** (Magisk / KernelSU / KernelSU Next)
- **ARM64 device** (arm64-v8a)
- **MLBB running** (com.mobile.legends:UnityKillsMe)
- **Android 5.0+**

## How to use

```bash
# 1. Download panxcz from releases
# 2. Push to device
adb push panxcz /data/local/tmp/

# 3. Open terminal on device
adb shell
su -c "chmod 755 /data/local/tmp/panxcz"
su -c "cd /data/local/tmp && ./panxcz"

# 4. When prompted:
#    HideScreenRecorder? 1[YES] 2[NO]:
#    Press 2 for normal mode

# 5. Open MLBB and start a match
# 6. ImGui menu will appear as overlay
```

## Build from source

```bash
# Requires Android NDK r25+
cd SRC-MLBB-NUSANTARA
$NDK_HOME/ndk-build \
  NDK_PROJECT_PATH=. \
  APP_BUILD_SCRIPT=jni/Android.mk \
  NDK_APPLICATION_MK=jni/Application.mk \
  -j$(nproc)

# Output: libs/arm64-v8a/panxcz
```

Or let GitHub Actions build it automatically on push.

## Offsets

All offsets are in `jni/src/Includes/Offset.h` and sourced from `dump.cs` (MLBB v2.1.95).

**When MLBB updates, offsets change.** To update:
1. Dump new IL2CPP metadata
2. Find new offsets in dump.cs
3. Update `Offset.h`
4. Rebuild

### Key Offsets

| Offset | Value | Class |
|--------|-------|-------|
| `OFF_BATTLE_MANAGER` | 0x7641E18 | BattleManager (static) |
| `OFF_LOCAL_PLAYER_SHOW` | 0x50 | BattleManager.m_LocalPlayerShow |
| `OFF_SHOW_PLAYERS` | 0x78 | BattleManager.m_ShowPlayers |
| `OFF_SHOW_MONSTERS` | 0x80 | BattleManager.m_ShowMonsters |
| `OFF_ENTITY_HP` | 0x1ac | ShowEntity.m_Hp |
| `OFF_ENTITY_HP_MAX` | 0x1b0 | ShowEntity.m_HpMax |
| `OFF_ENTITY_POSITION` | 0x294 | ShowEntity.m_vCachePosition |
| `OFF_PLAYER_HERO_NAME` | 0x8d8 | ShowPlayer.m_HeroName |
| `OFF_PLAYER_KILL` | 0x9e8 | ShowPlayer.m_killNum |
| `OFF_PLAYER_KILL_WILD` | 0xa28 | ShowPlayer.m_KillWildTimes |

## Monster IDs

| ID | Monster |
|----|---------|
| 2002 | Lord |
| 2003 | Turtle |
| 2004 | Red Buff |
| 2005 | Blue Buff |
| 2006 | Crab |
| 2056 | Lithowanderer |
| 2220-2232 | Event Monsters |

## Known Limitations

- **Root required** — cannot work without root access
- **External only** — reads memory from outside the game process
- **Offset-dependent** — breaks when game updates (need new dump.cs)
- **No injection** — does not modify game code, only reads memory
- **Touch simulation** — retri tap may not work on all devices
- **Screen recorder hide** — may not work on all ROMs

## How it works

1. **PID lookup** — finds MLBB process via `pidof`
2. **Base address** — reads `/proc/PID/maps` to find `libcsharp.so` base
3. **Memory read** — uses `process_vm_readv` syscall to read game memory
4. **Data parsing** — reads BattleManager → ShowPlayers/ShowMonsters lists
5. **World-to-screen** — converts 3D game coordinates to 2D screen coordinates
6. **ImGui overlay** — renders ESP, menu, alerts on transparent OpenGL window
7. **Touch injection** — simulates touch events via `/dev/uinput` for retri

## File structure

```
SRC-MLBB-NUSANTARA/
├── jni/
│   ├── Android.mk          # NDK build config
│   ├── Application.mk      # ABI + platform config
│   ├── include/
│   │   └── Memory/
│   │       ├── Memory.h    # Memory R/W API
│   │       └── PatternScanner.h
│   └── src/
│       ├── main.cpp         # Main entry + ImGui UI
│       ├── Includes/
│       │   ├── Offset.h     # All offsets (UPDATE THIS!)
│       │   └── Log.h
│       ├── Android_draw/
│       │   └── draw.cpp     # EGL + ImGui init
│       ├── Android_touch/
│       │   └── TouchHelperA.cpp  # Touch injection
│       ├── Memory/
│       │   └── Memory.cpp   # Process memory R/W
│       ├── ImGui/           # Dear ImGui library
│       ├── Engine/          # Canvas/WorldToScreen
│       └── ...
├── libs/
│   └── arm64-v8a/
│       └── panxcz           # Output binary
com.mobile.legends_64bit.cs  # IL2CPP dump
```

## What I fixed in v1.3

| Issue | Fix |
|-------|-----|
| SIGSEGV on launch | Added null checks for PID + libbase |
| EGL init skipped | Moved EGL init outside HideScreenRecorder if/else |
| Memory.cpp syntax error | Fixed double quote in `#include` |
| No error messages | Added clear `printf` messages |
| Wrong offsets | Updated from dump.cs (m_HeroName, m_Level, etc.) |
| Hardcoded offsets | Moved all to `Offset.h` |
| Binary name | Renamed from `rsa` to `panxcz` |

## Credits

- **ImGui** — ocornut/imgui
- **Memory read** — process_vm_readv syscall
- **Touch injection** — /dev/uinput
- **Dump** — IL2CPP Dumper

## Disclaimer

This tool is provided for **educational purposes only**. The author is not responsible for any misuse, account bans, or legal consequences. Use at your own risk.

---

**© Panxcz & Freebuff** 🎮
