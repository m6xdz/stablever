#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <impl/struct/game_snapshot.hxx>
#include <core/aim_assist/aimbot.hxx>
#include <core/relax/relax.hxx>
#include <core/replay/replay_bot.hxx>
#include <core/threads/cache.hxx>
#include <impl/input/mouse_hook.hxx>
#include <impl/config/config_store.hxx>
#include <impl/ui/studio.hxx>
#include <impl/util/texture_loader.hxx>
#include <impl/cloud/avatar.hxx>
#include <future>
#include <optional>
#include <unordered_map>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>

namespace ui {

    class c_overlay {
    public:
        using snapshot_fn = std::function<osu::full_snapshot_t( )>;

        bool create( HINSTANCE instance );
        void destroy( );
        bool pump( );
        [[nodiscard]] HWND hwnd( ) const { return m_hwnd; }

        void set_snapshot_source( snapshot_fn fn ) { m_snapshot_fn = std::move( fn ); }
        void set_cache( threads::c_cache* cache ) { m_cache = cache; }

        void tick_modules( const osu::game_snapshot_t& game, const osu::beatmap_data_t& beatmap );
        void reset_modules( const osu::game_snapshot_t& game );
        void apply_custom_keys( osu::game_snapshot_t& game ) const;
        config::settings_t capture_settings( ) const;
        void apply_settings( const config::settings_t& s );

        aim_assist::c_aimbot& aim( ) { return m_aim; }
        relax::c_relax& relax( ) { return m_relax; }
        replay::c_replay_bot& replay( ) { return m_replay; }

        void set_account(const std::string& name,const std::string& plan,const std::string& avatar="",const std::string& id="") {m_user=name;m_plan=plan;m_avatar_url=avatar;m_user_id=id;m_authorized=true;m_studio.user=name;m_studio.plan=plan;m_studio.user_id=id;m_studio.authorized=true;}
        void set_authorized(bool value,uint64_t deadline=UINT64_MAX){m_authorized=value;m_auth_deadline=deadline;}
        bool stream_proof = false;

        static constexpr int MENU_W = 980;
        static constexpr int MENU_H = 650;

    private:
        HWND m_hwnd = nullptr;
        bool m_visible = false;
        bool m_f4_was_down = false;
        snapshot_fn m_snapshot_fn;
        threads::c_cache* m_cache = nullptr;

        aim_assist::c_aimbot m_aim;
        input::c_mouse_hook m_mouse_hook;
        relax::c_relax m_relax;
        replay::c_replay_bot m_replay;

        ID3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        IDXGISwapChain* m_swap_chain = nullptr;
        ID3D11RenderTargetView* m_rtv = nullptr;

        int m_tab = 0;
        stable_ui::model m_studio;
        char m_replay_path_utf8[ 512 ]{};
        char m_config_name_utf8[ 128 ]{};
        char m_config_description_utf8[ 240 ]{};
        std::vector<config::profile_meta_t> m_cloud_profiles;
        std::string m_config_status;
        int m_config_selected = -1;
        bool m_cloud_busy=false;
        struct cloud_result {int kind;cloud::json data;std::string error;};
        std::future<cloud_result> m_cloud_job;
        cloud::avatar_cache m_avatars;
        std::string m_user="OSU!BAND member",m_plan="Stable",m_user_id,m_avatar_url,m_active_stamp,m_pending_name,m_review_stamp;
        ImTextureID m_user_avatar=ImTextureID_Invalid;
        std::vector<ImTextureID> m_profile_avatars;
        std::optional<config::settings_t> m_pending_config;
        std::unordered_map<std::string,std::string> m_known_review_status;
        uint64_t m_next_cloud_poll=0;
        bool m_hud_enabled=true;
        std::atomic<bool> m_authorized{false};
        std::atomic<uint64_t> m_auth_deadline{UINT64_MAX};
        struct toast_t{std::string title,detail;uint64_t born=0;bool positive=true;};
        std::vector<toast_t> m_toasts;

        osu::game_state_t m_prev_state = osu::game_state_t::unknown;
        int32_t m_prev_map_id = -1;
        int32_t  m_prev_game_time = -1;
        uint64_t m_game_time_stall_start_ms = 0;
        std::string m_prev_map_sig;

        int m_custom_left_key = 'Z';
        int m_custom_right_key = 'X';
        int m_menu_keybind = VK_F4;
        bool m_waiting_left = false;
        bool m_waiting_right = false;
        bool m_waiting_menu = false;

        int m_menu_offset_x = 0;
        int m_menu_offset_y = 0;
        bool m_menu_dragged = false;
        bool m_streamproof_hide_cursor = false;

        float m_anim_time = 0.f;
        float m_menu_open_anim = 0.f;
        bool m_menu_was_open = false;



        struct trail_point_t {
            float x = 0.f;
            float y = 0.f;
            double time = 0.0;
        };
        std::vector<trail_point_t> m_cursor_history;

        void cloud_task(int kind,cloud::json payload=cloud::json::object());
        void cloud_tick();
        void refresh_cloud();
        void notify(std::string title,std::string detail={},bool positive=true);
        void draw_toasts();
        bool osu_foreground() const;
        static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp );
        bool init_d3d( );
        void cleanup_d3d( );
        void render_frame( );
        void draw_menu( const osu::full_snapshot_t& snap );
        void update_overlay_position( );
        void apply_visibility( );
        void handle_hotkeys( );
    };

}