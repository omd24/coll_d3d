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
    uint mat_pad0;
    uint mat_pad1;
    uint mat_pad2;
};

TextureCube g_cubemap : register(t0);

Texture2D g_diffuse_maps[4] : register(t1);

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
