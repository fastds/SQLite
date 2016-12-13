/*
** 2010 February 1
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This header file defines the interface to the write-ahead logging 
** system. Refer to the comments below and the header comment attached to 
** the implementation of each function in log.c for further details.
该头文件定义了预写日志系统的接口。以下注释包含与每个log.c中的功能关联的注释
*/

#ifndef _WAL_H_
#define _WAL_H_

#include "sqliteInt.h"

/* Additional values that can be added to the sync_flags argument of
** sqlite3WalFrames():
*/
#define WAL_SYNC_TRANSACTIONS  0x20   /* Sync at the end of each transaction */
#define SQLITE_SYNC_MASK       0x13   /* Mask off the SQLITE_SYNC_* values */

#ifdef SQLITE_OMIT_WAL
# define sqlite3WalOpen(x,y,z)                   0
# define sqlite3WalLimit(x,y)
# define sqlite3WalClose(w,x,y,z)                0
# define sqlite3WalBeginReadTransaction(y,z)     0
# define sqlite3WalEndReadTransaction(z)
# define sqlite3WalRead(v,w,x,y,z)               0
# define sqlite3WalDbsize(y)                     0
# define sqlite3WalBeginWriteTransaction(y)      0
# define sqlite3WalEndWriteTransaction(x)        0
# define sqlite3WalUndo(x,y,z)                   0
# define sqlite3WalSavepoint(y,z)
# define sqlite3WalSavepointUndo(y,z)            0
# define sqlite3WalFrames(u,v,w,x,y,z)           0
# define sqlite3WalCheckpoint(r,s,t,u,v,w,x,y,z) 0
# define sqlite3WalCallback(z)                   0
# define sqlite3WalExclusiveMode(y,z)            0
# define sqlite3WalHeapMemory(z)                 0
# define sqlite3WalFramesize(z)                  0
#else

#define WAL_SAVEPOINT_NDATA 4

/* Connection to a write-ahead log (WAL) file. 
** There is one object of this type for each pager. 
*/
预写日志连接对象。每一个pager有一个该对象
typedef struct Wal Wal;

/* Open and close a connection to a write-ahead log. */
int sqlite3WalOpen(sqlite3_vfs*, sqlite3_file*, const char *, int, i64, Wal**);
int sqlite3WalClose(Wal *pWal, int sync_flags, int, u8 *);

/* Set the limiting size of a WAL file. */
设置对与写日志文件的大小限制
void sqlite3WalLimit(Wal*, i64);

/* Used by readers to open (lock) and close (unlock) a snapshot.  A 
** snapshot is like a read-transaction.  It is the state of the database
** at an instant in time.  sqlite3WalOpenSnapshot gets a read lock and
** preserves the current state even if the other threads or processes
** write to or checkpoint the WAL.  sqlite3WalCloseSnapshot() closes the
** transaction and releases the lock.
*/
被reader用来open (lock)和close (unlock)一个快照.一个快照就像一个读事务。是数据库某一时刻的状态。sqlite3WalOpenSnapshot获取一个读锁并维护当前
状态及时其他线程或者进程对wal进行写入和检查点操作。sqlite3WalCloseSnapshot关闭事务并释放锁
 
int sqlite3WalBeginReadTransaction(Wal *pWal, int *);
void sqlite3WalEndReadTransaction(Wal *pWal);

/* Read a page from the write-ahead log, if it is present. */
int sqlite3WalRead(Wal *pWal, Pgno pgno, int *pInWal, int nOut, u8 *pOut);

/* If the WAL is not empty, return the size of the database. */
如果wal非空，返回数据库的大小
Pgno sqlite3WalDbsize(Wal *pWal);

/* Obtain or release the WRITER lock. */		获取或释放writer锁
int sqlite3WalBeginWriteTransaction(Wal *pWal);
int sqlite3WalEndWriteTransaction(Wal *pWal);

/* Undo any frames written (but not committed) to the log */		撤销任何被写入日志的frames（不提交）
int sqlite3WalUndo(Wal *pWal, int (*xUndo)(void *, Pgno), void *pUndoCtx);

/* Return an integer that records the current (uncommitted) write	返回一个整数，该整数记录了当前（未提交）wal写入的位置
** position in the WAL */
void sqlite3WalSavepoint(Wal *pWal, u32 *aWalData);

/* Move the write position of the WAL back to iFrame.  Called in	将wal写入位置移动到iFrame。作为对一个ROLLBACK TO命令的响应
** response to a ROLLBACK TO command. */
int sqlite3WalSavepointUndo(Wal *pWal, u32 *aWalData);

/* Write a frame or frames to the log. */							如果一个或多个Frames记录日志
int sqlite3WalFrames(Wal *pWal, int, PgHdr *, Pgno, int, int);

/* Copy pages from the log to the database file */ 					从日志复制页面到数据库文件
int sqlite3WalCheckpoint(
  Wal *pWal,                      /* Write-ahead log connection */
  int eMode,                      /* One of PASSIVE, FULL and RESTART */
  int (*xBusy)(void*),            /* Function to call when busy */
  void *pBusyArg,                 /* Context argument for xBusyHandler */
  int sync_flags,                 /* Flags to sync db file with (or 0) */
  int nBuf,                       /* Size of buffer nBuf */
  u8 *zBuf,                       /* Temporary buffer to use */
  int *pnLog,                     /* OUT: Number of frames in WAL */
  int *pnCkpt                     /* OUT: Number of backfilled frames in WAL */
);

/* Return the value to pass to a sqlite3_wal_hook callback, the
** number of frames in the WAL at the point of the last commit since
** sqlite3WalCallback() was called.  If no commits have occurred since
** the last call, then return 0.
*/
返回传递给sqlite3_wal_hook回调函数的值，该值表示上一次调用sqlite3WalCallback()提交的wal中frame的数量。如果上一次调用没有发生提交，返回0
int sqlite3WalCallback(Wal *pWal);

/* Tell the wal layer that an EXCLUSIVE lock has been obtained (or released)
** by the pager layer on the database file.
告知wal层，pager层得到/释放数据库文件上的一个EXCLUSIVE锁
*/
int sqlite3WalExclusiveMode(Wal *pWal, int op);

/* Return true if the argument is non-NULL and the WAL module is using
** heap-memory for the wal-index. Otherwise, if the argument is NULL or the
** WAL module is using shared-memory, return false. 
*/
如果参数非空，并且wal模块使用 heap-memory来存储wal-index，返回真。否则，参数为NULL或者wal模块使用shared-memory模式，返回false
int sqlite3WalHeapMemory(Wal *pWal);

#ifdef SQLITE_ENABLE_ZIPVFS
/* If the WAL file is not empty, return the number of bytes of content
** stored in each frame (i.e. the db page-size when the WAL was created).
*/
如果wal文件非空，返回存储在每一个frame中的内容的字节数（如：当wal被创建时，为数据库的page-size）
int sqlite3WalFramesize(Wal *pWal);
#endif

#endif /* ifndef SQLITE_OMIT_WAL */
#endif /* _WAL_H_ */
