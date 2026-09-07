#pragma once

#include <core/beatmap/i_beatmap_provider.hxx>
#include <core/beatmap/osu_file_parser.hxx>
#include <core/beatmap/lazer_index.hxx>
#include <core/beatmap/lazer_ruleset_reader.hxx>
#include <impl/defs/offsets_lazer.hxx>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <atomic>

namespace beatmap {

    class c_lazer_memory : public i_beatmap_provider {
    public:
        void set_offsets( const offsets::lazer::table_t* offsets ) { m_offsets = offsets; }

        void set_data_dir( std::wstring path ) {
            if ( path != m_data_dir ) {
                m_data_dir = std::move( path );
                m_index.invalidate( );
                m_index_ready.store( false );
            }
        }

        [[nodiscard]] const std::wstring& data_dir( ) const { return m_data_dir; }
        [[nodiscard]] const std::wstring& last_beatmap_path( ) const { return m_last_beatmap_path; }
        [[nodiscard]] bool index_ready( ) const { return m_index_ready.load( ); }

        void warm_index( ) {
            if ( !m_data_dir.empty( ) ) {
                m_index.ensure_built( m_data_dir );
                m_index_ready.store( true );
            }
        }

        void invalidate_cache( ) {
            m_last_sig.clear( );
            m_cached = {};
            m_last_fail_sig.clear( );
        }

        void refresh_index( ) {
            m_index.invalidate( );
            if ( !m_data_dir.empty( ) )
                m_index.ensure_built( m_data_dir );
        }

        bool try_load( memory::c_process& process, const osu::game_snapshot_t& game, osu::beatmap_data_t& out ) override {
            out = {};
            out.map_id = game.map_id;
            out.set_id = game.set_id;

            const bool has_map =
                game.map_id > 0 || !game.beatmap_hash.empty( ) ||
                ( game.set_id > 0 && !game.beatmap_version.empty( ) );

            if ( !has_map )
                return false;

            const std::string sig =
                std::to_string( game.map_id ) + "|" + std::to_string( game.set_id ) + "|" +
                game.beatmap_hash + "|" + game.beatmap_version + "|" +
                std::to_string( game.cur_mod_state );

            const bool in_play = game.cur_state == osu::game_state_t::play;

            if ( !in_play && sig == m_last_sig && !m_cached.objects.empty( ) ) {
                out = m_cached;
                return true;
            }

            if ( !m_data_dir.empty( ) ) {
                std::wstring path;

                // Lazer stores each imported resource at:
                //   files/<hash[0]>/<hash[0..1]>/<sha256>
                // Try the exact current beatmap hash first. This is instant and avoids
                // blocking gameplay on a full recursive scan of the entire lazer store.
                if ( !game.beatmap_hash.empty( ) )
                    path = find_hash_path( m_data_dir, game.beatmap_hash );

                // The background index is only a fallback for non-default/older stores.
                if ( path.empty( ) && m_index_ready.load( ) ) {
                    if ( !game.beatmap_hash.empty( ) )
                        path = m_index.find_by_hash( game.beatmap_hash );
                    if ( path.empty( ) && game.map_id > 0 )
                        path = m_index.find_by_map_id( game.map_id );
                    if ( path.empty( ) && game.set_id > 0 && !game.beatmap_version.empty( ) )
                        path = m_index.find_by_set_and_version( game.set_id, game.beatmap_version );
                }

                if ( !path.empty( ) ) {
                    m_last_fail_sig.clear( );

                    if ( m_parser.load_from_path( path, game, out ) ) {
                        m_last_beatmap_path = path;
                        m_last_sig = sig;
                        m_cached = out;
                        return true;
                    }
                }
                else if ( m_index_ready.load( ) ) {
                    m_last_fail_sig = sig;
                }
            }

            // Do not feed the feature modules a partially reconstructed in-memory map.
            // The old fallback no longer has a valid one-field circle/slider/spinner
            // discriminator on 2026.804.2. The original .osu parser is authoritative.

            out.error = "lazer beatmap not loaded";
            return false;
        }

    private:
        const offsets::lazer::table_t* m_offsets = nullptr;
        std::wstring m_data_dir;
        std::wstring m_last_beatmap_path;
        std::string m_last_sig;
        c_osu_file_parser m_parser;
        c_lazer_index m_index;
        osu::beatmap_data_t m_cached;
        std::string m_last_fail_sig;
        std::atomic<bool> m_index_ready{ false };


        static std::wstring find_hash_path( const std::wstring& data_dir, const std::string& hash_in ) {
            if ( data_dir.empty( ) || hash_in.empty( ) )
                return L"";

            std::string hash = hash_in;
            std::transform( hash.begin( ), hash.end( ), hash.begin( ),
                []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

            const auto hash_wide = std::wstring( hash.begin( ), hash.end( ) );
            const auto files_root = std::filesystem::path( data_dir ) / L"files";

            if ( hash.size( ) >= 2 ) {
                // Current RealmFileStore path: files/1/1a/<full sha256>.
                auto candidate = files_root
                    / std::wstring( 1, hash[ 0 ] )
                    / std::wstring( hash.begin( ), hash.begin( ) + 2 )
                    / hash_wide;
                if ( std::filesystem::is_regular_file( candidate ) )
                    return candidate.wstring( );
            }

            // Compatibility with older/custom stores seen in the wild.
            if ( hash.size( ) >= 1 ) {
                auto candidate = files_root / std::wstring( 1, hash[ 0 ] ) / hash_wide;
                if ( std::filesystem::is_regular_file( candidate ) )
                    return candidate.wstring( );
            }

            if ( hash.size( ) >= 2 ) {
                auto candidate = files_root / std::wstring( hash.begin( ), hash.begin( ) + 2 ) / hash_wide;
                if ( std::filesystem::is_regular_file( candidate ) )
                    return candidate.wstring( );
            }

            return L"";
        }
    };

}