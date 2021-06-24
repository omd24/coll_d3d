cbuffer CbSsao : register(b0){
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_proj_tex;
    float4 g_offset_vectors[14];

    // for ssao_blur.hlsl
    float4 g_blur_weights[3];

    float2 g_inv_render_target_size;

    // coordinates given in view space
    float g_occlusion_radius;
    float g_occlusion_fade_start;
    float g_occlusion_fade_end;
    float g_surface_epsilon;
};

cbuffer CbRootConstants : register(b1){
    bool g_horizontal_blur;
};

// nonnumeric values cannot be added to a cbuffer
Texture2D g_normal_map : register(t0);
Texture2D g_depth_map : register(t1);
Texture2D g_input_map : register(t2);

SamplerState g_sam_point_clamp : register(s0);
SamplerState g_sam_linear_clamp : register(s1);
SamplerState g_sam_depth_map : register(s2);
SamplerState g_sam_linear_wrap : register(s3);

static const int g_blur_radius = 5;

static const float2 g_tex_coords[6] = {
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

struct VertexOut {
    float4 pos_h : SV_POSITION;
    float2 texc : TEXCOORD0;
};

VertexOut
VS (uint vid : SV_VertexID) {
    VertexOut ret;

    ret.texc = g_tex_coords[vid];

    // quad covering screen in NDC space
    ret.pos_h = float4(2.0f * ret.texc.x - 1.0f, 1.0f - 2.0f * ret.texc.y, 0.0f, 1.0f);

    return ret;
}
float
ndc_depth_to_view_depth (float z_ndc) {
    // z_ndc = A + B / z_view
    // A = proj_mat[2][2]
    // B = proj_mat[3][2]
    //
    float view_z = g_proj[3][2] / (z_ndc - g_proj[2][2]);
    return view_z;
}

float4
PS (VertexOut pin) : SV_Target{
  // unpack into float array
    float blur_weights[12] = {
        g_blur_weights[0].x, g_blur_weights[0].y, g_blur_weights[0].z, g_blur_weights[0].z,
        g_blur_weights[1].x, g_blur_weights[1].y, g_blur_weights[1].z, g_blur_weights[1].z,
        g_blur_weights[2].x, g_blur_weights[2].y, g_blur_weights[2].z, g_blur_weights[2].z,
    };
    float2 tex_offset;
    if (g_horizontal_blur)
        tex_offset = float2(g_inv_render_target_size.x, 0.0f);
    else
        tex_offset = float2(0.0f, g_inv_render_target_size.y);

    // center always contributes to sum
    float4 color = blur_weights[g_blur_radius] * g_input_map.SampleLevel(g_sam_point_clamp, pin.texc, 0.0f);
    float total_weight = blur_weights[g_blur_radius];
    
    float3 center_normal = g_normal_map.SampleLevel(g_sam_point_clamp, pin.texc, 0.0f).xyz;
    float center_depth = ndc_depth_to_view_depth(g_depth_map.SampleLevel(g_sam_depth_map, pin.texc, 0.0f).r);
    for (float i = -g_blur_radius; i <= g_blur_radius; ++i) {
        // already added center
        if (i == 0)
            continue;
        
        float2 tex = pin.texc + i * tex_offset;
        
        float3 neighbor_normal = g_normal_map.SampleLevel(g_sam_point_clamp, tex, 0.0f).xyz;
        float neighbor_depth = ndc_depth_to_view_depth(g_depth_map.SampleLevel(g_sam_depth_map, tex, 0.0f).r);
        
        // if not edge condition
        if (dot(neighbor_normal, center_normal) >= 0.8f && abs(neighbor_depth - center_depth) <= 0.2f) {
            float wt = blur_weights[i + g_blur_radius];
            
            color += wt * g_input_map.SampleLevel(g_sam_point_clamp, tex, 0.0f);
            total_weight += wt;
        }
    }
    // compensate for discared samples by making total weights sum to 1
    return color / total_weight;
}
