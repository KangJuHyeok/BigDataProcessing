#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cmath> 
#include <algorithm> // std::min을 사용하기 위해 추가

using namespace std;

// ========= [1] 공유 데이터 및 상수 설정 =========

constexpr int START_NUM = 1'000'000;
constexpr int END_NUM = 5'000'000;
constexpr int NUM_OPERATIONS = END_NUM - START_NUM + 1; // 총 연산 횟수

long long shared_counter = 0; 

// =================================================


// ========= [2] 커스텀 락 메커니즘 구현 공간 =========

/**
 * @brief 2. Pure Spinlock (std::atomic_flag 기반)
 */
class AtomicFlag_SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire));
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};


/**
 * @brief 3. TAS (Test-and-Set) Lock 구현
 */
class TAS_Lock {
    std::atomic<bool> lock_flag = false;
public:
    void lock() {
        while (lock_flag.exchange(true));
    }
    void unlock() {
        lock_flag.store(false);
    }
};

/**
 * @brief 4. TTAS (Test-and-Test-and-Set) Lock 구현 공간
 */
class TTAS_Lock {
    std::atomic<bool> lock_flag = false;
public:
    void lock() {
        while (true) {
            if (!lock_flag.load()) { // 1차 Test: 캐시에서 확인
                bool expected = false;
                if (lock_flag.compare_exchange_weak(expected, true)) { // 2차 Test-and-Set: Atomic 연산
                    return; // 락 획득 성공
                }
            }
            // 락이 잡혀있으면 캐시에서 계속 스핀
        }
    }
    void unlock() {
        lock_flag.store(false);
    }
};

/**
 * @brief 5. Backoff Lock 구현 공간
 */
class Backoff_Lock {
    std::atomic<bool> lock_flag = false;
public:
    void lock() {
        int current_delay = 1;
        const int MAX_DELAY = 1024;
        
        while (true) {
            if (!lock_flag.load()) { // 1차 Test
                bool expected = false;
                if (lock_flag.compare_exchange_weak(expected, true)) { // 2차 Test-and-Set
                    return; // 락 획득 성공
                }
            }
            
            // Backoff: 일정 시간 대기 (Busy-wait)
            for (int i = 0; i < current_delay; ++i) {
                // 여기에 실제 Backoff 지연
            }

            // delay 증가 (지수적 백오프)
            current_delay = std::min(current_delay * 2, MAX_DELAY);
        }
    }
    void unlock() {
        lock_flag.store(false);
    }
};


// =================================================

// 템플릿을 사용하여 다양한 락 메커니즘을 적용할 수 있도록 함수를 일반화합니다.
template<typename LockType>
void worker_function_with_lock(int thread_id, LockType& lock_instance, long long& counter, int iterations_per_thread) {
    for (int i = 0; i < iterations_per_thread; ++i) {
        lock_instance.lock();
        counter++; // Critical Section: 공유 카운터 증가
        lock_instance.unlock();
    }
}

// No Lock 전용 Worker 함수
void worker_function_no_lock(int thread_id, long long& counter, int iterations_per_thread) {
    for (int i = 0; i < iterations_per_thread; ++i) {
        counter++; // Critical Section: 공유 카운터 증가
    }
}


/**
 * @brief 실험 실행 및 결과 측정
 */
template<typename LockType>
double run_experiment(const string& lock_name, int num_threads, bool use_lock = true) {
    
    // 초기화
    shared_counter = 0;
    LockType lock_instance;
    
    // 각 스레드가 처리할 연산 횟수 분배
    int iterations_per_thread = NUM_OPERATIONS / num_threads;
    int remaining_iterations = NUM_OPERATIONS % num_threads;

    vector<thread> threads;
    auto start_time = chrono::high_resolution_clock::now();

    // 스레드 생성 및 실행
    for (int i = 0; i < num_threads; ++i) {
        int thread_iterations = iterations_per_thread + (i < remaining_iterations ? 1 : 0);
        
        if (use_lock) {
            // 락을 사용하는 경우
            threads.emplace_back(worker_function_with_lock<LockType>, i, ref(lock_instance), ref(shared_counter), thread_iterations);
        } else {
            // No Lock (락을 사용하지 않는 경우)
            threads.emplace_back(worker_function_no_lock, i, ref(shared_counter), thread_iterations);
        }
    }

    // 모든 스레드 종료 대기
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;
    
    // 결과 출력
    cout << lock_name << " (" << num_threads << " threads): ";
    cout << "Time = " << duration.count() * 1000 << " ms, ";
    
    // 정확성 검증 (정답은 NUM_OPERATIONS와 동일해야 함)
    long long expected_result = NUM_OPERATIONS;
    bool is_correct = (shared_counter == expected_result);
    
    cout << "Final Count = " << shared_counter;
    cout << (is_correct ? " (Correct)" : " (Incorrect)");

    if (!is_correct) {
        cout << ", Error = " << abs(shared_counter - expected_result);
    }
    cout << endl;

    return duration.count();
}


// =================================================

int main() {
    
    cout << "===== Lock Mechanism Performance Evaluation =====" << endl;
    cout << "Total Operations: " << NUM_OPERATIONS << " (Adding 1,000,000 to 5,000,000)" << endl;

    vector<int> thread_counts = {2, 4, 8, 16, 32};

    for (int num_threads : thread_counts) {
        cout << "\n--- Testing with " << num_threads << " Threads ---" << endl;
        
        // 1. No Lock
        run_experiment<TAS_Lock>("No Lock", num_threads, false);

        // 2. Pure Spinlock (std::mutex를 대체)
        run_experiment<AtomicFlag_SpinLock>("Pure Spinlock (atomic_flag)", num_threads);

        // 3. TAS Lock
        run_experiment<TAS_Lock>("TAS Lock", num_threads);
        
        // 4. TTAS Lock
        run_experiment<TTAS_Lock>("TTAS Lock", num_threads);

        // 5. Backoff Lock
        run_experiment<Backoff_Lock>("Backoff Lock", num_threads);
    }

    return 0;
}