#define SDL_MAIN_HANDLED
#define SPNG_STATIC
#define GLT_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <gl/glew.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"

#include "spng.h"
#include "gltext.h"


#define UNPOS SDL_WINDOWPOS_UNDEFINED
#define SHOWN SDL_WINDOW_SHOWN

__declspec(dllexport) uint32_t NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;


void __bad(const char* msg, const char* error) {
  printf("failed to %s : %s\n", msg, error);
  exit(0);
}


typedef struct stat stat_t; 

typedef struct offscreen_s {
  GLuint fb;
  GLuint rb;
  GLuint tx;
} offscreen_t;


typedef struct shape_s {
  float *xyz;
  float *uvw;
  GLuint buff[2];
  GLuint root;
  size_t size; 
} shape_t;

void dispose_shape(shape_t sp) {
  free(sp.xyz);
  free(sp.uvw);
  glDeleteBuffers(2, sp.buff);
  glDeleteVertexArrays(1, &(sp.root));
}

void draw_shape(shape_t sp, GLuint pr, GLuint tx) {
  glUseProgram(pr);
  glBindVertexArray(sp.root);
  if (tx) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tx);
  }  
  glDrawArrays(GL_TRIANGLES, 0, sp.size);
  glUseProgram(0);
}


typedef struct point_s {
  union {
    struct { float x, y, z; };
    struct { float u, v, w; };
  };
} point_t;


point_t scale_ndc(point_t pt, int w, int h) {
  float ratio = w / (float)h;
  if (ratio < 1) return (point_t){ pt.x, pt.y / ratio, pt.z };
  return (point_t){ pt.x * ratio, pt.y, pt.z };
}


GLuint   active_program = 0;
GLuint   poster_program = 0;
shape_t  active_screen;


void enable_buffers(shape_t sp) {
  float* data[] = { sp.xyz, sp.uvw };
  for (int n = 0; n < 2; n++) {
    glBindBuffer(GL_ARRAY_BUFFER, sp.buff[n]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * sp.size, data[n], GL_STATIC_DRAW);
    glVertexAttribPointer(n, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(n);
  }
}

shape_t gen_quad(point_t a_xyz, point_t b_xyz, point_t a_uvw, point_t b_uvw) {
  shape_t quad;
  point_t xyz[] = { a_xyz, b_xyz };
  point_t uvw[] = { a_uvw, b_uvw };
  
  quad.size = 6;
  quad.xyz = calloc(3 * quad.size, sizeof(float));
  quad.uvw = calloc(3 * quad.size, sizeof(float));
  
  for (char n = 0, x = 0x29; n < 6; n++) {
    char ix = (x >> n) & 0x01; 
    char iy = (~x >> (5-n)) & 0x01;
    
    quad.xyz[n*3] = xyz[ix].x;
    quad.uvw[n*3] = uvw[ix].u;
    
    quad.xyz[n*3 + 1] = xyz[iy].y;
    quad.uvw[n*3 + 1] = uvw[iy].v;  
  }
  
  glGenBuffers(2, quad.buff);
  glGenVertexArrays(1, &(quad.root));
  glBindVertexArray(quad.root);
  enable_buffers(quad);
  
  return quad;
}


bool check_shader(GLuint shader) {
  GLint isCompiled;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    GLint ll;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &ll);
    char log[ll];
    glGetShaderInfoLog(shader, ll, &ll, log);
    printf("%s", log);
    return false;
  }
  return true;
}

char* load_shader_code(const char *path) {
  FILE *fl = fopen(path, "r");
  if (fl == NULL) 
    __bad("open shader file", path);
  fseek(fl, 0, SEEK_END);
  long size = ftell(fl);
  fseek(fl, 0, SEEK_SET);
  char *code = malloc(size + 1);
  fread(code, 1, size, fl);
  fclose(fl);
  code[size] = 0;
  return code;
}

void dispose_shaders(GLuint shader[]) {
  for (; *shader; shader++) {
    glDeleteShader(*shader);
  }
}


bool check_program(GLuint program) {
  GLint isLinked;
  glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
  if (isLinked == GL_FALSE) {
    GLint ll;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ll);
    char log[ll];
    glGetProgramInfoLog(program, ll, &ll, log);
    printf("%s", log);
    return false;
  }
  return true;
}


GLuint create_program(const char** vert_s, const char** frag_s) {
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    
    glShaderSource(vert, 1, vert_s, 0);
    glShaderSource(frag, 1, frag_s, 0);
    
    glCompileShader(vert);
    if (!check_shader(vert)) {
      dispose_shaders((GLuint[3]){vert, frag, 0});
      return 0;
    }
    
    glCompileShader(frag);
    if (!check_shader(frag)) {
      dispose_shaders((GLuint[3]){vert, frag, 0});
      return 0;
    }
    
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    dispose_shaders((GLuint[3]){vert, frag, 0});
    
    if (!check_program(prog)) {
      glDeleteProgram(prog);
      return 0;
    } else return prog;
}


void flip_y_axis(void *dest, void *src, size_t height, size_t row_size) {
  for (int n = 0; n < height; n++) {
    memcpy(dest + row_size * n , src + row_size * (height - 1 - n), row_size);
  }
}



SDL_Window* create_window(int width, int height) {
  
  if(SDL_Init(SDL_INIT_VIDEO) < 0) 
    __bad("init SDL", SDL_GetError());
  
  SDL_Window* window = NULL;
  SDL_GLContext context;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  
  window = SDL_CreateWindow("shader-view", UNPOS, UNPOS, width, height, SDL_WINDOW_OPENGL);
  if (window == NULL) 
      __bad("create window", SDL_GetError());
  
  context = SDL_GL_CreateContext(window);
  if (context == NULL) 
    __bad("create opengl context", SDL_GetError());       
  
  glewExperimental = GL_TRUE;
  GLenum glew_error = glewInit();
  
  if (glew_error != GLEW_OK)
    __bad("initialize glew", glewGetErrorString(glew_error));
  if (SDL_GL_SetSwapInterval(1) < 0)
    __bad("set vsync", SDL_GetError());
  
  return window;
}

void dispose_window(SDL_Window* window) {
  glDeleteProgram(poster_program);
  glDeleteProgram(active_program);
  SDL_DestroyWindow(window);
  SDL_Quit();
}


offscreen_t create_offscreen(size_t w, size_t h) {
  offscreen_t off;
  glGenFramebuffers(1, &(off.fb));
  glBindFramebuffer(GL_FRAMEBUFFER, off.fb);
  
  glGenTextures(1, &(off.tx));
  glBindTexture(GL_TEXTURE_2D, off.tx);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, off.tx, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  
  glGenRenderbuffers(1, &(off.rb));
  glBindRenderbuffer(GL_RENDERBUFFER, off.rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, off.rb);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    __bad("create offscreen buffer", "");  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return off;
} 

void dispose_offscreen(offscreen_t off) {
  glDeleteTextures(1, &(off.tx));
  glDeleteRenderbuffers(1, &(off.rb));
  glDeleteFramebuffers(1, &(off.fb));
}


void draw_content(offscreen_t off) {
  glBindFramebuffer(GL_FRAMEBUFFER, off.fb);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  draw_shape(active_screen, active_program, 0);
  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_shape(active_screen, poster_program, off.tx);
}


static const char* usage = 
"shader-view: interactive preview for 2D fragment shaders.\n\n"
"modes: \n\n"
"shader-view -f shader.frag -- non interactive with hot-reload.\n"
"shader-view -i -f shader.frag -- interactive with mouse and time input.\n"
"shader-view -a N -f shader.frag -o name -- save one cycle of animation.\n\n"
"other options: \n\n"
"-x W,H   -- set window width and height (default 600,600).\n"
"-d value -- delay in milliseconds between window updates (default 20).\n"
"-a N     -- number of frames to save (remember time goes from 0.0 to 1.0).\n"
"-o name  -- images saved as name_1.png name_2.png name_N.png.\n";

const char *bypass_vert =
"#version 430 \n"
"layout(location = 0) in vec3 pos; \n"
"layout(location = 1) in vec3 tex; \n"
"out vec2 uv; \n"
"void main() { gl_Position = vec4(pos, 1.); uv = tex.xy; } \n";

const char *post_frag =
"#version 430 \n"
"in vec2 uv; \n"
"out vec4 color; \n"
"uniform sampler2D tex; \n"
"layout(location = 0) uniform int mode;\n "

"void main() { \n"
" vec2 ts = textureSize(tex, 0); \n"
" vec2 pp = uv / max(vec2(1.0), (ts.xy / ts.yx)); \n"
" vec4 cc = texture2D(tex, (pp + 1) * 0.5); \n"
" switch(mode) { \n"
"   case 0 : color = cc; break; \n"
"   case 1 : color = vec4(vec3(cc.r), 1.); break; \n"
"   case 2 : color = vec4(vec3(cc.g), 1.); break; \n"
"   case 3 : color = vec4(vec3(cc.b), 1.); break; \n"
"   case 4 : color = abs(cc); break; \n"
"   case 5 : color = vec4(1 - cc.rgb, 1.0); break;} } \n";


void split_path(const char* src, char* path, char* name) {
  char* x = strrchr(src, '/');
  if (x) {
    memcpy(name, x + 1, strlen(x));
    memcpy(path, src, strlen(src) - strlen(x));
  } else {
    memcpy(name, src, strlen(src));
  }
}

void check_mkdir(int error, const char* path) {
  if (error == 0 || errno == EEXIST) return;
  __bad("create directory", path);
}

void drill_path(char* path) {
  char* x = path[0] != '.' ? strchr(path, '/') : strchr(strchr(path, '/') + 1, '/');
  for(; x; x = strchr(x + 1, '/')) {
    *x = 0;
    check_mkdir(mkdir(path), path);
    *x = '/';
  }
  check_mkdir(mkdir(path), path);
}


int argument_pos(int argc, char **argv, const char *arg) {
  for (int n = 0; n < argc; n++) {
    if (strcmp(argv[n], arg) == 0) return n;
  }
  return 0;
}

void update_info(GLTtext *info, int x, int y, GLuint fb) {
  float px[3] = {0};
  char  sn[3];
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glReadPixels(x, y, 1, 1, GL_RGB, GL_FLOAT, px);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  for (int n = 0; n < 4; n++) { 
    sn[n] = px[n] >= 0 ? '+' : '-';
    px[n] = fabs(px[n]);
  }
  sprintf(info->_text, "INFO/R%c%1.3f/G%c%1.3f/B%c%1.3f", 
    sn[0], px[0], sn[1], px[1], sn[2], px[2]);
  info->_dirty = GL_TRUE;
}


int main(int argc, char **argv) {
  
  if (argument_pos(argc, argv, "-h") > 0) {
    printf("%s", usage);
    return 0;
  }
  int shader_path = argument_pos(argc, argv, "-f");
  if (shader_path > 0) {
    shader_path += 1;  
  } else {
    __bad("get shader path", "use -h for help");
  }
  
  int width = 600, height = 600, delay = 20;
  int size_arg = argument_pos(argc, argv, "-x");
  if (size_arg > 0) {
    sscanf(argv[size_arg + 1], "%d,%d", &width, &height);
  }
  int delay_arg = argument_pos(argc, argv, "-d");
  if (delay_arg > 0) {
    sscanf(argv[delay_arg + 1], "%d", &delay);
  }
  
  
  SDL_Window* window = create_window(width, height);
  offscreen_t offscr = create_offscreen(width, height);
  active_screen = gen_quad(  
    (point_t){-1, 1, 0}, 
    (point_t){1, -1, 0}, 
    scale_ndc((point_t){-1, 1, 0}, width, height), 
    scale_ndc((point_t){1, -1, 0}, width, height));

  poster_program = create_program(&bypass_vert, &post_frag);
  
  // ANIMATION BATCH

  int anim_arg = argument_pos(argc, argv, "-a");
  if (anim_arg > 0) {
    int out_arg = argument_pos(argc, argv, "-o");
    if (out_arg > 0) {
      
      int num_frames = atoi(argv[anim_arg + 1]);
      const char* src_frag = load_shader_code(argv[shader_path]);
      active_program = create_program(&bypass_vert, &src_frag);
     
      struct spng_ihdr ihdr = {
        .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA,
        .height = height,
        .width = width,
        .bit_depth = 24,
      };
      
      float delta = 1.0 / (num_frames-1);
      FILE* out_file[num_frames];
      size_t img_size = width * height * 4;
      
      uint16_t *frame_buf = malloc(sizeof(uint16_t) * img_size);
      uint16_t *flipped_buf = malloc(sizeof(uint16_t) * img_size);
      
      char out_name[128] = {0};
      char out_path[128] = {0};
      
      split_path(argv[out_arg + 1], out_path, out_name);
      if (strlen(out_path) > 0) drill_path(out_path);
      
      printf("start animation rendering\n");
          
      for (int n = 0; n < num_frames; n++) { 
        spng_ctx *enc = spng_ctx_new(SPNG_CTX_ENCODER);
        
        char out_join[128] = {0};
        sprintf(out_join, "%s/%s_%d.png", out_path, out_name, n);
        out_file[n] = fopen(out_join, "wb");
        
        if (out_file[n] != NULL) {
          spng_set_png_file(enc, out_file[n]);
          spng_set_ihdr(enc, &ihdr);
          
          glProgramUniform1f(active_program, 0, delta * n);
          draw_content(offscr);
          glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT, frame_buf);
          SDL_GL_SwapWindow(window);
          
          flip_y_axis(flipped_buf, frame_buf, height, sizeof(uint16_t) * width * 4);
          spng_encode_image(enc, flipped_buf, sizeof(uint16_t) * img_size, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
        
        } else {
          __bad("write output file", out_join);
        }  
        spng_ctx_free(enc);
      }
      free(frame_buf);
      free(flipped_buf);
      free((char*)src_frag);
      for (int n = 0; n < num_frames; n++) { 
        fclose(out_file[n]);
      }
      printf("... done (%d frames).\n", num_frames);        
    } else {
      __bad("output animation", "use -h for help");
    }
    
    dispose_shape(active_screen);
    dispose_window(window);
    return 0;
  }

  // INTERACTIVE AND PERSISTENT
  
  gltInit();
  GLTtext *info = gltCreateText();
  gltSetText(info, "INFO/R+0.000/G+0.000/B+0.000");
  
  bool interactive = argument_pos(argc, argv, "-i") > 0;
  bool finished = false;
  bool request_update = true;
  bool request_info = false;
  
  SDL_Event event;
  stat_t fstat[2] = {0};
       
  while (!finished) {
  
    if(stat(argv[shader_path], &fstat[1]) < 0)
      __bad("read shader file", argv[shader_path]);
    if (fstat[0].st_mtime != fstat[1].st_mtime) {
      printf("new shader\n");
      const char* src_frag = load_shader_code(argv[shader_path]);
      GLuint program = create_program(&bypass_vert, &src_frag);
      if (program) {
        if (active_program)  {
          glDeleteProgram(active_program);
        }
        active_program = program;
        request_update = true;
        if (!interactive) {
          draw_content(offscr);
          SDL_GL_SwapWindow(window);
        }
      }
      fstat[0] = fstat[1];
      free((char*)src_frag);
    }
    
    for (int n = 0; n < 1000 / delay; n++) {
      
      while(SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) finished = true;
        if (event.type == SDL_MOUSEMOTION && interactive) {
          int x, y;
          uint32_t btn_state = SDL_GetMouseState(&x, &y);
          
          if (btn_state & SDL_BUTTON(1)) {
            float ndc_x = (((float)x / width) * 2) - 1;
            float ndc_y = (((float)y / height) * 2) - 1;
            point_t mouse_pt = scale_ndc((point_t) { ndc_x, -ndc_y }, width, height);
            glProgramUniform2f(active_program, 1, mouse_pt.x, mouse_pt.y);            
          }
          if (btn_state & SDL_BUTTON(3)) {
            update_info(info, x, height - y, offscr.fb);
          }
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
          if (event.button.button == SDL_BUTTON_RIGHT) {
            update_info(info, event.button.x, height - event.button.y, offscr.fb);
            request_info = true;
          }
        }
        if (event.type == SDL_MOUSEBUTTONUP) { 
          request_info = false;
        }
        if (event.type == SDL_KEYDOWN) {
          int mode = 0;
          switch (event.key.keysym.sym) {
            case SDLK_r : mode = 1; break;
            case SDLK_g : mode = 2; break;
            case SDLK_b : mode = 3; break; 
            case SDLK_a : mode = 4; break;
            case SDLK_i : mode = 5; break;
          }
          glProgramUniform1i(poster_program, 0, mode);
        }
        if (event.type == SDL_KEYUP) {
          glProgramUniform1i(poster_program, 0, 0);
        }
        
        request_update = true;
      }
      if (interactive && request_update) { 
        draw_content(offscr);
        if (request_info) {
          gltBeginDraw();
          gltColor(1.0, 1.0, 1.0, 1.0);
          gltDrawText2D(info, 0, 0, 1);
          gltEndDraw();
        }
        SDL_GL_SwapWindow(window);
      }
      request_update = false;
      SDL_Delay(delay);   
    }
  }
  dispose_offscreen(offscr);
  dispose_shape(active_screen);
  gltTerminate();
  dispose_window(window);
  return 0;
}
