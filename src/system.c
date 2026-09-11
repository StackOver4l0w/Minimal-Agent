#include "system.h"
#include "djb2.h"
#include "apihash.h"
#include "string.h"
#include "djb2.h"

PVOID ResolveExportByName(PVOID moduleBase, const CHAR *exportName)
{
    if (moduleBase == NULL || exportName == NULL)
        return NULL;

    PUINT8 base = (PUINT8)moduleBase;
    PIMAGE_DOS_HEADER_MIN dos = (PIMAGE_DOS_HEADER_MIN)base;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE_MIN)
        return NULL;

    PUINT8 nt = base + dos->e_lfanew;
    if (*(PUINT32)nt != IMAGE_NT_SIGNATURE_MIN)
        return NULL;

    PUINT8 optionalHeader = nt + 24;
    UINT16 optionalMagic = *(PUINT16)optionalHeader;

    UINT32 exportRva = 0;
    UINT32 exportSize = 0;
    if (optionalMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC_MIN) {
        exportRva = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_32 +
                               (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8));
        exportSize = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_32 +
                                (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8) + 4);
    } else if (optionalMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC_MIN) {
        exportRva = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_64 +
                               (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8));
        exportSize = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_64 +
                                (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8) + 4);
    } else {
        return NULL;
    }

    if (exportRva == 0)
        return NULL;

    PIMAGE_EXPORT_DIRECTORY_MIN exportDir =
        (PIMAGE_EXPORT_DIRECTORY_MIN)(base + exportRva);

    PUINT32 names = (PUINT32)(base + exportDir->AddressOfNames);
    PUINT16 ordinals = (PUINT16)(base + exportDir->AddressOfNameOrdinals);
    PUINT32 functions = (PUINT32)(base + exportDir->AddressOfFunctions);

    for (UINT32 i = 0; i < exportDir->NumberOfNames; i++) {
        const CHAR *name = (const CHAR *)(base + names[i]);
        if (!AsciiEquals(name, exportName))
            continue;

        UINT16 ordinal = ordinals[i];
        if (ordinal >= exportDir->NumberOfFunctions)
            return NULL;

        UINT32 functionRva = functions[ordinal];

        if (functionRva >= exportRva && functionRva < exportRva + exportSize)
            return NULL;

        return (PVOID)(base + functionRva);
    }

    return NULL;
}

PVOID ResolveExportByHash(PVOID moduleBase, UINT64 exportHash)
{
    if (moduleBase == NULL)
        return NULL;

    PUINT8 base = (PUINT8)moduleBase;
    PIMAGE_DOS_HEADER_MIN dos = (PIMAGE_DOS_HEADER_MIN)base;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE_MIN)
        return NULL;

    PUINT8 nt = base + dos->e_lfanew;
    if (*(PUINT32)nt != IMAGE_NT_SIGNATURE_MIN)
        return NULL;

    PUINT8 optionalHeader = nt + 24;
    UINT16 optionalMagic = *(PUINT16)optionalHeader;

    UINT32 exportRva = 0;
    UINT32 exportSize = 0;
    if (optionalMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC_MIN) {
        exportRva = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_32 +
                               (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8));
        exportSize = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_32 +
                                (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8) + 4);
    } else if (optionalMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC_MIN) {
        exportRva = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_64 +
                               (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8));
        exportSize = *(PUINT32)(optionalHeader + IMAGE_EXPORT_DIRECTORY_RVA_64 +
                                (IMAGE_DIRECTORY_ENTRY_EXPORT_MIN * 8) + 4);
    } else {
        return NULL;
    }

    if (exportRva == 0)
        return NULL;

    PIMAGE_EXPORT_DIRECTORY_MIN exportDir =
        (PIMAGE_EXPORT_DIRECTORY_MIN)(base + exportRva);

    PUINT32 names = (PUINT32)(base + exportDir->AddressOfNames);
    PUINT16 ordinals = (PUINT16)(base + exportDir->AddressOfNameOrdinals);
    PUINT32 functions = (PUINT32)(base + exportDir->AddressOfFunctions);

    for (UINT32 i = 0; i < exportDir->NumberOfNames; i++) {
        const CHAR *name = (const CHAR *)(base + names[i]);
        if (HashAscii(name) != exportHash)
            continue;

        UINT16 ordinal = ordinals[i];
        if (ordinal >= exportDir->NumberOfFunctions)
            return NULL;

        UINT32 functionRva = functions[ordinal];

        if (functionRva >= exportRva && functionRva < exportRva + exportSize)
            return NULL;

        return (PVOID)(base + functionRva);
    }

    return NULL;
}

PVOID ResolveFromModuleByHash(UINT64 moduleNameHash, UINT64 exportHash)
{
    PVOID moduleBase = GetModuleHandleFromPEB(moduleNameHash);
    return ResolveExportByHash(moduleBase, exportHash);
}
