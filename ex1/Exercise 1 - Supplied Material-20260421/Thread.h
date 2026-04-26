#ifndef THREAD_H
#define THREAD_H
#include <setjmp.h>
enum State{RUNNING,READY,BLOCKED};
struct Thread {
    int tid;
    State state;
    sigjmp_buf env;
    char* stack;
    int quantums;

};

#endif