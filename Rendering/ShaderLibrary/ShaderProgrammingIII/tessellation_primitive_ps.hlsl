#include "tessellation_primitive.hlsli"
// Unit01: 分割後の面をマテリアル色で表示する。
float4 main(DS_OUT pin) : SV_TARGET { return pin.color; }
