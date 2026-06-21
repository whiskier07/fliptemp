// ============================================================================
// Timeline.h — Simple animation timeline with interpolation
// ============================================================================
// Modeled after CTimeline<float> from the uDWM reference.
// Drives enter/exit and rotation animations (CubicBezier easing by default).
// ============================================================================
#pragma once

#include <algorithm>
#include "Config.h"

// ============================================================================
// Timeline
// ============================================================================
struct Timeline
{
    // ---- Configuration ----
    float             m_durationSec  = 0.0f;
    float             m_elapsed      = 0.0f;
    float             m_startValue   = 0.0f;
    float             m_endValue     = 0.0f;
    InterpolationMode m_mode         = InterpolationMode::CubicBezier;
    bool              m_active       = false;

    // ---- Construction ----
    Timeline() = default;

    // ---- API ----

    // Begin a new animation segment (default: CubicBezier easing).
    void Restart(float startValue, float endValue, float durationSec);
    void Restart(float startValue, float endValue,
                 float durationSec, InterpolationMode mode);

    // Advance the timeline by dt seconds.
    void Update(float dtSeconds);

    // Returns the raw linear progress [0,1].
    float RawProgress() const;

    // Returns the interpolated value after easing.
    float Value() const;

    // Linear start→end lerp (RawProgress only, ignores m_mode).
    float LinearValue() const;

    // True if the timeline has completed.
    bool IsActive() const { return m_active; }
};
