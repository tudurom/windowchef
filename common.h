#ifndef _COMMON_H
#define _COMMON_H

#ifdef D
#define DMSG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DMSG
#endif

#endif
