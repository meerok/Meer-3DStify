/* stb_image, JPEG + PNG only, for album covers */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
