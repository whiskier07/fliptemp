// ============================================================================
// Timeline.cpp — Animation timeline implementation
// ============================================================================
#include "Timeline.h"
#include "FlipMath.h"

// ============================================================================
// Timeline implementation
// ============================================================================

void Timeline::Restart(float startValue, float endValue, float durationSec)
{
    Restart(startValue, endValue, durationSec, InterpolationMode::CubicBezier);
}

void Timeline::Restart(float startValue, float endValue,
                       float durationSec, InterpolationMode mode)
{
    m_startValue  = startValue;
    m_endValue    = endValue;
    m_durationSec = std::max(durationSec, 0.0001f);
    m_elapsed     = 0.0f;
    m_mode        = mode;
    m_active      = true;
}

void Timeline::Update(float dtSeconds)
{
    if (m_active)
    {
        m_elapsed = std::min(m_elapsed + dtSeconds, m_durationSec);
        if (m_elapsed >= m_durationSec)
            m_active = false;
    }
}

float Timeline::RawProgress() const
{
    return std::clamp(m_elapsed / std::max(m_durationSec, 0.0001f), 0.0f, 1.0f);
}

float Timeline::Value() const
{
    float t = RawProgress();

    if (m_mode == InterpolationMode::Cubic)
    {
        // uDWM / flip3d CTimeline::Progress — smoothstep
        t = t * t * (3.0f - (2.0f * t));
    }
    else if (m_mode == InterpolationMode::CubicBezier)
    {
        t = Math::TimelineEase(t);
    }

    return Math::Lerp(m_startValue, m_endValue, t);
}

float Timeline::LinearValue() const
{
    return Math::Lerp(m_startValue, m_endValue, RawProgress());
}
