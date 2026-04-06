#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <SDL/SDL.h>
// #include "../SDL/include/SDL.h"

#include "chrono.h"

// ---------------------------------------------------------------------------

#define RGB(r, g, b) b | (g << 8) | (r << 16)

static int cpu_mhz = 0;
static int dump    = 0;

static void ChronoShow(char* name, int computations) {
  float ms           = ChronoWatchReset();
  float cycles       = ms * (1000000.0f / 1000.0f) * (float)cpu_mhz;
  float cyc_per_comp = cycles / (float)computations;
  if ((dump & 15) == 0)
    fprintf(stdout, "%s: %f ms, %d cycles, %f cycles/loop\n", name, ms, (int)cycles, cyc_per_comp);
}

// Limit framerate and return any remaining time to the OS

static void FramerateLimit(int max_fps) {
  static unsigned int frame_time = 0;

  unsigned int t       = GetMsTime();
  unsigned int elapsed = t - frame_time;
  // El tiempo sobrante se "regala" al sistema operativo con la funcion POSIX sleep
  const unsigned int limit = 1000 / max_fps;  // 1000 miliseconds / framerate minimo deseado
  if (elapsed < limit) usleep((limit - elapsed) * 1000);  // arg in microseconds
  frame_time = GetMsTime();
}

// ---------------------------------------------------------------------------

static void Load_Paletted_BMP(const char* file_path, unsigned char** indices,
                              unsigned int** palette, int* width, int* height) {
  assert(NULL != file_path && "[Load_Paletted_BMP] Invalid file path");

  SDL_Surface* surface = SDL_LoadBMP(file_path);
  assert(NULL != surface && "[Load_Paletted_BMP] Could not load png");

  SDL_Palette* surf_palette = surface->format->palette;
  assert(NULL != surf_palette && surface->format->BitsPerPixel == 8 &&
         "[Load_Paletted_BMP] Incorrect bmp format");

  if (NULL != width) *width = surface->w;
  if (NULL != height) *height = surface->h;
  if (NULL != indices) {
    int bytes = surface->pitch * surface->h;
    *indices  = (unsigned char*)malloc(bytes);
    memcpy(*indices, surface->pixels, surface->w * surface->h);
  }

  if (palette) {
    *palette = (unsigned int*)malloc(surf_palette->ncolors * sizeof(unsigned int));
    for (int i = 0; i < surf_palette->ncolors; i++) {
      SDL_Color color = surf_palette->colors[i];
      (*palette)[i]   = RGB(color.r, color.g, color.b);
    }
  }

  SDL_FreeSurface(surface);
}

static void Load_BMP(const char* file_path, unsigned char** pixels, int* width, int* height) {
  assert(NULL != file_path && "[Load_BMP] Invalid file path");

  SDL_Surface* surface = SDL_LoadBMP(file_path);
  assert(NULL != surface && "[Load_BMP] Could not load png");

  if (NULL != width) *width = surface->w;
  if (NULL != height) *height = surface->h;
  if (NULL != pixels) {
    int bytes = surface->pitch * surface->h;
    *pixels   = (unsigned char*)malloc(bytes);
    memcpy(*pixels, surface->pixels, surface->w * surface->h);
  }

  SDL_FreeSurface(surface);
}

// Color
static int color_map_w = 0, color_map_h = 0;
static unsigned char* color_map;
static unsigned int* color_palette;

// Height map
static int height_map_w = 0, height_map_h = 0;
static unsigned char* height_map;

static void InitializeEffect() {
  Load_Paletted_BMP("../maps/C1W.bmp", &color_map, &color_palette, &color_map_w, &color_map_h);
  Load_BMP("../maps/D1.bmp", &height_map, &height_map_w, &height_map_h);
}

static int DoEffect(unsigned int* pixels, int w, int h, int stride, int frame, float projection) {
  int painted_pixels = 0;

  int cx = w >> 1;
  int cy = h >> 1;

  int depth = 20;

  for (int xp = 0; xp < w, xp++) {
    int du = lerp(-1, 1, xp / w);
    int u  = 512;
    int v  = 512;

    int max_y = 0;
    int dv = 1; // TODO: check what is the right dv

    // z relative to camera position
    for (int z = 0; z < depth; z++) {
      u += du;
      v += dv;

      int h  = height_map[u + v * height_map_w];
      int y  = h - ycam;
      int yp = cy - y * (projection / z);

      if (yp >= 0 && yp < h && yp > max_y) {
        for (int line_y = max_y; line_y < yp; line_y++) {
          pixels[xp + line_y * stride] = color_palette[(int)color_map[i + line_y * color_map_w]];
        }
        painted_pixels++;
      }
    }
  }

  return painted_pixels;
}

// ---------------------------------------------------------------------------

static void DisplayVertices(unsigned int* pixels, short* drawlist, int n_vertices, int stride) {
  int i;
  for (i = 0; i < n_vertices; i++) {
    int xp                   = drawlist[0];
    int yp                   = drawlist[1];
    pixels[xp + yp * stride] = drawlist[2];
    drawlist += 3;
  }
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  int end     = 0;
  int mouse_x = 0, mouse_y = 0;
  SDL_Surface* g_SDLSrf;
  int req_w = 1024;
  int req_h = 1024;

  if (argc < 2) {
    fprintf(stderr, "I need the cpu speed in Mhz!\n");
    exit(0);
  }
  cpu_mhz = atoi(argv[1]);
  assert(cpu_mhz > 0);
  fprintf(stdout, "Cycle times for a %d Mhz cpu\n", cpu_mhz);

  // Init SDL and screen
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "Can't Initialise SDL: %s\n", SDL_GetError());
    exit(1);
  }
  if (0 == SDL_SetVideoMode(req_w, req_h, 32, SDL_HWSURFACE | SDL_DOUBLEBUF)) {
    printf("Couldn't set %dx%dx32 video mode: %s\n", req_w, req_h, SDL_GetError());
    return 0;
  }

  InitializeEffect();

  // Horizontal field of view
  float hfov       = 90.0f * ((3.1416f * 2.0f) / 360.0f);  // Degrees to radians
  float half_scr_w = (float)(req_w >> 1);
  float projection = (1.0f / tan(hfov * 0.5f)) * half_scr_w;

  // Main loop
  g_SDLSrf = SDL_GetVideoSurface();
  while (!end) {
    SDL_Event event;

    // Your gfx effect goes here

    ChronoWatchReset();
    // Draw vertices; don't modify this section
    // Lock screen to get access to the memory array
    SDL_LockSurface(g_SDLSrf);

    // Clean the screen
    SDL_FillRect(g_SDLSrf, NULL, SDL_MapRGB(g_SDLSrf->format, 0, 0, 0));
    // ChronoShow("Clean", g_SDLSrf->w * g_SDLSrf->h);

    ChronoWatchReset();
    int n_draw = DoEffect(g_SDLSrf->pixels, g_SDLSrf->w, g_SDLSrf->h, g_SDLSrf->pitch >> 2, dump,
                          projection);
    ChronoShow("Voxel Terrain", n_draw);

    // Paint vertices
    // DisplayVertices(g_SDLSrf->pixels, drawlist, n_draw, g_SDLSrf->pitch >> 2);
    // ChronoShow("Preview", n_draw);

    // Unlock the draw surface, dump to physical screen
    ChronoWatchReset();
    SDL_UnlockSurface(g_SDLSrf);
    SDL_Flip(g_SDLSrf);
    // ChronoShow("Screen dump", g_SDLSrf->w * g_SDLSrf->h);

    // Limit framerate and return any remaining time to the OS
    // Comment this line for benchmarking
    FramerateLimit(60);

    dump++;

    // Recoger eventos de la ventana
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_MOUSEMOTION:
          mouse_x = event.motion.x;
          mouse_y = event.motion.y;
          break;
        case SDL_MOUSEBUTTONDOWN:
          // printf("Mouse button %d pressed at (%d,%d)\n",
          //        event.button.button, event.button.x, event.button.y);
          break;
        case SDL_QUIT:
          end = 1;
          break;
      }
    }
  }

  return 1;
}
