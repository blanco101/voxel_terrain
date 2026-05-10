#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <SDL/SDL.h>

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
static unsigned char* color_buffer;

// Height map
static int height_map_w = 0, height_map_h = 0;
static unsigned char* height_map;

// Packed map
static int packed_map_w = 0, packed_map_h = 0;
static uint16_t* packed_map;

// Camera
static float cam_x        = 0;
static float cam_y        = 100;
static float cam_z        = 0;
static float camera_speed = 1.0f;

static void printBits(size_t const size, void const* const ptr) {
  unsigned char* b = (unsigned char*)ptr;
  unsigned char byte;
  int i, j;

  for (i = size - 1; i >= 0; i--) {
    for (j = 7; j >= 0; j--) {
      byte = (b[i] >> j) & 1;
      printf("%u", byte);
    }
  }
}

static void InitializeEffect(int w, int h) {
  Load_Paletted_BMP("../maps/C1W.bmp", &color_map, &color_palette, &color_map_w, &color_map_h);
  Load_BMP("../maps/D1.bmp", &height_map, &height_map_w, &height_map_h);
  assert(color_map_w == height_map_w && color_map_h == height_map_h &&
         "Color and height map MUST have the same size");

  color_buffer = (unsigned char*)malloc(w * h);
  assert(color_buffer);

  packed_map_w = color_map_w;
  packed_map_h = color_map_h;
  packed_map   = (uint16_t*)malloc(packed_map_w * packed_map_h * sizeof(uint16_t));
  assert(packed_map);

  for (int i = 0; i < packed_map_w * packed_map_h; i++) {
    packed_map[i] = (height_map[i] << 8) | color_map[i];
  }
}

static float Lerp(float a, float b, float t) { return a + t * (b - a); }

int DoEffect(unsigned char* buffer, int w, int h, int stride, int frame, float projection) {
  int loop_count = 0;  // Do we need this or use width * height?

  // Screen center
  int cy    = h >> 1;
  int depth = 1024;

  // Traverse screen horizontally
  for (int xp = 0; xp < w; xp++) {
    // Assumes FOV of 90 degrees
    float du = Lerp(-1, 1, (float)xp / (float)w);
    float u  = cam_x;
    float v  = cam_z;

    // 0 Up - h Down
    int max_y = 0;
    float dv  = 1.0f;

    // z relative to camera position
    for (int z = 1; z < depth; z++) {
      u += du;
      v -= dv;
      int iu = (int)u & (packed_map_w - 1);
      int iv = (int)v & (packed_map_h - 1);

      uint16_t packed_value = packed_map[iu + iv * packed_map_w];
      int height            = packed_value >> 8;

      int y  = cam_y - height;
      int yp = cy - y * (projection / z);

      if (yp >= 0 && yp < h && yp > max_y) {
        // Traverse screen vertically
        unsigned char* row        = &buffer[h - 1 - yp + xp * stride];
        unsigned char color_index = packed_value & 0xFF;

        for (int line_y = h - yp; line_y < h - max_y; line_y++) {
          *row = color_index;
          row++;
        }
        max_y = yp;
      }

      loop_count++;
    }
  }

  return loop_count;
}

static int tile_size   = 16;
static int tile_size_x = 8;
static int tile_size_y = 8;

static void DrawToScreen(unsigned int* pixels, int w, int h, unsigned char* buffer) {
  /**
      PIXELS TRAVERSAL
      Bottom-up within each vertical strip:
      TOP
       y=0  ┌─────────────────────────────┐
            │11 12 | 23 24 | 35 36 | 47 48│
            │ 9 10 | 21 22 | 33 34 | 45 46│
            │ 7  8 | 19 20 | 31 32 | 43 44│
            │ 5  6 | 17 18 | 29 30 | 41 42│
            │ 3  4 | 15 16 | 27 28 | 39 40│
            │ 1  2 | 13 14 | 25 26 | 37 38│
    BOTTOM  └─────────────────────────────┘
            ^tile0  ^tile1  ^tile2  ^tile3

      BUFFER TRAVERSAL
      TOP
         y=0  ┌──────────────────────────────┐
              │ 1  3 |  5  7 |  9 11 | 13 15 │
              │ 2  4 |  6  8 | 10 12 | 14 16 │
              │17 19 | 21 23 | 25 27 | 29 31 │
              │18 20 | 22 24 | 26 28 | 30 32 │
              │33 35 | 37 39 | 41 43 | 45 47 │
              │34 36 | 38 40 | 42 44 | 46 48 │
      BOTTOM  └──────────────────────────────┘
            ^tile0  ^tile1  ^tile2  ^tile3
   */

  // Interleaving. It only works with powers of 2 (like textures)
  // vvvvvvvv|uuuuuuuuuu|vv
  //    8b       10b     2b

  // No Tiling
  // for (int py = 0; py < h; py++) {
  //   for (int px = 0; px < w; px++) {
  //     pixels[px + py * w] = color_palette[buffer[py + px * h]];
  //   }
  // }

  // Tiling
  for (int offset_x = 0; offset_x < w; offset_x += tile_size) {
    for (int py = 0; py < h; py++) {
      for (int px = offset_x; px < offset_x + tile_size; px++) {
        pixels[px + py * w] = color_palette[buffer[py + px * h]];
      }
    }
  }

  // int current_offset_x = 0;

  // for (int offset_y = h - 1; offset_y > 0; offset_y -= tile_size_y) {
  //   current_offset_x++;
  //   int offset_x = current_offset_x * tile_size_x;
  //   for (; offset_x < offset_x + tile_size_x; offset_x += tile_size_x) {
  //     for (int py = offset_y; py < offset_y + tile_size_y; py--) {
  //       for (int px = offset_x; px < offset_x + tile_size_x; px++) {
  //         pixels[px + py * w] = color_palette[buffer[(h - py - 1) + px * h]];
  //       }
  //     }
  //   }
  // }
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  int end     = 0;
  int mouse_x = 0, mouse_y = 0;
  SDL_Surface* g_SDLSrf;
  int req_w = 1024;
  int req_h = 768;

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

  // Horizontal field of view
  float hfov       = 90.0f * ((3.1416f * 2.0f) / 360.0f);  // Degrees to radians
  float half_scr_w = (float)(req_w >> 1);
  float projection = (1.0f / tan(hfov * 0.5f)) * half_scr_w;

  // Main loop
  g_SDLSrf = SDL_GetVideoSurface();
  InitializeEffect(g_SDLSrf->w, g_SDLSrf->h);

  while (!end) {
    SDL_Event event;

    unsigned char* keyboard_state = SDL_GetKeyState(NULL);
    if (keyboard_state[SDLK_w]) cam_z -= camera_speed;
    if (keyboard_state[SDLK_s]) cam_z += camera_speed;
    if (keyboard_state[SDLK_a]) cam_x -= camera_speed;
    if (keyboard_state[SDLK_d]) cam_x += camera_speed;
    if (keyboard_state[SDLK_q]) cam_y += camera_speed;
    if (keyboard_state[SDLK_e]) cam_y -= camera_speed;

    ChronoWatchReset();
    // Draw vertices; don't modify this section
    // Lock screen to get access to the memory array
    SDL_LockSurface(g_SDLSrf);

    // Clean the screen
    SDL_FillRect(g_SDLSrf, NULL, SDL_MapRGB(g_SDLSrf->format, 0, 0, 0));
    // ChronoShow("Clean", g_SDLSrf->w * g_SDLSrf->h);

    // Your gfx effect goes here
    // 1 = Blue
    memset(color_buffer, 1, g_SDLSrf->w * g_SDLSrf->h);

    ChronoWatchReset();
    int n_draw = DoEffect(color_buffer, g_SDLSrf->w, g_SDLSrf->h, g_SDLSrf->h, dump, projection);
    DrawToScreen(g_SDLSrf->pixels, g_SDLSrf->w, g_SDLSrf->h, color_buffer);
    ChronoShow("Voxel Terrain", n_draw);

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
          if (SDL_BUTTON_WHEELDOWN == event.button.button) {
            camera_speed -= 1.0f;
          } else if (SDL_BUTTON_WHEELUP == event.button.button) {
            camera_speed += 1.0f;
          }
          if (camera_speed < 0.0f) camera_speed = 0.0f;
          if (camera_speed > 10.0f) camera_speed = 10.0f;
          break;
        case SDL_QUIT:
          end = 1;
          break;
      }
    }
  }

  return 1;
}
