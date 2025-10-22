#include <iostream>
#include <chrono>
#include <vector>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <linux/futex.h>
#include <semaphore.h>
#include <atomic>

static int futex_wait(volatile int* addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, nullptr, nullptr, 0);
}
static int futex_wake(volatile int* addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, nullptr, nullptr, 0);
}

int main(int argc, char** argv) {
    int nproc  = (argc > 1) ? atoi(argv[1]) : 8;
    int ntrial = (argc > 2) ? atoi(argv[2]) : 50;

    std::cout << "Processes: " << nproc << ", Trials: " << ntrial << "\n";

    //---------------------------------------------
    // 1. Futex benchmark
    //---------------------------------------------
    {
        std::vector<double> lat;
        lat.reserve(ntrial);

        int* futex_val = static_cast<int*>(
            mmap(nullptr, sizeof(int), PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        if (futex_val == MAP_FAILED) { perror("mmap futex"); return 1; }

        for (int t = 0; t < ntrial; t++) {
            *futex_val = 0;

            for (int i = 0; i < nproc; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    while (*futex_val == 0)
                        futex_wait(futex_val, 0);
                    _exit(0);
                }
            }

            usleep(1000); // allow children to block

            auto start = std::chrono::high_resolution_clock::now();
            *futex_val = 1;
            futex_wake(futex_val, nproc);

            for (int i = 0; i < nproc; i++) wait(nullptr);
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> dur = end - start;
            lat.push_back(dur.count());
        }

        double avg = 0;
        for (auto v : lat) avg += v;
        avg /= lat.size();

        std::cout << "[FUTEX]   Average wake time: " << avg / nproc
                  << " us/proc (total " << avg << " us)\n";
        munmap(futex_val, sizeof(int));
    }

    //---------------------------------------------
    // 2. System V semaphore benchmark
    //---------------------------------------------
    {
        std::vector<double> lat;
        lat.reserve(ntrial);

        int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
        if (semid < 0) { perror("semget"); return 1; }

        for (int t = 0; t < ntrial; t++) {
            semctl(semid, 0, SETVAL, 0); // init to 0

            for (int i = 0; i < nproc; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    struct sembuf op = {0, -1, 0};
                    semop(semid, &op, 1);
                    _exit(0);
                }
            }

            usleep(1000); // allow children to block

            auto start = std::chrono::high_resolution_clock::now();
            struct sembuf op = {0, static_cast<short>(nproc), 0}; // release all
            semop(semid, &op, 1);

            for (int i = 0; i < nproc; i++) wait(nullptr);
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> dur = end - start;
            lat.push_back(dur.count());
        }

        double avg = 0;
        for (auto v : lat) avg += v;
        avg /= lat.size();

        std::cout << "[SYSV SEM] Average wake time: " << avg / nproc
                  << " us/proc (total " << avg << " us)\n";
        semctl(semid, 0, IPC_RMID);
    }

    //---------------------------------------------
    // 3. POSIX semaphore benchmark (unnamed, process-shared)
    //---------------------------------------------
    {
        std::vector<double> lat;
        lat.reserve(ntrial);

        sem_t* sem = static_cast<sem_t*>(
            mmap(nullptr, sizeof(sem_t), PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        if (sem == MAP_FAILED) { perror("mmap sem"); return 1; }

        for (int t = 0; t < ntrial; t++) {
            sem_init(sem, 1, 0); // 1 = process-shared

            for (int i = 0; i < nproc; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    sem_wait(sem);
                    _exit(0);
                }
            }

            usleep(1000); // allow children to block

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < nproc; i++) sem_post(sem);

            for (int i = 0; i < nproc; i++) wait(nullptr);
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> dur = end - start;
            lat.push_back(dur.count());
        }

        double avg = 0;
        for (auto v : lat) avg += v;
        avg /= lat.size();

        std::cout << "[POSIX SEM] Average wake time: " << avg / nproc
                  << " us/proc (total " << avg << " us)\n";

        sem_destroy(sem);
        munmap(sem, sizeof(sem_t));
    }

    return 0;
}
