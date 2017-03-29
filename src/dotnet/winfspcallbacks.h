#pragma once
#include <winfsp\winfsp.h>
#include "winfsp.h"


class WinFspCallbacks
{
public:    
    static VOID     Cleanup(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, ULONG Flags);
    static VOID     Close(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0);
    static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    static NTSTATUS SetVolumeLabel(FSP_FILE_SYSTEM *FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    static NTSTATUS Create(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS Open(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS Overwrite(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS Read(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
    static NTSTATUS Write(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS Flush(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS SetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    static NTSTATUS CanDelete(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName);
    static NTSTATUS Rename(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
    static NTSTATUS SetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor);
    static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
    static NTSTATUS GetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    // Reparse point
    static NTSTATUS ResolveReparsePoints(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
    static NTSTATUS GetReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, PSIZE_T PSize);
    static NTSTATUS SetReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size);
    static NTSTATUS DeleteReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size);
    static NTSTATUS GetReparsePointByName(FSP_FILE_SYSTEM *FileSystem, PVOID Context, PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize);
    //Streams
    static NTSTATUS GetStreamInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
};

