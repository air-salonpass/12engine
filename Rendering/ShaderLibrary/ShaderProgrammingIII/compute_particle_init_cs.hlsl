#include "compute_particle.hlsli"
RWStructuredBuffer<particle_data> particle_data_buffer : register(u0);
AppendStructuredBuffer<uint> particle_unused_buffer : register(u1);
RWStructuredBuffer<particle_header> particle_header_buffer : register(u3);
[numthreads(NumParticleThread,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(id.x>=system_num_particles) return;
    // Unit12: 全番号を未使用スタックへ積み、Unit13のヘッダーも死亡状態にする。
    particle_data_buffer[id.x].parameter=float4(0,0,-1,0);
    particle_header_buffer[id.x]=(particle_header)0;
    particle_unused_buffer.Append(id.x);
}
