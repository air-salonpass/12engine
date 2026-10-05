#include "compute_particle.hlsli"
Texture2D color_map:register(t0); SamplerState sampler_states[3]:register(s0);
// Unit12: アトラスから粒子画像を読み、粒子色・透明度を乗算する。
float4 main(PS_IN pin):SV_TARGET{return color_map.Sample(sampler_states[2],pin.texcoord)*pin.color;}
