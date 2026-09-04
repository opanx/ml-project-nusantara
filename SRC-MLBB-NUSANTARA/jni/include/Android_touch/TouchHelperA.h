#pragma once


bool Touch_Init(int w, int h, uint32_t orientation_, bool readOnly);
void UpdateScreenData(int w, int h, uint32_t orientation_);

void Touch_Close();
void Touch_Down(float x, float y);
void Touch_Move(float x, float y);
void Touch_Up();

// Tap pada koordinat NATIVE perangkat (sama dgn koordinat yg dikirim driver asli),
// dipakai auto-retri supaya tap jatuh PERSIS di titik yg dikalibrasi (bukan transform manual).
void Touch_TapNative(int x, int y, int holdMs);

// true kalau masih ada jari asli nempel di layar / tap sintetik lagi jalan
// (dipakai auto-retri: nunggu tangan bersih dulu baru nge-tap, biar ga tabrakan)
bool Touch_Busy();

// Kalibrasi 1-tap: set flag -> sentuhan asli berikutnya ditangkap,
// native & logical coord disimpan, flag di-clear otomatis.
extern bool g_retriCapture;
extern int  g_retriNativeX;
extern int  g_retriNativeY;
extern float g_retriLogicalX;
extern float g_retriLogicalY;

// Diagnostics touch (dibaca dari UI utk debug - kenapa tap ga nyampe)
extern bool g_touchDebugLog;      // print detail tap ke console
// 0 = gagal, 1 = ok (isi dari Touch_Init)
extern int  g_touchInitOk;
extern int  g_touchFdCount;        // jumlah device sentuh yg ke-grab
// statistik inject: berapa batch/byte yg berhasil ditulis ke uinput
extern long long g_touchMirrorWrites;
extern long long g_touchMirrorBytes;
extern long long g_touchTapWrites;
extern long long g_touchTapBytes;
extern int  g_touchLastErr;        // errno terakhir pas write gagal (0 = aman)
extern char g_touchDevName[64];    // nama device sentuh asli yg di-grab

