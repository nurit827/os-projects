#include "uthreads.h"
#include "IdAllocator.h"
#include "Thread.h"
#include <iostream>
#include <utility>
#include <algorithm>

static int running_thread_id;
// int* running_thread_loc;
IdAllocator allocator;
static std::deque<int> ready;
static Thread* threads[MAX_THREAD_NUM];


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
        return -1;
    }
    running_thread_id = 0;
    return 0;
    // std::cerr << "thread library error: " << "did not implement" << std::endl;
    // return -1;
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
    // Check if the provided entry point is valid
    if (entry_point == nullptr) {
        std::cout << "thread library error: entry point is null pointer" << std::endl;
        return -1;
    }

    // Attempt to allocate a new unique ID for the thread
    int next_id = allocator.next_id();
    
    // Check if we reached the maximum number of concurrent threads
    if (next_id >= MAX_THREAD_NUM) {
        return -1;
    }

    // Allocate memory for the thread's stack on the heap
    int* location = (int*)malloc(STACK_SIZE);
    
    // Add the new thread to the end of the READY queue with its ID and stack location
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
    
    // CASE 1: Terminating the main thread (tid 0)
    if (tid == 0) {
        // Release resources of the currently running thread if it's not the main thread
        if (running_thread_id != 0 && threads[running_thread_id]->stack != nullptr) {
            free(threads[running_thread_id]->stack);
        }

        // Iterate through the READY queue and free all allocated stacks
        for (int id : ready) {
            if (threads[id]->stack != nullptr) {
                free(threads[id]->stack);
            }
        }
        ready.clear();

        // End the entire process
        exit(0);
    }

    // CASE 2: A thread is trying to terminate itself
    if (tid == running_thread_id) {
        // Free the current thread's stack
        free(threads[running_thread_id]->stack);
        // Release the ID back to the allocator
        allocator.delete_id(tid);
        
        /* Note: This part requires a context switch to the next READY thread 
           using siglongjmp. Since this function "does not return", 
           a simple return statement is not enough here.
        */
        return 0; 
    }

    // CASE 3: Terminating a different thread that is currently in the READY state
    auto it = std::find_if(ready.begin(), ready.end(), [tid](const std::pair<int, int*>& p) {
        return p.first == tid;
    });

    // If the ID was not found in the ready list, return an error
    if (it == ready.end()) {
        return -1;
    }

    // Extract the stack pointer, free it, and remove the thread from the queue
    int* ptr = it->second;
    free(ptr);
    ready.erase(it);
    
    // Return the ID to the pool for future use
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
    std::cerr << "thread library error: " << "did not implement" << std::endl;
    return -1;
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
    std::cerr << "thread library error: " << "did not implement" << std::endl;
    return -1;
}
