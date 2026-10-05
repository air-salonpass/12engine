#include "compute_particle.hlsli"
RWStructuredBuffer<particle_header> particle_header_buffer : register(u3);
RWByteAddressBuffer indirect_data_buffer : register(u2);
[numthreads(1,1,1)]
void main()
{
    // Unit13導入版: 生存ヘッダーを前方へ集約する。最終版では下の並列バイトニックソートを使用する。
    uint count=indirect_data_buffer.Load(IndirectArgumentsNumCurrentParticle);
    for(uint i=1;i<count;++i){ particle_header key=particle_header_buffer[i]; int j=i-1;
        while(j>=0 && (particle_header_buffer[j].alive<key.alive)){particle_header_buffer[j+1]=particle_header_buffer[j];--j;}
        particle_header_buffer[j+1]=key; }
}
