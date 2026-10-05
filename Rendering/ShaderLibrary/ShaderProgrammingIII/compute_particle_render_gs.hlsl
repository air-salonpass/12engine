#include "compute_particle.hlsli"
StructuredBuffer<particle_data> particle_data_buffer : register(t0);
StructuredBuffer<particle_header> particle_header_buffer : register(t1);
[maxvertexcount(4)]
void main(point GS_IN input[1],inout TriangleStream<PS_IN> stream)
{
    // Unit13: ソート済みヘッダー経由で実データを参照し、常にカメラを向く板ポリゴンを作る。
    particle_header h=particle_header_buffer[input[0].vertex_id]; if(h.alive==0)return;
    particle_data d=particle_data_buffer[h.particle_index];
    float3 right=normalize(inverse_view_transform[0].xyz)*d.scale.x;
    float3 up=normalize(inverse_view_transform[1].xyz)*d.scale.y;
    static const float2 corner[4]={float2(-.5,-.5),float2(.5,-.5),float2(-.5,.5),float2(.5,.5)};
    static const float2 uv[4]={float2(0,1),float2(1,1),float2(0,0),float2(1,0)};
    [unroll] for(int i=0;i<4;++i){PS_IN o;float3 p=d.position.xyz+right*corner[i].x+up*corner[i].y;
        o.position=mul(float4(p,1),view_projection_transform);o.color=d.color;o.texcoord=d.texcoord.xy+d.texcoord.zw*uv[i];stream.Append(o);} stream.RestartStrip();
}
