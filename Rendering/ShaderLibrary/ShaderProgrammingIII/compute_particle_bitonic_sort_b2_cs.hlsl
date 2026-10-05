#include "compute_particle.hlsli"
#include "compute_particle_bitonic_sort.hlsli"
[numthreads(BitonicSortB2Thread,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    uint t=id.x,low=t&(increment-1),i=(t<<1)-low; bool reverse=((direction&i)==0);
    particle_header a=particle_header_buffer[i],b=particle_header_buffer[i+increment];
    if(reverse^particle_before(a,b)){particle_header tmp=a;a=b;b=tmp;}
    particle_header_buffer[i]=a; particle_header_buffer[i+increment]=b;
}
