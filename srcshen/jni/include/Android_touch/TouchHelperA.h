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

// Kalibrasi 1-tap: set flag -> sentuhan asli berikutnya ditangkap,
// native & logical coord disimpan, flag di-clear otomatis.
extern bool g_retriCapture;
extern int  g_retriNativeX;
extern int  g_retriNativeY;
extern float g_retriLogicalX;
extern float g_retriLogicalY;

