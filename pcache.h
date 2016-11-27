/*
** 2008 August 05
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This header file defines the interface that the sqlite page cache  该头文件定义了sqlite 页缓存子系统的借口
** subsystem. 
*/

#ifndef _PCACHE_H_

typedef struct PgHdr PgHdr;
typedef struct PCache PCache;

/*
** Every page in the cache is controlled by an instance of the following		//PgHdr结构体：控制缓存中的每一个页面
** structure.
*/
struct PgHdr {
  sqlite3_pcache_page *pPage;    /* Pcache object page handle */				//Pcache对象 页面句柄（PgHdr1结构体）
  void *pData;                   /* Page data */								//页面数据
  void *pExtra;                  /* Extra content */							//额外内容
  PgHdr *pDirty;                 /* Transient list of dirty pages */			//脏页面的内存维护的列表
  Pager *pPager;                 /* The pager this page is part of */			//本结构体是Pager的一部分
  Pgno pgno;                     /* Page number for this page */				//本页面的页面号
#ifdef SQLITE_CHECK_PAGES
  u32 pageHash;                  /* Hash of page content */						//页面内容的哈希值
#endif
  u16 flags;                     /* PGHDR flags defined below */				//在下面定义的PGHDR标记

  /**********************************************************************
  ** Elements above are public.  All that follows is private to pcache.c		//上述内容是私有的，后面的内容对于pcache.c
  ** and should not be accessed by other modules.								//是私有的，并且不能被其他module访问
  */
  i16 nRef;                      /* Number of users of this page */				//当前页面的引用数
  PCache *pCache;                /* Cache that owns this page */				//拥有该页面的缓存

  PgHdr *pDirtyNext;             /* Next element in list of dirty pages */		//脏页面列表中的下一个元素（脏页面列表是一个双向列表，第一个元素和最后一个元素的引用在PCache中）
  PgHdr *pDirtyPrev;             /* Previous element in list of dirty pages */	//脏页面列表中的上一个元素
};

/* Bit values for PgHdr.flags */												//PgHdr.flags的可用的位取值
#define PGHDR_DIRTY             0x002  /* Page has changed */					//页面已修改
#define PGHDR_NEED_SYNC         0x004  /* Fsync the rollback journal before		//在将页面写到磁盘时，同步回滚日志
                                       ** writing this page to the database */	
#define PGHDR_NEED_READ         0x008  /* Content is unread */					//内容还未被读取
#define PGHDR_REUSE_UNLIKELY    0x010  /* A hint that reuse is unlikely */		//指示页面无法被重用
#define PGHDR_DONT_WRITE        0x020  /* Do not write content to disk */		//内容不写到磁盘？？

/* Initialize and shutdown the page cache subsystem */							//初始化/关闭页面缓存子系统
int sqlite3PcacheInitialize(void);
void sqlite3PcacheShutdown(void);

/* Page cache buffer management:												//页缓存缓冲区管理
** These routines implement SQLITE_CONFIG_PAGECACHE.							//该函数实现SQLITE_CONFIG_PAGECACHE
*/
void sqlite3PCacheBufferSetup(void *, int sz, int n);

/* Create a new pager cache.													//创建一个新的pager缓存
** Under memory stress, invoke xStress to try to make pages clean.				//在内存压力下，调用xStress是试图释放部分页面
** Only clean and unpinned pages can be reclaimed.								//只有clean的和未被钉住的页面能够被回收
*/
void sqlite3PcacheOpen(
  int szPage,                    /* Size of every page */						//页面大小
  int szExtra,                   /* Extra space associated with each page */	//每个页面的额外空间
  int bPurgeable,                /* True if pages are on backing store */		//
  int (*xStress)(void*, PgHdr*), /* Call to try to make pages clean */			//
  void *pStress,                 /* Argument to xStress */
  PCache *pToInit                /* Preallocated space for the PCache */		//PCache的预分配空间
);

/* Modify the page-size after the cache has been created. */					//在缓存被创建之后修改页面大小
void sqlite3PcacheSetPageSize(PCache *, int);

/* Return the size in bytes of a PCache object.  Used to preallocate			//被用于存储空间的预分配
** storage space.
*/
int sqlite3PcacheSize(void);

/* One release per successful fetch.  Page is pinned until released.
** Reference counted. 
*/
int sqlite3PcacheFetch(PCache*, Pgno, int createFlag, PgHdr**);
void sqlite3PcacheRelease(PgHdr*);

void sqlite3PcacheDrop(PgHdr*);         /* Remove page from cache */
void sqlite3PcacheMakeDirty(PgHdr*);    /* Make sure page is marked dirty */
void sqlite3PcacheMakeClean(PgHdr*);    /* Mark a single page as clean */
void sqlite3PcacheCleanAll(PCache*);    /* Mark all dirty list pages as clean */

/* Change a page number.  Used by incr-vacuum. */
void sqlite3PcacheMove(PgHdr*, Pgno);									//改变页面号

/* Remove all pages with pgno>x.  Reset the cache if x==0 */
void sqlite3PcacheTruncate(PCache*, Pgno x);

/* Get a list of all dirty pages in the cache, sorted by page number */
PgHdr *sqlite3PcacheDirtyList(PCache*);

/* Reset and close the cache object */
void sqlite3PcacheClose(PCache*);

/* Clear flags from pages of the page cache */
void sqlite3PcacheClearSyncFlags(PCache *);

/* Discard the contents of the cache */
void sqlite3PcacheClear(PCache*);

/* Return the total number of outstanding page references */
int sqlite3PcacheRefCount(PCache*);

/* Increment the reference count of an existing page */
void sqlite3PcacheRef(PgHdr*);

int sqlite3PcachePageRefcount(PgHdr*);

/* Return the total number of pages stored in the cache */
int sqlite3PcachePagecount(PCache*);

#if defined(SQLITE_CHECK_PAGES) || defined(SQLITE_DEBUG)
/* Iterate through all dirty pages currently stored in the cache. This
** interface is only available if SQLITE_CHECK_PAGES is defined when the 
** library is built.
*/
void sqlite3PcacheIterateDirty(PCache *pCache, void (*xIter)(PgHdr *));
#endif

/* Set and get the suggested cache-size for the specified pager-cache.
**
** If no global maximum is configured, then the system attempts to limit
** the total number of pages cached by purgeable pager-caches to the sum
** of the suggested cache-sizes.
*/
void sqlite3PcacheSetCachesize(PCache *, int);
#ifdef SQLITE_TEST
int sqlite3PcacheGetCachesize(PCache *);
#endif

/* Free up as much memory as possible from the page cache */
void sqlite3PcacheShrink(PCache*);

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/* Try to return memory used by the pcache module to the main memory heap */
int sqlite3PcacheReleaseMemory(int);
#endif

#ifdef SQLITE_TEST
void sqlite3PcacheStats(int*,int*,int*,int*);
#endif

void sqlite3PCacheSetDefault(void);

#endif /* _PCACHE_H_ */
