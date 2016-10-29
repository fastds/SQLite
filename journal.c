/*
** 2007 August 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements a special kind of sqlite3_file object used			该文件实现了一个特殊的sqlite3_file 对象类型，它被SQLite用来创建日志文件，如果atomic-write优化被开启。
** by SQLite to create journal files if the atomic-write optimization
** is enabled.
**
** The distinctive characteristic of this sqlite3_file is that the			这个sqlite3_file的独特的特点是，磁盘上的实际的文件以“懒惰”方式创建。当文件被创建，调用者为内存缓冲区指定一个大小，
** actual on disk file is created lazily. When the file is created,			 用来提供read()、write()请求服务。 实际的磁盘上的文件在满足以下其中一个条件时候才被创建或填入值：
** the caller specifies a buffer size for an in-memory buffer to
** be used to service read() and write() requests. The actual file			1、被分配的缓冲区内存表述过大
** on disk is not created or populated until either:						2、sqlite3JournalCreate()被调用
**
**   1) The in-memory representation grows too large for the allocated 
**      buffer, or
**   2) The sqlite3JournalCreate() function is called.
*/
#ifdef SQLITE_ENABLE_ATOMIC_WRITE
#include "sqliteInt.h"


/*
** A JournalFile object is a subclass of sqlite3_file used by					JournalFile对象是sqlite3_file的子类，它被用于表示一个打开的日志文件句柄
** as an open file handle for journal files.
*/
struct JournalFile {
  sqlite3_io_methods *pMethod;    /* I/O methods on journal files */			
  int nBuf;                       /* Size of zBuf[] in bytes */					zBuf大小
  char *zBuf;                     /* Space to buffer journal writes */			日志被写入的缓冲区
  int iSize;                      /* Amount of zBuf[] currently used */			zBuf当前已被使用的大小
  int flags;                      /* xOpen flags */								VFS的xOpen标记
  sqlite3_vfs *pVfs;              /* The "real" underlying VFS */				真实的下层的VFS
  sqlite3_file *pReal;            /* The "real" underlying file descriptor */	真实的下层的日志文件描述符
  const char *zJournal;           /* Name of the journal file */				日志文件名
};
typedef struct JournalFile JournalFile;

/*
** If it does not already exists, create and populate the on-disk file 			如果不存在，为JournalFile p创建、填写磁盘文件
** for JournalFile p.
*/
static int createFile(JournalFile *p){
  int rc = SQLITE_OK;
  if( !p->pReal ){
    sqlite3_file *pReal = (sqlite3_file *)&p[1];
    rc = sqlite3OsOpen(p->pVfs, p->zJournal, pReal, p->flags, 0);
    if( rc==SQLITE_OK ){
      p->pReal = pReal;
      if( p->iSize>0 ){						缓冲区有内容
        assert(p->iSize<=p->nBuf);			
        rc = sqlite3OsWrite(p->pReal, p->zBuf, p->iSize, 0);
      }
    }
  }
  return rc;
}

/*
** Close the file.
*/
static int jrnlClose(sqlite3_file *pJfd){
  JournalFile *p = (JournalFile *)pJfd;
  if( p->pReal ){
    sqlite3OsClose(p->pReal);
  }
  sqlite3_free(p->zBuf);
  return SQLITE_OK;
}

/*
** Read data from the file.
*/
static int jrnlRead(
  sqlite3_file *pJfd,    /* The journal file from which to read */
  void *zBuf,            /* Put the results here */
  int iAmt,              /* Number of bytes to read */				要读的字节数
  sqlite_int64 iOfst     /* Begin reading at this offset */			要读取的起始位置的偏移量
){
  int rc = SQLITE_OK;
  JournalFile *p = (JournalFile *)pJfd;
  if( p->pReal ){
    rc = sqlite3OsRead(p->pReal, zBuf, iAmt, iOfst);
  }else if( (iAmt+iOfst)>p->iSize ){
    rc = SQLITE_IOERR_SHORT_READ;
  }else{
    memcpy(zBuf, &p->zBuf[iOfst], iAmt);
  }
  return rc;
}

/*
** Write data to the file.				将日志数据写到文件
*/
static int jrnlWrite(
  sqlite3_file *pJfd,    /* The journal file into which to write */
  const void *zBuf,      /* Take data to be written from here */			要写的数据的暂存的空间
  int iAmt,              /* Number of bytes to write */						要写的数据的字节数
  sqlite_int64 iOfst     /* Begin writing at this offset into the file */	写的起始位置的偏移量
){
  int rc = SQLITE_OK;
  JournalFile *p = (JournalFile *)pJfd;
  if( !p->pReal && (iOfst+iAmt)>p->nBuf ){
    rc = createFile(p);
  }
  if( rc==SQLITE_OK ){
    if( p->pReal ){
      rc = sqlite3OsWrite(p->pReal, zBuf, iAmt, iOfst);
    }else{
      memcpy(&p->zBuf[iOfst], zBuf, iAmt);
      if( p->iSize<(iOfst+iAmt) ){
        p->iSize = (iOfst+iAmt);
      }
    }
  }
  return rc;
}

/*
** Truncate the file.			截断文件
*/
static int jrnlTruncate(sqlite3_file *pJfd, sqlite_int64 size){
  int rc = SQLITE_OK;
  JournalFile *p = (JournalFile *)pJfd;
  if( p->pReal ){
    rc = sqlite3OsTruncate(p->pReal, size);
  }else if( size<p->iSize ){
    p->iSize = size;
  }
  return rc;
}

/*
** Sync the file.			同步文件
*/
static int jrnlSync(sqlite3_file *pJfd, int flags){
  int rc;
  JournalFile *p = (JournalFile *)pJfd;
  if( p->pReal ){
    rc = sqlite3OsSync(p->pReal, flags);
  }else{
    rc = SQLITE_OK;
  }
  return rc;
}

/*
** Query the size of the file in bytes.			查询文件字节大小
*/
static int jrnlFileSize(sqlite3_file *pJfd, sqlite_int64 *pSize){
  int rc = SQLITE_OK;
  JournalFile *p = (JournalFile *)pJfd;
  if( p->pReal ){
    rc = sqlite3OsFileSize(p->pReal, pSize);
  }else{
    *pSize = (sqlite_int64) p->iSize;		//文件不存在（可能是第一次创建，还没写到磁盘，将内存中日志文件已使用空间作为大小赋值返回）
  }
  return rc;
}

/*
** Table of methods for JournalFile sqlite3_file object.		
*/
static struct sqlite3_io_methods JournalFileMethods = {
  1,             /* iVersion */
  jrnlClose,     /* xClose */
  jrnlRead,      /* xRead */
  jrnlWrite,     /* xWrite */
  jrnlTruncate,  /* xTruncate */
  jrnlSync,      /* xSync */
  jrnlFileSize,  /* xFileSize */
  0,             /* xLock */
  0,             /* xUnlock */
  0,             /* xCheckReservedLock */
  0,             /* xFileControl */
  0,             /* xSectorSize */
  0,             /* xDeviceCharacteristics */
  0,             /* xShmMap */
  0,             /* xShmLock */
  0,             /* xShmBarrier */
  0              /* xShmUnmap */
};

/* 
** Open a journal file.
*/
int sqlite3JournalOpen(
  sqlite3_vfs *pVfs,         /* The VFS to use for actual file I/O */
  const char *zName,         /* Name of the journal file */
  sqlite3_file *pJfd,        /* Preallocated, blank file handle */ 			预分配的、空的文件句柄
  int flags,                 /* Opening flags */
  int nBuf                   /* Bytes buffered before opening the file */
){
  JournalFile *p = (JournalFile *)pJfd;
  memset(p, 0, sqlite3JournalSize(pVfs));
  if( nBuf>0 ){
    p->zBuf = sqlite3MallocZero(nBuf);
    if( !p->zBuf ){
      return SQLITE_NOMEM;
    }
  }else{
    return sqlite3OsOpen(pVfs, zName, pJfd, flags, 0);
  }
  p->pMethod = &JournalFileMethods;
  p->nBuf = nBuf;
  p->flags = flags;
  p->zJournal = zName;
  p->pVfs = pVfs;
  return SQLITE_OK;
}

/*
** If the argument p points to a JournalFile structure, and the underlying  如果p只想JournalFile结构体，并且底层的磁盘文件还未创建，则现在创建
** file has not yet been created, create it now.
*/
int sqlite3JournalCreate(sqlite3_file *p){
  if( p->pMethods!=&JournalFileMethods ){
    return SQLITE_OK;
  }
  return createFile((JournalFile *)p);
}

/* 
** Return the number of bytes required to store a JournalFile that uses vfs
** pVfs to create the underlying on-disk files.			存储一个JournalFile在底层磁盘文件需要的字节数
*/
int sqlite3JournalSize(sqlite3_vfs *pVfs){
  return (pVfs->szOsFile+sizeof(JournalFile));
}
#endif
