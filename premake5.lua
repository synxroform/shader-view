workspace "shader-view"
  configurations {"Debug", "Release"}
  location "build"

filter "language:C"
  includedirs{".", "include"}
  buildoptions {"-Wfatal-errors"}
  filter "configurations:Debug"
    symbols "On"
    buildoptions {"-ggdb3"}

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
  links {"SDL2", "glew32", "opengl32", "spng", "miniz"}
  targetdir "."
