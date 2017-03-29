using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using WinFsp;

namespace MemFsSharp
{
    class MemFsImpl : FileSystemInteface
    {
        public override uint CanDelete(FileSystem FileSystem, FspFileContext FileContext, string FileName)
        {
            return base.CanDelete(FileSystem, FileContext, FileName);
        }
        public override void Cleanup(FileSystem FileSystem, FspFileContext FileContext, string FileName, uint Flags)
        {
            base.Cleanup(FileSystem, FileContext, FileName, Flags);
        }
        public override void Close(FileSystem FileSystem, FspFileContext FileContext)
        {
            base.Close(FileSystem, FileContext);
        }
        public override uint Create(FileSystem FileSystem, string FileName, uint CreateOptions, uint GrantedAccess, uint fileAttributes, ref SecurityDescriptor descriptor, ulong AllocationSize, FspFileContext fileContext, ref FileInfo fileInfo)
        {
            return base.Create(FileSystem, FileName, CreateOptions, GrantedAccess, fileAttributes, ref descriptor, AllocationSize, fileContext, ref fileInfo);
        }
        public override uint DeleteReparsePoint(FileSystem FileSystem, FspFileContext FileContext, string FileName, string ReparseData)
        {
            return base.DeleteReparsePoint(FileSystem, FileContext, FileName, ReparseData);
        }
        public override uint Flush(FileSystem FileSystem, FspFileContext FileContext, ref FileInfo FileInfo)
        {
            return base.Flush(FileSystem, FileContext, ref FileInfo);
        }
        public override uint GetFileInfo(FileSystem FileSystem, FspFileContext FileContext, ref FileInfo FileInfo)
        {
            return base.GetFileInfo(FileSystem, FileContext, ref FileInfo);
        }
        public override uint GetReparsePoint(FileSystem FileSystem, FspFileContext FileContext, string FileName, IntPtr Buffer, ref ulong Size)
        {
            return base.GetReparsePoint(FileSystem, FileContext, FileName, Buffer, ref Size);
        }
        public override uint GetSecurity(FileSystem FileSystem, FspFileContext FileContext, ref SecurityDescriptor SecurityDescriptor)
        {
            return base.GetSecurity(FileSystem, FileContext, ref SecurityDescriptor);
        }
        public override uint GetSecurityByName(FileSystem fileSystem, string fileName, ref uint fileAttributes, ref SecurityDescriptor descriptor)
        {
            return base.GetSecurityByName(fileSystem, fileName, ref fileAttributes, ref descriptor);
        }
        public override uint GetVolumeInfo(FileSystem fileSystem, ref VolumeInformation volumeInfo)
        {
            return base.GetVolumeInfo(fileSystem, ref volumeInfo);
        }
        public override uint Open(FileSystem fileSystem, string fileName, uint CreateOptions, uint GrantedAccess, FspFileContext FileContext, ref FileInfo FileInfo)
        {
            return base.Open(fileSystem, fileName, CreateOptions, GrantedAccess, FileContext, ref FileInfo);
        }
        public override uint Overwrite(FileSystem FileSystem, FspFileContext FileContext, uint FileAttributes, bool ReplaceFileAttributes, ulong AllocationSize, ref FileInfo FileInfo)
        {
            return base.Overwrite(FileSystem, FileContext, FileAttributes, ReplaceFileAttributes, AllocationSize, ref FileInfo);
        }
        public override uint Read(FileSystem FileSystem, FspFileContext FileContext, IntPtr Buffer, ulong Offset, uint Length, ref uint BytesTransferred)
        {
            return base.Read(FileSystem, FileContext, Buffer, Offset, Length, ref BytesTransferred);
        }
        public override uint ReadDirectory(FileSystem FileSystem, FspFileContext FileContext, string Pattern, string Marker, ReadDirectoryBuffer buffer)
        {
            return base.ReadDirectory(FileSystem, FileContext, Pattern, Marker, buffer);
        }
        public override uint Rename(FileSystem FileSystem, FspFileContext FileContext, string FileName, string NewFileName, bool ReplaceIfExists)
        {
            return base.Rename(FileSystem, FileContext, FileName, NewFileName, ReplaceIfExists);
        }
        public override uint ResolveReparsePoints(FileSystem FileSystem, string FileName, uint ReparsePointIndex, bool ResolveLastPathComponent, ref IoStatusBlock PIoStatus, string ResolveName)
        {
            return base.ResolveReparsePoints(FileSystem, FileName, ReparsePointIndex, ResolveLastPathComponent, ref PIoStatus, ResolveName);
        }
        public override uint SetBasicInfo(FileSystem FileSystem, FspFileContext FileContext, uint FileAttributes, ulong CreationTime, ulong LastAccessTime, ulong LastWriteTime, ulong ChangeTime, ref FileInfo FileInfo)
        {
            return base.SetBasicInfo(FileSystem, FileContext, FileAttributes, CreationTime, LastAccessTime, LastWriteTime, ChangeTime, ref FileInfo);
        }
        public override uint SetFileSize(FileSystem FileSystem, FspFileContext FileContext, ulong NewSize, bool SetAllocationSize, ref FileInfo FileInfo)
        {
            return base.SetFileSize(FileSystem, FileContext, NewSize, SetAllocationSize, ref FileInfo);
        }
        public override uint SetReparsePoint(FileSystem FileSystem, FspFileContext FileContext, string FileName, string ReparseData)
        {
            return base.SetReparsePoint(FileSystem, FileContext, FileName, ReparseData);
        }
        public override uint SetSecurity(FileSystem FileSystem, FspFileContext FileContext, uint SecurityInformation, SecurityDescriptor ModificationDescriptor)
        {
            return base.SetSecurity(FileSystem, FileContext, SecurityInformation, ModificationDescriptor);
        }
        public override uint SetVolumeLabelA(FileSystem fileSystem, string volumeLabel, ref VolumeInformation volumeInfo)
        {
            return base.SetVolumeLabelA(fileSystem, volumeLabel, ref volumeInfo);
        }
        public override uint Write(FileSystem FileSystem, FspFileContext FileContext, IntPtr Buffer, ulong Offset, uint Length, bool WriteToEndOfFile, bool ConstrainedIo, ref uint BytesTransferred, ref FileInfo FileInfo)
        {
            return base.Write(FileSystem, FileContext, Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, ref BytesTransferred, ref FileInfo);
        }

    }
}
