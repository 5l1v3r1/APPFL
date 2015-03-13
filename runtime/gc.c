#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "stg.h"
#include "stgutils.h"


const float gcThreshold=0.0;

void *toPtr=NULL, *fromPtr=NULL;
void *scanPtr=NULL, *freePtr=NULL;

// wrapper functions
static inline size_t sizeofObj(Obj *p) {
  return sizeof(Obj);
}



// end of wrappers

void initGc(void) {
  assert(stgHeap && "heap not defined"); 
  fromPtr = stgHeap;
  toPtr = (char *)stgHeap + stgHeapSize/2;
  scanPtr = toPtr;
  freePtr = toPtr;
}

void swapPtrs(void) {
  assert( scanPtr == freePtr && "swapPtrs called when gc not finished");
  stgHeap = toPtr;
  stgHP = freePtr;
  toPtr = fromPtr;
  fromPtr = stgHP;
}

static inline bool isFrom(void *p) {
  return (p >= fromPtr && (char *)p < (char *)fromPtr + stgHeapSize/2); 
}

void updatePtr(PtrOrLiteral f) {
  Obj *p = derefPoL(f);
	if (isFrom(p)) {
		if(p->objType == FORWARD) {
			p = p->payload[0].op;
		} else {
            fprintf(stderr, "copy a %s from->to\n", objTypeNames[p->objType]); 
			memcpy(freePtr, p, sizeofObj(p));
			p->objType = FORWARD;
			p->payload[0].op = freePtr;
			p = freePtr;
			freePtr = (char *)freePtr + sizeofObj(p);
		}
	}
}

void processObj(Obj *p) {
  size_t i;
  Obj o = *p;
  InfoTab it = *o.infoPtr;
  switch(o.objType) {
  case FUN:
    // freevars
    for(i=0; i<it.fvCount; i++) {
      updatePtr(o.payload[i]);
    }    
    break;
  case PAP:
    // free vars
    for(i=0; i<it.fvCount; i++) {
      updatePtr(o.payload[i]);
    }
    // args already applied    
    for(i=it.fvCount; i<o.argCount; i++) {
      if (o.payload[i].argType == HEAPOBJ) {
         updatePtr(o.payload[i]);
      }
    }
    break;
  case CON:
    for(i=0; i<it.conFields.argCount; i++) {
      if (o.payload[i].argType == HEAPOBJ) {
        updatePtr(o.payload[i]);
      }
    }
    break;
  case THUNK:
  case BLACKHOLE:
    for(i=0; i<it.fvCount; i++) {
      updatePtr(o.payload[i]);
    }
    break;
  case INDIRECT:
    updatePtr(o.payload[0]);
    break;
  case UPDCONT:
    updatePtr(o.payload[0]);
    break;
  case CASECONT:
    for(i=0; i<it.fvCount; i++) {
      updatePtr(o.payload[i]);
    }
    break;
  case CALLCONT:
    for(i=1; i<=o.payload[0].i; i++) {
      updatePtr(o.payload[i]);
    }
    break;
  case FUNCONT:
    updatePtr(o.payload[0]);
    break;
  default:
    assert (false && "bad obj type");
  }
}


void gc(void) {

  if((char*)stgHP - (char *)stgHeap < gcThreshold*stgHeapSize) {
    return;
  }

  fprintf(stderr,"start gc\n");

  // all SHO's
  for(int i=0; i<stgStatObjCount; i++) {
    processObj(stgStatObj[i]);
  }

  //Cont. stack
  void *p = stgSP;
  while((char *)p < (char *)stgStack + stgStackSize) {
    processObj(p);
    p = (char *)p + sizeofObj(p);
  }
  //all roots are now added.

  // process to space
   while(scanPtr < freePtr) {
     processObj(scanPtr);
     scanPtr = (char *)scanPtr + sizeofObj(scanPtr);
   }

   swapPtrs();
   fprintf(stderr,"end gc\n");
}
