#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "stg.h"
#include "stgutils.h"

const bool DEBUG = false;
const bool EXTRA = true;  // run extra checks

void *toPtr = NULL, *fromPtr = NULL;
void *scanPtr = NULL, *freePtr = NULL;

// wrapper functions for possible interface changes
// see README-heapobj.txt
static inline size_t startFUNFVsB(Obj *p) { return 0; }
static inline size_t endFUNFVsB(Obj *p) { return p->infoPtr->layoutInfo.boxedCount; }
static inline size_t startFUNFVsU(Obj *p) { return endFUNFVsB(p); }
static inline size_t endFUNFVsU(Obj *p) { return startFUNFVsU(p) + p->infoPtr->layoutInfo.unboxedCount; }

static inline size_t startPAPFVsB(Obj *p) { return 0; }
static inline size_t endPAPFVsB(Obj *p) { return p->infoPtr->layoutInfo.boxedCount; }
static inline size_t startPAPFVsU(Obj *p) { return endPAPFVsB(p); }
static inline size_t endPAPFVsU(Obj *p) { return startPAPFVsU(p) + p->infoPtr->layoutInfo.unboxedCount; }
static inline size_t startPAPargsB(Obj *p) { return endPAPFVsU(p) + 1; }
static inline size_t endPAPargsB(Obj *p) { return startPAPargsB(p) + PUNPACK(p->payload[endPAPFVsU(p)].i); }
static inline size_t startPAPargsU(Obj *p) { return endPAPargsB(p); }
static inline size_t endPAPargsU(Obj *p) { return startPAPargsU(p) + NUNPACK(p->payload[endPAPFVsU(p)].i); }

static inline size_t startCONargsB(Obj *p) { return 0; }
static inline size_t endCONargsB(Obj *p) { return p->infoPtr->layoutInfo.boxedCount; }
static inline size_t startCONargsU(Obj *p) { return endCONargsB(p); }
static inline size_t endCONargsU(Obj *p) { return startCONargsU(p) + p->infoPtr->layoutInfo.unboxedCount; }
static inline void checkCONargs(Obj *p) {
  assert(
      !((uintptr_t) p->infoPtr & (uintptr_t) 1)
          && "...odd infoPtr checkCONargs");
  assert(
      p->infoPtr->conFields.arity >= 0 && p->infoPtr->conFields.arity <= 10
          && "gc:  unlikely number of constructor arguments");
  assert(
      p->infoPtr->conFields.arity == endCONargsU(p) && "gc: CON args mismatch");
}

// payload[0] of thunk is result field
static inline size_t startTHUNKFVsB(Obj *p) { return 1; }
static inline size_t endTHUNKFVsB(Obj *p) { return p->infoPtr->layoutInfo.boxedCount + 1; }
static inline size_t startTHUNKFVsU(Obj *p) { return endCONargsB(p); }
static inline size_t endTHUNKFVsU(Obj *p) { return startCONargsU(p) + p->infoPtr->layoutInfo.unboxedCount; }

static inline size_t startCALLFVsB(Obj *p) { return 1; }
static inline size_t endCALLFVsB(Obj *p) { return p->payload[0].i + 1; }

static inline size_t startCASEFVsB(Obj *p) { return 0; }
static inline size_t endCASEFVsB(Obj *p) { return p->infoPtr->layoutInfo.boxedCount; }
static inline size_t startCASEFVsU(Obj *p) { return endCASEFVsB(p); }
static inline size_t endCASEFVsU(Obj *p) { return startCASEFVsU(p) + p->infoPtr->layoutInfo.unboxedCount; }

static inline bool isFrom(void *p) {
  return (p >= fromPtr && (char *) p < (char *) fromPtr + stgHeapSize / 2);
}

static inline bool isTo(void *p) {
  return (p >= toPtr && (char *) p < (char *) toPtr + stgHeapSize / 2);
}

static inline bool isBoxed(PtrOrLiteral *f) { return (f->argType == HEAPOBJ); }

// use LSB to say it is a FORWARD
static inline uintptr_t setLSB(void *ptr) { return (uintptr_t) ptr | 1; }
static inline uintptr_t unsetLSB(void *ptr) { return (uintptr_t) ptr & ~1; }
static inline uintptr_t isLSBset(void *ptr) { return (uintptr_t) ptr & 1; }

// end of wrappers

void initGc(void) {
  assert(stgHeap && "gc: heap not defined");
  fromPtr = stgHeap;
  toPtr = (char *) stgHeap + stgHeapSize / 2;
  scanPtr = toPtr;
  freePtr = toPtr;
}

void swapPtrs(void) {
  assert(scanPtr == freePtr && "gc: not finished");
  size_t before = stgHP - stgHeap;
  stgHeap = toPtr;
  stgHP = freePtr;
  toPtr = fromPtr;
  fromPtr = stgHeap;
  freePtr = toPtr;
  scanPtr = toPtr;
  assert(stgHP - stgHeap <= before && "gc: increased heap size!\n");
}

void updatePtr(PtrOrLiteral *f) {
  Obj *p = derefPoL(*f);

  if (isFrom(p)) {
    if (isLSBset(p->infoPtr)) {
      if (DEBUG) fprintf(stderr, "update forward %s\n", p->ident);
      f->op = (Obj *) unsetLSB(p->infoPtr);
    } else {
      int size = getObjSize(p);
      if (DEBUG) {
        fprintf(stderr, "copy %s %s from->to size=%d\n", objTypeNames[p->objType], p->ident, size);
      }

      memcpy(freePtr, p, size);
      if (EXTRA) assert(isLSBset(freePtr) == 0 && "gc: bad alignment");

      p->infoPtr = (InfoTab *) setLSB(freePtr);
      f->op = (Obj *) freePtr;
      freePtr = (char *) freePtr + size;
    }
  } else if (isTo(p)) {
    // do nothing
  } else { // SHO
    if (f->op->objType == INDIRECT) {
      if (isFrom(f->op)) {
        if (DEBUG) fprintf(stderr, "fix INDIRECT to sho %s\n", f->op->ident);
        f->op = p;
      }
    }
  }
}

void processObj(Obj *p) {
  size_t i;
  if (DEBUG) fprintf(stderr, "processObj %s %s\n", objTypeNames[p->objType], p->ident);

  switch (p->objType) {
  case FUN:
    // process boxed freevars
    for (i = startFUNFVsB(p); i < endFUNFVsB(p); i++) {
      updatePtr(&p->payload[i]);
    }
    break;

  case PAP:
    if (endPAPFVsU(p)) {
      // boxed free vars
      for (i = startPAPFVsB(p); i < endPAPFVsB(p); i++) {
        updatePtr(&p->payload[i]);
      }
    }

    // boxed args already applied
    for (i = startPAPargsB(p); i < endPAPargsB(p); i++) {
      updatePtr(&p->payload[i]);
    }
    break;

  case CON:
    // boxed args
    for (i = startCONargsB(p); i < endCONargsB(p); i++) {
      updatePtr(&p->payload[i]);
    }
    break;

  case THUNK:
  case BLACKHOLE:
    for (i = startTHUNKFVsB(p); i < endTHUNKFVsB(p); i++) {
      updatePtr(&p->payload[i]);
    }
    break;

  case INDIRECT:
    updatePtr(&p->payload[0]);
    break;
  default:
    fprintf(stderr, "gc: bad obj. type %d %s", p->objType,
        objTypeNames[p->objType]);
    assert(false);
  }
}

void processCont(Obj *p) {
  size_t i;
  if (DEBUG) fprintf(stderr, "processCont %s %s\n", objTypeNames[p->objType], p->ident);
  switch (p->objType) {
  case UPDCONT:
    updatePtr(&p->payload[0]);
    break;
  case CASECONT:
    if (EXTRA) {
      for (i = startCASEFVsU(p); i < endCASEFVsU(p); i++) {
        assert(!isBoxed(&p->payload[i]) && "gc: unexpected boxed arg in CASE");
      }
    }

    for (i = startCASEFVsB(p); i < endCASEFVsB(p); i++) {
      if (EXTRA) assert(isBoxed(&p->payload[i]) && "gc: unexpected unboxed arg in CASE");
      updatePtr(&p->payload[i]);
    }
    break;
  case CALLCONT:
    for (i = startCALLFVsB(p); i < endCALLFVsB(p); i++) {
      if (EXTRA) assert(isBoxed(&p->payload[i]) && "gc: unexpected unboxed arg in CALL");
      updatePtr(&p->payload[i]);
    }
    break;
  case FUNCONT:
    if (EXTRA) assert(isBoxed(&p->payload[0]) && "gc: unexpected unboxed arg in FUN");
    updatePtr(&p->payload[0]);
    break;
  default:
    fprintf(stderr, "gc: bad cont. type %d %s\n", p->objType,
        objTypeNames[p->objType]);
    assert(false);
  }
}

void gc(void) {

  size_t before = stgHP - stgHeap;

  if (EXTRA) checkStgHeap();

  if (DEBUG) {
    fprintf(stderr, "old heap\n");
    showStgHeap();
    fprintf(stderr, "start gc heap size %lx\n", before);
  }

  // add stgCurVal
  if (stgCurVal.argType == HEAPOBJ) {
    processObj(stgCurVal.op);
  }

  // all SHO's
  for (int i = 0; i < stgStatObjCount; i++) {
    processObj(stgStatObj[i]);
  }

  //Cont. stack
  for (char *p = (char*) stgSP; p < (char*) stgStack + stgStackSize; p +=
      getObjSize((Obj *) p)) {
    processCont((Obj *) p);
  }

  //all roots are now added.

  //update stgCurVal
  if (EXTRA) assert(isBoxed(&stgCurVal) && "gc: unexpected unboxed arg in stgCurVal");
  updatePtr(&stgCurVal);

  // process "to" space
  while (scanPtr < freePtr) {
    processObj(scanPtr);
    scanPtr = (char *) scanPtr + getObjSize(scanPtr);
  }

  swapPtrs();

  if (EXTRA) checkStgHeap();
  if (DEBUG) {
    fprintf(stderr, "new heap\n");
    showStgHeap();
    size_t after = stgHP - stgHeap;
    fprintf(stderr, "end gc heap size %lx (change %lx)\n", after, before - after);
  }
}

// heap checker
static const int showDepthLimit = 1000;
static int depth;
static Obj *stack[1000];
static int stackp;

void checkStgObjRec(Obj *p) {
  size_t i;

  // depth check first
  if (depth + 1 >= showDepthLimit) {
    assert(false && "hc: checkStgObjRec depth exceeded\n");
  }
  assert((uintptr_t)p % 8 == 0 && "hc: bad Obj alignment");

  InfoTab it = *(p->infoPtr);
  assert((uintptr_t)(p->infoPtr) % 8 == 0 && "hc: bad infoPtr alignment");

  for (i = 0; i != depth; i++) {
    if (p == stack[i]) {
      return;
    }
  }
  stack[depth++] = p;

  if (strcmp(it.name, p->ident)) {
    if (p->objType != PAP) {
      fprintf(stderr, "mismatch in infotab and object names \"%s\" != \"%s\"\n",
          it.name, p->ident);
      assert(false);
    }
  }

  switch (p->objType) {
  case FUN: {
    assert(it.objType == FUN && "hc: FUN infotab type mismatch");
    int FVCount = endFUNFVsU(p);
    if (FVCount) {
      // check that unboxed FVs really are unboxed
      for (i = startFUNFVsU(p); i < FVCount; i++) {
        assert(!isBoxed(&p->payload[i]) && "hc: unexpected boxed FV in FUN");
      }
      // check that boxed FVs really are boxed
      for (i = startFUNFVsB(p); i < endFUNFVsB(p); i++) {
        assert(isBoxed(&p->payload[i]) && "hc: unexpected unboxed FV in FUN");
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
        assert(!isBoxed(&p->payload[i]) && "hc: unexpected boxed FV in PAP");
      }
      // check that boxed FVs really are nboxed
      for (i = startPAPFVsB(p); i < endPAPFVsB(p); i++) {
        assert(isBoxed(&p->payload[i]) && "hc: unexpected unboxed FV in PAP");
        checkStgObjRec(p->payload[i].op);
      }
    }

    // check that unboxed args really are unboxed
    for (i = startPAPargsU(p); i < endPAPargsU(p); i++) {
      assert(!isBoxed(&p->payload[i]) && "hc: unexpected boxed arg in PAP");
    }

    // check that boxed args really are boxed
    for (i = startPAPargsB(p); i < endPAPargsB(p); i++) {
      assert(isBoxed(&p->payload[i]) && "hc: unexpected unboxed arg in PAP");
      checkStgObjRec(p->payload[i].op);
    }
    break;
  }
  case CON:
    assert(it.objType == CON && "hc: CON infotab type mismatch");
    checkCONargs(p);
    // check that unboxed args really are unboxed
    for (i = startCONargsU(p); i < endCONargsU(p); i++) {
      assert(!isBoxed(&p->payload[i]) && "hc: unexpected boxed arg in CON");
    }
    // check that boxed args really are boxed
    for (i = startCONargsB(p); i < endCONargsB(p); i++) {
      assert(isBoxed(&p->payload[i]) && "hc: unexpected unboxed arg in CON");
      checkStgObjRec(p->payload[i].op);
    }
    break;
  case THUNK:
    assert(it.objType == THUNK && "hc: THUNK infotab type mismatch");
    //fallthrough..
  case BLACKHOLE:
    // check that unboxed FVs really are unboxed
    for (i = startTHUNKFVsU(p); i < endTHUNKFVsU(p); i++) {
      assert(!isBoxed(&p->payload[i]) && "hc: unexpected boxed arg in THUNK");
    }
    // check that boxed FVs really are boxed
    for (i = startTHUNKFVsB(p); i < endTHUNKFVsB(p); i++) {
        assert(isBoxed(&p->payload[i]) && "hc: unexpected unboxed arg in THUNK");
        checkStgObjRec(p->payload[i].op);
    }
    break;


  case INDIRECT:
    assert(isBoxed(&p->payload[0]) && "hc: unexpected unboxed arg in INDIRECT");
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

