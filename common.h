/* See the LICENSE file for copyright and license details. */

#ifndef _COMMON_H
#define _COMMON_H

#ifdef D
#define DMSG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DMSG(fmt, ...)
#endif

#endif
