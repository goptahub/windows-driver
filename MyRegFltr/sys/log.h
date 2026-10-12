#ifndef LOG_H
#define LOG_H

#include "regfltr.h"

//#define LOG_FILE L"C:\\Users\\vldm\\Desktop\\log.txt"
#define LOG_FILE L"\\??\\C:\\Users\\vldm\\Desktop\\log.txt"

NTSTATUS LogOpenFile(VOID);
VOID LogClose(VOID);
int LogPrintf(const wchar_t* format, ...);

#define LOGGING

#if defined(LOGGING)
	#define Log(fmt, ...)	LogPrintf(fmt, ##__VA_ARGS__)
	#define LogStart()		LogOpenFile()
	#define LogStop()		LogClose()
#else
	#define Log(fmt, ...)	((void)0)
	#define LogStart()		((void)0)
	#define LogStop()		((void)0)
#endif


#endif

