#pragma once

#include <impl/defs/offsets_lazer.hxx>
#include <impl/memory/process.hxx>
#include <impl/util/playfield.hxx>
#include <impl/memory/input.hxx>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace beatmap {

    class c_lazer_ruleset_reader {
    public:
        bool try_load(
            memory::c_process& process,
            const osu::game_snapshot_t& game,
            const offsets::lazer::table_t& off,
            osu::beatmap_data_t& out ) {

            if ( !off.has_hitobject_offsets( ) )
                return false;

            if ( game.drawable_ruleset == 0 || game.cur_state != osu::game_state_t::play )
                return false;

            const auto beatmap =
                process.read<uint64_t>( game.drawable_ruleset + off.drawable_osu_beatmap );
            if ( !beatmap )
                return false;

            const auto hit_objects_list =
                process.read<uint64_t>( beatmap + off.beatmap_hit_objects );
            if ( !hit_objects_list )
                return false;

            const auto items =
                process.read<uint64_t>( hit_objects_list + off.list_items );
            const auto count = process.read<int32_t>( hit_objects_list + off.list_size );
            if ( !items || count <= 0 || count > 100000 )
                return false;

            out.screen_width = 1920;
            out.screen_height = 1080;
            playfield::get_window_size( input::target_window( ), out.screen_width, out.screen_height );
            out.hr = ( game.cur_mod_state & 16 ) != 0;

            out.cs = 5.f;
            out.od = 5.f;
            out.ar = 5.f;

            const auto processed_beatmap_addr = process.read<uint64_t>( game.drawable_ruleset + off.drawable_osu_beatmap );
            if ( processed_beatmap_addr ) {
                const auto difficulty = process.read<uint64_t>( processed_beatmap_addr + 0x08 );
                if ( difficulty ) {
                    const float cs_val = process.read<float>( difficulty + 0x1c );
                    const float od_val = process.read<float>( difficulty + 0x20 );
                    const float ar_val = process.read<float>( difficulty + 0x24 );
                    if ( cs_val >= 0.f && cs_val <= 12.f ) out.cs = cs_val;
                    if ( od_val >= 0.f && od_val <= 12.f ) out.od = od_val;
                    if ( ar_val >= 0.f && ar_val <= 13.f ) out.ar = ar_val;
                }
            }

            std::vector<osu::hit_object_t> objects;
            objects.reserve( static_cast<size_t>( count ) );

            for ( int32_t i = 0; i < count; ++i ) {
                const auto obj_ptr =
                    process.read<uint64_t>( items + off.array_first_element + static_cast<uint64_t>( i ) * 8 );
                if ( !obj_ptr )
                    continue;

                const auto start_bindable = process.read<uint64_t>(
                    obj_ptr + off.hit_object_start_time_bindable );
                if ( !start_bindable )
                    continue;

                const auto start_time_raw =
                    process.read<double>( start_bindable + off.bindable_number_value );
                if ( !std::isfinite( start_time_raw ) || start_time_raw < -10000 || start_time_raw > 600000 )
                    continue;

                float x = process.read<float>( obj_ptr + off.osu_hit_object_position_xy );
                float y = process.read<float>( obj_ptr + off.osu_hit_object_position_xy + 4 );
                if ( !std::isfinite( x ) || !std::isfinite( y ) )
                    continue;

                osu::hit_object_t obj{};
                obj.start_time = static_cast<int32_t>( start_time_raw );
                obj.end_time = obj.start_time;
                obj.x = x;
                obj.y = out.hr ? ( 384.f - y ) : y;
                obj.type = static_cast<uint8_t>( osu::hit_object_type_t::circle );

                const auto has_dur = process.read<int32_t>( obj_ptr + off.hit_object_has_duration );

                if ( has_dur != 0 ) {
                    const bool is_center =
                        std::abs( x - 256.f ) < 1.f && std::abs( y - 192.f ) < 1.f;

                    if ( is_center ) {
                        obj.type = static_cast<uint8_t>( osu::hit_object_type_t::spinner );
                        read_nested_end( process, off, obj_ptr, obj );
                    }
                    else {
                        obj.type = static_cast<uint8_t>( osu::hit_object_type_t::slider );
                        read_nested_end( process, off, obj_ptr, obj );
                        read_slider_path( process, off, obj_ptr, obj, out.hr );
                    }
                }

                objects.push_back( obj );
            }

            if ( objects.empty( ) )
                return false;

            std::sort( objects.begin( ), objects.end( ),
                []( const osu::hit_object_t& a, const osu::hit_object_t& b ) {
                    return a.start_time < b.start_time;
                } );

            for ( size_t i = 0; i < objects.size( ); ++i ) {
                objects[ i ].stack_index = 0;
                for ( int32_t j = static_cast<int32_t>( i ) - 1; j >= 0; --j ) {
                    const auto& prev = objects[ j ];
                    if ( objects[ i ].start_time - prev.start_time > 2000 )
                        break;
                    
                    float dx = objects[ i ].x - prev.x;
                    float dy = objects[ i ].y - prev.y;
                    float dist = std::sqrt( dx * dx + dy * dy );
                    if ( dist < 5.0f ) {
                        objects[ i ].stack_index = prev.stack_index + 1;
                        break;
                    }
                }
            }

            for ( auto& obj : objects ) {
                project_to_screen( obj, out.screen_width, out.screen_height );
            }

            out.objects = std::move( objects );
            out.loaded = true;
            out.beatmap_path = "(memory:DrawableRuleset.Beatmap.HitObjects)";

            return true;
        }

    private:
        static void read_nested_end(
            memory::c_process& process,
            const offsets::lazer::table_t& off,
            uint64_t obj_ptr,
            osu::hit_object_t& obj ) {

            const auto nested = process.read<uint64_t>( obj_ptr + off.hit_object_nested_objects );
            if ( !nested ) return;

            const auto n_items = process.read<uint64_t>( nested + off.list_items );
            const auto n_count = process.read<int32_t>( nested + off.list_size );
            if ( !n_items || n_count <= 0 || n_count > 1000 ) return;

            const auto last_ptr = process.read<uint64_t>(
                n_items + off.array_first_element + static_cast<uint64_t>( n_count - 1 ) * 8 );
            if ( !last_ptr ) return;

            const auto last_bind = process.read<uint64_t>(
                last_ptr + off.hit_object_start_time_bindable );
            if ( last_bind ) {
                const auto end_time_raw = process.read<double>(
                    last_bind + off.bindable_number_value );
                if ( std::isfinite( end_time_raw ) && end_time_raw > obj.start_time &&
                     end_time_raw < obj.start_time + 300000 )
                    obj.end_time = static_cast<int32_t>( end_time_raw );
            }

            if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) )
                obj.slider_repeat = std::max( 1, n_count - 1 );
        }

        static void read_slider_path(
            memory::c_process& process,
            const offsets::lazer::table_t& off,
            uint64_t obj_ptr,
            osu::hit_object_t& obj,
            bool hr ) {

            const auto path_wrap = process.read<uint64_t>( obj_ptr + off.slider_path_wrapper );
            if ( !path_wrap ) return;

            const auto bl = process.read<uint64_t>( path_wrap + off.path_ctrl_points_list );
            if ( !bl ) return;

            const auto cp_items = process.read<uint64_t>( bl + off.list_items );
            const auto cp_count = process.read<int32_t>( bl + off.list_size );
            if ( !cp_items || cp_count < 2 || cp_count > 500 ) return;

            const size_t data_size = static_cast<size_t>( cp_count ) * 8;
            std::vector<uint8_t> buf( data_size );
            if ( !process.read_buffer( cp_items + off.array_first_element, buf.data( ), data_size ) )
                return;

            const auto type_bind = process.read<uint64_t>( obj_ptr + off.slider_path_wrapper + 0x10 );
            char type_char = 'L';
            if ( type_bind ) {
                int32_t type_val = process.read<int32_t>( type_bind + off.bindable_value );
                if ( type_val == 0 ) type_char = 'C';
                else if ( type_val == 1 ) type_char = 'B';
                else if ( type_val == 2 ) type_char = 'L';
                else if ( type_val == 3 ) type_char = 'P';
            }

            std::ostringstream curve;
            curve << type_char;

            float total_len = 0.f;
            float prev_x = obj.x;
            float prev_y = obj.y;

            for ( int32_t ci = 1; ci < cp_count; ++ci ) {
                const size_t base = static_cast<size_t>( ci ) * 8;
                float ox, oy;
                std::memcpy( &ox, &buf[ base ], 4 );
                std::memcpy( &oy, &buf[ base + 4 ], 4 );

                if ( !std::isfinite( ox ) || !std::isfinite( oy ) )
                    continue;
                if ( std::abs( ox ) > 1000.f || std::abs( oy ) > 1000.f )
                    continue;

                float abs_x = obj.x + ox;
                float abs_y = hr ? ( obj.y - oy ) : ( obj.y + oy );

                curve << "|" << abs_x << ":" << abs_y;

                float dx = abs_x - prev_x;
                float dy = abs_y - prev_y;
                total_len += std::sqrt( dx * dx + dy * dy );
                prev_x = abs_x;
                prev_y = abs_y;
            }

            const std::string curve_str = curve.str( );
            if ( curve_str.size( ) > 2 ) {
                obj.slider_curve_str = curve_str;
                obj.slider_length = total_len;
            }
        }

        static void project_to_screen( osu::hit_object_t& obj, int32_t sw, int32_t sh ) {
            const float playfield_height = sh * 0.8f;
            const float playfield_width = playfield_height * ( 4.f / 3.f );
            const float scale = playfield_width / 512.f;
            const float offset_x = ( sw - playfield_width ) * 0.5f;
            const float offset_y = ( sh - playfield_height ) * 0.5f;

            const float stack_offset = -static_cast<float>( obj.stack_index ) * 6.f * scale;
            obj.screen_x = offset_x + ( obj.x * scale ) + stack_offset;
            obj.screen_y = offset_y + ( obj.y * scale ) + stack_offset + 17.f;
        }
    };

}