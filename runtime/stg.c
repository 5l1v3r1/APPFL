#include <stdio.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> // for  mmap()
#include <fcntl.h>
#include <unistd.h> // for 
#include <stdio.h>
#include <errno.h>
#include <string.h>  // for memcpy()
#include <stdlib.h>  // for exit()
#ifndef __APPLE__
#include <malloc.h>  // for memalign()
#endif

#include "stg.h"
#include "cmm.h"
#include "obj.h"

void *stgHeap = NULL;
void *stgHP = NULL;
void *stgStack = NULL;
void *stgSP = NULL;

#ifndef __clang__
// register PtrOrLiteral stgCurVal asm("%r15");  // current value STG register
#else
PtrOrLiteral stgCurVal;  // current/return value
#endif

const char *objTypeNames[] = {
  "OBJTYPE0BAD",
  "FUN", 
  "PAP", 
  "CON",
  "THUNK",
  "BLACKHOLE",
  "INDIRECT"
};

const char *contTypeNames[] = {
  "BADCONTTYPE0",
  "BADCONTTYPE1",
  "BADCONTTYPE2",
  "BADCONTTYPE3",
  "BADCONTTYPE4",
  "BADCONTTYPE5",
  "BADCONTTYPE6",
  "UPDCONT", 
  "CASECONT", 
  "STACKCONT",
  "CALLCONT", 
  "FUNCONT"
};

Cont *stgAllocCont(CInfoTab *citp) {
  assert(citp->contType != CALLCONT &&
          "stgAllocCont: citp->contType == CALLCONT" );
  int payloadSize = citp->layoutInfo.payloadSize;
  size_t contSize = sizeof(Cont) + payloadSize * sizeof(PtrOrLiteral);
  contSize = ((contSize + 7)/8)*8; 
  fprintf(stderr, "allocating continuation with payloadSize %d\n", payloadSize);
  showCIT(citp);
  stgSP = (char *)stgSP - contSize;
  assert(stgSP >= stgStack);
  Cont *contp = (Cont *)stgSP;
  contp->cinfoPtr = citp;
  contp->_contSize = contSize;
  contp->entryCode = citp->entryCode;
  contp->contType = citp->contType;
  strcpy(contp->ident, citp->name);  // may be overwritten
  return contp;
}

// CALLCONTs have a common InfoTab entries but .layoutInfo is invalid
// payload[0].i == argc, the number of subsequent args
Cont *stgAllocCallCont(CInfoTab *citp, int argc) {
  assert(citp->contType == CALLCONT && 
	 "stgAllocCallCont: citp->contType != CALLCONT");
  size_t contSize = sizeof(Cont) + (argc + 1) * sizeof(PtrOrLiteral);
  contSize = ((contSize + 7)/8)*8; 
  fprintf(stderr, "allocating call continuation with argc %d\n", argc);
  showCIT(citp);
  stgSP = (char *)stgSP - contSize;
  assert(stgSP >= stgStack);
  Cont *contp = (Cont *)stgSP;
  contp->cinfoPtr = citp;
  contp->_contSize = contSize;
  contp->entryCode = citp->entryCode;
  contp->contType = CALLCONT;
#if USE_ARGTYPE
  contp->payload[0] = (PtrOrLiteral) {.argType = INT, .i = argc};
#else
  contp->payload[0] = (PtrOrLiteral) {.i = argc};
#endif
  strcpy(contp->ident, citp->name);  // may be overwritten
  return contp;
}

// TODO: 64 bit align
// return pointer to popped cont
// NOTE:  stgAllocCont will invalidate this pointer!
// CALLCONT is special
Cont *stgPopCont() {
  Cont *retVal = (Cont *)stgSP;
  CInfoTab *citp = getCInfoPtr(retVal);
  assert(citp->contType == getContType(retVal));
  assert( getCInfoPtr(retVal)->contType >= UPDCONT &&
          getCInfoPtr(retVal)->contType <= FUNCONT);
  int payloadSize;
  if (getCInfoPtr(retVal)->contType == CALLCONT) {
    payloadSize = retVal->payload[0].i + 1;
    fprintf(stderr, "popping call continuation with argc %d\n", payloadSize-1);
  } else {
    payloadSize = getCInfoPtr(retVal)->layoutInfo.payloadSize;
    fprintf(stderr, "popping continuation with payloadSize %d\n", payloadSize);
  }
  size_t contSize = sizeof(Cont) + payloadSize * sizeof(PtrOrLiteral);
  contSize = ((contSize + 7)/8)*8; 
  showCIT(getCInfoPtr(retVal));
  assert((char *)retVal + contSize <= (char *)stgStack + stgStackSize);
  stgSP = (char *)retVal + contSize;
  return retVal;
}

Obj* stgNewHeapObj(InfoTab *itp) {
  fprintf(stderr, "stgNewHeapObj: "); showIT(itp);
  int payloadSize = itp->layoutInfo.payloadSize;
  int fvs = itp->layoutInfo.boxedCount + itp->layoutInfo.unboxedCount;
  // assert(itp->fvCount == fvs);  // fvCount going away
  switch (itp->objType) {
  case FUN:
  case CON: assert(payloadSize == fvs && "stgNewHeapObj"); break;
  case THUNK: assert(payloadSize == fvs + 1 && "stgNewHeapObj"); break;
  default:  assert(false && "stgNewHeapObj");
  }
  size_t objSize = sizeof(Obj) + payloadSize * sizeof(PtrOrLiteral);
  objSize = ((objSize + 7)/8)*8; 
  Obj *objp = (Obj *)stgHP;
  stgHP = (char *)stgHP + objSize;
  memset(objp, 0, objSize); //zero out anything left by previous gc passes
  objp->infoPtr = itp;
  objp->_objSize = objSize;
#if USE_OBJTYPE
  objp->objType = itp->objType;
#endif
  strcpy(objp->ident, itp->name);  // may be overwritten
  fprintf(stderr, "stgNewHeapObj: "); showStgObj(objp);
  return objp;
}

// PAP is special
// pargc is number of pointer args to store
// npargc is number of non-pointer args to store
Obj* stgNewHeapPAP(InfoTab *itp, int pargc, int npargc) {
  assert(itp->objType == FUN && "stgNewHeapPap:  itp->objType != FUN" );
  int fvs = itp->layoutInfo.boxedCount + itp->layoutInfo.unboxedCount;
  // assert(itp->fvCount == fvs);      // fvCount going away
  assert(itp->layoutInfo.payloadSize == fvs);  // FUN
  fprintf(stderr, "stgNewHeapPap: "); showIT(itp);
  size_t objSize = sizeof(Obj) + 
    (fvs + pargc + npargc + 1) * sizeof(PtrOrLiteral);
  objSize = ((objSize + 7)/8)*8;
  Obj *objp = (Obj *)stgHP;
  stgHP = (char *)stgHP + objSize;
  memset(objp, 0, objSize); //zero out anything left by previous gc passes
  objp->infoPtr = setLSB2(itp); // set InfoPtr bit to say this is a PAP
  objp->_objSize = objSize;
#if USE_OBJTYPE
  objp->objType = PAP;
#endif
#if USE_ARGTYPE
  objp->payload[fvs] = (PtrOrLiteral) {.argType = INT, 
                                       .i = PNPACK(pargc, npargc)};
#else
  objp->payload[fvs] = (PtrOrLiteral) {.i = PNPACK(pargc, npargc)};
#endif
  strcpy(objp->ident, itp->name);  // may be overwritten
  fprintf(stderr, "stgNewHeapPAP: "); showStgObj(objp);
  return objp;
}

int getObjSize(Obj *o) {
  size_t objSize;
  ObjType type = getObjType(o);
  if (!(type >= FUN && type <= INDIRECT))
    fprintf(stderr, "bad obj type %d\n", type);
  assert(type >= FUN && type <= INDIRECT && "getObjSize:  bad obj type");
  if (type == PAP) {
    InfoTab *itp = getInfoPtr(o);
    int fvs = itp->layoutInfo.boxedCount + itp->layoutInfo.unboxedCount;
    objSize = sizeof(Obj) + (fvs + 1 + PNSIZE(o->payload[fvs].i)) * sizeof(PtrOrLiteral);
  } else {
    objSize = sizeof(Obj) + getInfoPtr(o)->layoutInfo.payloadSize * sizeof(PtrOrLiteral);
  }
  objSize = ((objSize + 7)/8)*8;
  assert(objSize == o->_objSize && "bad objSize");
  return objSize;
}

DEFUN0(stgCallCont) {
  // stgPopCont();  user must do this
  fprintf(stderr,"stgCallCont returning\n");
  RETURN0();  // fall back to the cmm trampoline
  ENDFUN;
}

CInfoTab it_stgCallCont __attribute__((aligned(8))) =
  { .name = "stgCallCont",
    //    .fvCount = 0,
    .entryCode = &stgCallCont,
    .contType = CALLCONT,
    .layoutInfo.boxedCount = -1,  // shouldn't be using this
    .layoutInfo.unboxedCount = -1,  // shouldn't be using this
  };

int getContSize(Cont *o) {
  size_t contSize;
  ContType type = getContType(o);
  assert(type >= UPDCONT && type <= FUNCONT && "getContSize:  bad cont type");
  if (type == CALLCONT) {
    contSize = sizeof(Cont) + (o->payload[0].i + 1) * sizeof(PtrOrLiteral);
  } else {
    contSize = sizeof(Cont) + getCInfoPtr(o)->layoutInfo.payloadSize * sizeof(PtrOrLiteral);
  }
  contSize = ((contSize + 7)/8)*8;
  assert(contSize == o->_contSize && "bad contSize");
  return contSize;
}



void showStgObjPretty(Obj *p);
void showStgObjDebug(Obj *p);

void showStgObj(Obj *p) {
  showStgObjPretty(p);
  // showStgObjDebug(Obj *p)
}
void showStgValDebug(PtrOrLiteral v);
void showStgValPretty(PtrOrLiteral v);

void showStgVal(PtrOrLiteral v) {
  showStgValPretty(v);
  // showStgValDebug(v);
}

void showIT(InfoTab *itp) {
  fprintf(stderr, "showIT: %s %s, bc %d ubc %d", 
	  objTypeNames[itp->objType], 
	  itp->name, 
	  itp->layoutInfo.boxedCount,
          itp->layoutInfo.unboxedCount);
  if (itp->objType != CALLCONT)
    fprintf(stderr, ", layoutInfo.payloadSize %d", itp->layoutInfo.payloadSize);
  fprintf(stderr, "\n");
}  

void showCIT(CInfoTab *citp) {
  fprintf(stderr, "showCIT: %s %s, bc %d ubc %d", 
	  contTypeNames[citp->contType], 
	  citp->name, 
	  citp->layoutInfo.boxedCount,
          citp->layoutInfo.unboxedCount);
  if (citp->contType != CALLCONT)
    fprintf(stderr, ", layoutInfo.payloadSize %d", citp->layoutInfo.payloadSize);
  fprintf(stderr, "\n");
}  

// ****************************************************************

static const int showDepthLimit = 1000;
static int depth;
static Obj *stack[1000];
static int stackp;


void showStgObjRecDebug(Obj *p);
void showStgObjDebug(Obj *p) {
  depth = 0;
  showStgObjRecDebug(p);
}

void showStgObjRecPretty(Obj *p);
void showStgObjPretty(Obj *p) {
  depth = 0;
  showStgObjRecPretty(p);
  fprintf(stderr,"\n");
}

void showStgCont(Obj *c) {
  ObjType type = getObjType(c);
  switch (type) {
  case UPDCONT:
    fprintf(stderr,"UPDCONT  %s\n", c->ident);
    return;

  case CASECONT:
    fprintf(stderr,"CASECONT %s\n", c->ident);
    return;

  case CALLCONT:
    fprintf(stderr,"CALLCONT %s\n", c->ident);
    return;

  case FUNCONT:
    fprintf(stderr,"FUNCONT  %s\n", c->ident);
    return;

  default:
    fprintf(stderr,"showStgCont default case! %d %s\n", type, objTypeNames[type]);
    exit(0);
  }
}

void showStgObjRecPretty(Obj *p) {

  // depth check first
  if (depth+1 >= showDepthLimit) {
    fprintf(stderr, "******showStgObjRec depth exceeded\n");
    return;
  }

  InfoTab it = *(getInfoPtr(p));

  for (int i=0; i != depth; i++) {
    if (p == stack[i]) {
      fprintf(stderr, "((%s))", it.name);
      return;
    }
  }
  stack[depth++] = p;

  ObjType type = getObjType(p);
  if (type != BLACKHOLE &&
      type != INDIRECT &&
      type != it.objType) {
    if (!(type == PAP && it.objType == FUN)) {
      fprintf(stderr, "mismatch in infotab and object type! %d != %d\n",
	      type, it.objType);
      assert(false);
    }
  }
  if (strcmp(it.name, p->ident)) {
    if(type != PAP) {
      fprintf(stderr, "mismatch in infotab and object names \"%s\" != \"%s\"\n",
	      it.name, p->ident);
      assert(false);
    }
  }

  switch (type) {
  case FUN:
  case PAP:
  case THUNK:
  case BLACKHOLE:
    fprintf(stderr, "%s = <%s>", p->ident, objTypeNames[type]);
    break;

  case CON:
    fprintf(stderr, "%s = %s", p->ident, it.conFields.conName );
    int arity = it.conFields.arity;
    if (arity > 0) {
      if (arity > 1) fprintf(stderr, "(");
      else fprintf(stderr, " ");
      showStgValPretty(p->payload[0]);
      for (int i = 1; i < arity; i++) {
	    fprintf(stderr, ", ");
	    showStgValPretty(p->payload[i]);
      }
      if (arity > 1) fprintf(stderr, ")");
    }
    break;

  case INDIRECT:
    fprintf(stderr, "%s --> ", p->ident );
    showStgObjRecPretty(p->payload[0].op);
    break;

  default:
    fprintf(stderr,"********* default in showStgObj!\n");
    // exit(0);
  }
  depth--;
}

void showStgValPretty(PtrOrLiteral v) {
#if USE_ARGTYPE
  switch (v.argType) {
  case INT:
#ifdef __clang__
    fprintf(stderr,"%lld", v.i);
#else
    fprintf(stderr,"%ld", v.i);
#endif
    break;
  case DOUBLE:
    fprintf(stderr,"%f", v.d);
    break;
    //  case FLOAT:
    //    fprintf(stderr,"%f", v.f);
    //    break;
  case HEAPOBJ:
    showStgObjRecPretty(v.op);
    break;
  default:
    fprintf(stderr,"undefined PtrOrLiteral.tag! tag=%d\n", v.argType);
    exit(1);
  }
#endif
}


void showStgObjRecDebug(Obj *p) {

  // depth check first
  if (depth+1 >= showDepthLimit) {
    fprintf(stderr, "******showStgObjRec depth exceeded\n");
    return;
  }
  for (int i=0; i != depth; i++) {
    if (p == stack[i]) {
      fprintf(stderr, "   ***cycle\n");
      return;
    }
  }
  stack[depth++] = p;



  InfoTab it = *(getInfoPtr(p));
  ObjType type = getObjType(p);
  fprintf(stderr, "%s %s %s ", objTypeNames[type],
	  objTypeNames[it.objType], it.name);
  switch (type) {
  case FUN:
    fprintf(stderr,"\n");
    break;

  case PAP:
    fprintf(stderr,"\n");
    break;

  case CON:
    fprintf(stderr,"tag %d arity %d\n", it.conFields.tag, it.conFields.arity );
    for (int i = 0; i != it.conFields.arity; i++)
      showStgValDebug(p->payload[i]);
    break;

  case THUNK:
    fprintf(stderr,"\n");
    break;

  case BLACKHOLE:
    fprintf(stderr,"\n");
    break;

  case INDIRECT:
    fprintf(stderr,"INDIRECT to\n");
    showStgObjRecDebug(p->payload[0].op);
    break;

  default:
    fprintf(stderr,"default in showStgObj!\n");
    exit(1);
  }
  depth--;
}

void showStgValDebug(PtrOrLiteral v) {
#if USE_ARGTYPE
  switch (v.argType) {
  case INT:
#ifdef  __clang__
    fprintf(stderr,"INT %lld\n", v.i);
#else
    fprintf(stderr,"INT %ld\n", v.i);
#endif
    break;
  case DOUBLE:
    fprintf(stderr,"DOUBLE %f\n", v.d);
    break;
  case HEAPOBJ:
    fprintf(stderr,"HEAPOBJ %p ", v.op);
    showStgObjRecDebug(v.op);
    break;
  default:
    fprintf(stderr,"undefined PtrOrLiteral.tag!\n");
    exit(0);
  }
#endif
}


void checkStgObjRec(Obj *p) {
  size_t i;

  // depth check first
  if (depth + 1 >= showDepthLimit) {
    assert(false && "hc: checkStgObjRec depth exceeded\n");
  }

  assert((uintptr_t)p % 8 == 0 && "hc: bad Obj alignment");

  assert(isHeap(p) || isSHO(p) && "hc: bad Obj location");

  InfoTab *itp = getInfoPtr(p);
  assert((uintptr_t)itp % 8 == 0 && "hc: bad infoPtr alignment");
  InfoTab it = *itp;

  for (i = 0; i != depth; i++) {
    if (p == stack[i]) {
      return;
    }
  }
  stack[depth++] = p;

  ObjType type = getObjType(p);

  if (strcmp(it.name, p->ident)) {
    if (type != PAP) {
      fprintf(stderr, "mismatch in infotab and object names \"%s\" != \"%s\"\n",
          it.name, p->ident);
      assert(false);
    }
  }

  switch (type) {
  case FUN: {
    assert(it.objType == FUN && "hc: FUN infotab type mismatch");
    int FVCount = endFUNFVsU(p);
    if (FVCount) {
      // check that unboxed FVs really are unboxed
      for (i = startFUNFVsU(p); i < FVCount; i++) {
        assert(isUnboxed(p->payload[i]) && "hc: unexpected boxed FV in FUN");
      }
      // check that boxed FVs really are boxed
      for (i = startFUNFVsB(p); i < endFUNFVsB(p); i++) {
        assert(isBoxed(p->payload[i]) && "hc: unexpected unboxed FV in FUN");
        checkStgObjRec(p->payload[i].op);
      }
    }
    break;
  }

  case PAP: {
    assert(it.objType == FUN && "hc: PAP infotab type mismatch");
    int FVCount = endPAPFVsU(p);
    if (FVCount) {
      // check that unboxed FVs really are unboxed
      for (i = startPAPFVsU(p); i < FVCount; i++) {
        assert(isUnboxed(p->payload[i]) && "hc: unexpected boxed FV in PAP");
      }
      // check that boxed FVs really are nboxed
      for (i = startPAPFVsB(p); i < endPAPFVsB(p); i++) {
        assert(isBoxed(p->payload[i]) && "hc: unexpected unboxed FV in PAP");
        checkStgObjRec(p->payload[i].op);
      }
    }

    // check that unboxed args really are unboxed
    for (i = startPAPargsU(p); i < endPAPargsU(p); i++) {
      assert(isUnboxed(p->payload[i]) && "hc: unexpected boxed arg in PAP");
    }

    // check that boxed args really are boxed
    for (i = startPAPargsB(p); i < endPAPargsB(p); i++) {
      assert(isBoxed(p->payload[i]) && "hc: unexpected unboxed arg in PAP");
      checkStgObjRec(p->payload[i].op);
    }
    break;
  }
  case CON:
    assert(it.objType == CON && "hc: CON infotab type mismatch");
    checkCONargs(p);
    // check that unboxed args really are unboxed
    for (i = startCONargsU(p); i < endCONargsU(p); i++) {
      assert(isUnboxed(p->payload[i]) && "hc: unexpected boxed arg in CON");
    }
    // check that boxed args really are boxed
    for (i = startCONargsB(p); i < endCONargsB(p); i++) {
      assert(isBoxed(p->payload[i]) && "hc: unexpected unboxed arg in CON");
      checkStgObjRec(p->payload[i].op);
    }
    break;
  case THUNK:
    assert(it.objType == THUNK && "hc: THUNK infotab type mismatch");
    //fallthrough..
  case BLACKHOLE:
    // check that unboxed FVs really are unboxed
    for (i = startTHUNKFVsU(p); i < endTHUNKFVsU(p); i++) {
      assert(isUnboxed(p->payload[i]) && "hc: unexpected boxed arg in THUNK");
    }
    // check that boxed FVs really are boxed
    for (i = startTHUNKFVsB(p); i < endTHUNKFVsB(p); i++) {
        assert(isBoxed(p->payload[i]) && "hc: unexpected unboxed arg in THUNK");
        checkStgObjRec(p->payload[i].op);
    }
    break;

  case INDIRECT:
    assert(isBoxed(p->payload[0]) && "hc: unexpected unboxed arg in INDIRECT");
    checkStgObjRec(p->payload[0].op);
    break;

  default:
    assert(false && "hc: bad obj in checkStgObjRec");
  }
  depth--;
}

void checkStgObj(Obj *p) {
  depth = 0;
  checkStgObjRec(p);
}

void checkStgHeap() {
  for (char *p = (char*) stgHeap; p < (char*) stgHP; p += getObjSize((Obj *) p)) {
    checkStgObj((Obj *) p);
  }
}


size_t stgStatObjCount;
Obj * stgStatObj[100];

bool isSHO(Obj *p) {
  return (p >= stgStatObj[0] && p <= stgStatObj[stgStatObjCount-1]);
}

bool isHeap(Obj *p) {
  return (p >= (Obj *)stgHeap && p < (Obj *)stgHP);
}

void showStgStack() {
  fprintf(stderr,"\nSTG Stack:\n\n");
  for (char *p = (char*)stgSP;
       p < (char*)stgStack + stgStackSize;
       p += getObjSize((Obj *)p)) {
     showStgCont((Obj *)p);
   }
}

void showStgHeap() {
  return;
  fprintf(stderr,"\nSTG static objects: \n\n");
  for (int i = 0; i != stgStatObjCount; i++) {
    showStgObj(stgStatObj[i]);
    fprintf(stderr,"\n");
  }

  fprintf(stderr,"\nSTG heap:\n\n");
  for (char *p = (char*)stgHeap;
      p < (char*)stgHP;
      p += getObjSize((Obj *)p)) {
    showStgObj((Obj *)p);
  }

  showStgStack();
}

const size_t stgHeapSize  = (size_t)4*(size_t)(1024*1024*1024);
const size_t stgStackSize  = (size_t)4*(size_t)(1024*1024*1024);

void initStg() {
  stgHeap =
    mmap( NULL,                   // void *address, NULL => no preference
	  stgHeapSize,           // size_t length
	  PROT_READ | PROT_WRITE, // int protect, write may require read
	  MAP_PRIVATE | MAP_ANON, // int flags
	  -1,                     // int filedes
	  0 );                    // off_t offset

  if (stgHeap == MAP_FAILED) {
    fprintf(stderr,"mmap stg heap failed!!\n"); 
    exit(1);
  }

  stgHP = stgHeap; // first free address

  stgStack = 
    mmap( NULL,                   // void *address, NULL => no preference
	  stgStackSize,           // size_t length
	  PROT_READ | PROT_WRITE, // int protect, write may require read
	  MAP_PRIVATE | MAP_ANON,  // int flags
	  -1,                     // int filedes
	  0 );                    // off_t offset

  if (stgStack == MAP_FAILED) {
    fprintf(stderr,"mmap stg stack failed!!\n"); 
    exit(1);
  }
  
  stgSP = (char *)stgStack + stgStackSize;

  fprintf(stderr,"Stg stack at %p and heap at %p\n", stgStack, stgHP);

  stgStatObjCount = 0;

}

