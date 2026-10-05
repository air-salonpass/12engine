#include "sky_box.hlsli"

TextureCube specular_pmrem : register(t0);
SamplerState samplerstate : register(s0);

PSGBufferOut main(VS_OUT pin)
{
    //	視線ベクトル
    float3 E = normalize(pin.world_position.xyz - camera_position.xyz);

	//	mipmap使うと崩れるのでmipmapなしになるようにしておく
    float3 base_color = specular_pmrem.SampleLevel(samplerstate, E, 0) * pin.color;

    //  GBufferDataに出力情報をまとめる
    GBufferData data = (GBufferData)0;
    data.shading_model = shading_model_unlit;
    data.emissive_color = base_color.rgb;
    data.w_position = pin.world_position.xyz;
    data.velocity = calculate_uvspace_velocity(pin.current_clip_position, pin.previous_clip_position);
    return EncodeGBuffer(data, view_projection_transform, z_buffer_parameteres);
}
