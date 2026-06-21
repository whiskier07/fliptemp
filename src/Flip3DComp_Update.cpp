// ============================================================================
// Flip3DComp_Update.cpp — Per-frame update, camera, card transforms, scroll
// ============================================================================
//
// Carousel uses fractional smooth scroll (m_scrollPos) for browse and exit.
// WrapCarouselScroll rotates the window list at slot boundaries; outgoing cards
// keep a continuous displaySlot through the front-edge fade (see CardModel).
//
// ============================================================================
#include "Flip3DComp.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ============================================================================
// Flip3DCompApp::EnterProgress
// ============================================================================
float Flip3DCompApp::EnterProgress() const
{
    return std::max(std::min(m_animEnter.Value(), 1.0f), 0.0f);
}

// ============================================================================
// Flip3DCompApp::ReplayEnterAnimation
// ============================================================================
void Flip3DCompApp::ReplayEnterAnimation()
{
    m_state = ViewState::Enter;
    m_animEnter.Restart(0.0f, 1.0f, kEnterExitDurationSec);
    m_scrollPos           = 0.0f;
    m_scrollTarget        = 0.0f;
    m_selectedHwnd        = nullptr;
    m_rRepeatedRotateRate = 0.0f;
    m_showOutgoingDuringRotation = false;
    m_lastPaintOrder.clear();
    m_originalFrontHwnd   = m_cards.empty() ? nullptr : m_cards[0].m_hwnd;
    InvalidateDisplaySlots();
}

// ============================================================================
// Flip3DCompApp::RotateListPhysically
// List splice when committing fractional scroll to discrete slots (exit only).
// ============================================================================
void Flip3DCompApp::RotateListPhysically(bool backward)
{
    if (m_cards.size() <= 1)
        return;

    if (backward)
        std::rotate(m_cards.rbegin(), m_cards.rbegin() + 1, m_cards.rend());
    else
        std::rotate(m_cards.begin(), m_cards.begin() + 1, m_cards.end());
}

// ============================================================================
// Flip3DCompApp::WrapCarouselScroll
// Keep scrollPos in (-0.5, 0.5) by rotating the list at integer boundaries.
// Forward wrap decouples the outgoing card's display slot so it can finish the
// front-edge opacity ramp (slot in [-1, 0]) without jumping to the back.
// ============================================================================
void Flip3DCompApp::WrapCarouselScroll()
{
    if (m_cards.size() <= 1)
        return;

    const int N = (int)m_cards.size();
    int guard = 0;
    while (m_scrollPos >= 0.5f && guard++ <= N)
    {
        const HWND  outgoingHwnd = m_cards[0].m_hwnd;
        const float outgoingSlot = GetCardDisplaySlot(0);

        RotateListPhysically(false);
        m_scrollPos    -= 1.0f;
        m_scrollTarget -= 1.0f;

        OnCarouselWrapForward(outgoingHwnd, outgoingSlot);
    }
    guard = 0;
    while (m_scrollPos <= -0.5f && guard++ <= N)
    {
        RotateListPhysically(true);
        m_scrollPos    += 1.0f;
        m_scrollTarget += 1.0f;

        OnCarouselWrapBackward();
    }
}

// ============================================================================
// Display slot helpers — continuous carousel coordinates per card
// ============================================================================
float Flip3DCompApp::CardListSlot(int listIndex) const
{
    return (float)listIndex - m_scrollPos;
}

float Flip3DCompApp::GetCardDisplaySlot(int listIndex) const
{
    if (listIndex < 0 || listIndex >= (int)m_cards.size())
        return 0.0f;

    const CardModel& c = m_cards[(size_t)listIndex];
    const bool scrollPath = (m_state == ViewState::Interactive);

    if (m_state == ViewState::ExitRepeatedRotate)
    {
        if (m_rotateTimeline.IsActive())
            return ComputeRotationDisplaySlot(listIndex);
        return CardListSlot(listIndex);
    }

    if (scrollPath && c.m_displaySlotValid)
        return c.m_displaySlot;

    return CardListSlot(listIndex);
}

void Flip3DCompApp::InvalidateDisplaySlots()
{
    for (auto& c : m_cards)
    {
        c.m_displaySlotValid = false;
        c.m_exitingFront     = false;
    }
}

void Flip3DCompApp::SyncDisplaySlotsToList()
{
    for (int k = 0; k < (int)m_cards.size(); ++k)
    {
        CardModel& c = m_cards[(size_t)k];
        if (c.m_exitingFront)
            continue;

        c.m_displaySlot      = CardListSlot(k);
        c.m_displaySlotValid = true;
    }
}

void Flip3DCompApp::AdvanceExitingDisplaySlots(float deltaScrollPos)
{
    if (deltaScrollPos == 0.0f)
        return;

    for (int k = 0; k < (int)m_cards.size(); ++k)
    {
        CardModel& c = m_cards[(size_t)k];
        if (!c.m_exitingFront)
            continue;

        c.m_displaySlot -= deltaScrollPos;
        if (c.m_displaySlot <= -1.0f)
        {
            c.m_exitingFront     = false;
            c.m_displaySlot      = CardListSlot(k);
            c.m_displaySlotValid = true;
        }
    }
}

void Flip3DCompApp::OnCarouselWrapForward(HWND outgoingHwnd, float outgoingSlot)
{
    for (auto& c : m_cards)
    {
        if (c.m_hwnd != outgoingHwnd)
            continue;

        c.m_displaySlot      = outgoingSlot;
        c.m_exitingFront     = true;
        c.m_displaySlotValid = true;
        break;
    }

    SyncDisplaySlotsToList();
}

void Flip3DCompApp::OnCarouselWrapBackward()
{
    SyncDisplaySlotsToList();
}

// ============================================================================
// Flip3DCompApp::RotateSelectedToListFront
// uDWM: after scroll settles, splice the ring so the selection is list index 0
// (e.g. A B C D E F G → C D E F G A B). Cards after C in the ring follow C at
// slots 1…, not the windows that were before C (A B).
// ============================================================================
void Flip3DCompApp::RotateSelectedToListFront()
{
    if (!m_selectedHwnd || m_cards.size() <= 1)
        return;

    const int idx = FindCardIndex(m_selectedHwnd);
    if (idx <= 0)
        return;

    for (int i = 0; i < idx; ++i)
        RotateListPhysically(false);
}

// ============================================================================
// Flip3DCompApp::CommitCarouselScroll
// Snap fractional scroll to integer slots and align the window list.
// ============================================================================
void Flip3DCompApp::CommitCarouselScroll()
{
    if (m_cards.size() <= 1)
    {
        m_scrollPos    = 0.0f;
        m_scrollTarget = 0.0f;
        InvalidateDisplaySlots();
        return;
    }

    AlignCarouselScrollSettled();
}

// ============================================================================
// Flip3DCompApp::AlignCarouselScrollSettled
// Wrap + rotate selection to list front + zero scroll (shared by commit & exit).
// ============================================================================
void Flip3DCompApp::AlignCarouselScrollSettled()
{
    if (m_cards.size() <= 1)
    {
        m_scrollPos    = 0.0f;
        m_scrollTarget = 0.0f;
        return;
    }

    WrapCarouselScroll();
    RotateSelectedToListFront();
    m_scrollPos    = 0.0f;
    m_scrollTarget = 0.0f;
    InvalidateDisplaySlots();
}

// ============================================================================
// Flip3DCompApp::RotateBy
// ============================================================================
void Flip3DCompApp::RotateBy(int deltaSteps)
{
    if (!deltaSteps || m_cards.size() <= 1
        || m_state == ViewState::Exit
        || m_state == ViewState::ExitRepeatedRotate)
        return;

    m_scrollTarget += (float)deltaSteps;
}

// ============================================================================
// Flip3DCompApp::ComputeScrollTargetForCard
// Scroll value that places listIndex at the visual front (slot ≈ 0).
// Short-path wrap is only used when it moves scrollPos in the direction that
// reduces |visualSlot| (slot = index - scrollPos increases as scrollPos falls).
// ============================================================================
float Flip3DCompApp::ComputeScrollTargetForCard(int listIndex) const
{
    const int N = (int)m_cards.size();
    if (N <= 1)
        return m_scrollPos;

    const float visualSlot = (float)listIndex - m_scrollPos;
    if (std::abs(visualSlot) < kScrollSettleEpsilon)
        return m_scrollPos;

    float shortDelta = visualSlot;
    if (shortDelta > (float)N * 0.5f)
        shortDelta -= (float)N;
    else if (shortDelta < -(float)N * 0.5f)
        shortDelta += (float)N;

    if (visualSlot > 0.0f)
    {
        if (shortDelta > 0.0f && std::abs(shortDelta) < std::abs(visualSlot))
            return m_scrollPos + shortDelta;
    }
    else
    {
        if (shortDelta < 0.0f && std::abs(shortDelta) < std::abs(visualSlot))
            return m_scrollPos + shortDelta;
    }

    return m_scrollPos + visualSlot;
}

// ============================================================================
// Flip3DCompApp::IsSelectionAtCarouselFront
// Exit scroll completes when the selection is list index 0 and scroll settled.
// ============================================================================
bool Flip3DCompApp::IsSelectionAtCarouselFront() const
{
    if (!m_selectedHwnd || m_cards.empty())
        return true;

    if (m_cards[0].m_hwnd != m_selectedHwnd)
        return false;

    return std::abs(m_scrollPos) < 0.05f
        && std::abs(m_scrollTarget - m_scrollPos) < 0.05f;
}

// ============================================================================
// Flip3DCompApp::StepCarouselScroll
// Shared smooth scroll + wrap + front-edge fade (browse and exit).
// ============================================================================
void Flip3DCompApp::StepCarouselScroll(float dtSeconds, bool notifyFrontChange)
{
    const float prevFront  = std::floor(m_scrollPos + 0.5f);
    const float prevScroll = m_scrollPos;
    const float a = 1.0f - std::exp(-dtSeconds / std::max(kScrollSmoothTimeSec, 1e-4f));
    m_scrollPos += (m_scrollTarget - m_scrollPos) * a;
    const float smoothDelta = m_scrollPos - prevScroll;
    WrapCarouselScroll();
    AdvanceExitingDisplaySlots(smoothDelta);

    if (notifyFrontChange)
    {
        const float newFront = std::floor(m_scrollPos + 0.5f);
        if (newFront != prevFront)
            NotifyAccessibilityFocusFront();
    }
}

// ============================================================================
// Flip3DCompApp::RotateToWindow
// ============================================================================
void Flip3DCompApp::RotateToWindow(HWND targetHwnd)
{
    if (m_cards.size() <= 1 || m_state == ViewState::Exit
        || m_state == ViewState::ExitRepeatedRotate)
        return;

    int idx = -1;
    for (int i = 0; i < (int)m_cards.size(); ++i)
    {
        if (m_cards[(size_t)i].m_hwnd == targetHwnd) { idx = i; break; }
    }
    if (idx < 0)
        return;

    m_scrollTarget = ComputeScrollTargetForCard(idx);
}

// ============================================================================
// Flip3DCompApp::TickSmoothScroll
// ============================================================================
void Flip3DCompApp::TickSmoothScroll(float dtSeconds)
{
    if (m_cards.size() <= 1 || m_state != ViewState::Interactive)
        return;

    StepCarouselScroll(dtSeconds, /*notifyFrontChange=*/true);
}

// ============================================================================
// Flip3DCompApp::Update
// ============================================================================
void Flip3DCompApp::Update(float dtSeconds)
{
    if (m_thumbnailsDirty)
        OnThumbnailSourceSizeChanged();

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        dtSeconds *= 0.05f;

    m_animEnter.Update(dtSeconds);

    if (m_rotateTimeline.IsActive())
    {
        m_rotateTimeline.Update(dtSeconds);
        OnRotationTimeUpdated();
    }

    if (!m_rotateTimeline.IsActive())
    {
        m_showOutgoingDuringRotation = false;
        TickRepeatedRotate();
    }

    TickSmoothScroll(dtSeconds);

    const float enterProgress = EnterProgress();
    const float washProgress  = std::clamp(m_animEnter.LinearValue(), 0.0f, 1.0f);

    if (m_sceneVisual)
        m_sceneVisual->SetOpacity(1.0f);
    for (auto& mon : m_monitorBackdrops)
    {
        if (mon.washVisual)
            mon.washVisual->SetOpacity(washProgress * kDesktopWashOpacityScale);
        if (mon.shellContainer)
            mon.shellContainer->SetOpacity(1.0f);
    }

    if (m_state == ViewState::Enter && !m_animEnter.IsActive())
    {
        m_state = ViewState::Interactive;
        InvalidateDisplaySlots();
        NotifyAccessibilityEvent(EVENT_SYSTEM_DIALOGSTART);
        NotifyAccessibilityFocusFront();
    }

    UpdateCamera(enterProgress);
    UpdateCards(enterProgress);

    if (m_dcompDevice)
        m_dcompDevice->Commit();

    if (m_state == ViewState::Exit && !m_animEnter.IsActive())
        DestroyWindow(m_hwnd);

    if (m_state == ViewState::ExitRepeatedRotate
        && !m_animEnter.IsActive()
        && !m_rotateTimeline.IsActive()
        && !m_cards.empty()
        && m_cards[0].m_hwnd == m_selectedHwnd)
    {
        DestroyWindow(m_hwnd);
    }
}

// ============================================================================
// Flip3DCompApp::BuildCameraMatrix
// View × Proj × Viewport — shared by card transforms and hit-testing.
// ============================================================================
Matrix4x4 Flip3DCompApp::BuildCameraMatrix(float enterProgress) const
{
    auto view     = BuildViewMatrix(enterProgress);
    auto proj     = BuildProjMatrix(1.0f, enterProgress);
    auto viewport = Math::BuildViewportMatrix(m_monW, m_monH, m_viewX, m_viewY);
    return Math::Multiply(Math::Multiply(view, proj), viewport);
}

// ============================================================================
// Flip3DCompApp::UpdateCamera
// Scene root stays identity. Each card bakes Model × View × Proj × VP on its
// container visual (same as the pre-SPATIAL single-matrix path).
// ============================================================================
void Flip3DCompApp::UpdateCamera(float /*enterProgress*/)
{
    if (!m_sceneVisual)
        return;

    m_sceneVisual->SetTransform(Math::Identity());
}

float Flip3DCompApp::ComputeFlatDepthRank(float slot, float enterProgress,
                                          int listIndex) const
{
    const float p = enterProgress;

    if (m_state == ViewState::Exit || m_state == ViewState::ExitRepeatedRotate)
        return slot;

    if (p < 1.0f - 1e-5f)
    {
        if (listIndex >= 0 && listIndex < (int)m_cards.size())
        {
            const CardModel& c = m_cards[(size_t)listIndex];
            if (m_selectedHwnd && c.m_hwnd == m_selectedHwnd)
                return -1.0f;
            return (float)c.m_initialCarouselIndex;
        }
    }

    return slot;
}

// ============================================================================
// Flip3DCompApp::ComputePaintKey
// DirectComposition has no depth buffer — sibling order must track carousel depth.
// Blending slot with desktop rank mid-exit causes keys to cross and flicker.
// ============================================================================
float Flip3DCompApp::ComputePaintKey(float slot, float enterProgress,
                                     int listIndex) const
{
    const float p = enterProgress;

    auto flatRank = [this, listIndex](int k) -> float
    {
        if (k >= 0 && k < (int)m_cards.size())
        {
            const CardModel& c = m_cards[(size_t)k];
            if (m_selectedHwnd && c.m_hwnd == m_selectedHwnd)
                return -1.0f;
            return (float)c.m_initialCarouselIndex;
        }
        return (float)listIndex;
    };

    if (m_state == ViewState::Exit || m_state == ViewState::ExitRepeatedRotate)
        return slot;

    if (m_state == ViewState::Enter)
        return p * slot + (1.0f - p) * flatRank(listIndex);

    return slot;
}

// ============================================================================
// Flip3DCompApp::UpdateCards
// ============================================================================
void Flip3DCompApp::UpdateCards(float enterProgress)
{
    if (!m_sceneVisual)
        return;

    const int N = (int)m_cards.size();
    if (N == 0)
        return;

    const float p      = enterProgress;
    const auto  camera = BuildCameraMatrix(p);
    const int   maxVis = kMaxVisibleCards;

    const bool scrollPath = (m_state == ViewState::Interactive);
    const bool exitRotate = (m_state == ViewState::ExitRepeatedRotate);
    if (scrollPath)
        SyncDisplaySlotsToList();

    struct CardDraw { CardModel* card; float slot; float opacity; float key; int listIndex; };
    std::vector<CardDraw> draws;
    draws.reserve((size_t)N);

    for (int k = 0; k < N; ++k)
    {
        CardModel& c = m_cards[(size_t)k];
        if (!c.m_containerVisual)
            continue;

        const float slot    = GetCardDisplaySlot(k);
        const float opacity = ComputeUpdateAlpha(c, p, slot);

        const float frontCull = exitRotate ? -2.0f
                              : (m_state == ViewState::Exit) ? -1.5f
                              :                                -1.0f;

        if (scrollPath)
        {
            const bool exitingFade = c.m_exitingFront && slot > -1.0f;
            if (!exitingFade && slot >= (float)maxVis)
            {
                c.m_containerVisual->SetVisible(FALSE);
                continue;
            }
            if (opacity < 0.01f)
            {
                c.m_containerVisual->SetVisible(FALSE);
                continue;
            }
        }
        else if (exitRotate)
        {
            if (!ShouldDrawRotationCard(k) && opacity < 0.01f)
            {
                c.m_containerVisual->SetVisible(FALSE);
                continue;
            }
            if (opacity < 0.01f)
            {
                c.m_containerVisual->SetVisible(FALSE);
                continue;
            }
        }
        else if (slot >= (float)maxVis
              || (slot <= frontCull && opacity < 0.01f))
        {
            c.m_containerVisual->SetVisible(FALSE);
            continue;
        }

        const float key = ComputePaintKey(slot, p, k);
        draws.push_back({ &c, slot, opacity, key, k });
    }

    for (int k = 0; k < N; ++k)
    {
        CardModel& c = m_cards[(size_t)k];
        if (!c.m_containerVisual)
            continue;
        bool listed = false;
        for (const auto& d : draws)
            if (d.card == &c) { listed = true; break; }
        if (!listed)
            c.m_containerVisual->SetVisible(FALSE);
    }

    std::sort(draws.begin(), draws.end(),
        [](const CardDraw& a, const CardDraw& b)
        {
            if (a.key != b.key)
                return a.key > b.key;
            return a.listIndex > b.listIndex;
        });

    std::vector<HWND> paintOrder;
    paintOrder.reserve(draws.size());
    for (const auto& d : draws)
        paintOrder.push_back(d.card->m_hwnd);

    const bool orderChanged = (paintOrder != m_lastPaintOrder);
    if (orderChanged)
    {
        for (auto& d : draws)
            m_sceneVisual->RemoveVisual(d.card->m_containerVisual.Get());
        m_lastPaintOrder = std::move(paintOrder);
    }

    IDCompositionVisual* prevVis = nullptr;
    for (auto& d : draws)
    {
        d.card->m_containerVisual->SetVisible(TRUE);
        d.card->m_containerVisual->SetOpacity(d.opacity);

        const float t = ComputeCarouselBezierT(d.slot);
        const float flatRank = ComputeFlatDepthRank(d.slot, p, d.listIndex);
        auto model = BuildModelMatrix(*d.card, t, p, flatRank);
        d.card->m_containerVisual->SetTransform(Math::Multiply(model, camera));

        if (orderChanged)
        {
            if (prevVis)
                m_sceneVisual->AddVisual(d.card->m_containerVisual.Get(), TRUE, prevVis);
            else
                m_sceneVisual->AddVisual(d.card->m_containerVisual.Get(), FALSE, nullptr);
            prevVis = d.card->m_containerVisual.Get();
        }
    }
}

// ============================================================================
// Flip3DCompApp::ComputeCarouselEdgeOpacity
// Continuous carousel edge roll-off used by the smooth-scroll path. Matches the
// uDWM steady-state alpha: full inside, 0.5 at the back boundary, fading toward
// the camera-side (slot < 0) as a card flies out the front.
// ============================================================================
float Flip3DCompApp::ComputeCarouselEdgeOpacity(float slot) const
{
    // Modern smooth-scroll touch: fade a card as it crosses the front plane so
    // the cyclic list-wrap is invisible. (uDWM relies on discrete steps here.)
    if (slot < 0.0f)
        return std::clamp(1.0f + slot, 0.0f, 1.0f);   // 0→1 over [-1, 0]

    // uDWM: the 0.5 (c_dFlipRotationPercent) back-edge hint and the cull beyond
    // the view only apply when there are more windows than fit
    // (AreAllQualifiedWindowsVisible == false). With everything in view → full.
    if ((int)m_cards.size() <= kMaxVisibleCards)
        return 1.0f;

    // Flip3DWindow::UpdateAlpha steady term: front maxView-1 windows full, the
    // backmost *visible* window (slot == maxView-1) is the 0.5 fade hint, and
    // windows past the view are culled by the caller. The 0.5→0 ramp over the
    // last slot smooths that discrete cull for the fractional-scroll path.
    const float Nf = (float)kMaxVisibleCards;
    if (slot >= Nf)
        return 0.0f;
    if (slot >= Nf - 2.0f)
        return (Nf - slot) / 2.0f;                     // slot 8→1.0, 9→0.5, 10→0
    return 1.0f;
}

// ============================================================================
// Flip3DCompApp::ComputeUpdateAlpha
// ============================================================================
float Flip3DCompApp::ComputeUpdateAlpha(const CardModel& card, float enterProgress,
                                        float carouselSlot) const
{
    float rotationOpacity = ComputeCarouselEdgeOpacity(carouselSlot);
    float enterScale      = 1.0f;

    if (m_state == ViewState::Exit
        && (int)m_cards.size() > kMaxVisibleCards
        && carouselSlot >= (float)kMaxVisibleCards - 1.0f)
    {
        rotationOpacity = kDesktopWashOpacityScale;
    }

    if (m_state == ViewState::ExitRepeatedRotate && m_rotateTimeline.IsActive())
    {
        const int idx = FindCardIndex(card.m_hwnd);
        if (idx >= 0)
            return ComputeRotationAlpha(card, enterProgress, idx, carouselSlot);
    }

    if (std::abs(enterProgress - 1.0f) >= 1e-6f)
    {
        if (std::abs(rotationOpacity - 1.0f) >= 1e-6f)
            rotationOpacity = enterProgress * rotationOpacity
                            + (1.0f - enterProgress);

        if (card.m_isMinimized)
            enterScale = enterProgress * enterProgress;
        else if (card.m_isShellDesktop)
            enterScale = enterProgress;
    }

    return std::clamp(rotationOpacity * enterScale, 0.0f, 1.0f);
}

// ============================================================================
// Flip3DCompApp::ComputeCarouselPathDenom
// uDWM UpdateModels: m_cWindows = max(count, 5), capped by max visible.
// ============================================================================
float Flip3DCompApp::ComputeCarouselPathDenom() const
{
    const int N = (int)m_cards.size();
    return (float)std::min(std::max(N, 5), kMaxVisibleCards);
}

// ============================================================================
// Flip3DCompApp::ComputeCarouselBezierT
// flip3d/uDWM: zIndex = -pathRelative; t = 1 - (zIndex - 0.5) / -m_cWindows
// (pathRelative == carousel slot: 0 = front, larger = further back)
// ============================================================================
float Flip3DCompApp::ComputeCarouselBezierT(float slot) const
{
    const float denom  = ComputeCarouselPathDenom();
    const float zIndex = -slot;
    // uDWM GetWorldFromParametric evaluates the quadratic path with NO clamp.
    // Windows past the view are culled by the caller, so in practice only the
    // back-edge card (t slightly < 0, against P0) and the front/outgoing card
    // (slot < 0 → t > 1, extending past P2 toward the camera) leave [0,1].
    return 1.0f - ((zIndex - 0.5f) / -denom);
}

// ============================================================================
// Flip3DCompApp::BuildViewMatrix
// ============================================================================
Matrix4x4 Flip3DCompApp::BuildViewMatrix(float p) const
{
    using namespace Math;

    float yaw   = kCameraFinalRotateY * p;
    float pitch = kCameraFinalRotateX * p;
    float roll  = kCameraFinalRotateZ * p;

    auto T1  = Translation(0, 0, 1);
    auto Rot = RotationRollPitchYaw(pitch, yaw, roll);
    auto T2  = Translation(0, 0, -1);
    auto T3  = Translation(-kCameraFinalTranslateX * p,
                            -kCameraFinalTranslateY * p,
                            -kCameraFinalTranslateZ * p);
    auto LA  = LookAtRH({0, 0, 1}, {0, 0, 0}, {0, 1, 0});

    auto V = T1;
    V = Multiply(V, Rot);
    V = Multiply(V, T2);
    V = Multiply(V, T3);
    V = Multiply(V, LA);
    return V;
}

// ============================================================================
// Flip3DCompApp::BuildProjMatrix
// ============================================================================
Matrix4x4 Flip3DCompApp::BuildProjMatrix(float /*aspect*/, float p) const
{
    float nearExtent = p * kNearPlaneEdgeSize + (1.0f - p);
    return Math::PerspectiveRH(nearExtent, 1.0f, kNearPlaneDistance, 50.0f);
}

// ============================================================================
// Flip3DCompApp::BuildModelMatrix
// uDWM UpdateModels / flip3d GetWorldFromParametric: carousel pathRelative for
// both 3D bezier and flatZ; lerp to desktop when enterProgress != 1.
// ============================================================================
Matrix4x4 Flip3DCompApp::BuildModelMatrix(const CardModel& c, float t,
                                          float enterProgress,
                                          float flatDepthRank) const
{
    using namespace Math;

    Vec3  cv     = Bezier(t);
    float fullW  = std::abs(c.m_targetSize.x);
    float fullH  = std::abs(c.m_targetSize.y);

    Vec3 pos3d = { cv.x, cv.y + fullH, cv.z };

    float p = std::clamp(enterProgress, 0.0f, 1.0f);
    const float flatZ = (-flatDepthRank) / 10000.0f;

    float tx, ty, tz, worldW, worldH;

    if (p >= 1.0f - 1e-5f)
    {
        tx = pos3d.x;  ty = pos3d.y;  tz = pos3d.z;
        worldW = fullW;
        worldH = fullH;
    }
    else if (p <= 1e-5f)
    {
        tx = c.m_flatPos.x;
        ty = c.m_flatPos.y;
        tz = flatZ;
        worldW = c.m_flatSize.x;
        worldH = c.m_flatSize.y;
    }
    else
    {
        Vec3 pos2d = { c.m_flatPos.x, c.m_flatPos.y, flatZ };
        tx     = Lerp(pos2d.x, pos3d.x, p);
        ty     = Lerp(pos2d.y, pos3d.y, p);
        tz     = Lerp(pos2d.z, pos3d.z, p);
        worldW = Lerp(c.m_flatSize.x, fullW, p);
        worldH = Lerp(c.m_flatSize.y, fullH, p);
    }

    float sx =  worldW / (float)std::max(c.m_srcWidth,  1);
    float sy = -worldH / (float)std::max(c.m_srcHeight, 1);

    auto S = Scale(sx, sy, 1.0f);
    auto T = Translation(tx, ty, tz);
    auto M = Multiply(S, T);

    if (m_rtl)
    {
        auto Mirror = Multiply(
            Scale(-1.0f, 1.0f, 1.0f),
            Translation((float)c.m_srcWidth, 0.0f, 0.0f));
        M = Multiply(Mirror, M);
    }

    return M;
}
