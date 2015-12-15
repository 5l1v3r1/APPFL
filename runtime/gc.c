#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "stg.h"
#include "stgutils.h"
#include "obj.h"
#include "options.h"

const bool DEBUG = true;
const bool EXTRA = false;  // run extra checks

static void *toPtr, *fromPtr;
static void *scanPtr, *freePtr;

static inline bool isFrom(void *p) {
  return (p >= fromPtr && (char *) p < (char *) fromPtr + stgHeapSize / 2);
}

static inline bool isTo(void *p) {
  return (p >= toPtr && (char *) p < (char *) toPtr + stgHeapSize / 2);
}

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

PtrOrLiteral updatePtrByValue (PtrOrLiteral f) {
  Obj *p = derefPoL(f);

  if (isFrom(p)) {
    if (isLSBset(p->infoPtr)) {
      if (DEBUG) fprintf(stderr, "update forward %s\n", p->ident);
      f.op = (Obj *) getInfoPtr(p);
    } else {
      int size = getObjSize(p);
      if (DEBUG) {
        fprintf(stderr, "copy %s %s from->to size=%d\n", objTypeNames[getObjType(p)], p->ident, size);
      }

      memcpy(freePtr, p, size);
      if (EXTRA) assert(isLSBset((InfoTab *)freePtr) == 0 && "gc: bad alignment");

      p->infoPtr = setLSB((InfoTab *)freePtr);
      f.op = (Obj *) freePtr;
      freePtr = (char *) freePtr + size;
    }
  } else if (isTo(p)) {
    // do nothing
  } else { // SHO
    if (isFrom(f.op) && getObjType(f.op) == INDIRECT) {
        if (DEBUG) fprintf(stderr, "fix INDIRECT to sho %s\n", f.op->ident);
        f.op = p;
    }
  }
  return f;
}

void updatePtr(PtrOrLiteral *f) {
  *f = updatePtrByValue(*f);
}

/*
void updatePtr(PtrOrLiteral *f) {
  Obj *p = derefPoL(*f);

  if (isFrom(p)) {
    if (isLSBset(p->infoPtr)) {
      if (DEBUG) fprintf(stderr, "update forward %s\n", p->ident);
      f->op = (Obj *) getInfoPtr(p);
    } else {
      int size = getObjSize(p);
      if (DEBUG) {
        fprintf(stderr, "copy %s %s from->to size=%d\n", objTypeNames[getObjType(p)], p->ident, size);
      }

      memcpy(freePtr, p, size);
      if (EXTRA) assert(isLSBset((uintptr_t)freePtr) == 0 && "gc: bad alignment");

      p->infoPtr = setLSB((uintptr_t)freePtr);
      f->op = (Obj *) freePtr;
      freePtr = (char *) freePtr + size;
    }
  } else if (isTo(p)) {
    // do nothing
  } else { // SHO
    if (isFrom(f->op) && getObjType(f->op) == INDIRECT) {
        if (DEBUG) fprintf(stderr, "fix INDIRECT to sho %s\n", f->op->ident);
        f->op = p;
    }
  }
}
*/

void processObj(Obj *p) {
  size_t i;
  if (DEBUG) fprintf(stderr, "processObj %s %s\n", objTypeNames[getObjType(p)], p->ident);

  switch (getObjType(p)) {
  case FUN: {
    int start = startFUNFVsB(p);
    int end = endFUNFVsB(p);
    // process boxed freevars
    for (i = start; i < end; i++) {
      updatePtr(&p->payload[i]);
    }
    break;
  }

  case PAP: {
    if (endPAPFVsU(p)) {
      // boxed free vars
      int start = startPAPFVsB(p);
      int end = endPAPFVsB(p);
      for (i = start; i < end; i++) {
        updatePtr(&p->payload[i]);
      }
    }
    /* mkd gc
    // boxed args already applied
    int start = startPAPargsB(p);
    int end = endPAPargsB(p);
    for (i = start; i < end; i++) {
      updatePtr(&p->payload[i]);
    }
    */
    Bitmap64 bm = p->payload[endPAPFVsU(p)].b;
    uint64_t mask = bm.bitmap.mask;
    int i = endPAPFVsU(p) + 1;
    for (int size = bm.bitmap.size; size != 0; size--, i++, mask >>= 1) {
      if (mask & 0x1UL) updatePtr(&p->payload[i]);
    } /* mkd gc */
    break;
  }

  case CON: {
    // boxed args
    int start = startCONargsB(p);
    int end = endCONargsB(p);
    for (i = start; i < end; i++) {
      updatePtr(&p->payload[i]);
    }
    break;
  }

  case THUNK:
  case BLACKHOLE: {
    int start = startTHUNKFVsB(p);
    int end = endTHUNKFVsB(p);
    for (i = start; i < end; i++) {
      updatePtr(&p->payload[i]);
    }
    break;
  }

  case INDIRECT:
    updatePtr(&p->payload[0]);
    break;
  default:
    fprintf(stderr, "gc: bad obj. type %d %s", getObjType(p),
        objTypeNames[getObjType(p)]);
    assert(false);
  }
}

void processCont(Cont *p) {
  size_t i;
  if (DEBUG) fprintf(stderr, "processCont %s %s\n", contTypeNames[getContType(p)], p->ident);
  switch (getContType(p)) {
  case UPDCONT:
    updatePtr(&p->payload[0]);
    break;
  case CASECONT: {
    if (EXTRA) {
      for (i = startCASEFVsU(p); i < endCASEFVsU(p); i++) {
        assert(isUnboxed(p->payload[i]) && "gc: unexpected boxed arg in CASE");
      }
    }

    int start = startCASEFVsB(p);
    int end = endCASEFVsB(p);
    for (i = start; i < end; i++) {
      if (EXTRA) assert(isBoxed(p->payload[i]) && "gc: unexpected unboxed arg in CASE");
      updatePtr(&p->payload[i]);
    }
    break;
  }
  case CALLCONT: {
    /*
    int start = startCALLFVsB(p);
    int end = endCALLFVsB(p);
    for (i = start; i < end; i++) {
      if (EXTRA) assert(isBoxed(p->payload[i]) && 
			"gc: unexpected unboxed arg in CALL");
      updatePtr(&p->payload[i]);
    }
    */
    Bitmap64 bm = p->layout;
    uint64_t mask = bm.bitmap.mask;
    int i = 0;
    for (int size = bm.bitmap.size; size != 0; size--, i++, mask >>= 1) {
      if (EXTRA) {
	if (mask & 0x1UL)
	  assert(isBoxed(p->payload[i]) && "gc: unexpected unboxed arg in CALLCONT");
	else
	  assert(!isBoxed(p->payload[i]) && "gc: unexpected boxed arg in CALLCONT");
      }
      if (mask & 0x1UL) updatePtr(&p->payload[i]);
    } /* mkd gc */

    break;
  }
  case FUNCONT:
    if (EXTRA) assert(isBoxed(p->payload[0]) && 
		      "gc: unexpected unboxed arg in FUN");
    updatePtr(&p->payload[0]);
    break;
  default:
    fprintf(stderr, "gc: bad cont. type %d %s\n", getContType(p),
        contTypeNames[getContType(p)]);
    assert(false);
  }
}

void gc(void) {

  // fprintf(stderr, "GARBAGE COLLECTION DISABLED in gc.c/gc(void)\n"); return;

  size_t before = stgHP - stgHeap;

  if (EXTRA) checkStgHeap();

  if (DEBUG) {
    fprintf(stderr, "old heap\n");
    showStgHeap();
    fprintf(stderr, "start gc heap size %lx\n", before);
  }

  // add stgCurVal
  if (EXTRA) assert(isBoxed(stgCurVal) && "gc: unexpected unboxed arg in stgCurVal");
  processObj(stgCurVal.op);

  // all SHO's
  for (int i = 0; i < stgStatObjCount; i++) {
    processObj(stgStatObj[i]);
  }
  //Cont. stack
  for (Cont *p = (Cont *)stgSP; 
       (char *)p < (char*) stgStack + stgStackSize; 
       p = (Cont *)((char*)p + getContSize(p))) {
    processCont(p);
  }

  //all roots are now added.

  //update stgCurVal
  if (EXTRA ) assert(isBoxed(stgCurVal) && "gc: unexpected unboxed arg in stgCurVal");
  stgCurVal = updatePtrByValue(stgCurVal);

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

