#pragma once
typedef  long unsigned int DWORD;
typedef long long int INT64;
typedef void* HANDLE;
typedef unsigned long long UINT64, *PUINT64;
typedef void* PVOID;

#ifndef VOID
typedef void VOID;
#endif
typedef unsigned int UINT32, *PUINT32;
typedef unsigned short UINT16, *PUINT16;
typedef unsigned char UINT8, *PUINT8;
typedef signed int INT32, *PINT32;
typedef unsigned short WCHAR, *PWCHAR;

#define TRUE 1
#define FALSE 0

#define NO_ERROR 0L
#define ERROR_SUCCESS 0

#ifndef NULL
#define NULL ((void *)0)
#endif

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
#define COMPILER_MSVC
#elif defined(__GNUC__)
#define COMPILER_GCC
#elif defined(__clang__)
#define COMPILER_CLANG
#endif

#if defined(COMPILER_MSVC)
	#if defined(_WIN64)
		#define ENVIRONMENT_x86_64
	#else
		#define ENVIRONMENT_I386
	#endif

#elif defined(COMPILER_GCC)
	#if defined(__aarch64__) || defined(_M_ARM64)
		#define ENVIRONMENT_ARM64
	#elif defined(__arm__) || defined(_M_ARM)
		#define ENVIRONMENT_ARM32
	#elif defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
		#define ENVIRONMENT_x86_64
	#elif defined(__i386__) || defined(_M_IX86)
		#define ENVIRONMENT_I386
	#else
		#error Unsupported architecture
	#endif

#elif defined(COMPILER_CLANG)
	#if defined(__aarch64__) || defined(_M_ARM64)
		#define ENVIRONMENT_ARM64
	#elif defined(__arm__) || defined(_M_ARM)
		#define ENVIRONMENT_ARM32
	#elif defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
		#define ENVIRONMENT_x86_64
	#elif defined(__i386__) || defined(_M_IX86)
		#define ENVIRONMENT_I386
	#else
		#error Unsupported architecture
	#endif
 #endif

#if defined(x86) || defined(__i386__) || defined(_M_IX86)
typedef unsigned int USIZE, *PUSIZE;
#else
typedef unsigned long long USIZE, *PUSIZE;
#endif
typedef char CHAR, *PCHAR;
typedef PVOID HINTERNET;
typedef int BOOL;
typedef long NTSTATUS;
typedef long LSTATUS;

#define DECLARE_HANDLE(name) struct name##__{int unused;}; typedef struct name##__ *name

DECLARE_HANDLE(HKEY);

typedef DWORD REGSAM;

#if defined(ENVIRONMENT_I386)
typedef unsigned long ULONG_PTR;
#else
typedef unsigned long long ULONG_PTR;
#endif

typedef ULONG_PTR SIZE_T;

#if defined(COMPILER_MSVC)

	#if defined(ENVIRONMENT_I386)

		#define WINAPI __stdcall
		#define WINAPIV __cdecl

	#else

		#define WINAPI

	#endif

#elif defined(COMPILER_GCC)

	#if defined(ENVIRONMENT_I386)

		#define WINAPI  __stdcall

	#else

		#define WINAPI
		#define WINAPIV

	#endif

#elif defined(COMPILER_CLANG)

	#if defined(ENVIRONMENT_I386)

	#define WINAPI  __attribute__((stdcall))

	#else

		#define WINAPI
		#define WINAPIV

	#endif

#endif

#ifndef WINAPI
#define WINAPI
#endif

typedef CHAR* va_list;

#define _ADDRESSOF(v) (&(v))
#define _INTSIZEOF(n) ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))
#define va_start(v,l)	((v) = (va_list)_ADDRESSOF(l) + _INTSIZEOF(l))
#define va_arg(v,l)	(*(l *)(((v) += _INTSIZEOF(l)) - _INTSIZEOF(l)))
#define va_end(v)		((v) = (va_list)0)
