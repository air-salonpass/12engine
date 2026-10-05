#include "compute_particle.hlsli"
// Unit12: 頂点番号だけをGSへ渡し、実際の四角形はGPU上で生成する。
GS_IN main(uint id:SV_VertexID){GS_IN o;o.vertex_id=id;return o;}
