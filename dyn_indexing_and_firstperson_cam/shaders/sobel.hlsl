// edge detection using sobel operator

Texture2D global_input : register(t0);
RWTexture2D<float4> global_output : register(u0);

// approximate luminance ('brightness') from an RGB value. These weights are dervied from
// experiments based on eye sensitivity to different wavelengths of light
float
calc_luminance (float3 color) {
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

[numthreads(16, 16, 1)]
void
sobel_cs (int3 dispatch_threadid : SV_DispatchThreadID) {
    // sample pixels in the neighborhood of this pixel (dispatch_threadid.xy)
    float4 c[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int2 xy = dispatch_threadid.xy + int2(-1 + j, -1 + i);
            c[i][j] = global_input[xy];
        }
    }

    // for each color channel, estimate partial x derivative using sobel scheme
    float4 gradient_x = -1.0f*c[0][0] - 2.0f*c[1][0] - 1.0f*c[2][0] + 1.0f*c[0][2] + 2.0f*c[1][2] + 1.0f*c[2][2];

    // for each color channel, estimate partial y derivative using sobel scheme
    float4 gradient_y = -1.0f*c[2][0] - 2.0f*c[2][1] - 1.0f*c[2][1] + 1.0f*c[0][0] + 2.0f*c[0][1] + 1.0f*c[0][2];

    // gradient is (gradient_x, gradient_y)
    // for each color channel, compute magnitude to get maximum rate of change
    float4 mag = sqrt(gradient_x*gradient_x + gradient_y*gradient_y);

    // make edges black and nonedges white
    mag = 1.0f - saturate(calc_luminance(mag.rgb));

    global_output[dispatch_threadid.xy] = mag;
}
