workspace "shader-view"
  configurations {"Debug", "Release"}
  location "build"

project "miniz"
  language "C"
  kind "StaticLib"
  files {"miniz.c", "miniz.h"}

project "spng"
  language "C"
  kind "StaticLib"
  files {"spng.c", "spng.h"}
  defines {"SPNG_USE_MINIZ"}
  
project "shader-view"
  language "C"
  kind "ConsoleApp"
  files {"main.c"}
  includedirs {"include", "."}
  links {"SDL2", "glew32", "opengl32", "spng", "miniz"}
  targetdir "."
