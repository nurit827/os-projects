#include "uthreads.h"
#include <iostream>

// Globals to track what happened
int counter_a = 0;
int counter_b = 0;
int tid_a = -1;
int tid_b = -1;

void thread_a() {
    while (true) {
        counter_a++;
        std::cout << "A: counter_a=" << counter_a << std::endl;
        uthread_sleep(0);
    }
}

void thread_b() {
    while (true) {
        counter_b++;
        std::cout << "B: counter_b=" << counter_b << std::endl;
        uthread_sleep(0);
    }
}

void self_blocker() {
    std::cout << "Self-blocker: about to block myself" << std::endl;
    uthread_block(uthread_get_tid());
    // Should only get here after someone resumes us
    std::cout << "Self-blocker: resumed!" << std::endl;
    uthread_terminate(uthread_get_tid());
}

int check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        exit(1);
    }
    std::cout << "  ✓ " << msg << std::endl;
    return 0;
}

int main() {
    if (uthread_init(100000) != 0) {
        std::cerr << "init failed" << std::endl;
        return 1;
    }

    // ===== Test 1: Error cases =====
    std::cout << "\n--- Test 1: Error cases ---" << std::endl;

    // Block invalid tid
    check(uthread_block(-1) == -1, "block(-1) returns -1");
    check(uthread_block(MAX_THREAD_NUM) == -1, "block(MAX_THREAD_NUM) returns -1");
    check(uthread_block(50) == -1, "block(non-existent tid) returns -1");

    // Block main thread
    check(uthread_block(0) == -1, "block(0) returns -1 (cannot block main)");

    // Resume invalid tid
    check(uthread_resume(-1) == -1, "resume(-1) returns -1");
    check(uthread_resume(50) == -1, "resume(non-existent tid) returns -1");

    // ===== Test 2: Block READY thread, then resume =====
    std::cout << "\n--- Test 2: Block READY thread, then resume ---" << std::endl;

    tid_a = uthread_spawn(thread_a);
    tid_b = uthread_spawn(thread_b);
    check(tid_a == 1 && tid_b == 2, "spawned A=1, B=2");

    // Both A and B are in READY queue. Block A while it's still in queue.
    check(uthread_block(tid_a) == 0, "block(A) returns 0");

    // Block A again — should be no-op, not an error
    check(uthread_block(tid_a) == 0, "block(A) again returns 0 (no-op)");

    // Yield from main. B should run, A should NOT.
    int counter_a_before = counter_a;
    int counter_b_before = counter_b;
    uthread_sleep(0);
    uthread_sleep(0);  // give B a couple turns

    check(counter_a == counter_a_before, "A did not run while blocked");
    check(counter_b > counter_b_before, "B did run");

    // Resume A. It should now be in the ready queue.
    check(uthread_resume(tid_a) == 0, "resume(A) returns 0");

    // Resume A again — already READY, should be no-op
    check(uthread_resume(tid_a) == 0, "resume(A) again returns 0 (no-op)");

    // Resume the running thread (main, tid 0) — should be no-op
    check(uthread_resume(0) == 0, "resume(main) returns 0 (no-op)");

    // Yield. Both A and B should run now.
    counter_a_before = counter_a;
    counter_b_before = counter_b;
    uthread_sleep(0);
    uthread_sleep(0);
    uthread_sleep(0);

    check(counter_a > counter_a_before, "A ran after resume");
    check(counter_b > counter_b_before, "B continued running");

    // Clean up before next test
    uthread_terminate(tid_a);
    uthread_terminate(tid_b);

    // ===== Test 3: Self-block, external resume =====
    std::cout << "\n--- Test 3: Self-block then external resume ---" << std::endl;

    int sb_tid = uthread_spawn(self_blocker);

    // Yield once. Self-blocker runs, prints, blocks itself, control returns to main.
    uthread_sleep(0);

    // Self-blocker should be BLOCKED. We don't have a get_state API, but we can
    // verify behaviorally: yielding shouldn't run it.
    uthread_sleep(0);
    uthread_sleep(0);

    // Each sleep(0) is one context switch (main → main since nothing else is ready).
    // self-blocker should NOT have gained any quantums.
    check(uthread_get_quantums(sb_tid) == 1,
          "self-blocker has exactly 1 quantum (blocked, didn't run again)");

    // Resume it
    check(uthread_resume(sb_tid) == 0, "resume(self_blocker) returns 0");

    // Yield. Self-blocker should run, print "resumed!", and terminate itself.
    uthread_sleep(0);

    // Now self_blocker should be gone
    check(uthread_get_quantums(sb_tid) == -1, "self-blocker is gone after self-terminate");

    std::cout << "\nAll tests passed!" << std::endl;
    uthread_terminate(0);
    return 0;
}