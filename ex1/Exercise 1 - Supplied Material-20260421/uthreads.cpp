#include "uthreads.h"
#include "IdAllocator.h"
#include "Thread.h"
#include <iostream>
#include <utility>
#include <algorithm>
typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

static int running_thread_id;
IdAllocator allocator;
static std::deque<int> ready;
static Thread* threads[MAX_THREAD_NUM];
static int total_quantums = 0;
enum SwitchReason {YIELDING, TERMINATED, BLOCKED, SLEEPING,PREEMPTED};


// Internal scheduler function. Saves the outgoing thread's CPU state (if it
// will live), updates state and bookkeeping, picks the next ready thread,
// and jumps to it. For TERMINATED, also frees the outgoing thread's resources.
//
// This function returns "normally" only on the resume path — when some future
// siglongjmp brings control back into the sigsetjmp below. On the save path,
// it ends with siglongjmp and never returns to the original caller.
static void switch_threads(SwitchReason reason) {
    Thread* outgoing = threads[running_thread_id];

    // --- Save the outgoing thread's state, if it will live ---
    if (reason != TERMINATED) {
        int ret = sigsetjmp(outgoing->env, 1);
        if (ret != 0) {
            // Resume path: another thread just siglongjmp'd us back here.
            // The incoming-thread bookkeeping was already done by whoever
            // jumped to us, so just return and let the resumed thread continue.
            return;
        }
        // Save path: ret == 0. Fall through to scheduling.
    }

    // --- Handle the outgoing thread based on why we're switching ---
    switch (reason) {
        case YIELDED:
            outgoing->state = READY;
            ready.push_back(outgoing->tid);
            break;

        case BLOCKED_REASON:
            outgoing->state = BLOCKED;
            // Not pushed to ready queue. Will be re-added by uthread_resume.
            break;

        case TERMINATED:
            // Free the outgoing thread's resources. Note: we are still
            // running on its stack at this moment. delete[] marks the memory
            // as freeable but doesn't overwrite it, and the few instructions
            // remaining before the siglongjmp below will not be disturbed.
            delete[] outgoing->stack;
            delete outgoing;
            threads[running_thread_id] = nullptr;
            allocator.delete_id(running_thread_id);
            break;

        // SLEEPING and PREEMPTED will be handled in step 4.
        default:
            // Should not reach here in step 3.
            break;
    }

    // --- Pick the next thread to run ---
    // In step 3 the ready queue should never be empty when we get here:
    // someone always yielded or terminated, and at minimum the main thread
    // exists. If you hit this assert, you have a bug elsewhere.
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

    // --- Jump into the incoming thread. Does not return. ---
    siglongjmp(incoming->env, 1);
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
        std::cerr << "thread library error: non-positive quantum\n";  // CHANGED: spec requires error msg
        return -1;
    }

    // Consume tid 0 from the allocator for the main thread.
    int main_tid = allocator.next_id();   // returns 0
    total_quantums = 1;                   // spec: total starts at 1, main has run for 1 quantum

    // CHANGED: Create the Thread struct for main and store it in threads[].
    Thread* main_thread = new Thread();
    main_thread->tid = main_tid;
    main_thread->state = RUNNING;
    main_thread->stack = nullptr;          // main uses the real stack
    main_thread->quantums = 1;             // spec: total starts at 1, main has run for 1 quantum
    // Note: don't touch main_thread->env yet. We'll sigsetjmp it the first
    // time main is switched away from.

    threads[main_tid] = main_thread;
    running_thread_id = main_tid;

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
    if (entry_point == nullptr) {
        std::cerr << "thread library error: entry point is null pointer\n";  // CHANGED: cerr not cout, and \n
        return -1;
    }

    int next_id = allocator.next_id();
    if (next_id >= MAX_THREAD_NUM) {
        // CHANGED: must release the ID we just took, otherwise we lose it forever.
        allocator.delete_id(next_id);
        std::cerr << "thread library error: max threads reached\n";
        return -1;
    }

    // CHANGED: stack is char*, allocated with new[]. Was int* via malloc, leaking.
    char* stack = new char[STACK_SIZE];

    // CHANGED: Create the Thread struct and store the stack pointer in it.
    Thread* t = new Thread();
    t->tid = next_id;
    t->state = READY;
    t->stack = stack;
    t->quantums = 0;
    // Note: jmp_buf setup (sigsetjmp + translate_address for SP and PC) goes
    // here, but we'll add it when we implement the context switch. For now
    // the bookkeeping alone is enough to make terminate work.

    threads[next_id] = t;       // CHANGED: actually store it
    ready.push_back(next_id);

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
    // CHANGED: validate up front using threads[] as source of truth.
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << "thread library error: terminate on invalid tid\n";  // CHANGED: error msg added
        return -1;
    }

    // CASE 1: terminating main → exit the whole process.
    if (tid == 0) {
        // CHANGED: iterate the full threads[] array (covers RUNNING, READY,
        // and future BLOCKED). Free both stack and Thread struct (was leaking
        // the struct). Use delete[]/delete to match new[]/new.
        for (int i = 0; i < MAX_THREAD_NUM; ++i) {
            if (threads[i] != nullptr) {
                delete[] threads[i]->stack;   // delete[] on nullptr is a no-op (safe for main)
                delete threads[i];
                threads[i] = nullptr;
            }
        }
        ready.clear();
        exit(0);
    }

    // CASE 2: thread terminates itself.
    if (tid == running_thread_id) {
        // CHANGED: do NOT free anything here. We're running on this thread's
        // stack — freeing it then calling another function would execute on
        // freed memory. Cleanup must happen inside switch_threads, after it
        // picks the next thread but before siglongjmp'ing into it.
        // switch_threads(TERMINATED) is not implemented yet — placeholder:
        // switch_threads(TERMINATED);
        return 0;   // unreachable once switch_threads exists
    }

    // CASE 3: terminating some other thread (currently READY in step 3).
    // CHANGED: ready holds plain int tids, so std::find works. Old code used
    // find_if with a pair predicate from before the refactor.
    auto it = std::find(ready.begin(), ready.end(), tid);
    if (it != ready.end()) {
        ready.erase(it);
    }
    // Note: if not in ready (will happen for BLOCKED in step 4), that's fine —
    // existence was already validated above.

    // CHANGED: free both stack and struct, use delete[]/delete, null the slot.
    delete[] threads[tid]->stack;
    delete threads[tid];
    threads[tid] = nullptr;
    allocator.delete_id(tid);

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
    std::cerr << "thread library error: " << "did not implement" << std::endl;
    return -1;
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
    std::cerr << "thread library error: " << "did not implement" << std::endl;
    return -1;
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
    // std::cerr << "thread library error: " << "did not implement" << std::endl;
    // return -1;
    if (num_quantums!=0) {
        return -1;
    }


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


/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid() {
    // std::cerr << "thread library error: " << "did not implement" << std::endl;
    // return -1;
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
    // std::cerr << "thread library error: " << "did not implement" << std::endl;
    // return -1;
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
    // std::cerr << "thread library error: " << "did not implement" << std::endl;
    // return -1;
    return threads[tid]->quantums;
}
