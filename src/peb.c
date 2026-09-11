#include "peb.h"
#include "types.h"
#include "djb2.h"

PPEB GetCurrentPEB(void)
{
	PPEB peb;
#if defined(PLATFORM_WINDOWS_X86_64) || defined(_M_X64) || defined(__x86_64__)
	__asm__("movq %%gs:%1, %0" : "=r"(peb) : "m"(*(PUINT64)(0x60)));

#elif defined(PLATFORM_WINDOWS_I386) || defined(_M_IX86) || defined(__i386__)
	__asm__("movl %%fs:%1, %0" : "=r"(peb) : "m"(*(PUINT32)(0x30)));

#elif defined(PLATFORM_WINDOWS_ARMV7A) || defined(_M_ARM) || defined(__arm__)
	__asm__("ldr %0, [r9, %1]" : "=r"(peb) : "i"(0x30));

#elif defined(PLATFORM_WINDOWS_AARCH64) || defined(_M_ARM64) || defined(__aarch64__)
	__asm__("ldr %0, [x18, #%1]" : "=r"(peb) : "i"(0x60));
#else
	#error "Unsupported platform"
#endif

	return peb;
}

PVOID GetModuleHandleFromPEB(UINT64 moduleNameHash)
{
	PPEB peb = GetCurrentPEB();
	PLIST_ENTRY list = &peb->LoaderData->InMemoryOrderModuleList;
	PLIST_ENTRY entry = list->Flink;

	while (entry != list)
	{
		PLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderModuleList);

		if (module->BaseDllName.Buffer != NULL && Hash(module->BaseDllName.Buffer) == moduleNameHash)
			return module->DllBase;

		entry = entry->Flink;
	}

	return NULL;
}
