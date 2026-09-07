#include <impl/includes.hxx>
#include <impl/ui/overlay.hxx>
#include <core/threads/cache.hxx>
#include <mmsystem.h>
#include <impl/cloud/client.hxx>
#include <impl/cloud/session_lease.hxx>
#include <impl/util/debug_log.hxx>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment( lib, "winmm.lib" )

static void enable_process_dpi_awareness( ) {
    typedef BOOL( WINAPI* PFN_SetProcessDpiAwarenessContext )( void* );
    if ( auto user32 = GetModuleHandleW( L"user32.dll" ) ) {
        if (auto SetProcessDpiAwarenessContextFn = ( PFN_SetProcessDpiAwarenessContext )GetProcAddress( user32, "SetProcessDpiAwarenessContext" ) ) {
            SetProcessDpiAwarenessContextFn( ( void* ) -4);
            return;
        }
    }
    typedef HRESULT( WINAPI* PFN_SetProcessDpiAwareness )( int );
    if ( auto shcore = LoadLibraryW( L"shcore.dll") ) {
        if (auto SetProcessDpiAwarenessFn = ( PFN_SetProcessDpiAwareness )GetProcAddress( shcore, "SetProcessDpiAwareness" ) ) {
            SetProcessDpiAwarenessFn( 2 );
            FreeLibrary( shcore );
            return;
        }
        FreeLibrary( shcore );
    }
    SetProcessDPIAware( );
}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, PWSTR, int ) {
    // Stable and Beta deliberately share one guard. Running two external runtimes
    // against the same osu! process was a source of menu jitter and duplicate input.
    HANDLE instance_mutex = CreateMutexW( nullptr, FALSE, L"Local\\OSUBAND.Runtime.SingleInstance" );
    if ( !instance_mutex ) {
        MessageBoxW( nullptr, L"Could not create the OSU!BAND instance guard.", L"OSU!BAND", MB_ICONERROR | MB_OK );
        return 2;
    }
    struct mutex_scope { HANDLE h{}; ~mutex_scope( ) { if ( h ) CloseHandle( h ); } } single_instance{ instance_mutex };
    if ( GetLastError( ) == ERROR_ALREADY_EXISTS ) {
        MessageBoxW( nullptr, L"OSU!BAND is already running. Close the existing instance before starting another one.", L"OSU!BAND", MB_ICONINFORMATION | MB_OK );
        return 2;
    }

    enable_process_dpi_awareness( );
    struct timer_scope { timer_scope( ) { timeBeginPeriod( 1 ); } ~timer_scope( ) { timeEndPeriod( 1 ); } } timer;

    cloud::client account;
    cloud::json entitlement;
    constexpr const char* channel = "stable";
#ifndef OSUBAND_DEVELOPMENT
    try {
        account = cloud::client::restore( );
        entitlement = account.session( channel );
        if ( !entitlement.value( "authorized", false ) )
            throw std::runtime_error( "Stable subscription inactive. Open the OSU!BAND Loader or your account." );
    }
    catch ( const std::exception& e ) {
        const auto message = cloud::wide( e.what( ) );
        MessageBoxW( nullptr, message.c_str( ), L"OSU!BAND Stable / Account", MB_ICONINFORMATION | MB_OK );
        return 3;
    }
#endif

    threads::c_cache cache;
    if ( !cache.init( ) ) {
        MessageBoxW( nullptr, L"init failed", L"OSU!BAND", MB_ICONERROR | MB_OK );
        return 1;
    }

    ui::c_overlay overlay;
#ifdef OSUBAND_DEVELOPMENT
    overlay.set_account( "LOCAL DEVELOPMENT", "Stable local test" );
#else
    const auto& profile = entitlement.at( "account" );
    overlay.set_account(
        profile.value( "displayName", "OSU!BAND member" ),
        entitlement.value( "plan", "Stable" ),
        profile.value( "avatarUrl", "" ),
        profile.value( "id", "" ) );
#endif

    if ( !overlay.create( instance ) ) {
        MessageBoxW( nullptr, L"create failed", L"OSU!BAND", MB_ICONERROR | MB_OK );
        return 1;
    }

    overlay.set_cache( &cache );
    overlay.set_snapshot_source( [&cache]( ) { return cache.get_snapshot( ); } );
    cache.set_module_tick( [&overlay]( const osu::game_snapshot_t& game, const osu::beatmap_data_t& beatmap ) {
        overlay.tick_modules( game, beatmap );
    } );

    cache.start( );

    std::atomic<bool> monitoring{ true };
    std::thread monitor;
#ifndef OSUBAND_DEVELOPMENT
    monitor = std::thread( [&] {
        cloud::session_lease lease;
        auto accept = [&]( const cloud::json& s ) {
            const int64_t expiry = s.contains( "expiresAt" ) && !s[ "expiresAt" ].is_null( ) ? s[ "expiresAt" ].get<int64_t>( ) : 0;
            lease.accept( GetTickCount64( ), s.value( "serverTime", int64_t( 0 ) ), expiry );
        };
        accept( entitlement );
        overlay.set_authorized( true, lease.deadline( ) );

        while ( monitoring ) {
            // Session checks are deliberately coarse. Cloud state still polls in the
            // overlay, but entitlement validation should not steal frames from osu!.
            for ( int i = 0; i < 300 && monitoring; ++i )
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            if ( !monitoring ) break;

            try {
                const auto s = account.session( channel );
                const bool allowed = s.value( "authorized", false );
                if ( allowed ) accept( s ); else lease.deny( );
                overlay.set_authorized( allowed, lease.deadline( ) );
            }
            catch ( const cloud::api_error& e ) {
                if ( e.status == 401 || e.status == 403 ) lease.deny( );
                overlay.set_authorized( lease.valid( GetTickCount64( ) ), lease.deadline( ) );
                dbg::log( "stable session: HTTP %lu; retained=%d", e.status, lease.valid( GetTickCount64( ) ) );
            }
            catch ( const std::exception& ) {
                overlay.set_authorized( lease.valid( GetTickCount64( ) ), lease.deadline( ) );
                dbg::log( "stable session: network unavailable; retained=%d", lease.valid( GetTickCount64( ) ) );
            }
        }
    } );
#endif

    while ( overlay.pump( ) ) {
        Sleep( 2 );
    }

    monitoring = false;
    if ( monitor.joinable( ) ) monitor.join( );
    cache.stop( );
    overlay.destroy( );
    return 0;
}
