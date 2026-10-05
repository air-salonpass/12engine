#include "terrain_primitive.hlsli"
// Unit02発展: 地形三角形をそのまま後段へ渡す。草生成を追加する際の拡張点にもなる。
[maxvertexcount(3)]
void main(triangle DS_OUT input[3], inout TriangleStream<DS_OUT> stream)
{ stream.Append(input[0]); stream.Append(input[1]); stream.Append(input[2]); stream.RestartStrip(); }
