/*
** 2007 August 14
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** 该文件包含了low-level主存分配驱动，满足当SQLite将用标准C函数库的 
** malloc/realloc/free借口去获得它需要的主存。
** 该文件包含了于sqlite3_mem_methods中声明的low-level主存分配的函数实现。
** 本文件的内容仅仅被用于SQLITE_SYSTEM_MALLOC是否被定义。SQLITE_SYSTEM_MALLOC宏
** 在SQLITE_MEMDEBUG和SQLITE_WIN32_MALLOC都没被定义的情况下自动被定义。这个
** 默认配是为了能在该文件中使用内存分配函数。
** This file contains low-level memory allocation drivers for when
** SQLite will use the standard C-library malloc/realloc/free interface
** to obtain the memory it needs.
** This file contains implementations of the low-level memory allocation
** routines specified in the sqlite3_mem_methods object.  The content of
** this file is only used if SQLITE_SYSTEM_MALLOC is defined.  The
** SQLITE_SYSTEM_MALLOC macro is defined automatically if neither the
** SQLITE_MEMDEBUG nor the SQLITE_WIN32_MALLOC macros are defined.  The
** default configuration is to use memory allocation routines in this
** file.
** C-预处理宏
** C-preprocessor macro summary:
**
**    HAVE_MALLOC_USABLE_SIZE     The configure script sets this symbol if
**                                the malloc_usable_size() interface exists
**                                on the target platform.  Or, this symbol
**                                can be set manually, if desired.
**                                If an equivalent interface exists by
**                                a different name, using a separate -D
**                                option to rename it.
**
**    SQLITE_WITHOUT_ZONEMALLOC   Some older macs lack support for the zone
**                                memory allocator.  Set this symbol to enable
**                                building on older macs.
**
**    SQLITE_WITHOUT_MSIZE        Set this symbol to disable the use of
**                                _msize() on windows systems.  This might
**                                be necessary when compiling for Delphi,
**                                for example.
*/
#include "sqliteInt.h"

/*
** 该版本的主存分配器为默认值。当编译时没有指定其他主存分配器时该默认值被使用。
** This version of the memory allocator is the default.  It is
** used when no other memory allocator is specified using compile-time
** macros.
*/
#ifdef SQLITE_SYSTEM_MALLOC

/*
** The MSVCRT has malloc_usable_size() but it is called _msize().
** The use of _msize() is automatic, but can be disabled by compiling
** with -DSQLITE_WITHOUT_MSIZE
*/
#if defined(_MSC_VER) && !defined(SQLITE_WITHOUT_MSIZE)
# define SQLITE_MALLOCSIZE _msize
#endif

#if defined(__APPLE__) && !defined(SQLITE_WITHOUT_ZONEMALLOC)

/*
** Use the zone allocator available on apple products unless the
** SQLITE_WITHOUT_ZONEMALLOC symbol is defined.
*/
#include <sys/sysctl.h>
#include <malloc/malloc.h>
#include <libkern/OSAtomic.h>
static malloc_zone_t* _sqliteZone_;
#define SQLITE_MALLOC(x) malloc_zone_malloc(_sqliteZone_, (x))
#define SQLITE_FREE(x) malloc_zone_free(_sqliteZone_, (x));
#define SQLITE_REALLOC(x,y) malloc_zone_realloc(_sqliteZone_, (x), (y))
#define SQLITE_MALLOCSIZE(x) \
        (_sqliteZone_ ? _sqliteZone_->size(_sqliteZone_,x) : malloc_size(x))

#else /* if not __APPLE__ */

/*
** 使用标准C库对非-apple系统的主存进行分配和释放，如果定义了SQLITE_WITHOUT_ZONEMALLOC也能被苹果系统使用
** Use standard C library malloc and free on non-Apple systems.  
** Also used by Apple systems if SQLITE_WITHOUT_ZONEMALLOC is defined.
*/
#define SQLITE_MALLOC(x)    malloc(x)
#define SQLITE_FREE(x)      free(x)
#define SQLITE_REALLOC(x,y) realloc((x),(y))

#if (defined(_MSC_VER) && !defined(SQLITE_WITHOUT_MSIZE)) \
      || (defined(HAVE_MALLOC_H) && defined(HAVE_MALLOC_USABLE_SIZE))
# include <malloc.h>    /* Needed for malloc_usable_size on linux */
#endif
#ifdef HAVE_MALLOC_USABLE_SIZE
# ifndef SQLITE_MALLOCSIZE
#  define SQLITE_MALLOCSIZE(x) malloc_usable_size(x)
# endif
#else
# undef SQLITE_MALLOCSIZE
#endif

#endif /* __APPLE__ or not __APPLE__ */

/*
** Like malloc(), but remember the size of the allocation
** so that we can find it later using sqlite3MemSize().
** 这里的函数传入的nByte>0, nByte<=0的情况在上层调用时就已经被处理
** For this low-level routine, we are guaranteed that nByte>0 because
** cases of nByte<=0 will be intercepted and dealt with by higher level
** routines.
*/
static void *sqlite3MemMalloc(int nByte){
#ifdef SQLITE_MALLOCSIZE
  void *p = SQLITE_MALLOC( nByte );
  if( p==0 ){
    testcase( sqlite3GlobalConfig.xLog!=0 );
    sqlite3_log(SQLITE_NOMEM, "failed to allocate %u bytes of memory", nByte);
  }
  return p;
#else
  sqlite3_int64 *p;
  assert( nByte>0 );
  nByte = ROUND8(nByte);
  p = SQLITE_MALLOC( nByte+8 );
  if( p ){
    p[0] = nByte;
    p++;
  }else{
    testcase( sqlite3GlobalConfig.xLog!=0 );
    sqlite3_log(SQLITE_NOMEM, "failed to allocate %u bytes of memory", nByte);
  }
  return (void *)p;
#endif
}

/*
** Like free() but works for allocations obtained from sqlite3MemMalloc()
** or sqlite3MemRealloc().
**
** For this low-level routine, we already know that pPrior!=0 since
** cases where pPrior==0 will have been intecepted and dealt with
** by higher-level routines.
*/
static void sqlite3MemFree(void *pPrior){
#ifdef SQLITE_MALLOCSIZE
  SQLITE_FREE(pPrior);
#else
  sqlite3_int64 *p = (sqlite3_int64*)pPrior;
  assert( pPrior!=0 );
  p--;
  SQLITE_FREE(p);
#endif
}

/*
** 返回xMalloc()或者xRealloc()返回的大小。
** Report the allocated size of a prior return from xMalloc()
** or xRealloc().
*/
static int sqlite3MemSize(void *pPrior){
#ifdef SQLITE_MALLOCSIZE
  return pPrior ? (int)SQLITE_MALLOCSIZE(pPrior) : 0;
#else
  sqlite3_int64 *p;
  if( pPrior==0 ) return 0;
  p = (sqlite3_int64*)pPrior;
  p--;
  return (int)p[0];
#endif
}

/*
** Like realloc().  Resize an allocation previously obtained from
** sqlite3MemMalloc().
**
** For this low-level interface, we know that pPrior!=0.  Cases where
** pPrior==0 while have been intercepted by higher-level routine and
** redirected to xMalloc.  Similarly, we know that nByte>0 becauses
** cases where nByte<=0 will have been intercepted by higher-level
** routines and redirected to xFree.
*/
static void *sqlite3MemRealloc(void *pPrior, int nByte){
#ifdef SQLITE_MALLOCSIZE
  void *p = SQLITE_REALLOC(pPrior, nByte);
  if( p==0 ){
    testcase( sqlite3GlobalConfig.xLog!=0 );
    sqlite3_log(SQLITE_NOMEM,
      "failed memory resize %u to %u bytes",
      SQLITE_MALLOCSIZE(pPrior), nByte);
  }
  return p;
#else
  sqlite3_int64 *p = (sqlite3_int64*)pPrior;
  assert( pPrior!=0 && nByte>0 );
  assert( nByte==ROUND8(nByte) ); /* EV: R-46199-30249 */
  p--;
  p = SQLITE_REALLOC(p, nByte+8 );
  if( p ){
    p[0] = nByte;
    p++;
  }else{
    testcase( sqlite3GlobalConfig.xLog!=0 );
    sqlite3_log(SQLITE_NOMEM,
      "failed memory resize %u to %u bytes",
      sqlite3MemSize(pPrior), nByte);
  }
  return (void*)p;
#endif
}

/*
** Round up a request size to the next valid allocation size.
*/
static int sqlite3MemRoundup(int n){
  return ROUND8(n);
}

/*
** Initialize this module.
*/
static int sqlite3MemInit(void *NotUsed){
#if defined(__APPLE__) && !defined(SQLITE_WITHOUT_ZONEMALLOC)
  int cpuCount;
  size_t len;
  if( _sqliteZone_ ){
    return SQLITE_OK;
  }
  len = sizeof(cpuCount);
  /* One usually wants to use hw.acctivecpu for MT decisions, but not here */
  sysctlbyname("hw.ncpu", &cpuCount, &len, NULL, 0);
  if( cpuCount>1 ){
    /* defer MT decisions to system malloc */
    _sqliteZone_ = malloc_default_zone();
  }else{
    /* only 1 core, use our own zone to contention over global locks, 
    ** e.g. we have our own dedicated locks */
    bool success;
    malloc_zone_t* newzone = malloc_create_zone(4096, 0);
    malloc_set_zone_name(newzone, "Sqlite_Heap");
    do{
      success = OSAtomicCompareAndSwapPtrBarrier(NULL, newzone, 
                                 (void * volatile *)&_sqliteZone_);
    }while(!_sqliteZone_);
    if( !success ){
      /* somebody registered a zone first */
      malloc_destroy_zone(newzone);
    }
  }
#endif
  UNUSED_PARAMETER(NotUsed);
  return SQLITE_OK;
}

/*
** Deinitialize this module.
*/
static void sqlite3MemShutdown(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  return;
}

/*
** This routine is the only routine in this file with external linkage.
**
** Populate the low-level memory allocation function pointers in
** sqlite3GlobalConfig.m with pointers to the routines in this file.
*/
void sqlite3MemSetDefault(void){
  static const sqlite3_mem_methods defaultMethods = {
     sqlite3MemMalloc,
     sqlite3MemFree,
     sqlite3MemRealloc,
     sqlite3MemSize,
     sqlite3MemRoundup,
     sqlite3MemInit,
     sqlite3MemShutdown,
     0
  };
  sqlite3_config(SQLITE_CONFIG_MALLOC, &defaultMethods);
}

#endif /* SQLITE_SYSTEM_MALLOC */
