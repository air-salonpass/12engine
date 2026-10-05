// Unit08: SSRのレイマーチ条件と合成量。
cbuffer SCREEN_SPACE_REFLECTION_CONSTANT_BUFFER : register(b2)
{
    float ssr_max_distance;
    int ssr_iteration;
    float ssr_thickness;
    int ssr_flags;
};
