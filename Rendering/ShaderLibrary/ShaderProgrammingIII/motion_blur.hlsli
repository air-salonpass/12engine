#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"

// Unit09: 速度タイルの一辺。二段階最大化で広いブラーを少ない参照回数にする。
static const int TileSize = 20;
float2 decode_velocity(float2 encoded_velocity) { return encoded_velocity; }
