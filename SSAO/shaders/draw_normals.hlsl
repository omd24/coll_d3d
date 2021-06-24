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
    float3 normal_world : NORMAL;
    float3 tangent_world : TANGENT;
    float2 texc : TEXCOORD;
};
VertOut
VS (VertIn vin, uint instance_id : SV_InstanceID) {
    VertOut ret = (VertOut)0.0f;
 
    // fetch material data
    MaterialData mat_data = g_mat_data[g_mat_index];

    // assuming nonuniform scale (otherwise have to use inverse-transpose of world-matrix)
    ret.normal_world = mul(vin.normal_local, (float3x3)g_world);
    ret.tangent_world = mul(vin.tangent_u, (float3x3)g_world);

    // transform to homogenous clip space
    float4 pos_w = mul(float4(vin.pos_local, 1.0f), g_world);
    ret.pos_h = mul(pos_w, g_view_proj);

    // output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.texc, 0.0f, 1.0f), g_tex_transform);
    ret.texc = mul(texc, mat_data.mat_transform).xy;

    return ret;
}
float4
PS (VertOut pin) : SV_Target{
    // fetch material data
    MaterialData mat_data = g_mat_data[g_mat_index];
    
    // extract data
    float4 diffuse_albedo = mat_data.diffuse_albedo;
    uint diffuse_tex_index = mat_data.diffuse_map_index;
    uint normal_map_index = mat_data.normal_map_index;

    // dynamically look up the texture in the array
    diffuse_albedo *=
        g_tex_maps[diffuse_tex_index].Sample(g_sam_anisotropic_wrap, pin.texc);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif
    
    // interpolations of normal can unnormalize it so renormalize!
    pin.normal_world = normalize(pin.normal_world);

    // NOTE! we use interpolated vertex normals for SSAO

    // write normal in view space coords
    float3 normal_v = mul(pin.normal_world, (float3x3)g_view);
    return float4(normal_v, 0.0f);
}

