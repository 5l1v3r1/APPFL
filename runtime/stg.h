#ifndef stg_h
#define stg_h
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdbool.h>

//------ stack and heap objects

typedef enum {          // superfluous, for sanity checking
  INT, 
  DOUBLE,
  BOOL,
  FLOAT,
  CHAR, 
  HEAPOBJ 
} ArgType;

typedef enum {
  // heap and stack objects
  FUN, 
  PAP, 
  CON,
  THUNK,
  BLACKHOLE,
  INDIRECT,
  // stack objects
  UPDCONT, 
  CASECONT, 
  CALLCONT, 
  FUNCONT,        // for strict evaluation
  // garbage collection
  FORWARD
} ObjType;
const char *objTypeNames[FORWARD+1];

struct _Obj;
struct _InfoTab;
typedef struct _Obj Obj;
typedef struct _InfoTab InfoTab;

//------ fake typedef fp (*fp)()
// how to roll these into one?
// seems like the only way to get a recursive type is to use a struct
typedef void (*vvfp)();
typedef vvfp (*FnPtr)();
typedef FnPtr (*CmmFnPtr)();

// PtrOrLiteral -- literal value or pointer to heap object 
typedef struct {
  ArgType argType;        // superfluous, for sanity checking
  union {
    int i;
    double d;
    float f;
    bool b;
    char c;
    Obj *op;
  };
} PtrOrLiteral;

// STG registers
extern PtrOrLiteral stgCurVal;  // current/return value

/*
  payload -- see README
*/

// stack or heap object
// in a proper implementation there will be no such struct declaration--
// they'll be variably-sized and self-describing


struct _Obj {
  InfoTab *infoPtr;         // canonical location of ObjType field
  ObjType objType;          // to distinguish PAP, FUN, BLACKHOLE, INDIRECT
  int argCount;             // for PAP, how many args already applied to?
  char ident[64];           // temporary, just for tracing
  // int payloadSize;          // this should really be in infotab?
  PtrOrLiteral payload[];
};

/*
typedef struct {
  CmmFnPtr retAddr;         // no need for an infotab
  ObjType objType;          // for sanity checking
  char ident[64];           // temporary, just for tracing
  PtrOrLiteral payload[32]; // fixed for now
} Cont;
*/

typedef struct {
  int payloadSize;
} LayoutInfo;

typedef struct {
  int arity;
  // curry paper suggests that we need type info
} FUNfields;

typedef struct {
} PAPfields;

typedef struct {
  int tag;
  int arity;
  char conName[64];
} CONfields;

typedef struct {
  //
} THUNKfields;

typedef struct {
  //
} UPDCONTfields;

typedef struct {
  //
} CASECONTfields;

typedef struct {
  //
} CALLCONTfields;

typedef struct {
  //
} FUNCONTfields;


// InfoTab
struct _InfoTab {
  char name[32];  // for debugging
  int fvCount;    // lexically determined, should be in layout
  CmmFnPtr entryCode; 
  ObjType objType; // kind of object, tag for union
  LayoutInfo layoutInfo;
  union {
    FUNfields funFields;
    PAPfields papFields;
    CONfields conFields;
    THUNKfields thunkFields;
    UPDCONTfields updcontFields;
    CASECONTfields casecontFields;
    CALLCONTfields callcontFields;
    FUNCONTfields funcontFields;
  };
};

extern void *stgHeap, *stgHP;
extern void *stgStack, *stgSP;
extern const size_t stgHeapSize;
extern const size_t stgStackSize;
extern size_t stgStatObjCount;
extern Obj * stgStatObj[];
extern void initStg();
extern void showStgObj(Obj *);
extern void showStgHeap();
extern void showStgStack();
extern void showStgVal(PtrOrLiteral);
extern void showIT(InfoTab *);
// extern int objSize(Obj *);

// allocate Obj on heap, returning pointer to new Obj
extern Obj* stgNewHeapObj(InfoTab *itp);
extern Obj* stgNewHeapPAP(InfoTab *itp, int argc);
// allocate Obj on continuation stack, returning pointer to new Obj
extern Obj *stgAllocCallCont2(InfoTab *it, int payloadSize);
extern Obj *stgAllocCont(InfoTab *it);
// remove Obj from top of continuation stack, returning pointer to de-alloced Obj
Obj *stgPopCont();


# define STGCALL0(f)				\
  CALL0_0(f)

# define STGCALL1(f,v1)				\
  CALL1_0(f,v1)

# define STGCALL2(f,v1,v2)			\
  CALL2_0(f,v1,v2)

# define STGCALL3(f,v1,v2,v3)			\
  CALL3_0(f,v1,v2,v3)

# define STGCALL4(f,v1,v2,v3,v4)		\
  CALL4_0(f,v1,v2,v3,v4)

#define STGJUMP0(f)				\
  JUMP0(f)

#define STGJUMP1(f,v1)				\
  JUMP1(f,v1)

#define STGJUMP2(f,v1,v2)			\
  JUMP2(f,v1,v2)

#define STGJUMP3(f,v1,v2,v3)			\
  JUMP3(f,v1,v2,v3)

#define STGJUMP4(f,v1,v2,v3,v4)			\
  JUMP4(f,v1,v2,v3,v4)

#define STGJUMP5(f,v1,v2,v3,v4,v5)		\
  JUMP5(f,v1,v2,v3,v4,v5)

#define STGJUMP6(f,v1,v2,v3,v4,v5,v6)		\
  JUMP6(f,v1,v2,v3,v4,v5,v6)

#define STGJUMP7(f,v1,v2,v3,v4,v5,v6,v7)	\
  JUMP7(f,v1,v2,v3,v4,v5,v6,v7)

#define STGJUMP8(f,v1,v2,v3,v4,v5,v6,v7,v8)	\
  JUMP8(f,v1,v2,v3,v4,v5,v6,v7,v8)


// return through continuation stack
/*
define STGRETURN0()				\
  STGJUMP0(((Cont *)stgSP)->retAddr)
*/

#define STGRETURN0()				\
  STGJUMP0(((Obj *)stgSP)->infoPtr->entryCode)

// no explicit return value stack
/*
define STGRETURN1(r)				\
  do {						\
    stgCurVal = r;				\
    STGJUMP0(((Cont *)stgSP)->retAddr);		\
  } while(0)
*/
#define STGRETURN1(r)				\
  do {						\
    stgCurVal = r;				\
    STGRETURN0();				\
  } while(0)


#define STGAPPLY1(f,v1)				\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 1};	\
    STGJUMP3(stgApply,N,f,v1);			\
  } while(0)

#define STGAPPLY2(f,v1,v2)			\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 2};	\
    STGJUMP4(stgApply,N,f,v1,v2);		\
  } while(0)

#define STGAPPLY3(f,v1,v2,v3)			\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 3};	\
    STGJUMP5(stgApply,N,f,v1,v2,v3);		\
  } while(0)

#define STGAPPLY4(f,v1,v2,v3,v4)		\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 4};	\
    STGJUMP6(stgApply,N,f,v1,v2,v3,v4);		\
  } while(0)

#define STGAPPLY5(f,v1,v2,v3,v4,v5)		\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 5};	\
    STGJUMP7(stgApply,N,f,v1,v2,v3,v4,v5);	\
  } while(0)

#define STGAPPLY6(f,v1,v2,v3,v4,v5,v6)		\
  do {						\
    PtrOrLiteral N = {.argType = INT, .i = 6};	\
    STGJUMP8(stgApply,N,f,v1,v2,v3,v4,v5,v6);	\
  } while(0)




/*
define STGRETURN1(o)				\
  do {						\
  JUMP1(((Cont *)stgSP)->infoPtr->entryCode, o) \
  } while (0)
*/

#endif  //ifdef stg_h
