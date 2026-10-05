#include "summed_area_table.hlsli"
// Unit11: Xパスの結果に対して各列の累積和を取り、2次元SATを完成させる。
[numthreads(LENGTH,1,1)] void main(uint3 position:SV_DispatchThreadID){ build_sat(position,true); }
