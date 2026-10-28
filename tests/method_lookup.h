#pragma once

#include "../src/mod.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace method_lookup_test {

struct FakeObject {
    void** vtable = nullptr;
};

struct VtableFixture {
    static constexpr size_t method_count = 4;
    void** vtable = nullptr;
    FakeObject first{}, second{};

    VtableFixture() {
        vtable = static_cast<void**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!vtable)
            throw std::runtime_error("allocate fake COM vtable");
        first.vtable = second.vtable = vtable;
    }
    ~VtableFixture() {
        if (vtable)
            VirtualFree(vtable, 0, MEM_RELEASE);
    }
    VtableFixture(const VtableFixture&) = delete;
    VtableFixture& operator=(const VtableFixture&) = delete;
};

inline uintptr_t marker_a() {
    return 0x13579bdf;
}
inline uintptr_t marker_b() {
    return 0x2468ace0;
}
inline uintptr_t marker_c() {
    return 0x55aa55aa;
}

inline void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

inline void register_fixture(VtableFixture& fixture, void* marker) {
    for (size_t i = 0; i < VtableFixture::method_count; ++i)
        fixture.vtable[i] = marker;
}

inline void test() {
    VtableFixture first;
    VtableFixture second;
    register_fixture(first, reinterpret_cast<void*>(&marker_a));
    register_fixture(second, reinterpret_cast<void*>(&marker_b));
    first.vtable[1] = reinterpret_cast<void*>(&marker_c);
    second.vtable[1] = reinterpret_cast<void*>(&marker_c);

    // The pre-hook lookup is a miss. A cache must not retain this unhooked
    // method after hook_method patches the table below.
    check(gd::original_method(&first.first, 0) == reinterpret_cast<void*>(&marker_a),
          "unregistered vtable lookup returns current method");
    gd::hook_method(&first.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_b));
    check(first.vtable[0] == reinterpret_cast<void*>(&marker_b), "first vtable receives replacement");
    check(gd::original_method(&first.first, 0) == reinterpret_cast<void*>(&marker_a),
          "registered lookup returns pre-hook method after prior miss");

    // A second object sharing the same vtable and a second hooked slot must
    // both resolve through the same saved original table.
    gd::hook_method(&first.second, VtableFixture::method_count, 1, reinterpret_cast<void*>(&marker_b));
    check(gd::original_method(&first.second, 0) == reinterpret_cast<void*>(&marker_a),
          "same vtable on second object retains slot zero original");
    check(gd::original_method(&first.second, 1) == reinterpret_cast<void*>(&marker_c),
          "same vtable lookup retains distinct slot one original");

    gd::hook_method(&second.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_c));
    gd::hook_method(&second.second, VtableFixture::method_count, 1, reinterpret_cast<void*>(&marker_a));
    check(gd::original_method(&second.first, 0) == reinterpret_cast<void*>(&marker_b),
          "second vtable resolves its own original method");
    check(gd::original_method(&second.second, 1) == reinterpret_cast<void*>(&marker_c),
          "second vtable resolves a distinct original method index");
    check(gd::original_method(&first.first, 0) == reinterpret_cast<void*>(&marker_a),
          "alternating vtable lookup returns first original again");

    // Grow the registry past likely vector capacities. The cache may retain
    // the heap-owned original slots, but must not retain a Table element address.
    std::array<VtableFixture, 40> more;
    for (size_t i = 0; i < more.size(); ++i) {
        register_fixture(more[i], reinterpret_cast<void*>(&marker_c));
        gd::hook_method(&more[i].first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_a));
        check(gd::original_method(&more[i].first, 0) == reinterpret_cast<void*>(&marker_c),
              "new vtable insertion retains original method");
        check(gd::original_method(&first.first, 0) == reinterpret_cast<void*>(&marker_a),
              "registry growth preserves earlier original-method lookup");
    }

    // Exercise stable positive cache hits on worker threads while unrelated
    // vtable records are inserted. Existing vtables are never modified here.
    std::atomic<bool> start{false}, stop{false}, failed{false};
    std::vector<std::thread> readers;
    std::array<VtableFixture, 16> concurrent_tables;
    for (unsigned thread_index = 0; thread_index < 3; ++thread_index) {
        readers.emplace_back([&, thread_index] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            const bool use_first = (thread_index & 1) == 0;
            FakeObject* object = use_first ? &first.first : &second.first;
            for (unsigned i = 0; !stop.load(std::memory_order_relaxed) || i < 1000; ++i) {
                const unsigned index = i & 1;
                const void* expected =
                    index == 0 ? (use_first ? reinterpret_cast<void*>(&marker_a) : reinterpret_cast<void*>(&marker_b))
                               : reinterpret_cast<void*>(&marker_c);
                if (gd::original_method(object, index) != expected)
                    failed.store(true, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    try {
        for (unsigned i = 0; i < 16; ++i) {
            auto& extra = concurrent_tables[i];
            register_fixture(extra, reinterpret_cast<void*>(&marker_c));
            gd::hook_method(&extra.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_b));
            if (gd::original_method(&extra.first, 0) != reinterpret_cast<void*>(&marker_c))
                failed.store(true, std::memory_order_relaxed);
        }
    } catch (...) {
        stop.store(true, std::memory_order_release);
        for (auto& reader : readers)
            reader.join();
        throw;
    }
    stop.store(true, std::memory_order_release);
    for (auto& reader : readers)
        reader.join();
    check(!failed.load(std::memory_order_relaxed), "cached method lookups survive concurrent registry additions");
}

inline double elapsed_ns(LARGE_INTEGER begin, LARGE_INTEGER end, LARGE_INTEGER frequency) {
    return double(end.QuadPart - begin.QuadPart) * 1.0e9 / double(frequency.QuadPart);
}

inline void benchmark() {
    VtableFixture first, second, third;
    register_fixture(first, reinterpret_cast<void*>(&marker_a));
    register_fixture(second, reinterpret_cast<void*>(&marker_b));
    register_fixture(third, reinterpret_cast<void*>(&marker_c));
    gd::hook_method(&first.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_b));
    gd::hook_method(&second.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_c));
    gd::hook_method(&third.first, VtableFixture::method_count, 0, reinterpret_cast<void*>(&marker_a));

    constexpr uint64_t warmup_calls = 200000;
    constexpr uint64_t measured_calls = 1000000;
    uintptr_t retained = 0;
    for (uint64_t i = 0; i < warmup_calls; ++i)
        retained += reinterpret_cast<uintptr_t>(gd::original_method(&first.first, 0)) + (i & 1);

    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart > 0, "query benchmark clock frequency");
    std::vector<double> same_vtable_ns, interleaved_ns;
    FakeObject* objects[]{&first.first, &second.first, &third.first};
    auto measure_same = [&] {
        LARGE_INTEGER first_tick{}, last_tick{};
        check(QueryPerformanceCounter(&first_tick) != FALSE, "start same-vtable benchmark epoch");
        for (uint64_t i = 0; i < measured_calls; ++i)
            retained += reinterpret_cast<uintptr_t>(gd::original_method(&first.first, 0)) + (i & 1);
        check(QueryPerformanceCounter(&last_tick) != FALSE, "finish same-vtable benchmark epoch");
        return elapsed_ns(first_tick, last_tick, frequency) / double(measured_calls);
    };
    auto measure_interleaved = [&] {
        LARGE_INTEGER first_tick{}, last_tick{};
        check(QueryPerformanceCounter(&first_tick) != FALSE, "start interleaved benchmark epoch");
        for (uint64_t i = 0; i < measured_calls; ++i)
            retained += reinterpret_cast<uintptr_t>(gd::original_method(objects[i % 3], 0)) + (i & 1);
        check(QueryPerformanceCounter(&last_tick) != FALSE, "finish interleaved benchmark epoch");
        return elapsed_ns(first_tick, last_tick, frequency) / double(measured_calls);
    };
    for (unsigned epoch = 0; epoch < 5; ++epoch) {
        if (epoch & 1) {
            interleaved_ns.push_back(measure_interleaved());
            same_vtable_ns.push_back(measure_same());
        } else {
            same_vtable_ns.push_back(measure_same());
            interleaved_ns.push_back(measure_interleaved());
        }
    }
    auto median = [](std::vector<double>& values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    printf("method lookup benchmark: calls/epoch=%llu epochs=5 same_vtable_median_ns=%.2f "
           "interleaved_3_vtables_median_ns=%.2f checksum=%llu\n",
           static_cast<unsigned long long>(measured_calls), median(same_vtable_ns), median(interleaved_ns),
           static_cast<unsigned long long>(retained));
}

} // namespace method_lookup_test
