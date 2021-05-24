// composite hlsl (combines two images)

Texture2D global_base_map : register(t0);
Texture2D global_edge_map : register(t1);

SamplerState global_sam_point_wrap : register(s0);
SamplerState global_sam_point_clamp : register(s1);
SamplerState global_sam_linear_wrap : register(s2);
SamplerState global_sam_linear_clamp : register(s3);
SamplerState global_sam_anisotropic_wrap : register(s4);
SamplerState global_sam_anisotropic_clamp : register(s5);

static const float2 global_tex_coords [6] = {
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

struct VertexOut {
    float4 posh : SV_POSITION;
    float2 texc : TEXCOORD;
};

VertexOut
composite_vs (uint vid : SV_VertexID) {
    VertexOut vout = (VertexOut) 0.0f;
    vout.texc = global_tex_coords[vid];

    // map [0,1]^2 to NDC space
    vout.posh = float4(
        2.0f * vout.texc.x - 1.0f,
        1.0f - 2.0f * vout.texc.y,
        0.0f,
        1.0f
    );

    return vout;
}

float4
composite_ps (VertexOut pin) : SV_Target {
    float4 c = global_base_map.SampleLevel(global_sam_point_clamp, pin.texc, 0.0f);
    float4 e = global_edge_map.SampleLevel(global_sam_point_clamp, pin.texc, 0.0f);

    return c * e;
}
