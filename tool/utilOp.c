#ifdef WIN32
#include <windows.h>
#define STDIN_FILENO	0
#else
#include <dirent.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "utilOp.h"


bool utilOpFindLargestFlashArea(const struct Script *scpt, uint32_t *baseP, uint32_t *lenP)
{
	uint32_t i, minIdx = 0, curLen, minLen;
	
	if (!scriptGetNthContiguousFlashAreaInfo(scpt, 0, NULL, &minLen, NULL))
		return false;
	
	for (i = 1; scriptGetNthContiguousFlashAreaInfo(scpt, i, NULL, &curLen, NULL); i++) {
		
		if (curLen > minLen) {
			
			minIdx = i;
			minLen = curLen;
		}
	}

	return scriptGetNthContiguousFlashAreaInfo(scpt, minIdx, baseP, lenP, NULL);
}

void utilOpShowProgress(struct UtilProgressState *state, const char *op, uint32_t startAddr, uint32_t length, uint32_t curOfst, bool showSpeed)
{
	int i, percent;
	
	if (!state->startTime) {
		state->startTime = getTicks();
		showSpeed = false;
	}
	
	percent = (int)(((uint64_t)curOfst * 100 + length / 2) / length);

	if (percent > 100)
		percent = 100;
	
	fprintf(stderr, "%9s %08X..%08X..%08X %3u%% [", op, startAddr, curOfst >= length ? startAddr + length - 1 : startAddr + curOfst, startAddr + length - 1, percent);
	for (i = 0; i < percent / 5; i++)
		fputc('=', stderr);
	for (;i < 20; i++)
		fputc(' ', stderr);
	fprintf(stderr, "]");
	if (showSpeed) {
		uint64_t timePassed = getTicks() - state->startTime;
		unsigned long long speed;
		
		if (!timePassed)
			timePassed = 1;
		
		speed = (1000ULL * curOfst + timePassed / 2) / timePassed;		//bytes per second
		fprintf(stderr, " %3llu.%03llu KB/s", speed >> 10, (speed & 1023) * 1000 / 1024);
	}
	fprintf(stderr, "\r");
	if (curOfst >= length)
		fprintf(stderr,"\n");
}

bool utilGetKey(void)
{
	struct timeval timeout;
	fd_set readfds;
	
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	
	
	if (select(1, &readfds, NULL, NULL, &timeout)) {
		getchar();
		return true;
	}

	return false;
}

uint64_t getTicks(void)
{
#ifdef WIN32
	FILETIME t;
	uint64_t ret;

	GetSystemTimeAsFileTime(&t);
	ret = t.dwHighDateTime;
	ret <<= 32;
	ret += t.dwLowDateTime;

	return ret / 10000;	//convert to milliseconds

#else
	struct timeval tv;
	if(gettimeofday(&tv, NULL))
		return 0;
	return (unsigned long)((tv.tv_sec * 1000ul) + (tv.tv_usec / 1000ul));
#endif
}

static bool utilFindFilesInDirTryOneFile(const char *dir, char separator, const char *file, UtilFindFilesInPathCallbackF cbk, void *userData)
{
	char *path = malloc(strlen(dir) + strlen(file) + 2);
	bool ret;
	
	if (!path)
		return true;
	
	sprintf(path, "%s%c%s", dir, separator, file);
	ret = cbk(userData, path, file);
	free(path);

	return ret;
}

#ifdef WIN32
	//return false if user requested stop
	static bool utilFindFilesInDir(const char *dir, const char *requiredFileNameEnding, UtilFindFilesInPathCallbackF cbk, void *userData)
	{
		char *end = "\\*", *str = malloc(strlen(dir) + strlen(end) + strlen(requiredFileNameEnding) + 1);
		WIN32_FIND_DATAA fdFile;
		HANDLE hFind = NULL;
		bool ret = true;
		
		if (!str)
			goto out;
		
		sprintf(str, "%s%s%s", dir, end, requiredFileNameEnding);
		
		if((hFind = FindFirstFileA(str, &fdFile)) == INVALID_HANDLE_VALUE)
			goto out;
	    
		do {
			//check for directory (just because we can)
			if(fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			
			ret = utilFindFilesInDirTryOneFile(dir, '\\', fdFile.cFileName, cbk, userData);
		
		} while (ret && FindNextFileA(hFind, &fdFile));

		FindClose(hFind);
		
	out:
		free(str);
		return ret;
	}
#else
	//return false if user requested stop
	static bool utilFindFilesInDir(const char *dir, const char *requiredFileNameEnding, UtilFindFilesInPathCallbackF cbk, void *userData)
	{
		struct dirent *de;
		bool ret = true;
		DIR *d;

		d = opendir(dir);
		if (!d)
			return true;
		
		while ((de = readdir(d)) != NULL) {
			
			//name long enoughto include ending?
			if (strlen(de->d_name) < strlen(requiredFileNameEnding))
				continue;
			
			//verify file ending
			if (strcmp(de->d_name + strlen(de->d_name) - strlen(requiredFileNameEnding), requiredFileNameEnding))
				continue;
			
			ret = utilFindFilesInDirTryOneFile(dir, '/', de->d_name, cbk, userData);
			
			if (!ret)
				break;
		}
		
		closedir(d);
		
		return ret;
	}

#endif

void utilFindFilesInPath(const char *const *paths, const char *requiredFileNameEnding, UtilFindFilesInPathCallbackF cbk, void *userData)
{
	const char *path;
	
	//we need a real non-null string
	if (!requiredFileNameEnding)
		requiredFileNameEnding = "";
	
	while((path = *paths++)) {
		
		if (!utilFindFilesInDir(path, requiredFileNameEnding, cbk, userData))
			break;
	}
}