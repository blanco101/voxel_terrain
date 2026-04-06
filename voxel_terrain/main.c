

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <SDL/SDL.h>

#include "chrono.h"

// ---------------------------------------------------------------------------

static int cpu_mhz = 0;
static int dump = 0;

static void ChronoShow ( char* name, int computations)
{
  float ms = ChronoWatchReset();
  float cycles = ms * (1000000.0f/1000.0f) * (float)cpu_mhz;
  float cyc_per_comp = cycles / (float)computations;
  if ((dump & 15) == 0)
    fprintf ( stdout, "%s: %f ms, %d cycles, %f cycles/loop\n", name, ms, (int)cycles, cyc_per_comp);
}


// // Limit framerate and return any remaining time to the OS 

static void FramerateLimit (int max_fps)
{   
  static unsigned int frame_time = 0;

  unsigned int t = GetMsTime();
  unsigned int elapsed = t - frame_time;
  // El tiempo sobrante se "regala" al sistema operativo con la funcion POSIX sleep
  const unsigned int limit = 1000 / max_fps;  // 1000 miliseconds / framerate minimo deseado 
  if ( elapsed < limit)
    usleep (( limit - elapsed) * 1000); // arg in microseconds
  frame_time = GetMsTime();
}

// ---------------------------------------------------------------------------

// Fully UNoptimized graphics effect example
static int DoEffect (short* drawlist, int max_vertices, int w, int h, int frame, float projection)
{
  float anim1 = 0.003f * (float)frame;
  float anim2 = 0.0015f * (float)frame;
  short* ori = drawlist;
  int i, j;
  int cx = w >> 1;  // Screen center
  int cy = h >> 1; 
  int n_sec = 200;
  int n_rad = 45;
  for(j = 0; j < n_sec; j++) {
    for(i = 0; i < n_rad; i++) {
      const float r1 = 150.0f;
      const float r2 = 350.0f;
      float alpha = 2.0f * 3.1426f * ((float)j)/(float)n_sec;
      float beta  = 2.0f * 3.1426f * ((float)i)/(float)n_rad;
      float x = (r2 + r1 * cos(beta + anim1)) * cos(alpha + anim2);
      float y = (r2 + r1 * cos(beta + anim1)) * sin(alpha + anim2);
      float z = r1 * sin(beta + anim1);
      z += 1000.0f;
      int xp = cx + (int)((x * projection) / z);
      int yp = cy - (int)((y * projection) / z);  
      if ((xp >= 0) && (yp >= 0) && (xp < w) && (yp < h)) {
        drawlist[0] = xp;
        drawlist[1] = yp;
        drawlist[2] = (i + (j<<2)) & 0xff;
        drawlist += 3;
      }
    } 
  }

  // Devuelve el numero de vertices que se han metido en la lista de pintado
  // No es necesario hacerlo con resta de punteros, solo importa devolver el numero correcto
  return (drawlist - ori) / 3;
}

// ---------------------------------------------------------------------------

static void DisplayVertices (unsigned int* pixels, short* drawlist, int n_vertices, int stride, unsigned int* palette)
{
  int i;
  for(i = 0; i < n_vertices; i++) {
      int xp = drawlist[0];
      int yp = drawlist[1];
      pixels [xp + yp * stride] = palette[drawlist[2]];
      drawlist += 3;
  } 
}


// ---------------------------------------------------------------------------

static unsigned int palette [256];

int main ( int argc, char** argv)
{
  int end = 0;
  int mouse_x = 0, mouse_y = 0;
  SDL_Surface  *g_SDLSrf;
  int req_w = 1024;
  int req_h = 768; 

  if ( argc < 2) { fprintf ( stderr, "I need the cpu speed in Mhz!\n"); exit(0);}
  cpu_mhz = atoi( argv[1]);
  assert(cpu_mhz > 0);
  fprintf ( stdout, "Cycle times for a %d Mhz cpu\n", cpu_mhz);

  // Init SDL and screen
  if ( SDL_Init( SDL_INIT_VIDEO) < 0 ) {
      fprintf(stderr, "Can't Initialise SDL: %s\n", SDL_GetError());
      exit(1);
  }
  if (0 == SDL_SetVideoMode( req_w, req_h, 32,  SDL_HWSURFACE | SDL_DOUBLEBUF)) {
      printf("Couldn't set %dx%dx32 video mode: %s\n", req_w, req_h, SDL_GetError());
      return 0;
  }

  // Dot palette
  int i;
  for (i=0; i<256; i++)
    palette [i] = i | (i << 8) | (i << 16);

  // Small footprint buffer for vertices draw  
  // we are not using an structure to avoid padding to 8 bytes (instead of current 6)
  int n_vertices = 10000;
  short* drawlist = (short*) malloc (n_vertices * 3 * sizeof(short));

  // Setup your effect initialization here
  // Horizontal field of view
  float hfov = 60.0f * ((3.1416f * 2.0f) / 360.0f);  // Degrees to radians
  float half_scr_w = (float)(req_w >> 1);
  float projection = (1.0f / tan ( hfov * 0.5f)) * half_scr_w;

  // Main loop
  g_SDLSrf = SDL_GetVideoSurface();
  while ( !end) { 

    SDL_Event event;

    // Your gfx effect goes here

    ChronoWatchReset();
    int n_draw = DoEffect (drawlist, n_vertices, g_SDLSrf->w, g_SDLSrf->h, dump, projection);
    assert(n_draw <= n_vertices);
    ChronoShow ( "Donut festival", n_draw);

    // Draw vertices; don't modify this section
    // Lock screen to get access to the memory array
    SDL_LockSurface( g_SDLSrf);

    // Clean the screen
    SDL_FillRect(g_SDLSrf, NULL, SDL_MapRGB(g_SDLSrf->format, 0, 0, 0));
    ChronoShow ( "Clean", g_SDLSrf->w * g_SDLSrf->h);

    // Paint vertices
    DisplayVertices (g_SDLSrf->pixels, drawlist, n_draw, g_SDLSrf->pitch >> 2, palette);
    ChronoShow ( "Preview", n_draw);

    //Unlock the draw surface, dump to physical screen
    ChronoWatchReset();
    SDL_UnlockSurface( g_SDLSrf);
    SDL_Flip( g_SDLSrf);
    ChronoShow ( "Screen dump", g_SDLSrf->w * g_SDLSrf->h);

    // Limit framerate and return any remaining time to the OS
    // Comment this line for benchmarking
    FramerateLimit (60);

    dump++;

    // Recoger eventos de la ventana
    while ( SDL_PollEvent(&event) ) 
    {
      switch (event.type) 
      {
        case SDL_MOUSEMOTION:
          mouse_x = event.motion.x;
          mouse_y = event.motion.y;
          break;
        case SDL_MOUSEBUTTONDOWN:
          //printf("Mouse button %d pressed at (%d,%d)\n",
          //       event.button.button, event.button.x, event.button.y);
          break;
        case SDL_QUIT:
          end = 1;
          break;
      }
    }
  }

  return 1;
}


