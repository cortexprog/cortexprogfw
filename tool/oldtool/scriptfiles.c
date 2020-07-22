#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "scriptfiles.h"
#include "cpu.h"

//assumes your arch is little endian - oh well :)


#define NUM_CPUID_VALS		8

struct MemCheckvalTuple {
	uint32_t addr;
	uint32_t and;
	uint32_t val;
} __attribute__((packed));

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
} __attribute__((packed));





static bool tryFile(FILE *f, uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP)
{
	long i, len, nameStart, footerStart, matchValsStart;
	struct MemCheckvalTuple ckv;
	struct ScriptFooter ftr;
	uint32_t val;
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
	if (targetid != ftr.targetid)
		return false;
	
	//if we got this far, cpuid matches - time to vcheck checkvals if they exist
	for (i = 0; i < ftr.numCheckvals; i++) {
		(void)fseek(f, matchValsStart + i * sizeof(struct MemCheckvalTuple), SEEK_SET);
		
		if (sizeof(struct MemCheckvalTuple) != fread(&ckv, 1, sizeof(struct MemCheckvalTuple), f)) {
			fprintf(stderr, "Failed to read checkval %lu\n", i);
			return false;
		}
		
		//try it
		if (!cpuMemReadEx(ckv.addr, 1, &val, true))
			return false;
		
		if ((val & ckv.and) != ckv.val)
			return false;
	}
	
	//it matches!
	if (loadSzP)
		*loadSzP = matchValsStart;
	if (loadAddrP)
		*loadAddrP = ftr.loadAddr;
	if (stageAddrP)
		*stageAddrP = ftr.stagingAddr;
	if (nameP) {
		*nameP = calloc(1, len - nameStart + 1);
		if (*nameP) {
			(void)fseek(f, nameStart, SEEK_SET);
			if (len - nameStart != fread(*nameP, 1, len - nameStart, f)) {
				
				fprintf(stderr, "Name read failed\n");
				return false;
			}
		}
	}
	
	return true;
}

static bool tryFileWithPathAndName(const char *dir, const char *name, char separator, uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
{
	char *filepath;
	FILE * f;
	
	filepath = malloc(strlen(dir) + 2 + strlen(name));
	sprintf(filepath, "%s%c%s", dir, separator, name);
	f = fopen(filepath, "rb");

	if (f) {
		
		if (tryFile(f, targetid, cpuidRegs, loadSzP, loadAddrP, stageAddrP, nameP)) {
			if (scptFileName)
				*scptFileName = filepath;
			
			if (scptFileHandle)
				*scptFileHandle = f;
			else
				fclose(f);
			
			return true;
		}
		
		fclose(f);
	}

	free(filepath);
	return false;
}

#ifdef WIN32
	bool scriptfileFindEx(const char *scriptpath, uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
	{
		char *end = "\\*.swds", *str = malloc(strlen(scriptpath) + strlen(end) + 1), *filepath;
		WIN32_FIND_DATA fdFile;
		HANDLE hFind = NULL;
		bool ret = false;
		
		sprintf(str, "%s%s", scriptpath, end);
		
		if((hFind = FindFirstFile(str, &fdFile)) == INVALID_HANDLE_VALUE)
			return NLUL;
	    
		do {
			
			//check for directory (just because we can)
			if(fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			
			//try it
			if (tryFileWithPathAndName(scriptpath, fdFile.cFileName, '\\', targetid, cpuidRegs, loadSzP, loadAddrP, stageAddrP, nameP, scptFileName, scptFileHandle))) {
				ret = true;
				break;
			}
		
		} while (FindNextFile(hFind, &fdFile));

		FindClose(hFind);
		
		return ret;
	}
#else
	bool scriptfileFindEx(const char *scriptpath, uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
	{
		char *end = ".swds", *filepath;
		struct dirent *de;
		bool ret = false;
		FILE *f = NULL;
		DIR *dir;

		if (!scriptpath)
			scriptpath = ".";
		dir = opendir(scriptpath);
		if (!dir)
			return false;
		
		while ((de = readdir(dir)) != NULL) {
			
			//verify extension
			if (strlen(de->d_name) < strlen(end) || strcmp(de->d_name + strlen(de->d_name) - strlen(end), end))
				continue;
			
			//try it
			if (tryFileWithPathAndName(scriptpath, de->d_name, '/', targetid, cpuidRegs, loadSzP, loadAddrP, stageAddrP, nameP, scptFileName, scptFileHandle)) {
				ret = true;
				break;
			}
		}
		
		closedir(dir);
		
		return ret;
	}

#endif

bool scriptfileFind(uint32_t targetid, const uint32_t *cpuidRegs, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
{
	char * scriptpaths[] = {"SCRIPTS", getenv("CORTEXPROG_SCRIPTS_DIR"), "/usr/share/cortexprog/scripts", "."};
	bool ret;
	int i;
	
	for (i = 0; i < sizeof(scriptpaths) / sizeof(*scriptpaths) && !ret; i++)
		ret = scriptfileFindEx(scriptpaths[i], targetid, cpuidRegs, loadSzP, loadAddrP, stageAddrP, nameP, scptFileName, scptFileHandle);
	
	return ret;
}

