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

static void LoadPalettedBMP(const char* file_path, unsigned char** indices, unsigned int** palette,
                            int* width, int* height) {
  assert(NULL != file_path && "[LoadPalettedBMP] Invalid file path");

  SDL_Surface* surface = SDL_LoadBMP(file_path);
  assert(NULL != surface && "[LoadPalettedBMP] Could not load png");

  SDL_Palette* bmp_palette = surface->format->palette;
  assert(NULL != bmp_palette && surface->format->BitsPerPixel == 8 &&
         "[LoadPalettedBMP] Incorrect bmp format");

  if (NULL != width) *width = surface->w;
  if (NULL != height) *height = surface->h;
  if (NULL != indices) {
    int bytes = surface->pitch * surface->h;
    *indices  = (unsigned char*)malloc(bytes);
    memcpy(*indices, surface->pixels, surface->w * surface->h);
  }

  if (palette) {
    *palette = (unsigned int*)malloc(bmp_palette->ncolors * sizeof(unsigned int));
    for (int i = 0; i < bmp_palette->ncolors; i++) {
      SDL_Color color = bmp_palette->colors[i];
      (*palette)[i]   = RGB(color.r, color.g, color.b);
    }
  }

  SDL_FreeSurface(surface);
}

static void LoadBMP(const char* file_path, unsigned char** pixels, int* width, int* height) {
  assert(NULL != file_path && "[LoadBMP] Invalid file path");

  SDL_Surface* surface = SDL_LoadBMP(file_path);
  assert(NULL != surface && "[LoadBMP] Could not load png");

  if (NULL != width) *width = surface->w;
  if (NULL != height) *height = surface->h;
  if (NULL != pixels) {
    int bytes = surface->pitch * surface->h;
    *pixels   = (unsigned char*)malloc(bytes);
    memcpy(*pixels, surface->pixels, surface->w * surface->h);
  }

  SDL_FreeSurface(surface);
}

static uint16_t* PackTexture(unsigned char* color_indices, unsigned char* height_map, int w,
                             int h) {
  uint16_t* packed_map = (uint16_t*)malloc(w * h * sizeof(uint16_t));
  assert(packed_map);

  for (int i = 0; i < w * h; i++) {
    packed_map[i] = (height_map[i] << 8) | color_indices[i];
  }

  return packed_map;
}

static void PrintBits(size_t const size, void const* const ptr) {
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

#define MIP_MAP_LEVELS 3

static uint16_t* LoadBMPMipmapLevel(int level, const char* color_path, const char* height_path,
                                    unsigned int** palette, int* width, int* height) {
  assert(level < MIP_MAP_LEVELS);
  assert(color_path);
  assert(height_path);

  static char color_full_path[512];
  static char height_full_path[512];

  sprintf(color_full_path, "%s_L%d.bmp", color_path, level);
  sprintf(height_full_path, "%s_L%d.bmp", height_path, level);
  fprintf(stdout, "Loading %s\n", color_full_path);
  fprintf(stdout, "Loading %s\n", height_full_path);

  unsigned char *color_indices, *height_map;
  int color_w, color_h;
  int height_w, height_h;

  LoadPalettedBMP(color_full_path, &color_indices, palette, &color_w, &color_h);
  LoadBMP(height_full_path, &height_map, &height_w, &height_h);

  assert(color_w == height_w && color_h == height_h &&
         "Color and height map MUST have the same size");

  if (NULL != width) *width = color_w;
  if (NULL != height) *height = color_h;

  uint16_t* packed = PackTexture(color_indices, height_map, color_w, color_h);
  free(color_indices);
  free(height_map);
  return packed;
}

// Texture Size
static int base_map_width;
static int base_map_height;

static unsigned int* palette;
static unsigned char* screen_color_indices;

// Camera
static float camera_x     = 0;
static float camera_y     = 100;
static float camera_z     = 0;
static float camera_speed = 1.0f;

// Mipmap Array
static uint16_t* mip_maps[MIP_MAP_LEVELS];

static void InitializeEffect(int screen_w, int screen_h) {
  int w, h;

  mip_maps[0] = LoadBMPMipmapLevel(0, "../maps/C1W", "../maps/D1", &palette, &base_map_width,
                                   &base_map_height);
  mip_maps[1] = LoadBMPMipmapLevel(1, "../maps/C1W", "../maps/D1", &palette, &w, &h);
  assert(base_map_width >> 1 == w && base_map_height >> 1 == h);
  mip_maps[2] = LoadBMPMipmapLevel(2, "../maps/C1W", "../maps/D1", &palette, &w, &h);
  assert(base_map_width >> 2 == w && base_map_height >> 2 == h);

  screen_color_indices = (unsigned char*)malloc(screen_w * screen_h);
  assert(screen_color_indices);
}

static float Lerp(float a, float b, float t) { return a + t * (b - a); }

int DoEffect(unsigned char* buffer, int w, int h, int stride, int frame, float projection) {
  // Screen center
  int screen_center_y = h >> 1;
  int depth           = 4096;
  int loops           = 0;

  // Traverse screen horizontally
  for (int xp = 0; xp < w; xp++) {
    float u = camera_x;
    float v = camera_z;

    // Assumes FOV of 90 degrees
    float du = Lerp(-1, 1, (float)xp / (float)w);
    float dv = 1.0f;

    // 0 Up - h Down
    int highest_drawn_col               = 0;
    int step_z                          = 1;
    int mip_level                       = 0;
    uint16_t* active_mip                = mip_maps[mip_level];
    int active_mip_width                = base_map_width;
    int active_mip_height               = base_map_height;
    int mip_level_switch_distance       = 512;
    unsigned char mip_level_debug_color = 0;

    // z relative to camera position
    for (int z = 1; z < depth; z += step_z) {
      loops++;
      u += du;
      v -= dv;
      int iu = (int)u & (active_mip_width - 1);
      int iv = (int)v & (active_mip_height - 1);

      unsigned int texel = active_mip[iu + iv * active_mip_width];
      int terrain_height = texel >> 8;

      int height_delta = camera_y - terrain_height;
      // TODO: precalculate projection / z
      int yp = screen_center_y - height_delta * (projection / z);

      if (yp >= 0 && yp < h && yp > highest_drawn_col) {
        unsigned char* row         = &buffer[h - 1 - yp + xp * stride];
        unsigned int palette_index = texel & 0xFF;

        // Draw into the transposed color buffer
        for (int line_y = h - yp; line_y < h - highest_drawn_col; line_y++) {
          *(row++) = palette_index;
          // *(row++) = mip_level_debug_color;
        }

        highest_drawn_col = yp;
      }

      if ((z & (mip_level_switch_distance - 1)) == 0 && mip_level < MIP_MAP_LEVELS - 1) {
        mip_level_debug_color++;
        ++mip_level;
        mip_level_switch_distance <<= 2;
        active_mip = mip_maps[mip_level];
        u *= 0.5f;
        v *= 0.5f;
        active_mip_width >>= 1;
        active_mip_height >>= 1;
        step_z *= 2;
      }
    }
  }

  return loops;
}

static int tile_size = 256;

static void DrawToScreen(unsigned int* pixels, int w, int h, unsigned char* buffer) {
  /**
     PIXELS TRAVERSAL
     TOP
      y=0  ┌─────────────────────────────┐
           │ 1  2 | 13 14 | 25 26 | 37 38│
           │ 3  4 | 15 16 | 27 28 | 39 40│
           │ 5  6 | 17 18 | 29 30 | 41 42│
           │ 7  8 | 19 20 | 31 32 | 43 44│
           │ 9 10 | 21 22 | 33 34 | 45 46│
           │11 12 | 23 24 | 35 36 | 47 48│
   BOTTOM  └─────────────────────────────┘
           ^tile0  ^tile1  ^tile2  ^tile3

     BUFFER TRAVERSAL
     TOP
      y=0  ┌───────────────────────┐
           │ 1  3  |  5  7 |  9 11 │
           │ 2  4  |  6  8 | 10 12 │
           │ 13 15 | 17 19 | 21 23 │
           │ 14 16 | 18 20 | 22 24 │
           │ 25 27 | 29 31 | 33 35 │
           │ 26 28 | 30 32 | 34 36 │
           │ 37 39 | 41 43 | 45 47 │
           │ 38 40 | 42 44 | 46 48 │
   BOTTOM  └───────────────────────┘
           ^tile0  ^tile1  ^tile2  ^tile3
  */

  // Interleaving. It only works with powers of 2 (like textures)
  // vvvvvvvv|uuuuuuuuuu|vv
  //    8b       10b     2b

  // No Tiling
  // for (int py = 0; py < h; py++) {
  //   for (int px = 0; px < w; px++) {
  //     pixels[px + py * w] = palette[buffer[py + px * h]];
  //   }
  // }

  // Tiling
  for (int offset_x = 0; offset_x < w; offset_x += tile_size) {
    for (int py = 0; py < h; py++) {
      for (int px = offset_x; px < offset_x + tile_size; px++) {
        pixels[px + py * w] = palette[buffer[py + px * h]];
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
  //         pixels[px + py * w] = palette[buffer[(h - py - 1) + px * h]];
  //       }
  //     }
  //   }
  // }
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  int quit    = 0;
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

  while (!quit) {
    SDL_Event event;

    unsigned char* keyboard_state = SDL_GetKeyState(NULL);
    if (keyboard_state[SDLK_w]) camera_z -= camera_speed;
    if (keyboard_state[SDLK_s]) camera_z += camera_speed;
    if (keyboard_state[SDLK_a]) camera_x -= camera_speed;
    if (keyboard_state[SDLK_d]) camera_x += camera_speed;
    if (keyboard_state[SDLK_q]) camera_y += camera_speed;
    if (keyboard_state[SDLK_e]) camera_y -= camera_speed;

    ChronoWatchReset();
    // Draw vertices; don't modify this section
    // Lock screen to get access to the memory array
    SDL_LockSurface(g_SDLSrf);

    // Clean the screen
    SDL_FillRect(g_SDLSrf, NULL, SDL_MapRGB(g_SDLSrf->format, 0, 0, 0));
    // ChronoShow("Clean", g_SDLSrf->w * g_SDLSrf->h);

    // Your gfx effect goes here
    // 1 = Blue
    memset(screen_color_indices, 1, g_SDLSrf->w * g_SDLSrf->h);

    ChronoWatchReset();
    int loops =
        DoEffect(screen_color_indices, g_SDLSrf->w, g_SDLSrf->h, g_SDLSrf->h, dump, projection);
    ChronoShow("Voxel Terrain", loops);
    DrawToScreen(g_SDLSrf->pixels, g_SDLSrf->w, g_SDLSrf->h, screen_color_indices);
    ChronoShow("Draw To Screen", g_SDLSrf->w * g_SDLSrf->h);

    // Unlock the draw surface, dump to physical screen
    ChronoWatchReset();
    SDL_UnlockSurface(g_SDLSrf);
    SDL_Flip(g_SDLSrf);
    // ChronoShow("Screen dump", g_SDLSrf->w * g_SDLSrf->h);

    // Limit framerate and return any remaining time to the OS
    // Comment this line for benchmarking
    // FramerateLimit(60);

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
          quit = 1;
          break;
      }
    }
  }

  return 1;
}
