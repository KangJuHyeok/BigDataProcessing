#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cmath> 
#include <algorithm> 

using namespace std;

// ========= [1] 공유 데이터 및 상수 설정 =========

constexpr int START_NUM = 1'000'000;
constexpr int END_NUM = 5'000'000;
constexpr int NUM_OPERATIONS = END_NUM - START_NUM + 1; // 총 연산 횟수

long long shared_counter = 0; 

// =================================================


// ========= [2] 커스텀 락 메커니즘 구현 공간 =========


/**
 * @brief 1. TAS (Test-and-Set) Lock 구현
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
 * @brief 2. TTAS (Test-and-Test-and-Set) Lock 구현 공간
 */
class TTAS_Lock {
    std::atomic<bool> lock_flag = false;
public:
    void lock() {
        while (true) {
            if (!lock_flag.load()) { 
                bool expected = false;
                if (lock_flag.compare_exchange_weak(expected, true)) { 
                    return; 
                }
            }
        }
    }
    void unlock() {
        lock_flag.store(false);
    }
};

/**
 * @brief 3. Backoff Lock 구현 공간
 */
class Backoff_Lock {
    std::atomic<bool> lock_flag = false;
public:
    void lock() {
        int current_delay = 1;
        const int MAX_DELAY = 1024;
        
        while (true) {
            if (!lock_flag.load()) { 
                bool expected = false;
                if (lock_flag.compare_exchange_weak(expected, true)) { 
                    return; 
                }
            }
            
            for (int i = 0; i < current_delay; ++i) {
            }

            current_delay = std::min(current_delay * 2, MAX_DELAY);
        }
    }
    void unlock() {
        lock_flag.store(false);
    }
};


// =================================================

/**
 * @brief 스레드 작업 함수 (Lock 사용)
 * @param start_val 스레드가 합산할 시작 숫자
 * @param end_val 스레드가 합산할 종료 숫자
 */
template<typename LockType>
void worker_function_with_lock(LockType& lock_instance, long long& counter, int start_val, int end_val) {
    for (int i = start_val; i <= end_val; ++i) {
        lock_instance.lock();
        counter += i; // Critical Section: 실제 숫자를 공유 카운터에 더함
        lock_instance.unlock();
    }
}

/**
 * @brief 스레드 작업 함수 (No Lock 사용)
 * @param start_val 스레드가 합산할 시작 숫자
 * @param end_val 스레드가 합산할 종료 숫자
 */
void worker_function_no_lock(long long& counter, int start_val, int end_val) {
    for (int i = start_val; i <= end_val; ++i) {
        counter += i; // Critical Section: 실제 숫자를 공유 카운터에 더함
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
    
    // [**1. 정답 계산 (가우스 공식)**]
    long long sum_to_end = (long long)END_NUM * (END_NUM + 1) / 2;
    long long sum_to_start_minus_1 = (long long)(START_NUM - 1) * START_NUM / 2;
    long long expected_result = sum_to_end - sum_to_start_minus_1;


    // [**2. 작업 범위 분배**]
    vector<thread> threads;
    auto start_time = chrono::high_resolution_clock::now();
    
    int current_start = START_NUM;
    
    for (int i = 0; i < num_threads; ++i) {
        // 각 스레드가 처리할 숫자의 개수
        int range_size = NUM_OPERATIONS / num_threads + (i < NUM_OPERATIONS % num_threads ? 1 : 0);
        int current_end = current_start + range_size - 1;

        if (current_end > END_NUM) { 
            current_end = END_NUM;
        }

        if (use_lock) {
            // 락을 사용하는 경우
            threads.emplace_back(worker_function_with_lock<LockType>, ref(lock_instance), ref(shared_counter), current_start, current_end);
        } else {
            // No Lock (락을 사용하지 않는 경우)
            threads.emplace_back(worker_function_no_lock, ref(shared_counter), current_start, current_end);
        }

        current_start = current_end + 1;
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
    
    // [**3. 정확성 검증**]
    bool is_correct = (shared_counter == expected_result);
    
    cout << "Final Sum = " << shared_counter;
    cout << (is_correct ? " (Correct)" : " (Incorrect)");

    if (!is_correct) {
        cout << ", Error = " << abs(shared_counter - expected_result);
    }
    cout << endl;

    return duration.count();
}


// =================================================

int main() {
    
    // 정답을 미리 출력 (1,000,000 부터 5,000,000 까지의 합)
    long long sum_to_end = (long long)END_NUM * (END_NUM + 1) / 2;
    long long sum_to_start_minus_1 = (long long)(START_NUM - 1) * START_NUM / 2;
    long long true_expected_result = sum_to_end - sum_to_start_minus_1;

    cout << "===== Lock Mechanism Performance Evaluation =====" << endl;
    cout << "Target Operation: Summing integers from " << START_NUM << " to " << END_NUM << endl;
    cout << "True Expected Result (Final Sum): " << true_expected_result << endl;


    vector<int> thread_counts = {2, 4, 8, 16, 32};

    for (int num_threads : thread_counts) {
        cout << "\n--- Testing with " << num_threads << " Threads ---" << endl;
        
        // 1. No Lock
        run_experiment<TAS_Lock>("No Lock", num_threads, false);

        // 2. TAS Lock
        run_experiment<TAS_Lock>("TAS Lock", num_threads);
        
        // 3. TTAS Lock
        run_experiment<TTAS_Lock>("TTAS Lock", num_threads);

        // 4. Backoff Lock
        run_experiment<Backoff_Lock>("Backoff Lock", num_threads);
    }

    return 0;
}
