#include <impl/ui/overlay.hxx>
#include <impl/ui/theme.hxx>
#include <impl/ui/animations.hxx>
#include <impl/ui/elements.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <ShlObj.h>
#include <shobjidl.h>
#include <algorithm>
#include <random>
#include <cstring>
#include <cstdint>

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "ole32.lib" )
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace {

    inline uint64_t get_time_ms( ) {
        static LARGE_INTEGER freq;
        static bool init = false;
        if ( !init ) {
            QueryPerformanceFrequency( &freq );
            init = true;
        }
        LARGE_INTEGER time;
        QueryPerformanceCounter( &time );
        return static_cast<uint64_t>( static_cast<double>( time.QuadPart ) / static_cast<double>( freq.QuadPart ) * 1000.0 );
    }

    bool overlay_aim_hook_transform(
        void* ctx, POINT pen, const MSLLHOOKSTRUCT& raw, POINT* out ) {
        (void)pen;
        if ( !ctx || !out ) return false;
        auto* overlay = static_cast<ui::c_overlay*>( ctx );
        const auto adjusted = overlay->aim( ).apply_hook_move( pen, raw );
        if ( !adjusted ) return false;
        *out = *adjusted;
        return true;
    }

    inline float& hover_anim( uint64_t key ) {
        static std::unordered_map<uint64_t, float> anims;
        return anims[ key ];
    }


}

namespace ui {

    bool c_overlay::create( HINSTANCE instance ) {
        ImGui_ImplWin32_EnableDpiAwareness( );

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof( wc );
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"OSUBAND.UI";
        RegisterClassExW( &wc );

        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"OSU!BAND", WS_POPUP,
            0, 0, MENU_W, MENU_H,
            nullptr, nullptr, instance, this );

        if ( !m_hwnd ) return false;

        // Use an explicit black color-key for the untouched DX11 backbuffer.
        // The previous full-window LWA_ALPHA=255 path could make transparent pixels
        // opaque black in fullscreen/compositor edge cases. UI panels are near-black,
        // never exact RGB(0,0,0), so the color key only removes the clear surface.
        SetLayeredWindowAttributes( m_hwnd, RGB(0,0,0), 0, LWA_COLORKEY );
        MARGINS glass{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea( m_hwnd, &glass );

        ShowWindow( m_hwnd, SW_HIDE );
        UpdateWindow( m_hwnd );

        if ( !init_d3d( ) ) return false;

        IMGUI_CHECKVERSION( );
        ImGui::CreateContext( );
        ImGuiIO& io = ImGui::GetIO( );
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        stable_ui::load_fonts("C:\\Windows\\Fonts\\segoeui.ttf");

        ImGuiStyle& style = ImGui::GetStyle( );
        style.WindowRounding = 12.f;
        style.WindowBorderSize = 0.f;

        ImGui_ImplWin32_Init( m_hwnd );
        ImGui_ImplDX11_Init( m_device, m_context );

        m_mouse_hook.set_filter_injected_only( true );
        m_mouse_hook.set_transform( overlay_aim_hook_transform, this );
        m_mouse_hook.install( );

        stable_ui::load_preferences();
        m_aim.start();
        refresh_cloud();

        return true;
    }

    void c_overlay::destroy( ) {
        if(m_cloud_job.valid())m_cloud_job.wait();
        m_avatars.clear();
        m_aim.stop( );
        m_mouse_hook.uninstall( );

        ImGui_ImplDX11_Shutdown( );
        ImGui_ImplWin32_Shutdown( );
        ImGui::DestroyContext( );
        cleanup_d3d( );
        if ( m_hwnd ) {
            DestroyWindow( m_hwnd );
            m_hwnd = nullptr;
        }
    }

    bool c_overlay::pump( ) {
        MSG msg;
        while ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) ) {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
            if ( msg.message == WM_QUIT )
                return false;
        }

        static bool last_proof=false;static bool affinity_set=false;
        if(!affinity_set||last_proof!=stream_proof){SetWindowDisplayAffinity(m_hwnd,stream_proof?WDA_EXCLUDEFROMCAPTURE:WDA_NONE);last_proof=stream_proof;affinity_set=true;}
        cloud_tick();
        handle_hotkeys( );
        update_overlay_position( );
        apply_visibility( );
        render_frame( );
        return true;
    }

    bool c_overlay::osu_foreground() const {
        const HWND osu=input::target_window();if(!osu||!IsWindow(osu)||IsIconic(osu))return false;
        const HWND fg=GetForegroundWindow();return fg&&GetAncestor(fg,GA_ROOT)==GetAncestor(osu,GA_ROOT);
    }

    void c_overlay::handle_hotkeys( ) {
        // Menu visibility is independent from osu! foreground state. This keeps the
        // editor usable on desktop and avoids fullscreen focus stealing.
        if(m_waiting_menu){m_f4_was_down=false;return;}
        const bool menu_down=(GetAsyncKeyState(m_menu_keybind)&0x8000)!=0;
        if(menu_down&&!m_f4_was_down)m_visible=!m_visible;
        m_f4_was_down=menu_down;
    }

    void c_overlay::update_overlay_position( ) {
        if ( !m_hwnd ) return;
        static uint64_t next_update_ms = 0;
        const uint64_t now_ms = GetTickCount64();
        if(now_ms < next_update_ms) return;
        next_update_ms = now_ms + 100;

        int x=0,y=0,w=0,h=0;
        const HWND osu=input::target_window();
        RECT client{};
        if(osu && IsWindow(osu) && !IsIconic(osu) && playfield::get_playfield_rect(osu,client)) {
            x=client.left; y=client.top; w=client.right-client.left; h=client.bottom-client.top;
        } else if(m_visible) {
            // When osu! is minimized/closed, keep the settings menu on the monitor
            // the user is currently working on instead of forcibly closing it.
            HWND anchor=GetForegroundWindow();
            HMONITOR mon=MonitorFromWindow(anchor?anchor:m_hwnd,MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};mi.cbSize=sizeof(mi);
            if(GetMonitorInfoW(mon,&mi)){x=mi.rcWork.left;y=mi.rcWork.top;w=mi.rcWork.right-mi.rcWork.left;h=mi.rcWork.bottom-mi.rcWork.top;}
        }
        if(w<=0||h<=0)return;
        static RECT previous{};
        RECT current{x,y,x+w,y+h};
        if(!EqualRect(&previous,&current)){SetWindowPos(m_hwnd,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);previous=current;}
    }

    void c_overlay::apply_visibility( ) {
        if(!m_hwnd)return;
        const HWND osu=input::target_window();
        const bool game_window=osu&&IsWindow(osu)&&!IsIconic(osu);
        const bool show=m_visible||(game_window&&m_hud_enabled)||!m_toasts.empty();
        if(!show){ShowWindow(m_hwnd,SW_HIDE);return;}
        LONG ex=GetWindowLongW(m_hwnd,GWL_EXSTYLE);
        ex|=WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_NOACTIVATE;
        ex&=~WS_EX_APPWINDOW;
        if(m_visible)ex&=~WS_EX_TRANSPARENT;else ex|=WS_EX_TRANSPARENT;
        SetWindowLongW(m_hwnd,GWL_EXSTYLE,ex);
        ShowWindow(m_hwnd,SW_SHOWNOACTIVATE);
    }

    LRESULT CALLBACK c_overlay::wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp ) {
        if ( ImGui_ImplWin32_WndProcHandler( hwnd, msg, wp, lp ) )
            return true;

        if ( msg == WM_NCCREATE ) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>( lp );
            SetWindowLongPtrW( hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( cs->lpCreateParams ) );
        }

        auto* self = reinterpret_cast<c_overlay*>( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
        if ( msg == WM_DESTROY ) {
            PostQuitMessage( 0 );
            return 0;
        }
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_ACTIVATE)return 0;
        if ( msg == WM_SETCURSOR ) {
            if ( self && self->stream_proof && self->m_streamproof_hide_cursor ) {
                SetCursor( nullptr );
                return 1;
            }
            SetCursor( LoadCursorW( nullptr, IDC_ARROW ) );
            return 1;
        }
        if ( msg == WM_SIZE && self && self->m_swap_chain ) {
            UINT w = LOWORD( lp ), h = HIWORD( lp );
            if ( w == 0 || h == 0 ) return 0;
            if ( self->m_rtv ) {
                self->m_rtv->Release( );
                self->m_rtv = nullptr;
            }
            self->m_swap_chain->ResizeBuffers( 0, w, h, DXGI_FORMAT_UNKNOWN, 0 );
            ID3D11Texture2D* back = nullptr;
            if ( SUCCEEDED( self->m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back ) ) ) && back ) {
                self->m_device->CreateRenderTargetView( back, nullptr, &self->m_rtv );
                back->Release( );
            }
        }
        return DefWindowProcW( hwnd, msg, wp, lp );
    }

    bool c_overlay::init_d3d( ) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL level{};
        if ( D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &sd, &m_swap_chain, &m_device, &level, &m_context ) != S_OK )
            return false;

        ID3D11Texture2D* back = nullptr;
        m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back ) );
        if ( !back ) return false;
        m_device->CreateRenderTargetView( back, nullptr, &m_rtv );
        back->Release( );
        return m_rtv != nullptr;
    }

    void c_overlay::cleanup_d3d( ) {
        if ( m_rtv ) { m_rtv->Release( ); m_rtv = nullptr; }
        if ( m_swap_chain ) { m_swap_chain->Release( ); m_swap_chain = nullptr; }
        if ( m_context ) { m_context->Release( ); m_context = nullptr; }
        if ( m_device ) { m_device->Release( ); m_device = nullptr; }
    }

    void c_overlay::notify(std::string title,std::string detail,bool positive){
        if(m_toasts.size()>=5)m_toasts.erase(m_toasts.begin());m_toasts.push_back({std::move(title),std::move(detail),GetTickCount64(),positive});
    }
    void c_overlay::draw_toasts(){
        auto* d=ImGui::GetForegroundDrawList();const auto disp=ImGui::GetIO().DisplaySize;const uint64_t now=GetTickCount64();float y=72.f;
        for(size_t i=0;i<m_toasts.size();){auto&t=m_toasts[i];float age=float(now-t.born)/1000.f;if(age>4.4f){m_toasts.erase(m_toasts.begin()+i);continue;}float a=age<.28f?age/.28f:(age>3.8f?std::max(0.f,(4.4f-age)/.6f):1.f);float slide=(1.f-a)*30.f,w=350.f,h=t.detail.empty()?52.f:70.f;ImVec2 q(disp.x-w-20.f+slide,y);ImU32 edge=t.positive?IM_COL32(90,210,255,int(170*a)):IM_COL32(255,130,140,int(180*a));d->AddRectFilled(q,ImVec2(q.x+w,q.y+h),IM_COL32(11,16,26,int(238*a)),11);d->AddRect(q,ImVec2(q.x+w,q.y+h),edge,11);d->AddText(ImVec2(q.x+16,q.y+12),IM_COL32(242,247,255,int(255*a)),t.title.c_str());if(!t.detail.empty())d->AddText(ImVec2(q.x+16,q.y+37),IM_COL32(145,157,178,int(255*a)),t.detail.c_str());y+=h+9;++i;}
    }
    void c_overlay::cloud_task(int kind,cloud::json payload){
        if(m_cloud_busy)return;try{auto account=cloud::client::restore();m_cloud_busy=true;m_cloud_job=std::async(std::launch::async,[account,kind,payload]{try{
            if(kind==2){auto b=payload;b["submit"]=false;account.api("beta/configs",&b,true,"stable");}
            if(kind==3){auto b=payload;b["submit"]=true;account.api("beta/configs",&b,true,"stable");}
            if(kind==4)account.api("beta/configs/install",&payload,true,"stable");
            if(kind==5)account.api("beta/configs/uninstall",&payload,true,"stable");
            if(kind==6)account.api("beta/active",&payload,true,"stable");
            auto data=account.api("beta/configs",nullptr,true,"stable");data["active"]=account.api("beta/active",nullptr,true,"stable").value("config",cloud::json());return cloud_result{kind,std::move(data),{}};
        }catch(const std::exception&e){return cloud_result{kind,{},e.what()};}});}catch(const std::exception&e){m_cloud_busy=false;m_config_status=e.what();notify("Cloud",e.what(),false);}
    }
    void c_overlay::refresh_cloud(){cloud_task(1);}
    void c_overlay::cloud_tick(){
        m_avatars.tick(m_device);m_user_avatar=static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(m_avatars.get(m_avatar_url)));m_profile_avatars.clear();for(const auto&p:m_cloud_profiles)m_profile_avatars.push_back(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(m_avatars.get(p.avatar_url))));
        if(m_cloud_job.valid()&&m_cloud_job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto r=m_cloud_job.get();m_cloud_busy=false;m_next_cloud_poll=GetTickCount64()+8000;if(!r.error.empty()){m_config_status=r.error;notify("Cloud",r.error,false);}else try{
            std::string selected=m_config_selected>=0&&m_config_selected<(int)m_cloud_profiles.size()?m_cloud_profiles[m_config_selected].id:"";auto prev=m_known_review_status;m_cloud_profiles.clear();m_config_selected=-1;m_user_id=r.data.value("userId",m_user_id);
            for(const auto&c:r.data.at("configs")){const auto jstr=[&](const char*key,const char*fallback=""){auto it=c.find(key);return it!=c.end()&&it->is_string()?it->get<std::string>():std::string(fallback);};const auto ji64=[&](const char*key,int64_t fallback=0){auto it=c.find(key);return it!=c.end()&&it->is_number_integer()?it->get<int64_t>():fallback;};const auto jint=[&](const char*key,int fallback=0){auto it=c.find(key);return it!=c.end()&&it->is_number_integer()?it->get<int>():fallback;};const auto jbool=[&](const char*key,bool fallback=false){auto it=c.find(key);return it!=c.end()&&it->is_boolean()?it->get<bool>():fallback;};config::profile_meta_t p;p.id=c.at("id");p.owner_id=c.at("owner_id");p.name=c.at("name");p.author=jstr("author");p.avatar_url=jstr("avatar");p.description=jstr("description");p.status=jstr("status","private");p.review_note=jstr("review_note");p.author_role=jstr("author_role","user");p.updated_at=ji64("updated_at");p.reviewed_at=ji64("reviewed_at");p.reviewed_by_name=jstr("reviewed_by_name");p.official=jint("official")!=0;p.installed=jbool("installed");p.revision=jint("revision",1);p.channel=jstr("channel","stable");if(p.channel!="stable")continue;if(p.id==selected)m_config_selected=(int)m_cloud_profiles.size();if(p.owner_id==m_user_id){auto it=prev.find(p.id);if(it!=prev.end()&&it->second!=p.status&&(p.status=="published"||p.status=="rejected")){auto admin=p.reviewed_by_name.empty()?"Administrator":p.reviewed_by_name;if(p.status=="published")notify("Config approved",admin+std::string(" · ")+p.name);else notify("Config rejected",admin+std::string(" · ")+p.name,false);}m_known_review_status[p.id]=p.status;}m_cloud_profiles.push_back(std::move(p));}
            const auto&review=r.data.value("reviewTarget",cloud::json());if(review.is_object()&&review.contains("config_id")){auto stamp=review.at("config_id").get<std::string>()+":"+std::to_string(review.at("revision").get<int>());if(stamp!=m_review_stamp){config::settings_t next;std::istringstream in(review.at("cfg").get<std::string>());if(config::parse_settings(in,next)){config::validate(next);next.lab_enabled=false;next.replay_path_utf8.clear();m_pending_config=next;m_pending_name=review.value("name","Review config");m_review_stamp=stamp;notify("Review config ready",m_pending_name+" · "+review.value("author",""));}}}
            const auto&active=r.data.at("active");if(active.is_object()){auto stamp=active.at("id").get<std::string>()+":"+std::to_string(active.at("updated_at").get<int64_t>());if(stamp!=m_active_stamp){config::settings_t next;std::istringstream in(active.at("cfg").get<std::string>());if(!config::parse_settings(in,next))throw std::runtime_error("Invalid cloud config");config::validate(next);next.lab_enabled=false;next.replay_path_utf8.clear();m_pending_config=next;m_pending_name=active.value("name","Cloud config");m_active_stamp=stamp;notify("Config queued",m_pending_name);}}
            if(r.kind==2)notify("Config saved","Private · Stable");if(r.kind==3)notify("Sent for review","An administrator can publish it for Stable.");if(r.kind==4)notify("Config installed","Cloud only");if(r.kind==5)notify("Config removed","You can install it again later.");
        }catch(const std::exception&e){m_config_status=e.what();notify("Cloud",e.what(),false);}}
        if(m_pending_config&&m_authorized&&m_prev_state!=osu::game_state_t::play){apply_settings(*m_pending_config);m_pending_config.reset();notify("Config applied",m_pending_name);m_config_status="Applied: "+m_pending_name;}
        if(!m_cloud_busy&&GetTickCount64()>=m_next_cloud_poll){m_next_cloud_poll=GetTickCount64()+8000;refresh_cloud();}
    }

    void c_overlay::render_frame( ) {
        osu::full_snapshot_t snap;
        if ( m_snapshot_fn )
            snap = m_snapshot_fn( );

        const HWND osu_hwnd=input::target_window();
        const bool game_window=osu_hwnd&&IsWindow(osu_hwnd)&&!IsIconic(osu_hwnd);
        const bool should_show = m_visible || (game_window && m_hud_enabled) || !m_toasts.empty();
        if(!should_show){::Sleep(8);return;}

        ImGui_ImplDX11_NewFrame( );
        ImGui_ImplWin32_NewFrame( );
        ImGui::NewFrame( );

        const float dt = ImGui::GetIO( ).DeltaTime;
        tick_animations( dt );
        m_anim_time = g_time;

        if ( m_visible && !m_menu_was_open ) {
            m_menu_open_anim = 0.f;
            m_menu_was_open = true;
        }
        else if ( !m_visible && m_menu_was_open ) {
            m_menu_was_open = false;
        }

        if ( m_visible ) {
            m_menu_open_anim += ( 1.0f - m_menu_open_anim ) * std::min( dt * 6.0f, 1.0f );
        }

        // Deliberately do not tint the full game/desktop surface. The menu itself
        // carries its own background; this avoids black/blur remnants after closing.

        if ( m_visible ) {
            draw_menu( snap );
            m_streamproof_hide_cursor = stream_proof && ( ImGui::GetIO( ).WantCaptureMouse );
        }
        else {
            m_streamproof_hide_cursor = false;
        }

        if(capture_settings().hud_enabled){
            SYSTEMTIME st{};GetLocalTime(&st);char clock[16]{};std::snprintf(clock,sizeof(clock),"%02u:%02u:%02u",st.wHour,st.wMinute,st.wSecond);
            auto* hud=ImGui::GetForegroundDrawList();const auto line=std::string("OSU!BAND Stable [osuband.dev] / ")+clock+" / "+m_user;
            const ImVec2 ts=ImGui::CalcTextSize(line.c_str());hud->AddRectFilled(ImVec2(18,18),ImVec2(46+ts.x,55),IM_COL32(12,17,27,235),10);hud->AddRect(ImVec2(18,18),ImVec2(46+ts.x,55),IM_COL32(90,210,255,90),10);hud->AddText(ImVec2(31,29),theme::accent(),line.c_str());
        }
        draw_toasts();
        ImGui::Render( );

        if ( m_context && m_rtv ) {
            const float clear_color[ 4 ]{ 0.0f, 0.0f, 0.0f, 0.0f };
            m_context->OMSetRenderTargets( 1, &m_rtv, nullptr );
            m_context->ClearRenderTargetView( m_rtv, clear_color );
        }

        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
        if ( m_swap_chain ) m_swap_chain->Present( 1, 0 );
    }

    void c_overlay::apply_custom_keys( osu::game_snapshot_t& game ) const {
        game.left_key = m_custom_left_key;
        game.right_key = m_custom_right_key;
    }

    config::settings_t c_overlay::capture_settings( ) const {
        config::settings_t s{};
        s.aim_enabled = m_aim.enabled;
        s.aim_ignore_sliders = m_aim.ignore_sliders;
        s.aim_tablet_mode = m_aim.tablet_mode;

        s.aim_strength_x = m_aim.strength_x;
        s.aim_strength_y = m_aim.strength_y;
        s.aim_decay_far = m_aim.decay_far;
        s.aim_lerp = m_aim.aim_lerp;
        s.aim_freeze_lerp = m_aim.freeze_lerp;
        s.aim_window = m_aim.aim_window;
        s.aim_legit_mode = m_aim.legit_mode;
        s.aim_legit_clamp = m_aim.legit_clamp;

        s.relax_enabled = m_relax.enabled;
        s.relax_ur = m_relax.ur;
        s.relax_tap_style = m_relax.tap_style;
        s.relax_singletap_bpm_cap = m_relax.singletap_bpm_cap;
        s.relax_k1_hold_center = m_relax.k1_hold_center;
        s.relax_k1_hold_spread = m_relax.k1_hold_spread;
        s.relax_k2_hold_center = m_relax.k2_hold_center;
        s.relax_k2_hold_spread = m_relax.k2_hold_spread;
        s.relax_hold_floor = m_relax.hold_floor;
        s.relax_hold_ceiling = m_relax.hold_ceiling;
        s.relax_manual_offset_ms = m_relax.manual_offset_ms;

        s.replay_enabled = m_replay.enabled;
        s.replay_path_utf8 = m_replay_path_utf8;
        s.replay_parse_buttons = m_replay.parse_buttons;

        s.custom_left_key = m_custom_left_key;
        s.custom_right_key = m_custom_right_key;
        s.menu_keybind = m_menu_keybind;
        s.stream_proof = stream_proof;
        s.hud_enabled=m_hud_enabled;s.lab_enabled=false;

        return s;
    }

    void c_overlay::apply_settings( const config::settings_t& s ) {
        m_aim.enabled = s.aim_enabled;
        m_aim.ignore_sliders = s.aim_ignore_sliders;
        m_aim.tablet_mode = s.aim_tablet_mode;

        m_aim.strength_x = s.aim_strength_x;
        m_aim.strength_y = s.aim_strength_y;
        m_aim.decay_far = s.aim_decay_far;
        m_aim.aim_lerp = s.aim_lerp;
        m_aim.freeze_lerp = s.aim_freeze_lerp;
        m_aim.aim_window = s.aim_window;
        m_aim.legit_mode = s.aim_legit_mode;
        m_aim.legit_clamp = s.aim_legit_clamp;

        m_relax.enabled = s.relax_enabled;
        m_relax.ur = s.relax_ur;
        m_relax.tap_style = s.relax_tap_style;
        m_relax.singletap_bpm_cap = s.relax_singletap_bpm_cap;
        m_relax.k1_hold_center = s.relax_k1_hold_center;
        m_relax.k1_hold_spread = s.relax_k1_hold_spread;
        m_relax.k2_hold_center = s.relax_k2_hold_center;
        m_relax.k2_hold_spread = s.relax_k2_hold_spread;
        m_relax.hold_floor = s.relax_hold_floor;
        m_relax.hold_ceiling = s.relax_hold_ceiling;
        m_relax.manual_offset_ms = s.relax_manual_offset_ms;

        m_replay.enabled = s.replay_enabled;
        if ( !s.replay_path_utf8.empty( ) ) {
            strncpy_s( m_replay_path_utf8, s.replay_path_utf8.c_str( ), _TRUNCATE );
            int wlen = MultiByteToWideChar( CP_UTF8, 0, m_replay_path_utf8, -1, nullptr, 0 );
            if ( wlen > 1 ) {
                std::wstring wide( static_cast<size_t>( wlen - 1 ), L'\0' );
                MultiByteToWideChar( CP_UTF8, 0, m_replay_path_utf8, -1, wide.data( ), wlen );
                m_replay.replay_path = wide;
                m_replay.load_replay( );
                m_replay.reset_sync( );
            }
        }

        m_replay.parse_buttons = s.replay_parse_buttons;

        m_custom_left_key = s.custom_left_key;
        m_custom_right_key = s.custom_right_key;
        m_menu_keybind = s.menu_keybind;
        stream_proof = s.stream_proof;
        m_hud_enabled=s.hud_enabled;

    }

    void c_overlay::reset_modules( const osu::game_snapshot_t& game ) {
        m_game_time_stall_start_ms = 0;
        m_aim.set_user_input_blocked( false );
        m_aim.on_leave_play( );
        osu::game_snapshot_t mod_game = game;
        apply_custom_keys( mod_game );
        m_relax.on_leave_play( mod_game );
        m_replay.on_leave_play( mod_game );
    }

    void c_overlay::tick_modules( const osu::game_snapshot_t& game, const osu::beatmap_data_t& beatmap ) {
        if(GetTickCount64()>=m_auth_deadline.load())m_authorized=false;
        if(!m_authorized){reset_modules(game);return;}
        const bool in_play = game.cur_state == osu::game_state_t::play;
        DWORD fgpid=0;GetWindowThreadProcessId(GetForegroundWindow(),&fgpid);if(in_play&&fgpid!=static_cast<DWORD>(game.pid)){reset_modules(game);m_prev_game_time=-1;return;}
        const bool replay_active = game.is_replay;
        const bool was_play = m_prev_state == osu::game_state_t::play;
        const std::string map_sig =
            std::to_string( game.map_id ) + "|" + std::to_string( game.set_id ) + "|" +
            game.map_folder + "|" + game.map_file + "|" + game.beatmap_hash + "|" +
            game.beatmap_version;

        if ( replay_active ) {
            m_prev_game_time = -1;
            m_prev_map_id = -1;
            m_prev_map_sig.clear( );
            m_game_time_stall_start_ms = 0;
            m_aim.set_user_input_blocked( true );
            return;
        }

        if ( was_play && !in_play ) {
            reset_modules( game );
            m_prev_game_time = -1;
            m_prev_map_id = -1;
            m_prev_map_sig.clear( );
            m_game_time_stall_start_ms = 0;
        }

        if ( in_play && !map_sig.empty( ) && !m_prev_map_sig.empty( ) && map_sig != m_prev_map_sig )
            reset_modules( game );

        if ( in_play && m_prev_map_id > 0 && game.map_id != 0 && game.map_id != m_prev_map_id )
            reset_modules( game );

        if ( was_play && in_play && m_prev_game_time >= 0 && game.cur_time < m_prev_game_time - 200 )
            reset_modules( game );

        m_prev_state = game.cur_state;

        if ( in_play && beatmap.loaded && !beatmap.objects.empty( ) ) {
            osu::game_snapshot_t mod_game = game;
            apply_custom_keys( mod_game );

            constexpr uint64_t k_pause_stall_ms = 120;
            const uint64_t     now_ms = get_time_ms( );
            const int32_t      cur_time = mod_game.cur_time;
            const bool         time_stalled = m_prev_game_time >= 0 && cur_time == m_prev_game_time;

            if ( time_stalled ) {
                if ( m_game_time_stall_start_ms == 0 )
                    m_game_time_stall_start_ms = now_ms;
            }
            else {
                m_game_time_stall_start_ms = 0;
            }

            const bool map_paused = m_game_time_stall_start_ms != 0
                                    && ( now_ms - m_game_time_stall_start_ms ) >= k_pause_stall_ms;

            m_aim.set_user_input_blocked( map_paused );

            if ( !map_paused )
                m_aim.update( mod_game, beatmap );

            if(map_paused)m_relax.on_leave_play(mod_game);else m_relax.update(mod_game,beatmap);
            m_replay.update(mod_game,beatmap,map_paused);
        }
        else {
            m_game_time_stall_start_ms = 0;
            m_aim.set_user_input_blocked( false );
        }

        if ( in_play ) {
            m_prev_map_id = game.map_id;
            m_prev_game_time = game.cur_time;
            if ( !map_sig.empty( ) )
                m_prev_map_sig = map_sig;
        }
    }

    void c_overlay::draw_menu( const osu::full_snapshot_t& snap ) {
        auto settings=capture_settings();
        m_studio.user=m_user;m_studio.plan=m_plan;m_studio.user_id=m_user_id;m_studio.authorized=m_authorized.load();
        m_studio.avatar=m_user_avatar;m_studio.profiles=m_cloud_profiles;m_studio.profile_avatars=m_profile_avatars;m_studio.cloud_busy=m_cloud_busy;
        m_studio.selected=m_config_selected;m_studio.waiting_menu=m_waiting_menu;
        { m_studio.replay_frames=(int)m_replay.frame_count();m_studio.replay_player=m_replay.player_name(); }
        if(m_studio.replay_path[0]=='\0'&&m_replay_path_utf8[0]!='\0')strncpy_s(m_studio.replay_path,m_replay_path_utf8,_TRUNCATE);
        if(!m_authorized)m_studio.message="Session unavailable. Reconnect your loader to continue.";
        const auto action=stable_ui::draw(m_studio,settings);
        m_config_selected=m_studio.selected;
        if(action.close)m_visible=false;
        if(action.pause){/* Stable has no global auto-toggle module controller; keep UI feedback only. */m_studio.paused=!m_studio.paused;}
        if(action.changed){apply_settings(settings);m_studio.message=stable_ui::tr("Settings applied.");}
        if(action.bind_menu){m_waiting_menu=true;m_studio.waiting_menu=true;m_studio.message="Press a key for the menu. Esc cancels.";}
        if(m_waiting_menu&&!action.bind_menu){
            for(int vk=8;vk<=254;++vk){if((GetAsyncKeyState(vk)&1)==0)continue;if(vk==VK_ESCAPE){m_waiting_menu=false;m_studio.waiting_menu=false;m_studio.message="Menu key unchanged.";break;}settings.menu_keybind=vk;apply_settings(settings);m_waiting_menu=false;m_studio.waiting_menu=false;m_studio.message=std::string("Menu key: ")+stable_ui::key_name(vk);break;}
        }
        if(action.refresh)refresh_cloud();
        auto send=[&](bool submit){std::string name=m_studio.profile_name,desc=m_studio.description;if(name.size()<2){m_studio.message="Enter a config name (2+ characters).";return;}auto portable=settings;portable.replay_path_utf8.clear();std::ostringstream out;config::serialize_settings(out,name,portable);cloud_task(submit?3:2,{{"name",name},{"description",desc},{"cfg",out.str()}});};
        if(action.save_private&&!m_cloud_busy)send(false);if(action.submit_review&&!m_cloud_busy)send(true);
        if(m_config_selected>=0&&m_config_selected<(int)m_cloud_profiles.size()){const auto&p=m_cloud_profiles[(size_t)m_config_selected];if(action.install&&!m_cloud_busy)cloud_task(4,{{"id",p.id},{"revision",p.revision}});if(action.uninstall&&!m_cloud_busy)cloud_task(5,{{"id",p.id}});if(action.load&&!m_cloud_busy)cloud_task(6,{{"id",p.id},{"revision",p.revision}});}
        if(action.uninject){notify("OSU!BAND","Closing cleanly…");PostMessageW(m_hwnd,WM_CLOSE,0,0);return;}
        if(action.browse_replay){wchar_t file[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=m_hwnd;ofn.lpstrFilter=L"osu! replay (*.osr)\0*.osr\0\0";ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&ofn))WideCharToMultiByte(CP_UTF8,0,file,-1,m_studio.replay_path,sizeof(m_studio.replay_path),nullptr,nullptr);}
        if(action.load_replay){settings.replay_path_utf8=m_studio.replay_path;if(settings.replay_path_utf8==m_replay_path_utf8){m_replay.load_replay();}else apply_settings(settings);m_studio.message=m_replay.replay_valid()?"Replay loaded.":m_replay.last_load_error();}
    }
}
