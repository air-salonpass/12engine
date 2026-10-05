Texture2D<float4> source_texture : register(t0);
RWTexture2D<float4> destination_texture : register(u0);
cbuffer SUMMED_AREA_TABLE_CONSTANT_BUFFER : register(b0) { float2 pixel_size; float2 sat_dummy; };
static const uint LENGTH=512;
groupshared float3 scan_buffer[LENGTH];

void build_sat(uint3 position, bool vertical)
{
    uint index=position.x;
    uint2 output_position = vertical ? position.yx : position.xy;
    // Unit11: 元画像が512x512以外でも、pixel_size間隔で均等に縮小して読み込む。
    uint2 sample_position = uint2(float2(output_position) * pixel_size);
    scan_buffer[index] = source_texture.Load(int3(sample_position,0)).rgb;
    // Unit11: Hillis-Steele法。共有メモリと同期を使い、行/列の累積和を並列生成する。
    [unroll] for(uint offset=1;offset<LENGTH;offset<<=1)
    {
        GroupMemoryBarrierWithGroupSync();
        float3 addend = index>=offset ? scan_buffer[index-offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        scan_buffer[index]+=addend;
    }
    destination_texture[output_position]=float4(scan_buffer[index],1);
}
