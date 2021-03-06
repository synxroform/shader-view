
// MIT LICENSE
// (c) github.com/synxroform


#define SDL_MAIN_HANDLED
#define SPNG_STATIC
#define GLT_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

#include <windows.h>

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
  GLuint fb[2]; // 0-direct 1-feedback
  GLuint rb[2]; // ..
  GLuint tx[2]; // ..
  size_t wh[2]; // width,height
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


typedef struct __program_set {
  union {
    GLuint prog[2];
    struct { GLuint frag, comp; };
  };
  GLuint post;
  struct {
    GLuint id;
    void* data;
    size_t item_size;
    size_t num_items;
  } ssbo;
} program_set_t;


program_set_t pgset = { 0, 0, 0, {0, NULL, 0} };
shape_t screen_quad;


void dispose_program_set(program_set_t set) {
  if (set.frag) glDeleteProgram(set.frag);
  if (set.post) glDeleteProgram(set.post);
  if (set.comp) glDeleteProgram(set.comp);
  if (set.ssbo.data) free(set.ssbo.data);
  if (set.ssbo.id) glDeleteBuffers(1, &(set.ssbo.id));
}


void update_compute_buffer(program_set_t *set, size_t item_size, size_t num_items) {
  if (set->ssbo.item_size * set->ssbo.num_items != item_size * num_items) {
    if (set->ssbo.data) free(set->ssbo.data);
    if (set->ssbo.id) glDeleteBuffers(1, &(set->ssbo.id));
    set->ssbo.data = malloc(item_size * num_items);
    glGenBuffers(1, &(set->ssbo.id));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, set->ssbo.id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, item_size * num_items, &(set->ssbo.data), GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    set->ssbo.item_size = item_size;
    set->ssbo.num_items = num_items;
  }
}

void broadcast_uniform1f(program_set_t set, GLuint id, float x) {
  for (int n = 0; n < 2; n++) {
    if (set.prog[n]) glProgramUniform1f(set.prog[n], id, x);
  }
}

void broadcast_uniform2f(program_set_t set, GLuint id, float x, float y) {
  for (int n = 0; n < 2; n++) {
    if (set.prog[n]) glProgramUniform2f(set.prog[n], id, x, y);
  }
}



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


// FILESYSTEM

char *ltrim(char *s) {
    while(isspace(*s)) s++; 
    return s;
}

void strpak(char *s, char end, char *out) {
  for (; *s != end; s++) {
    if (isspace(*s)) { 
      continue;
    } else {
      *out = *s;
      out++;
    } 
  }
}

void split_path(const char* src, char* path, char* name) {
  char* x = strrchr(src, '/');
  if (x) {
    memcpy(name, x + 1, strlen(x));
    memcpy(path, src, strlen(src) - strlen(x));
  } else {
    memcpy(name, src, strlen(src));
  }
}

void get_extension(const char* path, char* ext) {
  char* x = strrchr(path, '.');
  if (x) memcpy(ext, x + 1, strlen(x));
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


typedef struct __code_block {
  char* frag;
  char* comp;
  char* vert;
  struct __comp_opts {
    size_t item_size;
    size_t num_items;
  } comp_opts;
} code_block_t;


void dispose_code_block(code_block_t bk) {
  if (bk.frag) free(bk.frag);
  if (bk.comp) free(bk.comp);
  if (bk.vert) free(bk.vert);
}


code_block_t split_composed_code(char* code) {
    code_block_t block = { NULL, NULL, NULL, {0} };
    const char *action = "read composed file";
    char *p = code;
    
    if (p = strchr(p, '$')) {
      char *header_end = p;
      size_t header_size = header_end - code;
      p = ltrim(p + 1);
      if (strncmp("compute", p, 7) == 0) {
        if (p = strchr(p, '[')) {
          char args[64] = {0};
          strpak(p + 1, ']', args);
          sscanf(args, "%d,%d", &(block.comp_opts.item_size), &(block.comp_opts.num_items));
        }
        char *comp_start = strchr(p, '\n') + 1;
        if (p = strchr(p, '$')) {
          size_t comp_size = p - comp_start;
          block.comp = malloc(comp_size + header_size + 1);
          memcpy(block.comp, code, header_size);
          memcpy(block.comp + header_size, comp_start, comp_size);
          block.comp[comp_size + header_size] = 0;
          
          char *frag_start = strchr(p, '\n') + 1;
          p = strchr(p, 0);
          size_t frag_size = p - frag_start;
          block.frag = malloc(frag_size + header_size + 1);
          memcpy(block.frag, code, header_size);
          memcpy(block.frag + header_size, frag_start, frag_size);
          block.frag[frag_size + header_size] = 0;
        
        } else {
          __bad(action, "fragment block required");
        }
      } else {
        __bad(action, "no compute block");
      }
    } else {
      __bad(action, "no blocks");
    }
    return block;
}

code_block_t load_shader_code(const char *path) {
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
  
  char ext[8] = {0};
  get_extension(path, ext);
  if (strcmp(ext, "comp") == 0) {
      code_block_t cb = split_composed_code(code);
      free(code);
      return cb;
  } else {
    return (code_block_t){ code, NULL, NULL, {0} };
  }
}

// SHADER PROGRAM

void dispose_shaders(GLuint prog, GLuint shader[]) {
  for (; *shader; shader++) {
    glDetachShader(prog, *shader);
    glDeleteShader(*shader);
  }
}


bool check_shader(GLuint shader) {
  GLint isCompiled;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    GLint ll;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &ll);
    char log[ll];
    glGetShaderInfoLog(shader, ll, &ll, log);
    printf("[COMPILATION ERROR]\n%s", log);
    return false;
  }
  return true;
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


GLuint create_shader(const char** src, GLenum type) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, src, 0);
  glCompileShader(s);
  if (!check_shader(s)) {
    glDeleteShader(s);
    return 0;
  }
  return s;
}


GLuint create_program(const char** vert_s, const char** frag_s, const char** comp_s) {
    GLuint prog = glCreateProgram();
    GLuint sd[3] = {0};
    
    if (comp_s) {
      sd[0] = create_shader(comp_s, GL_COMPUTE_SHADER);
    } else {
      sd[0] = create_shader(vert_s, GL_VERTEX_SHADER);
      sd[1] = create_shader(frag_s, GL_FRAGMENT_SHADER); 
    }
    for (int n = 0; sd[n]; n++) glAttachShader(prog, sd[n]); 
    
    glLinkProgram(prog);
    if (!check_program(prog)) {
      glDeleteProgram(prog);
      return 0;
    };
    dispose_shaders(prog, sd);
    return prog;
}


// IMAGE TOOLS

void flip_y_axis(void *dest, void *src, size_t height, size_t row_size) {
  for (int n = 0; n < height; n++) {
    memcpy(dest + row_size * n , src + row_size * (height - 1 - n), row_size);
  }
}


// WINDOW

SDL_Window* create_window(int width, int height) {
  
  if(SDL_Init(SDL_INIT_VIDEO) < 0) 
    __bad("init SDL", SDL_GetError());
  
  SDL_Window* window = NULL;
  SDL_GLContext context;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  
  // as of now this is meaningless  
  // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
  
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
  
  //glEnable(GL_MULTISAMPLE);
  return window;
}

void dispose_window(SDL_Window* window) {
  SDL_DestroyWindow(window);
  SDL_Quit();
}


offscreen_t create_offscreen(size_t w, size_t h) {
  offscreen_t off = { .wh = {w, h} };
  glGenFramebuffers(2, off.fb);
  glGenTextures(2, off.tx);
  glGenRenderbuffers(2, off.rb);
  
  // RAW buffer
  glBindFramebuffer(GL_FRAMEBUFFER, off.fb[0]);
  
  glBindTexture(GL_TEXTURE_2D, off.tx[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, off.tx[0], 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  
  glBindRenderbuffer(GL_RENDERBUFFER, off.rb[0]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, off.rb[0]);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    __bad("create offscreen buffer", "");  
  
  // MSAA buffer // this trick doesn't work with procedural textures
  /*
  glBindFramebuffer(GL_FRAMEBUFFER, off.fb[1]);
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, off.tx[1]);
  glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGB32F, w, h, true);
  glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, off.tx[1], 0);
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
  
  glBindRenderbuffer(GL_RENDERBUFFER, off.rb[1]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, off.rb[1]);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    __bad("create MSAA buffer", "");
  */
  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return off;
} 

void dispose_offscreen(offscreen_t off) {
  glDeleteTextures(2, off.tx);
  glDeleteRenderbuffers(2, off.rb);
  glDeleteFramebuffers(2, off.fb);
}


void draw_content(offscreen_t off) {
  // COMPUTE SHADER [OPTIONAL]
  if (pgset.comp) {
    glUseProgram(pgset.comp);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pgset.ssbo.id);
    int group_size[3] = {1};
    glGetProgramiv(pgset.comp, GL_COMPUTE_WORK_GROUP_SIZE, group_size);
    glDispatchCompute(pgset.ssbo.num_items / group_size[0] , group_size[1], group_size[2]);
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
  }
  
  // FRAGMENT SHADER
  glBindFramebuffer(GL_FRAMEBUFFER, off.fb[0]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  draw_shape(screen_quad, pgset.frag, 0);
  
  // POSTPOROCESS SHADER
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_shape(screen_quad, pgset.post, off.tx[0]);
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
"sample smooth out vec2 uv; \n"
"void main() { gl_Position = vec4(pos, 1.); uv = tex.xy; } \n";

const char *post_frag =
"#version 430 \n"
"sample smooth in vec2 uv; \n"
"out vec4 color; \n"
"uniform sampler2D tex; \n"
"layout(location = 0) uniform int mode;\n "

"vec3 color_picker(vec2 uv) { \n"
"  vec3 hsb = clamp(vec3(uv.x, uv.y, 1.), 0., 1.); \n"
"  vec3 rgb = clamp(abs(mod(hsb.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0,1.0 ); \n"
"  rgb = rgb*rgb*(3.0-2.0*rgb); \n"
"  return hsb.z * mix(vec3(1.0), rgb, hsb.y); } \n"

"void main() { \n"
" vec2 ts = textureSize(tex, 0); \n"
" vec2 pp = uv / max(vec2(1.0), (ts.xy / ts.yx)); \n"
" vec4 cc = texture2D(tex, (pp + 1) * .5); \n"
" switch(mode) { \n"
"   case 0 : color = cc; break; \n"
"   case 1 : color = vec4(vec3(cc.r), 1.); break; \n"
"   case 2 : color = vec4(vec3(cc.g), 1.); break; \n"
"   case 3 : color = vec4(vec3(cc.b), 1.); break; \n"
"   case 4 : color = abs(cc); break; \n"
"   case 5 : color = vec4(1 - cc.rgb, 1.); break; \n"
"   case 6 : color = vec4(color_picker((pp + 1) * 0.5), 1.); } \n"
"} \n";





int argument_pos(int argc, char **argv, const char *arg) {
  for (int n = 0; n < argc; n++) {
    if (strcmp(argv[n], arg) == 0) return n;
  }
  return 0;
}


void copy_to_clipboard(const char* msg) {
  size_t ml = strlen(msg);
  void* clip = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, ml + 1);
  void* clip_mem = GlobalLock(clip);
  memcpy(clip_mem, msg, ml);
  ((char*)clip_mem)[ml] = 0;
  GlobalUnlock(clip);
  
  if (OpenClipboard(NULL)) {
    EmptyClipboard();
    SetClipboardData(CF_TEXT, clip);
    CloseClipboard();
  }
}




void update_info(GLTtext *info, int x, int y, GLuint fb, char* clipboard) {
  float px[3] = {0};
  char  xx[16 * 3] = {0};
  char  sn[3];
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glReadPixels(x, y, 1, 1, GL_RGB, GL_FLOAT, px);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  for (int n = 0; n < 3; n++) { 
    if (!isinf(px[n]) && !isnan(px[n])) {
      sn[n] = px[n] >= 0 ? '+' : '-';
      px[n] = fabs(px[n]);
      sprintf(xx + 16 * n, "%1.3f", px[n]);
    } else {
      sn[n] = '?';
      sprintf(xx + 16 * n, "%s", "?.???");
    }
  }
  if (clipboard) {
   memset(clipboard, 0, strlen(clipboard)); 
   sprintf(clipboard, "vec4(%.5s, %.5s, %.5s, 1.0)", xx, xx+16, xx+32); 
  }
  sprintf(info->_text, "INFO/R%c%.5s/G%c%.5s/B%c%.5s", 
    sn[0], xx, sn[1], xx+16, sn[2], xx+32);
  info->_dirty = GL_TRUE;
}


//////////////////////////////////////////////////////////////////

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
  screen_quad = gen_quad(  
    (point_t){-1, 1, 0}, 
    (point_t){1, -1, 0}, 
    scale_ndc((point_t){-1, 1, 0}, width, height), 
    scale_ndc((point_t){1, -1, 0}, width, height));

  pgset.post = create_program(&bypass_vert, &post_frag, NULL);
  
  // ANIMATION BATCH //

  int anim_arg = argument_pos(argc, argv, "-a");
  if (anim_arg > 0) {
    int out_arg = argument_pos(argc, argv, "-o");
    if (out_arg > 0) {
      
      int anim_fps, num_frames;
      float duration;
      sscanf(argv[anim_arg + 1], "%d,%f", &anim_fps, &duration);
      num_frames = floor(anim_fps * duration);
      code_block_t cblock = load_shader_code(argv[shader_path]);
      if (cblock.comp) {
        pgset.comp = create_program(NULL, NULL, (const char**)&(cblock.comp));
      }
      pgset.frag = create_program(&bypass_vert, (const char**)&(cblock.frag), NULL);
      glProgramUniform2i(pgset.frag, 3, width, height);
      
      struct spng_ihdr ihdr = {
        .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA,
        .height = height,
        .width = width,
        .bit_depth = 16,
      };
      
      float delta = duration / (num_frames-1);
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
          
          broadcast_uniform1f(pgset, 0, delta * n);
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
      dispose_code_block(cblock);
      for (int n = 0; n < num_frames; n++) { 
        fclose(out_file[n]);
      }
      printf("... done (%d frames).\n", num_frames);        
    } else {
      __bad("output animation", "use -h for help");
    }
    dispose_program_set(pgset);
    dispose_shape(screen_quad);
    dispose_window(window);
    return 0;
  }

  // INTERACTIVE AND PERSISTENT //
  
  gltInit();
  char picker[64] = {0};
  GLTtext *info = gltCreateText();
  GLTtext *error = gltCreateText();
  gltSetText(info, "INFO/R+0.000/G+0.000/B+0.000");
  gltSetText(error, "COMPILATION:ERROR");
  
  uint64_t timer = 0;
  point_t mouse = {.x = 0.5, .y = 0.5, .z = 0};
  
  bool interactive = argument_pos(argc, argv, "-i") > 0;
  bool finished = false;
  bool always_update = false;
  bool request_update = true;
  bool request_info = false;
  bool request_color = false;
  bool request_error = false;
 
  SDL_Event event;
  stat_t fstat[2] = {0};
       
  while (!finished) {
  
    if(stat(argv[shader_path], &fstat[1]) < 0)
      __bad("read shader file", argv[shader_path]);
    if (fstat[0].st_mtime != fstat[1].st_mtime) {
      code_block_t cblock = load_shader_code(argv[shader_path]);
      if (cblock.comp) {
        GLuint compute = create_program(NULL, NULL, (const char**)&(cblock.comp));
        if (compute) {
          if (pgset.comp) glDeleteProgram(pgset.comp);
          update_compute_buffer(&pgset, cblock.comp_opts.item_size, cblock.comp_opts.num_items);
          pgset.comp = compute;
        }
      }
      GLuint program = create_program(&bypass_vert, (const char**)&(cblock.frag), NULL);
      if (program) {
        if (pgset.frag)  {
          glDeleteProgram(pgset.frag);
        }
        pgset.frag = program;
        glProgramUniform2i(pgset.frag, 3, width, height);
        broadcast_uniform2f(pgset, 1,  mouse.x, mouse.y);
        request_error = false;
        if (!interactive) {
          draw_content(offscr);
          SDL_GL_SwapWindow(window);
        }
      } else {
        request_error = true;
      }
      request_update = true;
      fstat[0] = fstat[1];
      dispose_code_block(cblock);
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
            mouse = scale_ndc((point_t) { ndc_x, -ndc_y }, width, height);
            broadcast_uniform2f(pgset, 1, mouse.x, mouse.y);            
          }
          if (btn_state & SDL_BUTTON(3)) {
            GLuint fb = request_color ? 0 : offscr.fb[0];
            update_info(info, x, height - y, fb, picker);
          }
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
          if (event.button.button == SDL_BUTTON_RIGHT) {
            GLuint fb = request_color ? 0 : offscr.fb[0];
            update_info(info, event.button.x, height - event.button.y, fb, picker);
            request_info = true;
          }
        }
        if (event.type == SDL_MOUSEBUTTONUP) { 
          copy_to_clipboard(picker);
          request_info = false;
          request_color = false;
        }
        if (event.type == SDL_KEYDOWN) {
          int mode = 0;
          switch (event.key.keysym.sym) {
            case SDLK_r : mode = 1; break;
            case SDLK_g : mode = 2; break;
            case SDLK_b : mode = 3; break; 
            case SDLK_a : mode = 4; break;
            case SDLK_i : mode = 5; break;
            case SDLK_c : mode = 6; request_color = true; break;
            case SDLK_t : always_update = !always_update; break;
          }
          glProgramUniform1i(pgset.post, 0, mode);
        }
        if (event.type == SDL_KEYUP) {
          glProgramUniform1i(pgset.post, 0, 0);
        }    
        request_update = true;
      }
      if (interactive && request_update || always_update) {
        broadcast_uniform1f(pgset, 0, (float)timer / 1000);
        draw_content(offscr);
        if (request_info || request_error) {
          gltBeginDraw();
          gltColor(1.0, 1.0, 1.0, 1.0);
          gltDrawText2D(request_error ? error : info, 0, 0, 1);
          gltEndDraw();
        }
        SDL_GL_SwapWindow(window);
      }
      request_update = false;
      SDL_Delay(delay);  
      if (always_update) timer += delay;
    }
  }
  dispose_program_set(pgset);
  dispose_shape(screen_quad);
  dispose_offscreen(offscr);
  dispose_window(window);
  gltTerminate();
  return 0;
}
