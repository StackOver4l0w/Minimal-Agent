#pragma once
#include "types.h"
#include "wintypes.h"

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type *)((PCHAR)(address) - (USIZE)(&((type *)0)->field)))
#endif

typedef struct _LIST_ENTRY
{
	struct _LIST_ENTRY *Flink;
	struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _LDR_DATA_TABLE_ENTRY
{
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
	PVOID DllBase;
	PVOID EntryPoint;
	UINT32 SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _RTL_USER_PROCESS_PARAMETERS
{
	UINT32 MaximumLength;
	UINT32 Length;
	UINT32 Flags;
	UINT32 DebugFlags;
	PVOID ConsoleHandle;
	UINT32 ConsoleFlags;
	PVOID StandardInput;
	PVOID StandardOutput;
	PVOID StandardError;
	UNICODE_STRING CurrentDirectory_DosPath;
	PVOID CurrentDirectory_Handle;
	UNICODE_STRING DllPath;
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
	PWCHAR Environment;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB_LDR_DATA
{
	UINT32 Length;
	UINT32 Initialized;
	PVOID SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _PEB
{
	UINT8 InheritedAddressSpace;
	UINT8 ReadImageFileExecOptions;
	UINT8 BeingDebugged;
	UINT8 Spare;
	PVOID Mutant;
	PVOID ImageBase;
	PPEB_LDR_DATA LoaderData;
	PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
} PEB, *PPEB;

PPEB GetCurrentPEB(void);
PVOID GetModuleHandleFromPEB(UINT64 moduleNameHash);
