#pragma once

#if defined(_WIN32) && defined(msg801_EXPORTS)
#  define MSG801_API __declspec(dllexport)
#elif defined(_WIN32)
#  define MSG801_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#  define MSG801_API __attribute__((visibility("default")))
#else
#  define MSG801_API
#endif
