#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#else
#include <cstdlib>
constexpr int VK_F4 = 0x73;
#endif
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <locale>
#include <limits>

namespace config {

    struct settings_t {
        bool aim_enabled = false;
        bool aim_ignore_sliders = false;
        bool aim_tablet_mode = false;
        bool aim_legit_mode = true;
        float aim_strength_x = 5.0f;
        float aim_strength_y = 4.5f;
        float aim_decay_far = 0.02f;
        float aim_lerp = 0.24f;
        float aim_freeze_lerp = 0.17f;
        float aim_window = 100.f;
        float aim_legit_clamp = 1.3f;


        bool relax_enabled = false;
        float relax_ur = 60.f;
        int relax_tap_style = 0;
        int relax_singletap_bpm_cap = 100;
        float relax_k1_hold_center = 48.f;
        float relax_k1_hold_spread = 12.f;
        float relax_k2_hold_center = 48.f;
        float relax_k2_hold_spread = 12.f;
        float relax_hold_floor = 12.f;
        float relax_hold_ceiling = 150.f;
        int relax_manual_offset_ms = 0;

        bool replay_enabled = false;
        std::string replay_path_utf8;
        bool replay_parse_buttons = true;
        bool replay_move_cursor = true;
        bool lab_enabled = false;
        bool hud_enabled = false;
        int emergency_key = 0x77;

        bool autobot_enabled = false;
        float autobot_aim_spread = 0.15f;
        float autobot_curve_strength = 0.33f;
        float autobot_drift_amount = 1.5f;
        float autobot_momentum = 0.85f;
        float autobot_slider_laziness = 0.15f;
        float autobot_spinner_rpm = 400.f;

        bool tap_enabled = false;
        int tap_assist_window = 100;
        int tap_randomization = 15;
        bool tap_ignore_sliders = false;

        int custom_left_key = 'Z';
        int custom_right_key = 'X';
        int menu_keybind = VK_F4;
        bool stream_proof = false;
        std::string songs_path_utf8;

    };

    // Runtime configs are cloud-only.  The client deliberately does not
    // create a local configs directory or persist .cfg files.  Only the
    // encrypted account/device session is stored by impl/cloud/client.hxx.

    inline void write_line( std::ostream& out, const char* key, bool v ) {
        out << key << '=' << ( v ? '1' : '0' ) << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, int v ) {
        out << key << '=' << v << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, float v ) {
        out << key << '=' << v << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, const std::string& v ) {
        out << key << '=';
        for ( char c : v ) {
            if ( c == '\n' || c == '\r' )
                continue;
            if ( c == '\\' )
                out << "\\\\";
            else if ( c == '=' )
                out << "\\e";
            else
                out << c;
        }
        out << '\n';
    }

    inline std::string unescape_value( std::string v ) {
        std::string out;
        out.reserve( v.size( ) );
        for ( size_t i = 0; i < v.size( ); ++i ) {
            if ( v[ i ] == '\\' && i + 1 < v.size( ) ) {
                if ( v[ i + 1 ] == '\\' ) {
                    out.push_back( '\\' );
                    ++i;
                }
                else if ( v[ i + 1 ] == 'e' ) {
                    out.push_back( '=' );
                    ++i;
                }
                else {
                    out.push_back( v[ i ] );
                }
            }
            else {
                out.push_back( v[ i ] );
            }
        }
        return out;
    }

    inline void serialize_settings(std::ostream& out, const std::string& name, const settings_t& s) {
        out.imbue(std::locale::classic());
        write_line( out, "profile_name", name );
        write_line( out, "aim.enabled", s.aim_enabled );
        write_line( out, "aim.ignore_sliders", s.aim_ignore_sliders );
        write_line( out, "aim.tablet_mode", s.aim_tablet_mode );
        write_line( out, "aim.strength_x", s.aim_strength_x );
        write_line( out, "aim.strength_y", s.aim_strength_y );
        write_line( out, "aim.decay_far", s.aim_decay_far );
        write_line( out, "aim.lerp", s.aim_lerp );
        write_line( out, "aim.freeze_lerp", s.aim_freeze_lerp );
        write_line( out, "aim.window", s.aim_window );
        write_line( out, "aim.legit_mode", s.aim_legit_mode );
        write_line( out, "aim.legit_clamp", s.aim_legit_clamp );


        write_line( out, "relax.enabled", s.relax_enabled );
        write_line( out, "relax.ur", s.relax_ur );
        write_line( out, "relax.tap_style", s.relax_tap_style );
        write_line( out, "relax.singletap_bpm_cap", s.relax_singletap_bpm_cap );
        write_line( out, "relax.k1_hold_center", s.relax_k1_hold_center );
        write_line( out, "relax.k1_hold_spread", s.relax_k1_hold_spread );
        write_line( out, "relax.k2_hold_center", s.relax_k2_hold_center );
        write_line( out, "relax.k2_hold_spread", s.relax_k2_hold_spread );
        write_line( out, "relax.hold_floor", s.relax_hold_floor );
        write_line( out, "relax.hold_ceiling", s.relax_hold_ceiling );
        write_line( out, "relax.manual_offset_ms", s.relax_manual_offset_ms );

        write_line( out, "replay.enabled", s.replay_enabled );
        write_line( out, "replay.path", s.replay_path_utf8 );
        write_line( out, "replay.parse_buttons", s.replay_parse_buttons );
        write_line(out,"replay.move_cursor",s.replay_move_cursor);
        write_line(out,"system.lab_enabled",s.lab_enabled);
        write_line(out,"system.hud_enabled",s.hud_enabled);
        write_line(out,"keys.emergency",s.emergency_key);

        write_line( out, "autobot.enabled", s.autobot_enabled );
        write_line( out, "autobot.aim_spread", s.autobot_aim_spread );
        write_line( out, "autobot.curve_strength", s.autobot_curve_strength );
        write_line( out, "autobot.drift_amount", s.autobot_drift_amount );
        write_line( out, "autobot.momentum", s.autobot_momentum );
        write_line( out, "autobot.slider_laziness", s.autobot_slider_laziness );
        write_line( out, "autobot.spinner_rpm", s.autobot_spinner_rpm );

        write_line( out, "tap.enabled", s.tap_enabled );
        write_line( out, "tap.assist_window", s.tap_assist_window );
        write_line( out, "tap.randomization", s.tap_randomization );
        write_line( out, "tap.ignore_sliders", s.tap_ignore_sliders );


        write_line( out, "keys.left", s.custom_left_key );
        write_line( out, "keys.right", s.custom_right_key );
        write_line( out, "keys.menu", s.menu_keybind );
        write_line( out, "system.stream_proof", s.stream_proof );
        write_line( out, "system.songs_path", s.songs_path_utf8 );
    }

    inline bool parse_bool( const std::string& v, bool& out ) {
        if ( v == "1" || v == "true" || v == "True" || v == "yes" ) {
            out = true;
            return true;
        }
        if ( v == "0" || v == "false" || v == "False" || v == "no" ) {
            out = false;
            return true;
        }
        return false;
    }

    inline bool parse_settings(std::istream& in, settings_t& s) {
        s = settings_t{};
        std::string line;
        while ( std::getline( in, line ) ) {
            if ( line.empty( ) || line[ 0 ] == '#' )
                continue;
            const auto eq = line.find( '=' );
            if ( eq == std::string::npos )
                continue;

            const std::string key = line.substr( 0, eq );
            const std::string val = unescape_value( line.substr( eq + 1 ) );

            auto parse_int = [ & ]( int& dst ) {
                try {
                    size_t n=0; const int parsed=std::stoi(val,&n);
                    if(n==val.size()) dst=parsed;
                }
                catch ( ... ) {
                }
            };
            auto parse_float = [ & ]( float& dst ) {
                try {
                    std::istringstream input(val); input.imbue(std::locale::classic());
                    float parsed=0; input >> parsed;
                    if(input && input.eof() && std::isfinite(parsed)) dst=parsed;
                }
                catch ( ... ) {
                }
            };

            if ( key == "aim.enabled" )
                parse_bool( val, s.aim_enabled );
            else if ( key == "aim.ignore_sliders" )
                parse_bool( val, s.aim_ignore_sliders );
            else if ( key == "aim.tablet_mode" )
                parse_bool( val, s.aim_tablet_mode );
            else if ( key == "aim.strength_x" || key == "aim.gain_x" ) {
                float v = 7.5f;
                parse_float( v );
                if ( v <= 1.0f ) v = std::clamp( v * 15.0f, 1.0f, 15.0f );
                s.aim_strength_x = v;
            }
            else if ( key == "aim.strength_y" || key == "aim.gain_y" ) {
                float v = 7.5f;
                parse_float( v );
                if ( v <= 1.0f ) v = std::clamp( v * 15.0f, 1.0f, 15.0f );
                s.aim_strength_y = v;
            }
            else if ( key == "aim.strength" ) {
                float v = 0.5f;
                parse_float( v );
                if ( v > 1.0f ) v = std::clamp( v * 0.06666667f, 0.0f, 1.0f );
                s.aim_strength_x = std::clamp( v * 15.0f, 1.0f, 15.0f );
                s.aim_strength_y = std::clamp( v * 15.0f, 1.0f, 15.0f );
            }
            else if ( key == "aim.decay_far" )
                parse_float( s.aim_decay_far );
            else if ( key == "aim.lerp" || key == "aim.smoothness" || key == "aim.attraction_rate" )
                parse_float( s.aim_lerp );
            else if ( key == "aim.freeze_lerp" )
                parse_float( s.aim_freeze_lerp );
            else if ( key == "aim.window" || key == "aim.reaction_ms" || key == "aim.time_window_ms" || key == "aim.window_ms" )
                parse_float( s.aim_window );
            else if ( key == "aim.legit_mode" || key == "aim.motion_sync" )
                parse_bool( val, s.aim_legit_mode );
            else if ( key == "aim.legit_clamp" || key == "aim.velocity_ratio" )
                parse_float( s.aim_legit_clamp );

            else if ( key == "relax.enabled" )
                parse_bool( val, s.relax_enabled );
            else if ( key == "relax.ur" || key == "relax.ur_target" || key == "relax.hit_window_ms" )
                parse_float( s.relax_ur );
            else if ( key == "relax.tap_style" )
                parse_int( s.relax_tap_style );
            else if ( key == "relax.singletap_bpm_cap" )
                parse_int( s.relax_singletap_bpm_cap );
            else if ( key == "relax.k1_hold_center" )
                parse_float( s.relax_k1_hold_center );
            else if ( key == "relax.k1_hold_spread" )
                parse_float( s.relax_k1_hold_spread );
            else if ( key == "relax.k2_hold_center" )
                parse_float( s.relax_k2_hold_center );
            else if ( key == "relax.k2_hold_spread" )
                parse_float( s.relax_k2_hold_spread );
            else if ( key == "relax.hold_floor" )
                parse_float( s.relax_hold_floor );
            else if ( key == "relax.hold_ceiling" )
                parse_float( s.relax_hold_ceiling );
            else if ( key == "relax.manual_offset_ms" )
                parse_int( s.relax_manual_offset_ms );
            else if ( key == "replay.enabled" )
                parse_bool( val, s.replay_enabled );
            else if ( key == "replay.path" )
                s.replay_path_utf8 = val;
            else if ( key == "replay.parse_buttons" )
                parse_bool( val, s.replay_parse_buttons );
            else if(key=="replay.move_cursor") parse_bool(val,s.replay_move_cursor);
            else if(key=="system.lab_enabled") parse_bool(val,s.lab_enabled);
            else if(key=="system.hud_enabled") parse_bool(val,s.hud_enabled);
            else if(key=="keys.emergency") parse_int(s.emergency_key);
            else if ( key == "autobot.enabled" )
                parse_bool( val, s.autobot_enabled );
            else if ( key == "autobot.aim_spread" )
                parse_float( s.autobot_aim_spread );
            else if ( key == "autobot.curve_strength" )
                parse_float( s.autobot_curve_strength );
            else if ( key == "autobot.drift_amount" )
                parse_float( s.autobot_drift_amount );
            else if ( key == "autobot.momentum" )
                parse_float( s.autobot_momentum );
            else if ( key == "autobot.slider_laziness" )
                parse_float( s.autobot_slider_laziness );
            else if ( key == "autobot.spinner_rpm" )
                parse_float( s.autobot_spinner_rpm );
            else if ( key == "tap.enabled" )
                parse_bool( val, s.tap_enabled );
            else if ( key == "tap.assist_window" )
                parse_int( s.tap_assist_window );
            else if ( key == "tap.randomization" )
                parse_int( s.tap_randomization );
            else if ( key == "tap.ignore_sliders" )
                parse_bool( val, s.tap_ignore_sliders );
            else if ( key == "keys.left" )
                parse_int( s.custom_left_key );
            else if ( key == "keys.right" )
                parse_int( s.custom_right_key );
            else if ( key == "keys.menu" )
                parse_int( s.menu_keybind );
            else if ( key == "system.stream_proof" )
                parse_bool( val, s.stream_proof );
            else if ( key == "system.songs_path" )
                s.songs_path_utf8 = val;
        }
        return true;
    }


    inline void validate(settings_t& s) {
        const settings_t defaults;
        s.aim_strength_x=std::isfinite(s.aim_strength_x) ? std::clamp(s.aim_strength_x, 1.0f, 15.0f) : defaults.aim_strength_x;
        s.aim_strength_y=std::isfinite(s.aim_strength_y) ? std::clamp(s.aim_strength_y, 1.0f, 15.0f) : defaults.aim_strength_y;
        s.aim_decay_far=std::isfinite(s.aim_decay_far) ? std::clamp(s.aim_decay_far, 0.01f, 0.5f) : defaults.aim_decay_far;
        s.aim_lerp=std::isfinite(s.aim_lerp) ? std::clamp(s.aim_lerp, 0.15f, 0.4f) : defaults.aim_lerp;
        s.aim_freeze_lerp=std::isfinite(s.aim_freeze_lerp) ? std::clamp(s.aim_freeze_lerp, 0.1f, 0.3f) : defaults.aim_freeze_lerp;
        s.aim_window=std::isfinite(s.aim_window) ? std::clamp(s.aim_window, 75.0f, 110.0f) : defaults.aim_window;
        s.aim_legit_clamp=std::isfinite(s.aim_legit_clamp) ? std::clamp(s.aim_legit_clamp, 1.05f, 1.8f) : defaults.aim_legit_clamp;
        s.relax_ur=std::isfinite(s.relax_ur) ? std::clamp(s.relax_ur, 0.0f, 200.0f) : defaults.relax_ur;
        s.relax_k1_hold_center=std::isfinite(s.relax_k1_hold_center) ? std::clamp(s.relax_k1_hold_center, 30.0f, 120.0f) : defaults.relax_k1_hold_center;
        s.relax_k2_hold_center=std::isfinite(s.relax_k2_hold_center) ? std::clamp(s.relax_k2_hold_center, 30.0f, 120.0f) : defaults.relax_k2_hold_center;
        s.relax_k1_hold_spread=std::isfinite(s.relax_k1_hold_spread) ? std::clamp(s.relax_k1_hold_spread, 2.0f, 30.0f) : defaults.relax_k1_hold_spread;
        s.relax_k2_hold_spread=std::isfinite(s.relax_k2_hold_spread) ? std::clamp(s.relax_k2_hold_spread, 2.0f, 30.0f) : defaults.relax_k2_hold_spread;
        s.relax_hold_floor=std::isfinite(s.relax_hold_floor) ? std::clamp(s.relax_hold_floor, 10.0f, 60.0f) : defaults.relax_hold_floor;
        s.relax_hold_ceiling=std::isfinite(s.relax_hold_ceiling) ? std::clamp(s.relax_hold_ceiling, 60.0f, 150.0f) : defaults.relax_hold_ceiling;
        s.autobot_aim_spread=std::isfinite(s.autobot_aim_spread) ? std::clamp(s.autobot_aim_spread, 0.0f, 1.0f) : defaults.autobot_aim_spread;
        s.autobot_curve_strength=std::isfinite(s.autobot_curve_strength) ? std::clamp(s.autobot_curve_strength, 0.0f, 1.0f) : defaults.autobot_curve_strength;
        s.autobot_drift_amount=std::isfinite(s.autobot_drift_amount) ? std::clamp(s.autobot_drift_amount, 0.0f, 5.0f) : defaults.autobot_drift_amount;
        s.autobot_momentum=std::isfinite(s.autobot_momentum) ? std::clamp(s.autobot_momentum, 0.0f, 0.95f) : defaults.autobot_momentum;
        s.autobot_slider_laziness=std::isfinite(s.autobot_slider_laziness) ? std::clamp(s.autobot_slider_laziness, 0.0f, 1.0f) : defaults.autobot_slider_laziness;
        s.autobot_spinner_rpm=std::isfinite(s.autobot_spinner_rpm) ? std::clamp(s.autobot_spinner_rpm, 200.0f, 477.0f) : defaults.autobot_spinner_rpm;
        s.relax_tap_style=std::clamp(s.relax_tap_style, 0, 1);
        s.relax_singletap_bpm_cap=std::clamp(s.relax_singletap_bpm_cap, 100, 300);
        s.relax_manual_offset_ms=std::clamp(s.relax_manual_offset_ms, -100, 100);
        s.tap_assist_window=std::clamp(s.tap_assist_window, 0, 250);
        s.tap_randomization=std::clamp(s.tap_randomization, 0, 40);
        s.menu_keybind=std::clamp(s.menu_keybind, 8, 254);
        s.emergency_key=std::clamp(s.emergency_key, 8, 254);
        s.custom_left_key=std::clamp(s.custom_left_key, 8, 254);
        s.custom_right_key=std::clamp(s.custom_right_key, 8, 254);
        if(s.custom_left_key==s.custom_right_key) s.custom_right_key=(s.custom_left_key=='X'?'Z':'X');
        if(s.menu_keybind==s.emergency_key) s.emergency_key=(s.menu_keybind==0x77?0x78:0x77);
        // Only one component may own automatic tapping/cursor playback.
        if(s.replay_enabled) {s.autobot_enabled=false;s.aim_enabled=false;s.relax_enabled=false;s.tap_enabled=false;}
        else if(s.autobot_enabled) {s.aim_enabled=false;s.relax_enabled=false;s.tap_enabled=false;}
        else if(s.relax_enabled) s.tap_enabled=false;
    }

    struct profile_meta_t {
        std::string name, author="OSU!BAND", recipient="Everyone", description, channel="stable";
        int revision=1;
        std::string id,owner_id,avatar_url,status="private",review_note,author_role="user",reviewed_by_name;
        int64_t updated_at=0,reviewed_at=0;
        bool official=false,installed=false;
    };

    // Kept as explicit stubs so older call sites fail closed instead of
    // silently writing profile files to disk.
    inline bool save_profile(const std::string&, const settings_t&, const profile_meta_t* =nullptr) { return false; }
    inline bool load_profile(const std::string&, settings_t&) { return false; }
    inline std::vector<std::string> list_profiles() { return {}; }


}
