#include "FullscreenQuad.hlsli"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

float4 main(VS_OUT pin) : SV_TARGET {
	return tex.Sample(samp, pin.texcoord);
}
