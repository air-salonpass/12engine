#include "scene_constant_buffer.hlsli"

// Unit12/13: CPUとGPUで同じ16バイト境界の構造を使用する。
static const uint NumParticleThread=1024;
struct emit_particle_data { float4 parameter; float4 position; float4 rotation; float4 scale; float4 velocity; float4 acceleration; float4 color; };
struct particle_data { float4 parameter; float4 position; float4 rotation; float4 scale; float4 velocity; float4 acceleration; float4 texcoord; float4 color; };
struct particle_header { uint alive; uint particle_index; float depth; uint dummy; };
struct GS_IN { uint vertex_id : VERTEX_ID; };
struct PS_IN { float4 position : SV_POSITION; float4 color : COLOR; float2 texcoord : TEXCOORD; };

cbuffer COMPUTE_PARTICLE_COMMON_CONSTANT_BUFFER : register(b10)
{
    float elapsed_time;
    uint2 texture_split_count;
    uint system_num_particles;
    uint total_emit_count;
    uint3 common_dummy;
};
cbuffer COMPUTE_PARTICLE_BITONIC_SORT_CONSTANT_BUFFER : register(b11)
{ uint increment; uint direction; uint2 sort_dummy; };

static const uint IndirectArgumentsNumCurrentParticle=0;
static const uint IndirectArgumentsNumPreviousParticle=4;
static const uint IndirectArgumentsNumDeadParticle=8;
static const uint IndirectArgumentsEmitParticleDispatchIndirect=12;
static const uint IndirectArgumentsUpdateParticleDispatchIndirect=24;
static const uint IndirectArgumentsNumEmitParticleIndex=36;
static const uint IndirectArgumentsDrawIndirect=40;
static const uint BitonicSortB2Thread=256;
static const uint BitonicSortC2Thread=512;
