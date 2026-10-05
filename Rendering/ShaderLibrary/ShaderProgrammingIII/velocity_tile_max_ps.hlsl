#include "motion_blur.hlsli"
Texture2D<float4> gbuffer_velocity : register(t5);
SamplerState point_sampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    // Unit09: 各20x20タイルから最も長い速度ベクトルを選ぶ。
    float2 best = 0;
    int2 origin = int2(pin.position.xy) * TileSize;
    [loop] for(int y=0;y<TileSize;++y) [loop] for(int x=0;x<TileSize;++x)
    {
        float2 v = gbuffer_velocity.Load(int3(origin + int2(x,y),0)).xy;
        if(dot(v,v)>dot(best,best)) best=v;
    }
    return float4(best,0,1);
}
