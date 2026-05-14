#include "uthreads.h"
#include <iostream>

// Globals to coordinate the test (since threads can't easily return values)
int killer_tid = -1;
int victim_tid = -1;
bool victim_should_have_run = false;

void victim_thread() {
    // If we ever get here, mark it. The killer should kill us before this runs
    // a second time.
    victim_should_have_run = true;
    std::cout << "Victim: running, total_quantums=" 
              << uthread_get_total_quantums() << std::endl;
    uthread_sleep(0);
    
    // After being killed, we should never get back here.
    std::cerr << "ERROR: victim resumed after being terminated!" << std::endl;
    exit(1);
}

void killer_thread() {
    std::cout << "Killer: about to terminate victim (tid=" << victim_tid << ")" 
              << std::endl;
    
    // Kill the victim
    int ret = uthread_terminate(victim_tid);
    if (ret != 0) {
        std::cerr << "ERROR: terminate returned " << ret << std::endl;
        exit(1);
    }
    
    std::cout << "Killer: terminate returned 0" << std::endl;
    std::cout << "Killer: my quantums=" << uthread_get_quantums(killer_tid)
              << ", total=" << uthread_get_total_quantums() << std::endl;
    
    // Killing self at the end (per spec assumption)
    uthread_terminate(uthread_get_tid());
}

int main() {
    if (uthread_init(100000) != 0) {
        std::cerr << "init failed" << std::endl;
        return 1;
    }
    
    std::cout << "After init: total_quantums=" << uthread_get_total_quantums() 
              << " (expected 1)" << std::endl;
    std::cout << "Main quantums=" << uthread_get_quantums(0) 
              << " (expected 1)" << std::endl;
    
    // Spawn victim FIRST so it has a smaller tid and runs before killer
    victim_tid = uthread_spawn(victim_thread);
    killer_tid = uthread_spawn(killer_thread);
    
    std::cout << "Spawned victim=" << victim_tid 
              << ", killer=" << killer_tid << std::endl;
    
    // After spawn, neither has run yet
    std::cout << "Victim quantums=" << uthread_get_quantums(victim_tid) 
              << " (expected 0)" << std::endl;
    std::cout << "Killer quantums=" << uthread_get_quantums(killer_tid) 
              << " (expected 0)" << std::endl;
    
    // Yield. Order should be: main → victim → killer → main
    // - main yields, goes to back: queue = [victim, killer, main]
    // - victim runs (quantum 1), prints, yields: queue = [killer, main, victim]
    // - killer runs (quantum 1), kills victim, terminates self
    //   queue = [main]  (victim was removed from queue, killer freed itself)
    // - main resumes here
    uthread_sleep(0);
    
    std::cout << "Main resumed. total_quantums=" << uthread_get_total_quantums() 
              << std::endl;
    
    // Verify victim is gone
    int q = uthread_get_quantums(victim_tid);
    if (q != -1) {
        std::cerr << "ERROR: victim should be gone, but get_quantums returned " 
                  << q << std::endl;
        return 1;
    }
    std::cout << "Victim is gone (get_quantums returned -1) ✓" << std::endl;
    
    // Verify killer is gone
    q = uthread_get_quantums(killer_tid);
    if (q != -1) {
        std::cerr << "ERROR: killer should be gone, get_quantums returned " 
                  << q << std::endl;
        return 1;
    }
    std::cout << "Killer is gone (get_quantums returned -1) ✓" << std::endl;
    
    // Verify total_quantums: 1 (init) + victim(1) + killer(1) + main again(1) = 4
    if (uthread_get_total_quantums() != 4) {
        std::cerr << "ERROR: expected total_quantums=4, got " 
                  << uthread_get_total_quantums() << std::endl;
        return 1;
    }
    std::cout << "total_quantums=4 ✓" << std::endl;
    
    // Now check we can spawn a new thread and it gets the lowest free tid.
    // victim_tid was probably 1. After it died, tid 1 should be free.
    int new_tid = uthread_spawn(victim_thread);  // reuse the function, doesn't matter
    std::cout << "New spawn got tid=" << new_tid 
              << " (expected " << victim_tid << ")" << std::endl;
    if (new_tid != victim_tid) {
        std::cerr << "ERROR: tid recycling failed" << std::endl;
        return 1;
    }
    
    // Don't actually let it run — just terminate from main.
    uthread_terminate(new_tid);
    
    std::cout << "All checks passed!" << std::endl;
    uthread_terminate(0);
    return 0;
}