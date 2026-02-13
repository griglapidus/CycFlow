// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYCLIB_GLOBAL_H
#define CYCLIB_GLOBAL_H

/**
 * @file CycLib_global.h
 * @brief Global definitions and export macros for the CycLib library.
 */

#if defined(_MSC_VER) || defined(WIN64) || defined(_WIN64) || defined(__WIN64__) || defined(WIN32) \
|| defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define Q_DECL_EXPORT __declspec(dllexport)
#define Q_DECL_IMPORT __declspec(dllimport)
#else
#define Q_DECL_EXPORT __attribute__((visibility("default")))
#define Q_DECL_IMPORT __attribute__((visibility("default")))
#endif

#if defined(CYCLIB_STATIC)
#  define CYCLIB_EXPORT  ///< Macro is empty for static builds
#else
#  if defined(CYCLIB_LIBRARY)
#    define CYCLIB_EXPORT __declspec(dllexport)
#  else
#    define CYCLIB_EXPORT __declspec(dllimport)
#  endif
#endif

#endif // CYCLIB_GLOBAL_H
