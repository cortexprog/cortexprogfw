#ifndef _UTIL_OP_H_
#define _UTIL_OP_H_

#include "script.h"
#include "types.h"

struct UtilProgressState {
	uint64_t startTime;
};

//find the largest flash area in the chip. Ties are settled towards the lowest-address-starting one
bool utilOpFindLargestFlashArea(const struct Script *scpt, uint32_t *baseP, uint32_t *lenP);

//show a progressbar
void utilOpShowProgress(struct UtilProgressState *state, const char *op, uint32_t startAddr, uint32_t length, uint32_t curOfst, bool showSpeed);

//nonblocking: see if stdin has a key and if so, consume it.
bool utilGetKey(void);

//milliseconds form an unknown starting point
uint64_t getTicks(void);

//file finding: paths array ends with NULL; requiredFileNameEnding can be an extension, somethign else, or NULL; callback returns false to stop
typedef bool (*UtilFindFilesInPathCallbackF)(void *userData, const char *path, const char *justName);
void utilFindFilesInPath(const char *const *paths, const char *requiredFileNameEnding, UtilFindFilesInPathCallbackF cbk, void *userData);



#endif
