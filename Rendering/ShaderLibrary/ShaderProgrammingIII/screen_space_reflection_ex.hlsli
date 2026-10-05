#ifndef __SCREEN_SPACE_REFLECTION_EX_HLSLI__
#define __SCREEN_SPACE_REFLECTION_EX_HLSLI__

cbuffer SCREEN_SPACE_REFLECTION_CONSTANT_BUFFER : register(b2)
{
    float screen_space_reflection_max_distance;     //  反射最大距離
    float screen_space_reflection_ray_resolution;   //  レイのステップ解像度
    float screen_space_reflection_tickness;         //  深度判定を行う際の厚み
    int screen_space_reflection_flags;              //  処理の設定フラグ
};

static const int screen_space_reflection_flags_consideration_metalness = 0;
static const int screen_space_reflection_flags_reflection_dir_adjust = 1;
static const int screen_space_reflection_flags_consideration_uv_hole = 2;
static const int screen_space_reflection_flags_consideration_rougness = 3;
static const int screen_space_reflection_flags_z_check_subdivision = 4;

bool is_flag(int bitflag)
{
    return screen_space_reflection_flags & (1 << bitflag);
}

#endif  //  __SCREEN_SPACE_REFLECTION_EX_HLSLI__
