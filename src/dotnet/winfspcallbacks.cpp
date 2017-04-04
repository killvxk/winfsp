#include "winfsp.h"
#include "WinFspCallbacks.h"

using WinFsp::FileSystem;
using WinFsp::FspFileContext;
using WinFsp::VolumeInformation;
using WinFsp::FsHelperOp;

VOID WinFspCallbacks::Cleanup(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName, ULONG Flag) {

    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    fs->_fsInterface->Cleanup(fs, context, gcnew String(FileName), Flag);
}

VOID WinFspCallbacks::Close(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    fs->_fsInterface->Close(fs, context);
}

NTSTATUS WinFspCallbacks::GetVolumeInfo(FSP_FILE_SYSTEM *FspFileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    NTSTATUS result = fs->_fsInterface->GetVolumeInfo(fs, fs->_volumeInformation);
    if (NT_SUCCESS(result)) {
        VolumeInformation^ volumeInfo = fs->_volumeInformation;
        PWSTR volLable = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(volumeInfo->VolumeLabel).ToPointer();
        if (volumeInfo->VolumeLabel != nullptr) {
            VolumeInfo->VolumeLabelLength = (volumeInfo->VolumeLabel->Length > 16 ? 16 : volumeInfo->VolumeLabel ->Length) * sizeof WCHAR;
            memcpy(VolumeInfo->VolumeLabel, volLable, VolumeInfo->VolumeLabelLength);
        }
        else
            VolumeInfo->VolumeLabelLength = 0;
        VolumeInfo->FreeSize = volumeInfo->FreeSize;
        VolumeInfo->TotalSize = volumeInfo->TotalSize;
        return STATUS_SUCCESS;
    }
    else {
        return result;
    }
}

NTSTATUS WinFspCallbacks::SetVolumeLabel(FSP_FILE_SYSTEM *FspFileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    VolumeInformation^ volInfo = gcnew VolumeInformation();
    //ToDo:: Marshalling of native volume info
    return fs->_fsInterface->SetVolumeLabelA(fs, gcnew String(VolumeLabel), volInfo);
}
NTSTATUS WinFspCallbacks::GetSecurityByName(FSP_FILE_SYSTEM *FspFileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    RawSecurityDescriptor ^descriptor;
    UINT32 fileAttrib;
    UINT32 Size;
    NTSTATUS result =  fs->_fsInterface->GetSecurityByName(fs, gcnew String(FileName),fileAttrib,descriptor);
    *PFileAttributes = fileAttrib;
    result =  FsHelperOp::CopyRawDescriptorToPtr(descriptor,IntPtr::IntPtr(SecurityDescriptor),Size);
    *PSecurityDescriptorSize = Size;
    return result;

}
NTSTATUS WinFspCallbacks::Create(FSP_FILE_SYSTEM *FspFileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    String^ fName = gcnew String(FileName);
    FspFileContext ^context= gcnew FspFileContext();
    GCHandle contextHandle = GCHandle::Alloc(context);
    CREATE_RAW_SECURITY_DESC(SecurityDescriptor, descriptor, GetSecurityDescriptorLength(SecurityDescriptor));    
    NTSTATUS result = fs->_fsInterface->Create(fs, fName, CreateOptions, GrantedAccess, FileAttributes, descriptor, AllocationSize, context,context->FileInfo);
    if (NT_SUCCESS(result)) {
        *PFileNode = GCHandle::ToIntPtr(contextHandle).ToPointer();
        WinFsp::FileInfo^ info = context->FileInfo;
        COPY_FILE_INFO(info, FileInfo);
        Console::WriteLine("Info Size {0} Allocation SIze {1} ", info->AllocationSize, AllocationSize);
        return result;
    }
    else {
        contextHandle.Free();
        return result;
    }
    return 0;
}
NTSTATUS WinFspCallbacks::Open(FSP_FILE_SYSTEM *FspFileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    String^ fName = gcnew String(FileName);
    FspFileContext ^context=gcnew FspFileContext();
    GCHandle contextHandle = GCHandle::Alloc(context);
    NTSTATUS result = fs->_fsInterface->Open(fs, fName, CreateOptions, GrantedAccess, context,context->FileInfo);
    if (NT_SUCCESS(result)) {
        *PFileNode = GCHandle::ToIntPtr(contextHandle).ToPointer();
        WinFsp::FileInfo^ info = context->FileInfo;
        COPY_FILE_INFO(info, FileInfo);
        return result;
    }
    else {
        contextHandle.Free();
        return result;
    }
}
NTSTATUS WinFspCallbacks::Overwrite(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileContext);
    NTSTATUS result = fs->_fsInterface->Overwrite(fs, context, FileAttributes, ReplaceFileAttributes,AllocationSize,context->FileInfo);
    if (NT_SUCCESS(result)) {
        WinFsp::FileInfo^ info = context->FileInfo;
        COPY_FILE_INFO(info, FileInfo);
        return result;
    }
    else {        
        return result;
    }
}
NTSTATUS WinFspCallbacks::Read(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferrede) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileContext);        
    ULONG transferredBytes;
    NTSTATUS status=fs->_fsInterface->Read(fs, context,IntPtr::IntPtr(Buffer),Offset,Length,transferredBytes);
    *PBytesTransferrede = transferredBytes;
    return status;


}
NTSTATUS WinFspCallbacks::Write(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    ULONG transferredBytes;        
    NTSTATUS status = fs->_fsInterface->Write(fs, context, IntPtr::IntPtr(Buffer), Offset, Length, WriteToEndOfFile,ConstrainedIo,transferredBytes,context->FileInfo);
    *PBytesTransferred = transferredBytes;
    return status;
}
NTSTATUS WinFspCallbacks::Flush(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode);
    return fs->_fsInterface->Flush(fs, context, context->FileInfo);
}
NTSTATUS WinFspCallbacks::GetFileInfo(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);    
    NTSTATUS result = fs->_fsInterface->GetFileInfo(fs, context, context->FileInfo);
    if (NT_SUCCESS(result)) {
        COPY_FILE_INFO(context->FileInfo, FileInfo);
        return result;
    }
    else {
        return result;
    }
}
NTSTATUS WinFspCallbacks::SetBasicInfo(FSP_FILE_SYSTEM *FspFileSystem,
    PVOID FileContext, UINT32 FileAttributes,
    UINT64 CreationTime, UINT64 LastAccessTime,
    UINT64 LastWriteTime, UINT64 ChangeTime,
    FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileContext);
    NTSTATUS result = fs->_fsInterface->SetBasicInfo(fs, context, FileAttributes, CreationTime, LastAccessTime, LastWriteTime, ChangeTime, context->FileInfo);
    if (NT_SUCCESS(result)) {
        WinFsp::FileInfo^ info = context->FileInfo;
        COPY_FILE_INFO(info, FileInfo);
        return result;
    }
    else {
        return result;
    }
}
NTSTATUS WinFspCallbacks::SetFileSize(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    NTSTATUS result = fs->_fsInterface->SetFileSize(fs, context, NewSize, SetAllocationSize,context->FileInfo);
    if (NT_SUCCESS(result)) {
        WinFsp::FileInfo^ info = context->FileInfo;
        COPY_FILE_INFO(info, FileInfo);
        return result;
    }
    else {
        return result;
    }
}
NTSTATUS WinFspCallbacks::CanDelete(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    return fs->_fsInterface->CanDelete(fs, context, gcnew String(FileName));
}
NTSTATUS WinFspCallbacks::Rename(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    return fs->_fsInterface->Rename(fs, context, gcnew String(FileName), gcnew String(NewFileName),ReplaceIfExists);
}

NTSTATUS WinFspCallbacks::SetSecurity(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileNode0);
    ULONG length = GetSecurityDescriptorLength(ModificationDescriptor);
    CREATE_RAW_SECURITY_DESC(ModificationDescriptor, descriptor, length);
    return fs->_fsInterface->SetSecurity(fs, context, SecurityInformation, descriptor);
}

NTSTATUS WinFspCallbacks::ReadDirectory(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileContext);
    //ConvertSecurityDescriptorToStringSecurityDescriptor(ModificationDescriptor, SDDL_REVISION_1, SecurityInformation, retDesc, Length);
    
    WinFsp::ReadDirectoryBuffer ^buffer = gcnew WinFsp::ReadDirectoryBuffer(Buffer,Length,PBytesTransferred);   

    NTSTATUS result = fs->_fsInterface->ReadDirectory(fs, context, gcnew String(Pattern),gcnew String(Marker), buffer);
    if (NT_SUCCESS(result)) {
        buffer->SetEoF();
        return result;
    }
    else
    {
        return result;
    }
}

NTSTATUS WinFspCallbacks::GetSecurity(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize) {
    FileSystem ^fs = HANDLE_TO_FILESYSTEM(FspFileSystem);
    FspFileContext ^context = HANDLE_TO_FILECONTEXT(FileContext);
    RawSecurityDescriptor^ descriptor = nullptr;
    NTSTATUS result = fs->_fsInterface->GetSecurity(fs, context, descriptor);
    if (NT_SUCCESS(result)) {
        if (descriptor == nullptr)
            return STATUS_INVALID_DEVICE_REQUEST;
        return result;
    }
    else
    {
        return result;
    }
}

// Reparse point
NTSTATUS WinFspCallbacks::ResolveReparsePoints(FSP_FILE_SYSTEM *FspFileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinFspCallbacks::GetReparsePoint(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, PSIZE_T PSize) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}
NTSTATUS WinFspCallbacks::SetReparsePoint(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}
NTSTATUS WinFspCallbacks::DeleteReparsePoint(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}

//Streams
NTSTATUS WinFspCallbacks::GetStreamInfo(FSP_FILE_SYSTEM *FspFileSystem, PVOID FileNode0, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS  WinFspCallbacks::GetReparsePointByName(FSP_FILE_SYSTEM *FileSystem, PVOID Context, PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize) {
    return  STATUS_INVALID_DEVICE_REQUEST;
}