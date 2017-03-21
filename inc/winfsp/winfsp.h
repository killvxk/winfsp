/**
 * @file winfsp/winfsp.h
 * WinFsp User Mode API.
 *
 * In order to use the WinFsp API the user mode file system must include &lt;winfsp/winfsp.h&gt;
 * and link with the winfsp_x64.dll (or winfsp_x86.dll) library.
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

#ifndef WINFSP_WINFSP_H_INCLUDED
#define WINFSP_WINFSP_H_INCLUDED

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#pragma warning(push)
#pragma warning(disable:4005)           /* macro redefinition */
#include <ntstatus.h>
#pragma warning(pop)

#if defined(WINFSP_DLL_INTERNAL)
#define FSP_API                         __declspec(dllexport)
#else
#define FSP_API                         __declspec(dllimport)
#endif

#include <winfsp/fsctl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The REPARSE_DATA_BUFFER definitions appear to be missing from the user mode headers.
 */
#if !defined(SYMLINK_FLAG_RELATIVE)
#define SYMLINK_FLAG_RELATIVE           1
#define REPARSE_DATA_BUFFER_HEADER_SIZE FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)
typedef struct _REPARSE_DATA_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct
        {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

/**
 * @group File System
 *
 * A user mode file system is a program that uses the WinFsp API to expose a file system to
 * Windows. The user mode file system must implement the operations in FSP_FILE_SYSTEM_INTERFACE,
 * create a file system object using FspFileSystemCreate and start its dispatcher using
 * FspFileSystemStartDispatcher. At that point it will start receiving file system requests on the
 * FSP_FILE_SYSTEM_INTERFACE operations.
 */
typedef struct _FSP_FILE_SYSTEM FSP_FILE_SYSTEM;
typedef NTSTATUS FSP_FILE_SYSTEM_OPERATION_GUARD(FSP_FILE_SYSTEM *,
    FSP_FSCTL_TRANSACT_REQ *, FSP_FSCTL_TRANSACT_RSP *);
typedef NTSTATUS FSP_FILE_SYSTEM_OPERATION(FSP_FILE_SYSTEM *,
    FSP_FSCTL_TRANSACT_REQ *, FSP_FSCTL_TRANSACT_RSP *);
/**
 * User mode file system locking strategy.
 *
 * Two concurrency models are provided:
 *
 * 1. A fine-grained concurrency model where file system NAMESPACE accesses
 * are guarded using an exclusive-shared (read-write) lock. File I/O is not
 * guarded and concurrent reads/writes/etc. are possible. [Note that the FSD
 * will still apply an exclusive-shared lock PER INDIVIDUAL FILE, but it will
 * not limit I/O operations for different files.]
 *
 * The fine-grained concurrency model applies the exclusive-shared lock as
 * follows:
 * <ul>
 * <li>EXCL: SetVolumeLabel, Flush(Volume),
 * Create, Cleanup(Delete), SetInformation(Rename)</li>
 * <li>SHRD: GetVolumeInfo, Open, SetInformation(Disposition), ReadDirectory</li>
 * <li>NONE: all other operations</li>
 * </ul>
 *
 * 2. A coarse-grained concurrency model where all file system accesses are
 * guarded by a mutually exclusive lock.
 *
 * @see FspFileSystemSetOperationGuardStrategy
 */
typedef enum
{
    FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY_FINE = 0,
    FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY_COARSE,
} FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY;
enum
{
    FspCleanupDelete                    = 0x01,
    FspCleanupSetAllocationSize         = 0x02,
    FspCleanupSetArchiveBit             = 0x10,
    FspCleanupSetLastAccessTime         = 0x20,
    FspCleanupSetLastWriteTime          = 0x40,
    FspCleanupSetChangeTime             = 0x80,
};
/**
 * @class FSP_FILE_SYSTEM
 * File system interface.
 *
 * The operations in this interface must be implemented by the user mode
 * file system. Not all operations need be implemented. For example,
 * a user mode file system that does not wish to support reparse points,
 * need not implement the reparse point operations.
 *
 * Most of the operations accept a FileContext parameter. This parameter
 * has different meanings depending on the value of the FSP_FSCTL_VOLUME_PARAMS
 * flags UmFileContextIsUserContext2 and UmFileContextIsFullContext.
 *
 * There are three cases to consider:
 * <ul>
 * <li>When both of these flags are unset (default), the FileContext parameter
 * represents the file node. The file node is a void pointer (or an integer
 * that can fit in a pointer) that is used to uniquely identify an open file.
 * Opening the same file name should always yield the same file node value
 * for as long as the file with that name remains open anywhere in the system.
 * </li>
 * <li>When the UmFileContextIsUserContext2 is set, the FileContext parameter
 * represents the file descriptor. The file descriptor is a void pointer (or
 * an integer that can fit in a pointer) that is used to identify an open
 * instance of a file. Opening the same file name may yield a different file
 * descriptor.
 * </li>
 * <li>When the UmFileContextIsFullContext is set, the FileContext parameter
 * is a pointer to a FSP_FSCTL_TRANSACT_FULL_CONTEXT. This allows a user mode
 * file system to access the low-level UserContext and UserContext2 values.
 * The UserContext is used to store the file node and the UserContext2 is
 * used to store the file descriptor for an open file.
 * </li>
 * </ul>
 */
typedef struct _FSP_FILE_SYSTEM_INTERFACE
{
    /**
     * Get volume information.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param VolumeInfo [out]
     *     Pointer to a structure that will receive the volume information on successful return
     *     from this call.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*GetVolumeInfo)(FSP_FILE_SYSTEM *FileSystem,
        FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    /**
     * Set volume label.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param VolumeLabel
     *     The new label for the volume.
     * @param VolumeInfo [out]
     *     Pointer to a structure that will receive the volume information on successful return
     *     from this call.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*SetVolumeLabel)(FSP_FILE_SYSTEM *FileSystem,
        PWSTR VolumeLabel,
        FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    /**
     * Get file or directory attributes and security descriptor given a file name.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileName
     *     The name of the file or directory to get the attributes and security descriptor for.
     * @param PFileAttributes
     *     Pointer to a memory location that will receive the file attributes on successful return
     *     from this call. May be NULL.
     *
     *     If this call returns STATUS_REPARSE, the file system MAY place here the index of the
     *     first reparse point within FileName. The file system MAY also leave this at its default
     *     value of 0.
     * @param SecurityDescriptor
     *     Pointer to a buffer that will receive the file security descriptor on successful return
     *     from this call. May be NULL.
     * @param PSecurityDescriptorSize [in,out]
     *     Pointer to the security descriptor buffer size. On input it contains the size of the
     *     security descriptor buffer. On output it will contain the actual size of the security
     *     descriptor copied into the security descriptor buffer. May be NULL.
     * @return
     *     STATUS_SUCCESS, STATUS_REPARSE or error code.
     *
     *     STATUS_REPARSE should be returned by file systems that support reparse points when
     *     they encounter a FileName that contains reparse points anywhere but the final path
     *     component.
     */
    NTSTATUS (*GetSecurityByName)(FSP_FILE_SYSTEM *FileSystem,
        PWSTR FileName, PUINT32 PFileAttributes/* or ReparsePointIndex */,
        PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    /**
     * Create new file or directory.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileName
     *     The name of the file or directory to be created.
     * @param CreateOptions
     *     Create options for this request. This parameter has the same meaning as the
     *     CreateOptions parameter of the NtCreateFile API. User mode file systems should typically
     *     only be concerned with the flag FILE_DIRECTORY_FILE, which is an instruction to create a
     *     directory rather than a file. Some file systems may also want to pay attention to the
     *     FILE_NO_INTERMEDIATE_BUFFERING and FILE_WRITE_THROUGH flags, although these are
     *     typically handled by the FSD component.
     * @param GrantedAccess
     *     Determines the specific access rights that have been granted for this request. Upon
     *     receiving this call all access checks have been performed and the user mode file system
     *     need not perform any additional checks. However this parameter may be useful to a user
     *     mode file system; for example the WinFsp-FUSE layer uses this parameter to determine
     *     which flags to use in its POSIX open() call.
     * @param FileAttributes
     *     File attributes to apply to the newly created file or directory.
     * @param SecurityDescriptor
     *     Security descriptor to apply to the newly created file or directory. This security
     *     descriptor will always be in self-relative format. Its length can be retrieved using the
     *     Windows GetSecurityDescriptorLength API. Will be NULL for named streams.
     * @param AllocationSize
     *     Allocation size for the newly created file.
     * @param PFileContext [out]
     *     Pointer that will receive the file context on successful return from this call.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*Create)(FSP_FILE_SYSTEM *FileSystem,
        PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
        UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
        PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Open a file or directory.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileName
     *     The name of the file or directory to be opened.
     * @param CreateOptions
     *     Create options for this request. This parameter has the same meaning as the
     *     CreateOptions parameter of the NtCreateFile API. User mode file systems typically
     *     do not need to do anything special with respect to this parameter. Some file systems may
     *     also want to pay attention to the FILE_NO_INTERMEDIATE_BUFFERING and FILE_WRITE_THROUGH
     *     flags, although these are typically handled by the FSD component.
     * @param GrantedAccess
     *     Determines the specific access rights that have been granted for this request. Upon
     *     receiving this call all access checks have been performed and the user mode file system
     *     need not perform any additional checks. However this parameter may be useful to a user
     *     mode file system; for example the WinFsp-FUSE layer uses this parameter to determine
     *     which flags to use in its POSIX open() call.
     * @param PFileContext [out]
     *     Pointer that will receive the file context on successful return from this call.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*Open)(FSP_FILE_SYSTEM *FileSystem,
        PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
        PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Overwrite a file.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file to overwrite.
     * @param FileAttributes
     *     File attributes to apply to the overwritten file.
     * @param ReplaceFileAttributes
     *     When TRUE the existing file attributes should be replaced with the new ones.
     *     When FALSE the existing file attributes should be merged (or'ed) with the new ones.
     * @param AllocationSize
     *     Allocation size for the overwritten file.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*Overwrite)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize,
        FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Cleanup a file.
     *
     * When CreateFile is used to open or create a file the kernel creates a kernel mode file
     * object (type FILE_OBJECT) and a handle for it, which it returns to user-mode. The handle may
     * be duplicated (using DuplicateHandle), but all duplicate handles always refer to the same
     * file object. When all handles for a particular file object get closed (using CloseHandle)
     * the system sends a Cleanup request to the file system.
     *
     * There will be a Cleanup operation for every Create or Open operation posted to the user mode
     * file system. However the Cleanup operation is <b>not</b> the final close operation on a file.
     * The file system must be ready to receive additional operations until close time. This is true
     * even when the file is being deleted!
     *
     * The Flags parameter contains information about the cleanup operation:
     * <ul>
     * <li>FspCleanupDelete -
     * An important function of the Cleanup operation is to complete a delete operation. Deleting
     * a file or directory in Windows is a three-stage process where the file is first opened, then
     * tested to see if the delete can proceed and if the answer is positive the file is then
     * deleted during Cleanup.
     *
     * When this flag is set, this is the last outstanding cleanup for this particular file node.
     * </li>
     * <li>FspCleanupSetAllocationSize -
     * The NTFS and FAT file systems reset a file's allocation size when they receive the last
     * outstanding cleanup for a particular file node. User mode file systems that implement
     * allocation size and wish to duplicate the NTFS and FAT behavior can use this flag.
     * </li>
     * <li>
     * FspCleanupSetArchiveBit -
     * File systems that support the archive bit should set the file node's archive bit when this
     * flag is set.
     * </li>
     * <li>FspCleanupSetLastAccessTime, FspCleanupSetLastWriteTime, FspCleanupSetChangeTime - File
     * systems should set the corresponding file time when each one of these flags is set. Note that
     * updating the last access time is expensive and a file system may choose to not implement it.
     * </ul>
     *
     * There is no way to report failure of this operation. This is a Windows limitation.
     *
     * As an optimization a file system may specify the FSP_FSCTL_VOLUME_PARAMS ::
     * PostCleanupWhenModifiedOnly flag. In this case the FSD will only post Cleanup requests when
     * the file was modified/deleted.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to cleanup.
     * @param FileName
     *     The name of the file or directory to cleanup. Sent only when a Delete is requested.
     * @param Flags
     *     These flags determine whether the file was modified and whether to delete the file.
     * @see
     *     Close
     *     CanDelete
     */
    VOID (*Cleanup)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PWSTR FileName, ULONG Flags);
    /**
     * Close a file.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to be closed.
     */
    VOID (*Close)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext);
    /**
     * Read a file.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file to be read.
     * @param Buffer
     *     Pointer to a buffer that will receive the results of the read operation.
     * @param Offset
     *     Offset within the file to read from.
     * @param Length
     *     Length of data to read.
     * @param PBytesTransferred [out]
     *     Pointer to a memory location that will receive the actual number of bytes read.
     * @return
     *     STATUS_SUCCESS or error code. STATUS_PENDING is supported allowing for asynchronous
     *     operation.
     */
    NTSTATUS (*Read)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
        PULONG PBytesTransferred);
    /**
     * Write a file.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file to be written.
     * @param Buffer
     *     Pointer to a buffer that contains the data to write.
     * @param Offset
     *     Offset within the file to write to.
     * @param Length
     *     Length of data to write.
     * @param WriteToEndOfFile
     *     When TRUE the file system must write to the current end of file. In this case the Offset
     *     parameter will contain the value -1.
     * @param ConstrainedIo
     *     When TRUE the file system must not extend the file (i.e. change the file size).
     * @param PBytesTransferred [out]
     *     Pointer to a memory location that will receive the actual number of bytes written.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code. STATUS_PENDING is supported allowing for asynchronous
     *     operation.
     */
    NTSTATUS (*Write)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
        BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
        PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Flush a file or volume.
     *
     * Note that the FSD will also flush all file/volume caches prior to invoking this operation.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file to be flushed. When NULL the whole volume is being flushed.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc. Used when
     *     flushing file (not volume).
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*Flush)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Get file or directory information.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to get information for.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*GetFileInfo)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Set file or directory basic information.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to set information for.
     * @param FileAttributes
     *     File attributes to apply to the file or directory. If the value INVALID_FILE_ATTRIBUTES
     *     is sent, the file attributes should not be changed.
     * @param CreationTime
     *     Creation time to apply to the file or directory. If the value 0 is sent, the creation
     *     time should not be changed.
     * @param LastAccessTime
     *     Last access time to apply to the file or directory. If the value 0 is sent, the last
     *     access time should not be changed.
     * @param LastWriteTime
     *     Last write time to apply to the file or directory. If the value 0 is sent, the last
     *     write time should not be changed.
     * @param ChangeTime
     *     Change time to apply to the file or directory. If the value 0 is sent, the change time
     *     should not be changed.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*SetBasicInfo)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, UINT32 FileAttributes,
        UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
        FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Set file/allocation size.
     *
     * This function is used to change a file's sizes. Windows file systems maintain two kinds
     * of sizes: the file size is where the End Of File (EOF) is, and the allocation size is the
     * actual size that a file takes up on the "disk".
     *
     * The rules regarding file/allocation size are:
     * <ul>
     * <li>Allocation size must always be aligned to the allocation unit boundary. The allocation
     * unit is the product <code>(UINT64)SectorSize * (UINT64)SectorsPerAllocationUnit</code> from
     * the FSP_FSCTL_VOLUME_PARAMS structure. The FSD will always send properly aligned allocation
     * sizes when setting the allocation size.</li>
     * <li>Allocation size is always greater or equal to the file size.</li>
     * <li>A file size of more than the current allocation size will also extend the allocation
     * size to the next allocation unit boundary.</li>
     * <li>An allocation size of less than the current file size should also truncate the current
     * file size.</li>
     * </ul>
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file to set the file/allocation size for.
     * @param NewSize
     *     New file/allocation size to apply to the file.
     * @param SetAllocationSize
     *     If TRUE, then the allocation size is being set. if FALSE, then the file size is being set.
     * @param FileInfo [out]
     *     Pointer to a structure that will receive the file information on successful return
     *     from this call. This information includes file attributes, file times, etc.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*SetFileSize)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
        FSP_FSCTL_FILE_INFO *FileInfo);
    /**
     * Determine whether a file or directory can be deleted.
     *
     * This function tests whether a file or directory can be safely deleted. This function does
     * not need to perform access checks, but may performs tasks such as check for empty
     * directories, etc.
     *
     * This function should <b>NEVER</b> delete the file or directory in question. Deletion should
     * happen during Cleanup with the FspCleanupDelete flag set.
     *
     * This function gets called when Win32 API's such as DeleteFile or RemoveDirectory are used.
     * It does not get called when a file or directory is opened with FILE_DELETE_ON_CLOSE.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to test for deletion.
     * @param FileName
     *     The name of the file or directory to test for deletion.
     * @return
     *     STATUS_SUCCESS or error code.
     * @see
     *     Cleanup
     */
    NTSTATUS (*CanDelete)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PWSTR FileName);
    /**
     * Renames a file or directory.
     *
     * The kernel mode FSD provides certain guarantees prior to posting a rename operation:
     * <ul>
     * <li>A file cannot be renamed if a file with the same name exists and has open handles.</li>
     * <li>A directory cannot be renamed if it or any of its subdirectories contains a file that
     * has open handles.</li>
     * </ul>
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to be renamed.
     * @param FileName
     *     The current name of the file or directory to rename.
     * @param NewFileName
     *     The new name for the file or directory.
     * @param ReplaceIfExists
     *     Whether to replace a file that already exists at NewFileName.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*Rename)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
    /**
     * Get file or directory security descriptor.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to get the security descriptor for.
     * @param SecurityDescriptor
     *     Pointer to a buffer that will receive the file security descriptor on successful return
     *     from this call. May be NULL.
     * @param PSecurityDescriptorSize [in,out]
     *     Pointer to the security descriptor buffer size. On input it contains the size of the
     *     security descriptor buffer. On output it will contain the actual size of the security
     *     descriptor copied into the security descriptor buffer. Cannot be NULL.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*GetSecurity)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    /**
     * Set file or directory security descriptor.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to set the security descriptor for.
     * @param SecurityInformation
     *     Describes what parts of the file or directory security descriptor should
     *     be modified.
     * @param ModificationDescriptor
     *     Describes the modifications to apply to the file or directory security descriptor.
     * @return
     *     STATUS_SUCCESS or error code.
     * @see
     *     FspSetSecurityDescriptor
     *     FspDeleteSecurityDescriptor
     */
    NTSTATUS (*SetSecurity)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor);
    /**
     * Read a directory.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the directory to be read.
     * @param Pattern
     *     The pattern to match against files in this directory. Can be NULL. The file system
     *     can choose to ignore this parameter as the FSD will always perform its own pattern
     *     matching on the returned results.
     * @param Marker
     *     A file name that marks where in the directory to start reading. Files with names
     *     that are greater than (not equal to) this marker (in the directory order determined
     *     by the file system) should be returned. Can be NULL.
     * @param Buffer
     *     Pointer to a buffer that will receive the results of the read operation.
     * @param Length
     *     Length of data to read.
     * @param PBytesTransferred [out]
     *     Pointer to a memory location that will receive the actual number of bytes read.
     * @return
     *     STATUS_SUCCESS or error code. STATUS_PENDING is supported allowing for asynchronous
     *     operation.
     * @see
     *     FspFileSystemAddDirInfo
     */
    NTSTATUS (*ReadDirectory)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PWSTR Pattern, PWSTR Marker,
        PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
    /**
     * Resolve reparse points.
     *
     * Reparse points are a general mechanism for attaching special behavior to files.
     * A file or directory can contain a reparse point. A reparse point is data that has
     * special meaning to the file system, Windows or user applications. For example, NTFS
     * and Windows use reparse points to implement symbolic links. As another example,
     * a particular file system may use reparse points to emulate UNIX FIFO's.
     *
     * This function is expected to resolve as many reparse points as possible. If a reparse
     * point is encountered that is not understood by the file system further reparse point
     * resolution should stop; the reparse point data should be returned to the FSD with status
     * STATUS_REPARSE/reparse-tag. If a reparse point (symbolic link) is encountered that is
     * understood by the file system but points outside it, the reparse point should be
     * resolved, but further reparse point resolution should stop; the resolved file name
     * should be returned to the FSD with status STATUS_REPARSE/IO_REPARSE.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileName
     *     The name of the file or directory to have its reparse points resolved.
     * @param ReparsePointIndex
     *     The index of the first reparse point within FileName.
     * @param ResolveLastPathComponent
     *     If FALSE, the last path component of FileName should not be resolved, even
     *     if it is a reparse point that can be resolved. If TRUE, all path components
     *     should be resolved if possible.
     * @param PIoStatus
     *     Pointer to storage that will receive the status to return to the FSD. When
     *     this function succeeds it must set PIoStatus->Status to STATUS_REPARSE and
     *     PIoStatus->Information to either IO_REPARSE or the reparse tag.
     * @param Buffer
     *     Pointer to a buffer that will receive the resolved file name (IO_REPARSE) or
     *     reparse data (reparse tag). If the function returns a file name, it should
     *     not be NULL terminated.
     * @param PSize [in,out]
     *     Pointer to the buffer size. On input it contains the size of the buffer.
     *     On output it will contain the actual size of data copied.
     * @return
     *     STATUS_REPARSE or error code.
     */
    NTSTATUS (*ResolveReparsePoints)(FSP_FILE_SYSTEM *FileSystem,
        PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent,
        PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
    /**
     * Get reparse point.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the reparse point.
     * @param FileName
     *     The file name of the reparse point.
     * @param Buffer
     *     Pointer to a buffer that will receive the results of this operation. If
     *     the function returns a symbolic link path, it should not be NULL terminated.
     * @param PSize [in,out]
     *     Pointer to the buffer size. On input it contains the size of the buffer.
     *     On output it will contain the actual size of data copied.
     * @return
     *     STATUS_SUCCESS or error code.
     * @see
     *     SetReparsePoint
     */
    NTSTATUS (*GetReparsePoint)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        PWSTR FileName, PVOID Buffer, PSIZE_T PSize);
    /**
     * Set reparse point.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the reparse point.
     * @param FileName
     *     The file name of the reparse point.
     * @param Buffer
     *     Pointer to a buffer that contains the data for this operation. If this buffer
     *     contains a symbolic link path, it should not be assumed to be NULL terminated.
     * @param Size
     *     Size of data to write.
     * @return
     *     STATUS_SUCCESS or error code.
     * @see
     *     GetReparsePoint
     */
    NTSTATUS (*SetReparsePoint)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        PWSTR FileName, PVOID Buffer, SIZE_T Size);
    /**
     * Delete reparse point.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the reparse point.
     * @param FileName
     *     The file name of the reparse point.
     * @param Buffer
     *     Pointer to a buffer that contains the data for this operation.
     * @param Size
     *     Size of data to write.
     * @return
     *     STATUS_SUCCESS or error code.
     */
    NTSTATUS (*DeleteReparsePoint)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext,
        PWSTR FileName, PVOID Buffer, SIZE_T Size);
    /**
     * Get named streams information.
     *
     * @param FileSystem
     *     The file system on which this request is posted.
     * @param FileContext
     *     The file context of the file or directory to get stream information for.
     * @param Buffer
     *     Pointer to a buffer that will receive the stream information.
     * @param Length
     *     Length of buffer.
     * @param PBytesTransferred [out]
     *     Pointer to a memory location that will receive the actual number of bytes stored.
     * @return
     *     STATUS_SUCCESS or error code.
     * @see
     *     FspFileSystemAddStreamInfo
     */
    NTSTATUS (*GetStreamInfo)(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, PVOID Buffer, ULONG Length,
        PULONG PBytesTransferred);

    /*
     * This ensures that this interface will always contain 64 function pointers.
     * Please update when changing the interface as it is important for future compatibility.
     */
    NTSTATUS (*Reserved[40])();
} FSP_FILE_SYSTEM_INTERFACE;
FSP_FSCTL_STATIC_ASSERT(sizeof(FSP_FILE_SYSTEM_INTERFACE) == 64 * sizeof(NTSTATUS (*)()),
    "FSP_FILE_SYSTEM_INTERFACE must have 64 entries.");
typedef struct _FSP_FILE_SYSTEM
{
    UINT16 Version;
    PVOID UserContext;
    WCHAR VolumeName[FSP_FSCTL_VOLUME_NAME_SIZEMAX / sizeof(WCHAR)];
    HANDLE VolumeHandle;
    FSP_FILE_SYSTEM_OPERATION_GUARD *EnterOperation, *LeaveOperation;
    FSP_FILE_SYSTEM_OPERATION *Operations[FspFsctlTransactKindCount];
    const FSP_FILE_SYSTEM_INTERFACE *Interface;
    HANDLE DispatcherThread;
    ULONG DispatcherThreadCount;
    NTSTATUS DispatcherResult;
    PWSTR MountPoint;
    HANDLE MountHandle;
    UINT32 DebugLog;
    FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY OpGuardStrategy;
    SRWLOCK OpGuardLock;
    BOOLEAN UmFileContextIsUserContext2, UmFileContextIsFullContext;
} FSP_FILE_SYSTEM;
typedef struct _FSP_FILE_SYSTEM_OPERATION_CONTEXT
{
    FSP_FSCTL_TRANSACT_REQ *Request;
    FSP_FSCTL_TRANSACT_RSP *Response;
} FSP_FILE_SYSTEM_OPERATION_CONTEXT;
/**
 * Check whether creating a file system object is possible.
 *
 * @param DevicePath
 *     The name of the control device for this file system. This must be either
 *     FSP_FSCTL_DISK_DEVICE_NAME or FSP_FSCTL_NET_DEVICE_NAME.
 * @param MountPoint
 *     The mount point for the new file system. A value of NULL means that the file system should
 *     use the next available drive letter counting downwards from Z: as its mount point.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspFileSystemPreflight(PWSTR DevicePath,
    PWSTR MountPoint);
/**
 * Create a file system object.
 *
 * @param DevicePath
 *     The name of the control device for this file system. This must be either
 *     FSP_FSCTL_DISK_DEVICE_NAME or FSP_FSCTL_NET_DEVICE_NAME.
 * @param VolumeParams
 *     Volume parameters for the newly created file system.
 * @param Interface
 *     A pointer to the actual operations that actually implement this user mode file system.
 * @param PFileSystem [out]
 *     Pointer that will receive the file system object created on successful return from this
 *     call.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspFileSystemCreate(PWSTR DevicePath,
    const FSP_FSCTL_VOLUME_PARAMS *VolumeParams,
    const FSP_FILE_SYSTEM_INTERFACE *Interface,
    FSP_FILE_SYSTEM **PFileSystem);
/**
 * Delete a file system object.
 *
 * @param FileSystem
 *     The file system object.
 */
FSP_API VOID FspFileSystemDelete(FSP_FILE_SYSTEM *FileSystem);
/**
 * Set the mount point for a file system.
 *
 * This function supports drive letters (X:) or directories as mount points:
 * <ul>
 * <li>Drive letters: Refer to the documentation of the DefineDosDevice Windows API
 * to better understand how they are created.</li>
 * <li>Directories: They can be used as mount points for disk based file systems. They cannot
 * be used for network file systems. This is a limitation that Windows imposes on junctions.</li>
 * </ul>
 *
 * @param FileSystem
 *     The file system object.
 * @param MountPoint
 *     The mount point for the new file system. A value of NULL means that the file system should
 *     use the next available drive letter counting downwards from Z: as its mount point.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspFileSystemSetMountPoint(FSP_FILE_SYSTEM *FileSystem, PWSTR MountPoint);
FSP_API NTSTATUS FspFileSystemSetMountPointEx(FSP_FILE_SYSTEM *FileSystem, PWSTR MountPoint,
    PSECURITY_DESCRIPTOR SecurityDescriptor);
/**
 * Remove the mount point for a file system.
 *
 * @param FileSystem
 *     The file system object.
 */
FSP_API VOID FspFileSystemRemoveMountPoint(FSP_FILE_SYSTEM *FileSystem);
/**
 * Start the file system dispatcher.
 *
 * The file system dispatcher is used to dispatch operations posted by the FSD to the user mode
 * file system. Once this call starts executing the user mode file system will start receiving
 * file system requests from the kernel.
 *
 * @param FileSystem
 *     The file system object.
 * @param ThreadCount
 *     The number of threads for the file system dispatcher. A value of 0 will create a default
 *     number of threads and should be chosen in most cases.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspFileSystemStartDispatcher(FSP_FILE_SYSTEM *FileSystem, ULONG ThreadCount);
/**
 * Stop the file system dispatcher.
 *
 * @param FileSystem
 *     The file system object.
 */
FSP_API VOID FspFileSystemStopDispatcher(FSP_FILE_SYSTEM *FileSystem);
/**
 * Send a response to the FSD.
 *
 * This call is not required when the user mode file system performs synchronous processing of
 * requests. It is possible however for the following FSP_FILE_SYSTEM_INTERFACE operations to be
 * processed asynchronously:
 * <ul>
 * <li>Read</li>
 * <li>Write</li>
 * <li>ReadDirectory</li>
 * </ul>
 *
 * These operations are allowed to return STATUS_PENDING to postpone sending a response to the FSD.
 * At a later time the file system can use FspFileSystemSendResponse to send the response.
 *
 * @param FileSystem
 *     The file system object.
 * @param Response
 *     The response buffer.
 */
FSP_API VOID FspFileSystemSendResponse(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_RSP *Response);
/**
 * Get the current operation context.
 *
 * This function may be used only when servicing one of the FSP_FILE_SYSTEM_INTERFACE operations.
 * The current operation context is stored in thread local storage. It allows access to the
 * Request and Response associated with this operation.
 *
 * @return
 *     The current operation context.
 */
FSP_API FSP_FILE_SYSTEM_OPERATION_CONTEXT *FspFileSystemGetOperationContext(VOID);
static inline
PWSTR FspFileSystemMountPoint(FSP_FILE_SYSTEM *FileSystem)
{
    return FileSystem->MountPoint;
}
static inline
NTSTATUS FspFileSystemEnterOperation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response)
{
    if (0 == FileSystem->EnterOperation)
        return STATUS_SUCCESS;

    return FileSystem->EnterOperation(FileSystem, Request, Response);
}
static inline
NTSTATUS FspFileSystemLeaveOperation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response)
{
    if (0 == FileSystem->LeaveOperation)
        return STATUS_SUCCESS;

    return FileSystem->LeaveOperation(FileSystem, Request, Response);
}
static inline
VOID FspFileSystemSetOperationGuard(FSP_FILE_SYSTEM *FileSystem,
    FSP_FILE_SYSTEM_OPERATION_GUARD *EnterOperation,
    FSP_FILE_SYSTEM_OPERATION_GUARD *LeaveOperation)
{
    FileSystem->EnterOperation = EnterOperation;
    FileSystem->LeaveOperation = LeaveOperation;
}
/**
 * Set file system locking strategy.
 *
 * @param FileSystem
 *     The file system object.
 * @param GuardStrategy
 *     The locking (guard) strategy.
 * @see
 *     FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY
 */
static inline
VOID FspFileSystemSetOperationGuardStrategy(FSP_FILE_SYSTEM *FileSystem,
    FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY GuardStrategy)
{
    FileSystem->OpGuardStrategy = GuardStrategy;
}
static inline
VOID FspFileSystemSetOperation(FSP_FILE_SYSTEM *FileSystem,
    ULONG Index,
    FSP_FILE_SYSTEM_OPERATION *Operation)
{
    FileSystem->Operations[Index] = Operation;
}
static inline
VOID FspFileSystemGetDispatcherResult(FSP_FILE_SYSTEM *FileSystem,
    NTSTATUS *PDispatcherResult)
{
    /* 32-bit reads are atomic */
    *PDispatcherResult = FileSystem->DispatcherResult;
    MemoryBarrier();
}
static inline
VOID FspFileSystemSetDispatcherResult(FSP_FILE_SYSTEM *FileSystem,
    NTSTATUS DispatcherResult)
{
    if (NT_SUCCESS(DispatcherResult))
        return;
    InterlockedCompareExchange(&FileSystem->DispatcherResult, DispatcherResult, 0);
}
static inline
VOID FspFileSystemSetDebugLog(FSP_FILE_SYSTEM *FileSystem,
    UINT32 DebugLog)
{
    FileSystem->DebugLog = DebugLog;
}
static inline
BOOLEAN FspFileSystemIsOperationCaseSensitive(VOID)
{
    FSP_FSCTL_TRANSACT_REQ *Request = FspFileSystemGetOperationContext()->Request;
    return
        FspFsctlTransactCreateKind == Request->Kind && Request->Req.Create.CaseSensitive ||
        FspFsctlTransactQueryDirectoryKind == Request->Kind && Request->Req.QueryDirectory.CaseSensitive;
}

/*
 * Operations
 */
FSP_API NTSTATUS FspFileSystemOpEnter(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpLeave(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpCreate(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpOverwrite(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpCleanup(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpClose(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpRead(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpWrite(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpQueryInformation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpSetInformation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpFlushBuffers(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpQueryVolumeInformation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpSetVolumeInformation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpQueryDirectory(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpFileSystemControl(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpQuerySecurity(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpSetSecurity(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspFileSystemOpQueryStreamInformation(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request, FSP_FSCTL_TRANSACT_RSP *Response);

/*
 * Helpers
 */
/**
 * Get open information buffer.
 *
 * This is a helper for implementing the Create and Open operations. It cannot be used with
 * any other operations.
 *
 * The FileInfo parameter to Create and Open is typed as pointer to FSP_FSCTL_FILE_INFO. The
 * true type of this parameter is pointer to FSP_FSCTL_OPEN_FILE_INFO. This simple function
 * converts from one type to the other.
 *
 * The FSP_FSCTL_OPEN_FILE_INFO type contains a FSP_FSCTL_FILE_INFO as well as the fields
 * NormalizedName and NormalizedNameSize. These fields can be used for file name normalization.
 * File name normalization is used to ensure that the FSD and the OS know the correct case
 * of a newly opened file name.
 *
 * For case-sensitive file systems this functionality should be ignored. The FSD will always
 * assume that the normalized file name is the same as the file name used to open the file.
 *
 * For case-insensitive file systems this functionality may be ignored. In this case the FSD
 * will assume that the normalized file name is the upper case version of the file name used
 * to open the file. The file system will work correctly and the only way an application will
 * be able to tell that the file system does not preserve case in normalized file names is by
 * issuing a GetFinalPathNameByHandle API call (or NtQueryInformationFile with
 * FileNameInformation/FileNormalizedNameInformation).
 *
 * For case-insensitive file systems this functionality may also be used. In this case the
 * user mode file system may use the NormalizedName and NormalizedNameSize parameters to
 * report to the FSD the normalized file name. It should be noted that the normalized file
 * name may only differ in case from the file name used to open the file. The NormalizedName
 * field will point to a buffer that can receive the normalized file name. The
 * NormalizedNameSize field will contain the size of the normalized file name buffer. On
 * completion of the Create or Open operation it should contain the actual size of the
 * normalized file name copied into the normalized file name buffer. The normalized file name
 * should not contain a terminating zero.
 *
 * @param FileInfo
 *     The FileInfo parameter as passed to Create or Open operation.
 * @return
 *     A pointer to the open information buffer for this Create or Open operation.
 * @see
 *     Create
 *     Open
 */
static inline
FSP_FSCTL_OPEN_FILE_INFO *FspFileSystemGetOpenFileInfo(FSP_FSCTL_FILE_INFO *FileInfo)
{
    return (FSP_FSCTL_OPEN_FILE_INFO *)FileInfo;
}
/**
 * Add directory information to a buffer.
 *
 * This is a helper for implementing the ReadDirectory operation.
 *
 * @param DirInfo
 *     The directory information to add. A value of NULL acts as an EOF marker for a ReadDirectory
 *     operation.
 * @param Buffer
 *     Pointer to a buffer that will receive the results of the read operation. This should contain
 *     the same value passed to the ReadDirectory Buffer parameter.
 * @param Length
 *     Length of data to read. This should contain the same value passed to the ReadDirectory
 *     Length parameter.
 * @param PBytesTransferred [out]
 *     Pointer to a memory location that will receive the actual number of bytes read. This should
 *     contain the same value passed to the ReadDirectory PBytesTransferred parameter.
 *     FspFileSystemAddDirInfo uses the value pointed by this parameter to track how much of the
 *     buffer has been used so far.
 * @return
 *     TRUE if the directory information was added, FALSE if there was not enough space to add it.
 * @see
 *     ReadDirectory
 */
FSP_API BOOLEAN FspFileSystemAddDirInfo(FSP_FSCTL_DIR_INFO *DirInfo,
    PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
/**
 * Find reparse point in file name.
 *
 * Given a file name this function returns an index to the first path component that is a reparse
 * point. The function will call the supplied GetReparsePointByName function for every path
 * component until it finds a reparse point or the whole path is processed.
 *
 * This is a helper for implementing the GetSecurityByName operation in file systems
 * that support reparse points.
 *
 * @param FileSystem
 *     The file system object.
 * @param GetReparsePointByName
 *     Pointer to function that can retrieve reparse point information by name. The
 *     FspFileSystemFindReparsePoint will call this function with the Buffer and PSize
 *     arguments set to NULL. The function should return STATUS_SUCCESS if the passed
 *     FileName is a reparse point or STATUS_NOT_A_REPARSE_POINT (or other error code)
 *     otherwise.
 * @param Context
 *     User context to supply to GetReparsePointByName.
 * @param FileName
 *     The name of the file or directory.
 * @param PReparsePointIndex
 *     Pointer to a memory location that will receive the index of the first reparse point
 *     within FileName. A value is only placed in this memory location if the function returns
 *     TRUE. May be NULL.
 * @return
 *     TRUE if a reparse point was found, FALSE otherwise.
 * @see
 *     GetSecurityByName
 */
FSP_API BOOLEAN FspFileSystemFindReparsePoint(FSP_FILE_SYSTEM *FileSystem,
    NTSTATUS (*GetReparsePointByName)(
        FSP_FILE_SYSTEM *FileSystem, PVOID Context,
        PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize),
    PVOID Context,
    PWSTR FileName, PUINT32 PReparsePointIndex);
/**
 * Resolve reparse points.
 *
 * Given a file name (and an index where to start resolving) this function will attempt to
 * resolve as many reparse points as possible. The function will call the supplied
 * GetReparsePointByName function for every path component until it resolves the reparse points
 * or the whole path is processed.
 *
 * This is a helper for implementing the ResolveReparsePoints operation in file systems
 * that support reparse points.
 *
 * @param FileSystem
 *     The file system object.
 * @param GetReparsePointByName
 *     Pointer to function that can retrieve reparse point information by name. The function
 *     should return STATUS_SUCCESS if the passed FileName is a reparse point or
 *     STATUS_NOT_A_REPARSE_POINT (or other error code) otherwise.
 * @param Context
 *     User context to supply to GetReparsePointByName.
 * @param FileName
 *     The name of the file or directory to have its reparse points resolved.
 * @param ReparsePointIndex
 *     The index of the first reparse point within FileName.
 * @param ResolveLastPathComponent
 *     If FALSE, the last path component of FileName should not be resolved, even
 *     if it is a reparse point that can be resolved. If TRUE, all path components
 *     should be resolved if possible.
 * @param PIoStatus
 *     Pointer to storage that will receive the status to return to the FSD. When
 *     this function succeeds it must set PIoStatus->Status to STATUS_REPARSE and
 *     PIoStatus->Information to either IO_REPARSE or the reparse tag.
 * @param Buffer
 *     Pointer to a buffer that will receive the resolved file name (IO_REPARSE) or
 *     reparse data (reparse tag). If the function returns a file name, it should
 *     not be NULL terminated.
 * @param PSize [in,out]
 *     Pointer to the buffer size. On input it contains the size of the buffer.
 *     On output it will contain the actual size of data copied.
 * @return
 *     STATUS_REPARSE or error code.
 * @see
 *     ResolveReparsePoints
 */
FSP_API NTSTATUS FspFileSystemResolveReparsePoints(FSP_FILE_SYSTEM *FileSystem,
    NTSTATUS (*GetReparsePointByName)(
        FSP_FILE_SYSTEM *FileSystem, PVOID Context,
        PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize),
    PVOID Context,
    PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent,
    PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
/**
 * Test whether reparse data can be replaced.
 *
 * This is a helper for implementing the SetReparsePoint/DeleteReparsePoint operation
 * in file systems that support reparse points.
 *
 * @param CurrentReparseData
 *     Pointer to the current reparse data.
 * @param CurrentReparseDataSize
 *     Pointer to the current reparse data size.
 * @param ReplaceReparseData
 *     Pointer to the replacement reparse data.
 * @param ReplaceReparseDataSize
 *     Pointer to the replacement reparse data size.
 * @return
 *     STATUS_SUCCESS or error code.
 * @see
 *     SetReparsePoint
 *     DeleteReparsePoint
 */
FSP_API NTSTATUS FspFileSystemCanReplaceReparsePoint(
    PVOID CurrentReparseData, SIZE_T CurrentReparseDataSize,
    PVOID ReplaceReparseData, SIZE_T ReplaceReparseDataSize);
/**
 * Add named stream information to a buffer.
 *
 * This is a helper for implementing the GetStreamInfo operation.
 *
 * @param StreamInfo
 *     The stream information to add. A value of NULL acts as an EOF marker for a GetStreamInfo
 *     operation.
 * @param Buffer
 *     Pointer to a buffer that will receive the stream information. This should contain
 *     the same value passed to the GetStreamInfo Buffer parameter.
 * @param Length
 *     Length of buffer. This should contain the same value passed to the GetStreamInfo
 *     Length parameter.
 * @param PBytesTransferred [out]
 *     Pointer to a memory location that will receive the actual number of bytes stored. This should
 *     contain the same value passed to the GetStreamInfo PBytesTransferred parameter.
 * @return
 *     TRUE if the stream information was added, FALSE if there was not enough space to add it.
 * @see
 *     GetStreamInfo
 */
FSP_API BOOLEAN FspFileSystemAddStreamInfo(FSP_FSCTL_STREAM_INFO *StreamInfo,
    PVOID Buffer, ULONG Length, PULONG PBytesTransferred);

/*
 * Directory buffering
 */
FSP_API BOOLEAN FspFileSystemAcquireDirectoryBuffer(PVOID *PDirBuffer,
    BOOLEAN Reset, PNTSTATUS PResult);
FSP_API BOOLEAN FspFileSystemFillDirectoryBuffer(PVOID *PDirBuffer,
    FSP_FSCTL_DIR_INFO *DirInfo, PNTSTATUS PResult);
FSP_API VOID FspFileSystemReleaseDirectoryBuffer(PVOID *PDirBuffer);
FSP_API VOID FspFileSystemReadDirectoryBuffer(PVOID *PDirBuffer,
    PWSTR Marker,
    PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
FSP_API VOID FspFileSystemDeleteDirectoryBuffer(PVOID *PDirBuffer);

/*
 * Security
 */
FSP_API PGENERIC_MAPPING FspGetFileGenericMapping(VOID);
FSP_API NTSTATUS FspAccessCheckEx(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request,
    BOOLEAN CheckParentOrMain, BOOLEAN AllowTraverseCheck,
    UINT32 DesiredAccess, PUINT32 PGrantedAccess/* or ReparsePointIndex */,
    PSECURITY_DESCRIPTOR *PSecurityDescriptor);
FSP_API NTSTATUS FspCreateSecurityDescriptor(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request,
    PSECURITY_DESCRIPTOR ParentDescriptor,
    PSECURITY_DESCRIPTOR *PSecurityDescriptor);
/**
 * Modify security descriptor.
 *
 * This is a helper for implementing the SetSecurity operation.
 *
 * @param InputDescriptor
 *     The input security descriptor to be modified.
 * @param SecurityInformation
 *     Describes what parts of the InputDescriptor should be modified. This should contain
 *     the same value passed to the SetSecurity SecurityInformation parameter.
 * @param ModificationDescriptor
 *     Describes the modifications to apply to the InputDescriptor. This should contain
 *     the same value passed to the SetSecurity ModificationDescriptor parameter.
 * @param PSecurityDescriptor [out]
 *     Pointer to a memory location that will receive the resulting security descriptor.
 *     This security descriptor can be later freed using FspDeleteSecurityDescriptor.
 * @return
 *     STATUS_SUCCESS or error code.
 * @see
 *     SetSecurity
 *     FspDeleteSecurityDescriptor
 */
FSP_API NTSTATUS FspSetSecurityDescriptor(
    PSECURITY_DESCRIPTOR InputDescriptor,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR ModificationDescriptor,
    PSECURITY_DESCRIPTOR *PSecurityDescriptor);
/**
 * Delete security descriptor.
 *
 * This is a helper for implementing the SetSecurity operation.
 *
 * @param SecurityDescriptor
 *     The security descriptor to be deleted.
 * @param CreateFunc
 *     Function used to create the security descriptor. This parameter should be
 *     set to FspSetSecurityDescriptor for the public API.
 * @return
 *     STATUS_SUCCESS or error code.
 * @see
 *     SetSecurity
 *     FspSetSecurityDescriptor
 */
FSP_API VOID FspDeleteSecurityDescriptor(PSECURITY_DESCRIPTOR SecurityDescriptor,
    NTSTATUS (*CreateFunc)());
static inline
NTSTATUS FspAccessCheck(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_TRANSACT_REQ *Request,
    BOOLEAN CheckParentOrMain, BOOLEAN AllowTraverseCheck,
    UINT32 DesiredAccess, PUINT32 PGrantedAccess)
{
    return FspAccessCheckEx(FileSystem, Request,
        CheckParentOrMain, AllowTraverseCheck,
        DesiredAccess, PGrantedAccess,
        0);
}

/*
 * POSIX Interop
 */
FSP_API NTSTATUS FspPosixMapUidToSid(UINT32 Uid, PSID *PSid);
FSP_API NTSTATUS FspPosixMapSidToUid(PSID Sid, PUINT32 PUid);
FSP_API VOID FspDeleteSid(PSID Sid, NTSTATUS (*CreateFunc)());
FSP_API NTSTATUS FspPosixMapPermissionsToSecurityDescriptor(
    UINT32 Uid, UINT32 Gid, UINT32 Mode,
    PSECURITY_DESCRIPTOR *PSecurityDescriptor);
FSP_API NTSTATUS FspPosixMapSecurityDescriptorToPermissions(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    PUINT32 PUid, PUINT32 PGid, PUINT32 PMode);
FSP_API NTSTATUS FspPosixMapWindowsToPosixPathEx(PWSTR WindowsPath, char **PPosixPath,
    BOOLEAN Translate);
FSP_API NTSTATUS FspPosixMapPosixToWindowsPathEx(const char *PosixPath, PWSTR *PWindowsPath,
    BOOLEAN Translate);
static inline
NTSTATUS FspPosixMapWindowsToPosixPath(PWSTR WindowsPath, char **PPosixPath)
{
    return FspPosixMapWindowsToPosixPathEx(WindowsPath, PPosixPath, TRUE);
}
static inline
NTSTATUS FspPosixMapPosixToWindowsPath(const char *PosixPath, PWSTR *PWindowsPath)
{
    return FspPosixMapPosixToWindowsPathEx(PosixPath, PWindowsPath, TRUE);
}
FSP_API VOID FspPosixDeletePath(void *Path);
FSP_API VOID FspPosixEncodeWindowsPath(PWSTR WindowsPath, ULONG Size);
FSP_API VOID FspPosixDecodeWindowsPath(PWSTR WindowsPath, ULONG Size);

/*
 * Path Handling
 */
FSP_API VOID FspPathPrefix(PWSTR Path, PWSTR *PPrefix, PWSTR *PRemain, PWSTR Root);
FSP_API VOID FspPathSuffix(PWSTR Path, PWSTR *PRemain, PWSTR *PSuffix, PWSTR Root);
FSP_API VOID FspPathCombine(PWSTR Prefix, PWSTR Suffix);

/**
 * @group Service Framework
 *
 * User mode file systems typically are run as Windows services. WinFsp provides an API to make
 * the creation of Windows services easier. This API is provided for convenience and is not
 * necessary to expose a user mode file system to Windows.
 */
typedef struct _FSP_SERVICE FSP_SERVICE;
typedef NTSTATUS FSP_SERVICE_START(FSP_SERVICE *, ULONG, PWSTR *);
typedef NTSTATUS FSP_SERVICE_STOP(FSP_SERVICE *);
typedef NTSTATUS FSP_SERVICE_CONTROL(FSP_SERVICE *, ULONG, ULONG, PVOID);
#pragma warning(push)
#pragma warning(disable:4200)           /* zero-sized array in struct/union */
typedef struct _FSP_SERVICE
{
    UINT16 Version;
    PVOID UserContext;
    FSP_SERVICE_START *OnStart;
    FSP_SERVICE_STOP *OnStop;
    FSP_SERVICE_CONTROL *OnControl;
    ULONG AcceptControl;
    ULONG ExitCode;
    SERVICE_STATUS_HANDLE StatusHandle;
    SERVICE_STATUS ServiceStatus;
    CRITICAL_SECTION ServiceStatusGuard;
    CRITICAL_SECTION ServiceStopGuard;
    BOOLEAN AllowConsoleMode;
    WCHAR ServiceName[];
} FSP_SERVICE;
#pragma warning(pop)
/**
 * Run a service.
 *
 * This function wraps calls to FspServiceCreate, FspServiceLoop and FspServiceDelete to create,
 * run and delete a service. It is intended to be used from a service's main/wmain function.
 *
 * This function runs a service with console mode allowed.
 *
 * @param ServiceName
 *     The name of the service.
 * @param OnStart
 *     Function to call when the service starts.
 * @param OnStop
 *     Function to call when the service stops.
 * @param OnControl
 *     Function to call when the service receives a service control code.
 * @return
 *     Service process exit code.
 */
FSP_API ULONG FspServiceRunEx(PWSTR ServiceName,
    FSP_SERVICE_START *OnStart,
    FSP_SERVICE_STOP *OnStop,
    FSP_SERVICE_CONTROL *OnControl,
    PVOID UserContext);
static inline
ULONG FspServiceRun(PWSTR ServiceName,
    FSP_SERVICE_START *OnStart,
    FSP_SERVICE_STOP *OnStop,
    FSP_SERVICE_CONTROL *OnControl)
{
    return FspServiceRunEx(ServiceName, OnStart, OnStop, OnControl, 0);
}
/**
 * Create a service object.
 *
 * @param ServiceName
 *     The name of the service.
 * @param OnStart
 *     Function to call when the service starts.
 * @param OnStop
 *     Function to call when the service stops.
 * @param OnControl
 *     Function to call when the service receives a service control code.
 * @param PService [out]
 *     Pointer that will receive the service object created on successful return from this
 *     call.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspServiceCreate(PWSTR ServiceName,
    FSP_SERVICE_START *OnStart,
    FSP_SERVICE_STOP *OnStop,
    FSP_SERVICE_CONTROL *OnControl,
    FSP_SERVICE **PService);
/**
 * Delete a service object.
 *
 * @param Service
 *     The service object.
 */
FSP_API VOID FspServiceDelete(FSP_SERVICE *Service);
/**
 * Allow a service to run in console mode.
 *
 * A service that is run in console mode runs with a console attached and outside the control of
 * the Service Control Manager. This is useful for debugging and testing a service during
 * development.
 *
 * User mode file systems that wish to use the WinFsp Launcher functionality must also use this
 * call. The WinFsp Launcher is a Windows service that can be configured to launch and manage
 * multiple instances of a user mode file system.
 *
 * @param Service
 *     The service object.
 */
FSP_API VOID FspServiceAllowConsoleMode(FSP_SERVICE *Service);
/**
 * Configure the control codes that a service accepts.
 *
 * This API should be used prior to Start operations.
 *
 * @param Service
 *     The service object.
 * @param Control
 *     The control codes to accept. Note that the SERVICE_ACCEPT_PAUSE_CONTINUE code is silently
 *     ignored.
 */
FSP_API VOID FspServiceAcceptControl(FSP_SERVICE *Service, ULONG Control);
/**
 * Request additional time from the Service Control Manager.
 *
 * This API should be used during Start and Stop operations only.
 *
 * @param Service
 *     The service object.
 * @param Time
 *     Additional time (in milliseconds).
 */
FSP_API VOID FspServiceRequestTime(FSP_SERVICE *Service, ULONG Time);
/**
 * Set the service process exit code.
 *
 * @param Service
 *     The service object.
 * @param ExitCode
 *     Service process exit code.
 */
FSP_API VOID FspServiceSetExitCode(FSP_SERVICE *Service, ULONG ExitCode);
/**
 * Get the service process exit code.
 *
 * @param Service
 *     The service object.
 * @return
 *     Service process exit code.
 */
FSP_API ULONG FspServiceGetExitCode(FSP_SERVICE *Service);
/**
 * Run a service main loop.
 *
 * This function starts and runs a service. It executes the Windows StartServiceCtrlDispatcher API
 * to connect the service process to the Service Control Manager. If the Service Control Manager is
 * not available (and console mode is allowed) it will enter console mode.
 *
 * @param Service
 *     The service object.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API NTSTATUS FspServiceLoop(FSP_SERVICE *Service);
/**
 * Stops a running service.
 *
 * Stopping a service usually happens when the Service Control Manager instructs the service to
 * stop. In some situations (e.g. fatal errors) the service may wish to stop itself. It can do so
 * in a clean manner by calling this function.
 *
 * @param Service
 *     The service object.
 * @return
 *     STATUS_SUCCESS or error code.
 */
FSP_API VOID FspServiceStop(FSP_SERVICE *Service);
/**
 * Determine if the current process is running in user interactive mode.
 *
 * @return
 *     TRUE if the process is running in running user interactive mode.
 */
FSP_API BOOLEAN FspServiceIsInteractive(VOID);
/**
 * Log a service message.
 *
 * This function can be used to log an arbitrary message to the Windows Event Log or to the current
 * console if running in user interactive mode.
 *
 * @param Type
 *     One of EVENTLOG_INFORMATION_TYPE, EVENTLOG_WARNING_TYPE, EVENTLOG_ERROR_TYPE.
 * @param Format
 *     Format specification. This function uses the Windows wsprintf API for formatting. Refer to
 *     that API's documentation for details on the format specification.
 */
FSP_API VOID FspServiceLog(ULONG Type, PWSTR Format, ...);
FSP_API VOID FspServiceLogV(ULONG Type, PWSTR Format, va_list ap);

/*
 * Utility
 */
FSP_API NTSTATUS FspNtStatusFromWin32(DWORD Error);
FSP_API DWORD FspWin32FromNtStatus(NTSTATUS Status);
FSP_API VOID FspEventLog(ULONG Type, PWSTR Format, ...);
FSP_API VOID FspEventLogV(ULONG Type, PWSTR Format, va_list ap);
FSP_API VOID FspDebugLogSetHandle(HANDLE Handle);
FSP_API VOID FspDebugLog(const char *Format, ...);
FSP_API VOID FspDebugLogSD(const char *Format, PSECURITY_DESCRIPTOR SecurityDescriptor);
FSP_API VOID FspDebugLogFT(const char *Format, PFILETIME FileTime);
FSP_API VOID FspDebugLogRequest(FSP_FSCTL_TRANSACT_REQ *Request);
FSP_API VOID FspDebugLogResponse(FSP_FSCTL_TRANSACT_RSP *Response);
FSP_API NTSTATUS FspCallNamedPipeSecurely(PWSTR PipeName,
    PVOID InBuffer, ULONG InBufferSize, PVOID OutBuffer, ULONG OutBufferSize,
    PULONG PBytesTransferred, ULONG Timeout,
    PSID Sid);
FSP_API NTSTATUS FspVersion(PUINT32 PVersion);

/*
 * Delay load
 */
static inline
NTSTATUS FspLoad(PVOID *PModule)
{
#if defined(_WIN64)
#define FSP_DLLNAME                     "winfsp-x64.dll"
#else
#define FSP_DLLNAME                     "winfsp-x86.dll"
#endif
#define FSP_DLLPATH                     "bin\\" FSP_DLLNAME

    WINADVAPI
    LSTATUS
    APIENTRY
    RegGetValueW(
        HKEY hkey,
        LPCWSTR lpSubKey,
        LPCWSTR lpValue,
        DWORD dwFlags,
        LPDWORD pdwType,
        PVOID pvData,
        LPDWORD pcbData);

    WCHAR PathBuf[MAX_PATH];
    DWORD Size;
    HKEY RegKey;
    LONG Result;
    HMODULE Module;

    if (0 != PModule)
        *PModule = 0;

    Module = LoadLibraryW(L"" FSP_DLLNAME);
    if (0 == Module)
    {
        Result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\WinFsp",
            0, KEY_READ | KEY_WOW64_32KEY, &RegKey);
        if (ERROR_SUCCESS == Result)
        {
            Size = sizeof PathBuf - sizeof L"" FSP_DLLPATH + sizeof(WCHAR);
            Result = RegGetValueW(RegKey, 0, L"InstallDir",
                RRF_RT_REG_SZ, 0, PathBuf, &Size);
            RegCloseKey(RegKey);
        }
        if (ERROR_SUCCESS != Result)
            return STATUS_OBJECT_NAME_NOT_FOUND;

        RtlCopyMemory(PathBuf + (Size / sizeof(WCHAR) - 1), L"" FSP_DLLPATH, sizeof L"" FSP_DLLPATH);
        Module = LoadLibraryW(PathBuf);
        if (0 == Module)
            return STATUS_DLL_NOT_FOUND;
    }

    if (0 != PModule)
        *PModule = Module;

    return STATUS_SUCCESS;

#undef FSP_DLLNAME
#undef FSP_DLLPATH
}

#ifdef __cplusplus
}
#endif

#endif
