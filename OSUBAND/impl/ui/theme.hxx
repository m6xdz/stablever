#pragma once

#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace ui::theme {

    // OSU!BAND: deep midnight base + electric cyan + osu-inspired pink.
    inline ImVec4 accent_v4() { return ImVec4( 0.35f, 0.82f, 1.00f, 1.0f ); }
    inline ImU32 accent() { return IM_COL32( 90, 210, 255, 255 ); }
    inline ImU32 accent_alpha( float a ) {
        const float v = a <= 1.0f ? a * 255.0f : a;
        const int alpha = static_cast<int>( std::clamp( v, 0.0f, 255.0f ) );
        return IM_COL32( 90, 210, 255, alpha );
    }
    inline ImU32 accent_dim() { return IM_COL32( 48, 132, 185, 255 ); }

    inline ImU32 secondary() { return IM_COL32( 255, 92, 170, 255 ); }
    inline ImU32 secondary_alpha( float a ) {
        const float v = a <= 1.0f ? a * 255.0f : a;
        const int alpha = static_cast<int>( std::clamp( v, 0.0f, 255.0f ) );
        return IM_COL32( 255, 92, 170, alpha );
    }

    inline ImU32 bg0() { return IM_COL32( 7, 9, 17, 242 ); }
    inline ImU32 bg1() { return IM_COL32( 10, 13, 24, 242 ); }
    inline ImU32 sidebar() { return IM_COL32( 8, 10, 19, 248 ); }
    inline ImU32 panel() { return IM_COL32( 13, 17, 30, 220 ); }
    inline ImU32 panel_hover() { return IM_COL32( 18, 24, 41, 235 ); }
    inline ImU32 border() { return IM_COL32( 255, 255, 255, 18 ); }

    inline ImU32 text() { return IM_COL32( 193, 204, 226, 255 ); }
    inline ImU32 text_bright() { return IM_COL32( 242, 247, 255, 255 ); }
    inline ImU32 text_dim() { return IM_COL32( 104, 117, 144, 255 ); }
    inline ImU32 text_accent() { return IM_COL32( 109, 220, 255, 255 ); }

    inline ImU32 success() { return IM_COL32( 92, 232, 166, 255 ); }
    inline ImU32 warning() { return IM_COL32( 255, 198, 92, 255 ); }
    inline ImU32 danger() { return IM_COL32( 255, 102, 116, 255 ); }

    inline ImU32 track_bg() { return IM_COL32( 22, 29, 47, 235 ); }
    inline ImU32 track_fill() { return IM_COL32( 90, 210, 255, 225 ); }
    inline ImU32 handle() { return IM_COL32( 238, 247, 255, 255 ); }

    inline ImU32 tab_active_bg() { return IM_COL32( 90, 210, 255, 24 ); }
    inline ImU32 tab_hover_bg() { return IM_COL32( 90, 210, 255, 13 ); }

    inline float ease_out_cubic( float t ) {
        t = std::clamp( t, 0.f, 1.f );
        const float f = t - 1.f;
        return f * f * f + 1.f;
    }

    inline float ease_in_out( float t ) {
        t = std::clamp( t, 0.f, 1.f );
        return t < 0.5f ? 2.f * t * t : 1.f - std::pow( -2.f * t + 2.f, 2.f ) * 0.5f;
    }

    inline float ease_out_back( float t ) {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.f;
        t = std::clamp( t, 0.f, 1.f );
        return 1.f + c3 * std::pow( t - 1.f, 3.f ) + c1 * std::pow( t - 1.f, 2.f );
    }

    inline ImU32 lerp_color( ImU32 a, ImU32 b, float t ) {
        t = std::clamp( t, 0.0f, 1.0f );
        const float aa = static_cast<float>( ( a >> 24 ) & 0xFF );
        const float ar = static_cast<float>( ( a >> 16 ) & 0xFF );
        const float ag = static_cast<float>( ( a >> 8  ) & 0xFF );
        const float ab = static_cast<float>( ( a       ) & 0xFF );
        const float ba = static_cast<float>( ( b >> 24 ) & 0xFF );
        const float br = static_cast<float>( ( b >> 16 ) & 0xFF );
        const float bg = static_cast<float>( ( b >> 8  ) & 0xFF );
        const float bb = static_cast<float>( ( b       ) & 0xFF );
        return IM_COL32(
            static_cast<int>( ar + ( br - ar ) * t ),
            static_cast<int>( ag + ( bg - ag ) * t ),
            static_cast<int>( ab + ( bb - ab ) * t ),
            static_cast<int>( aa + ( ba - aa ) * t ) );
    }

}
