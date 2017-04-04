using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using FIO = System.IO;
using WinFsp;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Security.AccessControl;

namespace MemFsSharp
{
    class MemFsInterface : FileSystemInteface
    {
        #region FileNodeMangement
        private readonly ConcurrentDictionary<string, FileObject> _fileObjects;

        internal class FileObject
        {
            public string FileName { get; set; }
            public RawSecurityDescriptor descriptor;
            public WinFsp.FileInfo Info { get; internal set; }
            public FileObject()
            {
                Info = new WinFsp.FileInfo();
            }

        }
        internal class RamFile : FileObject
        {

            public System.IO.MemoryStream FileData;
            public RamFile(string FName)
            {
                Info.FileAttributes = (uint)WinFsp.WinFspFileAttributes.Normal;
                Info.ChangeTime = FsHelperOp.GetFileTime();
                Info.CreationTime = FsHelperOp.GetFileTime();
                Info.LastAccessTime = FsHelperOp.GetFileTime();
                Info.LastWriteTime = FsHelperOp.GetFileTime();
                FileData = new System.IO.MemoryStream();
                FileName = FName;
            }
        }
        internal class MemDir : FileObject
        {
            public ConcurrentDictionary<string, FileObject> Childeren { get; private set; }
            private long IndexCount = 0;
            public MemDir(string FName)
            {
                Info.FileAttributes = WinFspFileAttributes.Directory;
                Childeren = new ConcurrentDictionary<string, FileObject>(StringComparer.OrdinalIgnoreCase);
                FileName = FName;
                Info.ChangeTime = FsHelperOp.GetFileTime();
                Info.CreationTime = FsHelperOp.GetFileTime();
                Info.LastAccessTime = FsHelperOp.GetFileTime();
                Info.LastWriteTime = FsHelperOp.GetFileTime();
            }

            public long GetNextIndex()
            {
                return Interlocked.Increment(ref IndexCount);
            }
        }
        internal FileObject GetFileObject(string path)
        {
            path = path.TrimEnd('\\');
            FileObject retFileObject;
            _fileObjects.TryGetValue(path, out retFileObject);
            return retFileObject;
        }
        uint CreateFileObject(string path, bool isFolder, FileObject parent, out FileObject ret)
        {
            path = path.TrimEnd('\\');
            var newName = System.IO.Path.GetFileName(path);
            var dir = parent as MemDir;
            FileObject newFileObject;
            ret = null;
            if (dir != null)
            {

                if (isFolder)
                    ret = newFileObject = new MemDir(newName);
                else
                    ret = newFileObject = new RamFile(newName);

                if (!dir.Childeren.TryAdd(newName, newFileObject))
                    return NtStatus.ObjectNameCollision;
                else
                    newFileObject.Info.IndexNumber = (ulong)dir.GetNextIndex();

            }
            else
                return NtStatus.InvalidParameter;

            if (!_fileObjects.TryAdd(path, newFileObject))
            {
                return NtStatus.ObjectNameCollision;
            }
            ret = newFileObject;
            return NtStatus.Success;
        }
        internal FileObject RemoveFileObject(string fileName, FileObject parent)
        {
            fileName = fileName.TrimEnd('\\');
            var targetFileName = System.IO.Path.GetFileName(fileName);
            var dir = parent as MemDir;
            FileObject removeFileObject;
            if (dir != null)
            {
                if (!_fileObjects.TryRemove(fileName, out removeFileObject))
                    throw new KeyNotFoundException("FileObject not exist in Db.");
                if (!dir.Childeren.TryRemove(targetFileName, out removeFileObject))
                    throw new KeyNotFoundException("FileObject not exist in Dir.");
            }
            else
                throw new InvalidOperationException("Parent Object can be create in directory only");

            return removeFileObject;
        }
        internal uint MoveObject(string oldPath, string newPath, bool replace)
        {
            oldPath = oldPath.TrimEnd('\\');
            newPath = newPath.TrimEnd('\\');

            var srcPrnt = GetFileObject(System.IO.Path.GetDirectoryName(oldPath).TrimEnd('\\'));
            var dstPrnt = GetFileObject(System.IO.Path.GetDirectoryName(newPath).TrimEnd('\\'));
            var src = GetFileObject(oldPath);

            var newName = System.IO.Path.GetFileName(newPath);
            var oldname = System.IO.Path.GetFileName(oldPath);
            var dst = GetFileObject(newPath);
            var dstDir = dstPrnt as MemDir;
            var srcDir = srcPrnt as MemDir;
            if (srcPrnt == null || srcDir == null)
                return NtStatus.InvalidParameter;
            if (dstPrnt == null || dstDir == null)
                return NtStatus.InvalidParameter;
            if (dst != null)
            {
                if (!replace)
                {
                    return NtStatus.ObjectNameCollision;
                }
                else
                {
                    RemoveFileObject(newPath, dstPrnt);
                }
            }

            src.FileName = newName;
            if (!srcDir.Childeren.TryRemove(oldname, out src))
                throw new KeyNotFoundException("FileObject not found in sohurce dir");
            if (!_fileObjects.TryAdd(newPath, src))
                throw new System.IO.IOException("FileObject with same name already exist in Db.");
            if (!dstDir.Childeren.TryAdd(newName, src))
                throw new System.IO.IOException("FileObject with same name already exist in directory.");

            return NtStatus.Success;
        }
        #endregion

        FileSystem _fileSystem;
        VolumeInformation _volInfo;
        FileSystemConfig _fsConfig;
        MemDir rootDirectory;
        bool _isMounted;
        object _globalFslock;
        private void InitializeConfig()
        {
            _fsConfig = new FileSystemConfig();
            _fsConfig.FileSystemName = "MemFs";
            _fsConfig.UnicodeOnDisk = true;
            _fsConfig.VolumeCreationTime = (uint)DateTime.Now.Ticks;
            _fsConfig.MaxComponentLength = 256;
            _fsConfig.FileInfoTimeOut = 25;
            _fsConfig.RootSecurityDescriptor = "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
        }
        private void InitializeVolInfo() {
            _volInfo = new VolumeInformation();
            _volInfo.FreeSize = (ulong)1024 * 1024 * 1024 * 10;
            _volInfo.TotalSize = (ulong)1024 * 1024 * 1024 * 10;
            _volInfo.VolumeLabel = "MemFs";
            rootDirectory = new MemDir("\\");
            rootDirectory.descriptor = new RawSecurityDescriptor(_fsConfig.RootSecurityDescriptor);
            _fileObjects.TryAdd("\\", rootDirectory);
            _fileObjects.TryAdd("", rootDirectory);
        }
        public MemFsInterface() {
            _fileObjects = new ConcurrentDictionary<string, FileObject>(StringComparer.OrdinalIgnoreCase);
            InitializeConfig();
            InitializeVolInfo();
            _fileSystem = FileSystem.BuildFileSystem(_fsConfig, _volInfo, this);
            _globalFslock = new object();
        }
        public void MountFileSystem(string mountPoint)
        {
            lock (_globalFslock)
            {
                if (!_isMounted) {
                    _fileSystem.MountFileSystem(mountPoint);
                    _isMounted = true;
                    return;
                }
                throw new InvalidOperationException("Filesystem is already mounted");
            }
        }

        public override uint CanDelete(FileSystem FileSystem, FspFileContext FileContext, string FileName)
        {
            FileName = FileName.TrimEnd('\\');
            var dir = GetFileObject(FileName);
            if (dir != null)
            {
                if (dir is MemDir)
                {
                    var dirL = dir as MemDir;
                    if (dirL.Childeren.Count > 0)
                        return NtStatus.DirectoryNotEmpty;
                    else
                        return NtStatus.Success;
                }
                else
                    return NtStatus.Success;
            }
            else
                return NtStatus.ObjectNotFound;
        }
        public override void Cleanup(FileSystem FileSystem, FspFileContext FileContext, string FileName, uint Flags)
        {
            var fileObject = GetFileObject(FileName);
            if (fileObject == null)
                return;
            if (((uint)CleanUpFlags.FspCleanupDelete & Flags) != 0)
            {
                if (fileObject is MemDir && (fileObject as MemDir).Childeren.Count > 0)
                    return;
                string parentFile = FIO.Path.GetDirectoryName(FileName);
                var parentDir = GetFileObject(parentFile);
                if (parentDir != null && parentDir is MemDir)
                {
                    RemoveFileObject(FileName, parentDir);
                    Trace.WriteLine($"Delete FileName {FileName} {GetFileObject(FileName)}");
                    //Trace.WriteLine($"List Parent Dir- "+string.Join(",",(dir as RamDir).Childeren.Keys.ToArray()));

                }
            }
            if ((Flags & CleanUpFlags.FspCleanupSetAllocationSize) != 0)
            {
                if (fileObject is RamFile)
                {

                }

            }
        }
        public override void Close(FileSystem FileSystem, FspFileContext FileContext)
        {           
        }
        public override uint Create(FileSystem FileSystem, string FileName, uint CreateOptions, uint GrantedAccess, uint fileAttributes, ref RawSecurityDescriptor descriptor, ulong AllocationSize, FspFileContext fileContext, ref FileInfo fileInfo)
        {
            var parentName = FIO.Path.GetDirectoryName(FileName);
            var parent = GetFileObject(parentName);

            if (parent == null)
                return NtStatus.ObjectPathNotFound;


            var isFolder = (WinFspFileCreateOption.DirectoryFile & CreateOptions) > 0;

            FileObject file;
            uint status = CreateFileObject(FileName, isFolder, parent, out file);
            Trace.WriteLine($"Create FileName {FileName} {GetFileObject(FileName)} status {String.Format("{0:X}", status)}");
            if (NtStatus.Success!= status)
            {
                return status;
            }

            if (file == null)
            {
                return (uint)NtStatus.ObjectNotFound;
            }

            fileContext.FileInfo = file.Info ;
            fileContext.FileNode = file;

            if (!isFolder)
            {
                var ramFile = file as RamFile;            
                fileContext.FileInfo.AllocationSize = AllocationSize;
                ramFile.FileData.SetLength((long)AllocationSize);
                fileContext.FileInfo.FileAttributes = WinFspFileAttributes.Normal;
            }
            else
            {
                fileContext.FileInfo.FileAttributes = WinFspFileAttributes.Directory;
            }
            file.descriptor = descriptor;                      
            return NtStatus.Success;
        }
   
        public override uint Flush(FileSystem FileSystem, FspFileContext FileContext, ref FileInfo FileInfo)
        {
            return NtStatus.Success;
        }
        public override uint GetFileInfo(FileSystem FileSystem, FspFileContext FileContext, ref FileInfo FileInfo)
        {

            var file = FileContext.FileNode;
            if (file is RamFile)
            {
                RamFile ramFile = (file as RamFile);
                FileContext.FileInfo.AllocationSize = (ulong)ramFile.FileData.Capacity;
                FileContext.FileInfo.FileSize = (ulong)ramFile.FileData.Length;
            }
            return NtStatus.Success;
        }
        
        
        
        public override uint GetVolumeInfo(FileSystem fileSystem, ref VolumeInformation volumeInfo)
        {
            volumeInfo = _volInfo;
            return NtStatus.Success;
        }
        public override uint Open(FileSystem fileSystem, string fileName, uint CreateOptions, uint GrantedAccess, FspFileContext FileContext, ref FileInfo FileInfo)
        {
            fileName = fileName.TrimEnd('\\');
            var file = GetFileObject(fileName);
            Trace.WriteLine($"Open FileName {fileName} fileFound {file != null}");
            if (file == null)
                return NtStatus.ObjectNotFound;

            FileContext.FileInfo = file.Info;
            FileContext.FileNode = file;
            return NtStatus.Success;
        }
        public override uint Overwrite(FileSystem FileSystem, FspFileContext FileContext, uint FileAttributes, bool ReplaceFileAttributes, ulong AllocationSize, ref FileInfo FileInfo)
        {
            if (FileContext.FileNode == null)
                return NtStatus.InvalidParameter;
            var file = FileContext.FileNode as FileObject;
            Trace.WriteLine($"Overwrite FileName {file.FileName} fileFound {file != null}");

            file.Info.FileSize = 0;
            file.Info.LastAccessTime = file.Info.LastWriteTime = FsHelperOp.GetFileTime() ;
            return NtStatus.Success;
        }
        public override uint Read(FileSystem FileSystem, FspFileContext FileContext, IntPtr Buffer, ulong Offset, uint Length, ref uint BytesTransferred)
        {
            if (FileContext.FileNode == null)
                return NtStatus.InvalidParameter;

            var file = FileContext.FileNode as RamFile;
            if (file == null)
                return NtStatus.InvalidParameter;

            if ((ulong)Offset >= file.Info.FileSize)
                return NtStatus.EndOfFile;

            ulong EndOfOffset = Offset + Length;

            if (EndOfOffset > file.Info.FileSize)
                EndOfOffset = file.Info.FileSize;

            int length = (int)(EndOfOffset - Offset);

            byte[] data = new byte[length];

            file.FileData.Seek((long)Offset, FIO.SeekOrigin.Begin);
            file.FileData.Read(data, 0, length);

            Marshal.Copy(data, 0, Buffer, length);
            BytesTransferred = (uint)length;
            return NtStatus.Success;
        }
        public override uint ReadDirectory(FileSystem FileSystem, FspFileContext FileContext, string Pattern, string Marker, ReadDirectoryBuffer buffer)
        {
            if (FileContext == null || FileContext.FileNode == null || FileContext.FileInfo== null)
                return NtStatus.InvalidParameter;
            var dir = FileContext.FileNode as MemDir;

            if (dir == null)
                return NtStatus.InvalidParameter;
            bool gotMarker = false;

            foreach (var item in dir.Childeren.Values)
            {


                if (!gotMarker)
                    gotMarker = string.IsNullOrEmpty(Marker) || item.FileName == Marker;
                if (gotMarker)
                {
                    if (!string.IsNullOrEmpty(Pattern) && !Regex.IsMatch(item.FileName, Pattern, RegexOptions.IgnoreCase))
                        continue;
                    if (!buffer.AddItem(item.FileName, item.Info))
                        break;
                }
            }
            buffer.SetEoF();
            return NtStatus.Success;
        }
        public override uint Rename(FileSystem FileSystem, FspFileContext FileContext, string FileName, string NewFileName, bool ReplaceIfExists)
        {
            if (FileContext == null || FileContext.FileNode == null || FileContext.FileInfo == null)
                return NtStatus.InvalidParameter;

            return MoveObject(FileName, NewFileName, ReplaceIfExists);
        }        
        public override uint SetBasicInfo(FileSystem FileSystem, FspFileContext FileContext, uint FileAttributes, ulong CreationTime, ulong LastAccessTime, ulong LastWriteTime, ulong ChangeTime, ref FileInfo FileInfo)
        {
            if (FileContext.FileNode == null)
                return NtStatus.InvalidParameter;
            if (FileAttributes != WinFspFileAttributes.InvalidAttribute)
                FileContext.FileInfo.FileAttributes = FileAttributes;
            if (CreationTime != 0)
                FileContext.FileInfo.CreationTime = CreationTime;
            if (LastAccessTime != 0)
                FileContext.FileInfo.LastAccessTime = LastAccessTime;
            if (LastWriteTime != 0)
                FileContext.FileInfo.LastWriteTime = LastWriteTime;
            return NtStatus.Success;
        }
        public override uint SetFileSize(FileSystem FileSystem, FspFileContext FileContext, ulong NewSize, bool SetAllocationSize, ref FileInfo FileInfo)
        {
            if (FileContext.FileNode == null)
                return NtStatus.InvalidParameter;

            var file = FileContext.FileNode as RamFile;

            if (file == null)
                return NtStatus.InvalidParameter;
            //     Console.WriteLine("Size: Length {0}", NewSize);
            if (SetAllocationSize)
            {
                if (file.Info.AllocationSize != NewSize)
                    file.Info.AllocationSize = NewSize;
                file.Info.FileSize = NewSize;
                file.FileData.SetLength((long)NewSize);
            }
            else
            {
                if (file.Info.FileSize != NewSize)
                {
                    if (file.Info.AllocationSize < NewSize)
                    {
                        uint AllocationUnit = (uint)_fsConfig.SectorSize * (uint)_fsConfig.SectorPerAllocationUnit;
                        uint AllocationSize = (uint)(NewSize + AllocationUnit - 1) / AllocationUnit * AllocationUnit;
                        SetFileSize(FileSystem, FileContext, AllocationSize, true,ref FileContext.FileInfo);
                    }
                    file.Info.FileSize = NewSize;
                }
            }
            return NtStatus.Success;
        }      
        public override uint SetSecurity(FileSystem FileSystem, FspFileContext FileContext, uint SecurityInformation, RawSecurityDescriptor ModificationDescriptor)
        {
            return NtStatus.Success;
        }
        public override uint SetVolumeLabelA(FileSystem fileSystem, string volumeLabel, ref VolumeInformation volumeInfo)
        {
            return NtStatus.Success;
        }
        public override uint Write(FileSystem FileSystem, FspFileContext FileContext, IntPtr Buffer, ulong Offset, uint Length, bool WriteToEndOfFile, bool ConstrainedIo, ref uint BytesTransferred, ref FileInfo FileInfo)
        {
            if (FileContext.FileNode == null)
                return NtStatus.InvalidParameter;

            var file = FileContext.FileNode as RamFile;

            if (file == null)
                return NtStatus.InvalidParameter;
            ulong EndOffset = 0;
            if (ConstrainedIo)
            {
                if (Offset >= file.Info.FileSize)
                    return NtStatus.Success;
                EndOffset = Length + Offset;
                if (EndOffset > file.Info.FileSize)
                    EndOffset = file.Info.FileSize;
            }
            else
            {
                if (WriteToEndOfFile)
                    Offset = file.Info.FileSize;
                EndOffset = Offset + Length;
                if (EndOffset > file.Info.FileSize)
                    SetFileSize(FileSystem, FileContext, EndOffset, false,ref FileContext.FileInfo);
            }            
            ulong effectiveLength = EndOffset - Offset;
            file.FileData.Seek((long)Offset, FIO.SeekOrigin.Begin);
            var data = new byte[Length];            
            Marshal.Copy(Buffer, data, 0, (int)Length);
            file.FileData.Write(data, 0, (int)effectiveLength);
            _volInfo.FreeSize -= (ulong)effectiveLength;
            BytesTransferred=(uint)effectiveLength;

            return NtStatus.Success;
        }
        public override uint GetSecurityByName(FileSystem fileSystem, string fileName, ref uint fileAttributes, ref RawSecurityDescriptor descriptor)
        {
            
            fileName = fileName.TrimEnd('\\');
            var file = GetFileObject(fileName);
            Trace.WriteLine($"GetSecurity FileName {fileName} fileFound {file != null}");
            if (file == null)
                return NtStatus.ObjectNotFound;
            descriptor = file.descriptor;
            fileAttributes = file.Info.FileAttributes;
            descriptor.DiscretionaryAcl.
            return NtStatus.Success;
            
        }

    }
}
