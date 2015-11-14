/* Copyright (c) 2015 Harry Jeffery

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stddef.h>
#include <SDL2/SDL.h>
#include <FreeImage.h>
#include <getopt.h>

#include "image.h"
#include "texture.h"
#include "navigator.h"
#include "viewport.h"

SDL_Texture *create_chequered(SDL_Renderer *renderer);

struct {
  int fullscreen;
  int stdin;
  int recursive;
  int actual;
  int start_at;
  int solid_bg;
  int bg_r;
  int bg_g;
  int bg_b;
} g_options = {0,0,0,0,0,0,0,0,0};

void print_usage(const char* name)
{
  fprintf(stdout,
  "imv %s\n"
  "Usage: %s [-irfah] [-n NUM] [images...]\n"
  "\n"
  "Flags:\n"
  "  -i: Read paths from stdin. One path per line.\n"
  "  -r: Recursively search input paths.\n"
  "  -f: Start in fullscreen mode\n"
  "  -a: Default to images' actual size\n"
  "  -h: Print this help\n"
  "\n"
  "Options:\n"
  "  -n NUM: Start at picture number NUM.\n"
  "\n"
  "Mouse:\n"
  "   Click+Drag to Pan\n"
  "   MouseWheel to Zoom\n"
  "\n"
  "Hotkeys:\n"
  "         'q': Quit\n"
  "  '[',LArrow: Previous image\n"
  "  ']',RArrow: Next image\n"
  "     'i','+': Zoom in\n"
  "     'o','=': Zoom out\n"
  "         'h': Pan left\n"
  "         'j': Pan down\n"
  "         'k': Pan up\n"
  "         'l': Pan right\n"
  "         'r': Reset view\n"
  "         'a': Show image actual size\n"
  "         'c': Center view\n"
  "         'x': Close current image\n"
  "         'f': Toggle fullscreen\n"
  "         ' ': Toggle gif playback\n"
  "         '.': Step a frame of gif playback\n"
  "         'p': Print current image path to stdout\n"
  "\n"
  "Legal:\n"
  "This program is free software; you can redistribute it and/or\n"
  "modify it under the terms of the GNU General Public License\n"
  "as published by the Free Software Foundation; either version 2\n"
  "of the License, or (at your option) any later version.\n"
  "\n"
  "This software uses the FreeImage open source image library.\n"
  "See http://freeimage.sourceforge.net for details.\n"
  "FreeImage is used under the GNU GPLv2.\n"
  , IMV_VERSION, name);
}

void parse_args(int argc, char** argv)
{
  /* Do not print getopt errors */
  opterr = 0;

  const char* name = argv[0];
  char o;
  char* end;
  int n;

  while((o = getopt(argc, argv, "firahn:")) != -1) {
    switch(o) {
      case 'f': g_options.fullscreen = 1;   break;
      case 'i':
        g_options.stdin = 1;
        fprintf(stderr, "Warning: '-i' is deprecated. Just use '-' instead.\n");
        break;
      case 'r': g_options.recursive = 1;    break;
      case 'a': g_options.actual = 1;       break;
      case 'h': print_usage(name); exit(0); break;
      case 'n':
        n = strtol(optarg,&end,0);
        if(*end != '\0' || n <= 0) {
          fprintf(stderr, "Warning: wrong value for '-n'.\n");
        } else {
          g_options.start_at = n - 1;
        }
        break;
      case '?':
        fprintf(stderr, "Unknown argument '%c'. Aborting.\n", optopt);
        exit(1);
    }
  }
}

int main(int argc, char** argv)
{
  if(argc < 2) {
    print_usage(argv[0]);
    exit(1);
  }

  struct imv_navigator nav;
  imv_init_navigator(&nav);

  parse_args(argc,argv);

  for(int i = optind; i < argc; ++i) {
    if(!strcmp("-",argv[i])) {
      g_options.stdin = 1;
      continue;
    }
    if(g_options.recursive) {
      imv_navigator_add_path_recursive(&nav, argv[i]);
    } else {
      imv_navigator_add_path(&nav, argv[i]);
    }
  }

  if(g_options.stdin) {
    char buf[512];
    while(fgets(buf, sizeof(buf), stdin)) {
      size_t len = strlen(buf);
      if(buf[len-1] == '\n') {
        buf[--len] = 0;
      }
      if(len > 0) {
        if(g_options.recursive) {
          imv_navigator_add_path_recursive(&nav, buf);
        } else {
          imv_navigator_add_path(&nav, buf);
        }
      }
    }
  }

  if(!imv_navigator_get_current_path(&nav)) {
    fprintf(stderr, "No input files. Exiting.\n");
    exit(1);
  }

  if(SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL Failed to Init: %s\n", SDL_GetError());
    exit(1);
  }

  const int width = 1280;
  const int height = 720;

  SDL_Window *window = SDL_CreateWindow(
        "imv",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_RESIZABLE);

  SDL_Renderer *renderer =
    SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  /* Use linear sampling for scaling */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  /* construct a chequered background texture */
  SDL_Texture *chequered_tex = create_chequered(renderer);

  struct imv_image img;
  imv_init_image(&img);

  struct imv_texture tex;
  imv_init_texture(&tex, renderer);

  struct imv_viewport view;
  imv_init_viewport(&view, window);

  /* Put us in fullscren by default if requested */
  if(g_options.fullscreen) {
    imv_viewport_toggle_fullscreen(&view);
  }

  double last_time = SDL_GetTicks() / 1000.0;

  imv_navigator_set_path(&nav, g_options.start_at);

  int quit = 0;
  while(!quit) {
    SDL_Event e;
    while(!quit && SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_QUIT:
          quit = 1;
          break;
        case SDL_KEYDOWN:
          switch (e.key.keysym.sym) {
            case SDLK_q:      quit = 1;                                break;
            case SDLK_LEFTBRACKET:
            case SDLK_LEFT:   imv_navigator_prev_path(&nav);           break;
            case SDLK_RIGHTBRACKET:
            case SDLK_RIGHT:  imv_navigator_next_path(&nav);           break;
            case SDLK_EQUALS:
            case SDLK_i:
            case SDLK_UP:     imv_viewport_zoom(&view, &img, KBD, 1);  break;
            case SDLK_MINUS:
            case SDLK_o:
            case SDLK_DOWN:   imv_viewport_zoom(&view, &img, KBD, -1); break;
            case SDLK_a:     imv_viewport_scale_to_actual(&view, &img);break;
            case SDLK_r:     imv_viewport_scale_to_window(&view, &img);break;
            case SDLK_c:      imv_viewport_center(&view, &img);        break;
            case SDLK_j:      imv_viewport_move(&view, 0, -50);        break;
            case SDLK_k:      imv_viewport_move(&view, 0, 50);         break;
            case SDLK_h:      imv_viewport_move(&view, 50, 0);         break;
            case SDLK_l:      imv_viewport_move(&view, -50, 0);        break;
            case SDLK_x:      imv_navigator_remove_current_path(&nav); break;
            case SDLK_f:      imv_viewport_toggle_fullscreen(&view);   break;
            case SDLK_PERIOD: imv_image_load_next_frame(&img);         break;
            case SDLK_SPACE:  imv_viewport_toggle_playing(&view, &img);break;
            case SDLK_p:    puts(imv_navigator_get_current_path(&nav));break;
          }
          break;
        case SDL_MOUSEWHEEL:
          imv_viewport_zoom(&view, &img, MOUSE, e.wheel.y);
          break;
        case SDL_MOUSEMOTION:
          if(e.motion.state & SDL_BUTTON_LMASK) {
            imv_viewport_move(&view, e.motion.xrel, e.motion.yrel);
          }
          break;
        case SDL_WINDOWEVENT:
          imv_viewport_updated(&view, &img);
          break;
      }
    }

    if(quit) {
      break;
    }

    while(imv_navigator_has_changed(&nav)) {
      const char* current_path = imv_navigator_get_current_path(&nav);
      char title[256];
      snprintf(&title[0], sizeof(title), "imv - [%i/%i] [LOADING] %s",
          nav.cur_path + 1, nav.num_paths, current_path);
      imv_viewport_set_title(&view, title);

      if(!current_path) {
        fprintf(stderr, "No input files left. Exiting.\n");
        exit(1);
      }

      if(imv_image_load(&img, current_path) != 0) {
        imv_navigator_remove_current_path(&nav);
      } else {
        snprintf(&title[0], sizeof(title), "imv - [%i/%i] [%ix%i] %s",
            nav.cur_path + 1, nav.num_paths,
            img.width, img.height, current_path);
        imv_viewport_set_title(&view, title);
        imv_viewport_scale_to_window(&view, &img);
      }
      if(g_options.actual) {
        imv_viewport_scale_to_actual(&view, &img);
      }
    }

    if(view.playing) {
      double cur_time = SDL_GetTicks() / 1000.0;
      double dt = cur_time - last_time;
      last_time = SDL_GetTicks() / 1000.0;
      imv_image_play(&img, dt);
    }

    if(imv_image_has_changed(&img)) {
      imv_texture_set_image(&tex, img.cur_bmp);
      imv_viewport_set_redraw(&view);
    }

    if(view.redraw) {
      if(g_options.solid_bg) {
        SDL_SetRenderDrawColor(renderer,
            g_options.bg_r, g_options.bg_g, g_options.bg_b, 255);
        SDL_RenderClear(renderer);
      } else {
        /* chequered background */
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        int img_w, img_h;
        SDL_QueryTexture(chequered_tex, NULL, NULL, &img_w, &img_h);
        for(int y = 0; y < wh; y += img_h) {
          for(int x = 0; x < ww; x += img_w) {
            SDL_Rect dst_rect = {x,y,img_w,img_h};
            SDL_RenderCopy(renderer, chequered_tex, NULL, &dst_rect);
          }
        }
      }
      imv_texture_draw(&tex, view.x, view.y, view.scale);
      view.redraw = 0;
      SDL_RenderPresent(renderer);
    }
    SDL_Delay(10);
  }

  imv_destroy_image(&img);
  imv_destroy_texture(&tex);
  imv_destroy_navigator(&nav);
  imv_destroy_viewport(&view);

  SDL_DestroyTexture(chequered_tex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}

SDL_Texture *create_chequered(SDL_Renderer *renderer)
{
  SDL_RendererInfo ri;
  SDL_GetRendererInfo(renderer, &ri);
  int width = 512;
  int height = 512;
  if(ri.max_texture_width != 0 && ri.max_texture_width < width) {
    width = ri.max_texture_width;
  }
  if(ri.max_texture_height != 0 && ri.max_texture_height < height) {
    height = ri.max_texture_height;
  }
  const int box_size = 16;
  /* Create a chequered texture */
  const unsigned char l = 196;
  const unsigned char d = 96;

  size_t pixels_len = 3 * width * height;
  unsigned char *pixels = malloc(pixels_len);
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x += box_size) {
      unsigned char color = l;
      if(((x/box_size) % 2 == 0) ^ ((y/box_size) % 2 == 0)) {
        color = d;
      }
      memset(pixels + 3 * x + 3 * width * y, color, 3 * box_size);
    }
  }
  SDL_Texture *ret = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
    SDL_TEXTUREACCESS_STATIC,
    width, height);
  SDL_UpdateTexture(ret, NULL, pixels, 3 * width);
  free(pixels);
  return ret;
}
