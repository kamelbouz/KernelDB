#include "kerneldb/storage.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace kerneldb;
using Clock = std::chrono::steady_clock;

namespace {

void bench_one(int N) {
    auto dir = std::filesystem::temp_directory_path() /
               ("kerneldb_bench_" + std::to_string(N));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    StorageManager sm(dir, 8192);
    TableSchema schema{"bench", {
        {"id", DataType::Integer, 0},
        {"name", DataType::Varchar, 50}
    }};

    sm.create_table(schema);

    // ---------- INSERT ----------
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) {
        sm.insert(schema, Row{{
            std::int64_t{i},
            std::string{"user_" + std::to_string(i)}
        }});
    }
    auto t1 = Clock::now();

    // ---------- SCAN ----------
    auto rows = sm.scan(schema);
    auto t2 = Clock::now();

    // ---------- Re-SCAN (cache chaud) ----------
    auto rows2 = sm.scan(schema);
    auto t3 = Clock::now();

    auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto scan_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto scan2_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    std::printf("N=%6d  insert=%6lld ms  scan=%6lld ms  scan2=%6lld ms  rows=%zu/%zu  pages=%u\n",
                N,
                (long long)insert_ms,
                (long long)scan_ms,
                (long long)scan2_ms,
                rows.size(),
                rows2.size(),
                sm.page_count("bench"));

    sm.flush_all();
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    std::printf("=== KernelDB storage benchmark ===\n");
    std::printf("page_size = 8192\n\n");

    bench_one(1000);
    bench_one(10000);

    std::printf("\nDone.\n");
    return 0;
}

