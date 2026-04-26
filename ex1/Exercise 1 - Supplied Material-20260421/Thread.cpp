#include <setjmp.h>
enum State{RUNNING,READY,BLOCKED};
struct Thread {
    int tid;
    State State;
    sigjmp_buf env;
    char* stack;
    int quantums;

};
