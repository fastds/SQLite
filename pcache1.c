/*
** 2008 November 05
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
** This file implements the default page cache implementation (the					
** sqlite3_pcache interface). It also contains part of the implementation			
** of the SQLITE_CONFIG_PAGECACHE and sqlite3_release_memory() features.
** If the default page cache implementation is overriden, then neither of
** these two features are available.

*/

#include "sqliteInt.h"

typedef struct PCache1 PCache1;
typedef struct PgHdr1 PgHdr1;
typedef struct PgFreeslot PgFreeslot;
typedef struct PGroup PGroup;

/* Each page cache (or PCache) belongs to a PGroup.  A PGroup is a set 				每一个页缓存（PCache）数据一个PGroup。一个PGroup是一个或多个PCache的集合，使得它可在内存压力下能够重新利用每一个其他未被钉住的页面
** of one or more PCaches that are able to recycle each others unpinned				一个PGroup是一个下列对象的实例
** pages when they are under memory pressure.  A PGroup is an instance of
** the following object.
**
** This page cache implementation works in one of two modes:						页面缓存机制以以下两种模式之一的实现
**
**   (1)  Every PCache is the sole member of its own PGroup.  There is				（1） 每一个PCac和是它自己的PGrouop的一个成员。每一个PCahce由一个PGroup
**        one PGroup per PCache.
**
**   (2)  There is a single global PGroup that all PCaches are a member				（2） 有一个单独的全局的PGroup，所有的PCache都是它的成员
**        of.
**
** Mode 1 uses more memory (since PCache instances are not able to rob				模式1使用更多内存（因为PCache实例不能够抢占其他PCache未使用的页面），但是他能够在没有一个mutex的情况下运行，因此通常很高效
** unused pages from other PCaches) but it also operates without a mutex,			模式2需要一个mutex，这是为了前程安全，但是循环利用pages更高效
** and is therefore often faster.  Mode 2 requires a mutex in order to be
** threadsafe, but recycles pages more efficiently.
**
** For mode (1), PGroup.mutex is NULL.  For mode (2) there is only a single			对于模式一，PGroup.mutex为NULL。 对于模式二，仅仅只有一个单独的PGroup，即pcache1.grp全局变量和它的mutex是SQLITE_MUTEX_STATIC_LRU.
** PGroup which is the pcache1.grp global variable and its mutex is
** SQLITE_MUTEX_STATIC_LRU.
*/
struct PGroup {
  sqlite3_mutex *mutex;          /* MUTEX_STATIC_LRU or NULL */								取值：MUTEX_STATIC_LRU 或 NULL
  unsigned int nMaxPage;         /* Sum of nMax for MUTEX_STATIC_LRU or NULL caches */		
  unsigned int nMinPage;         /* Sum of nMin for purgeable caches */						可清除的caches的所有nMin之和
  unsigned int mxPinned;         /* nMaxpage + 10 - nMinPage */								
  unsigned int nCurrentPage;     /* Number of purgeable pages allocated */					可清除的已分配的页面数
  PgHdr1 *pLruHead, *pLruTail;   /* LRU list of unpinned pages */							未钉住页面的LRU列表
};

/* Each page cache is an instance of the following object.  Every					页面缓存对象。每个开开的数据库文件（包括每一个内存数据库和每一个临时数据库）有一个单独的页面缓存。
** open database file (including each in-memory database and each					指向该类型结构体的指针被转换和返回为一个不透明的sqlite3——pcache*句柄
** temporary or transient database) has a single page cache which
** is an instance of this object.
**
** Pointers to structures of this type are cast and returned as 			
** opaque sqlite3_pcache* handles.
*/
struct PCache1 {
  /* Cache configuration parameters. Page size (szPage) and the purgeable			缓存配置参数。缓存被创建时，面大小（szPage）和可清理标记（bPurgeable）被设置。nMax可能在任何时候被修改，它的修改是通过调用pcahce1Cachesize()
  ** flag (bPurgeable) are set when the cache is created. nMax may be 				方法。当访问nMax时PGroup的mutex必须被保存
  ** modified at any time by a call to the pcache1Cachesize() method.
  ** The PGroup mutex must be held when accessing nMax.
  */
  PGroup *pGroup;                     /* PGroup this cache belongs to */			当前缓存所属的PGroup
  int szPage;                         /* Size of allocated pages in bytes */		已分配页面的字节大小
  int szExtra;                        /* Size of extra space in bytes */			额外空间的字节大小
  int bPurgeable;                     /* True if cache is purgeable */				清除标记
  unsigned int nMin;                  /* Minimum number of pages reserved */		保留页面的最小数
  unsigned int nMax;                  /* Configured "cache_size" value */			配置的 cache_size大小
  unsigned int n90pct;                /* nMax*9/10 */								nMax的百分之九十
  unsigned int iMaxKey;               /* Largest key seen since xTruncate() */		xTruncate()以来的最大key

  /* Hash table of all pages. The following variables may only be accessed			所有页面的哈希表。以下变量可能仅仅在访问者持有PGroup mutex的时候被访问
  ** when the accessor is holding the PGroup mutex.
  */		
  unsigned int nRecyclable;           /* Number of pages in the LRU list */			LRU双向链表中的页面数(可回收利用的)
  unsigned int nPage;                 /* Total number of pages in apHash */			apHash中页面的总数
  unsigned int nHash;                 /* Number of slots in apHash[] */				apHash[]的槽的数量
  PgHdr1 **apHash;                    /* Hash table for fast lookup by key */		用于快速查找key的哈希表（二维数组）存储着多个LRU双向链表
};

/*
** Each cache entry is represented by an instance of the following 					以下对象，表示每一个cache entry。除非SQLITE_PCAHE_SEPARATE_HEADER被定义，在这个结构体在内存之前。
** structure. Unless SQLITE_PCACHE_SEPARATE_HEADER is defined, a buffer of			一个 PgHdr1.pCache->szPage字节大小的缓冲区被直接分配
** PgHdr1.pCache->szPage bytes is allocated directly before this structure 
** in memory.
*/
struct PgHdr1 {
  sqlite3_pcache_page page;
  unsigned int iKey;             /* Key value (page number) */						键的值（页号）；桶号 = 键的值（页号）% nHash(桶数)
  PgHdr1 *pNext;                 /* Next in hash table chain */						哈希表中链表中的下一个
  PCache1 *pCache;               /* Cache that currently owns this page */			拥有当前页面的页缓存
  PgHdr1 *pLruNext;              /* Next in LRU list of unpinned pages */			LRU列表中未钉住页面（内存压力大时可以用来替换）的下一个
  PgHdr1 *pLruPrev;              /* Previous in LRU list of unpinned pages */		.....................上一个
};

/*
** Free slots in the allocator used to divide up the buffer provided using			
** the SQLITE_CONFIG_PAGECACHE mechanism.
分配器中的空闲槽用来划分用SQLITE_CONFIG_PAGECACHE机制所提供的缓冲区
*/
struct PgFreeslot {
  PgFreeslot *pNext;  /* Next free slot */
};

/*
** Global data used by this cache.													被用于这个cache的全局数据
*/
static SQLITE_WSD struct PCacheGlobal {
  PGroup grp;                    /* The global PGroup for mode (2) */				模式2中的PGroup

  /* Variables related to SQLITE_CONFIG_PAGECACHE settings.  The					与SQLITE_CONFIT_PAGECACHE设置相关的变量。
  ** szSlot, nSlot, pStart, pEnd, nReserve, and isInit values are all
  ** fixed at sqlite3_initialize() time and do not require mutex protection.
  ** The nFreeSlot and pFree values do require mutex protection.
  */
  int isInit;                    /* True if initialized */							如果被初始化了，为true
  int szSlot;                    /* Size of each free slot */						每一个空闲槽（缓冲区）的大小
  int nSlot;                     /* The number of pcache slots */					pcache空闲槽（缓冲区）的数量
  int nReserve;                  /* Try to keep nFreeSlot above this */				需要保留的最小空闲槽数量
  void *pStart, *pEnd;           /* Bounds of pagecache malloc range */				页缓存原始分配的第一个槽，最后一个后面的槽（无效地址）
  
  /* Above requires no mutex.  Use mutex below for variable that follow. */			以下变量用于mutex情况
  sqlite3_mutex *mutex;          /* Mutex for accessing the following: */			访问以下内容的mutex
  PgFreeslot *pFree;             /* Free page blocks */								空闲的页面块（指向链表最后一个有效缓冲区地址）
  int nFreeSlot;                 /* Number of unused pcache slots */				未使用的pcache槽，设置后指向最后一个
  /* The following value requires a mutex to change.  We skip the mutex on			以下的值需要一个mutex去改变，我们跳过了在reading上的mutex，因为(1)大多数平台自动读取32-bit整数（2）即使一个错误的值被读取，
  ** reading because (1) most platforms read a 32-bit integer atomically and		也不会造成多大的伤害，因为这仅仅是一个优化项
  ** (2) even if an incorrect value is read, no great harm is done since this
  ** is really just an optimization. */
  int bUnderPressure;            /* True if low on PAGECACHE memory */				如果PAGECACHE内存处于较低水平
} pcache1_g;

/*
** All code in this file should access the global structure above via the
** alias "pcache1". This ensures that the WSD emulation is used when
** compiling for systems that do not support real WSD.
	该文件的所有代码应该访问上面的全局结构体（通过pcache1）。这保证当编译的目标系统不支持真正的WSD时，保证了WSD竞争被使用
*/
#define pcache1 (GLOBAL(struct PCacheGlobal, pcache1_g))				pcache1：表示PCacheGlobal对象

/*
** Macros to enter and leave the PCache LRU mutex.
*/
#define pcache1EnterMutex(X) sqlite3_mutex_enter((X)->mutex)
#define pcache1LeaveMutex(X) sqlite3_mutex_leave((X)->mutex)

/******************************************************************************/
/******** Page Allocation/SQLITE_CONFIG_PCACHE Related Functions **************/

/*
** This function is called during initialization if a static buffer is 
** supplied to use for the page-cache by passing the SQLITE_CONFIG_PAGECACHE
** verb to sqlite3_config(). Parameter pBuf points to an allocation large
** enough to contain 'n' buffers of 'sz' bytes each.
**
** This routine is called from sqlite3_initialize() and so it is guaranteed
** to be serialized already.  There is no need for further mutexing.
	 对PCache的缓冲区（已经分配完成）进行设置（对全局变量PCacheGlobal），初始化
	 参数描述：
	 pBuf：指向一块大的缓冲区，大小为n*sz
	 sz：每一个小缓冲区的字节大小
	 n：小缓冲区的数量
*/
void sqlite3PCacheBufferSetup(void *pBuf, int sz, int n){
  if( pcache1.isInit ){				//PCacheGlobal对象
    PgFreeslot *p;
    sz = ROUNDDOWN8(sz);
    pcache1.szSlot = sz;			//槽（缓冲区）的大小
    pcache1.nSlot = pcache1.nFreeSlot = n;	//n个槽（缓冲区）
    pcache1.nReserve = n>90 ? 10 : (n/10 + 1);
    pcache1.pStart = pBuf;		pStart指向第一个空闲缓冲区地址
    pcache1.pFree = 0;
    pcache1.bUnderPressure = 0;
	/**
	pcache1.pFree最终指向最后的指向的缓冲区（n--为1），每个缓冲区指向上一个获取地址。形成一个空闲槽的单向链表
	*/
    while( n-- ){
      p = (PgFreeslot*)pBuf;		
      p->pNext = pcache1.pFree;		
      pcache1.pFree = p;
      pBuf = (void*)&((char*)pBuf)[sz];		//指向下一个缓冲区
    }
    pcache1.pEnd = pBuf;	pEnd指向最后一个空闲缓冲区地址
  }
}

/*
** Malloc function used within this file to allocate space from the buffer
** configured using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no 
** such buffer exists or there is no space left in it, this function falls 
** back to sqlite3Malloc().
**
** Multiple threads can run this routine at the same time.  Global variables
** in pcache1 need to be protected via mutex.
** pcache空间分配，需要从pcache中的空闲槽单向链表中（有的话）取出。
如果空闲槽中已经没有可以分配的槽了，则调用sqliteMalloc分配空间
*/
static void *pcache1Alloc(int nByte){
  void *p = 0;
  assert( sqlite3_mutex_notheld(pcache1.grp.mutex) );
  sqlite3StatusSet(SQLITE_STATUS_PAGECACHE_SIZE, nByte);	//设置页面缓存大小的字节数
  if( nByte<=pcache1.szSlot ){					缓存大小小于等于槽大小
    sqlite3_mutex_enter(pcache1.mutex);
    p = (PgHdr1 *)pcache1.pFree;
    if( p ){
      pcache1.pFree = pcache1.pFree->pNext;
      pcache1.nFreeSlot--;
      pcache1.bUnderPressure = pcache1.nFreeSlot<pcache1.nReserve;		空闲槽<需要保留的数量，设置处于压力状态
      assert( pcache1.nFreeSlot>=0 );									
      sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, 1);				设置pcache使用状态
    }
    sqlite3_mutex_leave(pcache1.mutex);
  }
  if( p==0 ){		SQLITE_CONFIG_PAGECACHE池中没有内存可用。重新用sqlite3Malloc分配
    /* Memory is not available in the SQLITE_CONFIG_PAGECACHE pool.  Get
    ** it from sqlite3Malloc instead.
    */
    p = sqlite3Malloc(nByte);
#ifndef SQLITE_DISABLE_PAGECACHE_OVERFLOW_STATS
    if( p ){
      int sz = sqlite3MallocSize(p);
      sqlite3_mutex_enter(pcache1.mutex);
      sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, sz);			//添加溢出状态（缓存池中没有空闲可用的缓存）
      sqlite3_mutex_leave(pcache1.mutex);
    }
#endif
    sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
  }
  return p;			返回分配空间的首地址
}

/*
** Free an allocated buffer obtained from pcache1Alloc().
释放已经分配的缓冲区，通过将缓冲区重新添加到空闲槽单项链表尾部
*/
static int pcache1Free(void *p){
  int nFreed = 0;
  if( p==0 ) return 0;
  if( p>=pcache1.pStart && p<pcache1.pEnd ){
    PgFreeslot *pSlot;
    sqlite3_mutex_enter(pcache1.mutex);
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, -1);
    pSlot = (PgFreeslot*)p;
    pSlot->pNext = pcache1.pFree;
    pcache1.pFree = pSlot;
    pcache1.nFreeSlot++;
    pcache1.bUnderPressure = pcache1.nFreeSlot<pcache1.nReserve;
    assert( pcache1.nFreeSlot<=pcache1.nSlot );
    sqlite3_mutex_leave(pcache1.mutex);
  }else{		如果释放的地址范围不在原始范围缓存分配范围内，则是额外分配的，直接释放之前给它额外分配的空间
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    nFreed = sqlite3MallocSize(p);
#ifndef SQLITE_DISABLE_PAGECACHE_OVERFLOW_STATS
    sqlite3_mutex_enter(pcache1.mutex);
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, -nFreed);
    sqlite3_mutex_leave(pcache1.mutex);
#endif
    sqlite3_free(p);
  }
  return nFreed;
}

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
** Return the size of a pcache allocation
*/
static int pcache1MemSize(void *p){
  if( p>=pcache1.pStart && p<pcache1.pEnd ){
    return pcache1.szSlot;
  }else{
    int iSize;
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    iSize = sqlite3MallocSize(p);
    sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
    return iSize;
  }
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

/*
** Allocate a new page object initially associated with cache pCache.
为相关的pCache分配一个初始的页面对象
*/
static PgHdr1 *pcache1AllocPage(PCache1 *pCache){
  PgHdr1 *p = 0;
  void *pPg;

  /* The group mutex must be released before pcache1Alloc() is called. This
  ** is because it may call sqlite3_release_memory(), which assumes that 
  ** this mutex is not held. */
  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  pcache1LeaveMutex(pCache->pGroup);
#ifdef SQLITE_PCACHE_SEPARATE_HEADER  如果定义SQLITE_PCACHE_SEPARATE_HEADER（即页面数据需要的缓冲区与页面对象需要的缓冲区不分配在一个连续区域中，即分开分配；否则一次性分配一共所需的空间）
  pPg = pcache1Alloc(pCache->szPage);							分配页面对象需要的缓冲区
  p = sqlite3Malloc(sizeof(PgHdr1) + pCache->szExtra);			分配页面对象
  if( !pPg || !p ){
    pcache1Free(pPg);					
    sqlite3_free(p);
    pPg = 0;
  }
#else
  pPg = pcache1Alloc(sizeof(						将分配了空间的对象添加到自己的空闲槽当中
1) + pCache->szPage + pCache->szExtra);				 pCache->szExtra大小【是否：p->page.pBuf+p->page.pExtra需要的空间】
  p = (PgHdr1 *)&((u8 *)pPg)[pCache->szPage];		p指向额外分配的空间(extra)区域的首地址（所以额外空间是用来存pgHdr1的？）
#endif
  pcache1EnterMutex(pCache->pGroup);

  if( pPg ){
    p->page.pBuf = pPg;								将页面缓冲区指向刚刚分配的地址
    p->page.pExtra = &p[1];							额外份额配的空间指向自己的首地址
    if( pCache->bPurgeable ){						
      pCache->pGroup->nCurrentPage++;				缓存的页面数++
    }
    return p;										返回分配好的页面
  }
  return 0;
}

/*
** Free a page object allocated by pcache1AllocPage().
**
** The pointer is allowed to be NULL, which is prudent.  But it turns out
** that the current implementation happens to never call this routine
** with a NULL pointer, so we mark the NULL test with ALWAYS().
*/
static void pcache1FreePage(PgHdr1 *p){
  if( ALWAYS(p) ){
    PCache1 *pCache = p->pCache;
    assert( sqlite3_mutex_held(p->pCache->pGroup->mutex) );
    pcache1Free(p->page.pBuf);					放到空闲槽或释放空间
#ifdef SQLITE_PCACHE_SEPARATE_HEADER
    sqlite3_free(p);
#endif
    if( pCache->bPurgeable ){
      pCache->pGroup->nCurrentPage--;		可清除页面数量减少（空闲槽中的直接可以用，不用清除）
    }
  }
}

/*
** Malloc function used by SQLite to obtain space from the buffer configured
** using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no such buffer
** exists, this function falls back to sqlite3Malloc().
*/
void *sqlite3PageMalloc(int sz){
  return pcache1Alloc(sz);			分配空间，放到空闲槽
}

/*
** Free an allocated buffer obtained from sqlite3PageMalloc().
*/
void sqlite3PageFree(void *p){		释放一个page，将该page放到空闲槽
  pcache1Free(p);
}


/*
** Return true if it desirable to avoid allocating a new page cache
** entry.
**
** If memory was allocated specifically to the page cache using
** SQLITE_CONFIG_PAGECACHE but that memory has all been used, then
** it is desirable to avoid allocating a new page cache entry because
** presumably SQLITE_CONFIG_PAGECACHE was suppose to be sufficient
** for all page cache needs and we should not need to spill the
** allocation onto the heap.
**
** Or, the heap is used for all page cache memory but the heap is
** under memory pressure, then again it is desirable to avoid
** allocating a new page cache entry in order to avoid stressing
** the heap even further.
*/
static int pcache1UnderMemoryPressure(PCache1 *pCache){
  if( pcache1.nSlot && (pCache->szPage+pCache->szExtra)<=pcache1.szSlot ){
    return pcache1.bUnderPressure;
  }else{
    return sqlite3HeapNearlyFull();
  }
}

/******************************************************************************/
/******** General Implementation Functions ************************************/

/*
** This function is used to resize the hash table used by the cache passed
** as the first argument.
**
** The PCache mutex must be held when this function is called.
*/
static int pcache1ResizeHash(PCache1 *p){
  PgHdr1 **apNew;
  unsigned int nNew;
  unsigned int i;

  assert( sqlite3_mutex_held(p->pGroup->mutex) );

  nNew = p->nHash*2;
  if( nNew<256 ){
    nNew = 256;
  }

  pcache1LeaveMutex(p->pGroup);
  if( p->nHash ){ sqlite3BeginBenignMalloc(); }
  apNew = (PgHdr1 **)sqlite3MallocZero(sizeof(PgHdr1 *)*nNew);
  if( p->nHash ){ sqlite3EndBenignMalloc(); }
  pcache1EnterMutex(p->pGroup);
  if( apNew ){
    for(i=0; i<p->nHash; i++){
      PgHdr1 *pPage;
      PgHdr1 *pNext = p->apHash[i];
      while( (pPage = pNext)!=0 ){
        unsigned int h = pPage->iKey % nNew;
        pNext = pPage->pNext;
        pPage->pNext = apNew[h];
        apNew[h] = pPage;
      }
    }
    sqlite3_free(p->apHash);
    p->apHash = apNew;
    p->nHash = nNew;
  }

  return (p->apHash ? SQLITE_OK : SQLITE_NOMEM);
}

/*
** This function is used internally to remove the page pPage from the 
** PGroup LRU list, if is part of it. If pPage is not part of the PGroup
** LRU list, then this function is a no-op.
**
** The PGroup mutex must be held when this function is called.
**
** If pPage is NULL then this routine is a no-op.
	将页面pPage钉住.即将该页面从LRU列表中移除（如果指定的页面在其中的话）
*/
static void pcache1PinPage(PgHdr1 *pPage){
  PCache1 *pCache;
  PGroup *pGroup;

  if( pPage==0 ) return;
  pCache = pPage->pCache;
  pGroup = pCache->pGroup;
  assert( sqlite3_mutex_held(pGroup->mutex) );
  if( pPage->pLruNext || pPage==pGroup->pLruTail ){
    if( pPage->pLruPrev ){
      pPage->pLruPrev->pLruNext = pPage->pLruNext;
    }
    if( pPage->pLruNext ){
      pPage->pLruNext->pLruPrev = pPage->pLruPrev;
    }
    if( pGroup->pLruHead==pPage ){
      pGroup->pLruHead = pPage->pLruNext;
    }
    if( pGroup->pLruTail==pPage ){
      pGroup->pLruTail = pPage->pLruPrev;
    }
    pPage->pLruNext = 0;
    pPage->pLruPrev = 0;
    pPage->pCache->nRecyclable--;			//可回收利用的页面--
  }
}


/*
** Remove the page supplied as an argument from the hash table 
** (PCache1.apHash structure) that it is currently stored in.
**
** The PGroup mutex must be held when this function is called.
从哈希表中移除pPage	：
找到pPage所在桶
找到桶后，查找链表，找到即可移除（改变指针）
*/
static void pcache1RemoveFromHash(PgHdr1 *pPage){
  unsigned int h;
  PCache1 *pCache = pPage->pCache;
  PgHdr1 **pp;

  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  h = pPage->iKey % pCache->nHash;
  for(pp=&pCache->apHash[h]; (*pp)!=pPage; pp=&(*pp)->pNext);
  *pp = (*pp)->pNext;

  pCache->nPage--;								pcache中的页面--
}

/*
** If there are currently more than nMaxPage pages allocated, try
** to recycle pages to reduce the number allocated to nMaxPage.

	如果现在有超过nMaxPage页被分配，试图循环利用页面来减少分配给nMaxPage的数
	操作过程：
	从pGroup的链表的pLruTail开始查找，将该双向链表中的page，将其从hash表中移除，从空空闲槽中移除
*/
static void pcache1EnforceMaxPage(PGroup *pGroup){
  assert( sqlite3_mutex_held(pGroup->mutex) );
  while( pGroup->nCurrentPage>pGroup->nMaxPage && pGroup->pLruTail ){
    PgHdr1 *p = pGroup->pLruTail;
    assert( p->pCache->pGroup==pGroup );
    pcache1PinPage(p);
    pcache1RemoveFromHash(p);
    pcache1FreePage(p);
  }
}

/*
** Discard all pages from cache pCache with a page number (key value) 
** greater than or equal to iLimit. Any pinned pages that meet this 
** criteria are unpinned before they are discarded.
**
** The PCache mutex must be held when this function is called.
	从缓存pCache中丢弃（从哈希表中移除）所有大于等于给定的页号的页面。这些页面在丢弃之前进行unpinned操作
	
*/
static void pcache1TruncateUnsafe(
  PCache1 *pCache,             /* The cache to truncate */
  unsigned int iLimit          /* Drop pages with this pgno or larger */
){
  TESTONLY( unsigned int nPage = 0; )  /* To assert pCache->nPage is correct */
  unsigned int h;
  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  for(h=0; h<pCache->nHash; h++){
    PgHdr1 **pp = &pCache->apHash[h]; 		遍历桶
    PgHdr1 *pPage;
    while( (pPage = *pp)!=0 ){
      if( pPage->iKey>=iLimit ){			满足页号大于等于iLimit的都丢弃
        pCache->nPage--;
        *pp = pPage->pNext;					
        pcache1PinPage(pPage);				钉住要丢弃的页面
        pcache1FreePage(pPage);				释放该页面
      }else{
        pp = &pPage->pNext;
        TESTONLY( nPage++; )
      }
    }
  }
  assert( pCache->nPage==nPage );
}

/******************************************************************************/
/******** sqlite3_pcache Methods ****************sqlite3_pcache方法列表******************************/

/*
** Implementation of the sqlite3_pcache.xInit method.		
sqlite3_pcache.xInit方法的实现。分配pcache1空间，互斥系统
*/
static int pcache1Init(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  assert( pcache1.isInit==0 );
  memset(&pcache1, 0, sizeof(pcache1));
  if( sqlite3GlobalConfig.bCoreMutex ){
    pcache1.grp.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_LRU);
    pcache1.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_PMEM);
  }
  pcache1.grp.mxPinned = 10;			最大钉住的页面
  pcache1.isInit = 1;					设置初始化标记
  return SQLITE_OK;
}

/*
** Implementation of the sqlite3_pcache.xShutdown method.		sqlite3_pcache.xShutdown方法的实现
** Note that the static mutex allocated in xInit does 
** not need to be freed.

*/
static void pcache1Shutdown(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  assert( pcache1.isInit!=0 );
  memset(&pcache1, 0, sizeof(pcache1));
}

/*
** Implementation of the sqlite3_pcache.xCreate method.			sqlite3_pcache.xCreate的实现
**
** Allocate a new cache.	用来分配新的pCache
*/
static sqlite3_pcache *pcache1Create(int szPage, int szExtra, int bPurgeable){		pCache->szPage, pCache->szExtra + sizeof(PgHdr), pCache->bPurgeable
  PCache1 *pCache;      /* The newly created page cache */		重新创建的页缓存
  PGroup *pGroup;       /* The group the new page cache will belong to */	新的页缓存所属的PGroup
  int sz;               /* Bytes of memory required to allocate the new cache */	分配给新缓存的内存字节数

  /*
  ** The seperateCache variable is true if each PCache has its own private
  ** PGroup.  In other words, separateCache is true for mode (1) where no
  ** mutexing is required.
  **
  **   *  Always use a unified cache (mode-2) if ENABLE_MEMORY_MANAGEMENT
  **
  **   *  Always use a unified cache in single-threaded applications
  **
  **   *  Otherwise (if multi-threaded and ENABLE_MEMORY_MANAGEMENT is off)
  **      use separate caches (mode-1)
	seperateCache：true：表示没有给PCache有自己独立私有的PGroup，此时不需要互斥
		*总是使用未定义的cache，如果ENABLE_MEMORY_MANAGEMENT
		*
  */
#if defined(SQLITE_ENABLE_MEMORY_MANAGEMENT) || SQLITE_THREADSAFE==0
  const int separateCache = 0;
#else
  int separateCache = sqlite3GlobalConfig.bCoreMutex>0;
#endif

  assert( (szPage & (szPage-1))==0 && szPage>=512 && szPage<=65536 );
  assert( szExtra < 300 );
  

  sz = sizeof(PCache1) + sizeof(PGroup)*separateCache;
  pCache = (PCache1 *)sqlite3MallocZero(sz);
  if( pCache ){
    if( separateCache ){
      pGroup = (PGroup*)&pCache[1];
      pGroup->mxPinned = 10;
    }else{
      pGroup = &pcache1.grp;
    }
    pCache->pGroup = pGroup;
    pCache->szPage = szPage;
    pCache->szExtra = szExtra;
    pCache->bPurgeable = (bPurgeable ? 1 : 0);
    if( bPurgeable ){
      pCache->nMin = 10;
      pcache1EnterMutex(pGroup);
      pGroup->nMinPage += pCache->nMin;
      pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
      pcache1LeaveMutex(pGroup);
    }
  }
  return (sqlite3_pcache *)pCache;
}

/*
** Implementation of the sqlite3_pcache.xCachesize method. 
**
** Configure the cache_size limit for a cache.
配置缓冲区的cache_size限制，该限制表示pcache中页面的最大数量
*/
static void pcache1Cachesize(sqlite3_pcache *p, int nMax){
  PCache1 *pCache = (PCache1 *)p;
  if( pCache->bPurgeable ){
    PGroup *pGroup = pCache->pGroup;
    pcache1EnterMutex(pGroup);
    pGroup->nMaxPage += (nMax - pCache->nMax);		//pCache中的nMax就是分配的缓冲区空间（以页为单位）的数量	
    pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
    pCache->nMax = nMax;
    pCache->n90pct = pCache->nMax*9/10;
    pcache1EnforceMaxPage(pGroup);
    pcache1LeaveMutex(pGroup);
  }
}

/*
** Implementation of the sqlite3_pcache.xShrink method. 
**
** Free up as much memory as possible.
*/
static void pcache1Shrink(sqlite3_pcache *p){
  PCache1 *pCache = (PCache1*)p;
  if( pCache->bPurgeable ){
    PGroup *pGroup = pCache->pGroup;
    int savedMaxPage;
    pcache1EnterMutex(pGroup);
    savedMaxPage = pGroup->nMaxPage;
    pGroup->nMaxPage = 0;
    pcache1EnforceMaxPage(pGroup);
	
    pGroup->nMaxPage = savedMaxPage;
    pcache1LeaveMutex(pGroup);
  }
}

/*
** Implementation of the sqlite3_pcache.xPagecount method. 
	计算缓存中页面的总数
*/
static int pcache1Pagecount(sqlite3_pcache *p){
  int n;
  PCache1 *pCache = (PCache1*)p;
  pcache1EnterMutex(pCache->pGroup);
  n = pCache->nPage;
  pcache1LeaveMutex(pCache->pGroup);
  return n;
}

/*
** Implementation of the sqlite3_pcache.xFetch method. 
**
** Fetch a page by key value.
**
** Whether or not a new page may be allocated by this function depends on
** the value of the createFlag argument.  0 means do not allocate a new
** page.  1 means allocate a new page if space is easily available.  2 
** means to try really hard to allocate a new page.
**
** For a non-purgeable cache (a cache used as the storage for an in-memory
** database) there is really no difference between createFlag 1 and 2.  So
** the calling function (pcache.c) will never have a createFlag of 1 on
** a non-purgeable cache.
**
** There are three different approaches to obtaining space for a page,
** depending on the value of parameter createFlag (which may be 0, 1 or 2).
**
**   1. Regardless of the value of createFlag, the cache is searched for a 
**      copy of the requested page. If one is found, it is returned.
**
**   2. If createFlag==0 and the page is not already in the cache, NULL is
**      returned.
**
**   3. If createFlag is 1, and the page is not already in the cache, then
**      return NULL (do not allocate a new page) if any of the following
**      conditions are true:
**
**       (a) the number of pages pinned by the cache is greater than
**           PCache1.nMax, or
**
**       (b) the number of pages pinned by the cache is greater than
**           the sum of nMax for all purgeable caches, less the sum of 
**           nMin for all other purgeable caches, or
**
**   4. If none of the first three conditions apply and the cache is marked
**      as purgeable, and if one of the following is true:
**
**       (a) The number of pages allocated for the cache is already 
**           PCache1.nMax, or
**
**       (b) The number of pages allocated for all purgeable caches is
**           already equal to or greater than the sum of nMax for all
**           purgeable caches,
**
**       (c) The system is under memory pressure and wants to avoid
**           unnecessary pages cache entry allocations
**
**      then attempt to recycle a page from the LRU list. If it is the right
**      size, return the recycled buffer. Otherwise, free the buffer and
**      proceed to step 5. 
**
**   5. Otherwise, allocate and return a new page buffer.
sqlite3_pcache.xFetch方法的实现：通过一个key来fetch一个page
是否可以通过此函数分配新页面取决于createFlag参数的值。 0：表示不分配新页面。 1：表示如果空间容易获得，则分配新页面。 2：意味着要努力分配一个新的页面。

对于 non-purgeable的缓存（用作内存数据库的缓存），createFlag 1和2没有区别.所以调用函数（pcache.c）永远不会有一个createFlag为1的non-purgeable的缓存。
有三种不同的方法来获取页面空间，取决于参数createFlag的值（其可以是0，1或2）。
1、不管createFlag的值如何，都会在Cache中搜索所请求页面的副本。 如果找到，则返回。
2、如果createFlag==0，并且页面已经不在缓存中，返回NULL
3、如果createFlag==1，并且page已经不在cache中，并且以下任一条件满足，返回NULL(不分配一个新页面)
（a）由cache钉住的页面数大于PCache1.nMax或者
（b）由cache钉住的页面数大于所有purgeable缓存的nMax的总和，小鱼所有其他purgeable缓存的nMin或者
4、如果以上三个条件没有一个满足，并且cache被标记为purgeable，并且如果以下条件之一为真：
（a）分配给cache的页面数为PCache1.nMax
（b）分配给所有purgeable缓存的页面数大于等于这些缓存的nMax之和
（c）系统处于主存压力之下，并且想要避免不必要的页面缓存entry分配

试图从LRU列表中循环利用一个页面。如果大小合适，返回可利用的缓冲区。否则，释放缓冲区并且进行步骤5
5、否则，分配并返回一个新页面缓存
*/
static sqlite3_pcache_page *pcache1Fetch(
  sqlite3_pcache *p, 
  unsigned int iKey, 
  int createFlag
){
  unsigned int nPinned;
  PCache1 *pCache = (PCache1 *)p;
  PGroup *pGroup;
  PgHdr1 *pPage = 0;

  assert( pCache->bPurgeable || createFlag!=1 );
  assert( pCache->bPurgeable || pCache->nMin==0 );
  assert( pCache->bPurgeable==0 || pCache->nMin==10 );
  assert( pCache->nMin==0 || pCache->bPurgeable );
  pcache1EnterMutex(pGroup = pCache->pGroup);

  /* Step 1: Search the hash table for an existing entry. */					第一步：检索哈希表中一个已存在的entry
  if( pCache->nHash>0 ){
    unsigned int h = iKey % pCache->nHash;
    for(pPage=pCache->apHash[h]; pPage&&pPage->iKey!=iKey; pPage=pPage->pNext);
  }

  /* Step 2: Abort if no existing page is found and createFlag is 0 */			第二步：如果没有找到存在的页面并且createFlag为0则中断。
  if( pPage || createFlag==0 ){	
    pcache1PinPage(pPage);
    goto fetch_out;
  }

  /* The pGroup local variable will normally be initialized by the
  ** pcache1EnterMutex() macro above.  But if SQLITE_MUTEX_OMIT is defined,
  ** then pcache1EnterMutex() is a no-op, so we have to initialize the
  ** local variable here.  Delaying the initialization of pGroup is an
  ** optimization:  The common case is to exit the module before reaching
  ** this point.
  */
#ifdef SQLITE_MUTEX_OMIT
  pGroup = pCache->pGroup;
#endif

  /* Step 3: Abort if createFlag is 1 but the cache is nearly full */			第三步：如果createFlag=1，但是cache近乎满的状态，中断
  assert( pCache->nPage >= pCache->nRecyclable );
  nPinned = pCache->nPage - pCache->nRecyclable;
  assert( pGroup->mxPinned == pGroup->nMaxPage + 10 - pGroup->nMinPage );
  assert( pCache->n90pct == pCache->nMax*9/10 );
  if( createFlag==1 && (
        nPinned>=pGroup->mxPinned
     || nPinned>=pCache->n90pct
     || pcache1UnderMemoryPressure(pCache)
  )){
    goto fetch_out;
  }

  if( pCache->nPage>=pCache->nHash && pcache1ResizeHash(pCache) ){
    goto fetch_out;
  }

  /* Step 4. Try to recycle a page. */											第四步：试图从LRU链表中重用页面
  if( pCache->bPurgeable && pGroup->pLruTail && (		判断：如果1、可清理、LRU链表可用、pCache中的页面大于配置的最大值 || 可清理页面大于等于最大页面 || 内存压力大
         (pCache->nPage+1>=pCache->nMax)
      || pGroup->nCurrentPage>=pGroup->nMaxPage
      || pcache1UnderMemoryPressure(pCache)
  )){
    PCache1 *pOther;
    pPage = pGroup->pLruTail;
    pcache1RemoveFromHash(pPage);
    pcache1PinPage(pPage);
    pOther = pPage->pCache;

    /* We want to verify that szPage and szExtra are the same for pOther
    ** and pCache.  Assert that we can verify this by comparing sums. */
    assert( (pCache->szPage & (pCache->szPage-1))==0 && pCache->szPage>=512 );
    assert( pCache->szExtra<512 );
    assert( (pOther->szPage & (pOther->szPage-1))==0 && pOther->szPage>=512 );
    assert( pOther->szExtra<512 );

    if( pOther->szPage+pOther->szExtra != pCache->szPage+pCache->szExtra ){
      pcache1FreePage(pPage);
      pPage = 0;
    }else{
      pGroup->nCurrentPage -= (pOther->bPurgeable - pCache->bPurgeable);
    }
  }

  /* Step 5. If a usable page buffer has still not been found, 
  ** attempt to allocate a new one. 								仍然找不到可用的页面，试图分配一个页面
  */
  if( !pPage ){
    if( createFlag==1 ) sqlite3BeginBenignMalloc();
    pPage = pcache1AllocPage(pCache);
    if( createFlag==1 ) sqlite3EndBenignMalloc();
  }

  if( pPage ){											设置相关属性，将新分配的page放入哈希表中
    unsigned int h = iKey % pCache->nHash;
    pCache->nPage++;
    pPage->iKey = iKey;
    pPage->pNext = pCache->apHash[h];
    pPage->pCache = pCache;
    pPage->pLruPrev = 0;
    pPage->pLruNext = 0;
    *(void **)pPage->page.pExtra = 0;
    pCache->apHash[h] = pPage;
  }

fetch_out:
  if( pPage && iKey>pCache->iMaxKey ){
    pCache->iMaxKey = iKey;
  }
  pcache1LeaveMutex(pGroup);
  return &pPage->page;
}


/*
** Implementation of the sqlite3_pcache.xUnpin method.			
**
** Mark a page as unpinned (eligible for asynchronous recycling).

sqlite3_pcache.xUnpin方法的实现。将一个页面标记为“未钉住的”	（对于异步回收操作是可用的）				
*/
static void pcache1Unpin(
  sqlite3_pcache *p, 
  sqlite3_pcache_page *pPg, 
  int reuseUnlikely
){
  PCache1 *pCache = (PCache1 *)p;
  PgHdr1 *pPage = (PgHdr1 *)pPg;
  PGroup *pGroup = pCache->pGroup;
 
  assert( pPage->pCache==pCache );
  pcache1EnterMutex(pGroup);

  /* It is an error to call this function if the page is already 		如果page已经是PGroupLRU列表中的一部分，调用该方法是错误的行为（因为LRU中的都是未钉住的）
  ** part of the PGroup LRU list.
  如果page已经是LRU列表一部分，返回错误
  */
  assert( pPage->pLruPrev==0 && pPage->pLruNext==0 );
  assert( pGroup->pLruHead!=pPage && pGroup->pLruTail!=pPage );

  if( reuseUnlikely || pGroup->nCurrentPage>pGroup->nMaxPage ){
    pcache1RemoveFromHash(pPage);
    pcache1FreePage(pPage);
  }else{
    /* Add the page to the PGroup LRU list. */	将页面添加到LRU列表中
    if( pGroup->pLruHead ){
      pGroup->pLruHead->pLruPrev = pPage; 添加在双向链表头部
      pPage->pLruNext = pGroup->pLruHead;
      pGroup->pLruHead = pPage;
    }else{
      pGroup->pLruTail = pPage;
      pGroup->pLruHead = pPage;
    }
    pCache->nRecyclable++;
  }

  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xRekey method. 
重置页号
*/
static void pcache1Rekey(
  sqlite3_pcache *p,
  sqlite3_pcache_page *pPg,
  unsigned int iOld,
  unsigned int iNew
){
  PCache1 *pCache = (PCache1 *)p;
  PgHdr1 *pPage = (PgHdr1 *)pPg;
  PgHdr1 **pp;
  unsigned int h; 
  assert( pPage->iKey==iOld );
  assert( pPage->pCache==pCache );

  pcache1EnterMutex(pCache->pGroup);

  h = iOld%pCache->nHash;
  pp = &pCache->apHash[h];
  while( (*pp)!=pPage ){			找到老的页号对应的页面
    pp = &(*pp)->pNext;
  }
  *pp = pPage->pNext;

  h = iNew%pCache->nHash;
  pPage->iKey = iNew;
  pPage->pNext = pCache->apHash[h];
  pCache->apHash[h] = pPage;
  if( iNew>pCache->iMaxKey ){
    pCache->iMaxKey = iNew;
  }

  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xTruncate method. 
**
** Discard all unpinned pages in the cache with a page number equal to
** or greater than parameter iLimit. Any pinned pages with a page number
** equal to or greater than iLimit are implicitly unpinned.
丢弃所有cache中页号大于等于iLimit的未钉住的页面。任何页号大于等于iLimit的钉住的页面隐式的未钉住
*/
static void pcache1Truncate(sqlite3_pcache *p, unsigned int iLimit){
  PCache1 *pCache = (PCache1 *)p;
  pcache1EnterMutex(pCache->pGroup);
  if( iLimit<=pCache->iMaxKey ){
    pcache1TruncateUnsafe(pCache, iLimit);
    pCache->iMaxKey = iLimit-1;
  }
  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xDestroy method. 
**
** Destroy a cache allocated using pcache1Create().
*/
static void pcache1Destroy(sqlite3_pcache *p){
  PCache1 *pCache = (PCache1 *)p;
  PGroup *pGroup = pCache->pGroup;
  assert( pCache->bPurgeable || (pCache->nMax==0 && pCache->nMin==0) );
  pcache1EnterMutex(pGroup);
  pcache1TruncateUnsafe(pCache, 0);
  assert( pGroup->nMaxPage >= pCache->nMax );
  pGroup->nMaxPage -= pCache->nMax;
  assert( pGroup->nMinPage >= pCache->nMin );
  pGroup->nMinPage -= pCache->nMin;
  pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
  pcache1EnforceMaxPage(pGroup);
  pcache1LeaveMutex(pGroup);
  sqlite3_free(pCache->apHash);
  sqlite3_free(pCache);
}

/*
** This function is called during initialization (sqlite3_initialize()) to
** install the default pluggable cache module, assuming the user has not
** already provided an alternative.
*/
void sqlite3PCacheSetDefault(void){
  static const sqlite3_pcache_methods2 defaultMethods = {
    1,                       /* iVersion */
    0,                       /* pArg */
    pcache1Init,             /* xInit */
    pcache1Shutdown,         /* xShutdown */
    pcache1Create,           /* xCreate */
    pcache1Cachesize,        /* xCachesize */
    pcache1Pagecount,        /* xPagecount */
    pcache1Fetch,            /* xFetch */
    pcache1Unpin,            /* xUnpin */
    pcache1Rekey,            /* xRekey */
    pcache1Truncate,         /* xTruncate */
    pcache1Destroy,          /* xDestroy */
    pcache1Shrink            /* xShrink */
  };
  sqlite3_config(SQLITE_CONFIG_PCACHE2, &defaultMethods);
}

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
** This function is called to free superfluous dynamically allocated memory
** held by the pager system. Memory in use by any SQLite pager allocated
** by the current thread may be sqlite3_free()ed.
**
** nReq is the number of bytes of memory required. Once this much has
** been released, the function returns. The return value is the total number 
** of bytes of memory released.
释放pager系统持有的多余的动态分配的内存
nReq：内存需要的字节数。一旦被释放，方法返回。返回值是被释放的字节总数
*/
int sqlite3PcacheReleaseMemory(int nReq){
  int nFree = 0;
  assert( sqlite3_mutex_notheld(pcache1.grp.mutex) );
  assert( sqlite3_mutex_notheld(pcache1.mutex) );
  if( pcache1.pStart==0 ){
    PgHdr1 *p;
    pcache1EnterMutex(&pcache1.grp);
    while( (nReq<0 || nFree<nReq) && ((p=pcache1.grp.pLruTail)!=0) ){
      nFree += pcache1MemSize(p->page.pBuf);
#ifdef SQLITE_PCACHE_SEPARATE_HEADER
      nFree += sqlite3MemSize(p);
#endif
      pcache1PinPage(p);
      pcache1RemoveFromHash(p);
      pcache1FreePage(p);
    }
    pcache1LeaveMutex(&pcache1.grp);
  }
  return nFree;
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

#ifdef SQLITE_TEST
/*
** This function is used by test procedures to inspect the internal state
** of the global cache.
*/
void sqlite3PcacheStats(
  int *pnCurrent,      /* OUT: Total number of pages cached */
  int *pnMax,          /* OUT: Global maximum cache size */
  int *pnMin,          /* OUT: Sum of PCache1.nMin for purgeable caches */
  int *pnRecyclable    /* OUT: Total number of pages available for recycling */
){
  PgHdr1 *p;
  int nRecyclable = 0;
  for(p=pcache1.grp.pLruHead; p; p=p->pLruNext){
    nRecyclable++;
  }
  *pnCurrent = pcache1.grp.nCurrentPage;
  *pnMax = (int)pcache1.grp.nMaxPage;
  *pnMin = (int)pcache1.grp.nMinPage;
  *pnRecyclable = nRecyclable;
}
#endif
