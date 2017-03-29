
#include <winfsp/winfsp.h>
#include <vcclr.h>  
#define DEFAULT_SECTOR_SIZE               512
#define DEFAULT_SECTORS_PER_ALLOCATION_UNIT 1

#define DEFAULT_MAX_IRP_CAPACITY 1000
#define DEFAULT_IRP_TIMOUT 10*60*1000 
#define DEFAULT_TRANSACT_TIMEOUT 10*1000 

#define HANDLE_TO_FILESYSTEM(f) (WinFsp::FileSystem^)GCHandle::FromIntPtr(IntPtr::IntPtr(f)).Target
#define HANDLE_TO_FILECONTEXT(f) (WinFsp::FspFileContext^)GCHandle::FromIntPtr(IntPtr::IntPtr(f)).Target
#define COPY_FILE_INFO(fInfo,PtrInfo)  IntPtr pnt = Marshal::AllocHGlobal(Marshal::SizeOf(fInfo)); \
                                       Marshal::StructureToPtr(fInfo, pnt, false); \
                                       memcpy(PtrInfo, pnt.ToPointer(), sizeof FSP_FSCTL_FILE_INFO);Marshal::FreeHGlobal(pnt);
using namespace System;
using namespace System::Runtime;
using System::Runtime::InteropServices::Marshal;
using System::Runtime::InteropServices::GCHandle;
#pragma once
/// <summary>
/// 
/// </summary>
namespace WinFsp {
    ref class FileSystem;    
    #pragma region Constant declaration 
    public ref class CleanUpFlags {
    public:
        static const ULONG FspCleanupDelete = 0x01;
        static const ULONG FspCleanupSetAllocationSize = 0x02;
        static const ULONG FspCleanupSetArchiveBit = 0x10;
        static const ULONG FspCleanupSetLastAccessTime = 0x20;
        static const ULONG FspCleanupSetLastWriteTime = 0x40;
        static const ULONG FspCleanupSetChangeTime = 0x80;
    };

    public ref class WinFspFileAttributes {
    public:
        static const UINT32 ReadOnly = 0x00000001;
        static const UINT32 Hidden = 0x00000002;
        static const UINT32 System = 0x00000004;
        static const UINT32 Directory = 0x00000010;
        static const UINT32 Archive = 0x00000020;
        static const UINT32 Device = 0x00000040;
        static const UINT32 Normal = 0x00000080;
        static const UINT32 Temporary = 0x00000100;
        static const UINT32 SparseFile = 0x00000200;
        static const UINT32 ReparsePoint = 0x00000400;
        static const UINT32 Compressed = 0x00000800;
        static const UINT32 Offline = 0x00001000;
        static const UINT32 NotContentIndexed = 0x00002000;
        static const UINT32 Encrypted = 0x00004000;
        static const UINT32 IntegritySystem = 0x00008000;
        static const UINT32 Virtual = 0x00010000;
        static const UINT32 NoScrubData = 0x00020000;
        static const UINT32 EA = 0x00040000;
        static const UINT32 InvalidAttribute = 4294967295;
    };

    public ref class NtStatus
    {
    public:
        //NT STATUS
        static const UINT32 Success = 0x00000000;

        static const UINT32 MoreEntried = 0x00000105;
        static const UINT32 NoSuchDevice = 0xC000000E;
        static const UINT32 NoSuchFile = 0xC000000F;
        static const UINT32 NoMoreFiles = 0x80000006;
        static const UINT32 ObjectNameInvalid = 0xC0000033;
        static const UINT32 ObjectNotFound = 0xC0000034;
        static const UINT32 ObjectNameCollision = 0xC0000035;
        static const UINT32 ObjectPathNotFound = 0xC000003A;
        static const UINT32 FileIsDirectory = 0xC00000BA;
        static const UINT32 NotADirectory = 0xC0000103;
        static const UINT32 NotSupported = 0xC00000BB;
        static const UINT32 NoncontinuableException = 0xC0000025;
        static const UINT32 BufferOverflow = 0x80000005;
        static const UINT32 DirectoryNotEmpty = 0xC0000101;
        static const UINT32 EndOfFile = 0xC0000011;
        static const UINT32 NotImplemented = 0xC0000002;
        static const UINT32 RequestNotAccepted = 0xC00000D0;
        static const UINT32 InvalidParameter = 0xC000000D;
        static const UINT32 AccessDenied = 0xC0000022;
        static const UINT32 InvalidDeviceRequest = 0xC0000010;
    };

    public ref class WinFspFileCreateOption {
    public:
        static const UINT32 DirectoryFile = 0x00000001;
        static    const UINT32 WriteThrough = 0x00000002;
        static    const UINT32 SequentialFile = 0x00000004;
        static const UINT32 NoIntermediateBuffer = 0x00000008;

        static const UINT32 SynchronusIoAlert = 0x00000010;
        static const UINT32 SynchronusIoNoAlert = 0x00000020;
        static const UINT32 NonDirectoryFile = 0x00000040;
        static const UINT32 CreateTreeConnection = 0x00000080;

        static const UINT32 CompleteIfOplocked = 0x00000100;
        static const UINT32 NoEAKnowlaege = 0x00000200;
        static const UINT32 OpenRemoteInstance = 0x00000400;
        static const UINT32 RandomAccess = 0x00000800;

        static const UINT32 DeleteOnClose = 0x00001000;
        static const UINT32 OpenByFileId = 0x00002000;
        static const UINT32 OpenForBackupIntent = 0x00004000;
        static const UINT32 NoCompression = 0x00008000;
    };

    public ref class SecurityInformation {
    public:
        static const UINT32 OwnerSecurityInformation = (0x00000001L);
        static const UINT32 GroupSecurityInformation = (0x00000002L);
        static const UINT32 DaclSecurityInformation = (0x00000004L);
        static const UINT32 SaclSecurityInformation = (0x00000008L);
        static const UINT32 LabelSecurityInformation = (0x00000010L);
        static const UINT32 AttributeSecurityInformation = (0x00000020L);
        static const UINT32 ScopeSecurityInformation = (0x00000040L);
        static const UINT32 ProcessTrustLabelSecurityInformation = (0x00000080L);
        static const UINT32 BackupSecurityInformation = (0x00010000L);

        static const UINT32 ProtectedDaclSecurityInformation = (0x80000000L);
        static const UINT32 ProtectedSaclSecurityInformation = (0x40000000L);
        static const UINT32 UnProtectedDaclSecurityInformation = (0x20000000L);
        static const UINT32 UnProtectedSaclSecurityInformation = (0x10000000L);

    };

    #pragma endregion

    #pragma region WinFsp Types
    [System::Runtime::InteropServices::StructLayout(System::Runtime::InteropServices::LayoutKind::Sequential)]
    public ref struct FileInfo {
        UINT32 FileAttributes;
        UINT32 ReparseTag;
        UINT64 AllocationSize;
        UINT64 FileSize;
        UINT64 CreationTime;
        UINT64 LastAccessTime;
        UINT64 LastWriteTime;
        UINT64 ChangeTime;
        UINT64 IndexNumber;
        UINT32 HardLinks;
    };

    public ref struct FileSystemConfig {
        bool IsNetworkMount;
        String^ VolumePrefix;
        String^ RootSecurityDescriptor;
        String^ FileSystemName;
        UINT16 SectorSize;
        UINT16 SectorPerAllocationUnit;
        UINT64 VolumeCreationTime;
        ULONG SerialNumber;
        ULONG FileInfoTimeOut;
        ULONG MaxComponentLength;
        bool CaseSensitive;
        bool CaseSensitiveNames;
        bool UnicodeOnDisk;
        bool PresistACL;
        bool ReparsePoints;
        bool ReparsePointAccessChecks;
        bool NameSteam;
        bool PostCleanupWhenModifiedOnly;
        bool IsReadOnlyVolume;
    };
    public ref struct VolumeInformation
    {
        UINT64 TotalSize;
        UINT64 FreeSize;
        String^ VolumeLabel;
    };
    public enum MountType {
        NetMount,
        DiskMount
    };
    public ref class FspFileContext {
    public:
        Object^ FileNode;
        Object^ FileDescriptor;
        FileInfo^ FileInfo;
    };
    public ref class StreamInformation {

    };
    public ref struct IoStatusBlock {
    public:
            ULONG_PTR Information;
            NTSTATUS Status;
    };
    #pragma endregion
    
    #pragma region Interop classes declaration
    public ref class ReadDirectoryBuffer {    
    private:
        PVOID _buffer;
        ULONG _length;
        PULONG _pBytesTransferred;
    internal:
        ReadDirectoryBuffer(PVOID Buffer, ULONG Length, PULONG PBytesTransferred);        
    public:
        bool AddItem(String^ fname, FileInfo^ Info);
        void SetEoF();
    };

    public ref class FsHelperOp {
    public:
        UINT64 static GetFileTime();
        static NTSTATUS FileSystemResolveReparsePoints(FileSystem^ Fs,
            FspFileContext Context,
            String^ FileName,
            UINT32 ReparsePointIndex,
            BOOLEAN ResolveLastPathComponent,
            IoStatusBlock% IoStatus,
            IntPtr Buffer,
            SIZE_T% Size);
        static int CopyBufferToNativeByPin(IntPtr dest, array<byte^>^ data, int sourceOffset, int destOffset, int length);
    };
    public ref class SecurityDescriptor {
        IntPtr^ SecurityDescriptorPtr;
        ULONG Length;
        String^ Sddl;

        static bool ConvertToString(PSECURITY_DESCRIPTOR desc, ULONG length, String^% descriptorStr, PULONG error);
        static bool ConvertToDescriptor(String^ descriptrStr, PSECURITY_DESCRIPTOR* ptrDesc, PULONG length, PULONG error);
    public:
        SecurityDescriptor(PSECURITY_DESCRIPTOR ptr, ULONG length);
        SecurityDescriptor(String^ sddl);
        ~SecurityDescriptor();
        String^ ToString() override;
    };

    #pragma endregion

  

    public ref class FileSystemInteface abstract {            
    public:       
        /// <summary>
        /// Gets the volume information.
        /// </summary>
        /// <param name="fileSystem">The file system on which this request is posted.</param>
        /// <param name="volumeInfo">Ref to a structure that will receive the volume information on successful return from this call.</param>
        /// <returns> STATUS_SUCCESS or error code. </returns>
        virtual UINT32 GetVolumeInfo(FileSystem^ fileSystem, VolumeInformation^% volumeInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Sets the volume label.
        /// </summary>
        /// <param name="fileSystem">The file system on which this request is posted. </param>
        /// <param name="volumeLabel"> The name of the file or directory to get the attributes and security descriptor for. </param>
        /// <param name="volumeInfo"> Ref to a structure that will receive the volume information on successful return from this call.</param>
        /// <returns></returns>
        virtual UINT32 SetVolumeLabel(FileSystem^ fileSystem, 
            String^ volumeLabel, 
            VolumeInformation^% volumeInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Gets the security by FileName.
        /// </summary>
        /// <param name="fileSystem">The file system on which this request is posted.</param>
        /// <param name="fileName">The name of the file or directory to get the attributes and 
        /// security descriptor for.</param>
        /// <param name="fileAttributes"> Pointer to a memory location that will receive the 
        /// file attributes on successful return from this call. May be NULL. If this call 
        /// returns STATUS_REPARSE, the file system MAY place here the index of the first 
        /// reparse point within FileName.The file system MAY also leave this at its default
        /// value of 0.        
        ///</param>
        /// <param name="descriptor">
        /// Pointer to a buffer that will receive the file security descriptor on successful return
        /// from this call.May be NULL.
        /// </param>
        /// <returns>
        /// STATUS_SUCCESS, STATUS_REPARSE or error code.           
        /// STATUS_REPARSE should be returned by file systems that support reparse points when
        /// they encounter a FileName that contains reparse points anywhere but the final path
        /// component.
        /// </returns>
        virtual UINT32 GetSecurityByName(FileSystem^ fileSystem, 
            String^ fileName, 
            UINT32% fileAttributes, 
            SecurityDescriptor^% descriptor) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Creates the specified file system.
        /// </summary>
        /// <param name="FileSystem">The file system.</param>
        /// <param name="FileName">Name of the file.</param>
        /// <param name="CreateOptions">The create options.</param>
        /// <param name="GrantedAccess">The granted access.</param>
        /// <param name="fileAttributes">The file attributes.</param>
        /// <param name="descriptor">The descriptor.</param>
        /// <param name="AllocationSize">Size of the allocation.</param>
        /// <param name="fileContext">The file context.</param>
        /// <param name="fileInfo">The file information.</param>
        /// <returns></returns>
        virtual UINT32 Create(FileSystem^ FileSystem,
            String^ FileName, 
            UINT32 CreateOptions,
            UINT32 GrantedAccess,
            UINT32 fileAttributes,
            SecurityDescriptor^% descriptor,
            UINT64 AllocationSize,
            FspFileContext^ fileContext, 
            FileInfo^% fileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        ///  Open a file or directory.
        /// </summary>
        /// <param name="fileSystem">  The file system on which this request is posted.</param>
        /// <param name="fileName">  The name of the file or directory to be opened. </param>
        /// <param name="CreateOptions">The create options.</param>
        /// Create options for this request.This parameter has the same meaning as the
        /// CreateOptions parameter of the NtCreateFile API.User mode file systems typically
        /// do not need to do anything special with respect to this parameter.Some file systems may
        /// also want to pay attention to the FILE_NO_INTERMEDIATE_BUFFERING and FILE_WRITE_THROUGH
        /// flags, although these are typically handled by the FSD component.
        /// </param>
        /// <param name="GrantedAccess">
        /// Determines the specific access rights that have been granted for this request.Upon
        /// receiving this call all access checks have been performed and the user mode file system
        /// need not perform any additional checks.However this parameter may be useful to a user
        /// mode file system; for example the WinFsp - FUSE layer uses this parameter to determine
        /// which flags to use in its POSIX open() call.
        /// </param>
        /// <param name="FileContext"> Pointer that will receive the file context on successful return from this call.</param>
        /// <param name="FileInfo">
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.
        /// </param>
        /// <returns> STATUS_SUCCESS or error code.</returns>
        virtual UINT32 Open(FileSystem^ fileSystem,
            String^ fileName, 
            UINT32 CreateOptions, 
            UINT32 GrantedAccess,
            FspFileContext^ FileContext, 
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Overwrites the specified file system.
        /// </summary>
        /// <param name="FileSystem">The file system.</param>
        /// <param name="FileContext">The file context.</param>
        /// <param name="FileAttributes">The file attributes.</param>
        /// <param name="ReplaceFileAttributes">if set to <c>true</c> [replace file attributes].</param>
        /// <param name="AllocationSize">Size of the allocation.</param>
        /// <param name="FileInfo">The file information.</param>
        /// <returns></returns>
        virtual UINT32 Overwrite(FileSystem^ FileSystem,
            FspFileContext^ FileContext, 
            UINT32 FileAttributes, 
            bool ReplaceFileAttributes, 
            UINT64 AllocationSize,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Cleanups the specified file system.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext"> The file context of the file or directory to cleanup.</param>
        /// <param name="FileName"> The name of the file or directory to cleanup. Sent only when a Delete is requested.</param>
        /// <param name="Flags">  These flags determine whether the file was modified and whether to delete the file.</param>
        virtual  VOID Cleanup(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName,
            ULONG Flags) {                      
        }
        /// <summary>
        /// Close a file
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to be closed.</param>
        virtual  VOID Close(FileSystem^ FileSystem,
            FspFileContext^ FileContext) {

        }
        /// <summary>
        /// Read a file.
        /// </summary>
        /// <param name="FileSystem">  The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file to be read.</param>
        /// <param name="Buffer">Native memory pointer to a buffer that will receive the results of the read operation.</param>
        /// <param name="Offset">Offset within the file to read from.</param>
        /// <param name="Length"> Length of data to read.</param>
        /// <param name="BytesTransferred"> It receives the actual number of bytes read.</param>
        /// <returns>STATUS_SUCCESS or error code.STATUS_PENDING is supported allowing for asynchronous operation.</returns>
        virtual UINT32 Read(FileSystem ^FileSystem,
            FspFileContext^ FileContext, 
            IntPtr Buffer, 
            UINT64 Offset, 
            ULONG Length,
            ULONG% BytesTransferred) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Write a file.
        /// </summary>
        /// <param name="FileSystem">  The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file to be read.</param>
        /// <param name="Buffer">Native memory buffer that contains the data to write.</param>
        /// <param name="Offset">Offset within the file to write to.</param>
        /// <param name="Length"> Length of data to write.</param>
        /// <param name="WriteToEndOfFile">
        /// When TRUE the file system must write to the current end of file.In this case the Offset
        /// parameter will contain the value - 1.
        /// </param>
        /// <param name="ConstrainedIo">
        /// When TRUE the file system must not extend the file (i.e. change the file size).
        /// </param>
        /// <param name="BytesTransferred">
        /// It receive the actual number of bytes written.
        /// </param>
        /// <param name="FileInfo">
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.
        /// </param>
        /// <returns>
        /// STATUS_SUCCESS or error code.STATUS_PENDING is supported allowing for asynchronous
        /// operation.
        /// </returns>
        virtual UINT32 Write(FileSystem ^FileSystem,
            FspFileContext^ FileContext,
            IntPtr Buffer,
            UINT64 Offset,
            ULONG Length,
            bool WriteToEndOfFile,
            bool ConstrainedIo,
            ULONG% BytesTransferred,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        ///  Flush a file or volume.
        ///  Note that the FSD will also flush all file / volume caches prior to invoking this operation.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file to be flushed. When NULL the whole volume is being flushed.</param>
        /// <param name="FileInfo">
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.Used when
        /// flushing file(not volume).
        ///</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual   UINT32 Flush(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        ///  Get file or directory information.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to get information for.</param>
        /// <param name="FileInfo">
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.Used when
        /// flushing file(not volume).
        ///</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 GetFileInfo(FileSystem ^FileSystem,
            FspFileContext^ FileContext,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Set file or directory basic information.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to set information for.</param>
        /// <param name="FileAttributes">
        ///  File attributes to apply to the file or directory. If the value INVALID_FILE_ATTRIBUTES
        ///  is sent, the file attributes should not be changed.
        /// </param>
        /// <param name="CreationTime">        
        /// Creation time to apply to the file or directory.If the value 0 is sent, the creation
        /// time should not be changed.
        /// </param>
        /// <param name="LastAccessTime">
        /// Last access time to apply to the file or directory.If the value 0 is sent, the last
        /// access time should not be changed.
        /// </param>
        /// <param name="LastWriteTime">
        /// Last write time to apply to the file or directory.If the value 0 is sent, the last
        /// write time should not be changed.
        /// </param>
        /// <param name="ChangeTime">        
        /// Change time to apply to the file or directory.If the value 0 is sent, the change time
        /// should not be changed.        
        ///</param>
        /// <param name="FileInfo">        
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.
        ///</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual UINT32 SetBasicInfo(FileSystem ^FileSystem,
            FspFileContext^ FileContext,
            UINT32 FileAttributes,
            UINT64 CreationTime,
            UINT64 LastAccessTime,
            UINT64 LastWriteTime,
            UINT64 ChangeTime,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Set file / allocation size.
        /// This function is used to change a file's sizes. Windows file systems maintain two kinds
        /// of sizes : the file size is where the End Of File(EOF) is, and the allocation size is the
        /// actual size that a file takes up on the "disk".  
        /// The rules regarding file / allocation size are :
        ///<ul>
        /// <li>Allocation size must always be aligned to the allocation unit boundary.The allocation
        /// unit is the product <code>(UINT64)SectorSize/// (UINT64)SectorsPerAllocationUnit< / code> from
        /// the FSP_FSCTL_VOLUME_PARAMS structure.The FSD will always send properly aligned allocation
        /// sizes when setting the allocation size.< / li>
        /// <li>Allocation size is always greater or equal to the file size.< / li>
        /// <li>A file size of more than the current allocation size will also extend the allocation
        /// size to the next allocation unit boundary.< / li>
        /// <li>An allocation size of less than the current file size should also truncate the current
        /// file size.< / li>
        /// < / ul>
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file to set the file/allocation size for.</param>
        /// <param name="NewSize"> New file/allocation size to apply to the file.</param>
        /// <param name="SetAllocationSize"> If TRUE, then the allocation size is being set. if FALSE, then the file size is being set.</param>
        /// <param name="FileInfo">        
        /// Pointer to a structure that will receive the file information on successful return
        /// from this call.This information includes file attributes, file times, etc.
        ///</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 SetFileSize(FileSystem ^FileSystem,
            FspFileContext^ FileContext,
            UINT64 NewSize,
            bool SetAllocationSize,
            FileInfo^% FileInfo) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Determine whether a file or directory can be deleted.
        ///
        /// This function tests whether a file or directory can be safely deleted.This function does
        /// not need to perform access checks, but may performs tasks such as check for empty
        /// directories, etc.
        ///
        /// This function should <b>NEVER< / b> delete the file or directory in question.Deletion should
        /// happen during Cleanup with the FspCleanupDelete flag set.
        ///
        /// This function gets called when Win32 API's such as DeleteFile or RemoveDirectory are used.
        /// It does not get called when a file or directory is opened with FILE_DELETE_ON_CLOSE.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to test for deletion.</param>
        /// <param name="FileName">  The name of the file or directory to test for deletion.</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 CanDelete(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        ///  Renames a file or directory.
        ///
        /// The kernel mode FSD provides certain guarantees prior to posting a rename operation :
        ///<ul>
        /// <li>A file cannot be renamed if a file with the same name exists and has open handles.</li>
        /// <li>A directory cannot be renamed if it or any of its subdirectories contains a file that
        /// has open handles.</li>
        /// </ul>
        ///
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to be renamed.</param>
        /// <param name="FileName">The current name of the file or directory to rename.</param>
        /// <param name="NewFileName">  The new name for the file or directory.</param>
        /// <param name="ReplaceIfExists"> Whether to replace a file that already exists at NewFileName</param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 Rename(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName,
            String^ NewFileName,
            bool ReplaceIfExists) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        ///Get file or directory security descriptor.
        /// </summary>
        /// <param name="FileSystem>The file system on which this request is posted.</param>
        /// <param name="FileContext"> The file context of the file or directory to get the security descriptor for.</param>
        /// <param name="SecurityDescriptor">
        /// It will recieve the SecurityDescriptor.On input it contains the size of the
        /// security descriptor buffer.On output it will contain the actual size of the security
        /// descriptor copied into the security descriptor buffer.Cannot be NULL.
        /// </param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual UINT32 GetSecurity(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            SecurityDescriptor^% SecurityDescriptor) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        ///  Set file or directory security descriptor.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to set the security descriptor for.</param>
        /// <param name="SecurityInformation"> 
        /// Describes what parts of the file or directory security descriptor should
        /// be modified.
        /// </param>
        /// <param name="ModificationDescriptor">
        /// Describes the modifications to apply to the file or directory security descriptor.
        /// </param>
        /// <returns></returns>
        virtual  UINT32 SetSecurity(FileSystem ^FileSystem,
            FspFileContext^ FileContext,
            UINT32 SecurityInformation,
            SecurityDescriptor^ ModificationDescriptor) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Reads a directory.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the directory to be read.</param>
        /// <param name="Pattern">  
        /// The pattern to match against files in this directory. Can be NULL. The file system
        /// can choose to ignore this parameter as the FSD will always perform its own pattern
        /// matching on the returned results..
        /// </param>
        /// <param name="Marker">
        ///  A file name that marks where in the directory to start reading.Files with names
        ///  that are greater than(not equal to) this marker(in the directory order determined
        ///  by the file system) should be returned.Can be NULL.
        ///</param>
        /// <param name="Buffer">
        /// A helper classs object around the native memory buffer to add directory information.  
        /// </param>        
        /// <returns>STATUS_SUCCESS or error code.STATUS_PENDING is supported allowing for asynchronous operation.</returns>
        virtual  UINT32 ReadDirectory(FileSystem^ FileSystem,
            FspFileContext^ FileContext, 
            String^ Pattern, 
            String^ Marker,
            ReadDirectoryBuffer^ buffer) {
            return NtStatus::InvalidDeviceRequest;
        }


        /// <summary>
        /// Resolve reparse points.
        /// Reparse points are a general mechanism for attaching special behavior to files.
        /// A file or directory can contain a reparse point.A reparse point is data that has
        /// special meaning to the file system, Windows or user applications.For example, NTFS
        /// and Windows use reparse points to implement symbolic links.As another example,
        /// a particular file system may use reparse points to emulate UNIX FIFO's.
        /// This function is expected to resolve as many reparse points as possible.If a reparse
        /// point is encountered that is not understood by the file system further reparse point
        /// resolution should stop; the reparse point data should be returned to the FSD with status
        /// STATUS_REPARSE / reparse - tag.If a reparse point(symbolic link) is encountered that is
        /// understood by the file system but points outside it, the reparse point should be
        /// resolved, but further reparse point resolution should stop; the resolved file name
        /// should be returned to the FSD with status STATUS_REPARSE / IO_REPARSE.
        /// </summary>
        /// <param name="FileSystem">The file system on which this request is posted.</param>
        /// <param name="FileName">The name of the file or directory to have its reparse points resolved.</param>
        /// <param name="ReparsePointIndex">The index of the first reparse point within FileName.</param>
        /// <param name="ResolveLastPathComponent">If FALSE, the last path component of FileName should not be resolved, even
        /// if it is a reparse point that can be resolved.If TRUE, all path components
        /// should be resolved if possible.</param>
        /// <param name="PIoStatus">Pointer to storage that will receive the status to return to the FSD.When
        /// this function succeeds it must set PIoStatus-&gt;Status to STATUS_REPARSE and
        /// PIoStatus-&gt;Information to either IO_REPARSE or the reparse tag.</param>
        /// <param name="ResolveName">that will receive the resolved file name (IO_REPARSE) or
        /// reparse data(reparse tag).If the function returns a file name, it should
        /// not be NULL terminated.
        /// </param>
        /// <returns> STATUS_REPARSE or error code.</returns>
        virtual   UINT32 ResolveReparsePoints(FileSystem^ FileSystem,
            String^ FileName,
            UINT32 ReparsePointIndex,
            bool ResolveLastPathComponent,
            IoStatusBlock^% PIoStatus, String^ ResolveName) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Gets the reparse point.
        /// </summary>
        /// <param name="FileSystem">  The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the reparse point.</param>
        /// <param name="FileName"> The file name of the reparse point.</param>
        /// <param name="Buffer">
        ///  it will receive the results of this operation.If the function 
        ///  returns a symbolic link path, it should not be NULL terminated.
        /// </param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 GetReparsePoint(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName, 
            IntPtr Buffer, 
            ULONG_PTR% Size) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Sets the reparse point.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the reparse point.</param>
        /// <param name="FileName">The file name of the reparse point.</param>
        /// <param name="Buffer">
        /// Pointer to a buffer that contains the data for this operation.If this buffer
        /// contains a symbolic link path, it should not be assumed to be NULL terminated.
        /// </param>
        /// <returns>STATUS_SUCCESS or error code.</returns>
        virtual  UINT32 SetReparsePoint(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName,
            String^ ReparseData) {
            return NtStatus::InvalidDeviceRequest;
        }
        /// <summary>
        /// Deletes the reparse point.
        /// </summary>
        /// <param name="FileSystem"> The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the reparse point.</param>
        /// <param name="FileName">The file name of the reparse point.</param>
        /// <param name="ReparseData"> String that contains the data for this operation.</param>        
        /// <returns></returns>
        virtual  UINT32 DeleteReparsePoint(FileSystem^ FileSystem,
            FspFileContext^ FileContext,
            String^ FileName,
            String^ ReparseData) {
            return NtStatus::InvalidDeviceRequest;
        }

        /// <summary>
        /// Get named streams information.
        /// </summary>
        /// <param name="FileSystem">The file system on which this request is posted.</param>
        /// <param name="FileContext">The file context of the file or directory to get stream information for.</param>
        /// <param name="information">It will receive the stream information</param>
        /// <returns></returns>
        virtual UINT32 GetStreamInfo(FileSystem ^FileSystem,
            FspFileContext FileContext, 
            StreamInformation^ information) {
            return NtStatus::InvalidDeviceRequest;
        }
        virtual UINT32 GetReparsePointByName(
            FileSystem ^FileSystem, FspFileContext Context,
            String^ FileName, BOOLEAN IsDirectory, IntPtr Buffer, ULONG_PTR% PSize) {
            return NtStatus::InvalidDeviceRequest;
        }
    };    


    public ref class FileSystem {
    internal:
        static FSP_FILE_SYSTEM_INTERFACE* nativeInterface=NULL;
        static bool _isInitialized;
        IntPtr _fileSystemPtr;
        IntPtr RootSecurityDescriptor;
        VolumeInformation^ _volumeInformation;
        FileSystemInteface^ _fsInterface;
        FileSystemConfig^ _config;
        IntPtr nativeFsPointer;
    private:
        FileSystem(FileSystemConfig^ config, VolumeInformation^ volumeInformation, FileSystemInteface^ fsInterface);
    public:
        static FileSystem^ BuildFileSystem(FileSystemConfig^ config, VolumeInformation^ volumeInformation, FileSystemInteface^ fsInterface);
        void MountFileSystem(String^ mountPoint);
        void InitNativeInterface();
    };
}