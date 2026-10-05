#ifndef GAUSSIAN_FILTERING_HLSLI
#define GAUSSIAN_FILTERING_HLSLI

#include "sprite.hlsli"

// Unit02-01: 最大25x25のガウシアンカーネルをCPU側で計算して受け取る。
static const int KernelMax = 25;
cbuffer GAUSSIAN_FILTER : register(b2)
{
    float4 weights[KernelMax * KernelMax];
    float kernel_size;
    float2 texel_size;
    float gaussian_padding;
};

#endif
