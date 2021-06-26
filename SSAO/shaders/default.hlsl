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
    float3 tangent_u : TANGENT;
};
struct VertOut {
    float4 pos_h : SV_Position;
    float4 shadow_pos_h : POSITION0;
    float4 ssao_pos_h : POSITION1;
    float3 pos_world : Position2;
    float3 normal_world : NORMAL;
    float3 tangent_world : TANGENT;
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

    ret.tangent_world = mul(vin.tangent_u, (float3x3)g_world);

    // transform to homogenous clip space
    ret.pos_h = mul(pos_world, g_view_proj);

    // output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.texc, 0.0f, 1.0f), g_tex_transform);
    ret.texc = mul(texc, mat_data.mat_transform).xy;

    // generate projectvie tex-coords to project SSAO map onto scene
    ret.ssao_pos_h = mul(pos_world, g_view_proj_tex);

    // generate projective tex-coords to project shadow map onto the scene
    ret.shadow_pos_h = mul(pos_world, g_shadow_transform);
    
    return ret;
}
float4
PixelShader_Main (VertOut pin) : SV_Target{
    // fetch material data
    MaterialData mat_data = g_mat_data[g_mat_index];
    
    // extract data
    float4 diffuse_albedo = mat_data.diffuse_albedo;
    uint diffuse_tex_index = mat_data.diffuse_map_index;
    uint normal_map_index = mat_data.normal_map_index;
    float roughness = mat_data.roughness;
    float3 fresnel_r0 = mat_data.fresnel_r0;
    
    // dynamically look up the texture in the array
    diffuse_albedo *=
        g_tex_maps[diffuse_tex_index].Sample(g_sam_anisotropic_wrap, pin.texc);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif
    
    // interpolations of normal can unnormalize it so renormalize!
    pin.normal_world = normalize(pin.normal_world);

    float4 nmap_sample = g_tex_maps[normal_map_index].Sample(g_sam_anisotropic_wrap, pin.texc);
    float3 bumped_normal_w = normal_sample_to_world_space(nmap_sample.rgb, pin.normal_world, pin.tangent_world);

    //uncomment to turn off normal mapping
    //bumped_normal_w = pin.normal_world;

    // vector from point being lit "to eye"
    float3 to_eye = g_eye_pos_w - pin.pos_world;
    float dist_to_eye = length(to_eye);
    to_eye /= dist_to_eye; // normalize

    //
    // using the ambient map
    // -- finish texture projection and sample SSAO map
    pin.ssao_pos_h /= pin.ssao_pos_h.w;
    float ambient_access = g_ssao_map.Sample(g_sam_linear_clamp, pin.ssao_pos_h.xy, 0.0f).r;
    
    // -- apply accessiblity to indirect light term
    float4 ambient = ambient_access * g_ambient_light * diffuse_albedo;

    //
    // calculate shadow factor
    // only the first light casts a shadow
    float3 shadow_factor = float3(1.0f, 1.0f, 1.0f);
    shadow_factor[0] = calc_shadow_factor(pin.shadow_pos_h);
    
    const float shininess = (1.0f - roughness) * nmap_sample.a;
    Material mat = {diffuse_albedo, fresnel_r0, shininess};
    
    float4 direct_light = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (g_dir_light_flag) {
        direct_light = compute_lighting(
            g_lights, mat, pin.pos_world, bumped_normal_w, to_eye, shadow_factor
        );
    }
    float4 lit_color = ambient + direct_light;

    // add specular reflections
    float3 r = reflect(-to_eye, bumped_normal_w);
    float4 reflection_color = g_cubemap.Sample(g_sam_linear_wrap, r);
    float3 fresnel_factor = schlick_fresnel(fresnel_r0, bumped_normal_w, r);
    lit_color.rgb += shininess * fresnel_factor * reflection_color.rgb;
 
#ifdef FOG
    float fog_amount_factor = saturate((dist_to_eye - g_fog_start) / g_fog_range);
    lit_color = lerp(lit_color, g_fog_color, fog_amount_factor);
#endif
    
    // common convention to take alpha from diffuse material
    lit_color.a = diffuse_albedo.a;

    return lit_color;
}

