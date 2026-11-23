# FreeType is built from third_party/freetype (add_subdirectory in the top CMakeLists); RmlUi's
# find_package(Freetype) resolves to that target.
if(TARGET freetype)
  set(Freetype_FOUND TRUE)
  set(FREETYPE_FOUND TRUE)
  set(FREETYPE_VERSION_STRING "2.14.3")
  get_target_property(_ft_inc freetype INTERFACE_INCLUDE_DIRECTORIES)
  set(FREETYPE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/freetype/include")
  set(FREETYPE_LIBRARIES freetype)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()
endif()
