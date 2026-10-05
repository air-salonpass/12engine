#include "compute_particle.hlsli"
RWByteAddressBuffer indirect_data_buffer : register(u2);
[numthreads(1,1,1)]
void main()
{
    // Unit12/13: 死亡数を引いて次フレームの生存数とDrawInstancedIndirect引数を確定する。
    uint dead=indirect_data_buffer.Load(IndirectArgumentsNumDeadParticle);
    uint current=indirect_data_buffer.Load(IndirectArgumentsNumCurrentParticle)-dead;
    indirect_data_buffer.Store(IndirectArgumentsNumCurrentParticle,current);
    indirect_data_buffer.Store(IndirectArgumentsNumDeadParticle,0);
    indirect_data_buffer.Store4(IndirectArgumentsDrawIndirect,uint4(current,1,0,0));
}
