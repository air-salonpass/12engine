#ifndef __DEFERRED_RENDERING_HLSLI__
#define __DEFERRED_RENDERING_HLSLI__
#include "sprite.hlsli"
#include "lights.hlsli"
#include "scene_constant_buffer.hlsli"
#include "gbuffer.hlsli"
#include "shading_functions.hlsli"
#include "shading_models.hlsli"

static const int light_kind_directional_light = 0;
static const int light_kind_point_light = 1;
static const int light_kind_spot_light = 2;
static const int light_kind_ambient_light = 3;

struct IntegrateLightData
{
    //  共有の情報
    //  work_data[3].w は共有でライトの種別番号を入れておく

    //  平行光源の場合
    //  work_data[0] = direction
    //  work_data[1] = color
    //  work_data[2] = dummy
    //  work_data[3] = xyz = dummy, w = ライト種別

    //  点光源の場合
    //  work_data[0] = position
    //  work_data[1] = color
    //  work_data[2] = x = range, yzw = dummy
    //  work_data[3] = xyz = dummy, w = ライト種別

    //  スポットライトの場合
    //  work_data[0] = position
    //  work_data[1] = direction
    //  work_data[2] = color
    //  work_data[3] = x = range, y = inner cone, z = outer cone, w = ライト種別
    
    //  環境光の場合
    //  work_data[0] = color
    //  work_data[1] = dummy
    //  work_data[2] = dummy
    //  work_data[3] = xyz = dummy, w = ライト種別
    
    float4 work_data[4];
};

cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    //  ライト情報
    IntegrateLightData light_data;

    //  シャドウマップ関係
    int use_shadow; //  影を使用しているかどうか
    float shadow_attenuation; //  影色
    float shadow_bias; //  深度バイアス
    uint shadow_dummy;
    row_major float4x4 light_view_projection; //  ライトの位置から見た射影行列
};

//  ライト種別の取得
int get_light_kinds()
{
    return (int) (light_data.work_data[3].w + 0.5f);
}

directional_lights convert_directional_lights()
{
    directional_lights data;
    data.direction = light_data.work_data[0];
    data.color = light_data.work_data[1];
    return data;
}

point_lights convert_point_lights()
{
    point_lights data;
    data.position = light_data.work_data[0];
    data.color = light_data.work_data[1];
    data.range = light_data.work_data[2].x;
    return data;
}

spot_lights convert_spot_lights()
{
    spot_lights data;
    data.position = light_data.work_data[0];
    data.direction = light_data.work_data[1];
    data.color = light_data.work_data[2];
    data.range = light_data.work_data[3].x;
    data.inner_corn = light_data.work_data[3].y;
    data.outer_corn = light_data.work_data[3].z;
    return data;
}

float3 convert_ambient_lights()
{
    return light_data.work_data[0].xyz * light_data.work_data[0].w;
}

#endif  //  __DEFERRED_RENDERING_HLSLI__
