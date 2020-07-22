#ifndef _SCRIPTFILES_H_
#define _SCRIPTFILES_H_

#include <stdio.h>
#include "types.h"
#include "cpu.h"

//this and all its children to be freed by the caller
struct PotentialScriptfile {
	
	struct PotentialScriptfile *next;
	uint32_t loadAddr;
	uint32_t loadSz;
	uint32_t stageAddr;
	char *cpuName;				//user-visible cpu name
	char *scriptfileBaseName;	//file name without path
	char *scriptfilePath;		//full path
	FILE *scriptfile;			//actually open FILE handle
};

struct PotentialScriptfile* scriptfilesFind(struct Cpu *cpu, const uint32_t *cpuidRegs);



#endif
