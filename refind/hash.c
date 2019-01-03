/*
 * refind/hash.c
 * Stuff related to hashing files, directories and options for booting
 *
 * Copyright (c) 2018 Tim Engler
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lib.h"
#include "hash.h"
#include "screen.h"
#include "../include/refit_call_wrapper.h"
#include "mystrings.h"
#include "sha256.h"

//for logHack
#define GetTime ST->RuntimeServices->GetTime

//FIXME hack
static EFI_FILE_HANDLE logHandle;
static BOOLEAN logOpened = FALSE;
static CHAR16 *logFileName = L"log.txt";

static void SPrintTime(CHAR16 *buf, UINTN maxSize)
{
  EFI_TIME time;

  refit_call2_wrapper(GetTime, &time, NULL);

  SPrint(buf, maxSize, L"%04d-%02d-%02d %02d:%02d:%02d.%09d ",
	 time.Year,
	 time.Month,
	 time.Day,
	 time.Hour,
	 time.Minute,
	 time.Second,
	 time.Nanosecond);
}

static void logHack(CHAR16 *fmt, ...) {
  va_list          args;
  UINTN            len;
  CHAR16          Message[256];
  CHAR16          Time[256];
  EFI_STATUS      Status;
  
  if(!logOpened) {
    Status = refit_call5_wrapper(SelfVolume->RootDir->Open, SelfVolume->RootDir, &logHandle, logFileName,
				 EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if(CheckError(Status, Message)) {
      return;
    }
    logOpened = TRUE;
  }

  va_start (args, fmt);
  len = VSPrint(Message, 255, fmt, args);
  va_end (args);

  SPrintTime(Time,255);

  UINTN size;
  size = StrLen(Time) * sizeof(CHAR16);
  refit_call3_wrapper(SelfVolume->RootDir->Write, logHandle, &size, (BYTE *)Time);
  
  size = len * sizeof(CHAR16);
  refit_call3_wrapper(SelfVolume->RootDir->Write, logHandle, &size, (BYTE *)Message);
  
  refit_call1_wrapper(SelfVolume->RootDir->Flush, logHandle);
}

/*
  Reads a file and calls Func for each chunk of data.
  Func args are:
     VOID *ctx -- "ctx" as passed into ReadFileInChunks
     BYTE *buf  -- data read
     UINTN len  -- length of data
*/
EFI_STATUS ReadFileInChunks(IN EFI_FILE_HANDLE BaseDir, IN CHAR16 *FilePath, UINTN BufferSize, VOID *ctx,
			    VOID (*Func)(VOID * /*ctx*/, BYTE * /*buf*/, UINTN /*len*/))
{
    EFI_STATUS      Status;
    EFI_FILE_HANDLE FileHandle;
    //EFI_FILE_INFO   *FileInfo;
    CHAR16          Message[256];

    BYTE *Buf;

    logHack(L"ReadFileInChunks Start\n");

    Buf = AllocatePool(BufferSize);
    if (Buf == NULL) {
       return EFI_OUT_OF_RESOURCES;
    }

    // read the file, allocating a buffer on the way
    Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &FileHandle, FilePath, EFI_FILE_MODE_READ, 0);
    SPrint(Message, 255, L"while loading the file '%s'", FilePath);
    if (CheckError(Status, Message))
        return Status;

    /* FileInfo = LibFileInfo(FileHandle); */
    /* if (FileInfo == NULL) { */
    /*     // TODO: print and register the error */
    /*     refit_call1_wrapper(FileHandle->Close, FileHandle); */
    /*     return EFI_LOAD_ERROR; */
    /* } */
    /* ReadSize = FileInfo->FileSize; */
    /* FreePool(FileInfo); */

    for(;;) {
      UINTN CurrSize = BufferSize;
      
      Status = refit_call3_wrapper(FileHandle->Read, FileHandle, &CurrSize, Buf);

      if (CheckError(Status, Message)) {
        MyFreePool(Buf);
        refit_call1_wrapper(FileHandle->Close, FileHandle);
        return Status;
      }

      (*Func)(ctx, Buf, CurrSize);

      if(CurrSize < BufferSize) break;
    }
    
    Status = refit_call1_wrapper(FileHandle->Close, FileHandle);

    MyFreePool(Buf);

    logHack(L"ReadFileInChunks End\n");
    return EFI_SUCCESS;
}

const BYTE * SEP = (BYTE *)"\0\0\0";
const size_t SEP_LEN = sizeof(CHAR8) * 3;


VOID HashSep(SHA256_CTX *ctx)
{
  Sha256Update(ctx, SEP, SEP_LEN);
}
  

VOID HashCHAR16NTA(SHA256_CTX *ctx, CHAR16 *data)
{
  if(data == NULL) return;
  size_t len = StrLen(data) * sizeof(CHAR16);

  Sha256Update(ctx, (BYTE *)data, len);
}

VOID HashDataFunc(void *vctx, BYTE *buf, UINTN size)
{
  SHA256_CTX *ctx = (SHA256_CTX *)vctx;
		     
  logHack(L"HashDataFunc Start\n");
  Sha256Update(ctx, buf, size);
  logHack(L"HashDataFunc End\n");
}


#define CHUNK_SIZE (1024*1024*8)

VOID HashFile(SHA256_CTX *ctx, REFIT_VOLUME *DeviceVolume, CHAR16 *filePath)
{
  //hash the name of the volume
  HashCHAR16NTA(ctx, DeviceVolume->VolName);

  HashSep(ctx);
  
  //hash the partition name
  HashCHAR16NTA(ctx, DeviceVolume->PartName);

  HashSep(ctx);

  //hash the full path of the filename
  HashCHAR16NTA(ctx, filePath);

  HashSep(ctx);

  //now the data
  if (!FileExists(DeviceVolume->RootDir, filePath)) return;

  //TODO we should somehow report if there is an error
  ReadFileInChunks(DeviceVolume->RootDir, filePath, CHUNK_SIZE, ctx, HashDataFunc);
}

#define FixUpRoot(x) (x[0] == '\0' ? L"\\" : x)
  
static VOID HashDirRecursive(UINTN recursiveCount, SHA256_CTX *ctx, REFIT_VOLUME *Volume, CHAR16 *dirPath)
{
  REFIT_DIR_ITER          DirIter;
  EFI_FILE_INFO           *DirEntry;
  
  CHAR16 **Dirs = NULL;
  UINTN DirsCount = 0;

  logHack(L"%02d, HashDirRecursive for %s\n",recursiveCount, dirPath);

  DirIterOpen(Volume->RootDir, FixUpRoot(dirPath), &DirIter);
  while (DirIterNext(&DirIter, 0, NULL, &DirEntry)) {
    if (StrCmp(DirEntry->FileName,L".") == 0 || StrCmp(DirEntry->FileName,L"..") == 0)
      continue;   // skip "." and ".." (not sure if these will be
    		  // returned, but just to be safe)

    logHack(L"%02d, DirEntry %s, dirsCount %d, attr %d, isDir? %d\n",recursiveCount, DirEntry->FileName,DirsCount, DirEntry->Attribute,
	    DirEntry->Attribute & EFI_FILE_DIRECTORY);

    if (DirEntry->Attribute & EFI_FILE_DIRECTORY) {
      AddListElement((VOID ***)&Dirs,&DirsCount, (VOID *)StrDuplicate(DirEntry->FileName));
    }
    else {
      CHAR16 *FilePath = StrDuplicate(dirPath);
      MergeStrings(&FilePath, DirEntry->FileName,'\\');
      HashFile(ctx, Volume, FilePath);
      MyFreePool(FilePath);
    }
  }
  DirIterClose(&DirIter);

  for(int i = 0; i< DirsCount; i++) {
    CHAR16 *result = StrDuplicate(dirPath);
    MergeStrings(&result, Dirs[i],'\\');
    logHack(L"%02d, RecursiveCall dirPath %s, Dirs[%d] %s, result %s\n",recursiveCount,dirPath, i, Dirs[i], result);
    HashDirRecursive(recursiveCount+1, ctx, Volume, result);
    MyFreePool(result);
  }

  FreeList((VOID ***)&Dirs,&DirsCount);
}

static VOID HashHashPath(SHA256_CTX *ctx, REFIT_VOLUME *Volume, CHAR16 *hashPathC)
{
  CHAR16 *hashPath = StrDuplicate(hashPathC);
  CleanUpPathNameSlashes(hashPath);

  CHAR16 *filePattern = hashPath + StrLen(hashPath);

  while(filePattern > hashPath) {
    filePattern --;
    if(*filePattern == '\\') {
      //split the path and the trailing filename with a '\0' the end
      //result is that hashPath and filePattern become separate strings
      //(but hashPath alone must still be freed)
      *filePattern = '\0';
      filePattern++;
      break;
    }
  }

  REFIT_DIR_ITER          DirIter;
  EFI_FILE_INFO           *DirEntry;

  CHAR16 **Dirs = NULL;
  UINTN DirsCount = 0;
  
  DirIterOpen(Volume->RootDir, FixUpRoot(hashPath), &DirIter);
  while (DirIterNext(&DirIter, 0, filePattern, &DirEntry)) {
    //if the file pattern doesn't explicity start with a '.' we ignore files starting with "."
    if(filePattern[0] != '.') {
      if (DirEntry->FileName[0] == '.')
	  continue;   // skip any file that starts with a "."
    }
    else {
      if (StrCmp(DirEntry->FileName,L".") == 0 || StrCmp(DirEntry->FileName,L"..") == 0)
	continue;   // skip "." and ".." (not sure if these will be
		    // returned, but just to be safe)
    }
    
    if (DirEntry->Attribute & EFI_FILE_DIRECTORY)
      AddListElement((VOID ***)&Dirs,&DirsCount, (VOID *)DirEntry->FileName);
    else {
      CHAR16 *FilePath = StrDuplicate(hashPath);
      MergeStrings(&FilePath, DirEntry->FileName,'\\');
      HashFile(ctx, Volume, FilePath);
      MyFreePool(FilePath);
    }
  }

  DirIterClose(&DirIter);

  for(int i = 0; i < DirsCount; i++) {
    CHAR16 *DirPath = StrDuplicate(hashPath);
    MergeStrings(&DirPath, Dirs[i],'\\');
    HashDirRecursive(0, ctx, Volume, DirPath);
    MyFreePool(DirPath);
  }

  FreeList((VOID ***)&Dirs,&DirsCount);
  MyFreePool(hashPath);
}
		    
VOID GenerateHash(LOADER_ENTRY *Entry)
{
  SHA256_CTX ctx;
  Sha256Init(&ctx);

  HashCHAR16NTA(&ctx,Entry->LoaderPath);
  HashCHAR16NTA(&ctx,Entry->LoadOptions);
  HashCHAR16NTA(&ctx,Entry->InitrdPath);

  if(Entry->HashPaths != NULL) {
    for(int i = 0; i < Entry->HashPathsCount; i++)
      HashHashPath(&ctx, Entry->Volume, Entry->HashPaths[i]);
  }
  else {
    CHAR16 *VolName = NULL;
    CHAR16 *Path = NULL;
    CHAR16 *Filename = NULL;
    
    SplitPathName(Entry->LoaderPath, &VolName, &Path, &Filename);

    REFIT_VOLUME *Volume;

    if(VolName != NULL) {
      FindVolume(&Volume, VolName);
    }
    else
      Volume = Entry->Volume;

    if(Volume == NULL)
      ; // TODO error reporting
    else {
      logHack(L"GenerateHash calling HashDirRecursive Volume %s, Path %s Filename %s\n",VolName, Path, Filename);
      
      HashDirRecursive(0,&ctx, Volume, Path);
    }

    MyFreePool(VolName);
    MyFreePool(Path);
    MyFreePool(Filename);
  }

  //TODO Add in error reporting
  MyFreePool(Entry->Hash);
  Entry->Hash = AllocatePool(32 * sizeof(unsigned char));
  Entry->HashLength = 32;
  
  if(Entry->Hash == NULL) return;
  Sha256Final(&ctx, Entry->Hash);
  
  logHack(L"End GenerateHash\n");
}
