#ifndef _SCRIPTFILES_H_
#define _SCRIPTFILES_H_


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool scriptfileFind(uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **cpuNameP, char** scptFileName, FILE **scptFileHandle);



#endif
