#include "winfsp.h"
#include "winfspcallbacks.h"
#include <sddl.h>




namespace WinFsp {
   
    

    #pragma region Introp funtion defination
    ReadDirectoryBuffer::ReadDirectoryBuffer(PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        _buffer = Buffer;
        _length = Length;
        _pBytesTransferred = PBytesTransferred;
    }

    bool ReadDirectoryBuffer::AddItem(String^ FileName, FileInfo^ Info) {
        PWSTR fileName = (PWSTR)System::Runtime::InteropServices::Marshal::StringToHGlobalUni(FileName).ToPointer();
        UINT8 *DirInfoBuf = (UINT8*)malloc(sizeof(FSP_FSCTL_DIR_INFO) + FileName->Length * sizeof WCHAR);

        FSP_FSCTL_DIR_INFO *DirInfo = (FSP_FSCTL_DIR_INFO *)DirInfoBuf;
        memset(DirInfo->Padding, 0, sizeof DirInfo->Padding);

        DirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + wcslen(fileName) * sizeof(WCHAR));
        COPY_FILE_INFO(Info, &DirInfo->FileInfo);
        memcpy(DirInfo->FileNameBuf, fileName, DirInfo->Size - sizeof(FSP_FSCTL_DIR_INFO));


        //Marshal::FreeHGlobal(IntPtr::IntPtr(fileName));
        return FspFileSystemAddDirInfo(DirInfo, _buffer, _length, _pBytesTransferred);
    }

    UINT64 FsHelperOp::GetFileTime() {
        FILETIME FileTime;
        GetSystemTimeAsFileTime(&FileTime);
        return ((PLARGE_INTEGER)&FileTime)->QuadPart;
    }

    NTSTATUS FsHelperOp::FileSystemResolveReparsePoints(
        FileSystem^ Fs,
        FspFileContext Context,
        String^ FileName, 
        UINT32 ReparsePointIndex, 
        BOOLEAN ResolveLastPathComponent,
        IoStatusBlock% IoStatus, 
        IntPtr Buffer, 
        SIZE_T% Size) {
        FSP_FILE_SYSTEM *fsp = (FSP_FILE_SYSTEM*)Fs->nativeFsPointer.ToPointer();        
        pin_ptr<SIZE_T> sizePtr = &Size;
        IO_STATUS_BLOCK ioStausBlock;
        ioStausBlock.Information = IoStatus.Information;
        ioStausBlock.Status = IoStatus.Status;
        PWSTR fileName = (PWSTR)System::Runtime::InteropServices::Marshal::StringToHGlobalUni(FileName).ToPointer();
        NTSTATUS result =FspFileSystemResolveReparsePoints(fsp, WinFspCallbacks::GetReparsePointByName, 0,
            fileName, ReparsePointIndex, ResolveLastPathComponent,
            &ioStausBlock, Buffer.ToPointer(), sizePtr);
        Marshal::FreeHGlobal(IntPtr::IntPtr(fileName));
        return result;
    }
    int FsHelperOp::CopyBufferToNativeByPin(IntPtr dest, array<byte^>^ data, int sourceOffset, int destOffset, int length) {
        return 0;
    }
    void ReadDirectoryBuffer::SetEoF() {
        FspFileSystemAddDirInfo(0, _buffer, _length, _pBytesTransferred);
    }

    bool SecurityDescriptor::ConvertToString(PSECURITY_DESCRIPTOR desc, ULONG length, String^% descriptorStr, PULONG error) {
        LPSTR sddlPtr = NULL;
        if (ConvertSecurityDescriptorToStringSecurityDescriptorA(desc, SDDL_REVISION_1, BACKUP_SECURITY_INFORMATION, &sddlPtr, &length)) {
            descriptorStr = gcnew String(sddlPtr);
            return true;
        }
        else {
            *error = GetLastError();
            return false;
        }
    }
    bool SecurityDescriptor::ConvertToDescriptor(String^ descriptrStr, PSECURITY_DESCRIPTOR* ptrDesc, PULONG length, PULONG error) {
        LPCSTR sddlPtr = (LPCSTR)System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(descriptrStr).ToPointer();
        if (ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlPtr, SDDL_REVISION_1, ptrDesc, length)) {
            return true;
        }
        else {
            *error = GetLastError();
            return false;
        }
    }
    String^ SecurityDescriptor::ToString()
    {
        PSECURITY_DESCRIPTOR desc = (PSECURITY_DESCRIPTOR)SecurityDescriptorPtr->ToPointer();
        ULONG error = 00;
        if (!ConvertToString(desc, Length, Sddl, &error)) {
            Console::WriteLine("ConvertToString Security descriptor failed to convert Error {0}", error);
            return nullptr;
        }
        return Sddl;

    }
    SecurityDescriptor::~SecurityDescriptor() {
        PSECURITY_DESCRIPTOR sdes = (PSECURITY_DESCRIPTOR)SecurityDescriptorPtr->ToPointer();
        if (sdes != NULL)
            free(sdes);
    }

    SecurityDescriptor::SecurityDescriptor(PSECURITY_DESCRIPTOR ptr, ULONG length) {
        SecurityDescriptorPtr = IntPtr::IntPtr(ptr);
        Length = length;
        ULONG error = 00;
        if (!ConvertToString(ptr, length, Sddl, &error)) {
            Console::WriteLine("ConvertToString Security descriptor failed to convert Error {0}", error);
        }
    }

    SecurityDescriptor::SecurityDescriptor(String^ sddl) {
        PSECURITY_DESCRIPTOR pDes;
        ULONG length;
        ULONG error = 00;

        if (ConvertToDescriptor(sddl, &pDes, &length, &error))
        {
            Length = length;
            SecurityDescriptorPtr = IntPtr::IntPtr(pDes);
            Sddl = sddl;
        }
        Console::WriteLine("ConvertToString Security descriptor failed to convert Error {0}", sddl, error);
    }

    #pragma endregion
    
    #pragma FspFileSystem Definations
    FileSystem::FileSystem(FileSystemConfig^ config, VolumeInformation^ volumeInformation, FileSystemInteface^ fsInterface) {        
        _fsInterface = fsInterface;
        _volumeInformation = volumeInformation;
        _config = config;
    }
    FileSystem^ FileSystem::BuildFileSystem(
        FileSystemConfig^ config,
        VolumeInformation^ volumeInformation,
        FileSystemInteface^ fsInterface) {
        FileSystem ^fs = gcnew FileSystem(config, volumeInformation, fsInterface);
        GCHandle handle = GCHandle::Alloc(fs);
        fs->_fsHandle = GCHandle::ToIntPtr(handle);
        fs->_fsInterface = fsInterface;
        fs->_volumeInformation = volumeInformation;
        fs->_config = config;
        return fs;
    }
    void FileSystem::MountFileSystem(String^ mountPoint) {

        FSP_FSCTL_VOLUME_PARAMS *VolumeParams = (FSP_FSCTL_VOLUME_PARAMS *)malloc(sizeof FSP_FSCTL_VOLUME_PARAMS);
        FSP_FILE_SYSTEM *FileSystem;
        InitNativeInterface();
        memset(VolumeParams, 0, sizeof VolumeParams);        
        PWSTR devicePath = _config->IsNetworkMount ? L"" FSP_FSCTL_NET_DEVICE_NAME : L"" FSP_FSCTL_DISK_DEVICE_NAME;
        PSECURITY_DESCRIPTOR RootSecurity;
        ULONG RootSecuritySize;
        bool freeSddl;
        PWSTR RootSddl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
        if (_config->RootSecurityDescriptor == nullptr)
            _config->RootSecurityDescriptor = "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
        else {
            RootSddl = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(_config->RootSecurityDescriptor).ToPointer();
            freeSddl = true;
        }
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(RootSddl, SDDL_REVISION_1,
            &RootSecurity, &RootSecuritySize)) {
            free(RootSddl);
            throw gcnew Exception("Unable to create security descriptor for Root. Error code -" + FspNtStatusFromWin32(GetLastError()));
        }
        //    if(ToFree)
        //        free(RootSddl);
        RootSecurityDescriptor = IntPtr::IntPtr(RootSecurity);

        if (_config->SectorSize == 0)
            _config->SectorSize = DEFAULT_SECTOR_SIZE;
        VolumeParams->SectorSize = _config->SectorSize;

        if (_config->SectorPerAllocationUnit == 0)
            _config->SectorPerAllocationUnit = DEFAULT_SECTORS_PER_ALLOCATION_UNIT;
        VolumeParams->SectorsPerAllocationUnit = _config->SectorPerAllocationUnit;

        if (_config->VolumeCreationTime == 0)
            _config->VolumeCreationTime = FsHelperOp::GetFileTime();
        VolumeParams->VolumeCreationTime = _config->VolumeCreationTime;

        if (_config->SerialNumber == 0)
            _config->SerialNumber = (UINT32)(_config->VolumeCreationTime / (10000 * 1000));

        VolumeParams->VolumeSerialNumber = _config->SerialNumber;
        VolumeParams->FileInfoTimeout = _config->FileInfoTimeOut;

        VolumeParams->CaseSensitiveSearch = _config->CaseSensitive;
        VolumeParams->CasePreservedNames = _config->CaseSensitiveNames;
        VolumeParams->UnicodeOnDisk = _config->UnicodeOnDisk;
        VolumeParams->PersistentAcls = _config->PresistACL;
        VolumeParams->ReparsePoints = _config->ReparsePoints;
        VolumeParams->ReparsePointsAccessCheck = _config->ReparsePointAccessChecks;
        VolumeParams->NamedStreams = _config->NameSteam;
        VolumeParams->PostCleanupWhenModifiedOnly = _config->PostCleanupWhenModifiedOnly;
        VolumeParams->ReadOnlyVolume = _config->IsReadOnlyVolume;

        VolumeParams->UmFileContextIsUserContext2 = true;

        if (_config->VolumePrefix != nullptr) {
            PWSTR  volumePrefixStr = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(_config->VolumePrefix).ToPointer();
            wcscpy_s(VolumeParams->Prefix, _config->VolumePrefix->Length, volumePrefixStr);
            free(volumePrefixStr);
        }
        if (_config->FileSystemName == nullptr)
            _config->FileSystemName = "WINFSP";
        if (_config->FileSystemName->Length > 16) {
            _config->FileSystemName = _config->FileSystemName->Substring(0, 15);
        }
        FSP_FILE_SYSTEM_INTERFACE* inter = nativeInterface;        
        PWSTR fsName = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(_config->FileSystemName).ToPointer();
        memcpy(VolumeParams->FileSystemName, fsName, wcslen(fsName) * sizeof WCHAR);
        VolumeParams->FileSystemName[_config->FileSystemName->Length] = L'\0';        
        
        NTSTATUS result = FspFileSystemCreate(devicePath, VolumeParams, nativeInterface, &FileSystem);        
        if (result != 0)
            throw gcnew Exception("FileSystem Create failed with error code - 0x" + result.ToString("X"));
        _fileSystemPtr = IntPtr::IntPtr(FileSystem);     
        FileSystem->UserContext = _fsHandle.ToPointer();
        if (nullptr != mountPoint && mountPoint->Length >= 0)
        {
            PWSTR nativeMountPoint = (PWSTR)System::Runtime::InteropServices::Marshal::StringToHGlobalUni(mountPoint).ToPointer();
            result = FspFileSystemSetMountPoint((FSP_FILE_SYSTEM*)_fileSystemPtr.ToPointer(), mountPoint == "*" ? 0 : nativeMountPoint);
            if (!NT_SUCCESS(result))
                throw gcnew Exception("Set mount file systen failed with error code - 0x" + result.ToString("X"));
        }
        result = FspFileSystemStartDispatcher((FSP_FILE_SYSTEM*)_fileSystemPtr.ToPointer(), 10);
        if (result != 0)
            throw gcnew Exception("FileSystem Create failed with error code - 0x" + result.ToString("X"));
    }
    void FileSystem::InitNativeInterface() {
        if (!_isInitialized) {
            nativeInterface = (FSP_FILE_SYSTEM_INTERFACE*)malloc(sizeof(FSP_FILE_SYSTEM_INTERFACE));
            //Functions from initialVersions
            nativeInterface->CanDelete = WinFspCallbacks::CanDelete;
            nativeInterface->Cleanup = WinFspCallbacks::Cleanup;
            nativeInterface->Close = WinFspCallbacks::Close;
            nativeInterface->Create = WinFspCallbacks::Create;
            nativeInterface->Flush = WinFspCallbacks::Flush;
            nativeInterface->GetFileInfo = WinFspCallbacks::GetFileInfo;
            nativeInterface->GetSecurity = WinFspCallbacks::GetSecurity;
            nativeInterface->GetSecurityByName = WinFspCallbacks::GetSecurityByName;
            nativeInterface->GetVolumeInfo = WinFspCallbacks::GetVolumeInfo;
            nativeInterface->Open = WinFspCallbacks::Open;
            nativeInterface->Overwrite = WinFspCallbacks::Overwrite;
            nativeInterface->Read = WinFspCallbacks::Read;
            nativeInterface->ReadDirectory = WinFspCallbacks::ReadDirectory;
            nativeInterface->Rename = WinFspCallbacks::Rename;
            nativeInterface->SetBasicInfo = WinFspCallbacks::SetBasicInfo;
            nativeInterface->SetFileSize = WinFspCallbacks::SetFileSize;
            nativeInterface->SetSecurity = WinFspCallbacks::SetSecurity;
            nativeInterface->SetVolumeLabel = WinFspCallbacks::SetVolumeLabelA;
            nativeInterface->Write = WinFspCallbacks::Write;
            nativeInterface->GetStreamInfo = WinFspCallbacks::GetStreamInfo;
            nativeInterface->SetReparsePoint = WinFspCallbacks::SetReparsePoint;
            nativeInterface->ResolveReparsePoints = WinFspCallbacks::ResolveReparsePoints;
            nativeInterface->GetReparsePoint = WinFspCallbacks::GetReparsePoint;
            nativeInterface->DeleteReparsePoint = WinFspCallbacks::DeleteReparsePoint;
        }
    }

    #pragma endregion
    /*    
    WinFsp::WinFsp(WinFspConfig^ FsConfig, WinFspFileSystem::WinFspMinimalOperation^ operations, WinFspFileSystem::WinFspReparseOperation^ reparseOpeation, WinFspFileSystem::WinFspStreamOperation^ stream)
    {
        FSP_FSCTL_VOLUME_PARAMS *VolumeParams = (FSP_FSCTL_VOLUME_PARAMS *)malloc(sizeof FSP_FSCTL_VOLUME_PARAMS);
        FSP_FILE_SYSTEM *FileSystem;
        memset(VolumeParams, 0, sizeof VolumeParams);
        VolumeParamsPtr = IntPtr::IntPtr(VolumeParams);
        PWSTR devicePath = FsConfig->IsNetworkMount ? L"" FSP_FSCTL_NET_DEVICE_NAME : L"" FSP_FSCTL_DISK_DEVICE_NAME;
        PSECURITY_DESCRIPTOR RootSecurity;
        ULONG RootSecuritySize;
        PWSTR RootSddl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
        bool ToFree;
    
        if (FsConfig->RootSecurityDescriptor == nullptr)
            FsConfig->RootSecurityDescriptor = "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
        else {
            RootSddl = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(FsConfig->RootSecurityDescriptor).ToPointer();
            ToFree = true;
        }
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(RootSddl, SDDL_REVISION_1,
            &RootSecurity, &RootSecuritySize)) {
            free(RootSddl);
            throw gcnew Exception("Unable to create security descriptor for Root. Error code -" + FspNtStatusFromWin32(GetLastError()));
        }
    //    if(ToFree)
    //        free(RootSddl);
        RootSecurityDescriptor = IntPtr::IntPtr(RootSecurity);

        if (FsConfig->SectorSize == 0)
            FsConfig->SectorSize = DEFAULT_SECTOR_SIZE;
        VolumeParams->SectorSize = FsConfig->SectorSize;

        if (FsConfig->SectorPerAllocationUnit == 0)
            FsConfig->SectorPerAllocationUnit = DEFAULT_SECTORS_PER_ALLOCATION_UNIT;
        VolumeParams->SectorsPerAllocationUnit = FsConfig->SectorPerAllocationUnit;

        if (FsConfig->VolumeCreationTime == 0)
            FsConfig->VolumeCreationTime = GetFileTime();
        VolumeParams->VolumeCreationTime = FsConfig->VolumeCreationTime;

        if (FsConfig->SerialNumber == 0)
            FsConfig->SerialNumber = (UINT32)(FsConfig->VolumeCreationTime / (10000 * 1000));
        
        VolumeParams->VolumeSerialNumber          = FsConfig->SerialNumber;
        VolumeParams->FileInfoTimeout             = FsConfig->FileInfoTimeOut;

        VolumeParams->CaseSensitiveSearch         = FsConfig->CaseSensitive;
        VolumeParams->CasePreservedNames          = FsConfig->CaseSensitiveNames;
        VolumeParams->UnicodeOnDisk               = FsConfig->UnicodeOnDisk;
        VolumeParams->PersistentAcls              = FsConfig->PresistACL;
        VolumeParams->ReparsePoints               = FsConfig->ReparsePoints;
        VolumeParams->ReparsePointsAccessCheck    = FsConfig->ReparsePointAccessChecks;
        VolumeParams->NamedStreams                = FsConfig->NameSteam;
        VolumeParams->PostCleanupWhenModifiedOnly = FsConfig->PostCleanupWhenModifiedOnly;
        VolumeParams->ReadOnlyVolume              = FsConfig->IsReadOnlyVolume;

        VolumeParams->UmFileContextIsUserContext2= true;

        if (FsConfig->VolumePrefix != nullptr) {
            PWSTR  volumePrefixStr = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(FsConfig->VolumePrefix).ToPointer();
            wcscpy_s(VolumeParams->Prefix, FsConfig->VolumePrefix->Length, volumePrefixStr);
            free(volumePrefixStr);
        }
        if (FsConfig->FileSystemName == nullptr)
            FsConfig->FileSystemName = "WINFSP";
        if (FsConfig->FileSystemName->Length > 16) {
            FsConfig->FileSystemName = FsConfig->FileSystemName->Substring(0, 15);
        }
        FSP_FILE_SYSTEM_INTERFACE* inter = (FSP_FILE_SYSTEM_INTERFACE*)malloc(sizeof FSP_FILE_SYSTEM_INTERFACE);
        FspIntefacePtr = IntPtr::IntPtr(inter);
        memset(inter, 0, sizeof FSP_FILE_SYSTEM_INTERFACE);        
        PWSTR fsName = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(FsConfig->FileSystemName).ToPointer();
        memcpy(VolumeParams->FileSystemName, fsName, wcslen(fsName) * sizeof WCHAR);
        VolumeParams->FileSystemName[FsConfig->FileSystemName->Length] = L'\0';
        InitBasicFunction(inter);
        if (reparseOpeation)
            InitReparseFunction(inter);
        if (stream)
            InitStreamFunction(inter);
        NTSTATUS result=  FspFileSystemCreate(devicePath, VolumeParams, (FSP_FILE_SYSTEM_INTERFACE*)FspIntefacePtr.ToPointer(), &FileSystem);
        FileSystemPtr = IntPtr::IntPtr(FileSystem);
        WinFspInstances::Instance->AddFileSystem((UINT_PTR)FileSystemPtr.ToPointer(), gcnew WinFspFileSystem(FileSystem,operations, reparseOpeation,stream));
        if (result != 0)
            throw gcnew Exception("FileSystem Create failed with error code " + result);


    }


    
    NTSTATUS  WinFsp::WinFspCreateFileSystem() {

        return 0;
    }
    NTSTATUS  WinFsp::WinFspMountFileSystem() {
        return 0;
    }

    NTSTATUS WinFsp::StartFs(String^ mountPoint) {
        NTSTATUS Result = 0;
        if (nullptr != mountPoint && mountPoint->Length>=0)
        {
            PWSTR nativeMountPoint = (PWSTR) System::Runtime::InteropServices::Marshal::StringToHGlobalUni(mountPoint).ToPointer();
            Result = FspFileSystemSetMountPoint((FSP_FILE_SYSTEM*)FileSystemPtr.ToPointer(), mountPoint=="*" ? 0 : nativeMountPoint);
            if (!NT_SUCCESS(Result))        
                return Result;            
        }
        Result = FspFileSystemStartDispatcher((FSP_FILE_SYSTEM*)FileSystemPtr.ToPointer(), 10);
        if (Result != 0) 
            throw gcnew Exception("FileSystem Create failed with error code " + Result);
    }
    void WinFsp::DeleteFs() {
        FspFileSystemStopDispatcher((FSP_FILE_SYSTEM*)FileSystemPtr.ToPointer());
    }
    void WinFsp::StopFs() {

    }

    WinFspFileSystem^ WinFspInstances::GetFileSystem(UINT_PTR Key) {
        WinFspFileSystem^ fileSystem;
        _fspCollection->TryGetValue(Key, fileSystem);
        return fileSystem;
    }
    bool WinFspInstances::AddFileSystem(UINT_PTR Key, WinFspFileSystem^ fs) {
        return _fspCollection->TryAdd(Key, fs);
    }
    
    VOID WinFspFunc::Cleanup(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName,ULONG Flag) {

        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);        
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        fs->BasicOperaions->Cleanup(fs, context, gcnew String(FileName), Flag);
    }

    VOID WinFspFunc::Close(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        fs->BasicOperaions->Close(fs, context);
    }

    NTSTATUS WinFspFunc::GetVolumeInfo(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        VolInfo^ volInfo = nullptr;
        NTSTATUS result = fs->BasicOperaions->GetVolumeInfo(fs, volInfo);
        if (NT_SUCCESS(result) && volInfo != nullptr) {
            PWSTR volLable = (PWSTR)InteropServices::Marshal::StringToHGlobalUni(volInfo->VolumeLable).ToPointer();
            if (volInfo->VolumeLable != nullptr) {
                VolumeInfo->VolumeLabelLength = (volInfo->VolumeLable->Length > 16 ? 16 : volInfo->VolumeLable->Length) * sizeof WCHAR;
                memcpy(VolumeInfo->VolumeLabel, volLable, VolumeInfo->VolumeLabelLength);
            }
            else
                VolumeInfo->VolumeLabelLength = 0;
            VolumeInfo->FreeSize = volInfo->FreeSize;
            VolumeInfo->TotalSize = volInfo->TotalSize;
            return STATUS_SUCCESS;
        }
        else {
            return STATUS_INVALID_DEVICE_REQUEST;
        }
    }
    NTSTATUS WinFspFunc::SetVolumeLabel(FSP_FILE_SYSTEM *FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        VolInfo^ volInfo = gcnew VolInfo();
        return fs->BasicOperaions->SetVolumeLabelA(fs, gcnew String(VolumeLabel), volInfo);
    }
    NTSTATUS WinFspFunc::GetSecurityByName(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize) {
        return STATUS_NOT_IMPLEMENTED;
    }
    NTSTATUS WinFspFunc::Create(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        WinFspNet::FileInfo^ file = gcnew WinFspNet::FileInfo();
        UINT_PTR retFileNode = 0;
        String^ fName = gcnew String(FileName);
        FileOpenContext ^context = fs->Nodes->CreateNewContext(fName);
        SecuirtyDescriptor ^desciptor = gcnew SecuirtyDescriptor(SecurityDescriptor, GetSecurityDescriptorLength(SecurityDescriptor));
        NTSTATUS result = fs->BasicOperaions->Create(fs, fName, CreateOptions, GrantedAccess, FileAttributes, desciptor, AllocationSize,context);
        if (NT_SUCCESS(result)) {
            *PFileNode = (PVOID)context->NodeId;
            WinFspNet::FileInfo^ info = context->Node->Info;
            COPY_FILE_INFO(info, FileInfo);
            Console::WriteLine("Info Size {0} Allocation SIze {1} ", info->AllocationSize, AllocationSize);
            return result;
        }
        else {
            fs->Nodes->RemoveContext(context->NodeId);
            return result;
        }        
        return 0;
    }
    NTSTATUS WinFspFunc::Open(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        OpenFileInfo ^file = gcnew OpenFileInfo(FileInfo);
        String^ fName = gcnew String(FileName);
        UINT_PTR retFileNode = 0;
        FileOpenContext ^context = fs->Nodes->CreateNewContext(fName);
        NTSTATUS result= fs->BasicOperaions->Open(fs,fName,CreateOptions, GrantedAccess, context);
        if (NT_SUCCESS(result)) {
            *PFileNode = (PVOID)context->NodeId;        
            WinFspNet::FileInfo^ info = context->Node->Info;
            COPY_FILE_INFO(info, FileInfo);            
            return result;
        }
        else {
            fs->Nodes->RemoveContext(context->NodeId);
            return result;
        }
    }
    NTSTATUS WinFspFunc::Overwrite(FSP_FILE_SYSTEM *FileSystem,PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize,FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);        
        UINT_PTR retFileNode = 0;
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileContext);
        NTSTATUS result = fs->BasicOperaions->Overwrite(fs, context, FileAttributes, ReplaceFileAttributes);
        if (NT_SUCCESS(result)) {            
            WinFspNet::FileInfo^ info = context->Node->Info;
            COPY_FILE_INFO(info, FileInfo);
            return result;
        }
        else {
            fs->Nodes->RemoveContext(context->NodeId);
            return result;
        }
    }
    NTSTATUS WinFspFunc::Read(FSP_FILE_SYSTEM *FileSystem,PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,PULONG PBytesTransferrede) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        ReadFileBuffer ^buffer = gcnew ReadFileBuffer(Buffer, Length, Offset, PBytesTransferrede);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileContext);
        return fs->BasicOperaions->Read(fs, context, buffer);
        
        
    }
    NTSTATUS WinFspFunc::Write(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        WriteFileBuffer ^buffer = gcnew WriteFileBuffer(WriteToEndOfFile, ConstrainedIo,Buffer,Offset, Length, PBytesTransferred);        
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        return fs->BasicOperaions->Write(fs, context, buffer);
    }
    NTSTATUS WinFspFunc::Flush(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode);
        return fs->BasicOperaions->Flush(fs, context);
    }
    NTSTATUS WinFspFunc::GetFileInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        OpenFileInfo ^file = gcnew OpenFileInfo(FileInfo);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        WinFspNet::FileInfo^ info = context->Node->Info;
        NTSTATUS result = fs->BasicOperaions->GetFileInfo(fs, context,info);
        if (NT_SUCCESS(result)) {                
            COPY_FILE_INFO(info, FileInfo);
            return result;
        }
        else {        
            return result;
        }
    }
    NTSTATUS WinFspFunc::SetBasicInfo(FSP_FILE_SYSTEM *FileSystem,
        PVOID FileContext, UINT32 FileAttributes,
        UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
        FSP_FSCTL_FILE_INFO *FileInfo) {
        FspFileSYs ^fs = GET_FILESYSTEM(FileSystem);
        OpenFileInfo ^file = gcnew OpenFileInfo(FileInfo);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileContext);
        NTSTATUS result= fs->BasicOperaions->SetBasicInfo(fs, context, FileAttributes, CreationTime, LastAccessTime, LastWriteTime);
        if (NT_SUCCESS(result)) {
            WinFspNet::FileInfo^ info = context->Node->Info;
            COPY_FILE_INFO(info, FileInfo);
            return result;
        }
        else {            
            return result;
        }
    }
    NTSTATUS WinFspFunc::SetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        OpenFileInfo ^file = gcnew OpenFileInfo(FileInfo);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        NTSTATUS result= fs->BasicOperaions->SetFileSize(fs, context, NewSize, SetAllocationSize);
        if (NT_SUCCESS(result)) {
            WinFspNet::FileInfo^ info = context->Node->Info;
            COPY_FILE_INFO(info, FileInfo);
            return result;
        }
        else {            
            return result;
        }
    }
    NTSTATUS WinFspFunc::CanDelete(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        return fs->BasicOperaions->CanDelete(fs, context, gcnew String(FileName));
    }
    NTSTATUS WinFspFunc::Rename(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        return fs->BasicOperaions->Rename(fs, context, gcnew String(FileName), gcnew String(NewFileName), ReplaceIfExists);
    }
    NTSTATUS WinFspFunc::SetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileNode0);
        ULONG length = GetSecurityDescriptorLength(ModificationDescriptor);
        WinFspNet::SecuirtyDescriptor^ descriptor = gcnew  WinFspNet::SecuirtyDescriptor(ModificationDescriptor, length);          
        return fs->BasicOperaions->SetSecurity(fs, context, SecurityInformation, descriptor);
    }
    NTSTATUS WinFspFunc::ReadDirectory(FSP_FILE_SYSTEM *FileSystem,PVOID FileContext, PWSTR Pattern, PWSTR Marker,PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        //ConvertSecurityDescriptorToStringSecurityDescriptor(ModificationDescriptor, SDDL_REVISION_1, SecurityInformation, retDesc, Length);
        ReadDirectoryContext ^contextDir = gcnew ReadDirectoryContext();
        contextDir->BufferRet = Buffer;
        contextDir->Length = Length;
        contextDir->Marker = gcnew String(Marker);
        contextDir->PBytesTransferred = PBytesTransferred;

        ReadDirectoryBuffer ^buffer = gcnew ReadDirectoryBuffer(contextDir);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileContext);
        NTSTATUS result = fs->BasicOperaions->ReadDirectory(fs, context, gcnew String(Pattern),buffer);
        if (NT_SUCCESS(result)) {
            buffer->SetEof();
            return result;
        }
        else
        {
            return result;
        }
    }
    NTSTATUS WinFspFunc::GetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize) {
        WinFspFileSystem ^fs = GET_FILESYSTEM(FileSystem);
        FileOpenContext ^context = fs->Nodes->GetFileOpenContext((UINT_PTR)FileContext);
        SecuirtyDescriptor^ descriptor=nullptr;
        NTSTATUS result = fs->BasicOperaions->GetSecurity(fs, context,descriptor);
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
    NTSTATUS WinFspFunc::ResolveReparsePoints(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize) {
        return  0;
    }

    NTSTATUS WinFspFunc::GetReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, PSIZE_T PSize) {
        return  0;
    }
    NTSTATUS WinFspFunc::SetReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size) {
        return  0;
    }
    NTSTATUS WinFspFunc::DeleteReparsePoint(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PWSTR FileName, PVOID Buffer, SIZE_T Size) {
        return  0;
    }

    //Streams
    NTSTATUS WinFspFunc::GetStreamInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileNode0, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        return  0;
    }*/
}