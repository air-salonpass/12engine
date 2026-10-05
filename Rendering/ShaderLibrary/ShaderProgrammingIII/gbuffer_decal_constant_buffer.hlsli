// Unit04: デカール立方体の逆行列でワールド座標をローカル投影座標へ戻す。
cbuffer GBUFFER_DECAL_CONSTANT_BUFFER : register(b2)
{
    row_major float4x4 decal_inverse_world;
    float4 decal_color;
};
