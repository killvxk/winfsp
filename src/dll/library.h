/**
 * @file dll/library.h
 *
 * @copyright 2015-2017 Bill Zissimopoulos
 */
/*
 * This file is part of WinFsp.
 *
 * You can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 3 as published by the Free Software
 * Foundation.
 *
 * Licensees holding a valid commercial license may use this file in
 * accordance with the commercial license agreement provided with the
 * software.
 */

#ifndef WINFSP_DLL_LIBRARY_H_INCLUDED
#define WINFSP_DLL_LIBRARY_H_INCLUDED

#define WINFSP_DLL_INTERNAL
#include <winfsp/winfsp.h>
#include <shared/minimal.h>
#include <strsafe.h>

#define LIBRARY_NAME                    "WinFsp"

/* DEBUGLOG */
#if !defined(NDEBUG)
#define DEBUGLOG(fmt, ...)              \
    FspDebugLog("[U] " LIBRARY_NAME "!" __FUNCTION__ ": " fmt "\n", __VA_ARGS__)
#define DEBUGLOGSD(fmt, SD)             \
    FspDebugLogSD("[U] " LIBRARY_NAME "!" __FUNCTION__ ": " fmt "\n", SD)
#else
#define DEBUGLOG(fmt, ...)              ((void)0)
#define DEBUGLOGSD(fmt, SD)             ((void)0)
#endif

VOID FspPosixFinalize(BOOLEAN Dynamic);
VOID FspEventLogFinalize(BOOLEAN Dynamic);
VOID FspServiceFinalize(BOOLEAN Dynamic);
VOID fsp_fuse_finalize(BOOLEAN Dynamic);
VOID fsp_fuse_finalize_thread(VOID);

NTSTATUS FspFsctlRegister(VOID);
NTSTATUS FspFsctlUnregister(VOID);
NTSTATUS FspNpRegister(VOID);
NTSTATUS FspNpUnregister(VOID);
NTSTATUS FspEventLogRegister(VOID);
NTSTATUS FspEventLogUnregister(VOID);

PWSTR FspDiagIdent(VOID);

VOID FspFileSystemPeekInDirectoryBuffer(PVOID *PDirBuffer,
    PUINT8 *PBuffer, PULONG *PIndex, PULONG PCount);

BOOL WINAPI FspServiceConsoleCtrlHandler(DWORD CtrlType);

static inline ULONG FspPathSuffixIndex(PWSTR FileName)
{
    WCHAR Root[2] = L"\\";
    PWSTR Remain, Suffix;
    ULONG Result;

    FspPathSuffix(FileName, &Remain, &Suffix, Root);
    Result = Remain == Root ? 0 : (ULONG)(Suffix - Remain);
    FspPathCombine(FileName, Suffix);

    return Result;
}

static inline BOOLEAN FspPathIsDrive(PWSTR FileName)
{
    return
        (
            (L'A' <= FileName[0] && FileName[0] <= L'Z') ||
            (L'a' <= FileName[0] && FileName[0] <= L'z')
        ) &&
        L':' == FileName[1] && L'\0' == FileName[2];
}

#endif
