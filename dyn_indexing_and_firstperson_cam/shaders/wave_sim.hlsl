cbuffer CbUpdateSettings {
    float global_wave_constant0;
    float global_wave_constant1;
    float global_wave_constant2;
    
    float global_disturb_magnitude;
    int2 global_disturb_index;
};

RWTexture2D<float> global_prev_sol_input : register(u0);
RWTexture2D<float> global_curr_sol_input : register(u1);
RWTexture2D<float> global_output : register(u2);

[numthreads(16, 16, 1)]
void update_wave_cs (int3 dispatch_threadid : SV_DispatchThreadID) {
    // No need to bounds check:
    // out-of-bounds reads return 0
    // out-of-bounds writes are a no-op
    
    int x = dispatch_threadid.x;
    int y = dispatch_threadid.y;
    
    global_output[int2(x, y)] =
        global_wave_constant0 * global_prev_sol_input[int2(x, y)].r +
        global_wave_constant1 * global_curr_sol_input[int2(x, y)].r +
        global_wave_constant2 * (
            global_curr_sol_input[int2(x, y + 1)].r +
            global_curr_sol_input[int2(x, y - 1)].r +
            global_curr_sol_input[int2(x + 1, y)].r +
            global_curr_sol_input[int2(x - 1, y)].r);
}

[numthreads(1, 1, 1)]
void disturb_wave_cs (
    int3 group_threadid : SV_GroupThreadID,
    int3 dispatch_threadid : SV_DispatchThreadID
) {
    int x = global_disturb_index.x;
    int y = global_disturb_index.y;
    
    float half_mag = 0.5f * global_disturb_magnitude;
    
    // buffer is RW so operator += is well defined
    global_output[int2(x, y)] += global_disturb_magnitude;
    global_output[int2(x + 1, y)] += half_mag;
    global_output[int2(x - 1, y)] += half_mag;
    global_output[int2(x, y + 1)] += half_mag;
    global_output[int2(x, y - 1)] += half_mag;
    
}

