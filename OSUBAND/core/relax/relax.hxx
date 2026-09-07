#pragma once

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <core/relax/nt_input.hxx>
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <iterator>
#include <random>
#include <cmath>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <climits>

namespace relax {

    enum class tap_style_t : int { alternate = 0, singletap = 1 };

    class c_relax {
    public:
        bool enabled = false;
        float ur = 65.f;
        int tap_style = static_cast<int>( tap_style_t::alternate );
        int singletap_bpm_cap = 100;
        float k1_hold_center = 48.f;
        float k1_hold_spread = 12.f;
        float k2_hold_center = 48.f;
        float k2_hold_spread = 12.f;
        float hold_floor = 15.f;
        float hold_ceiling = 115.f;
        int32_t manual_offset_ms = 0;

        [[nodiscard]] size_t queue_size() const { std::lock_guard lock(m_mtx); return m_click_queue.size(); }
        [[nodiscard]] bool is_synced() const { return true; }
        [[nodiscard]] int last_hit_obj_idx() const { std::lock_guard lock(m_mtx); return m_last_hit_obj_idx; }
        [[nodiscard]] bool is_active() const { std::lock_guard lock(m_mtx); return enabled && m_in_play && !m_click_queue.empty(); }

        void on_leave_play(const osu::game_snapshot_t& game) {
            std::lock_guard lock(m_mtx);
            leave_play(game);
        }

        void update(const osu::game_snapshot_t& game, const osu::beatmap_data_t& map) {
            std::lock_guard lock(m_mtx);
            if (!enabled || game.cur_state != osu::game_state_t::play || !map.loaded || map.objects.empty()) {
                if (m_in_play) leave_play(game);
                return;
            }

            m_in_play = true;
            const int game_time = game.cur_time;

            if (game_time < m_last_game_time - 200 ||
                (m_last_update_game_time >= 0 && game_time - m_last_update_game_time > 300)) {
                // Do not replay stale edges after retry, a reader stall, alt-tab or a long frame.
                resync_to_current(game, map);
            }

            schedule_clicks(game, map);
            advance_past_objects(game, map);
            purge_stale(game_time);

            if (m_click_queue.size() > 96) {
                // A healthy 900 ms look-ahead is tiny. A larger queue means the reader/timeline
                // jumped or the map is malformed; fail closed instead of stalling the game thread.
                resync_to_current(game, map);
                schedule_clicks(game, map);
            }

            flush_queue(game);
            m_last_game_time = game_time;
            m_last_update_game_time = game_time;
        }

    private:
        struct scheduled_click_t {
            int press_time = 0;
            int release_time = 0;
            WORD key = 0;
            bool pressed = false;
            bool released = false;
        };

        std::vector<scheduled_click_t> m_click_queue;
        bool m_in_play = false;
        int m_last_game_time = 0;
        int m_last_update_game_time = -1;
        int m_last_hit_obj_idx = -1;
        int m_scheduled_through_idx = -1;
        int m_last_click_time = -99999;
        bool m_use_k2_next = false;
        bool m_left_down = false;
        bool m_right_down = false;
        WORD m_left_vk = 0;
        WORD m_right_vk = 0;
        int m_last_audio_time = 0;
        double m_last_audio_sync_time = 0.0;
        float m_last_jitter = 0.f;
        int m_stream_length = 0;
        c_nt_input m_nt;
        mutable std::mutex m_mtx;

        std::mt19937 m_rng{ []() -> uint32_t {
            try { return std::random_device{}(); }
            catch (...) { return static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()); }
        }() };
        std::normal_distribution<float> m_norm{0.f, 1.f};
        std::uniform_real_distribution<float> m_unit{0.f, 1.f};
        std::uniform_real_distribution<float> m_sub_ms{-1.49f, 1.49f};

        void leave_play(const osu::game_snapshot_t& game) {
            release_all_keys(game);
            m_click_queue.clear();
            m_last_hit_obj_idx = -1;
            m_scheduled_through_idx = -1;
            m_last_click_time = -99999;
            m_in_play = false;
            m_use_k2_next = false;
            m_last_audio_time = 0;
            m_last_audio_sync_time = 0.0;
            m_last_jitter = 0.f;
            m_stream_length = 0;
            m_last_update_game_time = -1;
        }

        float generate_hold(float center, float spread) {
            const float z = m_norm(m_rng);
            const float shaped = z + 0.3f * (z * z - 1.f);
            float hold = center + shaped * spread;
            hold += (m_unit(m_rng) - 0.5f) * 2.f;
            return std::clamp(hold, hold_floor, hold_ceiling);
        }

        float hit_jitter(const osu::beatmap_data_t& map) {
            const float sigma = ur / 10.f;
            const float new_jitter = m_norm(m_rng) * (sigma * 2.2f) + m_sub_ms(m_rng);
            const float jitter = 0.6f * m_last_jitter + 0.4f * new_jitter;
            m_last_jitter = jitter;
            float actual_od = map.od;
            if (map.hr) actual_od = std::min(10.f, actual_od * 1.4f);
            if (map.ez) actual_od *= 0.5f;
            const float hit_window_300 = 79.5f - 6.f * actual_od;
            return std::clamp(jitter, -std::max(5.f, hit_window_300 - 1.f), std::max(5.f, hit_window_300 - 1.f));
        }

        static float bpm_from_interval(int interval_ms) {
            return interval_ms > 0 ? 60000.f / static_cast<float>(interval_ms) : 9999.f;
        }

        static void resolve_keys(const osu::game_snapshot_t& game, WORD& k1, WORD& k2) {
            k1 = static_cast<WORD>(game.left_key);
            k2 = static_cast<WORD>(game.right_key);
            if (!k1) k1 = 'Z';
            if (!k2) k2 = 'X';
        }

        bool inject_key(WORD vk, bool down) {
            if (!vk) return false;
            if (m_nt.available()) return down ? m_nt.press(vk) : m_nt.release(vk);

            // Keep the fallback observable: if Windows rejects the event, do not mark
            // the key as held. This prevents unmatched releases after focus/input stalls.
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            if (!down) in.ki.dwFlags = KEYEVENTF_KEYUP;
            return input::send_inputs(1, &in, sizeof(INPUT)) == 1;
        }

        void release_all_keys(const osu::game_snapshot_t&) {
            if (m_left_down) inject_key(m_left_vk, false);
            if (m_right_down) inject_key(m_right_vk, false);
            m_left_down = m_right_down = false;
            m_left_vk = m_right_vk = 0;
        }

        bool press_key(WORD vk, bool& down, WORD& held_vk) {
            if (!vk || down) return false;
            if (!inject_key(vk, true)) return false;
            down = true;
            held_vk = vk;
            return true;
        }

        void release_key(bool& down, WORD& held_vk) {
            if (!down || !held_vk) return;
            inject_key(held_vk, false);
            down = false;
            held_vk = 0;
        }

        void reset_state(const osu::game_snapshot_t& game) {
            release_all_keys(game);
            m_click_queue.clear();
            m_last_hit_obj_idx = -1;
            m_scheduled_through_idx = -1;
            m_last_click_time = -99999;
            m_use_k2_next = false;
            m_last_audio_time = 0;
            m_last_audio_sync_time = 0.0;
            m_last_jitter = 0.f;
            m_stream_length = 0;
            m_last_update_game_time = -1;
        }

        void resync_to_current(const osu::game_snapshot_t& game, const osu::beatmap_data_t& map) {
            release_all_keys(game);
            m_click_queue.clear();
            const auto it = std::lower_bound(map.objects.begin(), map.objects.end(), game.cur_time - 60,
                [](const osu::hit_object_t& obj, int t) { return obj.start_time < t; });
            const int previous = static_cast<int>(std::distance(map.objects.begin(), it)) - 1;
            m_last_hit_obj_idx = previous;
            m_scheduled_through_idx = previous;
            m_last_click_time = -99999;
            m_use_k2_next = false;
            m_last_audio_time = game.cur_time;
            m_last_audio_sync_time = get_time_ms();
            m_last_jitter = 0.f;
            m_stream_length = 0;
            m_last_update_game_time = game.cur_time;
        }

        bool should_alternate(int inter_tap_ms) const {
            if (static_cast<tap_style_t>(tap_style) == tap_style_t::alternate) return true;
            return bpm_from_interval(inter_tap_ms) >= static_cast<float>(singletap_bpm_cap);
        }

        void advance_past_objects(const osu::game_snapshot_t& game, const osu::beatmap_data_t& map) {
            while (m_last_hit_obj_idx + 1 < static_cast<int>(map.objects.size())) {
                const auto& obj = map.objects[static_cast<size_t>(m_last_hit_obj_idx + 1)];
                if (obj.start_time < game.cur_time - 50) ++m_last_hit_obj_idx; else break;
            }
        }

        void purge_stale(int game_time) {
            m_click_queue.erase(std::remove_if(m_click_queue.begin(), m_click_queue.end(),
                [game_time](const scheduled_click_t& c) {
                    return c.released || (!c.pressed && c.press_time < game_time - 75);
                }), m_click_queue.end());
        }

        int key_busy_until(WORD key) const {
            int until = -1;
            for (const auto& c : m_click_queue)
                if (c.key == key && !c.released) until = std::max(until, c.release_time);
            return until;
        }

        void schedule_clicks(const osu::game_snapshot_t& game, const osu::beatmap_data_t& map) {
            WORD k1 = 0, k2 = 0;
            resolve_keys(game, k1, k2);
            int last_obj_start_time = -99999;
            if (m_scheduled_through_idx >= 0 && m_scheduled_through_idx < static_cast<int>(map.objects.size()))
                last_obj_start_time = map.objects[static_cast<size_t>(m_scheduled_through_idx)].start_time;

            constexpr int k_lookahead_ms = 900;
            for (int i = m_scheduled_through_idx + 1; i < static_cast<int>(map.objects.size()); ++i) {
                const auto& obj = map.objects[static_cast<size_t>(i)];
                if (obj.end_time < game.cur_time - 60) {
                    m_scheduled_through_idx = i;
                    last_obj_start_time = obj.start_time;
                    m_stream_length = 0;
                    continue;
                }
                if (obj.start_time > game.cur_time + k_lookahead_ms) break;

                const int prev_interval = obj.start_time - last_obj_start_time;
                m_stream_length = (prev_interval > 0 && prev_interval < 100) ? (m_stream_length + 1) : 0;
                last_obj_start_time = obj.start_time;

                const float jitter_ms = hit_jitter(map);
                int press_time = obj.start_time + static_cast<int>(std::round(jitter_ms)) + manual_offset_ms;
                const bool is_slider = (obj.type & static_cast<uint8_t>(osu::hit_object_type_t::slider)) != 0;
                const bool is_spinner = (obj.type & static_cast<uint8_t>(osu::hit_object_type_t::spinner)) != 0;
                const int natural_hold = std::max(0, obj.end_time - obj.start_time);

                int next_interval = 9999;
                if (i + 1 < static_cast<int>(map.objects.size()))
                    next_interval = std::max(0, map.objects[static_cast<size_t>(i + 1)].start_time - obj.start_time);

                int hold_dur = 0;
                if ((is_slider && natural_hold >= 30) || is_spinner) {
                    // Keep the key through the slider/spinner tail, but never create a runaway hold.
                    const int tail_jitter = static_cast<int>(std::round((m_unit(m_rng) * 2.f - 1.f) * 4.f));
                    hold_dur = std::max(15, obj.end_time - press_time + tail_jitter);
                    hold_dur = std::min(hold_dur, natural_hold + 80);
                } else {
                    float center = m_use_k2_next ? k2_hold_center : k1_hold_center;
                    float spread = m_use_k2_next ? k2_hold_spread : k1_hold_spread;
                    if (next_interval < 250) {
                        const float max_center = std::max(34.f, static_cast<float>(next_interval) * 0.52f);
                        if (center > max_center) {
                            spread *= std::sqrt(std::max(0.1f, max_center / std::max(center, 1.f)));
                            center = max_center;
                        }
                    }
                    center += std::min(static_cast<float>(m_stream_length) * 1.3f, 20.f);
                    center = std::min(center, static_cast<float>(next_interval) * 0.85f);
                    hold_dur = static_cast<int>(generate_hold(center, spread));
                }

                const int inter_tap = press_time - m_last_click_time;
                if (should_alternate(inter_tap)) m_use_k2_next = !m_use_k2_next;

                WORD chosen = m_use_k2_next ? k2 : k1;
                WORD other = m_use_k2_next ? k1 : k2;
                if (!chosen) continue;

                int chosen_busy = key_busy_until(chosen);
                int other_busy = other ? key_busy_until(other) : INT_MAX;
                if (chosen_busy > press_time + 2 && other && other_busy <= press_time + 2) {
                    chosen = other;
                    std::swap(chosen_busy, other_busy);
                }
                if (chosen_busy > press_time + 2) {
                    // Never cut an active slider hold. A tiny delay is safer than releasing the tail.
                    const int delayed = chosen_busy + 1;
                    if (delayed - press_time <= 18) press_time = delayed;
                    else {
                        m_scheduled_through_idx = i;
                        continue;
                    }
                }

                const int release_time = press_time + std::max(12, hold_dur);
                m_click_queue.push_back({press_time, release_time, chosen, false, false});
                m_last_click_time = press_time;
                m_scheduled_through_idx = i;
                if (m_click_queue.size() >= 96) break;
            }
        }

        inline double get_time_ms() const {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        double get_interpolated_game_time(const osu::game_snapshot_t& game) {
            const double now = get_time_ms();
            const int gt = game.cur_time;
            if (gt != m_last_audio_time) {
                m_last_audio_time = gt;
                m_last_audio_sync_time = now;
            }
            double elapsed = now - m_last_audio_sync_time;
            const double speed = game.speed_mult > 0.01f ? game.speed_mult : 1.f;
            // Never extrapolate across a stalled reader frame.
            if (elapsed > 24.0 / speed) elapsed = 0.0;
            return static_cast<double>(gt) + elapsed * speed;
        }

        void flush_queue(const osu::game_snapshot_t& game) {
            const double est_gt = get_interpolated_game_time(game);
            WORD k1 = 0, k2 = 0;
            resolve_keys(game, k1, k2);

            int dispatched = 0;
            while (dispatched < 32) {
                scheduled_click_t* next = nullptr;
                int when = 0;
                bool release = false;
                for (auto& c : m_click_queue) {
                    if (c.released) continue;
                    const bool is_release = c.pressed;
                    const int edge_time = is_release ? c.release_time : c.press_time;
                    if (static_cast<double>(edge_time) > est_gt) continue;
                    if (!next || edge_time < when || (edge_time == when && is_release && !release)) {
                        next = &c;
                        when = edge_time;
                        release = is_release;
                    }
                }
                if (!next) break;
                ++dispatched;

                bool& down = (next->key == k1) ? m_left_down : m_right_down;
                WORD& held = (next->key == k1) ? m_left_vk : m_right_vk;
                if (release) {
                    release_key(down, held);
                    next->released = true;
                } else {
                    if (down) {
                        // This should be prevented by the scheduler. Recover without leaving a key stuck.
                        release_key(down, held);
                    }
                    if (!press_key(next->key, down, held)) break;
                    next->pressed = true;
                }
            }

            if (dispatched >= 32) {
                // Do not spend an entire frame catching up after a timeline glitch.
                release_all_keys(game);
                m_click_queue.clear();
                m_scheduled_through_idx = m_last_hit_obj_idx;
                m_last_click_time = -99999;
                m_last_audio_time = game.cur_time;
                m_last_audio_sync_time = get_time_ms();
            }

            m_click_queue.erase(std::remove_if(m_click_queue.begin(), m_click_queue.end(),
                [](const scheduled_click_t& c) { return c.released; }), m_click_queue.end());
        }
    };
}
