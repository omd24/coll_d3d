cbuffer CbSettings : register(b0){
    // we cannot have an array entry in a cbuffer that gets mapped to root constants,
    // so list each element

    int global_blur_radius;

    // support up to 11 blur weights
    float w0;
    float w1;
    float w2;
    float w3;
    float w4;
    float w5;
    float w6;
    float w7;
    float w8;
    float w9;
    float w10;
};

static const int global_max_blur_radius = 5;

Texture2D global_input : register(t0);
RWTexture2D<float4> global_output : register(u0);

#define N 256
#define CacheSize (N + 2 * global_max_blur_radius)
groupshared float4 global_cache[CacheSize];

[numthreads(N, 1, 1)]
void horizontal_blur_cs (
    int3 group_threadid : SV_GroupThreadID,
    int3 dispatch_theadid : SV_DispatchThreadID
) {
    // put in an array for each indexing
    float weights[11] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10};

    // we don't have Texture2D.Length field
    int tex_x = 0;
    int tex_y = 0;
    global_input.GetDimensions(tex_x, tex_y);
    int2 tex_xy = int2(tex_x, tex_y);
    
    //
    // fill local thread storage to reduce bandwith.
    // to blur N pixels we need to load N + 2 * BlurRadius pixels
    //

    // this thread group runs N threads.
    // to get the extra 2 * BlurRadius pixels, have 2*BlurRadius threads sample one extra pixel
    if (group_threadid.x < global_blur_radius) {
        // clamp bounds at image border
        int x = max(dispatch_theadid.x - global_blur_radius, 0);
        global_cache[group_threadid.x] = global_input[int2(x, dispatch_theadid.y)];
    }
    if (group_threadid.x >= (N - global_blur_radius)) {
        // clamp bounds at image border
        int x = min(dispatch_theadid.x + global_blur_radius, tex_x - 1);
        global_cache[group_threadid.x + 2 * global_blur_radius] =
            global_input[int2(x, dispatch_theadid.y)];
    }
    // clamp bounds at image border
    global_cache[group_threadid.x + global_blur_radius] =
        global_input[min(dispatch_theadid.xy, tex_xy - 1)];

    // wait for all threads to finish
    GroupMemoryBarrierWithGroupSync();

    //
    // now blur each pixel
    //
    float4 blur_color = float4(0, 0, 0, 0);
    for (int i = -global_blur_radius; i <= global_blur_radius; ++i) {
        int k = group_threadid.x + global_blur_radius + i;
        blur_color += weights[i + global_blur_radius] * global_cache[k];
    }

    global_output[dispatch_theadid.xy] = blur_color;
}
[numthreads(1, N, 1)]
void vertical_blur_cs (
    int3 group_threadid : SV_GroupThreadID,
    int3 dispatch_theadid : SV_DispatchThreadID
) {
    // put in an array for each indexing
    float weights[11] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10};

    // we don't have Texture2D.Length field
    int tex_x = 0;
    int tex_y = 0;
    global_input.GetDimensions(tex_x, tex_y);
    int2 tex_xy = int2(tex_x, tex_y);

    //
    // fill local thread storage to reduce bandwith.
    // to blur N pixels we need to load N + 2 * BlurRadius pixels
    //

    // this thread group runs N threads.
    // to get the extra 2 * BlurRadius pixels, have 2*BlurRadius threads sample one extra pixel
    if (group_threadid.y < global_blur_radius) {
        // clamp bounds at image border
        int y = max(dispatch_theadid.y - global_blur_radius, 0);
        global_cache[group_threadid.y] = global_input[int2(dispatch_theadid.x, y)];
    }
    if (group_threadid.y >= (N - global_blur_radius)) {
        // clamp bounds at image border
        int y = min(dispatch_theadid.y + global_blur_radius, tex_y - 1);
        global_cache[group_threadid.y + 2 * global_blur_radius] =
            global_input[int2(dispatch_theadid.x, y)];
    }
    // clamp bounds at image border
    global_cache[group_threadid.y + global_blur_radius] =
        global_input[min(dispatch_theadid.xy, tex_xy - 1)];

    // wait for all threads to finish
    GroupMemoryBarrierWithGroupSync();

    //
    // now blur each pixel
    //
    float4 blur_color = float4(0, 0, 0, 0);
    for (int i = -global_blur_radius; i <= global_blur_radius; ++i) {
        int k = group_threadid.y + global_blur_radius + i;
        blur_color += weights[i + global_blur_radius] * global_cache[k];
    }

    global_output[dispatch_theadid.xy] = blur_color;
}
