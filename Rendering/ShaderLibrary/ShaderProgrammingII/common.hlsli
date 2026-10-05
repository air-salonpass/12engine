#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

float2 convert_ndcspaceposition_xy_to_uv(float2 ndc)
{
    return float2(ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f));
}

float2 convert_uv_to_ndcspaceposition_xy(float2 uv)
{
    return float2(uv.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f));
}
#endif  //  __COMMON_HLSLI__
