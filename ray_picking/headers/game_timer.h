#pragma once
#include "common.h"

// TODO(omid): Separate timer .h/.cpp and prevent direct member access.

struct GameTimer {
    float sec_per_count;
    float delta_time;

    int64_t base_time;
    int64_t paused_time;
    int64_t stop_time;
    int64_t prev_time;
    int64_t curr_time;

    bool is_stopped;
};
inline void
Timer_Init (GameTimer * timer) {
    timer->delta_time = -1.0f;
    timer->base_time = 0;
    timer->paused_time = 0;
    timer->prev_time = 0;
    timer->curr_time = 0;
    timer->is_stopped = false;
    int64_t count_per_sec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&count_per_sec);
    timer->sec_per_count = 1.0f / (float)count_per_sec;
}
inline float
Timer_GetTotalTime (GameTimer * timer) {
    // If we are stopped, do not count the time that has passed since we stopped.
    // Moreover, if we previously already had a pause, the distance 
    // stop_time - base_time includes paused time, which we do not want to count.
    // To correct this, we can subtract the paused time from stop_time:  
    //
    //                     |<--paused time-->|
    // ----*---------------*-----------------*------------*------------*------> time
    //  base_time       stop_time        start_time     stop_time    curr_time

    if (timer->is_stopped) {
        return (float)(((timer->stop_time - timer->paused_time) - timer->base_time) * timer->sec_per_count);
    }

    // The distance curr_time - base_time includes paused time,
    // which we do not want to count.  To correct this, we can subtract 
    // the paused time from curr_time:  
    //
    //  (curr_time - paused_time) - base_time 
    //
    //                     |<--paused time-->|
    // ----*---------------*-----------------*------------*------> time
    //  base_time       stop_time        start_time     curr_time

    else {
        return (float)(((timer->curr_time - timer->paused_time) - timer->base_time) * timer->sec_per_count);
    }
}
inline void
Timer_Reset (GameTimer * timer) {
    int64_t curr_time;
    QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);

    timer->base_time = curr_time;
    timer->prev_time = curr_time;
    timer->stop_time = 0;
    timer->is_stopped = false;
}
inline void
Timer_Start (GameTimer * timer) {
    int64_t start_time;
    QueryPerformanceCounter((LARGE_INTEGER*)&start_time);


    // Accumulate the time elapsed between stop and start pairs.
    //
    //                     |<-------d------->|
    // ----*---------------*-----------------*------------> time
    //  base_time       stop_time        start_time     

    if (timer->is_stopped) {
        timer->paused_time += (start_time - timer->stop_time);

        timer->prev_time = start_time;
        timer->stop_time = 0;
        timer->is_stopped = false;
    }
}
inline void
Timer_Stop (GameTimer * timer) {
    if (!timer->is_stopped) {
        int64_t curr_time;
        QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);

        timer->stop_time = curr_time;
        timer->is_stopped = true;
    }
}
inline void
Timer_Tick (GameTimer * timer) {
    if (timer->is_stopped) {
        timer->delta_time = 0.0;
        return;
    }

    __int64 curr_time;
    QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);
    timer->curr_time = curr_time;

    // Time difference between this frame and the previous.
    timer->delta_time = (timer->curr_time - timer->prev_time) * timer->sec_per_count;

    // Prepare for next frame.
    timer->prev_time = timer->curr_time;

    // Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
    // processor goes into a power save mode or we get shuffled to another
    // processor, then delta_time can be negative.
    if (timer->delta_time < 0.0) {
        timer->delta_time = 0.0;
    }
}
