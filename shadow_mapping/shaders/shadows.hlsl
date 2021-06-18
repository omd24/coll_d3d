
#include "common.hlsl"

struct VertexIn {
    float3 pos_l : POSITION;
    float2 texc : TEXCOORD;
};

struct VertexOut {
    float4 pos_h : SV_POSITION;
    float2 texc : TEXCOORD;
};

VertexOut
VS (VertexIn vin) {
    VertexOut ret = (VertexOut)0.0f;
    
    MaterialData mat_data = g_mat_data[g_mat_index];
    
    // transform to world space
    float4 pos_w = mul(float4(vin.pos_l, 1.0f), g_world);
    
    // transform to homogenous clip space
    ret.pos_h = mul(pos_w, g_view_proj);
    
    // output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.texc, 0.0f, 1.0f), g_tex_transform);
    ret.texc = mul (texc, mat_data.mat_transform).xy;
    
    return ret;
}

// PS is only used for alpha cut geometry (if needed) so that shadows show up correctly
// geometry that does not need to sample a tex can use NULL ps for depth pass
void
PS (VertexOut pin) {
    MaterialData mat_data = g_mat_data[g_mat_index];
    float4 diffuse_albedo = mat_data.diffuse_albedo;
    uint diffuse_map_index= mat_data.diffuse_map_index;
    
    diffuse_albedo *= g_tex_maps[diffuse_map_index].Sample(g_sam_anisotropic_wrap, pin.texc);
    
#ifdef ALPHA_TEX
    // discard pixel if tex alpha < .1
    clip(diffuse_albedo.a - .1f);
#endif
}

