#pragma once

#include <impl/memory/process.hxx>
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <string>
#include <vector>

namespace beatmap::songs {

    inline std::wstring get_process_exe_directory( const memory::c_process& process ) {
        if ( !process.valid( ) )
            return L"";

        wchar_t path[ MAX_PATH ]{};
        DWORD size = MAX_PATH;
        if ( !QueryFullProcessImageNameW( process.handle( ), 0, path, &size ) )
            return L"";

        std::wstring exe = path;
        const auto slash = exe.find_last_of( L"\\/" );
        if ( slash != std::wstring::npos )
            exe.resize( slash );
        return exe;
    }

    inline std::wstring appdata_path( int csidl, const wchar_t* suffix ) {
        wchar_t base[ MAX_PATH ]{};
        if ( FAILED( SHGetFolderPathW( nullptr, csidl, nullptr, 0, base ) ) )
            return L"";
        std::wstring path = base;
        path += suffix;
        return path;
    }

    inline std::wstring resolve_lazer_data_dir( const memory::c_process& process ) {
        std::vector<std::wstring> candidates;
        candidates.push_back( appdata_path( CSIDL_APPDATA, L"\\osu" ) );
        candidates.push_back( appdata_path( CSIDL_LOCAL_APPDATA, L"\\osu" ) );
        candidates.push_back( appdata_path( CSIDL_LOCAL_APPDATA, L"\\osulazer\\current" ) );

        const auto exe_dir = get_process_exe_directory( process );
        if ( !exe_dir.empty( ) )
            candidates.push_back( exe_dir );

        // A lazer data root normally contains client.realm and files/.
        for ( const auto& path : candidates ) {
            if ( path.empty( ) )
                continue;
            const auto root = std::filesystem::path( path );
            if ( std::filesystem::is_directory( root / L"files" ) &&
                 std::filesystem::is_regular_file( root / L"client.realm" ) )
                return path;
        }

        // Recovery/portable installs can expose the resource store before the realm file.
        for ( const auto& path : candidates ) {
            if ( path.empty( ) )
                continue;
            if ( std::filesystem::is_directory( std::filesystem::path( path ) / L"files" ) )
                return path;
        }

        return appdata_path( CSIDL_APPDATA, L"\\osu" );
    }

}
