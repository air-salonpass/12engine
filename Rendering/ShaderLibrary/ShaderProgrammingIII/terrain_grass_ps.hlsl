#include "terrain_primitive.hlsli"
// Unit02: ジオメトリシェーダーで生成する草用の半透明緑色。
float4 main(PS_IN pin) : SV_TARGET { return float4(0.15f, 0.55f, 0.12f, 1.0f); }
