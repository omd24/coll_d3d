#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif
#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif
#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "common.hlsl"

struct VertIn {
    float3 pos_local : POSITION;
    float3 normal_local : NORMAL;
    float2 texc : TEXCOORD;
};
struct VertOut {
    float4 pos_homogenous_clip_space : SV_Position;
    float3 pos_world : Position;
    float3 normal_world : NORMAL;
    float2 texc : TEXCOORD;
};
VertOut
VertexShader_Main (VertIn vin, uint instance_id : SV_InstanceID) {
    VertOut ret = (VertOut)0.0f;
 
    // fetch material data
    MaterialData mat_data = g_mat_data[g_mat_index];
    
    // transform to world space
    float4 pos_world = mul(float4(vin.pos_local, 1.0f), g_world);
    ret.pos_world = pos_world.xyz;
    
    // assuming nonuniform scale (otherwise have to use inverse-transpose of world-matrix)
    ret.normal_world = mul(vin.normal_local, (float3x3)g_world);

    // transform to homogenous clip space
    ret.pos_homogenous_clip_space = mul(pos_world, g_view_proj);

    // output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.texc, 0.0f, 1.0f), g_tex_transform);
    ret.texc = mul(texc, mat_data.mat_transform).xy;

    return ret;
}
float4
PixelShader_Main (VertOut pin) : SV_Target{
    // fetch material data
    MaterialData mat_data = g_mat_data[g_mat_index];
    
    // extract data
    float4 diffuse_albedo = mat_data.diffuse_albedo;
    uint diffuse_tex_index = mat_data.diffuse_map_index;
    float roughness = mat_data.roughness;
    float3 fresnel_r0 = mat_data.fresnel_r0;
    
    // dynamically look up the texture in the array
    diffuse_albedo *=
        g_diffuse_maps[diffuse_tex_index].Sample(g_sam_anisotropic_wrap, pin.texc);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif
    
    // interpolations of normal can unnormalize it so renormalize!
    pin.normal_world = normalize(pin.normal_world);

    // vector from point being lit "to eye"
    float3 to_eye = g_eye_pos_w - pin.pos_world;
    float dist_to_eye = length(to_eye);
    to_eye /= dist_to_eye; // normalize

    // indirect lighting
    float4 ambient = g_ambient_light * diffuse_albedo;

    const float shininess = 1.0f - roughness;
    Material mat = {diffuse_albedo, fresnel_r0, shininess};
    float3 shadow_factor = 1.0f;
    float4 direct_light = compute_lighting(
        g_lights, mat, pin.pos_world, pin.normal_world, to_eye, shadow_factor
    );
    float4 lit_color = ambient + direct_light;

    // reflect or refract ?
    float3 r = 0.0f;
    
    if (0 == mat_data.refract)
        r = reflect(-to_eye, pin.normal_world);
    else
        r = refract(-to_eye, pin.normal_world, mat_data.refract_ratio);
    
    float4 reflection_color = g_cubemap.Sample(g_sam_linear_wrap, r);
    float3 fresnel_factor = schlick_fresnel(fresnel_r0, pin.normal_world, r);
    lit_color.rgb += shininess * fresnel_factor * reflection_color.rgb;
 
#ifdef FOG
    float fog_amount_factor = saturate((dist_to_eye - g_fog_start) / g_fog_range);
    lit_color = lerp(lit_color, g_fog_color, fog_amount_factor);
#endif
    
    // common convention to take alpha from diffuse material
    lit_color.a = diffuse_albedo.a;

    return lit_color;
}

