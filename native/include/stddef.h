#ifndef _MIN_STDDEF_H
#define _MIN_STDDEF_H
#ifdef __LP64__
typedef unsigned long size_t;
typedef long ptrdiff_t;
#else
typedef unsigned int size_t;
typedef int ptrdiff_t;
#endif
#define NULL ((void*)0)
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
