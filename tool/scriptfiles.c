#include <stdlib.h>
#include <string.h>
#include "scriptfiles.h"
#include "utilOp.h"

//assumes your arch is little endian - oh well :)


#define NUM_CPUID_VALS		8

#pragma pack(push,1)
struct MemCheckvalTuple {
	uint32_t addr;
	uint32_t and;
	uint32_t val;
};
#pragma pack(pop)

#pragma pack(push,1)
struct ScriptFooter {
	//tuples here
	uint32_t loadAddr;
	uint32_t stagingAddr;
	uint32_t numCheckvals;
	uint32_t targetid;
	uint32_t cpuidAnd[NUM_CPUID_VALS];
	uint32_t cpuidVal[NUM_CPUID_VALS];
	uint32_t zero;
	//name here
};
#pragma pack(pop)

struct ScriptfilesFindInfo {
	struct PotentialScriptfile *results;
	struct Cpu *cpu;
	const uint32_t *cpuidRegs;
};




static bool tryFile(struct Cpu *cpu, FILE *f, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP)
{
	long len, nameStart, footerStart, matchValsStart;
	struct MemCheckvalTuple ckv;
	struct ScriptFooter ftr;
	uint32_t val;
	unsigned i;
	char c;
	
	
	(void)fseek(f, 0, SEEK_END);
	len = ftell(f);
	
	//find friendly name length
	for (nameStart = len - 1; nameStart >= sizeof(struct ScriptFooter); nameStart--) {
		(void)fseek(f, nameStart, SEEK_SET);
		
		if (1 != fread(&c, 1, 1, f)) {
			fprintf(stderr, "Failed to read name byte\n");
			return false;
		}
		
		if (!c)
			break;
	}
	
	if (c) {
		fprintf(stderr, "File appears broken - name too long\n");
		return false;
	}
	
	nameStart++;
	//nameStart now is offset in file of the first byte of the name (and thus one byte past the end of the "ScriptFooter" struct)
	
	//read footer
	footerStart = nameStart - sizeof(struct ScriptFooter);
	(void)fseek(f, footerStart, SEEK_SET);
	if (sizeof(struct ScriptFooter) != fread(&ftr, 1, sizeof(struct ScriptFooter), f)) {
		fprintf(stderr, "Failed to read footer\n");
		return false;
	}
	
	//sanity check num match vals
	matchValsStart = footerStart - sizeof(struct MemCheckvalTuple) * ftr.numCheckvals;
	if (matchValsStart < 0) {
		fprintf (stderr, "Num matvch vals invalid - fail!\n");
		return false;
	}
	
	//check cpuid and taget id match
	for (i = 0; i < NUM_CPUID_VALS; i++)
		if ((cpuidRegs[i] & ftr.cpuidAnd[i]) != ftr.cpuidVal[i])
			return false;
	
	//if we got this far, cpuid matches - time to vcheck checkvals if they exist
	for (i = 0; i < ftr.numCheckvals; i++) {
		(void)fseek(f, matchValsStart + i * sizeof(struct MemCheckvalTuple), SEEK_SET);
		
		if (sizeof(struct MemCheckvalTuple) != fread(&ckv, 1, sizeof(struct MemCheckvalTuple), f)) {
			fprintf(stderr, "Failed to read checkval %u\n", i);
			return false;
		}
		
		//try it
		if (!cpuMemReadEx(cpu, ckv.addr, 1, &val, true))
			return false;
		
		if ((val & ckv.and) != ckv.val)
			return false;
	}
	
	//it matches!
	*loadSzP = matchValsStart;
	*loadAddrP = ftr.loadAddr;
	*stageAddrP = ftr.stagingAddr;
	*nameP = calloc(1, len - nameStart + 1);
	if (*nameP) {
		(void)fseek(f, nameStart, SEEK_SET);
		if (len - nameStart != fread(*nameP, 1, len - nameStart, f)) {
			
			fprintf(stderr, "Name read failed\n");
			return false;
		}
	}
	
	return true;
}

static bool scriptfilesFindPerFileCbk(void *userData, const char *filepath, const char *name)
{
	struct ScriptfilesFindInfo *fi = (struct ScriptfilesFindInfo*)userData;
	uint32_t loadAddr, loadSz, stageAddr;
	char *cpuName;
	FILE *f;
	
	f = fopen(filepath, "rb");

	if (f) {
		
		//verify basefilename is unique so far
		struct PotentialScriptfile* t;
		
		for (t = fi->results; t && strcmp(t->scriptfileBaseName, name); t = t->next);
		
		//if t is now non-NULL, we alrady have that basename - skipt it now
		
		if (!t && tryFile(fi->cpu, f, fi->cpuidRegs, &loadSz, &loadAddr, &stageAddr, &cpuName)) {
			
			struct PotentialScriptfile* newNode = malloc(sizeof(struct PotentialScriptfile));
			if (newNode) {
				
				newNode->next = fi->results;
				newNode->loadAddr = loadAddr;
				newNode->loadSz = loadSz;
				newNode->stageAddr = stageAddr;
				newNode->cpuName = cpuName;
				newNode->scriptfileBaseName = strdup(name);
				newNode->scriptfilePath = strdup(filepath);
				newNode->scriptfile = f;
			
				if (newNode->scriptfileBaseName && newNode->scriptfilePath) {
					fi->results = newNode;
					return true;
				}
				free(newNode->scriptfileBaseName);
				free(newNode->scriptfilePath);
				free(newNode);
			}
			free(cpuName);
		}
		fclose(f);
	}
	
	return true;
}

//if a file with the same base name is found in multiple paths, we do NOT consider it twice
struct PotentialScriptfile* scriptfilesFind(struct Cpu *cpu, const uint32_t *cpuidRegs)
{
	char *scriptpaths[8] = {0,};
	struct ScriptfilesFindInfo fi = {0,};
	int idx = 0;
	
	fi.cpu = cpu;
	fi.cpuidRegs = cpuidRegs;

	scriptpaths[idx++] = "SCRIPTS";
	if (getenv("CORTEXPROG_SCRIPTS_DIR"))
		scriptpaths[idx++] = getenv("CORTEXPROG_SCRIPTS_DIR");
	scriptpaths[idx++] = "/usr/share/cortexprog/scripts";
	scriptpaths[idx++] = ".";
	
	utilFindFilesInPath((const char* const*)scriptpaths, ".swds", &scriptfilesFindPerFileCbk, &fi);

	return fi.results;
}

