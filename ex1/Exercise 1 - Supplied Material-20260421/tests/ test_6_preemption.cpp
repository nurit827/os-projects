#include "uthreads.h"
#include <iostream>

// Volatile so the compiler doesn't optimize away the busy loops.
volatile int counter[10] = {0};
volatile bool stop_flag = false;

void busy_thread() {
    int tid = uthread_get_tid();
    while (!stop_flag) {
        counter[tid]++;
    }
    // After the test signals stop, terminate cleanly.
    uthread_terminate(tid);
}

void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        exit(1);
    }
    std::cout << "  ✓ " << msg << std::endl;
}

// Burn CPU time by counting up. Returns when about `iterations` have been done.
// Used so main is "doing work" and the timer can fire.
void burn_cpu(long iterations) {
    volatile long x = 0;
    for (long i = 0; i < iterations; ++i) x++;
}

int main() {
    // ===== Test 1: Two busy threads both run without yielding =====
    std::cout << "\n--- Test 1: Pure preemption (no voluntary yields) ---" << std::endl;

    // Use a small quantum so preemption fires often during the test.
    if (uthread_init(50000) != 0) {  // 50ms per quantum
        std::cerr << "init failed" << std::endl;
        return 1;
    }

    int a = uthread_spawn(busy_thread);
    int b = uthread_spawn(busy_thread);
    check(a == 1 && b == 2, "spawned a=1, b=2");

    // Burn enough CPU time for many quanta to elapse.
    // At 50ms quantum, 1 second of CPU = ~20 quantum boundaries.
    burn_cpu(500000000L);

    // Stop them and let them terminate.
    stop_flag = true;
    // Yield a few times to let a and b notice and terminate.
    for (int i = 0; i < 10; ++i) uthread_sleep(0);

    check(counter[a] > 0, "thread a ran (got preempted in)");
    check(counter[b] > 0, "thread b ran (got preempted in)");
    std::cout << "  counter[a]=" << counter[a] << ", counter[b]=" << counter[b] << std::endl;

    // ===== Test 2: total_quantums grew due to preemption =====
    std::cout << "\n--- Test 2: total_quantums grew over time ---" << std::endl;
   
    // After init total was 1. After all the preemption + a few yields, it should
    // be much greater than just the number of yields we performed.
    int total = uthread_get_total_quantums();
    std::cout << "  total_quantums=" << total << std::endl;
    check(total > 10, "total_quantums grew significantly (preemption working)");

    // ===== Test 3: Sleep duration in real time, not just yields =====
    std::cout << "\n--- Test 3: Sleep with preemption running ---" << std::endl;

    stop_flag = false;
    int c = uthread_spawn(busy_thread);  // creates work to be preempted

    static volatile bool sleeper_done = false;
    static int sleeper_quantum_at_wake = 0;
    auto sleeper_fn = []() {
        int target_wake = uthread_get_total_quantums() + 3;
        uthread_sleep(3);
        sleeper_quantum_at_wake = uthread_get_total_quantums();
        sleeper_done = true;
        // Verify we woke up at or after target
        check(uthread_get_total_quantums() >= target_wake,
              "sleeper woke after >= target quantum");
        uthread_terminate(uthread_get_tid());
    };

    int s = uthread_spawn(sleeper_fn);
    (void)s;

    // Burn cpu while sleeper sleeps and busy thread c keeps the timer firing.
    burn_cpu(500000000L);

    check(sleeper_done, "sleeper woke up while we were burning CPU");
    std::cout << "  sleeper woke at total_quantums=" << sleeper_quantum_at_wake << std::endl;

    // Clean up busy thread c
    stop_flag = true;
    for (int i = 0; i < 10; ++i) uthread_sleep(0);

    // ===== Test 4: Block + Resume during preemption =====
    std::cout << "\n--- Test 4: Block during preemption ---" << std::endl;

    stop_flag = false;
    for (int i = 0; i < 10; ++i) counter[i] = 0;

    int d = uthread_spawn(busy_thread);
    int e = uthread_spawn(busy_thread);

    // Let them both run for a while via preemption
    burn_cpu(200000000L);
   
    int d_before_block = counter[d];

    // Block d. Now only e and main should run.
    check(uthread_block(d) == 0, "block(d) succeeded");
   
    // Burn more CPU. d should not run; e and main do.
    burn_cpu(500000000L);
    int d_after_block = counter[d];
    int e_after_block = counter[e];
   
    check(d_after_block == d_before_block, "d did not run while blocked");
    check(e_after_block > 0, "e kept running while d was blocked");

    // Resume d. It should run again.
    check(uthread_resume(d) == 0, "resume(d) succeeded");
    burn_cpu(300000000L);
    check(counter[d] > d_after_block, "d ran again after resume");

    // Cleanup
    stop_flag = true;
    for (int i = 0; i < 10; ++i) uthread_sleep(0);

    std::cout << "\nAll preemption tests passed!" << std::endl;
    uthread_terminate(0);
    return 0;
}