#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <vector>

// All supported-game addresses, instruction guards, replacement bytes and
// native layout offsets live here. Addresses are preferred VAs for readable
// comparison with IDA; at() relocates using the selected profile's image base.
namespace gd::target {
struct Bytes {
    std::array<uint8_t, 40> storage{};
    size_t length = 0;
    constexpr Bytes(std::initializer_list<uint8_t> values) : length(values.size()) {
        if (length > storage.size())
            throw "instruction signature exceeds capacity";
        size_t i = 0;
        for (auto value : values)
            storage[i++] = value;
    }
    std::vector<uint8_t> copy() const {
        return {storage.begin(), storage.begin() + length};
    }
};
struct Site {
    uintptr_t address;
    Bytes signature;
};
struct Patch {
    uintptr_t address;
    Bytes before, after;
};
struct EventSite {
    Site site;
    uint32_t tick;
};
struct BranchEvent {
    Site site;
    uint32_t tick;
    uintptr_t yes, no;
};

enum class CounterForm {
    IncMemory,
    IncEax,
    IncEcx,
    IncR8,
    LeaEaxEcx,
    LeaEaxEdx,
    IncEaxSetEbx,
    IncEaxCmpEsi2,
    IncEcxCmp60,
    DecEax,
    DecEaxEcx,
    LeaEaxR8,
    IncMemoryLoadEax,
    IncMemoryRestoreRbp,
    IncMemoryCall,
    IncMemoryCmp,
    IncR8LoadRdx,
    IncEcxStoreR8
};
enum class CounterBase { Global, Rdi, Rbx, R13, R14 };
struct CounterStep {
    Site site;
    uintptr_t value, identity;
    CounterForm form;
    CounterBase base = CounterBase::Global;
    uintptr_t auxiliary = 0;
};

enum class EntryRegister { None, Rax, Rcx, R8, Rbx, Rsi, Rdi, R14, R15, Rbp };
enum class EntryTail { None, MagicEax, BeforeDecR8, IndexRdi, CmpEdi, SaveR12CmpEdi };
struct EntryStep {
    Site site;
    EntryRegister base;
    ptrdiff_t offset;
    EntryRegister output;
    EntryTail tail = EntryTail::None;
};
struct EntryDisplay {
    Site site;
    EntryRegister player;
    uintptr_t skip;
};

enum class FrameLabelRegister { Rax, Rsi, Rdi, R14 };
enum class FrameEventTail { None, StackRbx, ZeroR8, Call };
struct FrameEventSite {
    Site site;
    uintptr_t object, no, call;
    FrameLabelRegister label;
    FrameEventTail tail;
    size_t stack_offset = 0;
};

enum class FrameQueryForm { RestoreRsi, IndirectLabel, RegisterLabel };
struct FrameQuerySite {
    Site site;
    size_t restore_rsi = 0;
    FrameQueryForm form = FrameQueryForm::RestoreRsi;
    uintptr_t no = 0;
    ptrdiff_t label_offset = 0;
};

struct Profile {
    const char* name;
    std::string_view sha256;
    uintptr_t preferred_base;
    Site present, frame, audio, song_filter;
    std::array<EntryStep, 30> entry_steps;
    std::array<EntryDisplay, 11> entry_displays;
    Site entry_warning;
    uintptr_t entry_owner, entry_display, entry_warning_skip, entry_confirm_update, entry_confirm_ticks,
        entry_touch_enabled, entry_input;
    Site menu_update0, menu_update1, menu_update2, menu_set, menu_clear, play_timer;
    Site animation_update, animation_sample, animation_draw, sprite_update, gameplay_visual, dm_sprites, gf_sprites;
    Site note_batch, note_position, note_draw;
    Site font_projection_constants;
    uintptr_t font_projection_constants_return;
    Site cursor_call;
    Patch frame_wait, play_decrement, live2d_menu, live2d_alternate, live2d_inline;
    Patch side_copy_left, side_copy_right, side_composite_copy;
    uintptr_t raw_cursor, afp_return, afp_import;
    uintptr_t audio_format, audio_main, audio_alternate, song_previous, song_raw, song_output, scene_pointer;
    uintptr_t menu_subtick, menu_seconds, menu_paused, play_mode, play_ticks, play_paused;
    uintptr_t game_mode_pointer, dm_effects, dm_judgments, note_options;
    // Supporting native routines/globals used by the isolated game-image tests.
    uintptr_t song_reset;
    Site filter_division0, filter_division1;
    uintptr_t constant_60, constant_afp;
    uintptr_t menu_previous, menu_hold, menu_ui0, menu_ui1, service_pause, debug_pause, countdown_beep, keysound;
    uintptr_t audio_states, audio_object, audio_timestamp, audio_epoch, audio_clock_import;
    uintptr_t voice_tree, voice_channels, song_state;
    uintptr_t memset_import, menu_character_builder, alternate_character_builder, menu_character_vector,
        saved_characters;
    uintptr_t sprite_records, gf_sprite_records, nested_sprite_record;
    uintptr_t audit_song_scene, audit_drum_worker;
    Site song_start, lights, bookkeeping;
    Site light_pattern;
    Patch song_delay_increment, bookkeeping_cadence;
    uintptr_t song_start_state, song_start_delay, session_age, report_age, color_phase;
    uintptr_t light_age, light_color, light_brightness, bookkeeping_return;
    uintptr_t light_mode, light_enabled, light_device, light_rgb_output, light_pattern_output, light_brightness_output;
    uintptr_t light_pattern_enabled, light_pattern_mode, light_pattern_index;
    Site secondary_update, secondary_at20, secondary_age_step, secondary_countdown0, secondary_countdown1;
    uintptr_t secondary_at20_yes, secondary_at20_no;
    uintptr_t secondary_player_base, secondary_service_mode, secondary_input_object, secondary_exit;
    std::array<uintptr_t, 6> secondary_inputs;
    Site lobby_update, match_update;
    std::array<Patch, 5> network_steps;
    uintptr_t lobby_active, lobby_state, lobby_age, match_active, match_state, match_age, chat_age, chat_send_age;
    uintptr_t network_connection, network_nodes, network_restart, network_break;
    Site secondary_at20_body;
    std::array<Site, 10> preview_steps;
    Site preview_countdown;
    uintptr_t preview_event, preview_next;
    std::array<Site, 3> preview_samples;
    Site hit_effect_short, hit_effect_long;
    std::array<uintptr_t, 3> audit_scene_tables;
    std::array<uintptr_t, 5> audit_scene_records;
    std::array<uintptr_t, 4> audit_scene_factories;
    Site tally_update, tally_step, tally_numbers;
    std::array<EventSite, 6> tally_events;
    uintptr_t tally_age, tally_state, tally_enabled, tally_number_next;
    uintptr_t tally_targets, tally_values, tally_sound_options, player_active;
    Site option_repeat;
    std::array<Site, 3> speed_steps;
    Site speed_sample;
    uintptr_t option_object, option_keys, option_modifiers, option_speed_update, option_speed_value;
    std::array<Site, 4> controller_time_reads;
    std::array<Site, 2> sprite_inline;
    uintptr_t dm_sprite_next, gf_sprite_next;
    Site result_begin, result_end, result_sound_step;
    std::array<EventSite, 13> result_events;
    std::array<uintptr_t, 4> result_event_yes, result_event_no;
    std::array<Site, 3> result_periods, result_countdowns;
    std::array<Site, 4> result_countups;
    uintptr_t result_age, result_state, result_color, result_color_mode, result_update;
    uintptr_t result_request, result_request_active, overlay_age, result_request_code;
    uintptr_t result_request_kind, result_countup0, result_countup1, result_gauges, result_gauge_targets,
        result_sound_queue;
    uintptr_t result_skip_input, result_sound_enqueue, result_gauge_update, result_countup_update0,
        result_countup_update1;
    uintptr_t result_countdown_update, result_gauge_dm_goal, result_gauge_gf_goal;
    uintptr_t result_color_yes, result_color_no;
    uintptr_t result_guitar_flag, result_game_stage;
    Site battle_begin, battle_step, battle_end, battle_cue_sequence, battle_roulette, battle_awards, battle_exit_step;
    std::array<BranchEvent, 10> battle_events;
    std::array<Site, 2> battle_numbers, battle_number_cues;
    std::array<Site, 3> battle_countdowns, battle_countdown_cues;
    uintptr_t battle_update, battle_age, battle_state, battle_exit_age, battle_outer_state;
    uintptr_t battle_dispatch, battle_dispatch_default, battle_cue_yes, battle_cue_no;
    std::array<uintptr_t, 2> battle_number_next, battle_number_cue_yes, battle_number_cue_no;
    uintptr_t battle_roulette_yes, battle_roulette_no, battle_roulette_after;
    uintptr_t battle_roulette_goal, battle_roulette_start, battle_roulette_value;
    uintptr_t battle_awards_stock, battle_awards_sound, battle_awards_next, battle_player, battle_awards_values,
        battle_awards_pending;
    uintptr_t battle_counter0, battle_scores, battle_countdown_last_yes, battle_countdown_last_no;
    uintptr_t battle_intro_idle, battle_intro_late, battle_intro_object, animation_label, label_in;
    std::array<uintptr_t, 3> battle_renderers;
    Site gf_effect_step, combo_effect_step;
    Site gf_number_step;
    uintptr_t gf_number_next, gf_number_native_start;
    Site shutter_fade, end_event, end_step, hud_phase_step, hud_countdown_step;
    std::array<Site, 3> effect_age_steps;
    std::array<uintptr_t, 3> effect_ages;
    uintptr_t gf_effect_age, gf_effect_next, combo_effect_age, shutter_object, shutter_update, shutter_fades,
        shutter_colors;
    uintptr_t end_update, end_age, end_enabled, end_gauge, hud_age, hud_countdowns;
    std::array<uintptr_t, 2> end_inputs;
    uintptr_t shutter_objects, end_object, animation_label_lookup;
    Site gf_overlay_hold, gf_overlay_hit;
    std::array<Site, 3> gf_overlay_counters;
    std::array<Site, 4> gf_overlay_fades;
    uintptr_t gf_overlay_update, gf_overlay_records, gf_overlay_hold_value, gf_overlay_hold_alpha,
        gf_overlay_hold_identity, gf_overlay_hold_next;
    std::array<uintptr_t, 3> gf_overlay_counter_values, gf_overlay_counter_identities;
    std::array<uintptr_t, 2> gf_overlay_alphas, gf_overlay_fade_next;
    uintptr_t gf_overlay_counter_object, gf_overlay_hit_value, gf_overlay_hit_next, gf_overlay_step, gf_overlay_scratch;
    std::array<uintptr_t, 3> gf_overlay_controls;
    Site song_load_timeout, song_disconnect_age, song_phase_age, song_ready_gate;
    std::array<Site, 4> song_local_waits;
    std::array<Site, 2> song_hold_steps, song_exit_steps;
    Site song_post_delay, song_progress_delay, song_end_timeout, song_network_age, song_visual_age, song_intro_age;
    uintptr_t song_controller_object, song_exit_age, song_post_age, song_progress_age, song_network_wait,
        song_visual_phase, song_intro_wait;
    uintptr_t song_ready_yes, song_ready_no, song_controller_update;
    Site judgment_cooldown_step, judgment_hold_step, judgment_feedback_step, service_light_step;
    std::array<Site, 3> service_dialog_steps, service_io_steps;
    std::array<Site, 2> service_word_steps, service_poll_steps;
    uintptr_t judgment_cooldown, judgment_hold, service_light_age, service_dialog_age, service_dialog_state,
        service_dialog_resource;
    std::array<uintptr_t, 2> service_words, service_dialog_updates, service_poll_updates, service_poll_yes,
        service_poll_no, service_dialog_limits;
    uintptr_t service_poll_age, service_poll_disabled, service_poll_enabled, service_poll_output, service_io_next;
    std::array<CounterStep, 74> system_counters;
    // Native scene fixtures also use these centralized support addresses.
    uintptr_t handshake_update, handshake_state, handshake_age;
    uintptr_t request_scene_update, request_scene_active, request_scene_state, request_scene_age, request_scene_total,
        request_scene_status;
    uintptr_t hold_scene_update, hold_scene_controller, hold_scene_input, scene_abort;
    uintptr_t service_gesture, service_gesture_state, service_gesture_age, service_gesture_edges;
    Site tutorial_begin, tutorial_end, tutorial_phase_step, tutorial_gauge;
    std::array<EventSite, 30> tutorial_events;
    std::array<Site, 6> tutorial_countdowns;
    uintptr_t tutorial_state, tutorial_remaining, tutorial_position, tutorial_pause, tutorial_gauge_next;
    uintptr_t tutorial_update, tutorial_narration, tutorial_sound, tutorial_players, tutorial_input_delay;
    std::array<size_t, 6> tutorial_countdown_offsets;
    size_t tutorial_player_stride, tutorial_gauge_target_distance, tutorial_next_effect_distance;
    std::array<uint32_t, 2> tutorial_pause_states, tutorial_pause_boundaries;
    Site tutorial_practice_cue;
    uintptr_t tutorial_practice_cue_yes, tutorial_practice_cue_no, tutorial_practice_cue_native;
    size_t tutorial_practice_saved_r13;
    Site summary_step, summary_event;
    std::array<Site, 8> summary_periods;
    uintptr_t summary_age, summary_state, summary_overall, summary_event_yes, summary_event_no;
    uintptr_t summary_update, summary_sound, summary_skip, summary_row_display, summary_digit_display;
    uintptr_t summary_rows, summary_row_targets, summary_total_target, summary_player;
    size_t summary_row_stride;
    Site qc_schedule, card_error_step, card_error_period;
    std::array<Site, 2> qc_native_steps;
    uintptr_t qc_age, qc_phase, qc_brightness, qc_due, qc_idle, qc_update, qc_sound_ready, qc_audio_state;
    uintptr_t card_error_age, card_error_due, card_error_idle;
    Site mini_age;
    Site mini_player_atlas;
    Site mini_enemy_atlas;
    Site mini_debris_atlas;
    Site mini_debris_motion;
    Site mini_bursts;
    Site mini_banner;
    Site mini_hurt;
    Site mini_fall;
    Site mini_player_velocity;
    Site mini_player_motion;
    Site mini_enemy_intro;
    Site mini_enemy_spawn;
    Site mini_enemy_death;
    Site mini_particle0;
    Site mini_particle1;
    Site mini_particle2;
    Site mini_particle3;
    Site mini_spin;
    uintptr_t mini_player_atlas_next;
    uintptr_t mini_enemy_atlas_next;
    uintptr_t mini_debris_atlas_next;
    uintptr_t mini_bursts_next;
    uintptr_t mini_banner_next;
    uintptr_t mini_hurt_next;
    uintptr_t mini_hurt_wait;
    uintptr_t mini_fall_next;
    uintptr_t mini_enemy_next;
    uintptr_t mini_enemy_death_due;
    uintptr_t mini_enemy_death_next;
    uintptr_t mini_particle2_next;
    uintptr_t mini_particle3_next;
    uintptr_t mini_spin_next;
    uintptr_t mini_update;
    uintptr_t mini_player_update;
    uintptr_t mini_enemy_update;
    uintptr_t mini_particle_update;
    uintptr_t mini_debris_speed;
    uintptr_t mini_particle_speed;
    uintptr_t mini_particle_target;
    uintptr_t mini_particle_divisor;
    uintptr_t mini_spin_low;
    uintptr_t mini_spin_high;
    uintptr_t mini_banner_scale;
    uintptr_t mini_banner_rotation;
    uintptr_t mini_gravity;
    uintptr_t mini_floor;
    uintptr_t mini_exit_floor;
    uintptr_t mini_enemy_bound;
    uintptr_t mini_enemy_drift;
    uintptr_t mini_enemy_target;
    uintptr_t mini_enemy_reset_x;
    uintptr_t mini_game_kind;
    uintptr_t mini_input_enabled;
    uintptr_t mini_shot_input;
    Site card_timeout_step, card_alternate_step;
    ptrdiff_t card_timeout_offset;
    uintptr_t card_update, card_pump, card_error_update, card_slots, card_count;
    size_t card_slot_stride, card_slot_age;
    std::array<Site, 8> arena_steps;
    std::array<bool, 8> arena_step_eax;
    Site arena_event, arena_gesture0, arena_gesture1, arena_voice_delay;
    uintptr_t arena_age, arena_state, arena_gesture_age0, arena_gesture_age1, arena_gesture_count0;
    std::array<FrameEventSite, 13> frame_events;
    Site spinner_fade, spinner_step;
    uintptr_t spinner_age, spinner_alpha, spinner_visible, spinner_draw, spinner_draw_next;
    uintptr_t spinner_submit, spinner_setup, spinner_vertices, spinner_buffer_index, spinner_textures;
    size_t spinner_buffer_stride;
    Site dm_tutorial_begin, dm_tutorial_end, dm_tutorial_step, dm_tutorial_position_step, dm_tutorial_gauge,
        dm_tutorial_effect;
    std::array<EventSite, 35> dm_tutorial_events;
    std::array<Site, 7> dm_tutorial_periods;
    std::array<Site, 2> dm_tutorial_delays;
    std::array<Site, 3> dm_tutorial_demo;
    uintptr_t dm_tutorial_state, dm_tutorial_remaining, dm_tutorial_position, dm_tutorial_pause, dm_tutorial_phase;
    uintptr_t dm_tutorial_input_delay, dm_tutorial_feedback_delay, dm_tutorial_gauge_value, dm_tutorial_gauge_target;
    uintptr_t dm_tutorial_gauge_next, dm_tutorial_effect_next, dm_tutorial_end_next, dm_tutorial_input_next;
    uintptr_t dm_tutorial_update, dm_tutorial_narration, dm_tutorial_miss, dm_tutorial_objects, dm_tutorial_active,
        dm_tutorial_lights;
    uintptr_t dm_tutorial_clip_frame;
    std::array<uintptr_t, 9> dm_tutorial_inputs;
    ptrdiff_t dm_tutorial_effect_object_offset;
    Site result_panel_queue;
    uintptr_t result_panel_update, result_panel_state, result_panel_age, result_panel_phase, result_panel_queue_data;
    Site lifecycle_wait_begin, lifecycle_init_step, lifecycle_init_event;
    std::array<Site, 2> lifecycle_wait_steps, card_release_steps;
    uintptr_t lifecycle_wait_age, lifecycle_wait_state, lifecycle_wait_update;
    uintptr_t lifecycle_init_age, card_release_enabled;
    ptrdiff_t card_release_state_offset;
    Site card_release_end;
    uintptr_t card_release_object, card_release_age, card_release_update;
    Site selection_frame_cue;
    uintptr_t options_preview_update, options_preview_data, options_preview_seek;
    uintptr_t selection_frame_object, selection_frame_no;
    size_t options_preview_stride, options_preview_scroll_offset, options_preview_scroll_count,
        options_preview_repeat_offset, options_preview_repeat_count;
    uint32_t selection_frame_sound;
    std::array<FrameQuerySite, 4> frame_queries;
    std::array<Site, 2> overlay_age_steps;
    uintptr_t overlay_open_query, overlay_close_query;
    // Passive probes for the shared main/touchscreen song selection controller.
    Site selection_request, selection_update;
    uintptr_t selection_owner, selection_index, selection_pending;
    Site selection_ready;
    uintptr_t selection_motion;
    uint32_t selection_ready_frame;
    const char *selection_idle_label, *selection_move_label;
    Site category_ready;
    uintptr_t category_motion;
};

inline constexpr Profile delta{
    .name = "GITADORA Delta M32-007-2025122301",
    .sha256 = "0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c",
    .preferred_base = 0x180000000,
    .present = {0x180420360, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30}},
    .frame = {0x1801A3910, {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x40, 0x18, 0x48, 0x89, 0x48, 0x08}},
    .audio = {0x1801B60F0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10}},
    .song_filter = {0x180286EC0, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20}},
    .entry_steps = {{
        {{0x1802BACD7, {0xff, 0xc9, 0x89, 0x0b, 0xb8, 0x89, 0x88, 0x88, 0x88}},
         EntryRegister::Rbx,
         0x0,
         EntryRegister::Rcx,
         EntryTail::MagicEax},
        {{0x1802BBACA, {0x46, 0x8b, 0x84, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::R8,
         EntryTail::BeforeDecR8},
        {{0x1802BBE15, {0x46, 0x8b, 0x84, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::R8,
         EntryTail::BeforeDecR8},
        {{0x1802BC59A, {0xff, 0xc9, 0x89, 0x4f, 0x04}}, EntryRegister::Rdi, 0x4, EntryRegister::Rcx, EntryTail::None},
        {{0x1802BC858, {0xff, 0xc9, 0x89, 0x4f, 0x04}}, EntryRegister::Rdi, 0x4, EntryRegister::Rcx, EntryTail::None},
        {{0x1802BD7D2, {0x41, 0xff, 0xc8, 0x44, 0x89, 0x03}},
         EntryRegister::Rbx,
         0x0,
         EntryRegister::R8,
         EntryTail::None},
        {{0x1802BDC36, {0xff, 0x0b, 0xb8, 0x89, 0x88, 0x88, 0x88}},
         EntryRegister::Rbx,
         0x0,
         EntryRegister::None,
         EntryTail::MagicEax},
        {{0x1802BF909, {0x41, 0xff, 0xc8, 0x44, 0x89, 0x06}},
         EntryRegister::Rsi,
         0x0,
         EntryRegister::R8,
         EntryTail::None},
        {{0x1802BFE10, {0xff, 0x0c, 0xaf, 0x48, 0x8d, 0x3c, 0xaf}},
         EntryRegister::Rdi,
         0x0,
         EntryRegister::None,
         EntryTail::IndexRdi},
        {{0x1802C1BE8, {0xff, 0x4e, 0x04, 0x39, 0x7e, 0x04}},
         EntryRegister::Rsi,
         0x4,
         EntryRegister::None,
         EntryTail::CmpEdi},
        {{0x1802C1F16, {0x44, 0x8b, 0xe0, 0xff, 0x0e, 0x39, 0x3e}},
         EntryRegister::Rsi,
         0x0,
         EntryRegister::None,
         EntryTail::SaveR12CmpEdi},
        {{0x1802BB6BA, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BB6CD, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BB84C, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BB87C, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BB8A5, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BB8FF, {0x42, 0xff, 0x8c, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::None,
         EntryTail::None},
        {{0x1802BBA93, {0xff, 0xc8, 0x42, 0x89, 0x84, 0x3f, 0xc0, 0xc9, 0x08, 0x30}},
         EntryRegister::R14,
         -0x4,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802BCC3E, {0xff, 0xc8, 0x89, 0x47, 0x08}}, EntryRegister::Rdi, 0x8, EntryRegister::Rax, EntryTail::None},
        {{0x1802BD099, {0xff, 0xc8, 0x89, 0x47, 0x08}}, EntryRegister::Rdi, 0x8, EntryRegister::Rax, EntryTail::None},
        {{0x1802BDA4C, {0xff, 0xc8, 0x89, 0x43, 0x04}}, EntryRegister::Rbx, 0x4, EntryRegister::Rax, EntryTail::None},
        {{0x1802BDB69, {0xff, 0xc8, 0x89, 0x43, 0x04}}, EntryRegister::Rbx, 0x4, EntryRegister::Rax, EntryTail::None},
        {{0x1802BDBA4, {0xff, 0xc8, 0x89, 0x43, 0x04}}, EntryRegister::Rbx, 0x4, EntryRegister::Rax, EntryTail::None},
        {{0x1802BF764, {0xff, 0xc8, 0x89, 0x86, 0x1c, 0x03, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x31c,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802BF837, {0xff, 0xc8, 0x89, 0x86, 0x1c, 0x03, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x31c,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802C0CA0, {0xff, 0xc8, 0x89, 0x86, 0xac, 0x01, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x1ac,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802C123F, {0xff, 0xc8, 0x89, 0x86, 0xac, 0x01, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x1ac,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802C209E, {0xff, 0xc8, 0x89, 0x86, 0xac, 0x01, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x1ac,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802C20D2, {0xff, 0xc8, 0x89, 0x86, 0xac, 0x01, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x1ac,
         EntryRegister::Rax,
         EntryTail::None},
        {{0x1802C1746, {0xff, 0x8e, 0xac, 0x01, 0x00, 0x00}},
         EntryRegister::Rsi,
         0x1ac,
         EntryRegister::None,
         EntryTail::None},
    }},
    .entry_displays = {{
        {{0x1802BACF3, {0x83, 0xf9, 0x3b, 0x75, 0x07}}, EntryRegister::Rsi, 0x1802BACFF},
        {{0x1802BBAFA, {0x83, 0xf9, 0x3b, 0x75, 0x0f}}, EntryRegister::Rsi, 0x1802BBB0E},
        {{0x1802BBE45, {0x83, 0xf9, 0x3b, 0x75, 0x0f}}, EntryRegister::Rsi, 0x1802BBE59},
        {{0x1802BC5B7, {0x83, 0xf9, 0x3b, 0x75, 0x07}}, EntryRegister::Rsi, 0x1802BC5C3},
        {{0x1802BC875, {0x83, 0xf9, 0x3b, 0x75, 0x07}}, EntryRegister::Rsi, 0x1802BC881},
        {{0x1802BD7F5, {0x83, 0xf9, 0x3b, 0x75, 0x0a}}, EntryRegister::Rsi, 0x1802BD804},
        {{0x1802BDC58, {0x83, 0xf9, 0x3b, 0x75, 0x1f}}, EntryRegister::Rsi, 0x1802BDC7C},
        {{0x1802BF92C, {0x83, 0xf9, 0x3b, 0x75, 0x0b}}, EntryRegister::R14, 0x1802BF93C},
        {{0x1802BFE37, {0x83, 0xf9, 0x3b, 0x75, 0x1c}}, EntryRegister::Rbp, 0x1802BFE58},
        {{0x1802C1CB1, {0x83, 0xf9, 0x3b, 0x75, 0x08}}, EntryRegister::R15, 0x1802C1CBE},
        {{0x1802C2064, {0x83, 0xf9, 0x3b, 0x75, 0x08}}, EntryRegister::R15, 0x1802C2071},
    }},
    .entry_warning = {0x1802C1C75, {0x81, 0x7e, 0x04, 0xb0, 0x04, 0x00, 0x00, 0x75, 0x0b}},
    .entry_owner = 0x1AFFC9108,
    .entry_display = 0x1802C6650,
    .entry_warning_skip = 0x1802C1C89,
    .entry_confirm_update = 0x1802BFDE0,
    .entry_confirm_ticks = 0x1B008CB50,
    .entry_touch_enabled = 0x182F4A204,
    .entry_input = 0x18023F730,
    .menu_update0 = {0x180280FE0, {0x48, 0x83, 0xec, 0x28, 0xb9, 1, 0, 0, 0}},
    .menu_update1 = {0x180281170, {0x48, 0x83, 0xec, 0x28, 0xb9, 1, 0, 0, 0}},
    .menu_update2 = {0x1802D46E0, {0x48, 0x83, 0xec, 0x28, 0xb9, 1, 0, 0, 0}},
    .menu_set = {0x180281320, {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x8d, 0x41, 1}},
    .menu_clear = {0x180281440, {0x33, 0xd2, 0x48, 0x8d, 0x0d, 0x77, 0x9e, 0xe0, 0x2f}},
    .play_timer = {0x1801F3170, {0x48, 0x83, 0xec, 0x28, 0x48, 0x89, 0x7c, 0x24, 0x20}},
    .animation_update = {0x180185E70, {0x48, 0x85, 0xc9, 0x0f, 0x84, 0x1f, 0x02, 0, 0}},
    .animation_sample = {0x1801869D0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30}},
    .animation_draw = {0x180185230, {0x48, 0x8b, 0x81, 0xa8, 0, 0, 0, 0x48, 0x8b, 0xd1}},
    .sprite_update = {0x1803B8120, {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0x09, 0x4c, 0x8b, 0xc1}},
    .gameplay_visual = {0x180289EF0, {0x48, 0x83, 0xec, 0x38, 0x83, 0x3d, 0x7d, 0xdc, 0x69, 0x02, 0x03}},
    .dm_sprites = {0x1803B81C0, {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8d, 0x05, 0x4f, 0xce, 0x53, 0x00}},
    .gf_sprites = {0x1803B8290, {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x0d, 0xd5, 0xd0, 0x53, 0x00}},
    .note_batch = {0x1803C06E0, {0x4c, 0x8b, 0xdc, 0x55, 0x57, 0x41, 0x57, 0x48, 0x81, 0xec, 0xa0, 0, 0, 0}},
    .note_position = {0x1803C0350,
                      {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x68, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57}},
    .note_draw = {0x1803BFCB0,
                  {0x48, 0x8b, 0xc4, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xf0, 0, 0,
                   0}},
    .font_projection_constants = {0x1800E501F,
                                  {0x33, 0xd2, 0x48, 0x8b, 0xcb, 0xc5, 0xf8, 0x77, 0xff, 0x90, 0xf0, 0x02, 0x00, 0x00}},
    .font_projection_constants_return = 0x1800E502D,
    .cursor_call = {0x1801B6152, {0xe8, 0x89, 0xee, 0xfa, 0xff}},
    .frame_wait = {0x1801A39F1,
                   {0xff, 0x15, 0x89, 0x15, 0x3e, 0,    0x48, 0x2b, 0x05, 0xba, 0x33, 0xbf,
                    0x14, 0x48, 0x83, 0xf8, 0x0e, 0x73, 0x10, 0xb9, 0x0e, 0,    0,    0,
                    0x48, 0x2b, 0xc8, 0x74, 0x06, 0xff, 0x15, 0xc4, 0x14, 0x3e, 0},
                   {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
    .play_decrement = {0x1801F31B5,
                       {0xff, 0xc8, 0x89, 0x05, 0xf7, 0x49, 0x73, 0x02},
                       {0x90, 0x90, 0x89, 0x05, 0xf7, 0x49, 0x73, 0x02}},
    .live2d_menu = {0x180306192, {0x8b, 0xfa, 0x8b, 0xd9}, {0x31, 0xff, 0x31, 0xdb}},
    .live2d_alternate = {0x180288DB4, {0x8b, 0xfa, 0x8b, 0xd9, 0x85, 0xc9}, {0x31, 0xff, 0x31, 0xdb, 0x85, 0xdb}},
    .live2d_inline = {0x18028B29B,
                      {0x8b, 0x3d, 0x13, 0x70, 0x6a, 0x02, 0x8b, 0x1d, 0x09, 0x70, 0x6a, 0x02},
                      {0x31, 0xff, 0x90, 0x90, 0x90, 0x90, 0x31, 0xdb, 0x90, 0x90, 0x90, 0x90}},
    .side_copy_left = {0x180176E42,
                       {0x83, 0x3d, 0x47, 0x76, 0x76, 0x00, 0x01},
                       {0xe9, 0x27, 0x01, 0x00, 0x00, 0x90, 0x90}},
    .side_copy_right = {0x180176FF5,
                        {0xb9, 0x02, 0x00, 0x00, 0x00, 0xe8, 0xf1, 0x03, 0x00, 0x00},
                        {0xe9, 0x91, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90}},
    .side_composite_copy = {0x180176D88, {0x48, 0x85, 0xed, 0x74, 0x2d}, {0xe9, 0x5f, 0x00, 0x00, 0x00}},
    .raw_cursor = 0x180164FE0,
    .afp_return = 0x1801A3F3B,
    .afp_import = 0x180584AF8,
    .audio_format = 0x195E369A8,
    .audio_main = 0x195E36C20,
    .audio_alternate = 0x195E36C24,
    .song_previous = 0x1B008B42C,
    .song_raw = 0x1B008B438,
    .song_output = 0x1B008B430,
    .scene_pointer = 0x1808C23B8,
    .menu_subtick = 0x1B008B2C0,
    .menu_seconds = 0x1B008B2C4,
    .menu_paused = 0x1B008B2CC,
    .play_mode = 0x182927BB0,
    .play_ticks = 0x182927BB4,
    .play_paused = 0x182927BB8,
    .game_mode_pointer = 0x1808C2538,
    .dm_effects = 0x182F55B98,
    .dm_judgments = 0x1B04C3784,
    .note_options = 0x1808C4A50,
    .song_reset = 0x180287190,
    .filter_division0 = {0x180287060, {0xc5, 0xf2, 0x5e, 0x15, 0x24, 0x19, 0x3e, 0x00}},
    .filter_division1 = {0x180287133, {0xc5, 0xf2, 0x5e, 0x15, 0x51, 0x18, 0x3e, 0x00}},
    .constant_60 = 0x18066898C,
    .constant_afp = 0x180668644,
    .menu_previous = 0x1B008B2C8,
    .menu_hold = 0x1B008B2CD,
    .menu_ui0 = 0x1B008B2D0,
    .menu_ui1 = 0x1B008B2D8,
    .service_pause = 0x195EE8BC0,
    .debug_pause = 0x18043F520,
    .countdown_beep = 0x1802810D0,
    .keysound = 0x1801C3940,
    .audio_states = 0x195E36994,
    .audio_object = 0x1835F4168,
    .audio_timestamp = 0x195E36C08,
    .audio_epoch = 0x195E36C00,
    .audio_clock_import = 0x180584F80,
    .voice_tree = 0x1B09C7BC0,
    .voice_channels = 0x1B0A67F1C,
    .song_state = 0x1B008B3A0,
    .memset_import = 0x180584F30,
    .menu_character_builder = 0x180306170,
    .alternate_character_builder = 0x180288DA0,
    .menu_character_vector = 0x182F3FF88,
    .saved_characters = 0x1829322B0,
    .sprite_records = 0x1808F5010,
    .gf_sprite_records = 0x1808F5370,
    .nested_sprite_record = 0x1808F53A0,
    .audit_song_scene = 0x1802B5760,
    .audit_drum_worker = 0x180150F80,
    .song_start = {0x180287590,
                   {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40}},
    .lights = {0x1800DAD00, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x0d, 0xc8, 0xf6, 0x81, 0x00}},
    .bookkeeping = {0x1801B9800, {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44, 0x24, 0x30, 0xfe}},
    .light_pattern = {0x1800DA300,
                      {0x4c, 0x8b, 0xdc, 0x48, 0x81, 0xec, 0x78, 0x02, 0x00, 0x00, 0x80, 0x3d, 0x06, 0x00, 0x82, 0x00}},
    .song_delay_increment = {0x1802876E4,
                             {0xff, 0xc0, 0x89, 0x05, 0x80, 0x3d, 0xe0, 0x2f},
                             {0x90, 0x90, 0x89, 0x05, 0x80, 0x3d, 0xe0, 0x2f}},
    .bookkeeping_cadence = {0x1801A3C5F,
                            {0x75, 0x05, 0xe8, 0x9a, 0x5b, 0x01, 0x00},
                            {0x90, 0x90, 0xe8, 0x9a, 0x5b, 0x01, 0x00}},
    .song_start_state = 0x1B008B440,
    .song_start_delay = 0x1B008B46C,
    .session_age = 0x1AF5BAD48,
    .report_age = 0x1AF5BAD78,
    .color_phase = 0x1AF6BEC40,
    .light_age = 0x1808FA3CC,
    .light_color = 0x1808FA3D8,
    .light_brightness = 0x1808FA3C8,
    .bookkeeping_return = 0x1801A3C66,
    .light_mode = 0x1808FA3D4,
    .light_enabled = 0x1808FA3C7,
    .light_device = 0x1835C2370,
    .light_rgb_output = 0x1800DA280,
    .light_pattern_output = 0x1800DA300,
    .light_brightness_output = 0x1800DA200,
    .light_pattern_enabled = 0x1808FA317,
    .light_pattern_mode = 0x1808FA3D0,
    .light_pattern_index = 0x1808FA3B4,
    .secondary_update = {0x1803C7FE0, {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}},
    .secondary_at20 = {0x1803C8433, {0x83, 0xf8, 0x14, 0x0f, 0x85, 0x07, 0x01, 0x00, 0x00}},
    .secondary_age_step = {0x1803C996D, {0xff, 0x43, 0x1c, 0x8b, 0x43, 0x1c}},
    .secondary_countdown0 = {0x1803C8CA4, {0xff, 0xc8, 0x89, 0x43, 0x24}},
    .secondary_countdown1 = {0x1803C9238, {0xff, 0xc8, 0x89, 0x43, 0x24}},
    .secondary_at20_yes = 0x1803C843C,
    .secondary_at20_no = 0x1803C8543,
    .secondary_player_base = 0x1B070962C,
    .secondary_service_mode = 0x195EE8BB8,
    .secondary_input_object = 0x1800D3830,
    .secondary_exit = 0x1803888F0,
    .secondary_inputs = {0x180663580, 0x1806635B0, 0x180663508, 0x180663550, 0x180663400, 0x180663448},
    .lobby_update = {0x1802685F0, {0x4c, 0x8b, 0xdc, 0x48, 0x81, 0xec, 0x88, 0, 0, 0}},
    .match_update = {0x18026B750, {0x48, 0x81, 0xec, 0xc8, 0, 0, 0, 0x48, 0x8b, 0x05, 0xda, 0x98, 0x56, 0}},
    .network_steps = {{{0x180268971, {0xff, 0xc0}, {0x90, 0x90}},
                       {0x180268A14, {0xff, 0xc0}, {0x90, 0x90}},
                       {0x18026BADB, {0xff, 0xc0}, {0x90, 0x90}},
                       {0x18026BE25, {0xff, 0xc0}, {0x90, 0x90}},
                       {0x18026BE7E, {0xff, 0x05, 0x68, 0xe7, 0xe1, 0x2f}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}}}},
    .lobby_active = 0x1B008A60C,
    .lobby_state = 0x1B008A610,
    .lobby_age = 0x1B008A614,
    .match_active = 0x1B008A120,
    .match_state = 0x1B008A130,
    .match_age = 0x1B008A134,
    .chat_age = 0x1B008A5EC,
    .chat_send_age = 0x1B008A5FC,
    .network_connection = 0x1B07097AC,
    .network_nodes = 0x1B07097B8,
    .network_restart = 0x1804474F0,
    .network_break = 0x18026B080,
    .secondary_at20_body = {0x1803C843C, {0x80, 0x3d, 0xb5, 0xcb, 0xba, 0x02, 0}},
    .preview_steps = {{{0x1803D80A1, {0x41, 0xff, 0x8c, 0x88, 0xc8, 0x49, 0, 0}},
                       {0x1803D80BF, {0x41, 0xff, 0x8c, 0x88, 0xcc, 0x49, 0, 0}},
                       {0x1803D80DD, {0x41, 0xff, 0x8c, 0x88, 0xd0, 0x49, 0, 0}},
                       {0x1803D80FB, {0x41, 0xff, 0x8c, 0x88, 0xd4, 0x49, 0, 0}},
                       {0x1803D8119, {0x41, 0xff, 0x8c, 0x88, 0xd8, 0x49, 0, 0}},
                       {0x1803D8137, {0x41, 0xff, 0x8c, 0x88, 0xdc, 0x49, 0, 0}},
                       {0x1803D8155, {0x41, 0xff, 0x8c, 0x88, 0xe0, 0x49, 0, 0}},
                       {0x1803D8173, {0x41, 0xff, 0x8c, 0x88, 0xe4, 0x49, 0, 0}},
                       {0x1803D8191, {0x41, 0xff, 0x8c, 0x88, 0xe8, 0x49, 0, 0}},
                       {0x1803D81AF, {0x41, 0xff, 0x8c, 0x88, 0xec, 0x49, 0, 0}}}},
    .preview_countdown = {0x1803D8298, {0x43, 0xff, 0x0c, 0x01, 0x43, 0x83, 0x3c, 0x01, 0,    0x0f, 0x8f, 0xa9,
                                        0x01, 0,    0,    0x43, 0xc7, 0x04, 0x01, 0x58, 0x02, 0,    0}},
    .preview_event = 0x1803D82AF,
    .preview_next = 0x1803D8450,
    .preview_samples = {{{0x1803D7018, {0xc5, 0xfa, 0x2a, 0x84, 0x81, 0xc8, 0x49, 0, 0}},
                         {0x1803D7122, {0xc5, 0xf2, 0x2a, 0x8c, 0x81, 0xc8, 0x49, 0, 0}},
                         {0x1803D7381, {0xc4, 0xc1, 0x7a, 0x2a, 0x04, 0x01}}}},
    .hit_effect_short = {0x1803836AB, {0xff, 0x03, 0x83, 0x3b, 0x07}},
    .hit_effect_long = {0x180383831, {0xff, 0xc0, 0x41, 0x89, 0x40, 0x48}},
    // Resolved through lifecycle initialization and its six-entry factory table.
    .audit_scene_tables = {0x1807D6830, 0x1807D7250, 0x1807D72B0},
    .audit_scene_records = {0x1808C06F0, 0x1808C0970, 0x1808C09C0, 0x1808C0A10, 0x1808C0A60},
    .audit_scene_factories = {0x1805DC620, 0x1805DC6A0, 0x1805DBEB0, 0x1805DC700},
    .tally_update = {0x1803ABA60, {0x48, 0x8b, 0xc4, 0x41, 0x55, 0x48, 0x83, 0xec, 0x60}},
    .tally_step = {0x1803AC3C0, {0xff, 0xc0, 0x44, 0x3b, 0x2d, 0xf7, 0x64, 0x11, 0x30}},
    .tally_numbers = {0x1803ABAF3, {0x44, 0x39, 0x0d, 0x66, 0x69, 0x11, 0x30}},
    .tally_events = {{{{0x1803ABBC8, {0x83, 0x3d, 0x91, 0x68, 0x11, 0x30, 0x5a}}, 90},
                      {{0x1803ABC0F, {0x83, 0x3d, 0x4a, 0x68, 0x11, 0x30, 0x6e}}, 110},
                      {{0x1803ABC58, {0x81, 0x3d, 0xfe, 0x67, 0x11, 0x30, 0x8c, 0, 0, 0}}, 140},
                      {{0x1803ABCAC, {0x81, 0x3d, 0xaa, 0x67, 0x11, 0x30, 0xaa, 0, 0, 0}}, 170},
                      {{0x1803ABCF8, {0x81, 0x3d, 0x5e, 0x67, 0x11, 0x30, 0xc8, 0, 0, 0}}, 200},
                      {{0x1803ABD44, {0x39, 0x3d, 0x16, 0x67, 0x11, 0x30}}, 260}}},
    .tally_age = 0x1B04C2460,
    .tally_state = 0x1B04C28C0,
    .tally_enabled = 0x1808F65F9,
    .tally_number_next = 0x1803ABBB3,
    .tally_targets = 0x1B04C28CC,
    .tally_values = 0x1B04C290C,
    .tally_sound_options = 0x1808F6604,
    .player_active = 0x1808F55CA,
    .option_repeat = {0x180389A20, {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x05, 0x9d, 0x98, 0x53, 0x00}},
    .speed_steps = {{{0x180389D13, {0x41, 0xfe, 0x83, 0x52, 0x10, 0, 0}},
                     {0x180389E0B, {0x41, 0xfe, 0x83, 0x52, 0x10, 0, 0}},
                     {0x180389EC5, {0x41, 0xfe, 0x83, 0x53, 0x10, 0, 0}}}},
    .speed_sample = {0x1803899CA, {0xc5, 0xfa, 0x2a, 0xc0, 0xc5, 0xf2, 0x59, 0xc8}},
    .option_object = 0x1808C32C8,
    .option_keys = 0x1B04D50C0,
    .option_modifiers = 0x180663568,
    .option_speed_update = 0x180389CF0,
    .option_speed_value = 0x180389960,
    // Only duration/phase consumers use the elapsed sidecar. Native dispatcher
    // counters also mark first-update entry and retain that ordinal behavior.
    .controller_time_reads = {{{0x1802019B2, {0x44, 0x8b, 0x7a, 0x18, 0x41, 0xb9, 0xd0, 0x02, 0, 0}},
                               {0x18020D00F, {0x8b, 0x79, 0x18, 0x41, 0xb9, 0xd0, 0x02, 0, 0}},
                               {0x180207AFA, {0xf7, 0x63, 0x18, 0xc1, 0xea, 0x05}},
                               {0x180210B5B, {0x41, 0xf7, 0x67, 0x1c, 0xc1, 0xea, 0x03}}}},
    .sprite_inline = {{{0x1803B81E4, {0x41, 0xff, 0x40, 0x06, 0x41, 0x8b, 0x40, 0x06}},
                       {0x1803B82B1, {0x8b, 0x05, 0xc9, 0xd0, 0x53, 0, 0xff, 0xc0}}}},
    .dm_sprite_next = 0x1803B824E,
    .gf_sprite_next = 0x1803B8352,
    .result_begin = {0x180396F2C, {0x48, 0x63, 0x05, 0x25, 0x37, 0xbc, 0x02}},
    .result_end = {0x1803992F2, {0xff, 0x05, 0x80, 0x13, 0xbc, 0x02, 0xff, 0x05, 0xc2, 0xf0, 0xbb, 0x02}},
    .result_sound_step = {0x1803987DA, {0x83, 0xe8, 0x01, 0x89, 0x03}},
    .result_events = {{{{0x18038E187, {0x83, 0x3d, 0x32, 0xa2, 0xbc, 0x02, 0x02}}, 2},
                       {{0x18038E3F7, {0x83, 0x3d, 0xc2, 0x9f, 0xbc, 0x02, 0x02}}, 2},
                       {{0x18038E817, {0x83, 0x3d, 0xa2, 0x9b, 0xbc, 0x02, 0x02}}, 2},
                       {{0x18038ECAE, {0x83, 0x3d, 0x0b, 0x97, 0xbc, 0x02, 0x02}}, 2},
                       {{0x18038EEB6, {0x81, 0x3d, 0x00, 0x95, 0xbc, 0x02, 0x99, 0x00, 0x00, 0x00}}, 153},
                       {{0x18038F1AA, {0x83, 0x3d, 0x0f, 0x92, 0xbc, 0x02, 0x0a}}, 10},
                       {{0x180397056, {0x39, 0x3d, 0x64, 0x13, 0xbc, 0x02}}, 1},
                       {{0x180397792, {0x83, 0x3d, 0x27, 0x0c, 0xbc, 0x02, 0x3c}}, 60},
                       {{0x18039791C, {0x83, 0x3d, 0x9d, 0x0a, 0xbc, 0x02, 0x3c}}, 60},
                       {{0x18038E660, {0x83, 0xf9, 0x02, 0x0f, 0x85, 0xc6, 0x00, 0x00, 0x00}}, 2},
                       {{0x18038EA8E, {0x83, 0xf8, 0x02, 0x0f, 0x85, 0xc0, 0x00, 0x00, 0x00}}, 2},
                       {{0x18038EEEE, {0x0f, 0x85, 0x9e, 0x00, 0x00, 0x00}}, 272},
                       {{0x18038F3C6, {0x83, 0xf8, 0x0a, 0x75, 0x34}}, 10}}},
    .result_event_yes = {0x18038E669, 0x18038EA97, 0x18038EEF4, 0x18038F3CB},
    .result_event_no = {0x18038E72F, 0x18038EB57, 0x18038EF92, 0x18038F3FF},
    .result_periods = {{{0x180390732, {0x39, 0x05, 0x88, 0x7c, 0xbc, 0x02}},
                        {0x1803938A8, {0x39, 0x05, 0x12, 0x4b, 0xbc, 0x02}},
                        {0x180398946, {0x83, 0xf9, 0x0f, 0x0f, 0x85, 0x2a, 0x01, 0x00, 0x00}}}},
    .result_countdowns =
        {{{0x18038D9C8, {0xff, 0xc8, 0x41, 0x89, 0x84, 0xb4, 0xd0, 0x22, 0x00, 0x00, 0x83, 0xf8, 0x01}},
          {0x18038DAAF, {0xff, 0xc8, 0x41, 0x89, 0x84, 0xb4, 0xe0, 0x22, 0x00, 0x00, 0x83, 0xf8, 0x01}},
          {0x18038DB87, {0xff, 0xc8, 0x41, 0x89, 0x84, 0xb4, 0xd8, 0x22, 0x00, 0x00, 0x83, 0xf8, 0x01}}}},
    .result_countups = {{{0x18038F7E7, {0x03, 0x4b, 0x08, 0x3b, 0xca}},
                         {0x18038F905, {0x42, 0x03, 0x8c, 0x0b, 0x10, 0x23, 0x00, 0x00, 0x3b, 0xca}},
                         {0x18039070D, {0x41, 0x8d, 0x04, 0x10, 0x3b, 0xc1}},
                         {0x18039076B, {0x8d, 0x04, 0x11, 0x89, 0x84, 0xab, 0xb8, 0xaa, 0xf5, 0x02}}}},
    .result_age = 0x182F583C0,
    .result_state = 0x182F5A658,
    .result_color = 0x182F5A678,
    .result_color_mode = 0x182F5A67C,
    .result_update = 0x180396EF0,
    .result_request = 0x18038F3C0,
    .result_request_active = 0x1AFEC38E0,
    .overlay_age = 0x1AFEC38D8,
    .result_request_code = 0x1AFEC38E4,
    .result_request_kind = 0x182F5AA70,
    .result_countup0 = 0x182F5A6D8,
    .result_countup1 = 0x182F5A6C0,
    .result_gauges = 0x182F5AAB0,
    .result_gauge_targets = 0x182F5AAC0,
    .result_sound_queue = 0x182F5AAD0,
    .result_skip_input = 0x18038C670,
    .result_sound_enqueue = 0x180395C30,
    .result_gauge_update = 0x180390680,
    .result_countup_update0 = 0x18038F4F0,
    .result_countup_update1 = 0x18038F870,
    .result_countdown_update = 0x18038D8C0,
    .result_gauge_dm_goal = 0x1808F6694,
    .result_gauge_gf_goal = 0x1808F66AC,
    .result_color_yes = 0x18039894F,
    .result_color_no = 0x180398A79,
    .result_guitar_flag = 0x1808F7271,
    .result_game_stage = 0x1808F5550,
    .battle_begin = {0x1803A41B4, {0x83, 0xff, 0x0b, 0x0f, 0x87, 0xd6, 0x12, 0x00, 0x00}},
    .battle_step = {0x1803A560E, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0xec, 0xbd, 0x11, 0x30}},
    .battle_end = {0x1803A5620, {0x8b, 0x0d, 0x36, 0xc6, 0x11, 0x30}},
    .battle_cue_sequence = {0x1803A4290, {0x41, 0x83, 0xf8, 0x2f, 0x77, 0x10}},
    .battle_roulette = {0x1803A4D63, {0x45, 0x84, 0xce, 0x0f, 0x85, 0x98, 0x00, 0x00, 0x00}},
    .battle_awards = {0x1803A50E6, {0x41, 0x83, 0xf8, 0x3c, 0x0f, 0x8e, 0xfd, 0x00, 0x00, 0x00}},
    .battle_exit_step = {0x1803A62DE, {0xff, 0xc8, 0x89, 0x05, 0x2e, 0xb1, 0x11, 0x30, 0x83, 0xf8, 0x1e}},
    .battle_events =
        {{{{0x1803A4260, {0x45, 0x3b, 0xc6, 0x75, 0x2b}}, 1, 0x1803A4265, 0x1803A4290},
          {{0x1803A4408, {0x41, 0x83, 0xf8, 0x3c, 0x0f, 0x85, 0x85, 0x00, 0x00, 0x00}}, 60, 0x1803A4412, 0x1803A4497},
          {{0x1803A472D, {0x41, 0x83, 0xf8, 0x3c, 0x0f, 0x85, 0x85, 0x00, 0x00, 0x00}}, 60, 0x1803A4737, 0x1803A47BC},
          {{0x1803A4C2B, {0x41, 0x83, 0xf8, 0x5a, 0x75, 0x75}}, 90, 0x1803A4C31, 0x1803A4CA6},
          {{0x1803A4CF1, {0x41, 0x83, 0xf9, 0x14, 0x7d, 0x2a, 0x45, 0x3b, 0xce, 0x75, 0x1a}},
           1,
           0x1803A4CFC,
           0x1803A4D16},
          {{0x1803A4D21, {0x41, 0x83, 0xf9, 0x1e, 0x75, 0x36}}, 30, 0x1803A4D27, 0x1803A4D5D},
          {{0x1803A4F1C, {0x41, 0x83, 0xf8, 0x02, 0x75, 0x5a}}, 2, 0x1803A4F22, 0x1803A4F7C},
          {{0x1803A4F7C, {0x41, 0x83, 0xf8, 0x16, 0x74, 0x06}}, 22, 0x1803A4F88, 0x1803A4F82},
          {{0x1803A4F82, {0x41, 0x83, 0xf8, 0x20, 0x75, 0x2b}}, 32, 0x1803A4F88, 0x1803A4FB3},
          {{0x1803A4FB3, {0x41, 0x83, 0xf8, 0x2a, 0x75, 0x2b}}, 42, 0x1803A4FB9, 0x1803A4FE4}}},
    .battle_numbers = {{{0x1803A4497, {0x44, 0x89, 0x35, 0xb2, 0xd7, 0x11, 0x30, 0x44, 0x8b, 0xce}},
                        {0x1803A47BC, {0x44, 0x89, 0x35, 0xb9, 0xd4, 0x11, 0x30, 0x44, 0x8b, 0xce}}}},
    .battle_number_cues = {{{0x1803A4672, {0x41, 0x3b, 0xc6, 0x75, 0x30}},
                            {0x1803A497C, {0x41, 0x3b, 0xc6, 0x75, 0x30}}}},
    .battle_countdowns = {{{0x1803A54AC, {0x44, 0x29, 0x35, 0x95, 0xc9, 0x11, 0x30}},
                           {0x1803A551C, {0x44, 0x29, 0x35, 0x21, 0xc9, 0x11, 0x30}},
                           {0x1803A558E, {0x41, 0x2b, 0xc6, 0x89, 0x05, 0xa9, 0xc8, 0x11, 0x30}}}},
    .battle_countdown_cues = {{{0x1803A54E0, {0x83, 0x3d, 0x61, 0xc9, 0x11, 0x30, 0x14}},
                               {0x1803A5550, {0x83, 0x3d, 0xed, 0xc8, 0x11, 0x30, 0x14}},
                               {0x1803A55CA, {0x83, 0xf8, 0x14, 0x75, 0x2a}}}},
    .battle_update = 0x1803A4180,
    .battle_age = 0x1B04C1404,
    .battle_state = 0x1B04C140C,
    .battle_exit_age = 0x1B04C1414,
    .battle_outer_state = 0x1B04C1408,
    .battle_dispatch = 0x1803A41BD,
    .battle_dispatch_default = 0x1803A5493,
    .battle_cue_yes = 0x1803A42AC,
    .battle_cue_no = 0x1803A42D7,
    .battle_number_next = {0x1803A4661, 0x1803A496B},
    .battle_number_cue_yes = {0x1803A4677, 0x1803A4981},
    .battle_number_cue_no = {0x1803A46A7, 0x1803A49B1},
    .battle_roulette_yes = 0x1803A4D6C,
    .battle_roulette_no = 0x1803A4E04,
    .battle_roulette_after = 0x1803A4DDB,
    .battle_roulette_goal = 0x1B04C1E28,
    .battle_roulette_start = 0x1B04C1E2C,
    .battle_roulette_value = 0x1B04C1E30,
    .battle_awards_stock = 0x1803A50F0,
    .battle_awards_sound = 0x1803A51C2,
    .battle_awards_next = 0x1803A51ED,
    .battle_player = 0x1B04C1E10,
    .battle_awards_values = 0x1B04C1CC4,
    .battle_awards_pending = 0x1B04C1CD4,
    .battle_counter0 = 0x1B04C1E40,
    .battle_scores = 0x1B04C1C50,
    .battle_countdown_last_yes = 0x1803A55CF,
    .battle_countdown_last_no = 0x1803A55F9,
    .battle_intro_idle = 0x1803A4D16,
    .battle_intro_late = 0x1803A4D21,
    .battle_intro_object = 0x1B04C14A8,
    .animation_label = 0x180184350,
    .label_in = 0x180636A00,
    .battle_renderers = {0x1803A38D0, 0x1803A2A80, 0x1803A2370},
    .gf_effect_step = {0x1803661AB, {0x8b, 0x05, 0x27, 0xd0, 0xbe, 0x02}},
    .combo_effect_step = {0x18038427E, {0xff, 0xc8, 0x89, 0x05, 0x7a, 0x55, 0x57, 0x00}},
    .gf_number_step = {0x18036f603, {0x8b, 0x53, 0x08, 0x44, 0x8b, 0x33}},
    .gf_number_next = 0x18036f63e,
    .gf_number_native_start = 0x18036f5ac,
    .shutter_fade = {0x1803B5981, {0x8b, 0x56, 0x04, 0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xfa, 0x2a, 0xc2, 0xc5, 0xfa,
                                   0x5e, 0xcf, 0x85, 0xc0, 0x7e, 0x04, 0xc5, 0xca, 0x5c, 0xc9, 0x8d, 0x42, 0x01,
                                   0xc5, 0xfa, 0x11, 0x49, 0x0c, 0x89, 0x46, 0x04, 0x83, 0xf8, 0x0a}},
    .end_event = {0x1803B864D, {0x39, 0x0d, 0xa5, 0xee, 0x53, 0x00}},
    .end_step = {0x1803B86D9, {0xff, 0x05, 0x19, 0xee, 0x53, 0x00}},
    .hud_phase_step = {0x1803A9357, {0xff, 0xc0, 0x25, 0xff, 0x0f, 0x00, 0x00, 0x89, 0x05, 0x7c, 0x8b, 0x11, 0x30}},
    .hud_countdown_step = {0x1803A82D6, {0x43, 0xff, 0x8c, 0x32, 0x94, 0x22, 0x4c, 0x30}},
    .effect_age_steps = {{{0x18036F2AF, {0xff, 0x05, 0xe7, 0xa3, 0x58, 0x00}},
                          {0x1803710AC, {0xff, 0x05, 0x0e, 0x86, 0x58, 0x00}},
                          {0x1803B5911, {0xff, 0x05, 0xa5, 0xde, 0x10, 0x30}}}},
    .effect_ages = {0x1808F969C, 0x1808F96C0, 0x1B04C37BC},
    .gf_effect_age = 0x182F531D8,
    .gf_effect_next = 0x1803661D3,
    .combo_effect_age = 0x1808F9800,
    .shutter_object = 0x1808C49E8,
    .shutter_update = 0x1803B57E0,
    .shutter_fades = 0x1B04C37C0,
    .shutter_colors = 0x1808C91D0,
    .end_update = 0x1803B8620,
    .end_age = 0x1808F74F8,
    .end_enabled = 0x1808F74FC,
    .end_gauge = 0x1B4BC6504,
    .hud_age = 0x1B04C1EE0,
    .hud_countdowns = 0x1B04C2294,
    .end_inputs = {0x1801F8200, 0x1803B8570},
    .shutter_objects = 0x1B04C2F00,
    .end_object = 0x1808F74D8,
    .animation_label_lookup = 0x1801843B0,
    .gf_overlay_hold = {0x1803621E4,
                        {0x43, 0x8b, 0x84, 0x1a, 0xd8, 0xca, 0x4b, 0x30, 0xc4, 0x81, 0x7a, 0x10, 0x84, 0x1a, 0x14, 0xcb,
                         0x4b, 0x30}},
    .gf_overlay_hit = {0x180362ED9, {0x43, 0x8b, 0x84, 0x1a, 0x18, 0xcb, 0x4b, 0x30}},
    .gf_overlay_counters = {{{0x1803625E1, {0xff, 0xc8, 0x43, 0x89, 0x84, 0x1a, 0xdc, 0xca, 0x4b, 0x30}},
                             {0x180362625, {0xff, 0xc8, 0x89, 0x01, 0x49, 0x8b, 0x84, 0xdb, 0xe0, 0xc7, 0x4b, 0x30}},
                             {0x180362E2E, {0xff, 0xc8, 0xb9, 0x6a, 0x01, 0x00, 0x00}}}},
    .gf_overlay_fades =
        {{{0x1803627BB, {0xc5, 0xfa, 0x58, 0xc6, 0xe9, 0x9f, 0x00, 0x00, 0x00}},
          {0x18036285F, {0xc5, 0xfa, 0x5c, 0xc6, 0xc4, 0x81, 0x7a, 0x11, 0x84, 0x1a, 0xf4, 0xca, 0x4b, 0x30}},
          {0x180362B4A, {0xc5, 0xfa, 0x58, 0xc6, 0xe9, 0x9f, 0x00, 0x00, 0x00}},
          {0x180362BEE, {0xc5, 0xfa, 0x5c, 0xc6, 0xc4, 0x81, 0x7a, 0x11, 0x84, 0x1a, 0x04, 0xcb, 0x4b, 0x30}}}},
    .gf_overlay_update = 0x180362080,
    .gf_overlay_records = 0x1B04BC760,
    .gf_overlay_hold_value = 0x1B04BCAD8,
    .gf_overlay_hold_alpha = 0x1B04BCB14,
    .gf_overlay_hold_identity = 0x1B04BCAB0,
    .gf_overlay_hold_next = 0x180362224,
    .gf_overlay_counter_values = {0x1B04BCADC, 0x1B04BCAE0, 0x1B04BCB1C},
    .gf_overlay_counter_identities = {0x1B04BCAB4, 0x1B04BCAB8, 0x0},
    .gf_overlay_alphas = {0x1B04BCAF4, 0x1B04BCB04},
    .gf_overlay_fade_next = {0x180362863, 0x180362BF2},
    .gf_overlay_counter_object = 0x1B04BC7E0,
    .gf_overlay_hit_value = 0x1B04BCB18,
    .gf_overlay_hit_next = 0x180362F02,
    .gf_overlay_step = 0x180668660,
    .gf_overlay_scratch = 0x181493A2D,
    .gf_overlay_controls = {0x1808F65CC, 0x1808F6598, 0x1808F659A},
    .song_load_timeout = {0x1802BA10F, {0x41, 0xff, 0x80, 0xd4, 0x1c, 0x00, 0x00}},
    .song_disconnect_age = {0x1802B57E0, {0xff, 0x80, 0x28, 0x1d, 0x00, 0x00}},
    .song_phase_age = {0x1802B586E, {0xff, 0xc1, 0x41, 0x89, 0x89, 0x28, 0x12, 0x00, 0x00}},
    .song_ready_gate = {0x1802B5BDF, {0x41, 0x38, 0xb9, 0x9d, 0x00, 0x00, 0x00}},
    .song_local_waits = {{{0x1802B5E5B, {0x41, 0x8b, 0x49, 0x74, 0x8d, 0x41, 0x01, 0x41, 0x89, 0x41, 0x74}},
                          {0x1802B6195, {0x41, 0x8b, 0x49, 0x74, 0x8d, 0x41, 0x01, 0x41, 0x89, 0x41, 0x74}},
                          {0x1802B77F8, {0x41, 0x8b, 0x49, 0x74, 0x8d, 0x41, 0x01, 0x41, 0x89, 0x41, 0x74}},
                          {0x1802B79F2, {0x41, 0x8b, 0x49, 0x74, 0x8d, 0x41, 0x01, 0x41, 0x89, 0x41, 0x74}}}},
    .song_hold_steps = {{{0x1802B6551, {0xff, 0x04, 0x0b, 0x83, 0x3c, 0x0b, 0x3c}},
                         {0x1802B6609, {0xff, 0x04, 0x0b, 0x83, 0x3c, 0x0b, 0x3c}}}},
    .song_exit_steps = {{{0x1802B621E, {0xff, 0x05, 0x2c, 0x66, 0xdd, 0x2f}},
                         {0x1802BA0D5, {0xff, 0xc0, 0x41, 0xff, 0x40, 0x34, 0x89, 0x05, 0x6f, 0x27, 0xdd, 0x2f}}}},
    .song_post_delay = {0x1802B1B7B, {0xff, 0xc1, 0x89, 0x0d, 0xd1, 0xac, 0xdd, 0x2f}},
    .song_progress_delay = {0x1802B30A4, {0xff, 0xc0, 0xb2, 0x01, 0x89, 0x05, 0x1e, 0x97, 0xdd, 0x2f}},
    .song_end_timeout = {0x1802B3101, {0xff, 0x83, 0x34, 0x12, 0x00, 0x00}},
    .song_network_age = {0x1802B35D2, {0x41, 0xff, 0xc1, 0x44, 0x89, 0x0d, 0x7c, 0x92, 0xdd, 0x2f}},
    .song_visual_age = {0x18028ACD0, {0xff, 0x05, 0xb2, 0x08, 0xe0, 0x2f}},
    .song_intro_age = {0x1802B5ADE, {0xff, 0x05, 0xe4, 0x6c, 0xdd, 0x2f}},
    .song_controller_object = 0x1B008BA98,
    .song_exit_age = 0x1B008C850,
    .song_post_age = 0x1B008C854,
    .song_progress_age = 0x1B008C7CC,
    .song_network_wait = 0x1B008C858,
    .song_visual_phase = 0x1B008B588,
    .song_intro_wait = 0x1B008C7C8,
    .song_ready_yes = 0x1802B5C0C,
    .song_ready_no = 0x1802B5C89,
    .song_controller_update = 0x1802BA060,
    .judgment_cooldown_step = {0x180271805, {0xff, 0x05, 0xa9, 0x48, 0x95, 0x34}},
    .judgment_hold_step = {0x1802756B7, {0x42, 0xff, 0x84, 0xbf, 0xb0, 0x65, 0xbc, 0x34}},
    .judgment_feedback_step = {0x1802756FA, {0x41, 0xff, 0x86, 0xdc, 0x4a, 0x00, 0x00}},
    .service_light_step = {0x1801D0F1C, {0xff, 0x05, 0x9e, 0x8a, 0x53, 0x30}},
    .service_dialog_steps =
        {{{0x1801D9DE6,
           {0x8b, 0x05, 0xc0, 0x65, 0x9a, 0x34, 0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xfa, 0x2a, 0xc0, 0xff, 0xc0}},
          {0x1801D9E1C,
           {0x8b, 0x05, 0x8a, 0x65, 0x9a, 0x34, 0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xfa, 0x2a, 0xc0, 0xff, 0xc0}},
          {0x1801D9F51,
           {0x8b, 0x05, 0x55, 0x64, 0x9a, 0x34, 0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xfa, 0x2a, 0xc0, 0xff, 0xc0}}}},
    .service_io_steps = {{{0x1801D03E8, {0xff, 0xc8, 0x44, 0x89, 0x7b, 0x08, 0x89, 0x03}},
                          {0x1801D03FE, {0xff, 0xc8, 0x89, 0x43, 0x08}},
                          {0x1801D0444, {0x8d, 0x42, 0x01, 0xeb, 0xcf}}}},
    .service_word_steps = {{{0x1801CD033, {0x66, 0x03, 0xc1, 0x66, 0x89, 0x05, 0xcb, 0x6e, 0x42, 0x03}},
                            {0x1801CD049, {0x66, 0x03, 0xc1, 0x66, 0x89, 0x05, 0xb9, 0x6e, 0x42, 0x03}}}},
    .service_poll_steps = {{{0x1801CF81D,
                             {0x8b, 0x05, 0xb5, 0x14, 0x7e, 0x30, 0xff, 0xc0, 0x89, 0x05, 0xad, 0x14, 0x7e, 0x30, 0xa8,
                              0x01, 0x74, 0x32}},
                            {0x1801D0EC0,
                             {0x8b, 0x05, 0x12, 0xfe, 0x7d, 0x30, 0xff, 0xc0, 0x89, 0x05, 0x0a, 0xfe, 0x7d, 0x30, 0xa8,
                              0x01, 0x74, 0x30}}}},
    .judgment_cooldown = 0x1B4BC60B4,
    .judgment_hold = 0x1B4BC65B0,
    .service_light_age = 0x1B07099C0,
    .service_dialog_age = 0x1B4B803AC,
    .service_dialog_state = 0x1B4B803A8,
    .service_dialog_resource = 0x1B04D2B10,
    .service_words = {0x1835F3F08, 0x1835F3F0C},
    .service_dialog_updates = {0x1801D9D00, 0x1801D9F30},
    .service_poll_updates = {0x1801CF810, 0x1801D0050},
    .service_poll_yes = {0x1801CF82F, 0x1801D0ED2},
    .service_poll_no = {0x1801CF861, 0x1801D0F02},
    .service_dialog_limits = {0x1806689E8, 0x180668AF0},
    .service_poll_age = 0x1B09B0CD8,
    .service_poll_disabled = 0x1B09AA406,
    .service_poll_enabled = 0x1811FAEC4,
    .service_poll_output = 0x18019CB00,
    .service_io_next = 0x1801D0418,
    .system_counters = {{
        {{0x1802d170f, {0xff, 0xc0, 0x89, 0x05, 0xb1, 0x7a, 0xdc, 0x2f}},
         0x1b00991c8,
         0x1b0099148,
         CounterForm::IncEax}, // Scene 0x35 handshake delay
        // Continuous ages: only native resets restart them, not state changes.
        {{0x18024e45d, {0x41, 0xff, 0xc0, 0x48, 0x63, 0x15, 0x5d, 0xb8, 0xd7, 0x2f}},
         0x1affc9cc8,
         0,
         CounterForm::IncR8LoadRdx,
         CounterBase::Global,
         0x1affc9cc4}, // Scene 0x39 request age and displaced state load
        {{0x18024e467, {0xff, 0xc1, 0x44, 0x89, 0x05, 0x58, 0xb8, 0xd7, 0x2f}},
         0x1affc9ccc,
         0,
         CounterForm::IncEcxStoreR8,
         CounterBase::Global,
         0x1affc9cc8}, // Scene lifetime and displaced request-age store
        {{0x18025ada7, {0xff, 0x43, 0x08, 0x81, 0x7b, 0x08, 0xb4, 0x00, 0x00, 0x00}},
         8,
         4,
         CounterForm::IncMemoryCmp,
         CounterBase::Rbx,
         180}, // Scene 0x41 input hold
        {{0x18025ad37, {0xff, 0x43, 0x0c, 0x83, 0x7b, 0x0c, 0x78}},
         12,
         4,
         CounterForm::IncMemoryCmp,
         CounterBase::Rbx,
         120}, // Scene 0x41 closing fallback
        {{0x1801fa7dc, {0xff, 0xc0, 0x89, 0x05, 0x08, 0xfb, 0xcf, 0x02}},
         0x182efa2ec,
         0,
         CounterForm::IncEax}, // Shared AFP character hide wait
        {{0x1801fc817, {0xff, 0xc0, 0x89, 0x05, 0xcd, 0xda, 0xcf, 0x02}},
         0x182efa2ec,
         0,
         CounterForm::IncEax}, // Standalone
                               // alias
        {{0x1801fa94d, {0xff, 0xc8, 0x89, 0x05, 0x9f, 0x05, 0xd0, 0x02}},
         0x182efaef4,
         0x182efaef0,
         CounterForm::DecEax}, // Shared AFP overlay hold
        {{0x18027ea00, {0xff, 0xc8, 0x89, 0x05, 0xec, 0xc4, 0xc7, 0x02}},
         0x182efaef4,
         0x182efaef0,
         CounterForm::DecEax}, // Standalone alias
        {{0x1801faa68, {0xff, 0xc8, 0x89, 0x05, 0x44, 0xf6, 0xd4, 0x02}},
         0x182f4a0b4,
         0x182f4a0b0,
         CounterForm::DecEax}, // Shared AFP notice hold
        {{0x18027e3a0, {0xff, 0xc8, 0x89, 0x05, 0x0c, 0xbd, 0xcc, 0x02}},
         0x182f4a0b4,
         0x182f4a0b0,
         CounterForm::DecEax},
        {{0x1803898b4, {0xff, 0xc8, 0x89, 0x05, 0xf8, 0x07, 0xbc, 0x02}},
         0x182f4a0b4,
         0x182f4a0b0,
         CounterForm::DecEax},
        {{0x18024a2b8, {0xff, 0x83, 0xcc, 0x17, 0x02, 0x00}},
         0x217cc,
         0x10,
         CounterForm::IncMemory,
         CounterBase::Rbx}, // Selection sequence stage waits

        {{0x180250ef4, {0xff, 0x05, 0x32, 0x9d, 0xd7, 0x2f}},
         0x1affcac2c,
         0x0,
         CounterForm::IncMemory}, // Movie transition poll
        {{0x180251256, {0xff, 0x05, 0x54, 0x99, 0xd7, 0x2f}},
         0x1affcabb0,
         0x0,
         CounterForm::IncMemory}, // Recorded play duration
        {{0x180251361, {0xff, 0x05, 0x41, 0x98, 0xd7, 0x2f}},
         0x1affcaba8,
         0x1affcaba0,
         CounterForm::IncMemory}, // Movie exit wait
        {{0x18028e73b, {0xff, 0x05, 0xeb, 0xc4, 0xd3, 0x2f}},
         0x1affcac2c,
         0x0,
         CounterForm::IncMemory}, // Movie transition poll alias
        {{0x18028e764, {0xff, 0x05, 0xc2, 0xc4, 0xd3, 0x2f}},
         0x1affcac2c,
         0x0,
         CounterForm::IncMemory}, // Movie transition poll alias
        {{0x18028e79b, {0xff, 0x05, 0x8b, 0xc4, 0xd3, 0x2f}},
         0x1affcac2c,
         0x0,
         CounterForm::IncMemory}, // Movie transition poll alias
        {{0x18028e7a6, {0xff, 0x05, 0x80, 0xc4, 0xd3, 0x2f}},
         0x1affcac2c,
         0x0,
         CounterForm::IncMemory}, // Movie transition poll alias
        {{0x180261934, {0xff, 0x05, 0x12, 0x04, 0xe0, 0x2f}},
         0x1b0061d4c,
         0x1b0061d08,
         CounterForm::IncMemory}, // Card removal animation timeout

        {{0x1803adcba, {0xff, 0x05, 0x04, 0x4d, 0x11, 0x30}}, 0x1b04c29c4, 0x1b04c29c8, CounterForm::IncMemory},
        {{0x1803adcc7, {0xff, 0x05, 0xf3, 0x4c, 0x11, 0x30}}, 0x1b04c29c0, 0, CounterForm::IncMemory},
        {{0x1801e5050, {0xff, 0xc0, 0xbb, 0x01, 0x00, 0x00, 0x00, 0x89, 0x05, 0x4b, 0x68, 0xd0, 0x2e}},
         0x1aeeeb8a8,
         0x1aeeeb8ac,
         CounterForm::IncEaxSetEbx}, // Session close wait
        {{0x1801e5087, {0xff, 0xc0, 0xbb, 0x01, 0x00, 0x00, 0x00, 0x89, 0x05, 0x14, 0x68, 0xd0, 0x2e}},
         0x1aeeeb8a8,
         0x1aeeeb8ac,
         CounterForm::IncEaxSetEbx}, // Session close wait
        {{0x1801e50e5, {0xff, 0xc0, 0xbb, 0x01, 0x00, 0x00, 0x00, 0x89, 0x05, 0xb6, 0x67, 0xd0, 0x2e}},
         0x1aeeeb8a8,
         0x1aeeeb8ac,
         CounterForm::IncEaxSetEbx}, // Session close wait
        {{0x1801e512a, {0xff, 0xc0, 0xbb, 0x01, 0x00, 0x00, 0x00, 0x89, 0x05, 0x71, 0x67, 0xd0, 0x2e}},
         0x1aeeeb8a8,
         0x1aeeeb8ac,
         CounterForm::IncEaxSetEbx}, // Session close wait
        {{0x1801e5207, {0xff, 0xc0, 0x89, 0x05, 0x99, 0x66, 0xd0, 0x2e}},
         0x1aeeeb8a8,
         0x1aeeeb8ac,
         CounterForm::IncEax}, // Session close wait
        {{0x1802293f0, {0xff, 0xc0, 0x83, 0xfe, 0x02, 0x89, 0x05, 0x31, 0x19, 0x39, 0x2f}},
         0x1af5bad2c,
         0x1af5bad28,
         CounterForm::IncEaxCmpEsi2}, // Session settings wait
        {{0x180229b16, {0xff, 0x05, 0x30, 0x51, 0x49, 0x2f}},
         0x1af6bec4c,
         0x1af6bec48,
         CounterForm::IncMemory}, // Session retry and display phase
        {{0x18022acaa, {0xff, 0x05, 0x74, 0xa4, 0xc9, 0x2f}},
         0x1afec5124,
         0x1afec5120,
         CounterForm::IncMemory}, // Network initialization timeout and display phase
        {{0x18022b352, {0xff, 0xc1, 0x83, 0xf9, 0x3c, 0x89, 0x0d, 0xbb, 0x86, 0xc9, 0x2f}},
         0x1afec3a18,
         0x1afec3a10,
         CounterForm::IncEcxCmp60}, // Test-mode transition delay
        {{0x18022b37f, {0xff, 0x05, 0x8f, 0x86, 0xc9, 0x2f}}, 0x1afec3a14, 0x0, CounterForm::IncMemory}, // Please-wait
                                                                                                         // blink phase
        {{0x180205b25, {0x8d, 0x41, 0x01, 0x89, 0x05, 0x5e, 0xfb, 0x97, 0x34}},
         0x1b4b8568c,
         0x1b4b85688,
         CounterForm::LeaEaxEcx}, // Service confirmation hold
        {{0x180205b94, {0x8d, 0x42, 0x01, 0x89, 0x05, 0xef, 0xfa, 0x97, 0x34}},
         0x1b4b8568c,
         0x1b4b85688,
         CounterForm::LeaEaxEdx}, // Service confirmation hold
        {{0x180205f2b, {0x8d, 0x41, 0x01, 0x89, 0x05, 0x58, 0xf7, 0x97, 0x34}},
         0x1b4b8568c,
         0x1b4b85688,
         CounterForm::LeaEaxEcx}, // Service confirmation hold
        {{0x180205f94, {0x8d, 0x42, 0x01, 0x89, 0x05, 0xef, 0xf6, 0x97, 0x34}},
         0x1b4b8568c,
         0x1b4b85688,
         CounterForm::LeaEaxEdx}, // Service confirmation hold
        {{0x1802200d0, {0xff, 0x05, 0x8e, 0x5a, 0x96, 0x34}},
         0x1b4b85b64,
         0x1b4b85b60,
         CounterForm::IncMemory}, // Clock-setting exit marker
        {{0x1802200e0, {0x8d, 0x42, 0x01, 0x89, 0x05, 0x7b, 0x5a, 0x96, 0x34}},
         0x1b4b85b64,
         0x1b4b85b60,
         CounterForm::LeaEaxEdx}, // Clock-setting exit delay
        {{0x180221e51, {0x8d, 0x41, 0x01, 0x89, 0x05, 0x22, 0x3d, 0x96, 0x34}},
         0x1b4b85b7c,
         0x1b4b85b78,
         CounterForm::LeaEaxEcx}, // Network configuration confirmation delay
        {{0x1802220bb, {0x8d, 0x41, 0x01, 0x89, 0x05, 0xb8, 0x3a, 0x96, 0x34}},
         0x1b4b85b7c,
         0x1b4b85b78,
         CounterForm::LeaEaxEcx}, // Network configuration confirmation delay
        {{0x180221ccf, {0xff, 0xc1, 0x89, 0x0d, 0xc5, 0x3e, 0x96, 0x34}},
         0x1b4b85b9c,
         0x1b4b85b98,
         CounterForm::IncEcx}, // Network configuration long button chord
        {{0x18022f883, {0xff, 0xc0, 0x89, 0x05, 0xed, 0x97, 0xd9, 0x2f}},
         0x1affc9078,
         0x0,
         CounterForm::IncEax}, // Initialization staged delay (continuous age)
        {{0x18022f8b1, {0xff, 0xc0, 0x89, 0x05, 0xbf, 0x97, 0xd9, 0x2f}},
         0x1affc9078,
         0x0,
         CounterForm::IncEax}, // Initialization staged delay (continuous age)
        {{0x1802281d7, {0xff, 0xc0, 0x89, 0x05, 0x15, 0xcf, 0xc9, 0x2f}},
         0x1afec50f4,
         0x1afec50f0,
         CounterForm::IncEax}, // License confirmation hold
        {{0x18022822e, {0xff, 0xc0, 0x89, 0x05, 0xbe, 0xce, 0xc9, 0x2f}},
         0x1afec50f4,
         0x1afec50f0,
         CounterForm::IncEax}, // License confirmation hold
        {{0x1802280ed, {0xff, 0x05, 0x05, 0xd0, 0xc9, 0x2f}},
         0x1afec50f8,
         0x1afec50f0,
         CounterForm::IncMemory}, // License response timeout
        {{0x18022833f, {0xff, 0xc0, 0x89, 0x05, 0xb5, 0xcd, 0xc9, 0x2f}},
         0x1afec50fc,
         0x1afec5060,
         CounterForm::IncEax}, // Network startup timeout
        {{0x180227f3c, {0xff, 0xc8, 0x89, 0x05, 0x4c, 0x7d, 0xdc, 0x2e}},
         0x1aefefc90,
         0x1aefefc88,
         CounterForm::DecEax}, // License dialog countdown
        {{0x18022663b, {0x8d, 0x41, 0xff, 0x89, 0x05, 0x4c, 0x96, 0xdc, 0x2e}},
         0x1aefefc90,
         0x1aefefc88,
         CounterForm::DecEaxEcx}, // License dialog countdown alias
        {{0x1801e18ea, {0x8d, 0x41, 0x01, 0x89, 0x05, 0xb5, 0xe2, 0xe0, 0x2e}},
         0x1aefefba8,
         0x0,
         CounterForm::LeaEaxEcx}, // Network status polling age
        {{0x180473467, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0x3b, 0x62, 0x29, 0x30}},
         0x1b07096ac,
         0x0,
         CounterForm::IncR8}, // Service-button gesture window (continuous across states 3/4)
        {{0x1804734a8, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0xfa, 0x61, 0x29, 0x30}},
         0x1b07096ac,
         0x0,
         CounterForm::IncR8}, // Service-button gesture window (continuous across states 3/4)
        {{0x1804734d4, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0xce, 0x61, 0x29, 0x30}},
         0x1b07096ac,
         0x0,
         CounterForm::IncR8}, // Service-button gesture window (continuous across states 3/4)
        {{0x180473506, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0x9c, 0x61, 0x29, 0x30}},
         0x1b07096ac,
         0x0,
         CounterForm::IncR8}, // Service-button gesture window (continuous across states 3/4)
        {{0x180473567, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0x3b, 0x61, 0x29, 0x30}},
         0x1b07096ac,
         0x0,
         CounterForm::IncR8}, // Service-button gesture window (continuous across states 3/4)
        {{0x1801e2f95, {0x41, 0x8d, 0x40, 0x01, 0x89, 0x05, 0x09, 0xcc, 0xe0, 0x2e}},
         0x1aefefba8,
         0,
         CounterForm::LeaEaxR8,
         CounterBase::Global,
         0x0}, // Network status polling alias
        {{0x18024041f, {0xff, 0xc0, 0x89, 0x87, 0xdc, 0x68, 0x55, 0x00}},
         0x5568dc,
         0,
         CounterForm::IncEax,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180240590, {0x8d, 0x41, 0x01, 0x89, 0x87, 0xf4, 0x68, 0x55, 0x00}},
         0x5568f4,
         0,
         CounterForm::LeaEaxEcx,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x18024070d, {0xff, 0xc8, 0x89, 0x87, 0x9c, 0xd0, 0x44, 0x00}},
         0x44d09c,
         0,
         CounterForm::DecEax,
         CounterBase::Rdi,
         0x0}, // Shared entry countdown; flags are requests from player callbacks
        {{0x180240774, {0xff, 0xc8, 0x89, 0x87, 0x68, 0xd0, 0x44, 0x00}},
         0x44d068,
         0,
         CounterForm::DecEax,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x18024180e, {0xff, 0x87, 0x5c, 0xd1, 0x44, 0x00}},
         0x44d15c,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180241923, {0xff, 0xc0, 0x89, 0x87, 0xf0, 0x68, 0x55, 0x00}},
         0x5568f0,
         0,
         CounterForm::IncEax,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x18024198b, {0xff, 0x87, 0x58, 0xd1, 0x44, 0x00}},
         0x44d158,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x1802419ee, {0xff, 0xc0, 0x89, 0x87, 0x58, 0xd1, 0x44, 0x00}},
         0x44d158,
         0,
         CounterForm::IncEax,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180241a7c, {0xff, 0x87, 0x64, 0xd1, 0x44, 0x00}},
         0x44d164,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180241b0a, {0xff, 0xc0, 0x89, 0x87, 0x58, 0xd1, 0x44, 0x00}},
         0x44d158,
         0,
         CounterForm::IncEax,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180241b31, {0xff, 0x87, 0x64, 0xd1, 0x44, 0x00}},
         0x44d164,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x180241b88, {0xff, 0x87, 0x64, 0xd1, 0x44, 0x00}},
         0x44d164,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry scene timer
        {{0x18023b09c, {0x41, 0xff, 0x45, 0x00, 0x41, 0x8b, 0x45, 0x00}},
         0x0,
         0,
         CounterForm::IncMemoryLoadEax,
         CounterBase::R13,
         0x0}, // Entry synchronization timeout
        {{0x18023b1c2, {0xff, 0x87, 0x48, 0xd1, 0x44, 0x00}},
         0x44d148,
         0,
         CounterForm::IncMemory,
         CounterBase::Rdi,
         0x0}, // Entry data send interval
        {{0x1802fec29, {0xff, 0x47, 0x0c, 0x48, 0x8b, 0x6c, 0x24, 0x78}},
         0xc,
         0,
         CounterForm::IncMemoryRestoreRbp,
         CounterBase::Rdi,
         0x78}, // Demo playback scene waits
        {{0x1802c2767, {0xff, 0x43, 0x10, 0xe8, 0x31, 0xc9, 0xf7, 0xff}},
         16,
         0,
         CounterForm::IncMemoryCall,
         CounterBase::Rbx,
         0x18023f0a0}, // Entry player wait and native status probe
        {{0x1802c28e3, {0xff, 0x43, 0x10, 0x83, 0x7b, 0x10, 0x78}},
         16,
         0,
         CounterForm::IncMemoryCmp,
         CounterBase::Rbx,
         0x78}, // Entry player completion delay
    }},
    .handshake_update = 0x1802D1380,
    .handshake_state = 0x1B0099148,
    .handshake_age = 0x1B00991C8,
    .request_scene_update = 0x18024E420,
    .request_scene_active = 0x1AFFC9CC0,
    .request_scene_state = 0x1AFFC9CC4,
    .request_scene_age = 0x1AFFC9CC8,
    .request_scene_total = 0x1AFFC9CCC,
    .request_scene_status = 0x1B09A7DB0,
    .hold_scene_update = 0x18025ACD0,
    .hold_scene_controller = 0x1808C2EA0,
    .hold_scene_input = 0x180442800,
    .scene_abort = 0x1AF5BAD42,
    .service_gesture = 0x1804733C0,
    .service_gesture_state = 0x1B07096A8,
    .service_gesture_age = 0x1B07096AC,
    .service_gesture_edges = 0x1B07096B0,
    .tutorial_begin = {0x1802de530, {0x48, 0x63, 0x15, 0x1d, 0xd0, 0xdb, 0x2f}},
    .tutorial_end = {0x1802e076d, {0xff, 0xc8, 0x89, 0x05, 0xe3, 0xad, 0xdb, 0x2f}},
    .tutorial_phase_step = {0x1802e075c, {0x83, 0x05, 0xfd, 0xad, 0xdb, 0x2f, 0x05}},
    .tutorial_gauge = {0x1802e040b, {0x41, 0x8b, 0x0e, 0x45, 0x8b, 0x46, 0xfc, 0x44, 0x3b, 0xc1}},
    .tutorial_events = {{{{0x1802ded4a, {0x3d, 0xe0, 0x01, 0x00, 0x00}}, 480},
                         {{0x1802dedaa, {0x3d, 0xf0, 0x00, 0x00, 0x00}}, 240},
                         {{0x1802dedf5, {0x3d, 0xa2, 0x00, 0x00, 0x00}}, 162},
                         {{0x1802df5cb, {0x3d, 0x57, 0x03, 0x00, 0x00}}, 855},
                         {{0x1802df5d2, {0x3d, 0x67, 0x02, 0x00, 0x00}}, 615},
                         {{0x1802df5d9, {0x3d, 0x77, 0x01, 0x00, 0x00}}, 375},
                         {{0x1802df5f0, {0x3d, 0x1b, 0x03, 0x00, 0x00}}, 795},
                         {{0x1802df5f7, {0x3d, 0x2b, 0x02, 0x00, 0x00}}, 555},
                         {{0x1802df5fe, {0x3d, 0x3b, 0x01, 0x00, 0x00}}, 315},
                         {{0x1802df615, {0x3d, 0xdf, 0x02, 0x00, 0x00}}, 735},
                         {{0x1802df61c, {0x3d, 0xef, 0x01, 0x00, 0x00}}, 495},
                         {{0x1802df623, {0x3d, 0xff, 0x00, 0x00, 0x00}}, 255},
                         {{0x1802df63a, {0x3d, 0xa3, 0x02, 0x00, 0x00}}, 675},
                         {{0x1802df641, {0x3d, 0xb3, 0x01, 0x00, 0x00}}, 435},
                         {{0x1802df648, {0x3d, 0xc3, 0x00, 0x00, 0x00}}, 195},
                         {{0x1802df65f, {0x3d, 0x4d, 0x03, 0x00, 0x00}}, 845},
                         {{0x1802df666, {0x3d, 0x5d, 0x02, 0x00, 0x00}}, 605},
                         {{0x1802df66d, {0x3d, 0x6d, 0x01, 0x00, 0x00}}, 365},
                         {{0x1802df687, {0x3d, 0x11, 0x03, 0x00, 0x00}}, 785},
                         {{0x1802df68e, {0x3d, 0x21, 0x02, 0x00, 0x00}}, 545},
                         {{0x1802df695, {0x3d, 0x31, 0x01, 0x00, 0x00}}, 305},
                         {{0x1802df6af, {0x3d, 0xd5, 0x02, 0x00, 0x00}}, 725},
                         {{0x1802df6b6, {0x3d, 0xe5, 0x01, 0x00, 0x00}}, 485},
                         {{0x1802df6bd, {0x3d, 0xf5, 0x00, 0x00, 0x00}}, 245},
                         {{0x1802df6d7, {0x3d, 0x99, 0x02, 0x00, 0x00}}, 665},
                         {{0x1802df6de, {0x3d, 0xa9, 0x01, 0x00, 0x00}}, 425},
                         {{0x1802df6e5, {0x3d, 0xb9, 0x00, 0x00, 0x00}}, 185},
                         {{0x1802df826, {0x81, 0xf9, 0xf0, 0x00, 0x00, 0x00}}, 240},
                         {{0x1802df910, {0x3d, 0xac, 0x00, 0x00, 0x00}}, 172},
                         {{0x1802dfbbb, {0x3d, 0xf0, 0x00, 0x00, 0x00}}, 240}}},
    .tutorial_countdowns = {{{0x1802dfde8, {0xff, 0xc8, 0x89, 0x83, 0xfc, 0x0d, 0x00, 0x00}},
                             {0x1802e033e, {0xff, 0xc8, 0x89, 0x87, 0x76, 0x01, 0x00, 0x00}},
                             {0x1802dff7f, {0xff, 0xc8, 0x89, 0x05, 0xd5, 0xb5, 0xdb, 0x2f}},
                             {0x1802e05ab, {0x41, 0xff, 0x4e, 0x04, 0x41, 0x8b, 0x46, 0x08}},
                             {0x1802e05b7, {0xff, 0xc8, 0xb9, 0xb4, 0x00, 0x00, 0x00}},
                             {0x1802e0651, {0xff, 0x0f, 0x48, 0x83, 0xc7, 0x04}}}},
    .tutorial_state = 0x1b009b554,
    .tutorial_remaining = 0x1b009b558,
    .tutorial_position = 0x1b009b560,
    .tutorial_pause = 0x1b009b564,
    .tutorial_gauge_next = 0x1802e04b5,
    .tutorial_update = 0x1802de520,
    .tutorial_narration = 0x1802db1d0,
    .tutorial_sound = 0x18024c820,
    .tutorial_players = 0x1b0099950,
    .tutorial_input_delay = 0x1b009b55c,
    .tutorial_countdown_offsets = {0xdfc, 0x176, 0, 4, 8, 0},
    .tutorial_player_stride = 3584,
    .tutorial_gauge_target_distance = 4,
    .tutorial_next_effect_distance = 8,
    .tutorial_pause_states = {30, 62},
    .tutorial_pause_boundaries = {480, 240},
    .tutorial_practice_cue = {0x1802e0437, {0x3b, 0xd0, 0x0f, 0x8e, 0x24, 0xff, 0xff, 0xff}},
    .tutorial_practice_cue_yes = 0x1802e0455,
    .tutorial_practice_cue_no = 0x1802e0363,
    .tutorial_practice_cue_native = 0x1802e043f,
    .tutorial_practice_saved_r13 = 0x98,
    .summary_step = {0x1803af473, {0xff, 0x05, 0xaf, 0xa3, 0x54, 0x00, 0x41, 0xff, 0xc1}},
    .summary_event = {0x1803b0db4, {0x83, 0xf9, 0x3c, 0x75, 0x79}},
    .summary_periods = {{{0x1803afb06, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803afdcc, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b00bc, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b038c, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b0705, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b0ac5, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b0d72, {0x25, 0x03, 0x00, 0x00, 0x80}},
                         {0x1803b0f7f, {0x25, 0x03, 0x00, 0x00, 0x80}}}},
    .summary_age = 0x1808f9824,
    .summary_state = 0x1808f9820,
    .summary_overall = 0x1808f9828,
    .summary_event_yes = 0x1803b0db9,
    .summary_event_no = 0x1803b0e32,
    .summary_update = 0x1803af420,
    .summary_sound = 0x1803aecf0,
    .summary_skip = 0x1803aed40,
    .summary_row_display = 0x1803aedd0,
    .summary_digit_display = 0x1803aec50,
    .summary_rows = 0x1808f9830,
    .summary_row_targets = 0x1808f98ac,
    .summary_total_target = 0x1808f9894,
    .summary_player = 0x1808f9964,
    .summary_row_stride = 24,
    .qc_schedule = {0x1801cee8f, {0xb8, 0x89, 0x88, 0x88, 0x88}},
    .card_error_step = {0x180233dd5, {0x41, 0xff, 0xc0, 0x44, 0x89, 0x05, 0xf1, 0x54, 0xd9, 0x2f}},
    .card_error_period = {0x180233e07, {0x44, 0x3b, 0xc1, 0x0f, 0x85, 0xda, 0x00, 0x00, 0x00}},
    .qc_native_steps = {{{0x1801cefb1, {0xff, 0x05, 0xc9, 0xb4, 0xd1, 0x2e}},
                         {0x1801ceff2, {0xff, 0x05, 0x88, 0xb4, 0xd1, 0x2e}}}},
    .qc_age = 0x1aeeea480,
    .qc_phase = 0x1aeeea484,
    .qc_brightness = 0x1aeeea488,
    .qc_due = 0x1801ceee1,
    .qc_idle = 0x1801ceff2,
    .qc_update = 0x1801cee60,
    .qc_sound_ready = 0x1aeeea48a,
    .qc_audio_state = 0x195e36994,
    .card_error_age = 0x1affc92d0,
    .card_error_due = 0x180233e10,
    .card_error_idle = 0x180233eea,
    .mini_age = {0x1803fbf61, {0xff, 0x81, 0xf8, 0x4f, 0x00, 0x00}},
    .mini_player_atlas = {0x1803fa1ed, {0xff, 0x81, 0xa4, 0x00, 0x00, 0x00}},
    .mini_enemy_atlas = {0x1803f8b6f, {0xff, 0x41, 0x48, 0x33, 0xf6}},
    .mini_debris_atlas = {0x1803fbfea, {0x41, 0xff, 0x42, 0x04, 0x41, 0x83, 0x7a, 0x04, 0x05}},
    .mini_debris_motion = {0x1803fc008, {0xc4, 0xc1, 0x4a, 0x58, 0x4a, 0xec}},
    .mini_bursts = {0x1803fc16c, {0x48, 0x8d, 0x83, 0x08, 0x02, 0x00, 0x00}},
    .mini_banner = {0x1803fc391, {0xff, 0x83, 0xec, 0x4f, 0x00, 0x00}},
    .mini_hurt = {0x1803fa277, {0x8b, 0x89, 0xb4, 0x00, 0x00, 0x00}},
    .mini_fall = {0x1803fa341, {0xc5, 0xc2, 0x58, 0x8b, 0xb0, 0x00, 0x00, 0x00}},
    .mini_player_velocity = {0x1803fa502, {0xc5, 0xfa, 0x58, 0x83, 0x88, 0x00, 0x00, 0x00}},
    .mini_player_motion = {0x1803fa52a, {0xc5, 0xb2, 0x58, 0xe8, 0xc5, 0xfa, 0x11, 0x83, 0x88, 0x00, 0x00, 0x00}},
    .mini_enemy_intro = {0x1803f8c06, {0xc5, 0xfa, 0x10, 0x41, 0x1c}},
    .mini_enemy_spawn = {0x1803f8d1c, {0xff, 0x41, 0x4c, 0x8b, 0x41, 0x4c}},
    .mini_enemy_death = {0x1803f949a, {0xff, 0x41, 0x50, 0xb8, 0x67, 0x66, 0x66, 0x66}},
    .mini_particle0 = {0x1803f82fc, {0xc5, 0xfa, 0x5c, 0x1d, 0xa8, 0x05, 0x27, 0x00}},
    .mini_particle1 = {0x1803f83cf, {0xc5, 0xfa, 0x5c, 0x2d, 0xd5, 0x04, 0x27, 0x00}},
    .mini_particle2 = {0x1803f84f8, {0xc5, 0xfa, 0x10, 0x41, 0x20}},
    .mini_particle3 = {0x1803f859e, {0xc5, 0xfa, 0x10, 0x41, 0x20}},
    .mini_spin = {0x1803f8688, {0xc5, 0xfa, 0x10, 0x53, 0x34}},
    .mini_player_atlas_next = 0x1803fa225,
    .mini_enemy_atlas_next = 0x1803f8b92,
    .mini_debris_atlas_next = 0x1803fc008,
    .mini_bursts_next = 0x1803fc37c,
    .mini_banner_next = 0x1803fc471,
    .mini_hurt_next = 0x1803fa33c,
    .mini_hurt_wait = 0x1803fa37a,
    .mini_fall_next = 0x1803fa7e0,
    .mini_enemy_next = 0x1803f966d,
    .mini_enemy_death_due = 0x1803f94c1,
    .mini_enemy_death_next = 0x1803f959b,
    .mini_particle2_next = 0x1803f850c,
    .mini_particle3_next = 0x1803f85ef,
    .mini_spin_next = 0x1803f86bc,
    .mini_update = 0x1803fbf40,
    .mini_player_update = 0x1803fa1e0,
    .mini_enemy_update = 0x1803f8b60,
    .mini_particle_update = 0x1803f82c0,
    .mini_debris_speed = 0x1806688a8,
    .mini_particle_speed = 0x1806688ac,
    .mini_particle_target = 0x180668c4c,
    .mini_particle_divisor = 0x180668930,
    .mini_spin_low = 0x180668eb4,
    .mini_spin_high = 0x180668eac,
    .mini_banner_scale = 0x180668a34,
    .mini_banner_rotation = 0x1806688ec,
    .mini_gravity = 0x1806687c8,
    .mini_floor = 0x180668c4c,
    .mini_exit_floor = 0x180668e08,
    .mini_enemy_bound = 0x180668848,
    .mini_enemy_drift = 0x180668894,
    .mini_enemy_target = 0x180668c6c,
    .mini_enemy_reset_x = 0x180668dc0,
    .mini_game_kind = 0x195ee8bb8,
    .mini_input_enabled = 0x1808c03af,
    .mini_shot_input = 0x1802d93b0,
    .card_timeout_step = {0x180234744, {0xff, 0x4b, 0xe8, 0x39, 0x6b, 0xe8}},
    .card_alternate_step = {0x1802344a6, {0xff, 0xc0, 0x89, 0x05, 0x22, 0x4e, 0xd9, 0x2f}},
    .card_timeout_offset = -24,
    .card_update = 0x1802346b0,
    .card_pump = 0x180167830,
    .card_error_update = 0x180233d00,
    .card_slots = 0x1affc91c8,
    .card_count = 0x1affc91c4,
    .card_slot_stride = 128,
    .card_slot_age = 8,
    .arena_steps = {{{0x1803458a8, {0xff, 0xc0, 0x89, 0x05, 0xac, 0xc4, 0xbf, 0x02}},
                     {0x180345a17, {0xff, 0x05, 0x3f, 0xc3, 0xbf, 0x02}},
                     {0x180345dd9, {0xff, 0x05, 0x7d, 0xbf, 0xbf, 0x02}},
                     {0x180346069, {0xff, 0x05, 0xed, 0xbc, 0xbf, 0x02}},
                     {0x180346369, {0xff, 0x05, 0xed, 0xb9, 0xbf, 0x02}},
                     {0x1803466e2, {0xff, 0xc0, 0x89, 0x05, 0x72, 0xb6, 0xbf, 0x02}},
                     {0x180346fa0, {0xff, 0xc0, 0x89, 0x05, 0xb4, 0xad, 0xbf, 0x02}},
                     {0x1803479f6, {0xff, 0xc0, 0x89, 0x05, 0x5e, 0xa3, 0xbf, 0x02}}}},
    .arena_step_eax = {true, false, false, false, false, true, true, true},
    .arena_event = {0x1803466ea, {0x3d, 0x2c, 0x01, 0x00, 0x00}},
    .arena_gesture0 = {0x180347627, {0x83, 0x2d, 0x8a, 0xa7, 0xbf, 0x02, 0x01}},
    .arena_gesture1 = {0x180347638, {0x8d, 0x49, 0xff, 0x89, 0x05, 0x7b, 0xa7, 0xbf, 0x02}},
    .arena_voice_delay = {0x18034759f, {0x83, 0xe8, 0x01, 0x89, 0x06}},
    .arena_age = 0x182f41d5c,
    .arena_state = 0x182f3ffa0,
    .arena_gesture_age0 = 0x182f41db8,
    .arena_gesture_age1 = 0x182f41dc4,
    .arena_gesture_count0 = 0x182f41dbc,
    .frame_events = {{{{0x180261a2c, {0x3b, 0xf7, 0x75, 0x86, 0x45, 0x33, 0xc0}},
                       0x1b00991d8,
                       0x1802619b6,
                       0,
                       FrameLabelRegister::Rdi,
                       FrameEventTail::ZeroR8},
                      {{0x180345841, {0x3b, 0xef, 0x75, 0x05, 0xe8, 0xd6, 0x1a, 0xe7, 0xff}},
                       0x182f41a08,
                       0x18034584a,
                       0x1801b7320,
                       FrameLabelRegister::Rdi,
                       FrameEventTail::Call,
                       0x38},
                      {{0x18034584c, {0x41, 0x3b, 0xee, 0x75, 0x26}},
                       0x182f41a08,
                       0x180345877,
                       0x0,
                       FrameLabelRegister::R14,
                       FrameEventTail::None,
                       0x38},
                      {{0x180345877, {0x3b, 0xee, 0x75, 0x27, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f41a08,
                       0x1803458a2,
                       0x0,
                       FrameLabelRegister::Rsi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x1803459c2, {0x3b, 0xe8, 0x75, 0x26, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419f8,
                       0x1803459ec,
                       0x0,
                       FrameLabelRegister::Rax,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x1803459ec, {0x3b, 0xee, 0x75, 0x27, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419f8,
                       0x180345a17,
                       0x0,
                       FrameLabelRegister::Rsi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x180345d84, {0x3b, 0xef, 0x75, 0x26, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f41a00,
                       0x180345dae,
                       0x0,
                       FrameLabelRegister::Rdi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x180345dae, {0x3b, 0xee, 0x75, 0x27, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f41a00,
                       0x180345dd9,
                       0x0,
                       FrameLabelRegister::Rsi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x180346014, {0x3b, 0xef, 0x75, 0x26, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419d8,
                       0x18034603e,
                       0x0,
                       FrameLabelRegister::Rdi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x18034603e, {0x3b, 0xee, 0x75, 0x27, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419d8,
                       0x180346069,
                       0x0,
                       FrameLabelRegister::Rsi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x180346314, {0x3b, 0xee, 0x75, 0x26, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419d0,
                       0x18034633e,
                       0x0,
                       FrameLabelRegister::Rsi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x18034633e, {0x3b, 0xef, 0x75, 0x27, 0x89, 0x5c, 0x24, 0x38}},
                       0x182f419d0,
                       0x180346369,
                       0x0,
                       FrameLabelRegister::Rdi,
                       FrameEventTail::StackRbx,
                       0x38},
                      {{0x180346aac, {0x3b, 0xc5, 0x75, 0x29, 0x45, 0x33, 0xc0}},
                       0x182f415f0,
                       0x180346ad9,
                       0x0,
                       FrameLabelRegister::Rax,
                       FrameEventTail::ZeroR8,
                       0x38}}},
    .spinner_fade = {0x1801f5a1a, {0x80, 0x3d, 0x27, 0xdf, 0xcc, 0x2f, 0x00}},
    .spinner_step = {0x1801f5c02, {0xff, 0x05, 0x38, 0xdd, 0xcc, 0x2f}},
    .spinner_age = 0x1afec3940,
    .spinner_alpha = 0x1afec3944,
    .spinner_visible = 0x1afec3948,
    .spinner_draw = 0x1801f5a10,
    .spinner_draw_next = 0x1801f5a55,
    .spinner_submit = 0x1804359a0,
    .spinner_setup = 0x180434f50,
    .spinner_vertices = 0x1b0a71340,
    .spinner_buffer_index = 0x1b0a77790,
    .spinner_textures = 0x1afec3908,
    .spinner_buffer_stride = 25680,
    .dm_tutorial_begin = {0x1802e4b3e, {0x48, 0x63, 0x15, 0x73, 0x7a, 0xdb, 0x2f}},
    .dm_tutorial_end = {0x1802e6546, {0x8b, 0x05, 0xd8, 0x5d, 0xdb, 0x2f}},
    .dm_tutorial_step = {0x1802e653e, {0xff, 0xc8, 0x89, 0x05, 0x76, 0x60, 0xdb, 0x2f}},
    .dm_tutorial_position_step = {0x1802e652d, {0x83, 0x05, 0x90, 0x60, 0xdb, 0x2f, 0x05}},
    .dm_tutorial_gauge = {0x1802e62e8, {0x8b, 0x0d, 0x42, 0x60, 0xdb, 0x2f}},
    .dm_tutorial_effect = {0x1802e64e0, {0x8b, 0x06, 0x48, 0x8b, 0x4f, 0xb8}},
    .dm_tutorial_events =
        {{{{0x1802e54f5, {0x3d, 0xe0, 0x01, 0x00, 0x00}}, 480}, {{0x1802e553b, {0x3d, 0xf0, 0x00, 0x00, 0x00}}, 240},
          {{0x1802e5580, {0x3d, 0xb9, 0x00, 0x00, 0x00}}, 185}, {{0x1802e5cda, {0x3d, 0x47, 0x04, 0x00, 0x00}}, 1095},
          {{0x1802e5ce1, {0x3d, 0x57, 0x03, 0x00, 0x00}}, 855}, {{0x1802e5ce8, {0x3d, 0x67, 0x02, 0x00, 0x00}}, 615},
          {{0x1802e5cef, {0x3d, 0x77, 0x01, 0x00, 0x00}}, 375}, {{0x1802e5d06, {0x3d, 0x0b, 0x04, 0x00, 0x00}}, 1035},
          {{0x1802e5d0d, {0x3d, 0x1b, 0x03, 0x00, 0x00}}, 795}, {{0x1802e5d14, {0x3d, 0x2b, 0x02, 0x00, 0x00}}, 555},
          {{0x1802e5d1b, {0x3d, 0x3b, 0x01, 0x00, 0x00}}, 315}, {{0x1802e5d32, {0x3d, 0xcf, 0x03, 0x00, 0x00}}, 975},
          {{0x1802e5d39, {0x3d, 0xdf, 0x02, 0x00, 0x00}}, 735}, {{0x1802e5d40, {0x3d, 0xef, 0x01, 0x00, 0x00}}, 495},
          {{0x1802e5d47, {0x3d, 0xff, 0x00, 0x00, 0x00}}, 255}, {{0x1802e5d5e, {0x3d, 0x93, 0x03, 0x00, 0x00}}, 915},
          {{0x1802e5d65, {0x3d, 0xa3, 0x02, 0x00, 0x00}}, 675}, {{0x1802e5d6c, {0x3d, 0xb3, 0x01, 0x00, 0x00}}, 435},
          {{0x1802e5d73, {0x3d, 0xc3, 0x00, 0x00, 0x00}}, 195}, {{0x1802e5d8a, {0x3d, 0x3d, 0x04, 0x00, 0x00}}, 1085},
          {{0x1802e5d91, {0x3d, 0x4d, 0x03, 0x00, 0x00}}, 845}, {{0x1802e5d98, {0x3d, 0x5d, 0x02, 0x00, 0x00}}, 605},
          {{0x1802e5d9f, {0x3d, 0x6d, 0x01, 0x00, 0x00}}, 365}, {{0x1802e5db9, {0x3d, 0x01, 0x04, 0x00, 0x00}}, 1025},
          {{0x1802e5dc0, {0x3d, 0x11, 0x03, 0x00, 0x00}}, 785}, {{0x1802e5dc7, {0x3d, 0x21, 0x02, 0x00, 0x00}}, 545},
          {{0x1802e5dce, {0x3d, 0x31, 0x01, 0x00, 0x00}}, 305}, {{0x1802e5de8, {0x3d, 0xc5, 0x03, 0x00, 0x00}}, 965},
          {{0x1802e5def, {0x3d, 0xd5, 0x02, 0x00, 0x00}}, 725}, {{0x1802e5df6, {0x3d, 0xe5, 0x01, 0x00, 0x00}}, 485},
          {{0x1802e5dfd, {0x3d, 0xf5, 0x00, 0x00, 0x00}}, 245}, {{0x1802e5e17, {0x3d, 0x89, 0x03, 0x00, 0x00}}, 905},
          {{0x1802e5e1e, {0x3d, 0x99, 0x02, 0x00, 0x00}}, 665}, {{0x1802e5e25, {0x3d, 0xa9, 0x01, 0x00, 0x00}}, 425},
          {{0x1802e5e2c, {0x3d, 0xb9, 0x00, 0x00, 0x00}}, 185}}},
    .dm_tutorial_periods = {{{0x1802e4ddd, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0x9b, 0x10, 0x00, 0x00}},
                             {0x1802e4ea2, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0xd6, 0x0f, 0x00, 0x00}},
                             {0x1802e4f67, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0x11, 0x0f, 0x00, 0x00}},
                             {0x1802e502c, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0x4c, 0x0e, 0x00, 0x00}},
                             {0x1802e50f1, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0x87, 0x0d, 0x00, 0x00}},
                             {0x1802e51b6, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0xc2, 0x0c, 0x00, 0x00}},
                             {0x1802e5260, {0x83, 0xf9, 0x01, 0x0f, 0x85, 0x18, 0x0c, 0x00, 0x00}}}},
    .dm_tutorial_delays = {{{0x1802e5fac, {0xff, 0xc8, 0x89, 0x05, 0x0c, 0x66, 0xdb, 0x2f}},
                            {0x1802e5fbe, {0xff, 0xc8, 0x89, 0x05, 0x0a, 0x66, 0xdb, 0x2f}}}},
    .dm_tutorial_demo = {{{0x1802e57f2, {0x83, 0xc0, 0x10, 0x41, 0x3b, 0xc7}},
                          {0x1802e5859, {0x83, 0xc1, 0xe0, 0x8b, 0xc3}},
                          {0x1802e590b, {0x83, 0xc0, 0x20, 0x89, 0x05, 0x18, 0x6a, 0xdb, 0x2f}}}},
    .dm_tutorial_state = 0x1b009c5b8,
    .dm_tutorial_remaining = 0x1b009c5bc,
    .dm_tutorial_position = 0x1b009c5c4,
    .dm_tutorial_pause = 0x1b009c5c8,
    .dm_tutorial_phase = 0x1b009c324,
    .dm_tutorial_input_delay = 0x1b009c5c0,
    .dm_tutorial_feedback_delay = 0x1b009c5d0,
    .dm_tutorial_gauge_value = 0x1b009c330,
    .dm_tutorial_gauge_target = 0x1b009c32c,
    .dm_tutorial_gauge_next = 0x1802e640d,
    .dm_tutorial_effect_next = 0x1802e6517,
    .dm_tutorial_end_next = 0x1802e6562,
    .dm_tutorial_input_next = 0x1802e5e81,
    .dm_tutorial_update = 0x1802e4b20,
    .dm_tutorial_narration = 0x1802e2b00,
    .dm_tutorial_miss = 0x1802e2d10,
    .dm_tutorial_objects = 0x1b009b580,
    .dm_tutorial_active = 0x1b009c328,
    .dm_tutorial_lights = 0x1b07099c4,
    .dm_tutorial_clip_frame = 0x1801842a0,
    .dm_tutorial_inputs = {0x1804418a0, 0x1804418b0, 0x180441b50, 0x180441a70, 0x180441a90, 0x1804418d0, 0x1804418f0,
                           0x180441910, 0x180441930},
    .dm_tutorial_effect_object_offset = -72,
    .result_panel_queue = {0x1803ae459, {0x41, 0x2b, 0xc6, 0x89, 0x03}},
    .result_panel_update = 0x1803adca0,
    .result_panel_state = 0x1b04c29c8,
    .result_panel_age = 0x1b04c29c4,
    .result_panel_phase = 0x1b04c29c0,
    .result_panel_queue_data = 0x1b04c2c44,
    .lifecycle_wait_begin = {0x180228b9f, {0x8b, 0x05, 0xf3, 0x70, 0xdc, 0x2e}},
    .lifecycle_init_step = {0x180230ceb, {0xff, 0x05, 0x7f, 0x83, 0xd9, 0x2f}},
    .lifecycle_init_event = {0x18022f745, {0x3d, 0xe0, 0x10, 0x00, 0x00}},
    .lifecycle_wait_steps = {{{0x180228bf8, {0xff, 0x05, 0x9e, 0x70, 0xdc, 0x2e}},
                              {0x180228efb, {0xff, 0x05, 0x9b, 0x6d, 0xdc, 0x2e}}}},
    .card_release_steps = {{{0x1802617c6, {0xff, 0x03, 0x83, 0x3d, 0x15, 0x3f, 0x3c, 0x03, 0x00}},
                            {0x18026182f, {0xff, 0x03, 0xc7, 0x43, 0xf8, 0x02, 0x00, 0x00, 0x00}}}},
    .lifecycle_wait_age = 0x1aefefc9c,
    .lifecycle_wait_state = 0x1aefefc98,
    .lifecycle_wait_update = 0x180228b90,
    .lifecycle_init_age = 0x1affc9070,
    .card_release_enabled = 0x1836256e4,
    .card_release_state_offset = -8,
    .card_release_end = {0x1802619a0, {0x81, 0x3d, 0xa2, 0x03, 0xe0, 0x2f, 0x58, 0x02, 0x00, 0x00}},
    .card_release_object = 0x1b00991d8,
    .card_release_age = 0x1b0061d4c,
    .card_release_update = 0x180261740,
    .selection_frame_cue = {0x180249b3c, {0x3b, 0xce, 0x75, 0x1a, 0xba, 0x29, 0x00, 0x00, 0x00}},
    .options_preview_update = 0x1803d7580,
    .options_preview_data = 0x1b009c5d8,
    .options_preview_seek = 0x1803d3c10,
    .selection_frame_object = 0x182efb1a0,
    .selection_frame_no = 0x180249b5a,
    .options_preview_stride = 19848,
    .options_preview_scroll_offset = 18888,
    .options_preview_scroll_count = 10,
    .options_preview_repeat_offset = 18728,
    .options_preview_repeat_count = 20,
    .selection_frame_sound = 41,
    .frame_queries =
        {{{{0x18036ddf9, {0x3b, 0xc6, 0x48, 0x8b, 0x74, 0x24, 0x38}}, 0x38},
          {{0x1803b85e9, {0x3b, 0xc6, 0x48, 0x8b, 0x74, 0x24, 0x38}}, 0x38},
          {{0x18036f3f0, {0x41, 0x3b, 0x4d, 0xfc, 0x75, 0x0d}}, 0, FrameQueryForm::IndirectLabel, 0x18036f403, -4},
          {{0x18036f70c, {0x3b, 0xc1, 0x0f, 0x85, 0x9c, 0x00, 0x00, 0x00}},
           0,
           FrameQueryForm::RegisterLabel,
           0x18036f7b0}}},
    .overlay_age_steps = {{{0x1801fab4d, {0xff, 0x05, 0x85, 0x8d, 0xcc, 0x2f}},
                           {0x1801fab87, {0xff, 0x0d, 0x4b, 0x8d, 0xcc, 0x2f}}}},
    .overlay_open_query = 0x1801f8200,
    .overlay_close_query = 0x1801f82d0,
    .selection_request = {0x180303d80, {0x48, 0x89, 0x5c, 0x24, 0x10}},
    .selection_update = {0x1803049a0, {0x40, 0x53, 0x56, 0x57, 0x41, 0x54}},
    .selection_owner = 0x1808f5008,
    .selection_index = 0x1b04c77a0,
    .selection_pending = 0x1b04c7818,
    .selection_ready = {0x1803eccc8, {0x81, 0xf9, 0xa4, 0x01, 0x00, 0x00}},
    .selection_motion = 0x1803ecc70,
    .selection_ready_frame = 420,
    .selection_idle_label = "loop",
    .selection_move_label = "move",
    .category_ready = {0x1803edaa1, {0x81, 0xf9, 0xa4, 0x01, 0x00, 0x00}},
    .category_motion = 0x1803eda50,

};

inline constexpr const Profile* supported[]{&delta};
inline const Profile* selected = nullptr;
inline bool select(std::string_view sha256) {
    selected = nullptr;
    for (const auto* candidate : supported)
        if (candidate->sha256 == sha256) {
            selected = candidate;
            return true;
        }
    return false;
}
inline const Profile& get() {
    if (!selected)
        throw std::runtime_error("game address profile has not passed the hash gate");
    return *selected;
}

// Native layouts currently shared by every supported profile. A build with a
// changed layout/ABI needs a separately verified layout, not just new addresses.
namespace layout {
namespace selection {
inline constexpr size_t owner_size = 4111424, stage = 72, enabled = 60, input_mode = 120;
inline constexpr size_t lock1 = 4111324, lock2 = 4111328, skip1 = 4111345, skip2 = 4111368, main_ui = 4111400;
inline constexpr size_t valid = 1, ready = 2, direction = 4, continuation = 8, phase = 12, velocity = 16, idle = 20,
                        clip = 40;
inline constexpr size_t clip_valid_slot = 4, clip_frame_slot = 8, ui_fixture_size = 56;
inline constexpr size_t clip_seek_slot = 9;
inline constexpr size_t clip_label_slot = 7;
inline constexpr unsigned afp_operation_ordinal = 114, afp_stop = 3841, afp_play = 3840;
inline constexpr size_t category_clip = 24, category_valid = 0;
} // namespace selection
inline constexpr size_t hold_scene_size = 16, hold_scene_active = 1, hold_scene_state = 4, hold_scene_hold = 8,
                        hold_scene_close = 12;
inline constexpr size_t hold_scene_vtable_size = 8, hold_scene_update_slot = 2, hold_scene_close_slot = 6,
                        hold_scene_done_slot = 7;
inline constexpr size_t gf_number_step = 4, gf_number_age = 8, gf_number_stack_step = 0x48;

namespace mini {
inline constexpr size_t size = 20480, age = 20472, enabled = 20464, mode = 20468, enemy = 192;
inline constexpr size_t player_tile = 160, player_step = 164, player_state = 188, hurt_flag = 172, hurt_age = 180;
inline constexpr size_t player_y = 124, player_x = 128, player_vx = 136, player_accel = 144, player_lane = 148,
                        player_vy = 176, player_alpha = 184;
inline constexpr size_t enemy_tile = 68, enemy_step = 72, enemy_spawn_age = 76, enemy_death_age = 80,
                        enemy_interval = 84, enemy_state = 92;
inline constexpr size_t enemy_x = 24, enemy_y = 28, enemy_ax = 48, enemy_ay = 52, enemy_vx = 56, enemy_vy = 60;
inline constexpr size_t debris_begin = 6388, debris_count = 100, debris_stride = 112;
inline constexpr ptrdiff_t debris_active = -4, debris_step = 4, debris_position = -20;
inline constexpr size_t burst_begin = 400, burst_count = 50, burst_stride = 120;
inline constexpr ptrdiff_t burst_active = -4, burst_step = 4;
inline constexpr size_t banner_age = 20460, banner_kind = 20456, banner_alpha = 20440, banner_active = 20452;
inline constexpr size_t banner_scale = 20436, banner_rotation = 20432, banner_vscale = 20444, banner_vrotation = 20448,
                        banner_resource = 20368;
inline constexpr size_t resource_height = 8;
inline constexpr size_t particle_begin = 17488, particle_count = 30, particle_stride = 96;
inline constexpr size_t particle_x = 32, particle_y = 36, particle_vx = 40, particle_vy = 44, particle_angle = 48,
                        particle_spin = 52;
inline constexpr size_t particle_active = 64, particle_age = 84, particle_state = 88;
} // namespace mini

inline constexpr size_t judgment_feedback = 19164, judgment_player_size = 19232;
inline constexpr size_t service_io_pending = 8, service_io_hold = 16, service_io_size = 24;
inline constexpr size_t song_storage = 7600, song_ordinal = 52, song_local_wait = 116, song_special = 157;
inline constexpr size_t song_disconnect = 7464, song_phase_time = 4648, song_load_time = 7380, song_end_time = 4660,
                        song_state = 12;
inline constexpr size_t song_outer_state = 8;
inline constexpr std::array<size_t, 4> song_hold_offsets{32, 36, 44, 48};
inline constexpr size_t gf_overlay_stride = 112, gf_overlay_storage = 0x500;
inline constexpr size_t shutter_fade_age = 4, shutter_alpha = 12, shutter_fade_stride = 12;
inline constexpr size_t shutter_size = 7600, shutter_setting = 4184, shutter_disabled = 157;
inline constexpr size_t hud_counter_stride = 272;
inline constexpr size_t battle_size = 0xC00, battle_player_stride = 0xD0, battle_score_category_stride = 0x2C;
inline constexpr size_t battle_score_initial = 4, battle_score_goal = 8, battle_score_value = 12;
inline constexpr size_t battle_score_property0 = 16, battle_score_property1 = 24, battle_score_property2 = 32;
inline constexpr size_t battle_score_property_display = 4, battle_award_columns = 4;
inline constexpr size_t result_size = 0x3000, result_queue_count = 20, result_queue_stride = 16,
                        result_queue_delay = 12;
inline constexpr size_t result_countup_step = 8, result_countup_target = 16, result_countup1_step = 0x2310;
inline constexpr std::array<size_t, 3> result_countdown_offsets{0x22D0, 0x22E0, 0x22D8};
inline constexpr size_t audio_format_stride = 292, audio_bits = 0x106, audio_rate = 0x108;
inline constexpr size_t scene_seek_index = 1863, voice_stride = 0x178, voice_node_object = 24, voice_cursor_slot = 16;
inline constexpr unsigned afp_ordinal = 15;
inline constexpr size_t animation_size = 312, pose_floats = 16, pose_unknown_index = 5;
inline constexpr size_t asset = 160, track = 168, frame = 200, kind = 202, active = 206;
inline constexpr size_t transform_flags = 240, playback_flags = 241, texture = 248;
inline constexpr size_t track_timeline = 16, timeline_end = 6, timeline_scale = 8, texture_ready = 0;
inline constexpr size_t sprite_object = 0, sprite_active = 12, sprite_frame = 16;
inline constexpr size_t dm_effect_stride = 184, dm_lanes = 9;
inline constexpr size_t entry_display_cache = 0x551110, entry_fixture_size = 0x556a00, entry_warning_counter = 4;
inline constexpr size_t note_fade1 = 4188, note_fade2 = 4190;
// Additional fields needed to exercise native routines in isolated fixtures.
inline constexpr size_t track_size = 112, track_positions = 24, track_alpha = 56;
inline constexpr size_t callback_size = 40, callback_function = 8;
inline constexpr size_t parent = 176, child = 184, callback_count = 210, callbacks = 216;
inline constexpr size_t pose_alpha_index = 9;
inline constexpr size_t sprite_size = 24, sprite_limit0 = 9, sprite_limit1 = 11, sprite_step = 20;
inline constexpr size_t sprite_bank_size = 1104;
inline constexpr size_t sprite_subframe = 8, sprite_cell = 10, dm_sprite_count = 36, nested_sprite_count = 8;
inline constexpr size_t gf_extra_sprite_offset = 24;
inline constexpr uint32_t light_period = 124, light_pattern_step = 8;
inline constexpr size_t secondary_state = 8, secondary_age = 28, secondary_countdown = 36;
inline constexpr size_t preview_positions = 0x49C8;
inline constexpr int preview_wrap = 1080, preview_period = 1200, preview_event_period = 600;
inline constexpr size_t hit_long_age = 72;
inline constexpr size_t tally_row_stride = 16, tally_columns = 3, tally_rows = 4;
inline constexpr ptrdiff_t tally_target_offset = -68, tally_value_offset = -4;
inline constexpr size_t option_held = 1174 * 4, option_repeat_count = 8, option_key_stride = 20;
inline constexpr size_t option_speed_enabled = 4172, option_speed = 4174, option_speed_previous = 4176;
inline constexpr size_t option_speed_age = 4178, option_speed_from = 4688;
inline constexpr size_t option_size = 1184 * 4;
inline constexpr size_t controller_table = 0, controller_index = 12, controller_phase = 16;
inline constexpr size_t controller_update_age = 24, controller_draw_age = 28, controller_size = 32;
inline constexpr uint32_t service_render_height = 720;
} // namespace layout
} // namespace gd::target
