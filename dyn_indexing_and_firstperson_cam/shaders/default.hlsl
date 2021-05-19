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

// array of textures
// unlike Texture2DArray, the textures can be different sizes and formats
// 8 is the number of textures (4 + 3elements of texture array)
Texture2D global_diffuse_maps[7] : register(t0);

// use space1 to avoid overlap wth texture array (in space0)
StructuredBuffer<MaterialData> global_mat_data : register(t0, space1);

// waves displacement map
// NOTE(omid): The 5 elements of texture array occupy registers t0, t1, t2, t3 and t4, t5, t6
Texture2D global_displacement_map : register(t7);

SamplerState global_sam_point_wrap : register(s0);
SamplerState global_sam_point_clamp : register(s1);
SamplerState global_sam_linear_wrap : register(s2);
SamplerState global_sam_linear_clamp : register(s3);
SamplerState global_sam_anisotropic_wrap : register(s4);
SamplerState global_sam_anisotropic_clamp : register(s5);

cbuffer PerObjectConstantBuffer : register(b0){
    float4x4 global_world;
    float4x4 global_tex_transform;
    
    float2 global_displacement_map_texel_size;
    float global_grid_spatial_step;
    float cb_per_obj_pad1;

    uint global_mat_index;
    uint obj_pad0;
    uint obj_pad1;
    uint obj_pad2;
}
cbuffer PerPassConstantBuffer : register(b1){
    float4x4 global_view;
    float4x4 global_inv_view;
    float4x4 global_proj;
    float4x4 global_inv_proj;
    float4x4 global_view_proj;
    float4x4 global_inv_view_proj;
    float3 global_eye_pos_w;
    float cb_per_obj_padding1;
    float2 global_render_target_size;
    float2 global_inv_render_target_size;
    float global_near_z;
    float global_far_z;
    float global_total_time;
    float global_delta_time;
    float4 global_ambient_light;
    
    // Allow application to change fog parameters once per frame.
    // For example, we may only use fog for certain times of day.
    float4 global_fog_color;
    float global_fog_start;
    float global_fog_range;
    float2 cb_per_obj_padding2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MAX_LIGHTS per object.
    Light global_lights[MAX_LIGHTS];
}

struct VertexShaderInput {
    float3 pos_local : POSITION;
    float3 normal_local : NORMAL;
    float2 texc : TEXCOORD;
};
struct VertexShaderOutput {
    float4 pos_homogenous_clip_space : SV_Position;
    float3 pos_world : Position;
    float3 normal_world : NORMAL;
    float2 texc : TEXCOORD;
};
VertexShaderOutput
VertexShader_Main (VertexShaderInput vin) {
    VertexShaderOutput result = (VertexShaderOutput)0.0f;

#ifdef DISPLACEMENT_MAP
    // sampler the displacement map using non-transformed [0,1]^2 texture-coords
    vin.pos_local.y += global_displacement_map.SampleLevel(global_sam_linear_wrap, vin.texc, 1.0f).r;
    
    // estimate normal using finite difference
    float du = global_displacement_map_texel_size.x;
    float dv = global_displacement_map_texel_size.y;
    float l = global_displacement_map.SampleLevel(global_sam_point_clamp, vin.texc - float2(du, 0.0f), 0.0f).r;
    float r = global_displacement_map.SampleLevel(global_sam_point_clamp, vin.texc + float2(du, 0.0f), 0.0f).r;
    float t = global_displacement_map.SampleLevel(global_sam_point_clamp, vin.texc - float2(0.0f, dv), 0.0f).r;
    float b = global_displacement_map.SampleLevel(global_sam_point_clamp, vin.texc + float2(0.0f, dv), 0.0f).r;
    vin.normal_local = normalize(float3(-r + l, 2.0f * global_grid_spatial_step, b - t));
#endif
    
    // fetch material data
    MaterialData mat_data = global_mat_data[global_mat_index];
    
    // transform to world space
    float4 pos_world = mul(float4(vin.pos_local, 1.0f), global_world);
    result.pos_world = pos_world.xyz;
    
    // assuming nonuniform scale (otherwise have to use inverse-transpose of world-matrix)
    result.normal_world = mul(vin.normal_local, (float3x3)global_world);

    // transform to homogenous clip space
    result.pos_homogenous_clip_space = mul(pos_world, global_view_proj);

    // output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.texc, 0.0f, 1.0f), global_tex_transform);
    result.texc = mul(texc, mat_data.mat_transform).xy;

    return result;
}
float4
PixelShader_Main (VertexShaderOutput pin) : SV_Target{
    // fetch material data
    MaterialData mat_data = global_mat_data[global_mat_index];
    
    // extract data
    float4 diffuse_albedo = mat_data.diffuse_albedo;
    uint diffuse_tex_index = mat_data.diffuse_map_index;
    float roughness = mat_data.roughness;
    float3 fresnel_r0 = mat_data.fresnel_r0;
    
    // dynamically look up the texture in the array
    diffuse_albedo *=
        global_diffuse_maps[diffuse_tex_index].Sample(global_sam_anisotropic_wrap, pin.texc);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif
    
    // interpolations of normal can unnormalize it so renormalize!
    pin.normal_world = normalize(pin.normal_world);

    // vector from point being lit "to eye"
    float3 to_eye = global_eye_pos_w - pin.pos_world;
    float dist_to_eye = length(to_eye);
    to_eye /= dist_to_eye; // normalize

    // indirect lighting
    float4 ambient = global_ambient_light * diffuse_albedo;

    const float shininess = 1.0f - roughness;
    Material mat = {diffuse_albedo, fresnel_r0, shininess};
    float3 shadow_factor = 1.0f;
    float4 direct_light = compute_lighting(
        global_lights, mat, pin.pos_world, pin.normal_world, to_eye, shadow_factor
    );
    float4 lit_color = ambient + direct_light;

#ifdef FOG
    float fog_amount_factor = saturate((dist_to_eye - global_fog_start) / global_fog_range);
    lit_color = lerp(lit_color, global_fog_color, fog_amount_factor);
#endif
    
    // common convention to take alpha from diffuse material
    lit_color.a = diffuse_albedo.a;

    return lit_color;
}

