#include "gltf_model_forward.hlsli"

struct GS_OUT
{
    VS_OUT vs;
    bool is_outline : IS_OUTLINE; // —ÖŠsü”»’è—p
};

cbuffer OUTLINE_CONSTANT_BUFFER : register(b4)
{
    float outline_size; //  —ÖŠsü’²®
    float3 outline_dummy;
};
