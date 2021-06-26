
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

    // already in homogenous clip space
    ret.pos_h = float4(vin.pos_l, 1.0f);

    ret.texc = vin.texc;

    return ret;
}
float4
PS (VertexOut pin) : SV_Target {
    //return float4(g_smap.Sample(g_sam_linear_wrap, pin.texc).rrr, 1.0f);
    return float4(g_ssao_map.Sample(g_sam_linear_wrap, pin.texc).rrr, 1.0f);
}
