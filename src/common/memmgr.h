/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2012-2021 Hercules Dev Team
 * Copyright (C) Athena Dev Teams
 *
 * Hercules is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef COMMON_MEMMGR_H
#define COMMON_MEMMGR_H

#include "common/hercules.h"

#define ALC_MARK __FILE__, __LINE__, __func__


// default use of the built-in memory manager
#if !defined(NO_MEMMGR) && !defined(USE_MEMMGR)
#if defined(MEMWATCH) || defined(DMALLOC) || defined(GCOLLECT)
// disable built-in memory manager when using another memory library
#define NO_MEMMGR
#else
// use built-in memory manager by default
#define USE_MEMMGR
#endif
#endif


//////////////////////////////////////////////////////////////////////
// Enable memory manager logging by default
#define LOG_MEMMGR

#define aMalloc(n)    (malloc_proxy((n), ALC_MARK))
#define aCalloc(m, n) (calloc_proxy((m), (n),ALC_MARK))
#define aFree(p)      (free_proxy((p), ALC_MARK))
#define aStrdup(p)    (strdup_proxy((p),ALC_MARK))
#define aStrndup(p,n) (strndup_proxy((p),(n),ALC_MARK))
#define aRealloc(p,n) (iMalloc->realloc((p),(n),ALC_MARK))
#define aReallocz(p,n) (iMalloc->reallocz((p),(n),ALC_MARK))

/////////////// Buffer Creation /////////////////
// Full credit for this goes to Shinomori [Ajarn]

#ifdef __GNUC__ // GCC has variable length arrays

#define CREATE_BUFFER(name, type, size) type name[size]
#define DELETE_BUFFER(name) (void)0

#else // others don't, so we emulate them

#define CREATE_BUFFER(name, type, size) type *name = (type *) aCalloc((size), sizeof(type))
#define DELETE_BUFFER(name) aFree(name)

#endif

////////////// Others //////////////////////////
// should be merged with any of above later
#define CREATE(result, type, number) ((result) = (type *) aCalloc((number), sizeof(type)))
#define RECREATE(result, type, number) ((result) = (type *) aReallocz((result), sizeof(type) * (number)))

////////////////////////////////////////////////

struct malloc_interface {
	void (*init) (void);
	void (*final) (void);
	/* */
	void* (*malloc)(size_t size, const char *file, int line, const char *func) __attribute__ ((alloc_size (1)));
	void* (*calloc)(size_t num, size_t size, const char *file, int line, const char *func) __attribute__ ((alloc_size (1, 2)));
	void* (*realloc)(void *p, size_t size, const char *file, int line, const char *func) __attribute__ ((alloc_size (2))) __attribute__((nonnull (1)));
	void* (*reallocz)(void *p, size_t size, const char *file, int line, const char *func) __attribute__ ((alloc_size (2)));
	char* (*astrdup)(const char *p, const char *file, int line, const char *func) __attribute__((nonnull (1)));
	char *(*astrndup)(const char *p, size_t size, const char *file, int line, const char *func) __attribute__((nonnull (1)));
	void  (*free)(void *p, const char *file, int line, const char *func);
	/* */
	void (*memory_check)(void);
	bool (*verify_ptr)(void* ptr);
	size_t (*usage) (void);
	/* */
	void (*post_shutdown) (void);
	void (*init_messages) (void);
};

void free_proxy(void *p, const char *file, int line, const char *func);
void *malloc_proxy(size_t size, const char *file, int line, const char *func) GCCATTR ((malloc, malloc (free_proxy, 1))) __attribute__ ((alloc_size (1)));
void *calloc_proxy(size_t num, size_t size, const char *file, int line, const char *func) GCCATTR ((malloc, malloc (free_proxy, 1))) __attribute__ ((alloc_size (1, 2)));
char *strdup_proxy(const char *p, const char *file, int line, const char *func) GCCATTR ((malloc, malloc (free_proxy, 1))) __attribute__((nonnull (1)));
char *strndup_proxy(const char *p, size_t size, const char *file, int line, const char *func) GCCATTR ((malloc, malloc (free_proxy, 1))) __attribute__((nonnull (1)));

#ifdef HERCULES_CORE

void malloc_defaults(void);

void memmgr_report(int extra);

HPShared struct malloc_interface *iMalloc;
#else

#ifndef iMalloc
#define iMalloc HPMi->memmgr
#endif  // iMalloc
// include allocation proxy functions
#include "common/memmgr_inc.h"
#endif // HERCULES_CORE

#endif /* COMMON_MEMMGR_H */
