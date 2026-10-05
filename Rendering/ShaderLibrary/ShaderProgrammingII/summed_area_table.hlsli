//  下の定義をインクルード前に設定しておくことでスレッドの数を設定
//  #define LENGTH

//  下の定義をインクルード前に設定しておくことでコンパイル方式を設定
//  #define PASS_X
//  #define PASS_Y


//  読み込んだテクスチャのぼかしテーブルを作成、
//  使用してぼかしてみる
#define	UNIT_15_SUB_04

//  ↑のままだと激重なので、共有メモリを利用して
//  テクスチャへのサンプリングを1回だけにして高速化
#define	UNIT_15_SUB_05

//  それでも重いので計算済み情報を取得するようにして再度高速化
#define	UNIT_15_SUB_06

Texture2D<float4> source_texture : register(t0);
RWTexture2D<float4> destination_texture : register(u0);

cbuffer SUMMED_AREA_TABLE_CONSTANT_BUFFER : register(b0)
{
    float2 pixel_size;
    float2 dummy;
};

#if defined(UNIT_15_SUB_05)
groupshared float3 temp_buffer[LENGTH];
#endif  //  defined(UNIT_15_SUB_05)

[numthreads(LENGTH, 1, 1)]
void main(uint3 position : SV_DispatchThreadID)
{
    const uint buffer_id = position.x;
    
#if defined(PASS_X)
    // 水平パス
    const uint2 texture_position = position.xy;
    const float2 texel_size = pixel_size.xy;
#endif
#if defined(PASS_Y)
    // 垂直パス
    const uint2 texture_position = position.yx;
    const float2 texel_size = pixel_size.yx;
#endif
    
#if defined(UNIT_15_SUB_05)
    
    //  テクスチャから色情報を1度だけ読み込んで共有メモリに保存
    float2 sample_position = float2(texture_position) * texel_size;
    temp_buffer[buffer_id] = source_texture.Load(int3(sample_position, 0)).rgb;

#if defined(UNIT_15_SUB_06)
    //  計算済みのバッファを確認していくことで計算量を削減する
    [unroll]
    for (int i = 0; i < log2(LENGTH); ++i)
    {
        //  計算待機
        GroupMemoryBarrierWithGroupSync();

        //  2べき乗飛ばしで再帰的に加算していく事で計算回数を削減する
        const uint delta = (0x1 << i);
        const float3 buffer = (buffer_id >= delta) ? (temp_buffer[buffer_id - delta]) : (0.0f);

        //  バッファ取得/操作するので待機
        GroupMemoryBarrierWithGroupSync();
        temp_buffer[buffer_id] += buffer;
    }
    
    destination_texture[texture_position] = float4(temp_buffer[buffer_id], 1.0f);
#else
    //  スレッドグループが共有メモリに対して色情報を保存しきるまで待機
    GroupMemoryBarrierWithGroupSync();
    
    //  計算済みのバッファを確認していくことで計算量を削減する
    float3 temp = temp_buffer[buffer_id].rgb;
    for (uint i = 0; i < buffer_id; ++i)
    {
        temp += temp_buffer[i].rgb;
    }
    destination_texture[texture_position] = float4(temp, 1.0f);
#endif

#else   //  defined(UNIT_15_SUB_05)
    
    //  今の配列までの数値を単純に加算させる
    float3 temp = 0;
    for (uint i = 0; i <= buffer_id; ++i)
    {
#if defined(PASS_X)
        int3 sample_position = int3((float)i * texel_size.x, position.y * texel_size.y, 0);
#endif
#if defined(PASS_Y)
        int3 sample_position = int3(position.y * texel_size.x, (float)i * texel_size.y, 0);
#endif
        temp += source_texture.Load(sample_position).rgb;
    }

    destination_texture[texture_position] = float4(temp, 1.0f);
    
#endif  //  defined(UNIT_15_SUB_05)
}
