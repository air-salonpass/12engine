#include "sprite.hlsli"

Texture2D brush_texture : register(t0);
SamplerState samp_state : register(s0);

Texture2D origin_canvas_data : register(t1);

float4 main(VS_OUT pin) : SV_TARGET
{
    //  Œ³‰æ‘œ
    float2 canvas_position;
    origin_canvas_data.GetDimensions(canvas_position.x, canvas_position.y);
    canvas_position = pin.position.xy / canvas_position;
    float4 origin_color = origin_canvas_data.Sample(samp_state, canvas_position);

    //  ƒuƒ‰ƒV‰æ‘œ
    float alpha = saturate(brush_texture.Sample(samp_state, pin.texcoord).a * pin.color.a);
    clip(alpha - 0.01f);

    //  ‘‚«‚İ‘¬“x¶¬
    float2 velocity = saturate(lerp(origin_color.xy, pin.color.xy, alpha));
    
    return float4(velocity, 0, 1);
}

