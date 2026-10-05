#include "compute_particle.hlsli"
RWStructuredBuffer<particle_data> particle_data_buffer : register(u0);
AppendStructuredBuffer<uint> particle_unused_buffer : register(u1);
RWByteAddressBuffer indirect_data_buffer : register(u2);
RWStructuredBuffer<particle_header> particle_header_buffer : register(u3);
[numthreads(NumParticleThread,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(id.x>=system_num_particles) return;
    particle_header h=particle_header_buffer[id.x]; if(h.alive==0) return;
    particle_data d=particle_data_buffer[h.particle_index]; d.parameter.y-=elapsed_time;
    if(d.parameter.y<0)
    {
        // Unit12: 寿命切れ番号をAppendで再利用可能にし、Unit13: ヘッダーだけを死亡にする。
        h.alive=0; particle_header_buffer[id.x]=h; particle_unused_buffer.Append(h.particle_index);
        indirect_data_buffer.InterlockedAdd(IndirectArgumentsNumDeadParticle,1); return;
    }
    d.velocity.xyz+=d.acceleration.xyz*elapsed_time; d.position.xyz+=d.velocity.xyz*elapsed_time;
    uint type=(uint)(d.parameter.x+0.5f); float2 cell=rcp(float2(texture_split_count));
    d.texcoord=float4(float2(type%texture_split_count.x,type/texture_split_count.x)*cell,cell);
    d.color.a=saturate(d.parameter.y);
    // Unit13: 射影後のwを奥行きキーとして記録し、後段で奥から手前へ並べる。
    h.depth=mul(float4(d.position.xyz,1),view_projection_transform).w;
    particle_header_buffer[id.x]=h; particle_data_buffer[h.particle_index]=d;
}
