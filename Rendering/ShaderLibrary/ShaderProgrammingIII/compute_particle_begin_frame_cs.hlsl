#include "compute_particle.hlsli"
RWByteAddressBuffer indirect_data_buffer : register(u2);
[numthreads(1,1,1)]
void main()
{
    // Unit12: GPU上の生存数から、生成・更新用Indirect引数を組み立てる。
    uint previous=indirect_data_buffer.Load(IndirectArgumentsNumCurrentParticle);
    uint current=min(system_num_particles,previous+total_emit_count);
    indirect_data_buffer.Store(IndirectArgumentsNumPreviousParticle,previous);
    indirect_data_buffer.Store(IndirectArgumentsNumCurrentParticle,current);
    indirect_data_buffer.Store(IndirectArgumentsNumDeadParticle,0);
    indirect_data_buffer.Store3(IndirectArgumentsEmitParticleDispatchIndirect,uint3(current-previous,1,1));
    indirect_data_buffer.Store(IndirectArgumentsNumEmitParticleIndex,previous);
    indirect_data_buffer.Store3(IndirectArgumentsUpdateParticleDispatchIndirect,uint3((current+NumParticleThread-1)/NumParticleThread,1,1));
}
