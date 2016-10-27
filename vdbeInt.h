/*
** 2003 September 6
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This is the header file for information that is private to the
** VDBE.  This information used to all be at the top of the single 
** source code file "vdbe.c".  When that file became too big (over
** 6000 lines long) it was split up into several smaller files and
** this header information was factored out.
*/
VDBE私有信息。这些信息被源码文件”vdbe.c“的顶层使用。当那个文件过大时(>6000行）
它将被划分成几个更小的文件，这个头信息被分解。
#ifndef _VDBEINT_H_
#define _VDBEINT_H_

/*
** SQL is translated into a sequence of instructions to be			
** executed by a virtual machine.  Each instruction is an instance
** of the following structure.

	SQL被翻译成一个指令序列，这些指令序列由虚拟机执行。每一个指令都是VdbeOp结构体的一个实例。
*/
typedef struct VdbeOp Op;

/*
** Boolean values  无符号Boolean值
*/
typedef unsigned char Bool;

/* Opaque type used by code in vdbesort.c 不透明类型，被vdbesort.c中的代码使用*/
typedef struct VdbeSorter VdbeSorter;		//排序器

/* Opaque type used by the explainer 被解释器使用的不透明类型*/
typedef struct Explain Explain;

/*
	一个游标是一个数据库文件中一个BTree内部的一个指针。游标能够根据特定的键检索BTree中的entry
	或者遍历所有BTree中的entries。用户可以在游标当前指向的位置插入新的entry或者获取键/值
	每个VM打开的游标由下面这个结构体表示
** A cursor is a pointer into a single BTree within a database file.
** The cursor can seek to a BTree entry with a particular key, or
** loop over all entries of the Btree.  You can also insert new BTree
** entries or retrieve the key or data from the entry that the cursor
** is currently pointing to.
** 
** Every cursor that the virtual machine has open is represented by an
** instance of the following structure.
*/
struct VdbeCursor {
  BtCursor *pCursor;    /* 后端的 cursor 结构体*/
  Btree *pBt;           /* Separate file holding temporary table */					//保存了一个临时表的单独的文件
  KeyInfo *pKeyInfo;    /* Info about index keys needed by index cursors */			//索引游标需要的索引键信息
  int iDb;              /* Index of cursor database in db->aDb[] (or -1) */			//db->aDb[] (or -1)中数据库的游标索引
  int pseudoTableReg;   /* Register holding pseudotable content. */					//注册保存虚表内容
  int nField;           /* Number of fields in the header 。*/						//header中的域数量
  Bool zeroed;          /* True if zeroed out and ready for reuse */				//如果置零，表示准备重用，值为真
  Bool rowidIsValid;    /* True if lastRowid is valid 如果上一个ROwid有效*/
  Bool atFirst;         /* True if pointing to first entry */						//是否指向第一个entry
  Bool useRandomRowid;  /* Generate new record numbers semi-randomly */				//生成新的半随机记录号
  Bool nullRow;         /* True if pointing to a row with no data */				//是否指向一个没有数据的行
  Bool deferredMoveto;  /* A call to sqlite3BtreeMoveto() is needed */				//是否需要调用后续的sqlite3BtreeMoveto()
  Bool isTable;         /* True if a table requiring integer keys */
  Bool isIndex;         /* True if an index containing keys only - no data */		//如果是一个只包含key但没有数据的索引返回真
  Bool isOrdered;       /* True if the underlying table is BTREE_UNORDERED */		//判断底层的表是否是BTREE_UNORDERED
  Bool isSorter;        /* True if a new-style sorter */							//如果是一个新型的排序器返回真？
  sqlite3_vtab_cursor *pVtabCursor;  /* The cursor for a virtual table */			//虚表游标
  const sqlite3_module *pModule;     /* Module for cursor pVtabCursor */			//pVtabCursor游标模块
  i64 seqCount;         /* Sequence counter */	//指令计数器？
  i64 movetoTarget;     /* Argument to the deferred sqlite3BtreeMoveto() */			//后续的sqlite3BtreeMoveto()参数
  i64 lastRowid;        /* Last rowid from a Next or NextIdx operation 上一个rowid*///来源于一个Next或NextIdx操作
  VdbeSorter *pSorter;  /* Sorter object for OP_SorterOpen cursors */				//排序器对象 for OP_SorterOpen游标

  /* Result of last sqlite3BtreeMoveto() done by an OP_NotExists or 
  ** OP_IsUnique opcode on this cursor. 
  
  上一次由作用于当前游标上的操作码OP_NotExists或者OP_IsUnique执行的sqlite3BtreeMoveto()的执行结果
  */
  int seekResult;

  /*
	游标当前指向的数据记录头的缓存信息。只有在cacheStatus和Vdbe.cacheCtr匹配的情况下有效。
	Vdbe.cacheCtr值不会为CACHE_STALE，当设置cacheStatus=CACHE_STALE时，保证cache已经过期
	Cached information about the header for the data record that the
  ** cursor is currently pointing to.  Only valid if cacheStatus matches
  ** Vdbe.cacheCtr.  Vdbe.cacheCtr will never take on the value of
  ** CACHE_STALE and so setting cacheStatus=CACHE_STALE guarantees that
  ** the cache is out of date.
  ** aRow可能指向(短暂的)当前行数据，和可能为NULL
  ** aRow might point to (ephemeral) data for the current row, or it might
  ** be NULL.
  */
  u32 cacheStatus;      /* Cache is valid if this matches Vdbe.cacheCtr */			//如果与Vdbe.cacheCtr匹配，则有效
  int payloadSize;      /* Total number of bytes in the record */					//记录中总的字节数
  u32 *aType;           /* Type values for all entries in the record */				//记录中所有entries的类型值
  u32 *aOffset;         /* Cached offsets to the start of each columns data */		//每一列数据相对缓存起始位置的偏移量
  u8 *aRow;             /* Data for the current row, if all on one page */			//当前行的数据(如果该行所有数据都在page中)
};
typedef struct VdbeCursor VdbeCursor;

/*
	当一个子程序被执行（OP_Program），该类型的结构体被分配去存储程序计数器
	=的当前值、当前内存单元数组、各种其他存储在Vdbe结构中的frame指定值。
	当子程序完成之后，这些值又从VdbeFrame复制回Vdbe，将VM的状态恢复到子程序开始执行之前的状态
** When a sub-program is executed (OP_Program), a structure of this type
** is allocated to store the current value of the program counter, as
** well as the current memory cell array and various other frame specific
** values stored in the Vdbe struct. When the sub-program is finished, 
** these values are copied back to the Vdbe from the VdbeFrame structure,
** restoring the state of the VM to as it was before the sub-program
** began executing.
**
** The memory for a VdbeFrame object is allocated and managed by a memory 			//VdbeFrame对象的主存由父(calling)frame中的一个内存单元分配和管理。
** cell in the parent (calling) frame. When the memory cell is deleted or			//当内存单元被删除或者复写，VdbeFrame对象不会被马上释放，而是被链接
** overwritten, the VdbeFrame object is not freed immediately. Instead, it	 		//到Vdbe.pDelFrame列表中。当VM在VdbeHalt()中被重置，Vdbe.pDelFrame列表中的内容即被删除
** is linked into the Vdbe.pDelFrame list. The contents of the Vdbe.pDelFrame
** list is deleted when the VM is reset in VdbeHalt(). The reason for doing			//采取这种方式而非马上删除VdbeFrame的原因是为了在属于子frame的内存单元被释放时，避免循环调用sqlite3VdbeMemRelease()
** this instead of deleting the VdbeFrame immediately is to avoid recursive
** calls to sqlite3VdbeMemRelease() when the memory cells belonging to the
** child frame are released.
**																					//当前正在执行的frame被存储在Vdbe.pFrame中。  如果当前正在执行的frame是主程序（包含了很多子程序），Vdbe.pFrame被设置为NULL
** The currently executing frame is stored in Vdbe.pFrame. Vdbe.pFrame is
** set to NULL if the currently executing frame is the main program.
*/
typedef struct VdbeFrame VdbeFrame;						//相当于，当子程序执行时，记录子程序执行前的状态(程序计数器，内存单元数等)，等子程序执行完成后，将执行前的状态恢复到父frame中
struct VdbeFrame {
  Vdbe *v;                /* VM this frame belongs to */						//当前frame所属的VM
  VdbeFrame *pParent;     /* Parent of this frame, or NULL if parent is main*/	//当前frame的父亲，或者NULL(当前frame为主程序)
  Op *aOp;                /* Program instructions for parent frame */			//父frame的程序指令
  Mem *aMem;              /* Array of memory cells for parent frame */			//父frame的主存单元数组
  u8 *aOnceFlag;          /* Array of OP_Once flags for parent frame */			//父frame的OP_Once flags数组
  VdbeCursor **apCsr;     /* Array of Vdbe cursors for parent frame */			//
  void *token;            /* Copy of SubProgram.token */						//SubProgram.token的一个拷贝
  i64 lastRowid;          /* Last insert rowid (sqlite3.lastRowid) */			//上一次插入的rowid(sqlite3.lastRowid)
  u16 nCursor;            /* Number of entries in apCsr */						//apCsr中的entry数
  int pc;                 /* Program Counter in parent (calling) frame */		//在父(calling) frame中的程序计数器
  int nOp;                /* Size of aOp array */								//aOp array的大小
  int nMem;               /* Number of entries in aMem */						//aMem中的entries数
  int nOnceFlag;          /* Number of entries in aOnceFlag */					//aOnceFlag中的entries数
  int nChildMem;          /* Number of memory cells for child frame */			//子frame的内存单元数
  int nChildCsr;          /* Number of cursors for child frame */				//子frame的cursors数
  int nChange;            /* Statement changes (Vdbe.nChanges)     */			//语句的改变(Vdbe.nChanges)
};

#define VdbeFrameMem(p) ((Mem *)&((u8 *)p)[ROUND8(sizeof(VdbeFrame))])

/*
** A value for VdbeCursor.cacheValid that means the cache is always invalid.
	CACHE_STALE这个值赋予VdbeCursor.cacheValid，意味着cache总是无效
*/
#define CACHE_STALE 0	

/*
	在内部实现中，vdb用Mem结构体操作几乎所有SQL值。每个Mem结构体可能缓存同一个存储值的多种表示(string,integer etc.)
** Internally, the vdbe manipulates nearly all SQL values as Mem
** structures. Each Mem struct may cache multiple representations (string,
** integer etc.) of the same value.
*/
struct Mem {
  sqlite3 *db;        /* The associated database connection */						//相关的数据库连接
  char *z;            /* String or BLOB value */									//String或BLOB值表述
  double r;           /* Real value */												//实数值表述
  union {																			//根据flags的取值，确定被设置的变量？？？？？？？
    i64 i;              /* Integer value used when MEM_Int is set in flags */		//MEM_Int被设置到flags中时，被使用
    int nZero;          /* Used when bit MEM_Zero is set in flags */				//MEM_Zero被设置到flags中时，被使用
    FuncDef *pDef;      /* Used only when flags==MEM_Agg */							//仅当flags==MEM_Agg时被使用
    RowSet *pRowSet;    /* Used only when flags==MEM_RowSet */						//仅当flags==MEM_RowSet时被使用
    VdbeFrame *pFrame;  /* Used when flags==MEM_Frame */							//当flags==MEM_Frame时被使用
  } u;																				//Mem表述的值类型
  int n;              /* Number of characters in string value, excluding '\0' */	//string值的大小，不包括'\0'
  u16 flags;          /* Some combination of MEM_Null, MEM_Str, MEM_Dyn, etc. */	//MEM_Null, MEM_Str, MEM_Dyn,等值的某个组合值
  u8  type;           /* One of SQLITE_NULL, SQLITE_TEXT, SQLITE_INTEGER, etc */	//存储值 类型：QLITE_NULL, SQLITE_TEXT, SQLITE_INTEGER, 等中的一个
  u8  enc;            /* SQLITE_UTF8, SQLITE_UTF16BE, SQLITE_UTF16LE */				//值的编码方式，SQLITE_UTF8, SQLITE_UTF16BE, SQLITE_UTF16LE 
#ifdef SQLITE_DEBUG
  Mem *pScopyFrom;    /* This Mem is a shallow copy of pScopyFrom */				//pScopyFrom的浅拷贝
  void *pFiller;      /* So that sizeof(Mem) is a multiple of 8 */					//以便sizeof(Mem)为8的倍数
#endif
  void (*xDel)(void *);  /* If not null, call this function to delete Mem.z */		//若不为null，调用该函数删除Mem.z(String or BLOB值表述)
  char *zMalloc;      /* Dynamic buffer allocated by sqlite3_malloc() */			//由sqlite3_malloc()分配的动态缓冲区
};

/* One or more of the following flags are set to indicate the validOK
** representations of the value stored in the Mem struct.
**
** If the MEM_Null flag is set, then the value is an SQL NULL value.
** No other flags may be set in this case.
**
** If the MEM_Str flag is set then Mem.z points at a string representation.
** Usually this is encoded in the same unicode encoding as the main
** database (see below for exceptions). If the MEM_Term flag is also
** set, then the string is null terminated. The MEM_Int and MEM_Real 
** flags may coexist with the MEM_Str flag.
	//通过设置一个或多个这些标记，指示存储在Mem结构体中的值的validOK表述
	//如果MEM_Null标记被设置，则该值是一个SQL NULL 值。这种情况下，没有其他的标记会被设置
	
	//如果MEM_Str flag被设置，Mem.z 指针指向一个字符串表示。
	//通常这个值用相同的unicode编码方式来为main数据库编码(见以下的可用值)。如果MEM_Term flag 也被设置，
	//string串终止。MEM_Int and MEM_Real标记可能与MEM_Str被结合设置。
*/
#define MEM_Null      0x0001   /* Value is NULL */  							//0000 0000 | 0000 0001
#define MEM_Str       0x0002   /* Value is a string */							//0000 0000 | 0000 0010
#define MEM_Int       0x0004   /* Value is an integer */						//0000 0000 | 0000 0100
#define MEM_Real      0x0008   /* Value is a real number 实数*/					//0000 0000 | 0000 1000
#define MEM_Blob      0x0010   /* Value is a BLOB 二进制大对象*/				//0000 0000 | 0000 1010
#define MEM_RowSet    0x0020   /* Value is a RowSet object 行集对象*/			//0000 0000 | 0000 1110
#define MEM_Frame     0x0040   /* Value is a VdbeFrame object */				//0000 0000 | 0010 1000
#define MEM_Invalid   0x0080   /* Value is undefined  表示值未定义(无效值)*/	//0000 0000 | 0101 0000
#define MEM_TypeMask  0x00ff   /* Mask of type bits 位掩码 */					//0000 0000 | 1111 1111

/* Whenever Mem contains a valid string or blob representation, one of
** the following flags must be set to determine the memory management
** policy for Mem.z.  The MEM_Term flag tells us whether or not the
** string is \000 or \u0000 terminated
	无论Mem包含的是一个string或blob表述，以下的flags之一必须被设置来确定Mem.z的主存管理策略
	MEM_Term flag 代表string是否终止？？？或者翻译成空？？
	
*/
#define MEM_Term      0x0200   /* String rep is nul terminated */					//字符串终结符表述 \000 or \u0000			0000 0010 | 0000 0000
#define MEM_Dyn       0x0400   /* Need to call sqliteFree() on Mem.z */ 			//需要在Mem.z上调用sqliteFree()				0000 0100 | 0000 0000
#define MEM_Static    0x0800   /* Mem.z points to a static string Mem.z	*/			// Mem.z指向一个静态字符串					0000 1000 | 0000 0000
#define MEM_Ephem     0x1000   /* Mem.z points to an ephemeral string */			// Mem.z指向一个短暂的字符串				0000 1010 | 0000 0000
#define MEM_Agg       0x2000   /* Mem.z points to an agg function context */		// Mem.z指向一个聚合函数上下文环境			0000 1110 | 0000 0000
#define MEM_Zero      0x4000   /* Mem.i contains count of 0s appended to blob */	//Mem.i 包含的追加到blob的0的数量			0010 1000 | 0000 0000
#ifdef SQLITE_OMIT_INCRBLOB
  #undef MEM_Zero
  #define MEM_Zero 0x0000
#endif

/*
** Clear any existing type flags from a Mem and replace them with f
	从一个Mem中清空任何已存在的类型标记，并用f替换它们
*/
#define MemSetTypeFlag(p, f) \
   ((p)->flags = ((p)->flags&~(MEM_TypeMask|MEM_Zero))|f)

/*
** Return true if a memory cell is not marked as invalid.  This macro
** is for use inside assert() statements only.
	返回真，如果一个主存单元未标记为invalid。这个宏，仅在assert()语句内部使用
*/
#ifdef SQLITE_DEBUG
#define memIsValid(M)  ((M)->flags & MEM_Invalid)==0
#endif


/* A VdbeFunc is just a FuncDef (defined in sqliteInt.h) that contains
** additional information about auxiliary information bound to arguments
** of the function.  This is used to implement the sqlite3_get_auxdata()
** and sqlite3_set_auxdata() APIs.  The "auxdata" is some auxiliary data
** that can be associated with a constant argument to a function.  This
** allows functions such as "regexp" to compile their constant regular
** expression argument once and reused the compiled code for multiple
** invocations.
	A VdbeFunc 就是一个FuncDef(定义在sqliteInt.h当中)，它包含了一些绑定到函数参数上的附加信息。
	被用来实现sqlite3_get_auxdata()和sqlite3_set_auxdata()APIs. "auxdata"是某个与一个函数的常量
	参数相关的附加数据。这使得“正则表达式”这样的函数编译他们的
	常量正则表达式参数一次，并在多次调用中重用的已编译的代码
*/
struct VdbeFunc {
  FuncDef *pFunc;               /* The definition of the function */				//函数的定义
  int nAux;                     /* Number of entries allocated for apAux[] */		//分配给apAux[]的项数
  struct AuxData {
    void *pAux;                   /* Aux data for the i-th argument					// 第i个附加数据
    void (*xDelete)(void *);      /* Destructor for the aux data */					//辅助数据的销毁器
  } apAux[1];                   /* One slot for each function argument */			//用于存放每个函数参数的槽
};

/*
** The "context" argument for a installable function.  A pointer to an
** instance of this structure is the first argument to the routines used
** implement the SQL functions.
**
** There is a typedef for this structure in sqlite.h.  So all routines,
** even the public interface to SQLite, can use a pointer to this structure.
** But this file is the only place where the internal details of this
** structure are known.
**
** This structure is defined inside of vdbeInt.h because it uses substructures
** (Mem) which are only defined there.
	“context”参数用于一个可安装功能。一个指向该结构体实例的指针是被用来实现SQL functions的函数的第一个参数，
	针对该结构体，在sqlite.h中有一个typedef。 所有所有的函数，甚至SQLite的公共接口，能使用一个指向该结构体的指针。
	但是这个文件是唯一描述这个结构体内部细节的地方。
	
	该结构体被定义在vdbeInt.h的内部，因为它用了仅在那里定义的子结构体(Mem)
*/
struct sqlite3_context {
  FuncDef *pFunc;       /* Pointer to function information.  MUST BE FIRST 指向功能函数信息的指针*/
  VdbeFunc *pVdbeFunc;  /* Auxilary data, if created. 如果创建了，则表示功能函数的附加信息*/
  Mem s;                /* The return value is stored here 返回值*/
  Mem *pMem;            /* Memory cell used to store aggregate context 被用来存储聚合上下文的内存单元*/
  CollSeq *pColl;       /* Collating sequence 排序序列/对照序列*/
  int isError;          /* Error code returned by the function. */	//function返回的错误码
  int skipFlag;         /* Skip skip accumulator loading if true */	//苏果威震的话，跳过skip累加器的加载
};

/*
** An Explain object accumulates indented output which is helpful
** in describing recursive data structures.	
	一个Explain对象累计带缩进的输出，这有助于对递归的数据结构进行描述
*/
struct Explain {
  Vdbe *pVdbe;       /* Attach the explanation to this Vdbe */	//关联expanation到当前Vdbe
  StrAccum str;      /* The string being accumulated */			//累计的string
  int nIndent;       /* Number of elements in aIndent */		//aIndent中元素的数目
  u16 aIndent[100];  /* Levels of indentation */				//缩进层级
  char zBase[100];   /* Initial space */						//初始空间
};

/*
** An instance of the virtual machine.  This structure contains the complete
** state of the virtual machine.
**
** The "sqlite3_stmt" structure pointer that is returned by sqlite3_prepare()
** is really a pointer to an instance of this structure.
**
** The Vdbe.inVtabMethod variable is set to non-zero for the duration of
** any virtual table method invocations made by the vdbe program. It is
** set to 2 for xDestroy method calls and 1 for all other methods. This
** variable is used for two purposes: to allow xDestroy methods to execute
** "DROP TABLE" statements and to prevent some nasty side effects of
** malloc failure when SQLite is invoked recursively by a virtual table 
** method function.

	虚拟机实例。该结构体包含了完整的虚拟机状态。
	
	sqlite3_prepare()函数返回"sqlite3_stmt"结构体指针，该指针实际上就是该结构体的实例。
	
	Vdbe.inVtabMethod 变量设置为 non-zero，以由vdbe程序构造的任何虚拟表方法调用的持续时间。？？？
	设置为2的时候则表示xDestroy方法的调用，设置为1时表示所有其他的方法。该变量的作用体现在两个
	方面：为了允许xDestroy方法执行"DROP TABLE"语句和防止SQLite被虚拟表方法函数递归/循环调用时
	分配内存失败而引发的一些副作用
*/
struct Vdbe {
  sqlite3 *db;            /* The database connection that owns this statement 用于该语句的数据库连接*/
  Op *aOp;                /* Space to hold the virtual machine's program */ 	//保存虚拟机程序的空间
  Mem *aMem;              /* The memory locations */		//内存位置
  Mem **apArg;            /* Arguments to currently executing user function */	//当前执行的用户函数的参数
  Mem *aColName;          /* Column names to return */		//返回的列名字
  Mem *pResultSet;        /* Pointer to an array of results */		//指向一个结果数据的指针
  int nMem;               /* Number of memory locations currently allocated */		//当前分配的内存位置数量
  int nOp;                /* Number of instructions in the program */			//程序中的指令数量
  int nOpAlloc;           /* Number of slots allocated for aOp[] */		//为aOp[]分配的槽数量
  int nLabel;             /* Number of labels used */			//用到的标签号
  int *aLabel;            /* Space to hold the labels */	//保存Label的空间
  u16 nResColumn;         /* Number of columns in one row of the result set */	//结果集中的一行的列数量
  u16 nCursor;            /* Number of slots in apCsr[] */		//apCsr[]中槽的数量
  u32 magic;              /* Magic number for sanity checking */	//完整性检查的数量？？
  char *zErrMsg;          /* Error message written here */			//被写入的错误消息
  Vdbe *pPrev,*pNext;     /* Linked list of VDBEs with the same Vdbe.db */	//同一个Vdbe.db链接的VDBEs列表
  VdbeCursor **apCsr;     /* One element of this array for each open cursor */	//数组中的一个元素for 每一个打开的游标
  Mem *aVar;              /* Values for the OP_Variable opcode. */		//OP_Variable操作码的值列表
  char **azVar;           /* Name of variables */			//变量名
  ynVar nVar;             /* Number of entries in aVar[] */		//aVar[]中的项的数量
  ynVar nzVar;            /* Number of entries in azVar[] */	//azVar[]中的项的数量
  u32 cacheCtr;           /* VdbeCursor row cache generation counter */		//VdbeCursor行缓存生成计数器
  int pc;                 /* The program counter */			//程序计数器
  int rc;                 /* Value to return */		//返回值
  u8 errorAction;         /* Recovery action to do in case of an error */		//发生错误时候的恢复操作
  u8 explain;             /* True if EXPLAIN present on SQL command */			//如果EXPLAIN出现在SQL命令中则为真。 
  u8 changeCntOn;         /* True to update the change-counter */				
  u8 expired;             /* True if the VM needs to be recompiled */		//如果VM需要重编译则为真
  u8 runOnlyOnce;         /* Automatically expire on reset */			//自动到期重置
  u8 minWriteFileFormat;  /* Minimum file format for writable database files */		//对于可写的数据库文件的最小文件格式。
  u8 inVtabMethod;        /* See comments above */			//看上面的注释
  u8 usesStmtJournal;     /* True if uses a statement journal */	//如果使用到statement journal，为真
  u8 readOnly;            /* True for read-only statements */		//只读语句，为真
  u8 isPrepareV2;         /* True if prepared with prepare_v2() */	//如果使用 prepare_v2()编译，为真
  int nChange;            /* Number of db changes made since last reset */		//上次重置后，db改变的数量
  yDbMask btreeMask;      /* Bitmask of db->aDb[] entries referenced */			//db->aDb[]项参考的位掩码
  yDbMask lockMask;       /* Subset of btreeMask that requires a lock */		//请求一个锁的btreeMask的子集
  int iStatement;         /* Statement number (or 0 if has not opened stmt) */	//语句号码，0表示未打开stmt
  int aCounter[3];        /* Counters used by sqlite3_stmt_status() */			//sqlite3_stmt_status()使用的计数器
#ifndef SQLITE_OMIT_TRACE
  i64 startTime;          /* Time when query started - used for profiling */	//查询开始时间
#endif
  i64 nFkConstraint;      /* Number of imm. FK constraints this VM */			//imm数量。 VM的FK约束
  i64 nStmtDefCons;       /* Number of def. constraints when stmt started */	//语句开启时def. constraints 的数量
  char *zSql;             /* Text of the SQL statement that generated this */	//SQL语句串
  void *pFree;            /* Free this when deleting the vdbe */			//删除Vdbe时释放
#ifdef SQLITE_DEBUG
  FILE *trace;            /* Write an execution trace here, if not NULL */		//不为NULL时，将执行路径写到其中
#endif
#ifdef SQLITE_ENABLE_TREE_EXPLAIN
  Explain *pExplain;      /* The explainer */			//解释器
  char *zExplain;         /* Explanation of data structures */		//数据结构的解释
#endif
  VdbeFrame *pFrame;      /* Parent frame */			//父frame
  VdbeFrame *pDelFrame;   /* List of frame objects to free on VM reset */	//VM重置时被释放的frame对象列表
  int nFrame;             /* Number of frames in pFrame list */			//pFrame list大小
  u32 expmask;            /* Binding to these vars invalidates VM */	//
  SubProgram *pProgram;   /* Linked list of all sub-programs used by VM */	//VM使用的所有子程序的链接链表
  int nOnceFlag;          /* Size of array aOnceFlag[] */		// aOnceFlag[]数组的大小
  u8 *aOnceFlag;          /* Flags for OP_Once */		//OP_Once的标记
};

/*
** The following are allowed values for Vdbe.magic		Vdbe.magic的可取值（4 Bytes）
*/
#define VDBE_MAGIC_INIT     0x26bceaa5    /* Building a VDBE program */		//构建一个VDBE程序
#define VDBE_MAGIC_RUN      0xbdf20da3    /* VDBE is ready to execute */	//VDBE准备好去执行
#define VDBE_MAGIC_HALT     0x519c2973    /* VDBE has completed execution */	//VDBE完成执行
#define VDBE_MAGIC_DEAD     0xb606c3c8    /* The VDBE has been deallocated */	//VDBE已经释放

/*
** Function prototypes 函数原型			
*/
void sqlite3VdbeFreeCursor(Vdbe *, VdbeCursor*);		
void sqliteVdbePopStack(Vdbe*,int);
int sqlite3VdbeCursorMoveto(VdbeCursor*);
#if defined(SQLITE_DEBUG) || defined(VDBE_PROFILE)
void sqlite3VdbePrintOp(FILE*, int, Op*);
#endif
u32 sqlite3VdbeSerialTypeLen(u32);
u32 sqlite3VdbeSerialType(Mem*, int);
u32 sqlite3VdbeSerialPut(unsigned char*, int, Mem*, int);
u32 sqlite3VdbeSerialGet(const unsigned char*, u32, Mem*);
void sqlite3VdbeDeleteAuxData(VdbeFunc*, int);

int sqlite2BtreeKeyCompare(BtCursor *, const void *, int, int, int *);
int sqlite3VdbeIdxKeyCompare(VdbeCursor*,UnpackedRecord*,int*);
int sqlite3VdbeIdxRowid(sqlite3*, BtCursor *, i64 *);
int sqlite3MemCompare(const Mem*, const Mem*, const CollSeq*);
int sqlite3VdbeExec(Vdbe*);
int sqlite3VdbeList(Vdbe*);
int sqlite3VdbeHalt(Vdbe*);
int sqlite3VdbeChangeEncoding(Mem *, int);
int sqlite3VdbeMemTooBig(Mem*);
int sqlite3VdbeMemCopy(Mem*, const Mem*);
void sqlite3VdbeMemShallowCopy(Mem*, const Mem*, int);		//浅拷贝
void sqlite3VdbeMemMove(Mem*, Mem*);
int sqlite3VdbeMemNulTerminate(Mem*);
int sqlite3VdbeMemSetStr(Mem*, const char*, int, u8, void(*)(void*));
void sqlite3VdbeMemSetInt64(Mem*, i64);
#ifdef SQLITE_OMIT_FLOATING_POINT
# define sqlite3VdbeMemSetDouble sqlite3VdbeMemSetInt64
#else
  void sqlite3VdbeMemSetDouble(Mem*, double);
#endif
void sqlite3VdbeMemSetNull(Mem*);
void sqlite3VdbeMemSetZeroBlob(Mem*,int);
void sqlite3VdbeMemSetRowSet(Mem*);
int sqlite3VdbeMemMakeWriteable(Mem*);
int sqlite3VdbeMemStringify(Mem*, int);
i64 sqlite3VdbeIntValue(Mem*);
int sqlite3VdbeMemIntegerify(Mem*);
double sqlite3VdbeRealValue(Mem*);
void sqlite3VdbeIntegerAffinity(Mem*);
int sqlite3VdbeMemRealify(Mem*);
int sqlite3VdbeMemNumerify(Mem*);
int sqlite3VdbeMemFromBtree(BtCursor*,int,int,int,Mem*);
void sqlite3VdbeMemRelease(Mem *p);
void sqlite3VdbeMemReleaseExternal(Mem *p);
#define VdbeMemRelease(X)  \
  if((X)->flags&(MEM_Agg|MEM_Dyn|MEM_RowSet|MEM_Frame)) \
    sqlite3VdbeMemReleaseExternal(X);
int sqlite3VdbeMemFinalize(Mem*, FuncDef*);
const char *sqlite3OpcodeName(int);
int sqlite3VdbeMemGrow(Mem *pMem, int n, int preserve);
int sqlite3VdbeCloseStatement(Vdbe *, int);
void sqlite3VdbeFrameDelete(VdbeFrame*);
int sqlite3VdbeFrameRestore(VdbeFrame *);
void sqlite3VdbeMemStoreType(Mem *pMem);
int sqlite3VdbeTransferError(Vdbe *p);

#ifdef SQLITE_OMIT_MERGE_SORT
# define sqlite3VdbeSorterInit(Y,Z)      SQLITE_OK
# define sqlite3VdbeSorterWrite(X,Y,Z)   SQLITE_OK
# define sqlite3VdbeSorterClose(Y,Z)
# define sqlite3VdbeSorterRowkey(Y,Z)    SQLITE_OK
# define sqlite3VdbeSorterRewind(X,Y,Z)  SQLITE_OK
# define sqlite3VdbeSorterNext(X,Y,Z)    SQLITE_OK
# define sqlite3VdbeSorterCompare(X,Y,Z) SQLITE_OK
#else
int sqlite3VdbeSorterInit(sqlite3 *, VdbeCursor *);
void sqlite3VdbeSorterClose(sqlite3 *, VdbeCursor *);
int sqlite3VdbeSorterRowkey(const VdbeCursor *, Mem *);
int sqlite3VdbeSorterNext(sqlite3 *, const VdbeCursor *, int *);
int sqlite3VdbeSorterRewind(sqlite3 *, const VdbeCursor *, int *);
int sqlite3VdbeSorterWrite(sqlite3 *, const VdbeCursor *, Mem *);
int sqlite3VdbeSorterCompare(const VdbeCursor *, Mem *, int *);
#endif

#if !defined(SQLITE_OMIT_SHARED_CACHE) && SQLITE_THREADSAFE>0
  void sqlite3VdbeEnter(Vdbe*);
  void sqlite3VdbeLeave(Vdbe*);
#else
# define sqlite3VdbeEnter(X)
# define sqlite3VdbeLeave(X)
#endif

#ifdef SQLITE_DEBUG
void sqlite3VdbeMemAboutToChange(Vdbe*,Mem*);
#endif

#ifndef SQLITE_OMIT_FOREIGN_KEY
int sqlite3VdbeCheckFk(Vdbe *, int);
#else
# define sqlite3VdbeCheckFk(p,i) 0
#endif

int sqlite3VdbeMemTranslate(Mem*, u8);
#ifdef SQLITE_DEBUG
  void sqlite3VdbePrintSql(Vdbe*);
  void sqlite3VdbeMemPrettyPrint(Mem *pMem, char *zBuf);
#endif
int sqlite3VdbeMemHandleBom(Mem *pMem);

#ifndef SQLITE_OMIT_INCRBLOB
  int sqlite3VdbeMemExpandBlob(Mem *);
  #define ExpandBlob(P) (((P)->flags&MEM_Zero)?sqlite3VdbeMemExpandBlob(P):0)
#else
  #define sqlite3VdbeMemExpandBlob(x) SQLITE_OK
  #define ExpandBlob(P) SQLITE_OK
#endif

#endif /* !defined(_VDBEINT_H_) */
