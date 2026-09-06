#pragma once

#if defined(_WIN32) && defined(SNOW_IMAGE_SHARED)
#if defined(SNOW_IMAGE_BUILDING_LIBRARY)
#define SNOW_IMAGE_API __declspec(dllexport)
#else
#define SNOW_IMAGE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SNOW_IMAGE_API __attribute__((visibility("default")))
#else
#define SNOW_IMAGE_API
#endif
