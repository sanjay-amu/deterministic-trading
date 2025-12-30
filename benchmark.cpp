#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <array>
#include <chrono>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <queue>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================
constexpr size_t QUEUE_SIZE = 8192;
constexpr size_t NUM_ORDERS = 1000000;
constexpr size_t WARMUP_ORDERS = 100000;

// ============================================================================
// ORDER STRUCTURE
// ============================================================================
struct Order {
    uint64_t order_id;
    uint64_t timestamp_in;   // When order entered queue
    uint64_t timestamp_out;  // When order exited queue
    double price;
    int quantity;
    char side;
    
    Order() : order_id(0), timestamp_in(0), timestamp_out(0), 
              price(0.0), quantity(0), side('B') {}
};

// ============================================================================
// HIGH-PRECISION TIMING
// ============================================================================
class Timer {
private:
    double conversion_factor_;
    
public:
    Timer() {
#ifdef __APPLE__
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        conversion_factor_ = static_cast<double>(info.numer) / 
                           static_cast<double>(info.denom);
#else
        conversion_factor_ = 1.0;
#endif
    }
    
    inline uint64_t now() const {
#ifdef __APPLE__
        return mach_absolute_time();
#else
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
#endif
    }
    
    inline double to_nanoseconds(uint64_t ticks) const {
        return ticks * conversion_factor_;
    }
};

// ============================================================================
// LOCK-FREE QUEUE
// ============================================================================
template<typename T, size_t Size>
class LockFreeQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::array<T, Size> buffer_;
    
public:
    LockFreeQueue() = default;
    
    bool try_push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) & (Size - 1);
        
        if (next_head == tail_.load(std::memory_order_acquire))
            return false;
        
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    bool try_pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        
        item = buffer_[tail];
        tail_.store((tail + 1) & (Size - 1), std::memory_order_release);
        return true;
    }
};

// ============================================================================
// MUTEX-BASED QUEUE
// ============================================================================
template<typename T>
class MutexQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    
public:
    bool try_push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        return true;
    }
    
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
            return false;
        item = queue_.front();
        queue_.pop();
        return true;
    }
};

// ============================================================================
// STATISTICS
// ============================================================================
class Statistics {
private:
    std::vector<double> data_;
    
public:
    void add_sample(double value) {
        data_.push_back(value);
    }
    
    void clear() {
        data_.clear();
    }
    
    const std::vector<double>& get_data() const {
        return data_;
    }
    
    double mean() const {
        if (data_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : data_) sum += v;
        return sum / data_.size();
    }
    
    double std_dev() const {
        if (data_.size() < 2) return 0.0;
        double m = mean();
        double sum_sq = 0.0;
        for (double v : data_) {
            double diff = v - m;
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq / (data_.size() - 1));
    }
    
    double percentile(double p) const {
        if (data_.empty()) return 0.0;
        std::vector<double> sorted = data_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(p * sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }
    
    double coefficient_of_variation() const {
        double m = mean();
        return (m > 0) ? (std_dev() / m) : 0.0;
    }
    
    size_t count() const { return data_.size(); }
};

// ============================================================================
// BENCHMARK RUNNER
// ============================================================================
template<typename QueueType>
class BenchmarkRunner {
private:
    Timer timer_;
    QueueType queue_;
    Statistics stats_;
    std::atomic<bool> producer_done_{false};
    
    void producer_thread(size_t num_orders) {
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_real_distribution<double> price_dist(100.0, 200.0);
        std::uniform_int_distribution<int> qty_dist(100, 10000);
        
        for (size_t i = 0; i < num_orders; ++i) {
            Order order;
            order.order_id = i;
            order.price = price_dist(rng);
            order.quantity = qty_dist(rng);
            order.side = (i % 2 == 0) ? 'B' : 'S';
            
            // Timestamp JUST before pushing to queue
            order.timestamp_in = timer_.now();
            
            while (!queue_.try_push(order)) {
                // Spin without yield to maintain timing accuracy
            }
        }
        
        producer_done_.store(true, std::memory_order_release);
    }
    
    void consumer_thread(size_t num_orders, bool is_warmup) {
        Order order;
        size_t processed = 0;
        
        while (processed < num_orders) {
            if (queue_.try_pop(order)) {
                // Timestamp IMMEDIATELY after popping
                order.timestamp_out = timer_.now();
                
                // Calculate latency (queue traversal time)
                double latency_ns = timer_.to_nanoseconds(
                    order.timestamp_out - order.timestamp_in
                );
                
                if (!is_warmup) {
                    stats_.add_sample(latency_ns);
                }
                
                processed++;
            }
            // No yield - keep spinning for accurate timing
        }
    }
    
public:
    void run_benchmark(const std::string& name, bool is_warmup = false) {
        size_t num_orders = is_warmup ? WARMUP_ORDERS : NUM_ORDERS;
        
        if (!is_warmup) {
            stats_.clear();
        }
        
        producer_done_.store(false);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::thread producer(&BenchmarkRunner::producer_thread, this, num_orders);
        std::thread consumer(&BenchmarkRunner::consumer_thread, this, num_orders, is_warmup);
        
        producer.join();
        consumer.join();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time
        );
        
        if (!is_warmup) {
            print_results(name, duration.count());
        }
    }
    
    void print_results(const std::string& name, uint64_t duration_us) {
        std::cout << "\n========================================\n";
        std::cout << "Results: " << name << "\n";
        std::cout << "========================================\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Total Orders:     " << stats_.count() << "\n";
        std::cout << "Duration:         " << duration_us / 1000.0 << " ms\n";
        std::cout << "Throughput:       " 
                  << (stats_.count() * 1000000.0 / duration_us) / 1000000.0 
                  << " MOPS\n";
        std::cout << "\nLatency Distribution (ns):\n";
        std::cout << "  Mean:           " << stats_.mean() << "\n";
        std::cout << "  Std Dev:        " << stats_.std_dev() << "\n";
        std::cout << "  CV:             " << stats_.coefficient_of_variation() << "\n";
        std::cout << "  Median (P50):   " << stats_.percentile(0.50) << "\n";
        std::cout << "  P95:            " << stats_.percentile(0.95) << "\n";
        std::cout << "  P99:            " << stats_.percentile(0.99) << "\n";
        std::cout << "  P99.9:          " << stats_.percentile(0.999) << "\n";
        std::cout << "  P99.99:         " << stats_.percentile(0.9999) << "\n";
        std::cout << "  Max:            " << stats_.percentile(1.0) << "\n";
    }
    
    void export_csv(const std::string& filename) {
        std::ofstream file(filename);
        file << "latency_ns\n";
        file << std::fixed << std::setprecision(2);
        
        // Export raw data directly (much faster)
        const auto& data = stats_.get_data();
        for (double val : data) {
            file << val << "\n";
        }
        
        file.close();
        std::cout << "\nData exported to: " << filename << "\n";
    }
};

// ============================================================================
// MAIN
// ============================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "High-Frequency Trading Benchmark\n";
    std::cout << "ARM64 Lock-Free vs Mutex-Based Queue\n";
    std::cout << "========================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Queue Size:     " << QUEUE_SIZE << "\n";
    std::cout << "  Orders/Run:     " << NUM_ORDERS << "\n";
    std::cout << "  Warmup Orders:  " << WARMUP_ORDERS << "\n";
    std::cout << "========================================\n";
    
    std::cout << "\n⚠️  IMPORTANT: This will run for ~30 seconds\n";
    std::cout << "Close all other applications for best results.\n";
    std::cout << "Press Enter to continue...";
    std::cin.get();
    
    // Baseline
    {
        std::cout << "\n[1/2] Running BASELINE (Mutex-Based Queue)...\n";
        BenchmarkRunner<MutexQueue<Order>> baseline;
        
        std::cout << "  Warming up...\n";
        baseline.run_benchmark("Warmup", true);
        
        std::cout << "  Running benchmark...\n";
        baseline.run_benchmark("Mutex-Based Queue");
        baseline.export_csv("baseline_results.csv");
    }
    
    // Lock-Free
    {
        std::cout << "\n[2/2] Running PROPOSED (Lock-Free Queue)...\n";
        BenchmarkRunner<LockFreeQueue<Order, QUEUE_SIZE>> optimized;
        
        std::cout << "  Warming up...\n";
        optimized.run_benchmark("Warmup", true);
        
        std::cout << "  Running benchmark...\n";
        optimized.run_benchmark("Lock-Free ARM64 Queue");
        optimized.export_csv("lockfree_results.csv");
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Benchmark Complete!\n";
    std::cout << "========================================\n";
    
    return 0;
}
