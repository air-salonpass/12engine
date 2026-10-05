#include "compute_particle.hlsli"
RWStructuredBuffer<particle_data> particle_data_buffer : register(u0);
ConsumeStructuredBuffer<uint> particle_unused_buffer : register(u1);
RWByteAddressBuffer indirect_data_buffer : register(u2);
RWStructuredBuffer<particle_header> particle_header_buffer : register(u3);
StructuredBuffer<emit_particle_data> emit_particle_buffer : register(t0);
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    // Unit12: 未使用番号をConsumeし、Unit13: 連続したヘッダー末尾へ登録する。
    uint data_index=particle_unused_buffer.Consume(), header_index;
    indirect_data_buffer.InterlockedAdd(IndirectArgumentsNumEmitParticleIndex,1,header_index);
    emit_particle_data src=emit_particle_buffer[id.x];
    particle_data d;
    d.parameter=float4(src.parameter.xy,0,0); d.position=src.position; d.rotation=src.rotation; d.scale=src.scale;
    d.velocity=src.velocity; d.acceleration=src.acceleration; d.texcoord=0; d.color=src.color;
    particle_data_buffer[data_index]=d;
    particle_header h; h.alive=1; h.particle_index=data_index; h.depth=1; h.dummy=0;
    particle_header_buffer[header_index]=h;
}
