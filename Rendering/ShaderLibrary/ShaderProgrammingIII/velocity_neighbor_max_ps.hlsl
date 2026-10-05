#include "motion_blur.hlsli"
Texture2D<float4> tile_max_velocity : register(t1);
static const int2 offsets[9] = { int2(-1,-1),int2(0,-1),int2(1,-1),int2(-1,0),int2(0,0),int2(1,0),int2(-1,1),int2(0,1),int2(1,1) };
float4 main(VS_OUT pin) : SV_TARGET
{
    // Unit09: 隣接タイルまで含めた最大速度を求め、タイル境界の途切れを防ぐ。
    float2 best=0; int2 p=int2(pin.position.xy);
    [unroll] for(int i=0;i<9;++i){ float2 v=tile_max_velocity.Load(int3(p+offsets[i],0)).xy; if(dot(v,v)>dot(best,best)) best=v; }
    return float4(best,0,1);
}
