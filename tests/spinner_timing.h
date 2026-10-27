#pragma once
static uint64_t __fastcall spinner_gpu_stub() {
    return 0;
}
static void native_spinner_timing_test() {
    const auto& p = gd::target::get();
    timing_commit(p.spinner_age, 16);
    timing_commit(p.spinner_vertices, p.spinner_buffer_stride);
    timing_commit(p.spinner_buffer_index, 4);
    timing_commit(p.spinner_textures, 7 * sizeof(void*));
    timing_memory<uint32_t>(p.spinner_buffer_index) = 0;
    memset(gd::at(p.spinner_textures), 0, 7 * sizeof(void*));
    TimingRedirect setup(p.spinner_setup, reinterpret_cast<void*>(spinner_gpu_stub));
    TimingRedirect submit(p.spinner_submit, reinterpret_cast<void*>(spinner_gpu_stub));
    const auto draw = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.spinner_draw));
    auto& age = timing_memory<uint32_t>(p.spinner_age);
    auto& alpha = timing_memory<int32_t>(p.spinner_alpha);
    auto& visible = timing_memory<uint8_t>(p.spinner_visible);
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        age = 0;
        alpha = 0;
        visible = 1;
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        draw();
        check(alpha == 8, "loading spinner appears immediately");
        for (unsigned frame = 0; frame < hz; ++frame) {
            gd::begin_timing_frame(1.0 / hz);
            draw();
            draw();
        }
        check(age == 60 && alpha == 255, "loading spinner atlas and opacity follow elapsed time");
        visible = 0;
        gd::begin_timing_frame(.25);
        draw();
        draw();
        check(alpha == 135, "loading spinner retains fractional fade-out time");
        gd::begin_timing_frame(3);
        draw();
        check(alpha == 0, "loading spinner fade finishes across a long stall");
    }
    gd::end_scene_frame();
    puts("PASS: native loading spinner atlas, fractional opacity, duplicate calls and stalled fade");
}
