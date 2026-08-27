#pragma once

#ifndef __INI_H__
#define __INI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#ifndef INI_HANDLER_LINENO
#define INI_HANDLER_LINENO 0
#endif

#if INI_HANDLER_LINENO
typedef int (*ini_handler)(void* user, const char* section,
                           const char* name, const char* value,
                           int lineno);
#else
typedef int (*ini_handler)(void* user, const char* section,
                           const char* name, const char* value);
#endif

typedef char* (*ini_reader)(char* str, int num, void* stream);

int ini_parse_stream(ini_reader reader, void* stream, ini_handler handler,
                     void* user);

int ini_parse_string(const char* str, ini_handler handler, void* user);

#ifndef INI_ALLOW_BOM
#define INI_ALLOW_BOM 1
#endif

#ifndef INI_ALLOW_INLINE_COMMENTS
#define INI_ALLOW_INLINE_COMMENTS 1
#endif
#ifndef INI_INLINE_COMMENT_PREFIXES
#define INI_INLINE_COMMENT_PREFIXES ";"
#endif

#ifndef INI_USE_STACK
#define INI_USE_STACK 0
#endif

#ifndef INI_MAX_LINE
#define INI_MAX_LINE (1024 * 1024)
#endif

#ifndef INI_ALLOW_REALLOC
#define INI_ALLOW_REALLOC 1
#endif

#ifndef INI_INITIAL_ALLOC
#define INI_INITIAL_ALLOC 200
#endif

#ifndef INI_STOP_ON_FIRST_ERROR
#define INI_STOP_ON_FIRST_ERROR 0
#endif

#ifdef __cplusplus
}
#endif

#endif
