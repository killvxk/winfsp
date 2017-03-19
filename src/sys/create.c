/**
 * @file sys/create.c
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

#include <sys/driver.h>

static NTSTATUS FspFsctlCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS FspFsvrtCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS FspFsvolCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS FspFsvolCreateNoLock(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    BOOLEAN MainFileOpen);
FSP_IOPREP_DISPATCH FspFsvolCreatePrepare;
FSP_IOCMPL_DISPATCH FspFsvolCreateComplete;
static NTSTATUS FspFsvolCreateTryOpen(PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response,
    FSP_FILE_NODE *FileNode, FSP_FILE_DESC *FileDesc, PFILE_OBJECT FileObject,
    BOOLEAN FlushImage);
static VOID FspFsvolCreatePostClose(FSP_FILE_DESC *FileDesc);
static FSP_IOP_REQUEST_FINI FspFsvolCreateRequestFini;
static FSP_IOP_REQUEST_FINI FspFsvolCreateTryOpenRequestFini;
static FSP_IOP_REQUEST_FINI FspFsvolCreateOverwriteRequestFini;
static NTSTATUS FspFsvolCreateSharingViolationOplock(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    BOOLEAN CanWait);
static BOOLEAN FspFsvolCreateOpenOrOverwriteOplock(PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response,
    PNTSTATUS PResult);
static VOID FspFsvolCreateOpenOrOverwriteOplockPrepare(
    PVOID Context, PIRP Irp);
static VOID FspFsvolCreateOpenOrOverwriteOplockComplete(
    PVOID Context, PIRP Irp);
FSP_DRIVER_DISPATCH FspCreate;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, FspFsctlCreate)
#pragma alloc_text(PAGE, FspFsvrtCreate)
#pragma alloc_text(PAGE, FspFsvolCreate)
#pragma alloc_text(PAGE, FspFsvolCreateNoLock)
#pragma alloc_text(PAGE, FspFsvolCreatePrepare)
#pragma alloc_text(PAGE, FspFsvolCreateComplete)
#pragma alloc_text(PAGE, FspFsvolCreateTryOpen)
#pragma alloc_text(PAGE, FspFsvolCreatePostClose)
#pragma alloc_text(PAGE, FspFsvolCreateRequestFini)
#pragma alloc_text(PAGE, FspFsvolCreateTryOpenRequestFini)
#pragma alloc_text(PAGE, FspFsvolCreateOverwriteRequestFini)
#pragma alloc_text(PAGE, FspFsvolCreateSharingViolationOplock)
#pragma alloc_text(PAGE, FspFsvolCreateOpenOrOverwriteOplock)
#pragma alloc_text(PAGE, FspFsvolCreateOpenOrOverwriteOplockPrepare)
#pragma alloc_text(PAGE, FspFsvolCreateOpenOrOverwriteOplockComplete)
#pragma alloc_text(PAGE, FspCreate)
#endif

#define PREFIXW                         L"" FSP_FSCTL_VOLUME_PARAMS_PREFIX
#define PREFIXW_SIZE                    (sizeof PREFIXW - sizeof(WCHAR))

enum
{
    /* Create */
    RequestDeviceObject                 = 0,
    RequestFileDesc                     = 1,
    RequestAccessToken                  = 2,
    RequestProcess                      = 3,

    /* TryOpen/Overwrite */
    //RequestDeviceObject               = 0,
    //RequestFileDesc                   = 1,
    RequestFileObject                   = 2,
    RequestState                        = 3,

    /* RequestState */
    RequestPending                      = 0,
    RequestProcessing                   = 1,
};

static NTSTATUS FspFsctlCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    NTSTATUS Result;
    PFILE_OBJECT FileObject = IrpSp->FileObject;

    if (0 == FileObject->RelatedFileObject &&
        PREFIXW_SIZE <= FileObject->FileName.Length &&
        RtlEqualMemory(PREFIXW, FileObject->FileName.Buffer, PREFIXW_SIZE))
        Result = FspVolumeCreate(DeviceObject, Irp, IrpSp);
    else
    {
        Result = STATUS_SUCCESS;
        Irp->IoStatus.Information = FILE_OPENED;
    }

    return Result;
}

static NTSTATUS FspFsvrtCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    Irp->IoStatus.Information = FILE_OPENED;
    return STATUS_SUCCESS;
}

static NTSTATUS FspFsvolCreate(
    PDEVICE_OBJECT FsvolDeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    NTSTATUS Result = STATUS_SUCCESS;
    BOOLEAN MainFileOpen = FspMainFileOpenCheck(Irp);

    if (!MainFileOpen)
    {
        FspFsvolDeviceFileRenameAcquireShared(FsvolDeviceObject);
        try
        {
            Result = FspFsvolCreateNoLock(FsvolDeviceObject, Irp, IrpSp, FALSE);
        }
        finally
        {
            if (FSP_STATUS_IOQ_POST != Result)
                FspFsvolDeviceFileRenameRelease(FsvolDeviceObject);
        }
    }
    else
        Result = FspFsvolCreateNoLock(FsvolDeviceObject, Irp, IrpSp, TRUE);

    return Result;
}

static NTSTATUS FspFsvolCreateNoLock(
    PDEVICE_OBJECT FsvolDeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    BOOLEAN MainFileOpen)
{
    PAGED_CODE();

    NTSTATUS Result;
    FSP_FSVOL_DEVICE_EXTENSION *FsvolDeviceExtension = FspFsvolDeviceExtension(FsvolDeviceObject);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFILE_OBJECT RelatedFileObject = FileObject->RelatedFileObject;
    UNICODE_STRING FileName = FileObject->FileName;

    /* open the volume object? */
    if ((0 == RelatedFileObject || !FspFileNodeIsValid(RelatedFileObject->FsContext)) &&
        0 == FileName.Length)
    {
        if (0 != FsvolDeviceExtension->FsvrtDeviceObject)
#pragma prefast(disable:28175, "We are a filesystem: ok to access Vpb")
            FileObject->Vpb = FsvolDeviceExtension->FsvrtDeviceObject->Vpb;

        Irp->IoStatus.Information = FILE_OPENED;
        return STATUS_SUCCESS;
    }

    PACCESS_STATE AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;
    ULONG CreateDisposition = (IrpSp->Parameters.Create.Options >> 24) & 0xff;
    ULONG CreateOptions = IrpSp->Parameters.Create.Options;
    USHORT FileAttributes = IrpSp->Parameters.Create.FileAttributes;
    PSECURITY_DESCRIPTOR SecurityDescriptor = AccessState->SecurityDescriptor;
    ULONG SecurityDescriptorSize = 0;
    UINT64 AllocationSize = Irp->Overlay.AllocationSize.QuadPart;
    UINT64 AllocationUnit;
    ACCESS_MASK DesiredAccess = AccessState->RemainingDesiredAccess;
    ACCESS_MASK GrantedAccess = AccessState->PreviouslyGrantedAccess;
    USHORT ShareAccess = IrpSp->Parameters.Create.ShareAccess;
    PFILE_FULL_EA_INFORMATION EaBuffer = Irp->AssociatedIrp.SystemBuffer;
    //ULONG EaLength = IrpSp->Parameters.Create.EaLength;
    ULONG Flags = IrpSp->Flags;
    KPROCESSOR_MODE RequestorMode =
        FlagOn(Flags, SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode;
    BOOLEAN CaseSensitive =
        //BooleanFlagOn(Flags, SL_CASE_SENSITIVE) ||
        !!FsvolDeviceExtension->VolumeParams.CaseSensitiveSearch;
    BOOLEAN HasTraversePrivilege =
        BooleanFlagOn(AccessState->Flags, TOKEN_HAS_TRAVERSE_PRIVILEGE);
    BOOLEAN HasBackupPrivilege =
        BooleanFlagOn(AccessState->Flags, TOKEN_HAS_BACKUP_PRIVILEGE);
    BOOLEAN HasRestorePrivilege =
        BooleanFlagOn(AccessState->Flags, TOKEN_HAS_RESTORE_PRIVILEGE);
    BOOLEAN HasTrailingBackslash = FALSE;
    FSP_FILE_NODE *FileNode, *RelatedFileNode;
    FSP_FILE_DESC *FileDesc;
    UNICODE_STRING MainFileName = { 0 }, StreamPart = { 0 };
    ULONG StreamType = FspFileNameStreamTypeNone;
    FSP_FSCTL_TRANSACT_REQ *Request;

    /* cannot open files by fileid */
    if (FlagOn(CreateOptions, FILE_OPEN_BY_FILE_ID))
        return STATUS_NOT_IMPLEMENTED;

    /* no EA support currently */
    if (0 != EaBuffer)
        return STATUS_EAS_NOT_SUPPORTED;

    /* cannot open a paging file */
    if (FlagOn(Flags, SL_OPEN_PAGING_FILE))
        return STATUS_ACCESS_DENIED;

    /* check create options */
    if (FlagOn(CreateOptions, FILE_NON_DIRECTORY_FILE) &&
        FlagOn(CreateOptions, FILE_DIRECTORY_FILE))
        return STATUS_INVALID_PARAMETER;

    /* do not allow the temporary bit on a directory */
    if (FlagOn(CreateOptions, FILE_DIRECTORY_FILE) &&
        FlagOn(FileAttributes, FILE_ATTRIBUTE_TEMPORARY) &&
        (FILE_CREATE == CreateDisposition || FILE_OPEN_IF == CreateDisposition))
        return STATUS_INVALID_PARAMETER;

    /* check security descriptor validity */
    if (0 != SecurityDescriptor)
    {
#if 0
        /* captured security descriptor is always valid */
        if (!RtlValidSecurityDescriptor(SecurityDescriptor))
            return STATUS_INVALID_PARAMETER;
#endif
        SecurityDescriptorSize = RtlLengthSecurityDescriptor(SecurityDescriptor);
    }

    /* align allocation size */
    AllocationUnit = FsvolDeviceExtension->VolumeParams.SectorSize *
        FsvolDeviceExtension->VolumeParams.SectorsPerAllocationUnit;
    AllocationSize = (AllocationSize + AllocationUnit - 1) / AllocationUnit * AllocationUnit;

    /* according to fastfat, filenames that begin with two backslashes are ok */
    if (sizeof(WCHAR) * 2 <= FileName.Length &&
        L'\\' == FileName.Buffer[1] && L'\\' == FileName.Buffer[0])
    {
        FileName.Length -= sizeof(WCHAR);
        FileName.MaximumLength -= sizeof(WCHAR);
        FileName.Buffer++;
    }

    /* is this a relative or absolute open? */
    if (0 != RelatedFileObject)
    {
        RelatedFileNode = RelatedFileObject->FsContext;

        /*
         * Accesses of RelatedFileNode->FileName are protected
         * by FSP_FSVOL_DEVICE_EXTENSION::FileRenameResource.
         */

        /* is this a valid RelatedFileObject? */
        if (!FspFileNodeIsValid(RelatedFileNode))
            return STATUS_OBJECT_PATH_NOT_FOUND;

        /* must be a relative path */
        if (sizeof(WCHAR) <= FileName.Length && L'\\' == FileName.Buffer[0])
            return STATUS_INVALID_PARAMETER; /* IFSTEST */

        BOOLEAN AppendBackslash =
            sizeof(WCHAR) * 2/* not empty or root */ <= RelatedFileNode->FileName.Length &&
            sizeof(WCHAR) <= FileName.Length && L':' != FileName.Buffer[0];
        Result = FspFileNodeCreate(FsvolDeviceObject,
            RelatedFileNode->FileName.Length + AppendBackslash * sizeof(WCHAR) + FileName.Length,
            &FileNode);
        if (!NT_SUCCESS(Result))
            return Result;

        Result = RtlAppendUnicodeStringToString(&FileNode->FileName, &RelatedFileNode->FileName);
        ASSERT(NT_SUCCESS(Result));
        if (AppendBackslash)
        {
            Result = RtlAppendUnicodeToString(&FileNode->FileName, L"\\");
            ASSERT(NT_SUCCESS(Result));
        }
    }
    else
    {
        /* must be an absolute path */
        if (sizeof(WCHAR) <= FileName.Length && L'\\' != FileName.Buffer[0])
            return STATUS_OBJECT_NAME_INVALID;

        Result = FspFileNodeCreate(FsvolDeviceObject,
            FileName.Length,
            &FileNode);
        if (!NT_SUCCESS(Result))
            return Result;
    }

    Result = RtlAppendUnicodeStringToString(&FileNode->FileName, &FileName);
    ASSERT(NT_SUCCESS(Result));

    /* check filename validity */
    if (!FspFileNameIsValid(&FileNode->FileName, FsvolDeviceExtension->VolumeParams.MaxComponentLength,
        FsvolDeviceExtension->VolumeParams.NamedStreams ? &StreamPart : 0,
        &StreamType))
    {
        FspFileNodeDereference(FileNode);
        return STATUS_OBJECT_NAME_INVALID;
    }

    /* if we have a stream part (even non-empty), ensure that FileNode->FileName has single colon */
    if (0 != StreamPart.Buffer)
    {
        ASSERT(
            (PUINT8)FileNode->FileName.Buffer + sizeof(WCHAR) <= (PUINT8)StreamPart.Buffer &&
            (PUINT8)StreamPart.Buffer + StreamPart.Length <=
                (PUINT8)FileNode->FileName.Buffer + FileNode->FileName.Length);

        FileNode->FileName.Length = (USHORT)
            ((PUINT8)StreamPart.Buffer - (PUINT8)FileNode->FileName.Buffer + StreamPart.Length -
            (0 == StreamPart.Length) * sizeof(WCHAR));
    }

    /*
     * Check and remove any volume prefix. Only do this when RelatedFileObject is NULL,
     * because the volume prefix has been removed already from the RelatedFileNode.
     */
    if (0 == RelatedFileObject && 0 < FsvolDeviceExtension->VolumePrefix.Length)
    {
        if (!FspFsvolDeviceVolumePrefixInString(FsvolDeviceObject, &FileNode->FileName) ||
            (FileNode->FileName.Length > FsvolDeviceExtension->VolumePrefix.Length &&
            '\\' != FileNode->FileName.Buffer[FsvolDeviceExtension->VolumePrefix.Length / sizeof(WCHAR)]))
        {
            FspFileNodeDereference(FileNode);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        if (FileNode->FileName.Length > FsvolDeviceExtension->VolumePrefix.Length)
        {
            FileNode->FileName.Length -= FsvolDeviceExtension->VolumePrefix.Length;
            FileNode->FileName.MaximumLength -= FsvolDeviceExtension->VolumePrefix.Length;
            FileNode->FileName.Buffer += FsvolDeviceExtension->VolumePrefix.Length / sizeof(WCHAR);
        }
        else
            FileNode->FileName.Length = sizeof(WCHAR);
    }

    ASSERT(sizeof(WCHAR) <= FileNode->FileName.Length && L'\\' == FileNode->FileName.Buffer[0]);

    /* check for trailing backslash */
    if (sizeof(WCHAR) * 2/* not empty or root */ <= FileNode->FileName.Length &&
        L'\\' == FileNode->FileName.Buffer[FileNode->FileName.Length / sizeof(WCHAR) - 1])
    {
        HasTrailingBackslash = TRUE;
        FileNode->FileName.Length -= sizeof(WCHAR);
    }

    /* if a $DATA stream type, this cannot be a directory */
    if (FspFileNameStreamTypeData == StreamType)
    {
        if (FlagOn(CreateOptions, FILE_DIRECTORY_FILE))
        {
            FspFileNodeDereference(FileNode);
            return STATUS_NOT_A_DIRECTORY;
        }
    }

    /* not all operations allowed on the root directory */
    if (sizeof(WCHAR) == FileNode->FileName.Length &&
        (FILE_CREATE == CreateDisposition ||
        FILE_OVERWRITE == CreateDisposition ||
        FILE_OVERWRITE_IF == CreateDisposition ||
        FILE_SUPERSEDE == CreateDisposition ||
        BooleanFlagOn(Flags, SL_OPEN_TARGET_DIRECTORY)))
    {
        FspFileNodeDereference(FileNode);
        return STATUS_ACCESS_DENIED;
    }

    /* cannot FILE_DELETE_ON_CLOSE on the root directory */
    if (sizeof(WCHAR) == FileNode->FileName.Length &&
        FlagOn(CreateOptions, FILE_DELETE_ON_CLOSE))
    {
        FspFileNodeDereference(FileNode);
        return STATUS_CANNOT_DELETE;
    }

    Result = FspFileDescCreate(&FileDesc);
    if (!NT_SUCCESS(Result))
    {
        FspFileNodeDereference(FileNode);
        return Result;
    }

    /* fix FileAttributes */
    ClearFlag(FileAttributes,
        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT);
    if (CreateOptions & FILE_DIRECTORY_FILE)
        SetFlag(FileAttributes, FILE_ATTRIBUTE_DIRECTORY);

    /* if we have a non-empty stream part, open the main file */
    if (0 != StreamPart.Length)
    {
        /* named streams can never be directories (even when attached to directories) */
        if (FlagOn(CreateOptions, FILE_DIRECTORY_FILE))
        {
            Result = STATUS_NOT_A_DIRECTORY;
            goto main_stream_exit;
        }

        /* cannot open target directory of a named stream */
        if (FlagOn(Flags, SL_OPEN_TARGET_DIRECTORY))
        {
            Result = STATUS_OBJECT_NAME_INVALID;
            goto main_stream_exit;
        }

        MainFileName.Length = MainFileName.MaximumLength = (USHORT)
            ((PUINT8)StreamPart.Buffer - (PUINT8)FileNode->FileName.Buffer - sizeof(WCHAR));
        MainFileName.Buffer = FileNode->FileName.Buffer;

        Result = FspMainFileOpen(
            FsvolDeviceObject,
            FileObject->DeviceObject, /* use as device hint when using MUP */
            &MainFileName, CaseSensitive,
            SecurityDescriptor,
            FileAttributes,
            CreateDisposition,
            &FileDesc->MainFileHandle,
            &FileDesc->MainFileObject);
        if (!NT_SUCCESS(Result))
            goto main_stream_exit;

        /* check that the main file is one we recognize */
        if (!FspFileNodeIsValid(FileDesc->MainFileObject->FsContext))
        {
            Result = STATUS_OBJECT_NAME_NOT_FOUND;
            goto main_stream_exit;
        }

        /* cannot set security descriptor or file attributes on named stream */
        SecurityDescriptor = 0;
        SecurityDescriptorSize = 0;
        FileAttributes = 0;

        /* remember the main file node */
        ASSERT(0 == FileNode->MainFileNode);
        FileNode->MainFileNode = FileDesc->MainFileObject->FsContext;
        FspFileNodeReference(FileNode->MainFileNode);

        Result = STATUS_SUCCESS;

    main_stream_exit:
        if (!NT_SUCCESS(Result))
        {
            FspFileDescDelete(FileDesc);
            FspFileNodeDereference(FileNode);
            return Result;
        }
    }

    /* create the user-mode file system request */
    Result = FspIopCreateRequestEx(Irp, &FileNode->FileName, SecurityDescriptorSize,
        FspFsvolCreateRequestFini, &Request);
    if (!NT_SUCCESS(Result))
    {
        FspFileDescDelete(FileDesc);
        FspFileNodeDereference(FileNode);
        return Result;
    }

    /*
     * The new request is associated with our IRP. Go ahead and associate our FileNode/FileDesc
     * with the Request as well. After this is done completing our IRP will automatically
     * delete the Request and any associated resources.
     */
    FileDesc->FileNode = FileNode;
    FileDesc->CaseSensitive = CaseSensitive;
    FileDesc->HasTraversePrivilege = HasTraversePrivilege;

    if (!MainFileOpen)
    {
        FspFsvolDeviceFileRenameSetOwner(FsvolDeviceObject, Request);
        FspIopRequestContext(Request, RequestDeviceObject) = FsvolDeviceObject;
    }
    FspIopRequestContext(Request, RequestFileDesc) = FileDesc;

    /* populate the Create request */
    Request->Kind = FspFsctlTransactCreateKind;
    Request->Req.Create.CreateOptions = CreateOptions;
    Request->Req.Create.FileAttributes = FileAttributes;
    Request->Req.Create.SecurityDescriptor.Offset = 0 == SecurityDescriptorSize ? 0 :
        FSP_FSCTL_DEFAULT_ALIGN_UP(Request->FileName.Size);
    Request->Req.Create.SecurityDescriptor.Size = (UINT16)SecurityDescriptorSize;
    Request->Req.Create.AllocationSize = AllocationSize;
    Request->Req.Create.AccessToken = 0;
    Request->Req.Create.DesiredAccess = DesiredAccess;
    Request->Req.Create.GrantedAccess = GrantedAccess;
    Request->Req.Create.ShareAccess = ShareAccess;
    Request->Req.Create.Ea.Offset = 0;
    Request->Req.Create.Ea.Size = 0;
    Request->Req.Create.UserMode = UserMode == RequestorMode;
    Request->Req.Create.HasTraversePrivilege = HasTraversePrivilege;
    Request->Req.Create.HasBackupPrivilege = HasBackupPrivilege;
    Request->Req.Create.HasRestorePrivilege = HasRestorePrivilege;
    Request->Req.Create.OpenTargetDirectory = BooleanFlagOn(Flags, SL_OPEN_TARGET_DIRECTORY);
    Request->Req.Create.CaseSensitive = CaseSensitive;
    Request->Req.Create.HasTrailingBackslash = HasTrailingBackslash;
    Request->Req.Create.NamedStream = MainFileName.Length;

    ASSERT(
        0 == StreamPart.Length && 0 == MainFileName.Length ||
        0 != StreamPart.Length && 0 != MainFileName.Length);

    /* copy the security descriptor (if any) into the request */
    if (0 != SecurityDescriptorSize)
        RtlCopyMemory(Request->Buffer + Request->Req.Create.SecurityDescriptor.Offset,
            SecurityDescriptor, SecurityDescriptorSize);

    /* fix FileNode->FileName if we are doing SL_OPEN_TARGET_DIRECTORY */
    if (Request->Req.Create.OpenTargetDirectory)
    {
        UNICODE_STRING Suffix;

        FspFileNameSuffix(&FileNode->FileName, &FileNode->FileName, &Suffix);
    }

    /* zero Irp->IoStatus, because we now use it to maintain state in FspFsvolCreateComplete */
    Irp->IoStatus.Status = 0;
    Irp->IoStatus.Information = 0;

    return FSP_STATUS_IOQ_POST;
}

NTSTATUS FspFsvolCreatePrepare(
    PIRP Irp, FSP_FSCTL_TRANSACT_REQ *Request)
{
    PAGED_CODE();

    NTSTATUS Result;
    BOOLEAN Success;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PSECURITY_SUBJECT_CONTEXT SecuritySubjectContext;
    SECURITY_QUALITY_OF_SERVICE SecurityQualityOfService;
    SECURITY_CLIENT_CONTEXT SecurityClientContext;
    HANDLE UserModeAccessToken;
    PEPROCESS Process;
    FSP_FILE_NODE *FileNode;
    FSP_FILE_DESC *FileDesc;
    PFILE_OBJECT FileObject;

    if (FspFsctlTransactCreateKind == Request->Kind)
    {
        SecuritySubjectContext = &IrpSp->Parameters.Create.SecurityContext->
            AccessState->SubjectSecurityContext;

        /* duplicate the subject context access token into an impersonation token */
        SecurityQualityOfService.Length = sizeof SecurityQualityOfService;
        SecurityQualityOfService.ImpersonationLevel = SecurityIdentification;
        SecurityQualityOfService.ContextTrackingMode = SECURITY_STATIC_TRACKING;
        SecurityQualityOfService.EffectiveOnly = FALSE;
        SeLockSubjectContext(SecuritySubjectContext);
        Result = SeCreateClientSecurityFromSubjectContext(SecuritySubjectContext,
            &SecurityQualityOfService, FALSE, &SecurityClientContext);
        SeUnlockSubjectContext(SecuritySubjectContext);
        if (!NT_SUCCESS(Result))
            return Result;

        ASSERT(TokenImpersonation == SeTokenType(SecurityClientContext.ClientToken));

        /* get a user-mode handle to the impersonation token */
        Result = ObOpenObjectByPointer(SecurityClientContext.ClientToken,
            0, 0, TOKEN_QUERY, *SeTokenObjectType, UserMode, &UserModeAccessToken);
        SeDeleteClientSecurity(&SecurityClientContext);
        if (!NT_SUCCESS(Result))
            return Result;

        /* get a pointer to the current process so that we can close the impersonation token later */
        Process = PsGetCurrentProcess();
        ObReferenceObject(Process);

        /* send the user-mode handle to the user-mode file system */
        FspIopRequestContext(Request, RequestAccessToken) = UserModeAccessToken;
        FspIopRequestContext(Request, RequestProcess) = Process;
        Request->Req.Create.AccessToken = (UINT_PTR)UserModeAccessToken;

        return STATUS_SUCCESS;
    }
    else if (FspFsctlTransactOverwriteKind == Request->Kind)
    {
        FileDesc = FspIopRequestContext(Request, RequestFileDesc);
        FileNode = FileDesc->FileNode;
        FileObject = FspIopRequestContext(Request, RequestFileObject);

        /* lock the FileNode for overwriting */
        Result = STATUS_SUCCESS;
        Success = DEBUGTEST(90) &&
            FspFileNodeTryAcquireExclusive(FileNode, Full) &&
            FspFsvolCreateOpenOrOverwriteOplock(Irp, 0, &Result);
        if (!Success)
        {
            if (STATUS_PENDING == Result)
                return Result;
            if (!NT_SUCCESS(Result))
            {
                FspFileNodeRelease(FileNode, Full);
                return Result;
            }

            FspIopRetryPrepareIrp(Irp, &Result);
            return Result;
        }

        /* see what the MM thinks about all this */
        LARGE_INTEGER Zero = { 0 };
        Success = MmCanFileBeTruncated(&FileNode->NonPaged->SectionObjectPointers, &Zero);
        if (!Success)
        {
            FspFileNodeRelease(FileNode, Full);

            return STATUS_USER_MAPPED_FILE;
        }

        /* purge any caches on this file */
        CcPurgeCacheSection(&FileNode->NonPaged->SectionObjectPointers, 0, 0, FALSE);

        FspFileNodeSetOwner(FileNode, Full, Request);
        FspIopRequestContext(Request, RequestState) = (PVOID)RequestProcessing;

        return STATUS_SUCCESS;
    }
    else
    {
        ASSERT(0);

        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS FspFsvolCreateComplete(
    PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response)
{
    FSP_ENTER_IOC(PAGED_CODE());

    PDEVICE_OBJECT FsvolDeviceObject = IrpSp->DeviceObject;
    FSP_FSVOL_DEVICE_EXTENSION *FsvolDeviceExtension = FspFsvolDeviceExtension(FsvolDeviceObject);
    PACCESS_STATE AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    FSP_FILE_DESC *FileDesc = FspIopRequestContext(Request, RequestFileDesc);
    FSP_FILE_NODE *FileNode = FileDesc->FileNode;
    FSP_FILE_NODE *OpenedFileNode;
    ULONG SharingViolationReason;
    UNICODE_STRING NormalizedName;
    PREPARSE_DATA_BUFFER ReparseData;
    UNICODE_STRING ReparseTargetPrefix0, ReparseTargetPrefix1, ReparseTargetPath;

    if (FspFsctlTransactCreateKind == Request->Kind)
    {
        /* did the user-mode file system sent us a failure code? */
        if (!NT_SUCCESS(Response->IoStatus.Status))
        {
            Irp->IoStatus.Information = STATUS_SHARING_VIOLATION == Response->IoStatus.Status ?
                Response->IoStatus.Information : 0;
            Result = Response->IoStatus.Status;
            FSP_RETURN();
        }

        /* special case STATUS_REPARSE */
        if (STATUS_REPARSE == Response->IoStatus.Status)
        {
            if (IO_REMOUNT == Response->IoStatus.Information)
            {
                Irp->IoStatus.Information = IO_REMOUNT;
                FSP_RETURN(Result = STATUS_REPARSE);
            }
            else
            if (IO_REPARSE == Response->IoStatus.Information ||
                IO_REPARSE_TAG_SYMLINK == Response->IoStatus.Information)
            {
                /*
                 * IO_REPARSE means that the user-mode file system has returned an absolute (in
                 * the device namespace) path. Send it as is to the IO Manager.
                 *
                 * IO_REPARSE_TAG_SYMLINK means that the user-mode file system has returned a full
                 * symbolic link reparse buffer. If the symbolic link is absolute send it to the
                 * IO Manager as is. If the symbolic link is device-relative (absolute in the
                 * device namespace) prefix it with our volume name/prefix and then send it to the
                 * IO Manager.
                 *
                 * We do our own handling of IO_REPARSE_TAG_SYMLINK reparse points because
                 * experimentation with handing them directly to the IO Manager revealed problems
                 * with UNC paths (\??\UNC\{VolumePrefix}\{FilePath}).
                 */

                if (IO_REPARSE == Response->IoStatus.Information)
                {
                    RtlZeroMemory(&ReparseTargetPrefix0, sizeof ReparseTargetPrefix0);
                    RtlZeroMemory(&ReparseTargetPrefix1, sizeof ReparseTargetPrefix1);

                    ReparseTargetPath.Length = ReparseTargetPath.MaximumLength =
                        Response->Rsp.Create.Reparse.Buffer.Size;
                    ReparseTargetPath.Buffer =
                        (PVOID)(Response->Buffer + Response->Rsp.Create.Reparse.Buffer.Offset);

                    if ((PUINT8)ReparseTargetPath.Buffer + ReparseTargetPath.Length >
                        (PUINT8)Response + Response->Size ||
                        ReparseTargetPath.Length < sizeof(WCHAR) || L'\\' != ReparseTargetPath.Buffer[0])
                        FSP_RETURN(Result = STATUS_REPARSE_POINT_NOT_RESOLVED);
                }
                else
                {
                    ASSERT(IO_REPARSE_TAG_SYMLINK == Response->IoStatus.Information);

                    ReparseData = (PVOID)(Response->Buffer + Response->Rsp.Create.Reparse.Buffer.Offset);

                    if ((PUINT8)ReparseData + Response->Rsp.Create.Reparse.Buffer.Size >
                        (PUINT8)Response + Response->Size)
                        FSP_RETURN(Result = STATUS_REPARSE_POINT_NOT_RESOLVED);

                    Result = FsRtlValidateReparsePointBuffer(Response->Rsp.Create.Reparse.Buffer.Size,
                        ReparseData);
                    if (!NT_SUCCESS(Result))
                        FSP_RETURN(Result = STATUS_REPARSE_POINT_NOT_RESOLVED);

                    if (!FlagOn(ReparseData->SymbolicLinkReparseBuffer.Flags, SYMLINK_FLAG_RELATIVE))
                    {
                        RtlZeroMemory(&ReparseTargetPrefix0, sizeof ReparseTargetPrefix0);
                        RtlZeroMemory(&ReparseTargetPrefix1, sizeof ReparseTargetPrefix1);
                    }
                    else
                    {
                        RtlCopyMemory(&ReparseTargetPrefix0, &FsvolDeviceExtension->VolumeName,
                            sizeof ReparseTargetPrefix0);
                        RtlCopyMemory(&ReparseTargetPrefix1, &FsvolDeviceExtension->VolumePrefix,
                            sizeof ReparseTargetPrefix1);
                    }

                    ReparseTargetPath.Buffer = ReparseData->SymbolicLinkReparseBuffer.PathBuffer +
                        ReparseData->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
                    ReparseTargetPath.Length = ReparseTargetPath.MaximumLength =
                        ReparseData->SymbolicLinkReparseBuffer.SubstituteNameLength;

                    if (ReparseTargetPath.Length < sizeof(WCHAR) || L'\\' != ReparseTargetPath.Buffer[0])
                        FSP_RETURN(Result = STATUS_REPARSE_POINT_NOT_RESOLVED);
                }

                if (ReparseTargetPrefix0.Length + ReparseTargetPrefix1.Length + ReparseTargetPath.Length >
                    FileObject->FileName.MaximumLength)
                {
                    PVOID Buffer = FspAllocExternal(
                        ReparseTargetPrefix0.Length + ReparseTargetPrefix1.Length + ReparseTargetPath.Length);
                    if (0 == Buffer)
                        FSP_RETURN(Result = STATUS_INSUFFICIENT_RESOURCES);
                    FspFreeExternal(FileObject->FileName.Buffer);
                    FileObject->FileName.MaximumLength =
                        ReparseTargetPrefix0.Length + ReparseTargetPrefix1.Length + ReparseTargetPath.Length;
                    FileObject->FileName.Buffer = Buffer;
                }
                FileObject->FileName.Length = 0;
                RtlAppendUnicodeStringToString(&FileObject->FileName, &ReparseTargetPrefix0);
                RtlAppendUnicodeStringToString(&FileObject->FileName, &ReparseTargetPrefix1);
                RtlAppendUnicodeStringToString(&FileObject->FileName, &ReparseTargetPath);

                /*
                 * The RelatedFileObject does not need to be changed according to:
                 * https://support.microsoft.com/en-us/kb/319447
                 *
                 * Quote:
                 *     The fact that the first create-file operation is performed
                 *     relative to another file object does not matter. Do not modify
                 *     the RelatedFileObject field of the FILE_OBJECT. To perform the
                 *     reparse operation, the IO Manager considers only the FileName
                 *     field and not the RelatedFileObject. Additionally, the IO Manager
                 *     frees the RelatedFileObject, as appropriate, when it handles the
                 *     STATUS_REPARSE status returned by the filter. Therefore, it is not
                 *     the responsibility of the filter to free that file object.
                 */

                Irp->IoStatus.Information = IO_REPARSE;
                FSP_RETURN(Result = STATUS_REPARSE);
            }
            else
            {
                ReparseData = (PVOID)(Response->Buffer + Response->Rsp.Create.Reparse.Buffer.Offset);

                if ((PUINT8)ReparseData + Response->Rsp.Create.Reparse.Buffer.Size >
                    (PUINT8)Response + Response->Size)
                    FSP_RETURN(Result = STATUS_IO_REPARSE_DATA_INVALID);

                Result = FsRtlValidateReparsePointBuffer(Response->Rsp.Create.Reparse.Buffer.Size,
                    ReparseData);
                if (!NT_SUCCESS(Result))
                    FSP_RETURN();

                ASSERT(0 == Irp->Tail.Overlay.AuxiliaryBuffer);
                Irp->Tail.Overlay.AuxiliaryBuffer = FspAllocNonPagedExternal(
                    Response->Rsp.Create.Reparse.Buffer.Size);
                if (0 == Irp->Tail.Overlay.AuxiliaryBuffer)
                    FSP_RETURN(Result = STATUS_INSUFFICIENT_RESOURCES);

                RtlCopyMemory(Irp->Tail.Overlay.AuxiliaryBuffer, ReparseData,
                    Response->Rsp.Create.Reparse.Buffer.Size);

                Irp->IoStatus.Information = ReparseData->ReparseTag;
                FSP_RETURN(Result = STATUS_REPARSE);
            }
        }

        /* populate the FileNode/FileDesc fields from the Response */
        FileNode->UserContext = Response->Rsp.Create.Opened.UserContext;
        FileNode->IndexNumber = Response->Rsp.Create.Opened.FileInfo.IndexNumber;
        FileNode->IsDirectory = BooleanFlagOn(Response->Rsp.Create.Opened.FileInfo.FileAttributes,
            FILE_ATTRIBUTE_DIRECTORY);
        FileNode->IsRootDirectory = FileNode->IsDirectory &&
            sizeof(WCHAR) == FileNode->FileName.Length && L'\\' == FileNode->FileName.Buffer[0];
        FileDesc->UserContext2 = Response->Rsp.Create.Opened.UserContext2;
        FileDesc->DeleteOnClose = BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_DELETE_ON_CLOSE);

        /* handle normalized names */
        if (!FsvolDeviceExtension->VolumeParams.CaseSensitiveSearch)
        {
            /* is there a normalized file name as part of the response? */
            if (0 == Response->Rsp.Create.Opened.FileName.Size)
            {
                /* if not, the default is to upper case the name */

                FspFileNameUpcase(&FileNode->FileName, &FileNode->FileName, 0);
            }
            else
            {
                /* if yes, verify it and then set it */

                if (Response->Buffer + Response->Rsp.Create.Opened.FileName.Size >
                    (PUINT8)Response + Response->Size)
                {
                    FspFsvolCreatePostClose(FileDesc);
                    FSP_RETURN(Result = STATUS_OBJECT_NAME_INVALID);
                }

                NormalizedName.Length = NormalizedName.MaximumLength =
                    Response->Rsp.Create.Opened.FileName.Size;
                NormalizedName.Buffer = (PVOID)Response->Buffer;

                /* normalized file name can only differ in case from requested one */
                if (0 != FspFileNameCompare(&FileNode->FileName, &NormalizedName, TRUE, 0))
                {
                    FspFsvolCreatePostClose(FileDesc);
                    FSP_RETURN(Result = STATUS_OBJECT_NAME_INVALID);
                }

                ASSERT(FileNode->FileName.Length == NormalizedName.Length);
                RtlCopyMemory(FileNode->FileName.Buffer, NormalizedName.Buffer, NormalizedName.Length);
            }
        }

        /* open the FileNode */
        Result = FspFileNodeOpen(FileNode, FileObject,
            Response->Rsp.Create.Opened.GrantedAccess, IrpSp->Parameters.Create.ShareAccess,
            &OpenedFileNode, &SharingViolationReason);
        if (!NT_SUCCESS(Result))
        {
            if (STATUS_SHARING_VIOLATION == Result)
            {
                ASSERT(0 != OpenedFileNode);

                if (STATUS_SHARING_VIOLATION != Irp->IoStatus.Status)
                {
                    ASSERT(0 == Irp->IoStatus.Information ||
                        FILE_OPBATCH_BREAK_UNDERWAY == Irp->IoStatus.Information);

                    FspIopSetIrpResponse(Irp, Response);
                    FspIopRequestContext(Request, FspIopRequestExtraContext) = OpenedFileNode;

                    /* HACK: We have run out of Contexts. Store the sharing violation reason in the IRP. */
                    Irp->IoStatus.Status = STATUS_SHARING_VIOLATION;
                    Irp->IoStatus.Information = SharingViolationReason;

                    Result = FspFsvolCreateSharingViolationOplock(
                        FsvolDeviceObject, Irp, IrpSp, FALSE);
                    FSP_RETURN();
                }
                else
                {
                    FspFileNodeClose(OpenedFileNode, 0, TRUE);
                    FspFileNodeDereference(OpenedFileNode);
                }
            }
            else
                ASSERT(0 == OpenedFileNode);

            /* unable to open the FileNode; post a Close request */
            FspFsvolCreatePostClose(FileDesc);

            FSP_RETURN();
        }

        if (FileNode != OpenedFileNode)
        {
            FspFileNodeDereference(FileNode);
            FileDesc->FileNode = FileNode = OpenedFileNode;
        }

        /* set up the AccessState */
        AccessState->RemainingDesiredAccess = 0;
        AccessState->PreviouslyGrantedAccess = Response->Rsp.Create.Opened.GrantedAccess;
        FileDesc->GrantedAccess = Response->Rsp.Create.Opened.GrantedAccess;

        /* set up the FileObject */
        if (0 != FsvolDeviceExtension->FsvrtDeviceObject)
#pragma prefast(disable:28175, "We are a filesystem: ok to access Vpb")
            FileObject->Vpb = FsvolDeviceExtension->FsvrtDeviceObject->Vpb;
        FileObject->SectionObjectPointer = &FileNode->NonPaged->SectionObjectPointers;
        FileObject->PrivateCacheMap = 0;
        FileObject->FsContext = FileNode;
        FileObject->FsContext2 = FileDesc;
        if (!FileNode->IsDirectory)
        {
            /* properly set temporary bit for lazy writer */
            if (FlagOn(Response->Rsp.Create.Opened.FileInfo.FileAttributes,
                FILE_ATTRIBUTE_TEMPORARY))
                SetFlag(FileObject->Flags, FO_TEMPORARY_FILE);
        }
        if (FspTimeoutInfinity32 == FsvolDeviceExtension->VolumeParams.FileInfoTimeout &&
            !FlagOn(IrpSp->Parameters.Create.Options, FILE_NO_INTERMEDIATE_BUFFERING) &&
            !Response->Rsp.Create.Opened.DisableCache)
            /* enable caching! */
            SetFlag(FileObject->Flags, FO_CACHE_SUPPORTED);

        if (FILE_SUPERSEDED != Response->IoStatus.Information &&
            FILE_OVERWRITTEN != Response->IoStatus.Information)
        {
            /*
             * FastFat quote:
             *     If the user wants write access access to the file make sure there
             *     is not a process mapping this file as an image.  Any attempt to
             *     delete the file will be stopped in fileinfo.c
             *
             *     If the user wants to delete on close, we must check at this
             *     point though.
             */
            BOOLEAN FlushImage =
                !FileNode->IsDirectory &&
                (FlagOn(Response->Rsp.Create.Opened.GrantedAccess, FILE_WRITE_DATA) ||
                BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_DELETE_ON_CLOSE));

            Result = FspFsvolCreateTryOpen(Irp, Response, FileNode, FileDesc, FileObject, FlushImage);
        }
        else
        {
            /*
             * Oh, noes! We have to go back to user mode to overwrite the file!
             */

            USHORT FileAttributes = IrpSp->Parameters.Create.FileAttributes;
            UINT64 AllocationSize = Request->Req.Create.AllocationSize;

            ClearFlag(FileAttributes, FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DIRECTORY);
            if (FileNode->IsDirectory)
                SetFlag(FileAttributes, FILE_ATTRIBUTE_DIRECTORY);

            /* overwrite/supersede operations must have the correct hidden/system attributes set */
            if ((FlagOn(Response->Rsp.Create.Opened.FileInfo.FileAttributes, FILE_ATTRIBUTE_HIDDEN) &&
                    !FlagOn(FileAttributes, FILE_ATTRIBUTE_HIDDEN)) ||
                (FlagOn(Response->Rsp.Create.Opened.FileInfo.FileAttributes, FILE_ATTRIBUTE_SYSTEM) &&
                    !FlagOn(FileAttributes, FILE_ATTRIBUTE_SYSTEM)))
            {
                FspFsvolCreatePostClose(FileDesc);
                FspFileNodeClose(FileNode, FileObject, TRUE);

                Result = STATUS_ACCESS_DENIED;
                FSP_RETURN();
            }

            PVOID RequestDeviceObjectValue = FspIopRequestContext(Request, RequestDeviceObject);

            /* disassociate the FileDesc momentarily from the Request */
            FspIopRequestContext(Request, RequestDeviceObject) = 0;
            FspIopRequestContext(Request, RequestFileDesc) = 0;

            /* reset the request */
            Request->Kind = FspFsctlTransactOverwriteKind;
            RtlZeroMemory(&Request->Req.Create, sizeof Request->Req.Create);
            FspIopResetRequest(Request, FspFsvolCreateOverwriteRequestFini);
            FspIopRequestContext(Request, RequestDeviceObject) = RequestDeviceObjectValue;
            FspIopRequestContext(Request, RequestFileDesc) = FileDesc;
            FspIopRequestContext(Request, RequestFileObject) = FileObject;
            FspIopRequestContext(Request, RequestState) = (PVOID)RequestPending;

            /* populate the Overwrite request */
            Request->Req.Overwrite.UserContext = FileNode->UserContext;
            Request->Req.Overwrite.UserContext2 = FileDesc->UserContext2;
            Request->Req.Overwrite.FileAttributes = FileAttributes;
            Request->Req.Overwrite.AllocationSize = AllocationSize;
            Request->Req.Overwrite.Supersede = FILE_SUPERSEDED == Response->IoStatus.Information;

            /*
             * Post it as BestEffort.
             *
             * Note that it is still possible for this request to not be delivered,
             * if the volume device Ioq is stopped or if the IRP is canceled.
             */
            FspIoqPostIrpBestEffort(FsvolDeviceExtension->Ioq, Irp, &Result);
        }
    }
    else if (FspFsctlTransactReservedKind == Request->Kind)
    {
        /*
         * A Reserved request is a special request used when retrying a file open.
         */

        BOOLEAN FlushImage = 0 != FspIopRequestContext(Request, RequestState);

        Result = FspFsvolCreateTryOpen(Irp, Response, FileNode, FileDesc, FileObject, FlushImage);
    }
    else if (FspFsctlTransactOverwriteKind == Request->Kind)
    {
        /*
         * An Overwrite request will either succeed or else fail and close the corresponding file.
         * There is no need to reach out to user-mode again and ask them to close the file.
         */

        /* did the user-mode file system sent us a failure code? */
        if (!NT_SUCCESS(Response->IoStatus.Status))
        {
            Irp->IoStatus.Information = 0;
            Result = Response->IoStatus.Status;
            FSP_RETURN();
        }

        /* file was successfully overwritten/superseded */
        if (0 == FileNode->MainFileNode)
            FspFileNodeOverwriteStreams(FileNode);
        FspFileNodeSetFileInfo(FileNode, FileObject, &Response->Rsp.Overwrite.FileInfo, TRUE);
        FspFileNodeNotifyChange(FileNode,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE,
            FILE_ACTION_MODIFIED,
            FALSE);

        FspFileNodeReleaseOwner(FileNode, Full, Request);

        /* SUCCESS! */
        FspIopRequestContext(Request, RequestFileDesc) = 0;
        Irp->IoStatus.Information = Request->Req.Overwrite.Supersede ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
        Result = Irp->IoStatus.Status; /* get success value from oplock processing */
    }
    else
        ASSERT(0);

    FSP_LEAVE_IOC(
        "FileObject=%p[%p:\"%wZ\"]",
        IrpSp->FileObject, IrpSp->FileObject->RelatedFileObject, IrpSp->FileObject->FileName);
}

static NTSTATUS FspFsvolCreateTryOpen(PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response,
    FSP_FILE_NODE *FileNode, FSP_FILE_DESC *FileDesc, PFILE_OBJECT FileObject,
    BOOLEAN FlushImage)
{
    PAGED_CODE();

    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    NTSTATUS Result;
    BOOLEAN Success;

    if (FspFsctlTransactCreateKind == Request->Kind)
    {
        PVOID RequestDeviceObjectValue = FspIopRequestContext(Request, RequestDeviceObject);

        /* disassociate the FileDesc momentarily from the Request */
        Request = FspIrpRequest(Irp);
        FspIopRequestContext(Request, RequestDeviceObject) = 0;
        FspIopRequestContext(Request, RequestFileDesc) = 0;

        /* reset the Request and reassociate the FileDesc and FileObject with it */
        Request->Kind = FspFsctlTransactReservedKind;
        FspIopResetRequest(Request, FspFsvolCreateTryOpenRequestFini);
        FspIopRequestContext(Request, RequestDeviceObject) = RequestDeviceObjectValue;
        FspIopRequestContext(Request, RequestFileDesc) = FileDesc;
        FspIopRequestContext(Request, RequestFileObject) = FileObject;
        FspIopRequestContext(Request, RequestState) = (PVOID)(UINT_PTR)FlushImage;
    }

    Result = STATUS_SUCCESS;
    Success = DEBUGTEST(90) &&
        FspFileNodeTryAcquireExclusive(FileNode, Main) &&
        FspFsvolCreateOpenOrOverwriteOplock(Irp, Response, &Result);
    if (!Success)
    {
        if (STATUS_PENDING == Result)
            return Result;
        if (!NT_SUCCESS(Result))
        {
            FspFileNodeRelease(FileNode, Main);
            return Result;
        }

        FspIopRetryCompleteIrp(Irp, Response, &Result);
        return Result;
    }

    FspFileNodeSetFileInfo(FileNode, FileObject, &Response->Rsp.Create.Opened.FileInfo,
        FILE_CREATED == Response->IoStatus.Information);

    if (FlushImage)
    {
        Success = MmFlushImageSection(&FileNode->NonPaged->SectionObjectPointers,
            MmFlushForWrite);
        if (!Success)
        {
            FspFileNodeRelease(FileNode, Main);

            PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
            BOOLEAN DeleteOnClose = BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_DELETE_ON_CLOSE);

            if (0 == Request)
            {
                FspFsvolCreatePostClose(FileDesc);
                FspFileNodeClose(FileNode, FileObject, TRUE);
            }

            Irp->IoStatus.Information = 0;
            return DeleteOnClose ? STATUS_CANNOT_DELETE : STATUS_SHARING_VIOLATION;
        }
    }

    if (FILE_CREATED == Response->IoStatus.Information)
        FspFileNodeNotifyChange(FileNode,
            FileNode->IsDirectory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
            FILE_ACTION_ADDED,
            TRUE);

    FspFileNodeRelease(FileNode, Main);

    /* SUCCESS! */
    FspIopRequestContext(Request, RequestFileDesc) = 0;
    Irp->IoStatus.Information = Response->IoStatus.Information;
    return Irp->IoStatus.Status; /* get success value from oplock processing */
}

static VOID FspFsvolCreatePostClose(FSP_FILE_DESC *FileDesc)
{
    PAGED_CODE();

    FSP_FILE_NODE *FileNode = FileDesc->FileNode;
    PDEVICE_OBJECT FsvolDeviceObject = FileNode->FsvolDeviceObject;
    FSP_FSCTL_TRANSACT_REQ *Request;

    /* create the user-mode file system request; MustSucceed because we cannot fail */
    FspIopCreateRequestMustSucceed(0, 0, 0, &Request);

    /* populate the Close request */
    Request->Kind = FspFsctlTransactCloseKind;
    Request->Req.Close.UserContext = FileNode->UserContext;
    Request->Req.Close.UserContext2 = FileDesc->UserContext2;

    /*
     * Post as a BestEffort work request. This allows us to complete our own IRP
     * and return immediately.
     */
    FspIopPostWorkRequestBestEffort(FsvolDeviceObject, Request);

    /*
     * Note that it is still possible for this request to not be delivered,
     * if the volume device Ioq is stopped. But such failures are benign
     * from our perspective, because they mean that the file system is going
     * away and should correctly tear things down.
     */
}

static VOID FspFsvolCreateRequestFini(FSP_FSCTL_TRANSACT_REQ *Request, PVOID Context[4])
{
    PAGED_CODE();

    PDEVICE_OBJECT FsvolDeviceObject = Context[RequestDeviceObject];
    FSP_FILE_DESC *FileDesc = Context[RequestFileDesc];
    HANDLE AccessToken = Context[RequestAccessToken];
    PEPROCESS Process = Context[RequestProcess];
    FSP_FILE_NODE *ExtraFileNode = FspIopRequestContext(Request, FspIopRequestExtraContext);

    if (0 != ExtraFileNode)
    {
        ASSERT(FspFileNodeIsValid(ExtraFileNode));
        FspFileNodeClose(ExtraFileNode, 0, TRUE);
        FspFileNodeDereference(ExtraFileNode);
    }

    if (0 != FileDesc)
    {
        FspFileNodeDereference(FileDesc->FileNode);
        FspFileDescDelete(FileDesc);
    }

    if (0 != AccessToken)
    {
        KAPC_STATE ApcState;
        BOOLEAN Attach;

        ASSERT(0 != Process);
        Attach = Process != PsGetCurrentProcess();

        if (Attach)
            KeStackAttachProcess(Process, &ApcState);
#if DBG
        NTSTATUS Result0;
        Result0 = ObCloseHandle(AccessToken, UserMode);
        if (!NT_SUCCESS(Result0))
            DEBUGLOG("ObCloseHandle() = %s", NtStatusSym(Result0));
#else
        ObCloseHandle(AccessToken, UserMode);
#endif
        if (Attach)
            KeUnstackDetachProcess(&ApcState);

        ObDereferenceObject(Process);
    }

    if (0 != FsvolDeviceObject)
        FspFsvolDeviceFileRenameReleaseOwner(FsvolDeviceObject, Request);
}

static VOID FspFsvolCreateTryOpenRequestFini(FSP_FSCTL_TRANSACT_REQ *Request, PVOID Context[4])
{
    PAGED_CODE();

    PDEVICE_OBJECT FsvolDeviceObject = Context[RequestDeviceObject];
    FSP_FILE_DESC *FileDesc = Context[RequestFileDesc];
    PFILE_OBJECT FileObject = Context[RequestFileObject];
    PIRP OplockIrp = FspIopRequestContext(Request, FspIopRequestExtraContext);

    if (0 != FileDesc)
    {
        ASSERT(0 != FileObject);

        if (OplockIrp)
        {
            ASSERT(IO_TYPE_IRP == OplockIrp->Type);
            FspFileNodeOplockCheckEx(FileDesc->FileNode, OplockIrp,
                OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK);
        }

        FspFsvolCreatePostClose(FileDesc);
        FspFileNodeClose(FileDesc->FileNode, FileObject, TRUE);
        FspFileNodeDereference(FileDesc->FileNode);
        FspFileDescDelete(FileDesc);
    }

    if (0 != FsvolDeviceObject)
        FspFsvolDeviceFileRenameReleaseOwner(FsvolDeviceObject, Request);
}

static VOID FspFsvolCreateOverwriteRequestFini(FSP_FSCTL_TRANSACT_REQ *Request, PVOID Context[4])
{
    PAGED_CODE();

    PDEVICE_OBJECT FsvolDeviceObject = Context[RequestDeviceObject];
    FSP_FILE_DESC *FileDesc = Context[RequestFileDesc];
    PFILE_OBJECT FileObject = Context[RequestFileObject];
    ULONG State = (ULONG)(UINT_PTR)Context[RequestState];
    PIRP OplockIrp = FspIopRequestContext(Request, FspIopRequestExtraContext);

    if (0 != FileDesc)
    {
        ASSERT(0 != FileObject);

        if (RequestPending == State)
            FspFsvolCreatePostClose(FileDesc);
        else if (RequestProcessing == State)
            FspFileNodeReleaseOwner(FileDesc->FileNode, Full, Request);

        if (OplockIrp)
        {
            ASSERT(IO_TYPE_IRP == OplockIrp->Type);
            FspFileNodeOplockCheckEx(FileDesc->FileNode, OplockIrp,
                OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK);
        }

        FspFileNodeClose(FileDesc->FileNode, FileObject, TRUE);
        FspFileNodeDereference(FileDesc->FileNode);
        FspFileDescDelete(FileDesc);
    }

    if (0 != FsvolDeviceObject)
        FspFsvolDeviceFileRenameReleaseOwner(FsvolDeviceObject, Request);
}

static NTSTATUS FspFsvolCreateSharingViolationOplock(
    PDEVICE_OBJECT FsvolDeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp,
    BOOLEAN CanWait)
{
    PAGED_CODE();

    /*
     * Perform oplock checks in the case that we received STATUS_SHARING_VIOLATION.
     * The logic below is rather tortured, because it tries to mimic what FastFat
     * does in Win7 when a sharing violation happens. Here is an explanation.
     *
     * When this routine first gets called the CanWait parameter is FALSE. This is
     * because it gets called in the context of a completion thread, which belongs
     * to the user mode file system. This means that we cannot block this thread
     * because the user mode file system may have a limited number of threads to
     * service file system requests.
     *
     * When CanWait is FALSE the routine checks whether there are any oplocks on
     * this file. If there are none it simply returns STATUS_SHARING_VIOLATION.
     * Otherwise it posts a work item so that the routine can be executed in the
     * context of a worker thread. When executed in the context of a worker thread
     * the CanWait parameter is TRUE.
     *
     * When CanWait is TRUE the routine is free to wait for any oplocks breaks. We
     * try to break both Batch and Handle oplocks and return the appropriate status
     * code.
     *
     * We acquire the FileNode shared to allow oplock breaks to happen concurrently.
     * See https://www.osronline.com/showthread.cfm?link=248984
     */

    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    FSP_FSCTL_TRANSACT_RSP *Response;
    FSP_FILE_DESC *FileDesc = FspIopRequestContext(Request, RequestFileDesc);
    FSP_FILE_NODE *ExtraFileNode = FspIopRequestContext(Request, FspIopRequestExtraContext);
    ULONG SharingViolationReason = (ULONG)Irp->IoStatus.Information;
    NTSTATUS Result;
    BOOLEAN Success;

    ASSERT(FspFsctlTransactCreateKind == Request->Kind);
    ASSERT(
        FspFileNodeSharingViolationGeneral == SharingViolationReason ||
        FspFileNodeSharingViolationMainFile == SharingViolationReason ||
        FspFileNodeSharingViolationStream == SharingViolationReason);

    Success = DEBUGTEST(90) &&
        FspFileNodeTryAcquireSharedF(ExtraFileNode, FspFileNodeAcquireMain, CanWait);
    if (!Success)
        return FspWqRepostIrpWorkItem(Irp,
            FspFsvolCreateSharingViolationOplock, FspFsvolCreateRequestFini);

    if (!CanWait)
    {
        /*
         * If there is no Batch or Handle oplock we are done; else retry in a
         * worker thread to break the oplocks.
         */
        Success = DEBUGTEST(90) &&
            !(FspFileNodeSharingViolationMainFile == SharingViolationReason) &&
            !(FspFileNodeSharingViolationStream == SharingViolationReason) &&
            !(FspFileNodeOplockIsBatch(ExtraFileNode)) &&
            !(!FlagOn(IrpSp->Parameters.Create.Options, FILE_COMPLETE_IF_OPLOCKED) &&
                FspFileNodeOplockIsHandle(ExtraFileNode));

        FspFileNodeRelease(ExtraFileNode, Main);

        if (!Success)
            return FspWqRepostIrpWorkItem(Irp,
                FspFsvolCreateSharingViolationOplock, FspFsvolCreateRequestFini);

        FspFileNodeClose(ExtraFileNode, 0, TRUE);
        FspFileNodeDereference(ExtraFileNode);
        FspIopRequestContext(Request, FspIopRequestExtraContext) = 0;

        FspFsvolCreatePostClose(FileDesc);

        Irp->IoStatus.Information = 0;
        return STATUS_SHARING_VIOLATION;
    }
    else
    {
        Irp->IoStatus.Information = 0;

        Result = STATUS_SHARING_VIOLATION;
        if (FspFileNodeSharingViolationMainFile == SharingViolationReason)
        {
            /*
             * If a SUPERSEDE, OVERWRITE or OVERWRITE_IF operation is performed
             * on an alternate data stream and FILE_SHARE_DELETE is not specified
             * and there is a Batch or Filter oplock on the primary data stream,
             * request a break of the Batch or Filter oplock on the primary data
             * stream.
             */

            FSP_FILE_NODE *FileNode = FileDesc->FileNode;

            /* break Batch oplocks on the main file and this stream */
            Result = FspFileNodeCheckBatchOplocksOnAllStreams(FsvolDeviceObject, Irp,
                ExtraFileNode, FspFileNodeAcquireMain, &FileNode->FileName);
            if (STATUS_SUCCESS != Result)
                Result = STATUS_SHARING_VIOLATION;
        }
        else
        if (FspFileNodeSharingViolationStream == SharingViolationReason)
        {
            /*
             * If a SUPERSEDE, OVERWRITE or OVERWRITE_IF operation is performed
             * on the primary data stream and DELETE access has been requested
             * and there are Batch or Filter oplocks on any alternate data stream,
             * request a break of the Batch or Filter oplocks on all alternate data
             * streams that have them.
             */

            /* break Batch oplocks on the main file and all our streams */
            Result = FspFileNodeCheckBatchOplocksOnAllStreams(FsvolDeviceObject, Irp,
                ExtraFileNode, FspFileNodeAcquireMain, 0);
            if (STATUS_SUCCESS != Result)
                Result = STATUS_SHARING_VIOLATION;
        }
        else
        if (FspFileNodeOplockIsBatch(ExtraFileNode))
        {
            FspFileNodeRelease(ExtraFileNode, Main);

            /* wait for Batch oplock break to complete */
            Result = FspFileNodeOplockCheck(ExtraFileNode, Irp);
            ASSERT(STATUS_OPLOCK_BREAK_IN_PROGRESS != Result);
            if (STATUS_SUCCESS != Result)
                Result = STATUS_SHARING_VIOLATION;
            else
                Irp->IoStatus.Information = FILE_OPBATCH_BREAK_UNDERWAY;
        }
        else
        if (!FlagOn(IrpSp->Parameters.Create.Options, FILE_COMPLETE_IF_OPLOCKED) &&
            FspFileNodeOplockIsHandle(ExtraFileNode))
        {
            FspFileNodeRelease(ExtraFileNode, Main);

            /* wait for Handle oplock break to complete */
            Result = FspFileNodeOplockBreakHandle(ExtraFileNode, Irp, 0);
            ASSERT(STATUS_OPLOCK_BREAK_IN_PROGRESS != Result);
            if (STATUS_SUCCESS != Result)
                Result = STATUS_SHARING_VIOLATION;
        }
        else
            FspFileNodeRelease(ExtraFileNode, Main);

        Response = FspIopIrpResponse(Irp);

        FspFileNodeClose(ExtraFileNode, 0, TRUE);
        FspFileNodeDereference(ExtraFileNode);
        FspIopRequestContext(Request, FspIopRequestExtraContext) = 0;

        if (STATUS_SUCCESS != Result)
        {
            FspFsvolCreatePostClose(FileDesc);

            Response->IoStatus.Status = (UINT32)Result;
            Response->IoStatus.Information = (UINT32)Irp->IoStatus.Information;
        }

        FspIopRetryCompleteIrp(Irp, Response, &Result);
        return Result;
    }
}

static BOOLEAN FspFsvolCreateOpenOrOverwriteOplock(PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response,
    PNTSTATUS PResult)
{
    PAGED_CODE();

    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT FsvolDeviceObject = IrpSp->DeviceObject;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_NODE *FileNode = FileObject->FsContext;
    UINT32 RequestKind = FspIrpRequest(Irp)->Kind;
    ULONG OplockCount = 0;
    NTSTATUS Result;

    ASSERT(
        FspFsctlTransactReservedKind == RequestKind ||
        FspFsctlTransactOverwriteKind == RequestKind);

    /* add oplock key to the file object */
    Result = FspFileNodeOplockCheckEx(FileNode, Irp,
        OPLOCK_FLAG_OPLOCK_KEY_CHECK_ONLY);
    if (!NT_SUCCESS(Result))
    {
        *PResult = Result;
        return FALSE;
    }

    /* get current handle count */
    FspFsvolDeviceLockContextTable(FsvolDeviceObject);
    OplockCount = FileNode->HandleCount;
    FspFsvolDeviceUnlockContextTable(FsvolDeviceObject);

    /* if there are more than one handles, perform oplock check */
    if (1 < OplockCount)
    {
        Result = FspFileNodeOplockCheckAsyncEx(
            FileNode,
            FspFsctlTransactReservedKind == RequestKind ? FspFileNodeAcquireMain : FspFileNodeAcquireFull,
            (PVOID)Response,
            Irp,
            FspFsvolCreateOpenOrOverwriteOplockComplete,
            FspFsvolCreateOpenOrOverwriteOplockPrepare);
        if (STATUS_PENDING == Result)
        {
            *PResult = Result;
            return FALSE;
        }
    }

    /* is an oplock requested during Create? */
    if (STATUS_SUCCESS == Result &&
        FlagOn(IrpSp->Parameters.Create.Options, FILE_OPEN_REQUIRING_OPLOCK))
    {
        Result = FspFileNodeOplockFsctl(FileNode, Irp, OplockCount);
        ASSERT(STATUS_PENDING != Result);

        if (STATUS_SUCCESS == Result)
            /* if we added an oplock, remember to remove it later in case of failure */
            FspIopRequestContext(FspIrpRequest(Irp), FspIopRequestExtraContext) = Irp;
    }

    *PResult = Irp->IoStatus.Status = Result;
    return STATUS_SUCCESS == Result || STATUS_OPLOCK_BREAK_IN_PROGRESS == Result;
}

static VOID FspFsvolCreateOpenOrOverwriteOplockPrepare(
    PVOID Context, PIRP Irp)
{
    PAGED_CODE();

    FSP_FSCTL_TRANSACT_RSP *Response = FspFileNodeReleaseForOplock(Context);

    if (0 != Response)
        FspIopSetIrpResponse(Irp, Response);
}

static VOID FspFsvolCreateOpenOrOverwriteOplockComplete(
    PVOID Context, PIRP Irp)
{
    PAGED_CODE();

    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    NTSTATUS Result;

    if (FspFsctlTransactReservedKind == Request->Kind)
        FspIopRetryCompleteIrp(Irp, FspIopIrpResponse(Irp), &Result);
    else
    if (FspFsctlTransactOverwriteKind == Request->Kind)
        FspIopRetryPrepareIrp(Irp, &Result);
    else
        ASSERT(0);
}

NTSTATUS FspCreate(
    PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    FSP_ENTER_MJ(PAGED_CODE());

    switch (FspDeviceExtension(DeviceObject)->Kind)
    {
    case FspFsvolDeviceExtensionKind:
        FSP_RETURN(Result = FspFsvolCreate(DeviceObject, Irp, IrpSp));
    case FspFsvrtDeviceExtensionKind:
        FSP_RETURN(Result = FspFsvrtCreate(DeviceObject, Irp, IrpSp));
    case FspFsctlDeviceExtensionKind:
        FSP_RETURN(Result = FspFsctlCreate(DeviceObject, Irp, IrpSp));
    default:
        FSP_RETURN(Result = STATUS_INVALID_DEVICE_REQUEST);
    }

    FSP_LEAVE_MJ(
        "FileObject=%p[%p:\"%wZ\"], "
        "Flags=%x, "
        "DesiredAccess=%#lx, "
        "ShareAccess=%#x, "
        "Options=%#lx, "
        "FileAttributes=%#x, "
        "AllocationSize=%#lx:%#lx, "
        "Ea=%p, EaLength=%ld",
        IrpSp->FileObject, IrpSp->FileObject->RelatedFileObject, IrpSp->FileObject->FileName,
        IrpSp->Flags,
        IrpSp->Parameters.Create.SecurityContext->AccessState->OriginalDesiredAccess,
        IrpSp->Parameters.Create.ShareAccess,
        IrpSp->Parameters.Create.Options,
        IrpSp->Parameters.Create.FileAttributes,
        Irp->Overlay.AllocationSize.HighPart, Irp->Overlay.AllocationSize.LowPart,
        Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.Create.EaLength);
}
