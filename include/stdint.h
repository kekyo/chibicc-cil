#ifndef __STDINT_H
#define __STDINT_H

typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef __INTPTR_TYPE__ intptr_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#define INT8_MIN   __INT8_MIN__
#define INT16_MIN  __INT16_MIN__
#define INT32_MIN  __INT32_MIN__
#define INT64_MIN  __INT64_MIN__
#define INTPTR_MIN  __INTPTR_MIN__

#define INT8_MAX   __INT8_MAX__
#define INT16_MAX  __INT16_MAX__
#define INT32_MAX  __INT32_MAX__
#define INT64_MAX  __INT64_MAX__
#define INTPTR_MAX  __INTPTR_MAX__

#define UINT8_MAX  __UINT8_MAX__
#define UINT16_MAX __UINT16_MAX__
#define UINT32_MAX __UINT32MAX__
#define UINT64_MAX __UINT64_MAX__
#define UINTPTR_MAX  __UINTPTR_MAX__

#endif
