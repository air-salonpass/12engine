// C++側のGltfRendererで作るRoot SignatureとBinding番号を共有する。
// registerを変更するときはHLSLだけでなく、Root ParameterとDescriptor Tableも同時に直すこと。
// 1 Meshletは最大64頂点・126三角形。CPU側のMeshlet生成条件と配列上限を一致させる。
cbuffer SceneConstants : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 view;
    row_major float4x4 projection;
    row_major float4x4 inverse_view;
    float4 base_color_factor;
    float4 emissive_metallic;
    float4 roughness_flags;
    uint skinned;
    float3 scene_padding;
    float4 environment_parameters;
    float4 light_direction;
};

cbuffer JointConstants : register(b1) { row_major float4x4 joint_matrices[256]; };

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
    uint4 joints;
    float4 weights;
};

struct Meshlet
{
    uint vertex_offset;
    uint vertex_count;
    uint triangle_offset;
    uint triangle_count;
};

StructuredBuffer<Vertex> vertices : register(t8);
StructuredBuffer<Meshlet> meshlets : register(t9);
StructuredBuffer<uint> meshlet_vertex_indices : register(t10);
StructuredBuffer<uint> meshlet_triangles : register(t11);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float tangent_sign : TANGENT1;
    float2 uv : TEXCOORD;
    float3 view_position : TEXCOORD1;
};

uint3 unpack_triangle(uint packed)
{
    return uint3(packed & 0x3ff, (packed >> 10) & 0x3ff, (packed >> 20) & 0x3ff);
}

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint thread_id : SV_GroupThreadID, uint group_id : SV_GroupID,
    out vertices VSOutput output_vertices[64], out indices uint3 output_triangles[126])
{
    Meshlet meshlet = meshlets[group_id];
    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);
    if (thread_id < meshlet.triangle_count)
        output_triangles[thread_id] = unpack_triangle(meshlet_triangles[meshlet.triangle_offset + thread_id]);
    if (thread_id >= meshlet.vertex_count) return;

    Vertex input = vertices[meshlet_vertex_indices[meshlet.vertex_offset + thread_id]];
    float4 position = float4(input.position, 1.0);
    float3 normal = input.normal;
    float3 tangent = input.tangent.xyz;
    if (skinned != 0)
    {
        float4x4 skin = joint_matrices[input.joints.x] * input.weights.x;
        skin += joint_matrices[input.joints.y] * input.weights.y;
        skin += joint_matrices[input.joints.z] * input.weights.z;
        skin += joint_matrices[input.joints.w] * input.weights.w;
        position = mul(position, skin);
        normal = mul(normal, (float3x3)skin);
        tangent = mul(tangent, (float3x3)skin);
    }

    float4 world_position = mul(position, world);
    float4 view_position = mul(world_position, view);
    VSOutput output;
    output.position = mul(view_position, projection);
    output.normal = normalize(mul(mul(normal, (float3x3)world), (float3x3)view));
    output.tangent = normalize(mul(mul(tangent, (float3x3)world), (float3x3)view));
    output.tangent_sign = input.tangent.w;
    output.uv = input.texcoord;
    output.view_position = view_position.xyz;
    output_vertices[thread_id] = output;
}
