// OpenGL entry points: GLES 3 on Android, glad-loaded GL 3.3 core (RmlUi's loader) on desktop
#pragma once
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <RmlUi_Include_GL3.h>
#endif
