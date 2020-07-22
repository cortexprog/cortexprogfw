#ifndef _UTIL_H_
#define _UTIL_H_

#ifndef WIN32
#include <stdint.h>
#endif

#ifdef D_CPU_SUPPORTS_UNALIGNED_ACCESS_

	typedef uint32_t unaligned_uint32_t;
	typedef uint16_t unaligned_uint16_t;
	
	#define WRAP_UNALIGNED_POINTER_32(_ptr)	(_ptr)
	#define WRAP_UNALIGNED_POINTER_16(_ptr)	(_ptr)
	
	#define UNALIGNED(_wrapped)				(*(_wrapped))

#else
	
	#pragma pack(push,1)
	struct UtilContainer32 {
			uint32_t val;
	};
	#pragma pack(pop)
	
	#pragma pack(push,1)
	struct UtilContainer16 {
			uint16_t val;
	};
	#pragma pack(pop)
	
	typedef struct UtilContainer32 unaligned_uint32_t;
	typedef struct UtilContainer16 unaligned_uint16_t;
	
	#define WRAP_UNALIGNED_POINTER_32(_ptr)	((unaligned_uint32_t*)(_ptr))
	#define WRAP_UNALIGNED_POINTER_16(_ptr)	((unaligned_uint16_t*)(_ptr))
	
	#define UNALIGNED(_wrapped)				((_wrapped)->val)
	
#endif

#define UNALIGNED_32(_ptr)				UNALIGNED(WRAP_UNALIGNED_POINTER_32(_ptr))
#define UNALIGNED_16(_ptr)				UNALIGNED(WRAP_UNALIGNED_POINTER_16(_ptr))

#endif
