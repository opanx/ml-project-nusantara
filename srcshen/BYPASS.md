# 🛡️ Panxcz Direct-Call Bypass — Penjelasan Lengkap (v1.5)

> **Educational use only.** Ini dokumentasi teknis dari engine `RemoteCall.h` yang
> dipakai fitur **Direct Call** di tab Auto Retri. Bacaan wajib buat yang mau
> paham kenapa cara kita beda dari tool lain, dan cara tuning RVA cast retri.

---

## 1. Masalah yang kita selesaikan

Temen lo nawarin 2 cara cast retri otomatis:

| Cara | Kecepatan | Risiko | Keterangan |
|------|-----------|--------|------------|
| **Method 1 — Direct Call** | ⚡ Cepat (ms) | Butuh bypass kuat | Panggil langsung fungsi cast retri DI DALAM proses game |
| **Method 2 — Touch button** | 🐢 Lambat (harus tap tombol di layar) | Aman | Simulasi sentuhan tombol retri (cara herz-kimmy) |

Kalau bypass-nya lemah, method 1 = jalan pintas menuju ban (Aegis langsung
deteksi). Kalau bypass-nya kuat, method 1 jauh lebih presisi: **ga perlu**
posisi tombol, ga kena masalah "tap nyasar ke joystick", ga kena delay input.

**Bypass kita dirancang khusus biar method 1 layak dipakai.**

---

## 2. Arsitektur bypass kita (3 fase, ~5ms, SEKALI per match)

### Fase 1 — ATTACH (sekali, saat match mulai)
```
PTRACE_SEIZE → PTRACE_INTERRUPT → cari region RWX milik game (ART JIT code cache)
```
- `PTRACE_SEIZE` (bukan `PTRACE_ATTACH`) = attach senyap, tanpa "signal storm"
  SIGSTOP yang gampang dikira anti-cheat sebagai anomali.
- Region RWX yang kita pinjam = **JIT code cache milik ART** — mapping yang
  memang wajar ada di tiap proses Android 8+ (game MLBB jalan di atas ART).
  Kita nulis stub di situ, byte aslinya di-backup dulu, dan **selalu
  dikembalikan** setelah selesai → JIT cache balik 100% orisinal.

### Fase 2 — INSTALL (sekali, masih dalam attach itu)
Stub (hanya syscall, tanpa libc) jalan 1× di proses game:
```
mmap(0x2000, RWX)                    → region trampoline (di luar .so game)
mprotect(page0 → RX)                 → code executable, sisa page1 RW (scratch ack)
rt_sigaction(SIGRTMIN+8, trampoline) → handler dipasang, oldact di-save
```
Lalu: **byte JIT dikembalikan → PTRACE_DETACH**. `TracerPid` balik ke `0`.

### Fase 3 — FIRE (tiap cast retri, tanpa ptrace sama sekali)
```
tgkill(pid, tid_main, SIGRTMIN+8)    ← satu sinyal, itu saja
```
- Sinyal diterima oleh **main thread game** (thread logic battle, bukan thread
  asing) → kernel menjalankan trampoline kita.
- Trampoline (11 instruksi, murni ARM64, tanpa libc):
  1. `ldr` address player dari literal → baca `m_iSummonSkillId` (`+0x95c`)
  2. `blr` → panggil **fungsi cast milik game sendiri** (absolute address)
  3. tulis magic `1` ke scratch → tool kita konfirmasi via `process_vm_readv`
  4. `ret` → balik ke kernel `rt_sigreturn`, game jalan normal lagi
- Tool tidak pernah menyentuh proses game saat FIRE: tidak ada ptrace, tidak
  ada write, tidak ada thread baru. Cuma satu sinyal OS.

### Cleanup
Saat tool exit / toggle off / ganti match: attach singkat lagi →
`rt_sigaction` di-restore ke handler asli + `munmap` region trampoline →
byte JIT dikembalikan. **Bahkan forensik pasca-match ga nemu apa-apa.**

---

## 3. Kenapa ini BEDA dari tool lain (dan lebih kuat)

| Aspek | Tool lain | **Panxcz (kita)** |
|-------|-----------|-------------------|
| **ptrace** | GameGuardian & sejenisnya: attach **selama sesi** → `TracerPid` kebaca anti-cheat sepanjang match → flag instan | Attach cuma **~5ms sekali di awal match**, langsung detach. Selama gameplay `TracerPid: 0`. Sinyal `kill()` sama sekali ga perlu ptrace. |
| **Modifikasi kode game** | Inline hook / patch .text (biar fungsi jalan) → **hash integrity check** langsung kena | **0 byte** sentuh `.text` game. Trampoline di region anon baru (RX), bukan di library game. Hash game tetap valid. |
| **Thread baru** | Injector `pthread_create` / Frida spawn thread baru → kelihatan di `/proc/pid/task` + stack asing | **0 thread baru**. Trampoline jalan di main thread game yang sudah ada, frame stack = event async native. |
| **Footprint runtime** | Frida / LSPosed / agent .so: library asing di maps, /proc scans, port/pipe | 1 region anon RX kecil (wajar seperti ART JIT) + tidak ada pipe/port/thread. Tool kita sendiri juga ga nempel di proses game. |
| **Trigger** | Baca-tulis memori tiap frame (kecepatan hack kelihatan) | 1 sinyal per cast, jeda natural (cooldown retri 30s + retry interval). |
| **Integrity .so** | Patch opcode → hash beda → ban | `liblogic.so` / `libcsharp.so` tidak pernah diubah. |
| **Cadence** | Spam / tap 100ms-an → heuristik perilaku kena | Sama persis kayak manusia: 1 cast, nunggu CD, jitter. |
| **Cleanup** | Banyak tool "matiin" tapi ninggalin hook/thread/pipe | Restore total: handler asli balik, region di-munmap, JIT dikembalikan. |

**Inti perbedaannya:** tool lain meninggalkan *jejak permanen atau
semi-permanen* di proses game selama sesi (ptrace nempel, kode ke-patch,
thread asing). Kita meninggalkan **jejak ~0**: satu kali install 5ms yang
self-cleaning, dan semua cast berikutnya cuma lewat sinyal OS biasa yang
dieksekusi oleh kode game sendiri.

---

## 4. Komponen bypass (kode)

| File | Isi |
|------|-----|
| `srcshen/jni/src/Includes/RemoteCall.h` | Engine lengkap: attach, stub mmap/mprotect/sigaction, trampoline, fire, uninstall. Header-only (ga ubah Android.mk). |
| `srcshen/jni/src/main.cpp` → `RetriBypassTick()` | Install ulang otomatis tiap ganti match (Oneself berubah), uninstall kalau toggle off. |
| `main.cpp` → `DoRetriTap()` | Prioritaskan `RC::Fire()` kalau bypass ready, fallback ke touch. |
| Tab **Auto Retri → Direct Call (Bypass)** | Toggle + status INSTALLED/FAILED live di UI. |

Catatan implementasi penting:
- **Sinyal dipilih `40` (SIGRTMIN+8)** — dijauhkan dari sinyal yang dipakai ART
  (yang ngumpul di kedua ujung), biar ga bentrok sama GC/suspension runtime.
- **`tgkill(pid, pid, sig)`** → sinyal cuma dikirim ke **main thread**
  (tid == pid), bukan thread acak — jadi fungsi battle logic dipanggil dari
  thread yang memang beneran ngurus battle.
- **Semua stub/trampoline murni syscall** — ga boleh panggil libc (malloc,
  printf) dari dalam proses game (bukan async-signal-safe). Verifikasi byte
  instruksi pakai assembler asli (aarch64-linux-gnu-as) + capstone.

---

## 5. ⚠️ Satu hal yang PERLU diverifikasi di device (jujur, bukan overclaim)

Bypass-nya **lengkap dan jalan**. Yang belum 100% pasti tanpa test device
adalah **RVA fungsi cast retri**:

```cpp
// main.cpp baris ~440
#define RETRI_CAST_RVA 0x26c998cULL   // ChooseHeroMgr.ReqUseSummonSkill (dump 22.1.97.12061)
```

- `ReqUseSummonSkill(int skillId, bool bSecondSkill, bool manuallySelect)` —
  kandidat terkuat dari dump (ini jalur "request cast summon skill").
- Trampoline memanggilnya dengan: `x0 = context (0 default)`, `w1 = skillId`
  (dibaca live dari player `+0x95c`), `x2 = 0`, `x3 = 1`.
- **Kalau pas test game crash / retri ga ke-cast** → tinggal ganti 1 angka di
  `RETRI_CAST_RVA` (dan/atau `cfg.context`) ke fungsi yang temen lo tau pasti
  jalur cast-nya — engine & bypass-nya ga perlu diubah sama sekali.
- Selama RVA belum diverifikasi, **Direct Call default OFF**; tool tetap pakai
  touch (cara herz-kimmy yang terbukti jalan). Nyalakan toggle setelah RVA
  ketemu — kalau install gagal (misal device tanpa region RWX), otomatis
  fallback ke touch, tidak ada risiko crash.

---

## 6. FAQ

**Q: Kenapa ga sekalian inline-hook fungsi cast-nya?**
Karena Aegis hash-check `.text` library game. Byte ke-ubah = ban. Trampoline
kita di region anon, bukan di library — hash aman.

**Q: Kenapa ga pake pthread_create di proses game?**
Thread baru = jejak di `/proc/<pid>/task` + stack asing yang bisa di-scan.
Signal handler di thread yang sudah ada = tanpa jejak.

**Q: Kalau device ga punya region RWX (JIT mati)?**
`RC_Install` balikin `RC_NO_RWX` → tool otomatis pakai touch. Aman.

**Q: Sinyal 40 bisa bentrok sama game?**
Sangat kecil kemungkinannya — 40 di tengah range RT signal (32–64), jauh dari
yang biasa dipakai ART/Unity. Self-test di Install nge-verifikasi handler
beneran jalan sebelum dipakai; kalau gagal, install dianggap gagal → fallback.

**Q: Apakah bypass ini anti-ban 100%?**
**Tidak. Dan siapa pun yang bilang 100% anti-ban itu boong.** Ini mengurangi
permukaan deteksi secara drastis (jejak nyaris nol, kode game tak tersentuh,
tidak ada ptrace saat bermain), tapi Aegis itu deteksi berlapis — tetap main
di akun yang lo sayang dengan risiko sendiri. 😄