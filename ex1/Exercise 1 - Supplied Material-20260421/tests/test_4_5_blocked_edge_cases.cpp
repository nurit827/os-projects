#include "uthreads.h"
#include <iostream>

int counter[10] = {0};
int order_log[20];
int order_idx = 0;

void simple_thread() {
    int tid = uthread_get_tid();
    while (true) {
        counter[tid]++;
        order_log[order_idx++] = tid;
        uthread_sleep(0);
    }
}

void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        exit(1);
    }
    std::cout << "  ✓ " << msg << std::endl;
}

int main() {
    if (uthread_init(100000) != 0) {
        std::cerr << "init failed" << std::endl; return 1;
    }

    // ===== Test 1: Terminate a BLOCKED thread =====
    std::cout << "\n--- Test 1: Terminate a BLOCKED thread ---" << std::endl;
    int t1 = uthread_spawn(simple_thread);
    check(t1 == 1, "spawned t1=1");
    check(uthread_block(t1) == 0, "block(t1)");
    // t1 is now BLOCKED, not in ready queue
    check(uthread_terminate(t1) == 0, "terminate blocked t1");
    check(uthread_get_quantums(t1) == -1, "t1 is gone");

    // Spawn again — should reuse tid 1
    int t1b = uthread_spawn(simple_thread);
    check(t1b == 1, "tid 1 recycled after blocked-terminate");
    uthread_terminate(t1b);

    // ===== Test 2: Block an already-BLOCKED thread =====
    std::cout << "\n--- Test 2: Block already-BLOCKED thread ---" << std::endl;
    int t2 = uthread_spawn(simple_thread);
    check(uthread_block(t2) == 0, "block(t2) first time");
    check(uthread_block(t2) == 0, "block(t2) again — no-op");
    check(uthread_block(t2) == 0, "block(t2) third time — still no-op");

    // Verify it's still blocked: yield from main, t2 should not run
    int before = counter[t2];
    uthread_sleep(0);
    uthread_sleep(0);
    check(counter[t2] == before, "t2 still blocked, didn't run");

    uthread_terminate(t2);

    // ===== Test 3: Resume a never-blocked thread =====
    std::cout << "\n--- Test 3: Resume a never-blocked thread ---" << std::endl;
    int t3 = uthread_spawn(simple_thread);
    check(uthread_resume(t3) == 0, "resume(t3) — was READY, no-op");

    // Verify still in ready queue and runs normally
    before = counter[t3];
    uthread_sleep(0);
    check(counter[t3] > before, "t3 still runs after no-op resume");

    uthread_terminate(t3);

    // ===== Test 4: Resume a terminated thread =====
    std::cout << "\n--- Test 4: Resume a terminated thread ---" << std::endl;
    int t4 = uthread_spawn(simple_thread);
    int saved_tid = t4;
    uthread_terminate(t4);
    check(uthread_resume(saved_tid) == -1, "resume on terminated tid returns -1");

    // ===== Test 5: Resume order = ready queue order =====
    std::cout << "\n--- Test 5: Multiple blocked, resume in specific order ---" << std::endl;
    int a = uthread_spawn(simple_thread);
    int b = uthread_spawn(simple_thread);
    int c = uthread_spawn(simple_thread);

    // Block all three
    uthread_block(a);
    uthread_block(b);
    uthread_block(c);

    // Verify none of them run
    int ca_before = counter[a], cb_before = counter[b], cc_before = counter[c];
    uthread_sleep(0); uthread_sleep(0); uthread_sleep(0);
    check(counter[a] == ca_before, "a stayed blocked");
    check(counter[b] == cb_before, "b stayed blocked");
    check(counter[c] == cc_before, "c stayed blocked");

    // Resume in order: c, a, b. Ready queue should be [c, a, b].
    // Reset the order log.
    order_idx = 0;
    uthread_resume(c);
    uthread_resume(a);
    uthread_resume(b);

    // Yield from main. They should run in order c, a, b, then main, then c again, etc.
    // We'll yield 3 times so each of c, a, b runs once.
    uthread_sleep(0);
    uthread_sleep(0);
    uthread_sleep(0);

    check(order_idx >= 3, "at least 3 threads ran");
    check(order_log[0] == c, "first to run after resume was c");
    check(order_log[1] == a, "second was a");
    check(order_log[2] == b, "third was b");

    uthread_terminate(a);
    uthread_terminate(b);
    uthread_terminate(c);

    // ===== Test 6: Block-then-terminate flurry — exercises memory cleanup =====
    std::cout << "\n--- Test 6: Repeated block + terminate ---" << std::endl;
    for (int i = 0; i < 50; ++i) {
        int t = uthread_spawn(simple_thread);
        uthread_block(t);
        uthread_terminate(t);
    }
    check(true, "50 block+terminate cycles completed without crash");

    // After all that, can we still spawn and run a thread?
    int sanity = uthread_spawn(simple_thread);
    int sanity_before = counter[sanity];
    uthread_sleep(0);
    check(counter[sanity] > sanity_before, "fresh thread still works after stress");
    uthread_terminate(sanity);

    // ===== Test 7: Self-block, then someone resumes us, then we terminate =====
    // (This is mostly redundant with the earlier self-block test, but worth
    //  re-checking now that other code has changed.)
    std::cout << "\n--- Test 7: Self-block, resume, run, terminate ---" << std::endl;
    int t7 = uthread_spawn(simple_thread);
    int t7_initial = counter[t7];
    // Let it run once first
    uthread_sleep(0);
    check(counter[t7] == t7_initial + 1, "t7 ran once");
    // Now block it
    uthread_block(t7);
    int t7_before = counter[t7];
    uthread_sleep(0); uthread_sleep(0);
    check(counter[t7] == t7_before, "t7 blocked");
    // Resume and verify it runs
    uthread_resume(t7);
    uthread_sleep(0);
    check(counter[t7] > t7_before, "t7 ran after resume");
    uthread_terminate(t7);

    std::cout << "\nAll edge-case tests passed!" << std::endl;
    uthread_terminate(0);
    return 0;
}
