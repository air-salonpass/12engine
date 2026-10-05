#include "summed_area_table.hlsli"
// Unit11: 各行の累積和。
[numthreads(LENGTH,1,1)] void main(uint3 position:SV_DispatchThreadID){ build_sat(position,false); }
