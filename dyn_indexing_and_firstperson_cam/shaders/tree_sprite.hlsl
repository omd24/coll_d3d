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

Texture2DArray global_tree_map_array : register(t0);

SamplerState global_sam_point_wrap : register(s0);
SamplerState global_sam_point_clamp : register(s1);
SamplerState global_sam_linear_wrap : register(s2);
SamplerState global_sam_linear_clamp : register(s3);
SamplerState global_sam_anisotropic_wrap : register(s4);
SamplerState global_sam_anisotropic_clamp : register(s5);

cbuffer PerObjectConstantBuffer : register(b0) {
    float4x4 global_world;
    float4x4 global_tex_transform;
}
cbuffer PerPassConstantBuffer : register(b1) {
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
cbuffer MaterialConstantBuffer : register(b2) {
    float4 global_diffuse_albedo;
    float3 global_fresnel_r0;
    float global_roughness;
    float4x4 global_mat_transform;
};

struct VertexShaderInput {
    float3 pos_world : POSITION;
    float2 size_world : SIZE;
};
struct VertexShaderOutput {
    float3 center_world : Position;
    float2 size_world : SIZE;
};
struct GeoShaderOutput {
    float4 pos_homogenous : SV_Position;
    float3 pos_world : POSITION;
    float3 normal_world : NORMAL;
    float2 texc : TEXCOORD;
    uint primt_Id : SV_PrimitiveID;
};
VertexShaderOutput
VS_Main (VertexShaderInput vin) {
    VertexShaderOutput result;

    // just pass data over geo shader
    result.center_world = vin.pos_world;
    result.size_world = vin.size_world;

    return result;
}
[maxvertexcount(4)]
void
GS_Main (
    point VertexShaderOutput gin[1],
    uint prim_id : SV_PrimitiveID,
    inout TriangleStream<GeoShaderOutput> triangle_stream
) {
    // compute local coord sys of sprite rel to world space
    // billboard facing eye and aligned with y-axis
    
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = global_eye_pos_w - gin[0].center_world;
    look.y = 0.0f;
    look = normalize(look);
    float3 right = cross(up, look);
    
    // compute triangle strip vertices in world space
    float half_width = 0.5f * gin[0].size_world.x;
    float half_height = 0.5f * gin[0].size_world.y;
    
    float4 v[4];
    v[0] = float4(gin[0].center_world + half_width * right - half_height * up, 1.0f);
    v[1] = float4(gin[0].center_world + half_width * right + half_height * up, 1.0f);
    v[2] = float4(gin[0].center_world - half_width * right - half_height * up, 1.0f);
    v[3] = float4(gin[0].center_world - half_width * right + half_height * up, 1.0f);
    
    // transform to world space and output
    float2 texc[4] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };
    
    GeoShaderOutput gout;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        gout.pos_homogenous = mul(v[i], global_view_proj);
        gout.pos_world = v[i].xyz;
        gout.normal_world = look;
        gout.texc = texc[i];
        gout.primt_Id = prim_id;
        
        triangle_stream.Append(gout);
    }

}
float4
PS_Main (GeoShaderOutput pin) : SV_Target {
    
    float3 uvw = float3(pin.texc, pin.primt_Id % 3);
    float4 diffuse_albedo =
        global_tree_map_array.Sample(global_sam_anisotropic_wrap, uvw) * global_diffuse_albedo;

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

    const float shininess = 1.0f - global_roughness;
    Material mat = { diffuse_albedo, global_fresnel_r0, shininess };
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

