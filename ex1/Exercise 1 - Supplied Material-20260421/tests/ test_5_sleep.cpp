#include "uthreads.h"
#include <iostream>

int counter[10] = {0};
int run_log[100];
int log_idx = 0;

void simple_thread() {
    int tid = uthread_get_tid();
    while (true) {
        counter[tid]++;
        run_log[log_idx++] = tid;
        uthread_sleep(0);   // yield
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

    // ===== Test 1: Error cases =====
    std::cout << "\n--- Test 1: Sleep error cases ---" << std::endl;
   
    // Main can't sleep with n != 0
    check(uthread_sleep(5) == -1, "main sleep(5) returns -1");
    check(uthread_sleep(1) == -1, "main sleep(1) returns -1");
   
    // Main CAN sleep(0) — that's just yield (no other threads → main resumes)
    check(uthread_sleep(0) == 0, "main sleep(0) is OK");

    // ===== Test 2: Basic sleep — thread sleeps, others run =====
    std::cout << "\n--- Test 2: Basic sleep ---" << std::endl;
    int a = uthread_spawn(simple_thread);  // tid 1
    int b = uthread_spawn(simple_thread);  // tid 2
   
    // Yield once: a runs, prints, yields. Then b runs, prints, yields.
    // Then back to main.
    uthread_sleep(0);
    check(counter[a] >= 1, "a ran at least once");
    check(counter[b] >= 1, "b ran at least once");

    // ===== Test 3: Sleep duration is correct =====
    std::cout << "\n--- Test 3: Sleep duration ---" << std::endl;
   
    // Get a, b to a known state, then have a sleep for 3 quantums.
    // We'll do this by making main yield in a controlled way and observing
    // when a starts running again.
    //
    // Setup: a will go to sleep(3) on its NEXT run. We need a way to trigger
    // that. Easiest: kill a and b, replace with new threads.
    uthread_terminate(a);
    uthread_terminate(b);
   
    // Use a sleep marker. We'll use the wake_up_time semantics directly:
    // A spawned thread calls sleep(N) once and then yields forever after.
    // Track when it next runs.
   
    // Actually, easier approach: spawn a thread whose first action is sleep(3).
    // Then yield from main repeatedly and watch when it wakes.
   
    static int sleeper_runs = 0;
    static bool sleeper_done = false;
    auto sleeper_fn = []() {
        uthread_sleep(3);   // sleep for 3 quantums
        sleeper_runs++;
        sleeper_done = true;
        // yield forever after
        while (true) uthread_sleep(0);
    };
   
    int s_tid = uthread_spawn(sleeper_fn);
   
    // After spawn, ready = [sleeper]. Main yields:
    //   q=T+1: sleeper runs, calls sleep(3) → wake_up_time = T+1+3 = T+4
    //          switch_threads picks main. ready empty, but main wasn't pushed
    //          either (sleeper is sleeping, main was the only one yielding).
    //          Wait — main called yield, so main IS pushed back. main runs.
    //   q=T+2: main yields again.
    //   q=T+3: main yields again.
    //   q=T+4: wake-up loop fires for sleeper. sleeper goes to ready.
    //          But main was just pushed too. Order: depends on whether wake-up
    //          runs before or after main is added... let's just verify timing.
   
    int t0 = uthread_get_total_quantums();
   
    uthread_sleep(0);  // sleeper runs, sleeps. main runs again.
    // sleeper should NOT have completed sleep yet
    check(!sleeper_done, "after 1 yield: sleeper still sleeping");
   
    uthread_sleep(0);  // main runs (sleeper still sleeping)
    check(!sleeper_done, "after 2 yields: sleeper still sleeping");
   
    uthread_sleep(0);  // main runs
    // Now total_quantums has advanced 3 times since sleeper called sleep(3).
    // The wake-up loop should fire on the NEXT switch.
   
    uthread_sleep(0);  // sleeper should wake up and run on this switch
    check(sleeper_done, "after 4 yields: sleeper woke up and ran");
   
    int elapsed = uthread_get_total_quantums() - t0;
    std::cout << "  (took " << elapsed << " quantums for sleep(3) to complete)" << std::endl;
   
    uthread_terminate(s_tid);

    // ===== Test 4: THE KEY EDGE CASE — sleep + block =====
    std::cout << "\n--- Test 4: Sleep + Block (the crucial one) ---" << std::endl;
   
    static bool sb_done = false;
    auto sleep_block_fn = []() {
        uthread_sleep(2);   // sleep for 2 quantums
        sb_done = true;
        while (true) uthread_sleep(0);
    };
   
    int sb_tid = uthread_spawn(sleep_block_fn);
   
    // Sleeper runs once, calls sleep(2). Wake_up at total_quantums + 2.
    uthread_sleep(0);
    check(!sb_done, "sleeper went to sleep");
   
    // Now block it BEFORE its sleep is over.
    check(uthread_block(sb_tid) == 0, "block sleeper while it's sleeping");
   
    // Yield several times — way more than 2 quantums.
    // Sleep should expire, but block should keep it off the ready queue.
    for (int i = 0; i < 10; ++i) uthread_sleep(0);
    check(!sb_done, "sleeper did NOT run even after sleep expired (still blocked)");
   
    // Now resume it. Sleep is already over, so it should go straight to ready.
    check(uthread_resume(sb_tid) == 0, "resume sleeper");
    uthread_sleep(0);  // sleeper should run now
    check(sb_done, "sleeper ran after resume (sleep was already done)");
   
    uthread_terminate(sb_tid);

    // ===== Test 5: Block first, THEN sleep would be — but sleep is self-only =====
    // (Can't really happen — only the running thread can call sleep on itself,
    //  and a blocked thread isn't running. So block-then-sleep order is impossible
    //  to construct. Skipping.)

    // ===== Test 6: Resume during sleep (no block) — should be no-op =====
    std::cout << "\n--- Test 6: Resume non-blocked sleeper is no-op ---" << std::endl;
   
    static bool r_done = false;
    auto resume_test_fn = []() {
        uthread_sleep(2);
        r_done = true;
        while (true) uthread_sleep(0);
    };
   
    int r_tid = uthread_spawn(resume_test_fn);
    uthread_sleep(0);  // r_tid runs and sleeps
   
    // Resume the sleeping thread — but it wasn't blocked, just sleeping.
    // Per spec: "Resuming a thread in a RUNNING or READY state has no effect"
    // What about a sleeping thread? It's in BLOCKED state but is_blocked == false.
    // Resume should be no-op (clears is_blocked, but it was already false).
    check(uthread_resume(r_tid) == 0, "resume non-blocked sleeper returns 0");
    check(!r_done, "resume did NOT wake the sleeper early");
   
    // Let sleep expire normally
    for (int i = 0; i < 5; ++i) uthread_sleep(0);
    check(r_done, "sleeper woke up at the right time");
   
    uthread_terminate(r_tid);

    // ===== Test 7: Multiple threads waking up at the same time =====
    std::cout << "\n--- Test 7: Multiple simultaneous wake-ups ---" << std::endl;
   
    static int wake_log[10];
    static int wake_idx = 0;
   
    auto sync_sleep_fn = []() {
        int tid = uthread_get_tid();
        uthread_sleep(2);
        wake_log[wake_idx++] = tid;
        // immediately terminate
        uthread_terminate(tid);
    };
   
    // Spawn 3 threads. When main yields, they each run and call sleep(2).
    // They all sleep until total_quantums has advanced by 2 from when they slept.
    // Since they slept on consecutive quantums, they wake up on consecutive
    // quantums too. Not exactly "the same time" — they'll wake one quantum apart.
    //
    // To get them to wake at literally the same quantum, we need them all to
    // have the same wake_up_time. Easiest way: have main set up the situation
    // so they all sleep on the same wake_up_time value.
    //
    // Actually, simplest: spawn 3, let them all run + sleep(N) where N
    // adjusts so wake_up_time is the same.
   
    // Let's just verify they all wake up correctly, even if not the same quantum.
    wake_idx = 0;
    int x = uthread_spawn(sync_sleep_fn);
    int y = uthread_spawn(sync_sleep_fn);
    int z = uthread_spawn(sync_sleep_fn);
    (void)x; (void)y; (void)z;
   
    // Yield enough times to let them all run, sleep, wake, and terminate.
    for (int i = 0; i < 20; ++i) uthread_sleep(0);
   
    check(wake_idx == 3, "all 3 sleepers woke up and ran");

    // ===== Test 8: Sleep(1) — minimum non-zero sleep =====
    std::cout << "\n--- Test 8: Sleep(1) ---" << std::endl;
   
    static bool s1_done = false;
    auto sleep1_fn = []() {
        uthread_sleep(1);
        s1_done = true;
        while (true) uthread_sleep(0);
    };
   
    int s1_tid = uthread_spawn(sleep1_fn);
    uthread_sleep(0);  // s1 runs, sleeps(1)
    check(!s1_done, "s1 went to sleep");
    uthread_sleep(0);  // sleep should expire here
    uthread_sleep(0);  // s1 should run by now
    check(s1_done, "s1 woke up after sleep(1)");
   
    uthread_terminate(s1_tid);

    // ===== Test 9: Terminate a sleeping thread =====
    std::cout << "\n--- Test 9: Terminate sleeping thread ---" << std::endl;
   
    static bool ts_done = false;
    auto term_sleep_fn = []() {
        uthread_sleep(100);
        ts_done = true;
        while (true) uthread_sleep(0);
    };
   
    int ts_tid = uthread_spawn(term_sleep_fn);
    uthread_sleep(0);
   
    // Now terminate it while sleeping
    check(uthread_terminate(ts_tid) == 0, "terminated sleeping thread");
    check(uthread_get_quantums(ts_tid) == -1, "sleeping thread is gone");
    check(!ts_done, "sleeping thread never woke (was killed)");

    std::cout << "\nAll sleep tests passed!" << std::endl;
    uthread_terminate(0);
    return 0;
}