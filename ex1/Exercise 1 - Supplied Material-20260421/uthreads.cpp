#include "uthreads.h"
#include "IdAllocator.h"
#include "Thread.h"
#include <iostream>
#include <new>
#include <utility>
#include <algorithm>
#include <signal.h>
#include <sys/time.h>
typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

static int running_thread_id;
IdAllocator allocator;
static std::deque<int> ready;
static Thread* threads[MAX_THREAD_NUM];
static int total_quantums = 0;
static struct itimerval timer_config;
// Stack of a self-terminated thread, freed on the next context switch (when we
// are no longer running on it).
static char* pending_stack_free = nullptr;
enum SwitchReason {YIELDING, TERMINATED, BLOCKING, SLEEPING,PREEMPTED};
static sigset_t vtalrm_set;

static void block_timer() {
    sigprocmask(SIG_BLOCK, &vtalrm_set, NULL);
}

static void unblock_timer() {
    sigprocmask(SIG_UNBLOCK, &vtalrm_set, NULL);
}

address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
        "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

static void switch_threads(SwitchReason reason) {
    if (pending_stack_free != nullptr) {
        delete[] pending_stack_free;
        pending_stack_free = nullptr;
    }
    Thread* outgoing = threads[running_thread_id];

    if (reason != TERMINATED) {
        int ret = sigsetjmp(outgoing->env, 1);
        if (ret != 0) {
            unblock_timer();
            return;
        }
    }

    switch (reason) {
        case YIELDING:
            outgoing->state = READY;
            ready.push_back(outgoing->tid);
            break;

        case BLOCKING:
            outgoing->state = State::BLOCKED;
            break;

        case SLEEPING:
            outgoing->state = State::BLOCKED;
            break;

        case TERMINATED:
            pending_stack_free = outgoing->stack;
            delete outgoing;
            threads[running_thread_id] = nullptr;
            allocator.delete_id(running_thread_id);
            break;

        case PREEMPTED:
            outgoing->state = READY;
            ready.push_back(outgoing->tid);
            break;

        default:
            break;
    }

    if (ready.empty()) {
        std::cerr << "system error: ready queue empty in scheduler\n";
        exit(1);
    }
    int incoming_tid = ready.front();
    ready.pop_front();

    Thread* incoming = threads[incoming_tid];
    incoming->state = RUNNING;
    incoming->quantums += 1;
    total_quantums += 1;
    running_thread_id = incoming_tid;
    for (int i = 0; i < MAX_THREAD_NUM; ++i) {
        Thread* t = threads[i];
        if (t == nullptr) {
            continue;
        }
        if (t->wake_up_time != 0 && total_quantums >= t->wake_up_time) {
            t->wake_up_time = 0;

            if (!t->is_blocked) {
                t->state = READY;
                ready.push_back(i);
            }
        }
    }
    if (setitimer(ITIMER_VIRTUAL, &timer_config, NULL) < 0) {
        std::cerr << "system error: setitimer failed\n";
        exit(1);
    }
    siglongjmp(incoming->env, 1);
}


static void timer_handler(int sig) {
    switch_threads(PREEMPTED);
}

/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        std::cerr << "thread library error: non-positive quantum\n";
        return -1;
    }

    int main_tid = allocator.next_id();
    total_quantums = 1;

    Thread* main_thread = new (std::nothrow) Thread();
    if (main_thread == nullptr) {
        std::cerr << "system error: memory allocation failed\n";
        exit(1);
    }
    main_thread->tid = main_tid;
    main_thread->state = RUNNING;
    main_thread->stack = nullptr;
    main_thread->quantums = 1;

    threads[main_tid] = main_thread;
    running_thread_id = main_tid;

    sigemptyset(&vtalrm_set);
    sigaddset(&vtalrm_set, SIGVTALRM);
    struct sigaction sa = {0};
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << "system error: sigaction failed\n";
        exit(1);
    }


    timer_config.it_value.tv_sec  = quantum_usecs / 1000000;
    timer_config.it_value.tv_usec = quantum_usecs % 1000000;
    timer_config.it_interval.tv_sec  = quantum_usecs / 1000000;
    timer_config.it_interval.tv_usec = quantum_usecs % 1000000;

    if (setitimer(ITIMER_VIRTUAL, &timer_config, NULL) < 0) {
        std::cerr << "system error: setitimer failed\n";
        exit(1);
    }

    return 0;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/

int uthread_spawn(thread_entry_point entry_point) {
    block_timer();
    if (entry_point == nullptr) {
        std::cerr << "thread library error: entry point is null pointer\n";
        unblock_timer();
        return -1;
    }

    int next_id = allocator.next_id();
    if (next_id >= MAX_THREAD_NUM) {
        allocator.delete_id(next_id);
        std::cerr << "thread library error: max threads reached\n";
        unblock_timer();
        return -1;
    }

    char* stack = new (std::nothrow) char[STACK_SIZE];
    if (stack == nullptr) {
        std::cerr << "system error: memory allocation failed\n";
        exit(1);
    }

    Thread* t = new (std::nothrow) Thread();
    if (t == nullptr) {
        delete[] stack;
        std::cerr << "system error: memory allocation failed\n";
        exit(1);
    }
    t->tid = next_id;
    t->state = READY;
    t->stack = stack;
    t->quantums = 0;

    address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t)entry_point;
    sigsetjmp(t->env, 1);
    (t->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (t->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&t->env->__saved_mask);

    threads[next_id] = t;
    ready.push_back(next_id);

    unblock_timer();
    return next_id;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/

int uthread_terminate(int tid) {
    block_timer();
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << "thread library error: terminate on invalid tid\n";
        unblock_timer();
        return -1;
    }

    if (tid == 0) {
        if (pending_stack_free != nullptr) {
            delete[] pending_stack_free;
            pending_stack_free = nullptr;
        }
        for (int i = 0; i < MAX_THREAD_NUM; ++i) {
            if (threads[i] != nullptr) {
                delete[] threads[i]->stack;
                delete threads[i];
                threads[i] = nullptr;
            }
        }
        ready.clear();
        exit(0);
    }

    if (tid == running_thread_id) {
        switch_threads(TERMINATED);
        return 0;
    }

    auto it = std::find(ready.begin(), ready.end(), tid);
    if (it != ready.end()) {
        ready.erase(it);
    }

    delete[] threads[tid]->stack;
    delete threads[tid];
    threads[tid] = nullptr;
    allocator.delete_id(tid);
    unblock_timer();
    return 0;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is *not* considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid) {
    block_timer();
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << "thread library error: block on invalid tid\n";
        unblock_timer();
        return -1;
    }
    if (tid==0) {
        std::cerr << "thread library error: cannot block main thread\n";
        unblock_timer();
        return -1;
    }
    if (tid == running_thread_id) {
        threads[tid]->is_blocked = true;
        switch_threads(BLOCKING);
        return 0;
    }
    Thread* t = threads[tid];
    if (t->is_blocked) {
        unblock_timer();
        return 0;
          }
    auto it = std::find(ready.begin(), ready.end(), tid);
    if (it != ready.end()) {
        ready.erase(it);
     }
    t->state = BLOCKED;
    t->is_blocked = true;
    unblock_timer();
    return 0;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * When a thread transition to the READY state it is placed at the end of the READY queue.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
    block_timer();
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << "thread library error: resume on invalid tid\n";
        unblock_timer();
        return -1;
    }
    Thread* t = threads[tid];
    if (!t->is_blocked) {
        unblock_timer();
        return 0;
    }
    t->is_blocked = false;
    if (t->wake_up_time == 0) {
        t->state = READY;
        ready.push_back(tid);
    }
    unblock_timer();
    return 0;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * A call with num_quantums == 0 will immediately stop the thread and move it to the back of the execution queue.
 *
 * It is considered an error if the main thread (tid == 0) calls this function with num_quantums != 0.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
    block_timer();
    if (num_quantums < 0) {
        std::cerr << "thread library error: negative sleep\n";
        unblock_timer();
        return -1;
    }
    if (running_thread_id == 0 && num_quantums != 0) {
        std::cerr << "thread library error: main cannot sleep\n";
        unblock_timer();
        return -1;
    }
    if (num_quantums == 0) {
        switch_threads(YIELDING);
        return 0;
    }
    threads[running_thread_id]->wake_up_time = total_quantums + num_quantums;
    switch_threads(SLEEPING);
    return 0;
}



/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid() {
    return running_thread_id;
}


/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums() {
    return total_quantums;
}


/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << "thread library error: get_quantums on invalid tid\n";
        return -1;
    }
    return threads[tid]->quantums;
}
