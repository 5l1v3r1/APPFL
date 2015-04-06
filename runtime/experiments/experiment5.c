// using the C call stack for calls
// inlining the dispatcher


#include <stdio.h>

//------ fake typedef fp (*fp)()

typedef void (*vvfp)();
typedef vvfp (*FnPtr)();

typedef enum {
  CmmReturn,
  CmmJump
} CmmReturnKind;

typedef struct {
  CmmReturnKind retKind;
  FnPtr fp;
} CmmRetVal;

typedef CmmRetVal (*CmmFnPtr)();
void cmmCall(CmmFnPtr f);

#define RETURN0()				\
  do {						\
    CmmRetVal rv;				\
    rv.retKind = CmmReturn;			\
    return rv;					\
  } while (0)

#define JUMP0(f)				\
  do {						\
    CmmRetVal rv;				\
    rv.retKind = CmmJump;			\
    rv.fp = (FnPtr)&f;				\
    return rv;					\
  } while (0)

#define CALL0orig(f)				\
  do {						\
    CmmRetVal rv;				\
    rv = ((CmmFnPtr)(*f))();			\
    while (rv.retKind == CmmJump) 		\
      rv = ((CmmFnPtr)(*rv.fp))();		\
  } while (0)

#define CALL0(f)				\
  do {						\
    CmmRetVal rv = {.fp = (FnPtr)f};				\
    while ((rv = ((CmmFnPtr)(*rv.fp))()).retKind == CmmJump);	\
  } while (0)


CmmRetVal f();
CmmRetVal g();
CmmRetVal h();
CmmRetVal q();
CmmRetVal r();

CmmRetVal f() {
  printf("f CALLing g\n");
  CALL0(g);
  printf("f back from...somewhere\n");
  RETURN0();
}

CmmRetVal g() {
  printf("g JUMPing to h\n");
  JUMP0(h);
  printf("ERROR g back from h!\n");
  RETURN0();
}

CmmRetVal h() {
  printf("h JUMPing to q\n");
  JUMP0(q);
  printf("ERROR h back from q!\n");
  RETURN0();
}

CmmRetVal q() {
  printf("q calling r\n");
  CALL0(r);
  printf("q back from r\n");
  RETURN0();
}

CmmRetVal r() {
  printf("r here\n");
  RETURN0();
}
  
int main (int argc, char **argv) {
  printf("running mini-interpreter\n");

  //cmmCall((CmmFnPtr)f);
  CALL0(f);

  printf("exiting mini-interpreter\n");
  return 0;
}