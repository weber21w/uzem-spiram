// Scaler.cpp
#include "Scaler.h"
#include <cstdlib>
#include <cstdio>
#include <SDL2/SDL.h>
#include "avr8.h"   // for u8, u32, SCALER_NONE, SCALER_BASE_MASK, etc.


int SCREEN_WIDTH  = 0;
int SCREEN_HEIGHT = 0;

// -- Define the one copy of each global declared extern in Scaler.h --
int  initial_scaler_mode   = SCALER_NONE;
ScalerFunc activeScaler    = nullptr;
int  scaleFactor           = 1;
#ifdef ENABLE_CRT
int  crtEffectEnabled      = 0;
#endif
u32 *scale_buffer          = nullptr;
size_t scale_buffer_size   = 0;
int  lastMode              = 0;
#ifdef ENABLE_SCALE4X
u32 *scale4x_tmp           = nullptr;
size_t scale4x_tmp_size    = 0;
#endif

void ApplyScalerIfNeeded() {
    if (!activeScaler || !scale_buffer) return;
    int w = surface->w;
    int h = surface->h;
    activeScaler((u32*)surface->pixels, w, h, scale_buffer);
#ifdef ENABLE_CRT
    if (crtEffectEnabled)
        ApplyCRTEffect(scale_buffer, w * scaleFactor, h * scaleFactor);
#endif
    SDL_UpdateTexture(scaledTexture, nullptr, scale_buffer, w * scaleFactor * sizeof(u32));
}

void SetScaler(int mode) {
    if (!surface || surface->w == 0 || surface->h == 0) return;
	
    if (mode == lastMode) return;
    lastMode = mode;
#ifdef ENABLE_CRT
    crtEffectEnabled = (mode & SCALER_CRT) ? 1 : 0;
#endif
    int w = surface->w, h = surface->h;
    if (scale_buffer) { free(scale_buffer); scale_buffer = nullptr; scale_buffer_size = 0; }
#ifdef ENABLE_SCALE4X
    if (scale4x_tmp) { free(scale4x_tmp); scale4x_tmp = nullptr; scale4x_tmp_size = 0; }
#endif
    int prevScale = currentTextureScale;
    switch (mode & SCALER_BASE_MASK) {
        case SCALER_NONE:
            activeScaler = nullptr;
            scaleFactor  = 1;
            break;
#ifdef ENABLE_SCALE2X
        case SCALER_SCALE2X:
            activeScaler        = ApplyScale2x;
            scaleFactor         = 2;
            scale_buffer_size   = (size_t)w * 2 * h * 2;
            scale_buffer        = (u32*)malloc(scale_buffer_size * sizeof(u32));
            break;
#endif
#ifdef ENABLE_SCALE3X
        case SCALER_SCALE3X:
            activeScaler        = ApplyScale3x;
            scaleFactor         = 3;
            scale_buffer_size   = (size_t)w * 3 * h * 3;
            scale_buffer        = (u32*)malloc(scale_buffer_size * sizeof(u32));
            break;
#endif
#ifdef ENABLE_SCALE4X
        case SCALER_SCALE4X:
            activeScaler        = ApplyScale4x;
            scaleFactor         = 4;
            scale4x_tmp_size    = (size_t)w * 2 * h * 2;
            scale4x_tmp         = (u32*)malloc(scale4x_tmp_size * sizeof(u32));
            scale_buffer_size   = (size_t)w * 4 * h * 4;
            scale_buffer        = (u32*)malloc(scale_buffer_size * sizeof(u32));
            break;
#endif
        default:
            activeScaler = nullptr;
            scaleFactor  = 1;
            break;
    }
    if (scaleFactor != prevScale) {
        if (scaledTexture) SDL_DestroyTexture(scaledTexture);
        scaledTexture = SDL_CreateTexture(renderer,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          w * scaleFactor,
                                          h * scaleFactor);
        currentTextureScale = scaleFactor;
    }
}

int NextScaler() {
    static const int modes[] = {
        SCALER_NONE,
#ifdef ENABLE_SCALE2X
        SCALER_SCALE2X,
#endif
#ifdef ENABLE_SCALE3X
        SCALER_SCALE3X,
#endif
#ifdef ENABLE_SCALE4X
        SCALER_SCALE4X,
#endif
    };
    int count  = sizeof(modes)/sizeof(modes[0]);
    int crtBit = lastMode & SCALER_CRT;
    for (int i = 0; i < count; ++i) {
        if (modes[i] == (lastMode & SCALER_BASE_MASK)) {
            int nxt = modes[(i+1)%count] | crtBit;
            SetScaler(nxt);
            return nxt;
        }
    }
    int fallback = modes[0] | crtBit;
    SetScaler(fallback);
    return fallback;
}

#ifdef ENABLE_SCALE2X
void ApplyScale2x(u32 *src, int w, int h, u32 *dst) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            u32 A = src[(y>0?y-1:y)*w + x];
            u32 B = src[y*w + (x>0?x-1:x)];
            u32 C = src[y*w + x];
            u32 D = src[y*w + (x<w-1?x+1:x)];
            u32 E = src[(y<h-1?y+1:y)*w + x];
            int dx = x*2, dy = y*2;
            u32 *row0 = dst + (dy  )*(w*2);
            u32 *row1 = dst + (dy+1)*(w*2);
            row0[dx  ] = (B!=D && A!=E) ? B : C;
            row0[dx+1] = (B!=D && A!=E) ? D : C;
            row1[dx  ] = (B!=D && A!=E) ? A : C;
            row1[dx+1] = (B!=D && A!=E) ? E : C;
        }
    }
}
#endif

#ifdef ENABLE_SCALE3X
void ApplyScale3x(u32 *src, int w, int h, u32 *dst) {
    for (int y = 1; y < h-1; ++y) {
        for (int x = 1; x < w-1; ++x) {
            u32 A = src[(y-1)*w + (x-1)];
            u32 B = src[(y-1)*w + x];
            u32 C = src[(y-1)*w + (x+1)];
            u32 D = src[y*w + (x-1)];
            u32 E = src[y*w + x];
            u32 F = src[y*w + (x+1)];
            u32 G = src[(y+1)*w + (x-1)];
            u32 H = src[(y+1)*w + x];
            u32 I = src[(y+1)*w + (x+1)];
            int dx = x*3, dy = y*3;
            u32 *r0 = dst + (dy  )*(w*3);
            u32 *r1 = dst + (dy+1)*(w*3);
            u32 *r2 = dst + (dy+2)*(w*3);
            r0[dx  ] = (D==B && D!=H && B!=F) ? D : E;
            r0[dx+1] = (B!=F && D!=F && B!=D) ? B : E;
            r0[dx+2] = (B==F && B!=D && F!=H) ? F : E;
            r1[dx  ] = (D!=B && D!=H && B!=H) ? D : E;
            r1[dx+1] = E;
            r1[dx+2] = (F!=B && F!=H && B!=H) ? F : E;
            r2[dx  ] = (D==H && D!=B && H!=F) ? D : E;
            r2[dx+1] = (H!=F && D!=F && H!=D) ? H : E;
            r2[dx+2] = (H==F && H!=D && F!=B) ? F : E;
        }
    }
}
#endif

#ifdef ENABLE_SCALE4X
void ApplyScale4x(u32 *src, int w, int h, u32 *dst) {
    if (!scale4x_tmp) return;
    ApplyScale2x(src,           w,   h,   scale4x_tmp);
    ApplyScale2x(scale4x_tmp,  w*2, h*2, dst);
}
#endif

#ifdef ENABLE_CRT
void ApplyCRTEffect(u32 *buf, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            u32 *px = &buf[y*w + x];
            u32 p = *px;
            u8  r = (p>>16)&0xFF;
            u8  g = (p>>8 )&0xFF;
            u8  b = (p    )&0xFF;
            if (y & 1) {
                r = (r * 220) / 255;
                g = (g * 220) / 255;
                b = (b * 220) / 255;
            }
            u8 gray = (r + g + b) / 3;
            r = (r + gray) / 2;
            g = (g + gray) / 2;
            b = (b + gray) / 2;
            *px = (0xFFu<<24) | (r<<16) | (g<<8) | b;
        }
    }
}
#endif
