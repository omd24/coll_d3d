
#include "common.hlsl"

struct VertIn {
    float3 pos_local : POSITION;
    float3 normal_local : NORMAL;
    float2 texc : TEXCOORD;
};
struct VertOut {
    float4 pos_hcs : SV_Position;
    float3 pos_local : Position;
};

VertOut
VS (VertIn vin, uint instance_id : SV_InstanceID) {
    VertOut ret = (VertOut)0.0f;
 
    // use local vertex position as cubemap lookup vector
    ret.pos_local = vin.pos_local;
 
    // transform to world space
    float4 pos_world = mul(float4(vin.pos_local, 1.0f), g_world);
 
    // always position sky at center (on camera)
    pos_world.xyz += g_eye_pos_w;

    // set z = w so that z/w = 1 (i.e., skydome always on far plane)
    ret.pos_hcs = mul(pos_world, g_view_proj).xyww;

    return ret;
}
float4
PS (VertOut pin) : SV_Target{
    return g_cubemap.Sample(g_sam_linear_wrap, pin.pos_local);
}

