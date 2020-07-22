#ifndef _MY_TYPES_H_
#define _MY_TYPES_H_


#ifdef WIN32
	#include <windows.h>

	typedef unsigned char bool;
	#define false 0
	#define true 1
	
	typedef unsigned long long uint64_t;
	typedef signed long long int64_t;
	typedef unsigned int uint32_t;
	typedef signed int int32_t;
	typedef unsigned short uint16_t;
	typedef signed short int16_t;
	typedef unsigned char uint8_t;
	typedef signed char int8_t;
	#define inline
	
#else
	#include <stdint.h>
	#include <stdbool.h>
#endif

#endif