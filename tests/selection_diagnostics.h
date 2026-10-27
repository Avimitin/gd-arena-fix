#pragma once
#include "../src/selection_diagnostics.h"

static int64_t __fastcall selection_update_observer(int64_t a, uint64_t b, uint64_t c) {
    check(a == -123 && b == 456 && c == 789, "selection update preserves arguments");
    return -987;
}
static int64_t __fastcall selection_request_observer(int direction, int sound) {
    check(direction == -2 && sound == 1, "selection request preserves arguments");
    return 123;
}
static void selection_diagnostics_test() {
    namespace layout = gd::target::layout::selection;
    const auto& p = gd::target::get();
    timing_commit(p.selection_owner, sizeof(void*));
    timing_commit(p.selection_index, sizeof(int));
    timing_commit(p.selection_pending, 2 * sizeof(int));
    auto& global_owner = timing_memory<void*>(p.selection_owner);
    const auto saved_owner = global_owner;
    std::vector<uint8_t> owner(layout::owner_size), ui(layout::ui_fixture_size);
    global_owner = owner.data();
    using Request = int64_t(__fastcall*)(int, int);
    using Update = int64_t(__fastcall*)(int64_t, uint64_t, uint64_t);
    const auto hooks = gd::selection_diagnostic_hooks();
    const auto request = reinterpret_cast<Request>(gd::at(p.selection_request.address));
    const auto native = reinterpret_cast<Request>(*hooks[0].original);
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        std::fill(owner.begin(), owner.end(), uint8_t(0));
        std::fill(ui.begin(), ui.end(), uint8_t(0));
        anim_field<void*>(owner.data(), layout::main_ui) = ui.data();
        ui[layout::valid] = 1;
        int direction = 1;
        switch (scenario) {
        case 0:
            anim_field<int>(owner.data(), layout::lock1) = 1;
            break;
        case 1:
            anim_field<void*>(owner.data(), layout::main_ui) = nullptr;
            break;
        case 2:
            anim_field<int>(owner.data(), layout::lock2) = 1;
            break;
        case 3:
            ui[layout::valid] = 0;
            break;
        case 4:
            anim_field<int>(ui.data(), layout::phase) = 600;
            break;
        case 5:
            anim_field<int>(ui.data(), layout::direction) = -1;
            break;
        case 6:
            direction = 0;
            ui[layout::continuation] = 1;
            break;
        }
        const auto initial_owner = owner, initial_ui = ui;
        const auto expected_return = native(direction, 1);
        const auto expected_owner = owner, expected_ui = ui;
        std::copy(initial_owner.begin(), initial_owner.end(), owner.begin());
        std::copy(initial_ui.begin(), initial_ui.end(), ui.begin());
        check(request(direction, 1) == expected_return, "observed native request returns unchanged");
        check(owner == expected_owner && ui == expected_ui,
              "diagnostics preserve native locks, rejection paths and continuation/release side effects");
    }
    const auto saved_request = *hooks[0].original, saved_update = *hooks[1].original;
    *hooks[0].original = reinterpret_cast<void*>(selection_request_observer);
    *hooks[1].original = reinterpret_cast<void*>(selection_update_observer);
    const auto before_owner = owner, before_ui = ui;
    check(request(-2, 1) == 123, "request return value passes through without narrowing");
    check(reinterpret_cast<Update>(gd::at(p.selection_update.address))(-123, 456, 789) == -987,
          "update return value passes through unchanged");
    check(owner == before_owner && ui == before_ui, "diagnostic wrappers never write native state");
    *hooks[0].original = saved_request;
    *hooks[1].original = saved_update;
    global_owner = saved_owner;
    puts("PASS: selection probes preserve native rejection/release behavior, arguments, returns and memory");
}
