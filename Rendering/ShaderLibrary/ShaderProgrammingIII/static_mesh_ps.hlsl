#include "static_mesh.hlsli"

Texture2D color_map : register(t0);
SamplerState color_sampler_state : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
	return color_map.Sample(color_sampler_state, pin.texcoord) * pin.color;
}

