#include "Phong.hlsli"
cbuffer CbMesh : register(b0) {
	float4				materialColor;
};
Texture2D DiffuseMap		: register(t0);
Texture2D NormalMap		    : register(t1); //< ココを追加
SamplerState LinearSampler	: register(s0);

//	ランバートの拡散反射
float3 CalcLambert(float3 N, float3 L,
                    float3 C, float3 K) {
    float power = saturate(dot(N, -L));
    return C * power * K;
}
//	フォンの鏡面反射
float3 CalcPhongSpecular(float3 N, float3 L, float3 E,
                         float3 C, float3 K) {
    float3 R = reflect(L, N);
    float power = max(dot(-E, R), 0);
    power = pow(power, 128);
    return C * power * K;
}
// 半球ライティングの関数をシェーダーⅠから持ってくる
float3 CalcHemiSphereLight(float3 normal, float3 up, float3 sky_color, float3 ground_color, float4 hemisphere_weight){
    float factor = dot(normal, up) * 0.5f + 0.5f;
    return lerp(ground_color, sky_color, factor) * hemisphere_weight.x;
}
float4 main(VS_OUT pin) : SV_TARGET
{
	float4 color = DiffuseMap.Sample(LinearSampler, pin.texcoord) * materialColor;
	float3 N = normalize(pin.normal);
    //  接線/従法線算出
    //  tangent.xyz = 接線
    //  tangent.w = 従法線の向き
    float3 T = normalize(pin.tangent.xyz);
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * pin.tangent.w);
    {
        float4 sampled = NormalMap.Sample(LinearSampler, pin.texcoord);
        float3 normaFactor = sampled.xyz * 2.0f - 1.0f;
        N = normalize((normaFactor.x * T)
            + (normaFactor.y * B) + (normaFactor.z * N));
    }
    
	float3 L = normalize(lightDirection.xyz);
    float3 LC = lightColor.rgb;
    float3 E = normalize(pin.position.xyz - cameraPosition.xyz);
    float3 totalDiffuse = 0;	//拡散反射
    float3 totalSpecular = 0;	//鏡面反射
	//	平行光源の計算
    totalDiffuse += CalcLambert(N, L, LC, 1);
    totalSpecular += CalcPhongSpecular(N, L, E, LC, 1);
    //  環境光
    //  半球ライティング
    float3 skyColor = float3(0, 0, 1);
    float3 groundColor = float3(0, 1, 0);
    float hemiSphereWeight = 0.3f;
    float3 skyDirection = float3(0, 1, 0);
    totalDiffuse += CalcHemiSphereLight(N, skyDirection, skyColor,
                                groundColor, hemiSphereWeight);

    color.rgb *= totalDiffuse;
    color.rgb += totalSpecular;

	return color;
}
