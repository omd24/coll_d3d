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
Texture2D g_random_vec_map : register(t2);

SamplerState g_sam_point_clamp : register(s0);
SamplerState g_sam_linear_clamp : register(s1);
SamplerState g_sam_depth_map : register(s2);
SamplerState g_sam_linear_wrap : register(s3);

static const int g_sample_count = 14;

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
    float3 pos_v : POSITION;
    float2 texc : TEXCOORD0;
};

VertexOut
VS (uint vid : SV_VertexID) {
    VertexOut ret;

    ret.texc = g_tex_coords[vid];

    // quad covering screen in NDC space
    ret.pos_h = float4(2.0f * ret.texc.x - 1.0f, 1.0f - 2.0f * ret.texc.y, 0.0f, 1.0f);

    // transform quad corners to view space near plane
    float4 pos_proj = mul(ret.pos_h, g_inv_proj);
    ret.pos_v = pos_proj.xyz / pos_proj.w;

    return ret;
}
//
// determine how much potential occluding point r (corresponding to random sample q) occludes point p
// as a function of dist_z = | p.z - r.z |
float
occlusion_func (float dist_z) {
    // 
    // if q is behind p then q cannot occlude p.
    // if q and p are close enough we assume same plane, so
    //  q should be at least epsilon depth in front of p to occlude it
    
    // this is the linear fallout function to calculate occlusion
    //      1.0     ------------------\
    //              |                 | \
    //              |                 |   \
    //              |                 |     \
    //              |                 |       \
    //              |                 |         \
    //      0.0    Eps               zstart     zend         -> z values
    //

    float occlusion = 0.0f;
    if (dist_z > g_surface_epsilon) {
        float fade_len = g_occlusion_fade_end - g_occlusion_fade_start;
        occlusion = saturate((g_occlusion_fade_end - dist_z) / fade_len);
    }
    return occlusion;
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
    // p -- the point we are processing (calculating occlusion for)
    // r -- potential occluder obtatined from random point q
    // q -- random point from p (obtained with a random offset)
    // n -- normal vector at point p

    // get view space normal and depth at current pixel p
    float3 n = normalize(g_normal_map.SampleLevel(g_sam_point_clamp, pin.texc, 0.0f).xyz);
    float pz = g_depth_map.SampleLevel(g_sam_depth_map, pin.texc, 0.0f).r;
    pz = ndc_depth_to_view_depth(pz);
    
    //
    // reconstruct p coordinates in view space (px, py, pz)
    // we know p = v * (pz/vz)
    // where v is "to view space near plane" vector interpolated
    // (interpolated from vertex shader 6 quad corners)
    float3 p = pin.pos_v * (pz / pin.pos_v.z);

    // extract random vector from the random map and transform [0,1] --> [-1,+1]
    float3 rand_vec = 2.0f * g_random_vec_map.SampleLevel(g_sam_linear_wrap, 4.0f * pin.texc, 0.0f).rgb - 1.0f;

    float occlusion_sum = 0.0f;

    // sample neighboring points about p in the hemisphere oriented by n
    for (int i = 0; i < g_sample_count; ++i) {
        // reflecting our fixed offset vectors about a random vector gives us
        // offset vectors with random uniform distribution
        float3 offset = reflect(g_offset_vectors[i].xyz, rand_vec);

        // flip offset vector if it's behind the plane defined by (p, n)
        float flip = sign(dot(offset, n));

        // sample a point near p within specified radius
        float3 q = p + flip * g_occlusion_radius * offset;

        // project q and generate projetive tex coords
        float4 proj_q = mul(float4(q, 1.0f), g_proj_tex);
        proj_q /= proj_q.w;

        // find rz (the nearest depth value along the ray from eye to q)
        float rz = g_depth_map.SampleLevel(g_sam_depth_map, proj_q.xy, 0.0f).r;
        rz = ndc_depth_to_view_depth(rz);

        //
        // reconstruct r coordinates in view space (rx, ry, rz)
        // we know r = q * (rz/qz)
        float3 r = q * (rz / q.z);

        //
        // Occlusion Test:
        //      * the product dot(n, normalize(r - p) measures how much in front of
        //        of the plane (p, n) the occluder r is
        //        this also prevents self shadowing (false occlusion)
        //      * the weight of the occlusion is scaled based on how far the occluder
        //        is from p. If r is far from p then it does not occlude
        float dist_z = p.z - r.z;
        float dp = max(dot(n, normalize(r - p)), 0.0f);

        float occlusion = dp * occlusion_func(dist_z);
        occlusion_sum += occlusion;
    }
    occlusion_sum /= g_sample_count;

    float access = 1.0f - occlusion_sum;

    // sharpen the contrast of the SSAO map to make the SSAO effect more dramatic
    return saturate(pow(access, 6.0f));
}
