#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif
#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif
#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "light_utils.hlsl"

struct MaterialData {
    float4 diffuse_albedo;
    float3 fresnel_r0;
    float roughness;
    float4x4 mat_transform;

    uint diffuse_map_index;
    uint normal_map_index;
    uint mat_pad1;
    uint mat_pad2;
};

TextureCube g_cubemap : register(t0);

Texture2D g_tex_maps[10] : register(t1);

StructuredBuffer<MaterialData> g_mat_data : register(t0, space1);

SamplerState g_sam_point_wrap : register(s0);
SamplerState g_sam_point_clamp : register(s1);
SamplerState g_sam_linear_wrap : register(s2);
SamplerState g_sam_linear_clamp : register(s3);
SamplerState g_sam_anisotropic_wrap : register(s4);
SamplerState g_sam_anisotropic_clamp : register(s5);

cbuffer PerObjBuffer : register(b0) {
    float4x4 g_world;
    float4x4 g_tex_transform;
    uint g_mat_index;
    uint obj_pad0;
    uint obj_pad1;
    uint obj_pad2;
}

cbuffer PerPassConstantBuffer : register(b1){
    float4x4 g_view;
    float4x4 g_inv_view;
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_view_proj;
    float4x4 g_inv_view_proj;
    float3 g_eye_pos_w;
    float cb_per_obj_padding1;
    float2 g_render_target_size;
    float2 g_inv_render_target_size;
    float g_near_z;
    float g_far_z;
    float g_total_time;
    float g_delta_time;
    float4 g_ambient_light;
    
    // Allow application to change fog parameters once per frame.
    // For example, we may only use fog for certain times of day.
    float4 g_fog_color;
    float g_fog_start;
    float g_fog_range;
    float2 cb_per_obj_padding2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MAX_LIGHTS per object.
    Light g_lights[MAX_LIGHTS];
}

//
// Transform a normal map sample to world space
// suffix:
// _w for world space
// _t for tangent space
//
float3
normal_sample_to_world_space (float3 nmap_sample, float3 unit_normal_w, float3 tangent_w) {
    // uncompress each component from [0, 1] to [-1, 1]
    float3 normal_t = 2.0f * nmap_sample - 1.0f;

    // build orthonormal TBN basis
    float3 N = unit_normal_w;
    float3 T = normalize(tangent_w - dot(tangent_w, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    // transform from tangent space to world space
    float3 bumped_normal_w = mul(normal_t, TBN);

    return bumped_normal_w;
}
