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

        SetLayeredWindowAttributes( m_hwnd, 0, 255, LWA_ALPHA );

        ShowWindow( m_hwnd, SW_HIDE );
        UpdateWindow( m_hwnd );

        if ( !init_d3d( ) ) return false;

        IMGUI_CHECKVERSION( );
        ImGui::CreateContext( );
        ImGuiIO& io = ImGui::GetIO( );
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImFontConfig font_cfg{};
        font_cfg.OversampleH = 3;
        font_cfg.OversampleV = 3;
        font_cfg.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &font_cfg );

        ImGuiStyle& style = ImGui::GetStyle( );
        style.WindowRounding = 12.f;
        style.WindowBorderSize = 0.f;

        ImGui_ImplWin32_Init( m_hwnd );
        ImGui_ImplDX11_Init( m_device, m_context );

        m_mouse_hook.set_filter_injected_only( true );
        m_mouse_hook.set_transform( overlay_aim_hook_transform, this );
        m_mouse_hook.install( );

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
        if(!osu_foreground()){m_visible=false;m_f4_was_down=false;return;}
        if(m_waiting_menu){m_f4_was_down=false;return;}
        const bool menu_down=(GetAsyncKeyState(m_menu_keybind)&0x8000)!=0;
        if(menu_down&&!m_f4_was_down)m_visible=!m_visible;
        m_f4_was_down=menu_down;
    }

    void c_overlay::update_overlay_position( ) {
        if(!m_hwnd)return;static uint64_t next=0;const uint64_t now=GetTickCount64();if(now<next)return;next=now+100;
        const HWND osu=input::target_window();if(!osu||!IsWindow(osu)||IsIconic(osu))return;RECT client{};if(!playfield::get_playfield_rect(osu,client))return;
        static RECT prev{};RECT cur{client.left,client.top,client.right,client.bottom};if(!EqualRect(&prev,&cur)){SetWindowPos(m_hwnd,HWND_TOPMOST,client.left,client.top,client.right-client.left,client.bottom-client.top,SWP_NOACTIVATE);prev=cur;}
    }

    void c_overlay::apply_visibility( ) {
        if(!m_hwnd)return;
        const bool in_osu=osu_foreground();
        const bool show=in_osu&&(m_visible||m_hud_enabled||!m_toasts.empty());
        if(!show){ShowWindow(m_hwnd,SW_HIDE);return;}
        LONG ex=GetWindowLongW(m_hwnd,GWL_EXSTYLE);
        ex|=WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_NOACTIVATE;ex&=~WS_EX_APPWINDOW;
        if(m_visible)ex&=~WS_EX_TRANSPARENT;else ex|=WS_EX_TRANSPARENT;
        SetWindowLongW(m_hwnd,GWL_EXSTYLE,ex);ShowWindow(m_hwnd,SW_SHOWNOACTIVATE);
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
            for(const auto&c:r.data.at("configs")){config::profile_meta_t p;p.id=c.at("id");p.owner_id=c.at("owner_id");p.name=c.at("name");p.author=c.value("author","");p.avatar_url=c.value("avatar","");p.description=c.value("description","");p.status=c.value("status","private");p.review_note=c.value("review_note","");p.author_role=c.value("author_role","user");p.updated_at=c.value("updated_at",int64_t(0));p.reviewed_at=c.value("reviewed_at",int64_t(0));p.reviewed_by_name=c.value("reviewed_by_name","");p.official=c.value("official",0)!=0;p.installed=c.value("installed",false);p.revision=c.at("revision");p.channel=c.value("channel","stable");if(p.channel!="stable")continue;if(p.id==selected)m_config_selected=(int)m_cloud_profiles.size();if(p.owner_id==m_user_id){auto it=prev.find(p.id);if(it!=prev.end()&&it->second!=p.status&&(p.status=="published"||p.status=="rejected")){auto admin=p.reviewed_by_name.empty()?"Administrator":p.reviewed_by_name;if(p.status=="published")notify("Config approved",admin+std::string(" · ")+p.name);else notify("Config rejected",admin+std::string(" · ")+p.name,false);}m_known_review_status[p.id]=p.status;}m_cloud_profiles.push_back(std::move(p));}
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

        const bool should_show = osu_foreground() && (m_visible || m_hud_enabled || !m_toasts.empty());
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

        if ( m_visible ) {
            ImDrawList* bg_dl = ImGui::GetBackgroundDrawList( );
            const ImVec2 disp = ImGui::GetIO( ).DisplaySize;
            const float bg_a = m_menu_open_anim * 0.2f;
            if ( bg_a > 0.01f ) {
                const int a = static_cast<int>( bg_a * 255 );
                bg_dl->AddRectFilledMultiColor(
                    ImVec2( 0, 0 ), disp,
                    IM_COL32( 2, 2, 12, a ), IM_COL32( 2, 2, 12, a ),
                    IM_COL32( 4, 2, 16, a ), IM_COL32( 4, 2, 16, a ) );
            }
        }

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
        update_bind_capture( );

        ImGuiIO& io = ImGui::GetIO( );

        static const float MENU_W = 980.f;
        static const float MENU_H = 650.f;
        static const float SIDEBAR_W = 190.f;
        static const float TITLE_H = 58.f;
        static const float PADDING = 16.f;
        static const float L_X = SIDEBAR_W + PADDING;
        static const float L_W = 360.f;
        static const float GAP = 14.f;
        static const float R_X = L_X + L_W + GAP;
        static const float R_W = 360.f;

        const float scale = theme::ease_out_back( std::min( m_menu_open_anim * 1.2f, 1.0f ) );
        const float alpha = std::min( m_menu_open_anim * 1.5f, 1.0f );

        float menu_x = ( io.DisplaySize.x - MENU_W * scale ) * 0.5f + m_menu_offset_x;
        float menu_y = ( io.DisplaySize.y - MENU_H * scale ) * 0.5f + m_menu_offset_y +
                       ( 1.0f - scale ) * 28.0f;

        ImGui::SetNextWindowPos( ImVec2( menu_x, menu_y ), ImGuiCond_Always );
        ImGui::SetNextWindowSize( ImVec2( MENU_W * scale, MENU_H * scale ), ImGuiCond_Always );

        ImGuiWindowFlags wf =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0, 0 ) );
        ImGui::Begin( "##OSUBAND_MAIN", nullptr, wf );
        ImGui::PopStyleVar( 2 );

        ImDrawList* dl = ImGui::GetWindowDrawList( );
        const ImVec2 wpos = ImGui::GetWindowPos( );
        const ImVec2 wsize = ImGui::GetWindowSize( );
        auto S = [&]( float x, float y ) { return ImVec2( wpos.x + x, wpos.y + y ); };

        // Window shadow + midnight body.
        if ( alpha > 0.01f ) {
            const ImVec2 br = ImVec2( wpos.x + wsize.x, wpos.y + wsize.y );
            dl->AddRectFilled( ImVec2( wpos.x + 8, wpos.y + 10 ), ImVec2( br.x + 8, br.y + 10 ),
                IM_COL32( 0, 0, 0, static_cast<int>( 95 * alpha ) ), 15.0f );
            dl->AddRectFilled( wpos, br, theme::bg0(), 14.0f );
            dl->AddRectFilledMultiColor(
                wpos, br,
                IM_COL32( 14, 20, 36, static_cast<int>( 245 * alpha ) ),
                IM_COL32( 7, 10, 20, static_cast<int>( 245 * alpha ) ),
                IM_COL32( 9, 8, 22, static_cast<int>( 245 * alpha ) ),
                IM_COL32( 6, 9, 17, static_cast<int>( 245 * alpha ) ) );
            dl->AddRect( wpos, br, theme::border(), 14.0f, 0, 1.0f );

            // Animated spectral accent rail.
            const float wave = std::sin( g_time * 1.25f ) * 0.5f + 0.5f;
            const float rail_x = wpos.x + SIDEBAR_W + 20.0f + wave * ( wsize.x - SIDEBAR_W - 160.0f );
            dl->AddRectFilledMultiColor(
                ImVec2( rail_x - 65.f, wpos.y + 1.f ), ImVec2( rail_x + 65.f, wpos.y + 3.f ),
                theme::secondary_alpha( 0.0f ), theme::accent_alpha( 0.75f ),
                theme::accent_alpha( 0.75f ), theme::secondary_alpha( 0.0f ) );
        }

        // Decorative low-opacity waveform behind the content. Purely visual.
        {
            const float base_y = wpos.y + wsize.y - 34.0f;
            for ( int i = 0; i < 26; ++i ) {
                const float x = wpos.x + SIDEBAR_W + 25.0f + i * 28.0f;
                const float amp = 4.0f + 7.0f * ( std::sin( g_time * 1.6f + i * 0.62f ) * 0.5f + 0.5f );
                dl->AddLine( ImVec2( x, base_y - amp ), ImVec2( x, base_y + amp ),
                    IM_COL32( 90, 210, 255, static_cast<int>( 16 * alpha ) ), 2.0f );
            }
        }

        // Sidebar.
        {
            const ImVec2 sb_br = ImVec2( wpos.x + SIDEBAR_W, wpos.y + wsize.y );
            dl->AddRectFilled( wpos, sb_br, theme::sidebar(), 14.0f, ImDrawFlags_RoundCornersLeft );
            dl->AddLine( ImVec2( wpos.x + SIDEBAR_W, wpos.y + 12.f ),
                ImVec2( wpos.x + SIDEBAR_W, wpos.y + wsize.y - 12.f ),
                IM_COL32( 255, 255, 255, static_cast<int>( 12 * alpha ) ), 1.0f );
        }

        // OSU!BAND vector logo: pulse disc + wordmark. No external image dependency.
        {
            const float logo_anim = std::min( m_menu_open_anim * 3.2f, 1.0f );
            const ImVec2 c = S( 29.f, 31.f + ( 1.0f - logo_anim ) * 8.0f );
            const float pulse = 1.0f + 0.05f * std::sin( g_time * 2.3f );
            dl->AddCircleFilled( c, 13.f * pulse, IM_COL32( 13, 29, 46, static_cast<int>( 255 * logo_anim ) ), 28 );
            dl->AddCircle( c, 13.f * pulse, theme::accent_alpha( 0.80f * logo_anim ), 28, 1.4f );
            dl->AddCircle( c, 7.5f * pulse, theme::secondary_alpha( 0.55f * logo_anim ), 22, 1.2f );
            const float bars[5] = { 4.f, 8.f, 12.f, 7.f, 4.f };
            for ( int i = 0; i < 5; ++i ) {
                const float bx = c.x - 6.f + i * 3.f;
                dl->AddLine( ImVec2( bx, c.y - bars[i] * 0.5f ), ImVec2( bx, c.y + bars[i] * 0.5f ),
                    i == 2 ? theme::secondary() : theme::accent(), 1.5f );
            }

            dl->AddText( S( 50.f, 18.f ), IM_COL32( 244, 249, 255, static_cast<int>( 255 * logo_anim ) ), "OSU!" );
            dl->AddText( S( 86.f, 18.f ), theme::text_accent(), "BAND" );
            dl->AddText( S( 50.f, 38.f ), IM_COL32( 104, 118, 145, static_cast<int>( 220 * logo_anim ) ), "lazer runtime" );
        }

        update_tab_transition( m_tab, io.DeltaTime );

        static const char* tab_names[] = { "Aim Assist", "Relax", "Replay", "Status", "Cloud Configs" };
        static const char* tab_subtitles[] = {
            "Cursor correction", "Timing control", "Replay playback",
            "Runtime and keybinds", "Published Stable configs"
        };
        static const int tab_ids[] = { 0, 1, 3, 5, 6 };
        static const float tab_y[] = { 112.f, 153.f, 194.f, 288.f, 329.f };
        const float tab_h=36.f;
        dl->AddText(S(17.f,86.f),theme::text_dim(),"GAMEPLAY");
        dl->AddText(S(17.f,262.f),theme::text_dim(),"UTILITY");
        const ImVec2 mouse=io.MousePos;const bool mouse_clicked=ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        int visible_active=0;for(int i=0;i<5;++i)if(tab_ids[i]==m_tab)visible_active=i;
        const float target_indicator_y=wpos.y+tab_y[visible_active];if(g_tab_indicator_y<=.01f)g_tab_indicator_y=target_indicator_y;
        g_tab_indicator_y+=(target_indicator_y-g_tab_indicator_y)*std::min(io.DeltaTime*15.f,1.f);
        for(int i=0;i<5;++i){const float tx=wpos.x+10.f,ty=wpos.y+tab_y[i],tw=SIDEBAR_W-20.f;ImVec2 mn(tx,ty),mx(tx+tw,ty+tab_h);bool hov=mouse.x>=mn.x&&mouse.x<=mx.x&&mouse.y>=mn.y&&mouse.y<=mx.y;bool active=tab_ids[i]==m_tab;if(hov&&mouse_clicked)m_tab=tab_ids[i];float&anim=tab_hover_anim(i);float target=(hov||active)?1.f:0.f;anim+=(target-anim)*std::min(io.DeltaTime*12.f,1.f);if(anim>.01f){ImU32 bg=active?IM_COL32(90,210,255,int(25*alpha)):IM_COL32(255,255,255,int(7*anim*alpha));dl->AddRectFilled(mn,mx,bg,8);if(active)dl->AddRect(mn,mx,IM_COL32(90,210,255,int(32*alpha)),8,0,1);}if(active)dl->AddRectFilled(ImVec2(mn.x,g_tab_indicator_y+7),ImVec2(mn.x+3,g_tab_indicator_y+tab_h-7),theme::accent(),2);
            const float icx=mn.x+18,icy=mn.y+tab_h*.5f;const ImU32 ic=active?theme::accent():(hov?theme::text_bright():theme::text_dim());
            if(i==0){dl->AddCircle(ImVec2(icx,icy),5,ic,18,1.4f);dl->AddCircleFilled(ImVec2(icx,icy),1.5f,ic);}
            else if(i==1)dl->AddBezierCubic(ImVec2(icx-7,icy+2),ImVec2(icx-3,icy-7),ImVec2(icx+3,icy+7),ImVec2(icx+7,icy-2),ic,1.6f);
            else if(i==2){dl->PathArcTo(ImVec2(icx,icy),6,-2.6f,2.4f,16);dl->PathStroke(ic,0,1.5f);}
            else if(i==3)dl->AddCircleFilled(ImVec2(icx,icy),5,snap.game.attached?theme::success():theme::text_dim(),16);
            else{dl->AddRect(ImVec2(icx-6,icy-5),ImVec2(icx+6,icy+5),ic,3,0,1.3f);dl->AddLine(ImVec2(icx-3,icy-1),ImVec2(icx+3,icy-1),ic,1);}
            dl->AddText(ImVec2(mn.x+35,mn.y+10),active?theme::text_bright():(hov?theme::text_bright():theme::text()),tab_names[i]);
        }

        // User card. Username/avatar extraction is intentionally not guessed from unverified memory.
        // This card is already shaped for the future verified game-profile bridge / website account.
        {
            const ImVec2 p0 = S( 10.f, MENU_H - 84.f );
            const ImVec2 p1 = S( SIDEBAR_W - 10.f, MENU_H - 12.f );
            dl->AddRectFilled( p0, p1, IM_COL32( 13, 17, 29, 225 ), 10.f );
            dl->AddRect( p0, p1, IM_COL32( 255, 255, 255, 15 ), 10.f, 0, 1.f );
            const ImVec2 av( p0.x + 25.f, p0.y + 29.f );
            dl->AddCircleFilled( av, 17.f, IM_COL32( 19, 35, 51, 255 ), 26 );
            dl->AddCircle( av, 17.f, snap.game.attached ? theme::accent() : theme::text_dim(), 26, 1.2f );
            for ( int i = 0; i < 4; ++i ) {
                const float h = 4.f + 6.f * ( std::sin( g_time * 2.f + i ) * 0.5f + 0.5f );
                dl->AddLine( ImVec2( av.x - 5.f + i * 3.2f, av.y - h * 0.5f ),
                    ImVec2( av.x - 5.f + i * 3.2f, av.y + h * 0.5f ), theme::accent(), 1.3f );
            }
            if(m_user_avatar)dl->AddImageRounded(m_user_avatar,ImVec2(av.x-17,av.y-17),ImVec2(av.x+17,av.y+17),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,17.f);
            dl->AddText(ImVec2(p0.x+50.f,p0.y+14.f),theme::text_bright(),m_user.substr(0,20).c_str());
            dl->AddText(ImVec2(p0.x+50.f,p0.y+34.f),snap.game.attached?theme::success():theme::text_dim(),snap.game.attached?"lazer connected":"waiting for lazer");
            const std::string sub="Stable · "+m_plan;dl->AddText(ImVec2(p0.x+50.f,p0.y+51.f),theme::text_dim(),sub.substr(0,25).c_str());
        }

        // Top bar / draggable area.
        {
            const ImVec2 tp0 = S( SIDEBAR_W + 1.f, 0.f );
            const ImVec2 tp1 = S( MENU_W, TITLE_H );
            dl->AddRectFilled( tp0, tp1, IM_COL32( 8, 11, 20, 190 ), 14.f, ImDrawFlags_RoundCornersTopRight );
            dl->AddLine( ImVec2( tp0.x + 16.f, tp1.y ), ImVec2( tp1.x - 16.f, tp1.y ), IM_COL32(255,255,255,10), 1.f );

            ImGui::SetCursorPos( ImVec2( SIDEBAR_W + 1, 0 ) );
            ImGui::InvisibleButton( "##titlebar", ImVec2( wsize.x - SIDEBAR_W - 42.0f, TITLE_H ) );
            if ( ImGui::IsItemActive( ) ) {
                m_menu_offset_x += static_cast<int>( io.MouseDelta.x );
                m_menu_offset_y += static_cast<int>( io.MouseDelta.y );
            }

            dl->AddText( S( SIDEBAR_W + 18.f, 12.f ), theme::text_bright(), tab_names[visible_active] );
            dl->AddText( S( SIDEBAR_W + 18.f, 32.f ), theme::text_dim(), tab_subtitles[visible_active] );

            const bool connected = snap.game.attached && snap.game.client == osu::client_kind_t::lazer;
            const float pill_w = 116.f;
            const ImVec2 pp0 = S( MENU_W - pill_w - 48.f, 15.f );
            const ImVec2 pp1 = ImVec2( pp0.x + pill_w, pp0.y + 28.f );
            dl->AddRectFilled( pp0, pp1, connected ? IM_COL32( 92, 232, 166, 15 ) : IM_COL32( 104, 117, 144, 13 ), 14.f );
            dl->AddRect( pp0, pp1, connected ? IM_COL32( 92, 232, 166, 55 ) : IM_COL32( 104, 117, 144, 32 ), 14.f, 0, 1.f );
            dl->AddCircleFilled( ImVec2( pp0.x + 13.f, pp0.y + 14.f ), 3.5f, connected ? theme::success() : theme::text_dim(), 12 );
            dl->AddText( ImVec2( pp0.x + 23.f, pp0.y + 6.f ), connected ? theme::success() : theme::text_dim(), connected ? "LAZER READY" : "NO CLIENT" );
        }

        // Close button.
        {
            ImGui::SetCursorPos( ImVec2( MENU_W - 38.0f, 8.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0, 0, 0, 0 ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.12f, 0.18f, 0.25f, 0.55f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.55f, 0.61f, 0.72f, 1.f ) );
            if ( ImGui::Button( "x##close", ImVec2( 28.f, 28.f ) ) )
                PostMessageW( m_hwnd, WM_CLOSE, 0, 0 );
            ImGui::PopStyleColor( 4 );
        }

        const float entrance = card_entrance( 0, 0.0f );
        const float content_alpha = entrance;
        const float content_slide = ( 1.0f - entrance ) * 12.0f;

        if ( snap.game.is_replay ) {
            const float warn_y = wpos.y + TITLE_H + 10.0f;
            dl->AddRectFilled(
                ImVec2( wpos.x + SIDEBAR_W + 12.0f, warn_y ),
                ImVec2( wpos.x + wsize.x - 12.0f, warn_y + 30.0f ),
                IM_COL32( 200, 50, 50, static_cast<int>( 60 * alpha ) ), 6.0f );
            dl->AddText(
                ImVec2( wpos.x + SIDEBAR_W + 22.0f, warn_y + 7.0f ),
                IM_COL32( 255, 128, 148, static_cast<int>( 255 * alpha ) ), "REPLAY MODE - gameplay modules are suspended" );
        }

        const float replay_banner_h = snap.game.is_replay ? 36.0f : 0.0f;

        if ( m_tab == 0 ) {
            float lbox_top = TITLE_H + 14.0f + content_slide + replay_banner_h;
            float ly = lbox_top + 30.0f;
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorPos(ImVec2(L_X + 12.0f, ly));
            checkbox("Enable aim assist", &m_aim.enabled);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 12.0f, ly));
            checkbox("Ignore sliders", &m_aim.ignore_sliders);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 12.0f, ly));
            checkbox("Tablet mode", &m_aim.tablet_mode);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 12.0f, ly));
            checkbox("Legit mode", &m_aim.legit_mode);
            ly = ImGui::GetCursorPos().y + 4.0f;
            const float lbox_bottom = ly + 12.0f;
            dl->ChannelsSetCurrent(0);
            draw_glass_card( dl, S(L_X, lbox_top), S(L_X + L_W, lbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( L_X + 14.0f, lbox_top + 8.0f ), theme::text_accent(), "aim assist" );
            dl->ChannelsMerge();

            const float rbox_top = TITLE_H + 14.0f;
            float ry = rbox_top + 30.0f;
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Strength X", &m_aim.strength_x, 1.0f, 15.0f, "", "%.1f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Strength Y", &m_aim.strength_y, 1.0f, 15.0f, "", "%.1f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Aim Lerp", &m_aim.aim_lerp, 0.15f, 0.40f, "", "%.2f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Aim Window (ms)", &m_aim.aim_window, 75.f, 110.f, "", "%.0f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Decay Far", &m_aim.decay_far, 0.01f, 0.50f, "", "%.2f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            slider_float("Freeze Lerp", &m_aim.freeze_lerp, 0.10f, 0.30f, "", "%.2f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));

            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
            if (m_aim.legit_mode) {
                ImGui::SetCursorPos(ImVec2(R_X + 12.0f, ry));
                slider_float("Legit Clamp", &m_aim.legit_clamp, 1.05f, 1.80f, "", "%.2f");
                ry = ImGui::GetCursorPos().y + 3.0f;
            }
            const float rbox_bottom = ry + 12.0f;
            dl->ChannelsSetCurrent(0);
            draw_glass_card( dl, S(R_X, rbox_top), S(R_X + R_W, rbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( R_X + 14.0f, rbox_top + 8.0f ), theme::text_accent(), "aim assist tune" );
            dl->ChannelsMerge();
        }
        else         if ( m_tab == 1 ) {
            const float lbox_top = TITLE_H + 14.0f + content_slide + replay_banner_h;
            float ly = lbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            checkbox( "Enable relax", &m_relax.enabled );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            slider_int( "Manual offset", &m_relax.manual_offset_ms, -100, 100, " ms" );
            ly = ImGui::GetCursorPos( ).y + 8.0f;

            { bool is_singletap = ( m_relax.tap_style == 1 );
              ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
              checkbox( "Singletap mode", &is_singletap );
              m_relax.tap_style = is_singletap ? 1 : 0; }
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            slider_int( "Max ST BPM", &m_relax.singletap_bpm_cap, 100, 300, " bpm" );
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(L_X, lbox_top), S(L_X + L_W, lbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( L_X + 14.0f, lbox_top + 8.0f ), theme::text_accent(), "relax options" );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 14.0f;
            float ry = rbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            dl->AddText( S( R_X + 14.0f, ry ), theme::text_bright(), "K1 Hold Shape" );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "K1 Center", &m_relax.k1_hold_center, 30.f, 120.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "K1 Spread", &m_relax.k1_hold_spread, 2.f, 30.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            dl->AddText( S( R_X + 14.0f, ry ), theme::text_bright(), "K2 Hold Shape" );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "K2 Center", &m_relax.k2_hold_center, 30.f, 120.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "K2 Spread", &m_relax.k2_hold_spread, 2.f, 30.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "Hold floor", &m_relax.hold_floor, 10.f, 60.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            slider_float( "Hold ceiling", &m_relax.hold_ceiling, 60.f, 150.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            if ( m_relax.is_active( ) )
                dl->AddText( S( R_X + 14.0f, ry ), IM_COL32( 100, 230, 160, 255 ), "Status: Running" );
            else if ( m_relax.is_synced( ) && m_relax.enabled )
                dl->AddText( S( R_X + 14.0f, ry ), IM_COL32( 230, 200, 100, 255 ), "Status: Synced" );
            else
                dl->AddText( S( R_X + 14.0f, ry ), theme::text_dim(), "Status: Idle" );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            if ( m_relax.enabled && snap.beatmap.loaded ) {
                char buf[ 64 ];
                sprintf_s( buf, "Hit object: %d / %zu", m_relax.last_hit_obj_idx( ), snap.beatmap.objects.size( ) );
                dl->AddText( S( R_X + 14.0f, ry ), theme::text(), buf );
                ry += ImGui::GetTextLineHeight( );
            }

            const float rbox_bottom = ry + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(R_X, rbox_top), S(R_X + R_W, rbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( R_X + 14.0f, rbox_top + 8.0f ), theme::text_accent(), "hold times & status" );
            dl->ChannelsMerge( );
        }
        else if ( m_tab == 3 ) {
            const float lbox_top = TITLE_H + 14.0f + content_slide + replay_banner_h;
            float ly = lbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            checkbox( "Enable replay bot", &m_replay.enabled );
            if ( ImGui::IsItemClicked( ) && m_replay.enabled ) m_replay.reset_sync( );
            ly = ImGui::GetCursorPos( ).y + 6.0f;

            dl->AddText( S( L_X + 14.0f, ly ), theme::text_dim(), "Replay path:" );
            ly += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            text_input( "##replay_path", m_replay_path_utf8, IM_ARRAYSIZE( m_replay_path_utf8 ), L_W - 24.0f );
            ly = ImGui::GetCursorPos( ).y + 6.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            if ( button( "Browse", L_W - 24.0f, 24.0f ) ) {
                OPENFILENAMEW ofn{};
                wchar_t file[ 512 ]{};
                ofn.lStructSize = sizeof( ofn );
                ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Replay Files\0*.osr\0All\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = 512;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if ( GetOpenFileNameW( &ofn ) ) {
                    WideCharToMultiByte( CP_UTF8, 0, file, -1, m_replay_path_utf8, IM_ARRAYSIZE( m_replay_path_utf8 ), nullptr, nullptr );
                    m_replay.replay_path.assign( file );
                    m_replay.load_replay( );
                    m_replay.reset_sync( );
                }
            }
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            if ( button( "Load Replay", L_W - 24.0f, 24.0f ) ) {
                wchar_t wide[ 512 ]{};
                MultiByteToWideChar( CP_UTF8, 0, m_replay_path_utf8, -1, wide, 512 );
                m_replay.replay_path = wide;
                m_replay.load_replay( );
                m_replay.reset_sync( );
            }
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(L_X, lbox_top), S(L_X + L_W, lbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( L_X + 14.0f, lbox_top + 8.0f ), theme::text_accent(), "replay loading" );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 14.0f;
            float ry = rbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( R_X + 12.0f, ry ) );
            checkbox( "Parse buttons", &m_replay.parse_buttons );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            char buf[ 64 ];
            sprintf_s( buf, "Frames: %zu", m_replay.frame_count( ) );
            dl->AddText( S( R_X + 14.0f, ry ), theme::text(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Valid: %s", m_replay.replay_valid( ) ? "yes" : "no" );
            dl->AddText( S( R_X + 14.0f, ry ), m_replay.replay_valid( ) ? IM_COL32( 100, 230, 160, 255 ) : IM_COL32( 255, 130, 130, 255 ), buf );
            ry += ImGui::GetTextLineHeight( );

            if ( !m_replay.last_load_error( ).empty( ) ) {
                ry += 4.0f;
                dl->AddText( S( R_X + 14.0f, ry ), IM_COL32( 255, 130, 130, 255 ), m_replay.last_load_error( ).c_str( ) );
                ry += ImGui::GetTextLineHeight( );
            }

            const float rbox_bottom = ry + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(R_X, rbox_top), S(R_X + R_W, rbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( R_X + 14.0f, rbox_top + 8.0f ), theme::text_accent(), "replay options" );
            dl->ChannelsMerge( );
        }
         else if ( m_tab == 5 ) {
             const float lbox_top = TITLE_H + 14.0f + content_slide + replay_banner_h;
            float ly = lbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            dl->AddText( S( L_X + 14.0f, ly ), theme::text_bright(), "Gameplay & Menu Keybinds:" );
            ly += ImGui::GetTextLineHeight( ) + 8.0f;

            char left_buf[ 16 ]{}, right_buf[ 16 ]{}, menu_buf[ 16 ]{};
            GetKeyNameTextA( MapVirtualKeyA( m_custom_left_key, MAPVK_VK_TO_VSC ) << 16, left_buf, sizeof( left_buf ) );
            GetKeyNameTextA( MapVirtualKeyA( m_custom_right_key, MAPVK_VK_TO_VSC ) << 16, right_buf, sizeof( right_buf ) );
            GetKeyNameTextA( MapVirtualKeyA( m_menu_keybind, MAPVK_VK_TO_VSC ) << 16, menu_buf, sizeof( menu_buf ) );
            if ( !left_buf[ 0 ] ) {
                if ( m_custom_left_key >= 32 && m_custom_left_key <= 126 ) sprintf_s( left_buf, "%c", m_custom_left_key );
                else sprintf_s( left_buf, "0x%02X", m_custom_left_key );
            }
            if ( !right_buf[ 0 ] ) {
                if ( m_custom_right_key >= 32 && m_custom_right_key <= 126 ) sprintf_s( right_buf, "%c", m_custom_right_key );
                else sprintf_s( right_buf, "0x%02X", m_custom_right_key );
            }
            if ( !menu_buf[ 0 ] ) {
                if ( m_menu_keybind >= 32 && m_menu_keybind <= 126 ) sprintf_s( menu_buf, "%c", m_menu_keybind );
                else sprintf_s( menu_buf, "0x%02X", m_menu_keybind );
            }

            if ( m_waiting_left ) {
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                button( "Press key...##lbtn", L_W - 24.0f, 22.0f );
                for ( int k = 8; k < 256; ++k ) {
                    if ( k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ) continue;
                    if ( GetAsyncKeyState( k ) & 0x8000 ) {
                        m_custom_left_key = k;
                        m_waiting_left = false;
                        if ( m_relax.is_active( ) ) {
                            osu::game_snapshot_t mod = snap.game;
                            apply_custom_keys( mod );
                            m_relax.on_leave_play( mod );
                        }
                        break;
                    }
                }
            }
            else {
                const std::string lbl = std::string( "Left Key: " ) + left_buf + "##lbtn";
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                if ( button( lbl.c_str( ), L_W - 24.0f, 22.0f ) ) {
                    m_waiting_left = true; m_waiting_right = false; m_waiting_menu = false;
                }
            }
            ly += 26.0f;

            if ( m_waiting_right ) {
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                button( "Press key...##rbtn", L_W - 24.0f, 22.0f );
                for ( int k = 8; k < 256; ++k ) {
                    if ( k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ) continue;
                    if ( GetAsyncKeyState( k ) & 0x8000 ) {
                        m_custom_right_key = k;
                        m_waiting_right = false;
                        if ( m_relax.is_active( ) ) {
                            osu::game_snapshot_t mod = snap.game;
                            apply_custom_keys( mod );
                            m_relax.on_leave_play( mod );
                        }
                        break;
                    }
                }
            }
            else {
                const std::string lbl = std::string( "Right Key: " ) + right_buf + "##rbtn";
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                if ( button( lbl.c_str( ), L_W - 24.0f, 22.0f ) ) {
                    m_waiting_right = true; m_waiting_left = false; m_waiting_menu = false;
                }
            }
            ly += 26.0f;

            if ( m_waiting_menu ) {
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                button( "Press key...##menubtn", L_W - 24.0f, 22.0f );
                for ( int k = 8; k < 256; ++k ) {
                    if ( k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ) continue;
                    if ( GetAsyncKeyState( k ) & 0x8000 ) {
                        m_menu_keybind = k;
                        m_waiting_menu = false;
                        break;
                    }
                }
            }
            else {
                const std::string lbl = std::string( "Menu Toggle Key: " ) + menu_buf + "##menubtn";
                ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
                if ( button( lbl.c_str( ), L_W - 24.0f, 22.0f ) ) {
                    m_waiting_menu = true; m_waiting_left = false; m_waiting_right = false;
                }
            }
            ly += 28.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            checkbox( "Stream proof", &stream_proof );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 12.0f, ly ) );
            checkbox( "Watermark", &m_hud_enabled );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            const float lbox_bottom = ly + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(L_X, lbox_top), S(L_X + L_W, lbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( L_X + 14.0f, lbox_top + 8.0f ), theme::text_accent(), "gameplay bindings" );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 14.0f;
            float ry = rbox_top + 30.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            const char* client = snap.game.client == osu::client_kind_t::lazer ? "osu!lazer" : "none";

            const bool osu_wnd = input::target_window( ) && IsWindow( input::target_window( ) );

            char buf[ 128 ];
            sprintf_s( buf, "Osu window: %s", osu_wnd ? "found" : "not found" );
            dl->AddText( S( R_X + 14.0f, ry ), osu_wnd ? IM_COL32( 100, 230, 160, 255 ) : theme::text_dim(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Client: %s", client );
            dl->AddText( S( R_X + 14.0f, ry ), theme::text_bright(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Attached PID: %d", snap.game.pid );
            dl->AddText( S( R_X + 14.0f, ry ), theme::text(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            if ( snap.game.cur_state == osu::game_state_t::play )
                sprintf_s( buf, "Time: %d ms", snap.game.cur_time );
            else sprintf_s( buf, "Time: --" );
            dl->AddText( S( R_X + 14.0f, ry ), theme::text(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Aim mouse hook: %s", m_mouse_hook.installed( ) ? "active" : "failed (poll fallback)" );
            dl->AddText( S( R_X + 14.0f, ry ),
                m_mouse_hook.installed( ) ? IM_COL32( 100, 230, 160, 255 ) : IM_COL32( 255, 200, 100, 255 ), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Mouse input: %s", input::using_nt_input( ) ? "win32u" : "SendInput" );
            dl->AddText( S( R_X + 14.0f, ry ), theme::text(), buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            if ( snap.beatmap.loaded ) {
                sprintf_s( buf, "CS: %.1f | OD: %.1f | AR: %.1f", snap.beatmap.cs, snap.beatmap.od, snap.beatmap.ar );
                dl->AddText( S( R_X + 14.0f, ry ), theme::text_bright(), buf );
                ry += ImGui::GetTextLineHeight( ) + 4.0f;
            }



            const float rbox_bottom = ry + 12.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_glass_card( dl, S(R_X, rbox_top), S(R_X + R_W, rbox_bottom), 8.0f, theme::accent() );
            dl->AddText( S( R_X + 14.0f, rbox_top + 8.0f ), theme::text_accent(), "runtime status" );
            dl->ChannelsMerge( );
        }
        else if ( m_tab == 6 ) {
            const float lbox_top=TITLE_H+14.f+content_slide;float ly=lbox_top+30.f;
            dl->ChannelsSplit(2);dl->ChannelsSetCurrent(1);
            dl->AddText(S(L_X+14.f,ly),theme::text_bright(),"Cloud configs · Stable only");ly+=ImGui::GetTextLineHeight()+8.f;
            ImGui::SetCursorPos(ImVec2(L_X+12.f,ly));if(button("Refresh cloud",L_W-24.f,24.f))refresh_cloud();ly=ImGui::GetCursorPos().y+8.f;
            const float list_h=MENU_H-ly-96.f;ImGui::SetCursorPos(ImVec2(L_X+12.f,ly));
            if(ImGui::BeginListBox("##cloud_configs",ImVec2(L_W-24.f,list_h))){
                for(int i=0;i<(int)m_cloud_profiles.size();++i){const auto& c=m_cloud_profiles[(size_t)i];std::string label=c.name+"##"+c.id;if(c.installed)label+="  [installed]";if(c.status=="pending")label+="  [review]";if(ImGui::Selectable(label.c_str(),m_config_selected==i)){m_config_selected=i;strncpy_s(m_config_name_utf8,c.name.c_str(),_TRUNCATE);strncpy_s(m_config_description_utf8,c.description.c_str(),_TRUNCATE);}}
                ImGui::EndListBox();
            }
            ly=ImGui::GetCursorPos().y+7.f;
            if(m_config_selected>=0&&m_config_selected<(int)m_cloud_profiles.size()){
                const auto& c=m_cloud_profiles[(size_t)m_config_selected];
                ImGui::SetCursorPos(ImVec2(L_X+12.f,ly));
                if(c.installed){if(button("Apply",(L_W-36.f)*.5f,24.f))cloud_task(6,{{"id",c.id},{"revision",c.revision}});ImGui::SetCursorPos(ImVec2(L_X+18.f+(L_W-36.f)*.5f,ly));if(button("Remove",(L_W-36.f)*.5f,24.f))cloud_task(5,{{"id",c.id}});}
                else if(button("Install",L_W-24.f,24.f))cloud_task(4,{{"id",c.id},{"revision",c.revision}});
            }
            ly=ImGui::GetCursorPos().y+5.f;if(!m_config_status.empty())dl->AddText(S(L_X+14.f,ly),theme::text_dim(),m_config_status.substr(0,52).c_str());
            const float lbox_bottom=std::min((float)MENU_H-12.f,ly+28.f);dl->ChannelsSetCurrent(0);draw_glass_card(dl,S(L_X,lbox_top),S(L_X+L_W,lbox_bottom),8.f,theme::accent());dl->AddText(S(L_X+14.f,lbox_top+8.f),theme::text_accent(),"cloud library");dl->ChannelsMerge();

            const float rbox_top=TITLE_H+14.f;float ry=rbox_top+30.f;dl->ChannelsSplit(2);dl->ChannelsSetCurrent(1);
            dl->AddText(S(R_X+14.f,ry),theme::text_bright(),"Publish your current settings");ry+=ImGui::GetTextLineHeight()+8.f;
            dl->AddText(S(R_X+14.f,ry),theme::text_dim(),"Name");ry+=20.f;ImGui::SetCursorPos(ImVec2(R_X+12.f,ry));text_input("##cloud_name",m_config_name_utf8,IM_ARRAYSIZE(m_config_name_utf8),R_W-24.f);ry=ImGui::GetCursorPos().y+7.f;
            dl->AddText(S(R_X+14.f,ry),theme::text_dim(),"Description");ry+=20.f;ImGui::SetCursorPos(ImVec2(R_X+12.f,ry));text_input("##cloud_desc",m_config_description_utf8,IM_ARRAYSIZE(m_config_description_utf8),R_W-24.f);ry=ImGui::GetCursorPos().y+10.f;
            dl->AddText(S(R_X+14.f,ry),theme::text_dim(),"Save private: only your account sees it.");ry+=21.f;dl->AddText(S(R_X+14.f,ry),theme::text_dim(),"Submit for review: asks an admin to publish it");ry+=19.f;dl->AddText(S(R_X+14.f,ry),theme::text_dim(),"for other Stable users. No .cfg file is created.");ry+=30.f;
            auto send=[&](int kind){std::string name=m_config_name_utf8,desc=m_config_description_utf8;if(name.size()<2){m_config_status="Enter a name (2+ characters).";return;}auto cur=capture_settings();cur.lab_enabled=false;cur.replay_path_utf8.clear();std::ostringstream out;config::serialize_settings(out,name,cur);cloud_task(kind,{{"name",name},{"description",desc},{"cfg",out.str()}});};
            ImGui::SetCursorPos(ImVec2(R_X+12.f,ry));if(button("Save private",(R_W-36.f)*.5f,26.f))send(2);ImGui::SetCursorPos(ImVec2(R_X+18.f+(R_W-36.f)*.5f,ry));if(button("Submit for review",(R_W-36.f)*.5f,26.f))send(3);ry=ImGui::GetCursorPos().y+18.f;
            ImGui::SetCursorPos(ImVec2(R_X+12.f,ry));if(button("UNINJECT / CLOSE",R_W-24.f,26.f)){notify("OSU!BAND","Closing cleanly…");PostMessageW(m_hwnd,WM_CLOSE,0,0);}ry=ImGui::GetCursorPos().y+8.f;
            const float rbox_bottom=ry+12.f;dl->ChannelsSetCurrent(0);draw_glass_card(dl,S(R_X,rbox_top),S(R_X+R_W,rbox_bottom),8.f,theme::accent());dl->AddText(S(R_X+14.f,rbox_top+8.f),theme::text_accent(),"publish / review");dl->ChannelsMerge();
        }

        render_open_dropdown( );
        render_open_color_picker( );

        ImGui::SetCursorPos( ImVec2( wsize.x, wsize.y ) );
        ImGui::Dummy( ImVec2( 1.0f, 1.0f ) );

        ImGui::End( );
    }
}
