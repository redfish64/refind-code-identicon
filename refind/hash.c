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

#include "hash.h"

UINTN FindNextSep(CHAR16 *data, UINTN sepIndex, CHAR16 sep)
{
  for(;;) {
    sepIndex++;
    if(data[sepIndex] == sep || data[sepIndex] == '\0')
      return sepIndex;
  }
}

#define MAX_DIR_DEPTH 255
#define MAX_PATH_LENGTH 2048

VOID MyFreeArrOfArr(VOID **x)
{
  if(x == NULL) return;
  
  VOID **cx = x;

  while(cx)
    MyFreePool(cx++);
  MyFreePool(x);
}

struct DirData {
  CHAR16 **entries; //entries in the directory that match the current glob
  UINTN currEntryIndex; //index of current entry in entries
  int pathIndex; //the char offset of the directory in the path variable
  int globStartIndex; // start position of the current glob
  int globEndIndex; // end position of the current glob
} typedef DirData;

VOID FindFilesMatchingGlob(VOID *data, VOID (*func)(VOID *, REFIT_VOLUME *, CHAR16 *),
			   REFIT_VOLUME *volume, CHAR16 *globc)
{
  DirData dirData[MAX_DEPTH];
  int dirDataCount = 1;

  //we start by adding the root directory to our dir stack
  dirData[0] = { NULL,
		 0,
		 0,
		 1, //we set pathIndex to 1 because of the initial '\\' in the path
		 0 };

  //we want to cleanup the glob so that it has just '\\' as a separator and no starting or trailing '\\'
  //so we allocate a new string to do this, because CleanUpPathNameSlashes writes over the input data
  CHAR16 *glob = StrDuplicate(globc);
  
  if(glob == NULL || glob[0] == '\0') return;
  
  CleanUpPathNameSlashes(glob);
  
  if(glob == NULL) return;
  
  //we start out with path as just the first \\
  CHAR16 currPath[MAX_PATH_LENGTH];
  StrCpy16(currPath,L"\\");

  while(dirDataCount > 0) {
    DirData *dd = &dirData[dirDataCount-1];

    //if we get to the max depth, we have to rewind
    if(dirDataCount == MAX_DEPTH) {
      MyFreeArrOfArr(dd->entries);
      dd->entries = NULL;
      dirDataCount --;
      continue;
    }

    //if we haven't loaded the entries yet
    if(dd->entries == NULL) {
      dd->globEndIndex = FindNextSep(glob, dd->globSepIndex,'\\');
      dd->entries = GetDirFilesMatchingGlob(volume, currPath, &(glob[dd->globStartIndex]), dd->globEndIndex - dd->globStartIndex);
      dd->currEntryIndex = 0;
    }

    //look for the next entry to process

    //if we have no entries at all or we're out of entries
    if(dd->entries == NULL || dd->entries[dd->currEntryIndex] == '\0') {
      //free it and go back
      MyFreeArrOfArr(dd->entries);
      dd->entries = NULL;
      dirDataCount--;
      continue;
    }

    CHAR16 *entry = dd->entries[dd->currEntryIndex];

    //if we don't have room to store the entry in the path, we have to skip it (+1 for the '\\')
    if(StrLen16(entry) + StrLen16(currPath) + 1 > MAX_PATH_LENGTH-1) {
      dd->currEntryIndex++;
      continue;
    }

    //add the entry to the path
    currPath[dd->pathIndex]='\\';
    StrCpy16(&(currPath[dd->pathIndex+1]),entry);

    dd->currEntryIndex++;

    //if we are at the end of the glob
    if(glob[dd->globEndIndex] == '\0') {
      (*func)(data, vol, currPath);
    }
    else {
      //we need to create a new dirData to search through the 

      dirDataCount ++;

      DirData *ndd = dd+1;
      ndd->entries = NULL;
      ndd->currEntryIndex = 0;
      ndd->pathIndex = StrLen16(currPath);
      ndd->globStartIndex = dd->globEndIndex+1; //+1 for the '\\'
      ndd->globEndIndex = ndd->globStartIndex;
    }
  } 
}


VOID UpdateHashForVolPathFunc(VOID *ctx, REFIT_VOLUME *DeviceVolume, CHAR16 *path)
{
  if(IsDir(DeviceVolume, path)) {
    RecursiveFindFiles(ctx, HashFileFunc, DeviceVolume, path);
  }
  else
    HashFile(ctx, DeviceVolume, path);

  HashSep(ctx);
}

const BYTE [] SEP = "\0\0\0";
const size_t SEP_LEN = sizeof(BYTE) * 3;


VOID HashSep(SHA256_CTX *ctx)
{
  Sha256Update(ctx, SEP, SEP_LEN);
}
  
  
VOID HashFile(void *vctx, REFIT_VOLUME *DeviceVolume, CHAR16 *file)
{
  SHA256_CTX *ctx = (SHA256_CTX *)vctx;
		     
  //hash the name of the volume
  HashCHAR16NTA(ctx, DeviceVolume->VolName);

  HashSep(ctx);
  
  //hash the partition name
  HashCHAR16NTA(ctx, DeviceVolume->PartName);

  HashSep(ctx);

  //hash the full path of the filename
  HashCHAR16NTA(ctx, file);

  HashSep(ctx);

  //now the data
  if (!FileExists(DeviceVolume->RootDir, file)) return;

  ReadFileInChunks(ctx, HashDataFunc, DeviceVolume->RootDir, file);
}

  


UINTN StrLen16(CHAR16 *data)
{
  UINTN i = 0;
  while(data) {
    data++;
    i ++;
  }

  return i;
}

VOID HashCHAR16NTA(SHA256_CTX *ctx, CHAR16 *data)
{
  size_t len = StrLen16(data) * sizeof(CHAR16);

  Sha256Update(ctx, data, len);
}

VOID HashGlob(SHA256_CTX *ctx, CHAR16 *glob)
{
  FindFilesMatchingGlob(glob);

  if(files != NULL) {
    for(int j = 0; files[j] != NULL; j++) {
      HashFile(ctx, files[j]);
    }

    //TODO do we want to keep the list of actual files hashed?
    MyFreePool(files);
  }
}
VOID GenerateHash(LOADER_ENTRY *Entry)
{
  //TODO FIXME
  //if(HashFilesCount != 0) {
  SHA256_CTX ctx;
  Sha256Init(&ctx);

  HashCHAR16NTA(&ctx,Entry->LoaderPath);
  HashCHAR16NTA(&ctx,Entry->LoadOptions);
  HashCHAR16NTA(&ctx,Entry->InitrdPath);

  if(Entry->HashGlobs != NULL) {
    for(int i = 0; i < HashGlobsCount, i++)
      HashGlob(&ctx, Entry->HashGlobs[i]);
  }

  MyFreePool(Entry->Hash);
  Entry->Hash = AllocateZeroPool(32 * sizeof(unsigned char));
  if(Entry->Hash == NULL) return;
  Sha256Final(&ctx, Entry->Hash);
}
