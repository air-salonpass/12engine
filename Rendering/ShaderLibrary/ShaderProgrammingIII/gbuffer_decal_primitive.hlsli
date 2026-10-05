#include "geometric_primitive_object_data.hlsli"
#include "scene_constant_buffer.hlsli"
#include "gbuffer_decal_constant_buffer.hlsli"
struct DECAL_PS_IN { float4 position : SV_POSITION; float4 clip_position : TEXCOORD0; };
