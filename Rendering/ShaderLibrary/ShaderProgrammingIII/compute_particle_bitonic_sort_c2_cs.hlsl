#include "compute_particle.hlsli"
#include "compute_particle_bitonic_sort.hlsli"
groupshared particle_header shared_data[BitonicSortC2Thread*2];
[numthreads(BitonicSortC2Thread,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    uint t=id.x,mask=BitonicSortC2Thread*2-1;
    for(uint inc=increment;inc>0;inc>>=1)
    {
        uint low=t&(inc-1),i=(t<<1)-low; bool reverse=((direction&i)==0); particle_header a,b;
        if(inc==increment){a=particle_header_buffer[i];b=particle_header_buffer[i+inc];}
        else{GroupMemoryBarrierWithGroupSync();a=shared_data[i&mask];b=shared_data[(i+inc)&mask];}
        if(reverse^particle_before(a,b)){particle_header tmp=a;a=b;b=tmp;}
        if(inc==1){particle_header_buffer[i]=a;particle_header_buffer[i+inc]=b;}
        else{GroupMemoryBarrierWithGroupSync();shared_data[i&mask]=a;shared_data[(i+inc)&mask]=b;}
    }
}
