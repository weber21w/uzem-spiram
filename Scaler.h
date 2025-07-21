#ifndef SCALER_H
#define SCALER_H

#include <SDL2/SDL.h>
#include "avr8.h"   // for u32

// ————————————————————————————————
// Scaler mode bits
// ————————————————————————————————

// Base scaling factors:
#define SCALER_NONE      1
#ifdef ENABLE_SCALE2X
  #define SCALER_SCALE2X   2
#endif
#ifdef ENABLE_SCALE3X
  #define SCALER_SCALE3X   3
#endif
#ifdef ENABLE_SCALE4X
  #define SCALER_SCALE4X   4
#endif

// CRT flag (OR this in to enable the CRT effect)
#ifdef ENABLE_CRT
  #define SCALER_CRT       0x10
#endif

// Mask for extracting just the base factor (1,2,3,4)
#define SCALER_BASE_MASK 0x0F

// ————————————————————————————————
// Scaler function type
// ————————————————————————————————
typedef void (*ScalerFunc)(u32 *src, int w, int h, u32 *dst);

// ————————————————————————————————
// Globals (defined once in Scaler.cpp)
// ————————————————————————————————
extern int           initial_scaler_mode;
extern int           SCREEN_WIDTH;         // width before scaling
extern int           SCREEN_HEIGHT;        // height before scaling
extern ScalerFunc    activeScaler;         // current scaler function
extern int           currentTextureScale;  // current factor (1,2,3,4)
extern SDL_Surface  *surface;              // source surface
extern SDL_Renderer *renderer;             // SDL renderer
extern SDL_Texture  *scaledTexture;        // streaming GL texture for output

// ————————————————————————————————
// API
// ————————————————————————————————
/// Apply the active scaler (and CRT effect) to surface→pixels and upload to scaledTexture
void ApplyScalerIfNeeded();

/// Switch to a new scaler mode (e.g. SCALER_SCALE2X|SCALER_CRT)
void SetScaler(int mode);

/// Cycle to the next scaler mode and return it
int NextScaler();

// ————————————————————————————————
// Scaler implementations
// ————————————————————————————————
#ifdef ENABLE_SCALE2X
void ApplyScale2x(u32 *src, int w, int h, u32 *dst);
#endif

#ifdef ENABLE_SCALE3X
void ApplyScale3x(u32 *src, int w, int h, u32 *dst);
#endif

#ifdef ENABLE_SCALE4X
void ApplyScale4x(u32 *src, int w, int h, u32 *dst);
#endif

#ifdef ENABLE_CRT
void ApplyCRTEffect(u32 *buf, int w, int h);
#endif

#endif // SCALER_H
