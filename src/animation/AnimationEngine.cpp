#include <eternal/animation/AnimationEngine.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eternal {

// ============================================================================
// Task 46: Core animation manager and tick loop
// ============================================================================

AnimationEngine::AnimationEngine() {
    // Pre-allocate pool slots
    m_pool.reserve(64);
    m_animations.reserve(64);
}

AnimationEngine::~AnimationEngine() = default;

EasingFunction AnimationEngine::makeEasing(const std::string& curveName) {
    BezierCurve bezier = m_curveManager.get(curveName);
    return [bezier](float t) -> float {
        return bezier.evaluate(t);
    };
}

Animation& AnimationEngine::acquireAnimation() {
    // Try to reclaim from pool
    if (!m_pool.empty()) {
        m_animations.push_back(std::move(m_pool.back()));
        m_pool.pop_back();
        auto& anim = m_animations.back();
        anim.reset();
        return anim;
    }
    m_animations.emplace_back();
    return m_animations.back();
}

void AnimationEngine::recycleAnimation(Animation& anim) {
    anim.reset();
    anim.markPooled();
    m_pool.push_back(std::move(anim));
}

AnimationID AnimationEngine::allocateId() {
    return m_nextId++;
}

AnimationID AnimationEngine::create(AnimationID id, float from, float to,
                                    float duration_ms, const std::string& curve) {
    return create(id, from, to, duration_ms, curve, AnimationPriority::Normal);
}

AnimationID AnimationEngine::create(AnimationID id, float from, float to,
                                    float duration_ms, const std::string& curve,
                                    AnimationPriority priority) {
    AnimationID assignedId = (id != 0) ? id : allocateId();

    // Cancel any existing animation with same ID (respecting priority)
    for (auto& anim : m_animations) {
        if (anim.getId() == assignedId && !anim.isFinished()) {
            if (anim.getPriority() > priority) {
                // Existing animation has higher priority; reject
                return 0;
            }
            anim.reset(); // Will be cleaned up
        }
    }

    // Remove stale entries with same ID
    std::erase_if(m_animations, [assignedId](const Animation& a) {
        return a.getId() == assignedId && a.getId() != 0 &&
               a.getState() != AnimationState::Running;
    });

    EasingFunction easing = makeEasing(curve);
    Animation newAnim(assignedId, from, to, duration_ms, curve, std::move(easing));
    newAnim.setPriority(priority);
    m_animations.push_back(std::move(newAnim));

    return assignedId;
}

void AnimationEngine::cancel(AnimationID id) {
    for (auto it = m_animations.begin(); it != m_animations.end(); ++it) {
        if (it->getId() == id && !it->isFinished()) {
            // Respect priority: high priority animations can't be cancelled
            if (it->getPriority() == AnimationPriority::High) {
                return;
            }
            recycleAnimation(*it);
            m_animations.erase(it);
            return;
        }
    }
}

void AnimationEngine::forceCancel(AnimationID id) {
    for (auto it = m_animations.begin(); it != m_animations.end(); ++it) {
        if (it->getId() == id) {
            recycleAnimation(*it);
            m_animations.erase(it);
            return;
        }
    }
}

void AnimationEngine::update(float delta_ms) {
    if (!m_enabled)
        return;

    float adjusted = delta_ms * m_globalSpeed;

    // Update all active animations
    for (auto& anim : m_animations) {
        anim.update(adjusted);
    }

    // Update per-window shader time accumulators
    for (auto& [surface, config] : m_windowShaders) {
        if (config.enabled) {
            config.time += delta_ms / 1000.0f;
        }
    }

    // Update kinetic scroll physics
    updateKineticScroll(adjusted);

    // Automatic cleanup: recycle finished animations, remove from active list
    auto it = m_animations.begin();
    while (it != m_animations.end()) {
        if (it->isFinished()) {
            recycleAnimation(*it);
            it = m_animations.erase(it);
        } else {
            ++it;
        }
    }

    // Clean up finished ghost surfaces
    cleanupGhosts();

    // Clean up finished fullscreen transitions
    for (auto fsIt = m_fsTransitions.begin(); fsIt != m_fsTransitions.end(); ) {
        if (!fsIt->second.active) {
            fsIt = m_fsTransitions.erase(fsIt);
        } else {
            ++fsIt;
        }
    }

    // Clean up finished geometry animations
    for (auto gIt = m_geometryAnims.begin(); gIt != m_geometryAnims.end(); ) {
        if (!gIt->second.active) {
            gIt = m_geometryAnims.erase(gIt);
        } else {
            ++gIt;
        }
    }
}

bool AnimationEngine::isAnimating(AnimationID id) const {
    for (const auto& a : m_animations) {
        if (a.getId() == id && !a.isFinished())
            return true;
    }
    return false;
}

float AnimationEngine::getValue(AnimationID id) const {
    for (const auto& a : m_animations) {
        if (a.getId() == id)
            return a.getValue();
    }
    return 0.0f;
}

Animation* AnimationEngine::getAnimation(AnimationID id) {
    for (auto& a : m_animations) {
        if (a.getId() == id)
            return &a;
    }
    return nullptr;
}

const Animation* AnimationEngine::getAnimation(AnimationID id) const {
    for (const auto& a : m_animations) {
        if (a.getId() == id)
            return &a;
    }
    return nullptr;
}

std::size_t AnimationEngine::getActiveCount() const {
    std::size_t count = 0;
    for (const auto& a : m_animations) {
        if (!a.isFinished())
            ++count;
    }
    return count;
}

void AnimationEngine::setGlobalSpeed(float speed) {
    m_globalSpeed = std::max(0.0f, speed);
}

void AnimationEngine::setEnabled(bool enabled) {
    m_enabled = enabled;
}

// ============================================================================
// Task 48: Window open/close animations
// ============================================================================

void AnimationEngine::setWindowAnimConfig(const WindowAnimConfig& config) {
    m_windowAnimConfig = config;
}

void AnimationEngine::setWindowAnimStyleOverride(Surface* surface, WindowAnimStyle style) {
    if (surface)
        m_windowAnimOverrides[surface] = style;
}

WindowAnimStyle AnimationEngine::resolveOpenStyle(Surface* surface) const {
    auto it = m_windowAnimOverrides.find(surface);
    if (it != m_windowAnimOverrides.end())
        return it->second;
    return m_windowAnimConfig.openStyle;
}

WindowAnimStyle AnimationEngine::resolveCloseStyle(Surface* surface) const {
    auto it = m_windowAnimOverrides.find(surface);
    if (it != m_windowAnimOverrides.end())
        return it->second;
    return m_windowAnimConfig.closeStyle;
}

AnimationID AnimationEngine::animateWindowOpen(Surface* surface,
                                                int x, int y, int w, int h) {
    if (!surface) return 0;

    WindowAnimStyle style = resolveOpenStyle(surface);
    if (style == WindowAnimStyle::None) {
        surface->setGeometry(x, y, w, h);
        surface->setOpacity(1.0f);
        return 0;
    }

    float duration = m_windowAnimConfig.openDuration_ms;
    const auto& curve = m_windowAnimConfig.openCurve;

    // Avoid resizing the client every frame during the open effect. That
    // causes configure churn and unstable startup for real toplevels.
    const int targetY = y;
    int initialY = y;

    // Group animation ID for the whole open sequence
    AnimationID groupId = allocateId();

    // All styles animate opacity
    AnimationID opacityId = allocateId();
    float opacityFrom = 0.0f;
    float opacityTo = 1.0f;

    // Position offset animation
    AnimationID slideId = allocateId();
    float slideFrom = 0.0f;

    switch (style) {
    case WindowAnimStyle::Slide:
        slideFrom = static_cast<float>(h) * 0.3f; // Slide up from below
        break;
    case WindowAnimStyle::Fade:
        break;
    case WindowAnimStyle::Zoom:
        opacityFrom = 0.5f;
        break;
    case WindowAnimStyle::PopIn:
        slideFrom = static_cast<float>(h) * 0.08f;
        break;
    case WindowAnimStyle::SlideFade:
        slideFrom = static_cast<float>(h) * 0.15f;
        break;
    case WindowAnimStyle::None:
        return 0;
    }

    if (style == WindowAnimStyle::Zoom) {
        slideFrom = static_cast<float>(h) * 0.04f;
    }

    initialY = y + static_cast<int>(slideFrom);
    surface->setOpacity(opacityFrom);
    surface->setGeometry(x, initialY, w, h);

    // Create opacity animation
    {
        AnimationID id = create(opacityId, opacityFrom, opacityTo, duration, curve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surface](float val) {
                if (surface) surface->setOpacity(val);
            };
        }
    }

    // Create slide animation if needed
    if (std::fabs(slideFrom) > 0.01f) {
        AnimationID id = create(slideId, static_cast<float>(initialY),
                                static_cast<float>(targetY), duration, curve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surface, x, w, h](float currentY) {
                if (!surface) return;
                surface->setGeometry(x, static_cast<int>(currentY), w, h);
            };
        }
    }

    return groupId;
}

AnimationID AnimationEngine::animateWindowClose(Surface* surface,
                                                 int x, int y, int w, int h) {
    if (!surface) return 0;

    WindowAnimStyle style = resolveCloseStyle(surface);
    if (style == WindowAnimStyle::None) {
        return 0;
    }

    float duration = m_windowAnimConfig.closeDuration_ms;
    const auto& curve = m_windowAnimConfig.closeCurve;
    AnimationID groupId = allocateId();

    // Create ghost surface for rendering during close animation
    GhostSurface ghost;
    ghost.animId = groupId;
    ghost.originalSurface = surface;
    ghost.x = x;
    ghost.y = y;
    ghost.width = w;
    ghost.height = h;
    ghost.opacity = 1.0f;
    ghost.scaleX = 1.0f;
    ghost.scaleY = 1.0f;
    ghost.offsetX = 0.0f;
    ghost.offsetY = 0.0f;
    ghost.finished = false;

    m_ghosts.push_back(ghost);
    AnimationID ghostAnimId = groupId;

    // Opacity: fade out
    AnimationID opacityId = allocateId();
    {
        AnimationID id = create(opacityId, 1.0f, 0.0f, duration, curve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [this, ghostAnimId](float val) {
                for (auto& g : m_ghosts) {
                    if (g.animId == ghostAnimId) { g.opacity = val; break; }
                }
            };
        }
    }

    // Scale: shrink
    float scaleTarget = 0.8f;
    if (style == WindowAnimStyle::Zoom) scaleTarget = 0.0f;
    if (style == WindowAnimStyle::Fade) scaleTarget = 1.0f;
    if (style == WindowAnimStyle::PopIn) scaleTarget = 0.6f;

    AnimationID scaleId = allocateId();
    {
        const std::string& scaleCurve = (style == WindowAnimStyle::PopIn) ? "overshot" : curve;
        AnimationID id = create(scaleId, 1.0f, scaleTarget, duration, scaleCurve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [this, ghostAnimId](float scale) {
                for (auto& g : m_ghosts) {
                    if (g.animId == ghostAnimId) {
                        g.scaleX = scale;
                        g.scaleY = scale;
                        break;
                    }
                }
            };
            anim->onComplete = [this, ghostAnimId]() {
                for (auto& g : m_ghosts) {
                    if (g.animId == ghostAnimId) { g.finished = true; break; }
                }
            };
        }
    }

    // Slide: move down
    if (style == WindowAnimStyle::Slide || style == WindowAnimStyle::SlideFade) {
        float slideTarget = static_cast<float>(h) * 0.15f;
        if (style == WindowAnimStyle::Slide) slideTarget = static_cast<float>(h) * 0.3f;

        AnimationID slideId = allocateId();
        AnimationID id = create(slideId, 0.0f, slideTarget, duration, curve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [this, ghostAnimId](float offset) {
                for (auto& g : m_ghosts) {
                    if (g.animId == ghostAnimId) { g.offsetY = offset; break; }
                }
            };
        }
    }

    return groupId;
}

const std::vector<GhostSurface>& AnimationEngine::getGhostSurfaces() const {
    return m_ghosts;
}

void AnimationEngine::cleanupGhosts() {
    std::erase_if(m_ghosts, [](const GhostSurface& g) {
        return g.finished;
    });
}

// ============================================================================
// Task 49: Window move/resize smooth animation
// ============================================================================

void AnimationEngine::setMoveResizeConfig(const MoveResizeConfig& config) {
    m_moveResizeConfig = config;
}

void AnimationEngine::animateGeometry(Surface* surface,
                                       int newX, int newY, int newW, int newH) {
    if (!surface) return;

    if (!m_moveResizeConfig.enableMoveAnim && !m_moveResizeConfig.enableResizeAnim) {
        surface->setGeometry(newX, newY, newW, newH);
        return;
    }

    auto& ga = m_geometryAnims[surface];

    // Get current geometry (either animated or actual)
    auto currentGeo = surface->getGeometry();
    int curX = currentGeo.x;
    int curY = currentGeo.y;
    int curW = currentGeo.width;
    int curH = currentGeo.height;

    // If we already have an active animation, use animated current values
    if (ga.active) {
        // Get current animated values from running animations
        if (isAnimating(ga.xAnim)) curX = static_cast<int>(getValue(ga.xAnim));
        if (isAnimating(ga.yAnim)) curY = static_cast<int>(getValue(ga.yAnim));
        if (isAnimating(ga.wAnim)) curW = static_cast<int>(getValue(ga.wAnim));
        if (isAnimating(ga.hAnim)) curH = static_cast<int>(getValue(ga.hAnim));

        // Cancel old animations
        forceCancel(ga.xAnim);
        forceCancel(ga.yAnim);
        forceCancel(ga.wAnim);
        forceCancel(ga.hAnim);
    }

    ga.surface = surface;
    ga.targetX = newX;
    ga.targetY = newY;
    ga.targetW = newW;
    ga.targetH = newH;
    ga.active = true;

    const auto& moveCurve = m_moveResizeConfig.moveCurve;
    const auto& resizeCurve = m_moveResizeConfig.resizeCurve;
    float moveDur = m_moveResizeConfig.moveDuration_ms;
    float resizeDur = m_moveResizeConfig.resizeDuration_ms;

    [[maybe_unused]] auto updateGeo = [surface, &ga, this]() {
        if (!surface) return;
        int ax = isAnimating(ga.xAnim) ? static_cast<int>(getValue(ga.xAnim)) : ga.targetX;
        int ay = isAnimating(ga.yAnim) ? static_cast<int>(getValue(ga.yAnim)) : ga.targetY;
        int aw = isAnimating(ga.wAnim) ? static_cast<int>(getValue(ga.wAnim)) : ga.targetW;
        int ah = isAnimating(ga.hAnim) ? static_cast<int>(getValue(ga.hAnim)) : ga.targetH;
        surface->setGeometry(ax, ay, aw, ah);
    };

    Surface* surfPtr = surface;
    auto markGeometryAnimationComplete = [this, surfPtr]() {
        auto gIt = m_geometryAnims.find(surfPtr);
        if (gIt == m_geometryAnims.end()) {
            return;
        }

        auto& g = gIt->second;
        if (!isAnimating(g.xAnim) && !isAnimating(g.yAnim) &&
            !isAnimating(g.wAnim) && !isAnimating(g.hAnim)) {
            g.active = false;
        }
    };

    // Animate X
    if (m_moveResizeConfig.enableMoveAnim && curX != newX) {
        ga.xAnim = allocateId();
        AnimationID id = create(ga.xAnim, static_cast<float>(curX),
                                static_cast<float>(newX), moveDur, moveCurve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surfPtr, this, &ga = m_geometryAnims[surfPtr]](float val) {
                if (!surfPtr) return;
                auto geo = surfPtr->getGeometry();
                surfPtr->setGeometry(static_cast<int>(val), geo.y, geo.width, geo.height);
            };
            anim->onComplete = markGeometryAnimationComplete;
        }
    } else {
        ga.xAnim = 0;
    }

    // Animate Y
    if (m_moveResizeConfig.enableMoveAnim && curY != newY) {
        ga.yAnim = allocateId();
        AnimationID id = create(ga.yAnim, static_cast<float>(curY),
                                static_cast<float>(newY), moveDur, moveCurve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surfPtr](float val) {
                if (!surfPtr) return;
                auto geo = surfPtr->getGeometry();
                surfPtr->setGeometry(geo.x, static_cast<int>(val), geo.width, geo.height);
            };
            anim->onComplete = markGeometryAnimationComplete;
        }
    } else {
        ga.yAnim = 0;
    }

    // Animate Width
    if (m_moveResizeConfig.enableResizeAnim && curW != newW) {
        ga.wAnim = allocateId();
        AnimationID id = create(ga.wAnim, static_cast<float>(curW),
                                static_cast<float>(newW), resizeDur, resizeCurve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surfPtr](float val) {
                if (!surfPtr) return;
                auto geo = surfPtr->getGeometry();
                surfPtr->setGeometry(geo.x, geo.y, static_cast<int>(val), geo.height);
            };
            anim->onComplete = markGeometryAnimationComplete;
        }
    } else {
        ga.wAnim = 0;
    }

    // Animate Height
    if (m_moveResizeConfig.enableResizeAnim && curH != newH) {
        ga.hAnim = allocateId();
        AnimationID id = create(ga.hAnim, static_cast<float>(curH),
                                static_cast<float>(newH), resizeDur, resizeCurve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [surfPtr](float val) {
                if (!surfPtr) return;
                auto geo = surfPtr->getGeometry();
                surfPtr->setGeometry(geo.x, geo.y, geo.width, static_cast<int>(val));
            };
            // Mark geometry animation complete when last anim finishes
            anim->onComplete = [surfPtr, this]() {
                auto gIt = m_geometryAnims.find(surfPtr);
                if (gIt != m_geometryAnims.end()) {
                    auto& g = gIt->second;
                    if (!isAnimating(g.xAnim) && !isAnimating(g.yAnim) &&
                        !isAnimating(g.wAnim) && !isAnimating(g.hAnim)) {
                        g.active = false;
                    }
                }
            };
        }
    } else {
        ga.hAnim = 0;
    }

    // If no animations were actually needed
    if (ga.xAnim == 0 && ga.yAnim == 0 && ga.wAnim == 0 && ga.hAnim == 0) {
        surface->setGeometry(newX, newY, newW, newH);
        ga.active = false;
        return;
    }
}

void AnimationEngine::setGeometryImmediate(Surface* surface,
                                            int x, int y, int w, int h) {
    if (!surface) return;
    cancelGeometryAnimation(surface);
    surface->setGeometry(x, y, w, h);
}

void AnimationEngine::cancelGeometryAnimation(Surface* surface) {
    auto it = m_geometryAnims.find(surface);
    if (it != m_geometryAnims.end()) {
        auto& ga = it->second;
        forceCancel(ga.xAnim);
        forceCancel(ga.yAnim);
        forceCancel(ga.wAnim);
        forceCancel(ga.hAnim);
        ga.active = false;
        m_geometryAnims.erase(it);
    }
}

bool AnimationEngine::hasGeometryAnimation(Surface* surface) const {
    auto it = m_geometryAnims.find(surface);
    return it != m_geometryAnims.end() && it->second.active;
}

std::optional<SurfaceBox> AnimationEngine::getAnimatedGeometry(Surface* surface) const {
    auto it = m_geometryAnims.find(surface);
    if (it == m_geometryAnims.end() || !it->second.active)
        return std::nullopt;

    const auto& ga = it->second;
    SurfaceBox box;
    box.x = isAnimating(ga.xAnim) ? static_cast<int>(getValue(ga.xAnim)) : ga.targetX;
    box.y = isAnimating(ga.yAnim) ? static_cast<int>(getValue(ga.yAnim)) : ga.targetY;
    box.width  = isAnimating(ga.wAnim) ? static_cast<int>(getValue(ga.wAnim)) : ga.targetW;
    box.height = isAnimating(ga.hAnim) ? static_cast<int>(getValue(ga.hAnim)) : ga.targetH;
    return box;
}

// ============================================================================
// Task 50: Workspace transition animations
// ============================================================================

void AnimationEngine::setWorkspaceTransitionConfig(const WorkspaceTransitionConfig& config) {
    m_wsTransConfig = config;
}

void AnimationEngine::startWorkspaceTransition(int fromWs, int toWs, int direction) {
    if (m_wsTransition.active) {
        forceCancel(m_wsTransition.animId);
    }

    m_wsTransition.fromWorkspace = fromWs;
    m_wsTransition.toWorkspace = toWs;
    m_wsTransition.direction = direction;
    m_wsTransition.progress = 0.0f;
    m_wsTransition.gestureActive = false;
    m_wsTransition.active = true;

    m_wsTransition.animId = allocateId();
    AnimationID id = create(m_wsTransition.animId, 0.0f, 1.0f,
                            m_wsTransConfig.duration_ms, m_wsTransConfig.curve,
                            AnimationPriority::High);
    if (auto* anim = getAnimation(id)) {
        anim->onUpdate = [this](float val) {
            m_wsTransition.progress = val;
        };
        anim->onComplete = [this]() {
            m_wsTransition.progress = 1.0f;
            m_wsTransition.active = false;
        };
    }
}

void AnimationEngine::startWorkspaceGesture(int fromWs, int direction) {
    if (m_wsTransition.active && !m_wsTransition.gestureActive) {
        forceCancel(m_wsTransition.animId);
    }

    m_wsTransition.fromWorkspace = fromWs;
    m_wsTransition.toWorkspace = fromWs + direction;
    m_wsTransition.direction = direction;
    m_wsTransition.progress = 0.0f;
    m_wsTransition.gestureActive = true;
    m_wsTransition.active = true;
    m_wsTransition.animId = 0;
}

void AnimationEngine::updateWorkspaceGesture(float delta) {
    if (!m_wsTransition.gestureActive) return;

    // Direct 1:1 mapping of gesture delta to progress
    m_wsTransition.progress += delta;
    m_wsTransition.progress = std::clamp(m_wsTransition.progress, -1.0f, 1.0f);
}

bool AnimationEngine::endWorkspaceGesture() {
    if (!m_wsTransition.gestureActive) return false;

    m_wsTransition.gestureActive = false;
    float progress = std::fabs(m_wsTransition.progress);

    if (progress >= m_wsTransConfig.gestureThreshold) {
        // Commit: animate remaining progress to 1.0
        float currentProgress = m_wsTransition.progress;
        float target = (currentProgress >= 0.0f) ? 1.0f : -1.0f;
        float remaining = std::fabs(target - currentProgress);
        float duration = m_wsTransConfig.duration_ms * remaining;
        duration = std::max(duration, 50.0f);

        m_wsTransition.animId = allocateId();
        AnimationID id = create(m_wsTransition.animId,
                                currentProgress, target,
                                duration, m_wsTransConfig.curve,
                                AnimationPriority::High);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [this](float val) {
                m_wsTransition.progress = val;
            };
            anim->onComplete = [this]() {
                m_wsTransition.progress = (m_wsTransition.direction > 0) ? 1.0f : -1.0f;
                m_wsTransition.active = false;
            };
        }
        return true;
    } else {
        // Cancel: snap back to 0
        float currentProgress = m_wsTransition.progress;
        float duration = m_wsTransConfig.duration_ms * std::fabs(currentProgress);
        duration = std::max(duration, 50.0f);

        m_wsTransition.animId = allocateId();
        AnimationID id = create(m_wsTransition.animId,
                                currentProgress, 0.0f,
                                duration, m_wsTransConfig.curve);
        if (auto* anim = getAnimation(id)) {
            anim->onUpdate = [this](float val) {
                m_wsTransition.progress = val;
            };
            anim->onComplete = [this]() {
                m_wsTransition.progress = 0.0f;
                m_wsTransition.active = false;
            };
        }
        return false;
    }
}

const WorkspaceTransition& AnimationEngine::getWorkspaceTransition() const {
    return m_wsTransition;
}

bool AnimationEngine::isWorkspaceTransitionActive() const {
    return m_wsTransition.active;
}

// ============================================================================
// Task 51: Fullscreen crossfade transitions
// ============================================================================

void AnimationEngine::setFullscreenTransitionConfig(const FullscreenTransitionConfig& config) {
    m_fsTransConfig = config;
}

void AnimationEngine::startFullscreenTransition(Surface* surface,
                                                 int fromX, int fromY, int fromW, int fromH,
                                                 int toX, int toY, int toW, int toH) {
    if (!surface) return;

    // Cancel existing transition for this surface
    auto it = m_fsTransitions.find(surface);
    if (it != m_fsTransitions.end() && it->second.active) {
        forceCancel(it->second.animId);
    }

    FullscreenTransition fst;
    fst.surface = surface;
    fst.fromX = fromX; fst.fromY = fromY;
    fst.fromW = fromW; fst.fromH = fromH;
    fst.toX = toX; fst.toY = toY;
    fst.toW = toW; fst.toH = toH;
    fst.progress = 0.0f;
    fst.active = true;

    fst.animId = allocateId();
    AnimationID id = create(fst.animId, 0.0f, 1.0f,
                            m_fsTransConfig.duration_ms, m_fsTransConfig.curve);

    m_fsTransitions[surface] = fst;

    if (auto* anim = getAnimation(id)) {
        anim->onUpdate = [surface, this](float t) {
            auto fsIt = m_fsTransitions.find(surface);
            if (fsIt == m_fsTransitions.end()) return;
            auto& fs = fsIt->second;
            fs.progress = t;

            // Interpolate geometry smoothly between old and new states
            int cx = fs.fromX + static_cast<int>((fs.toX - fs.fromX) * t);
            int cy = fs.fromY + static_cast<int>((fs.toY - fs.fromY) * t);
            int cw = fs.fromW + static_cast<int>((fs.toW - fs.fromW) * t);
            int ch = fs.fromH + static_cast<int>((fs.toH - fs.fromH) * t);

            surface->setGeometry(cx, cy, cw, ch);
        };
        anim->onComplete = [surface, this]() {
            auto fsIt = m_fsTransitions.find(surface);
            if (fsIt != m_fsTransitions.end()) {
                fsIt->second.progress = 1.0f;
                fsIt->second.active = false;
            }
        };
    }
}

const FullscreenTransition* AnimationEngine::getFullscreenTransition(Surface* surface) const {
    auto it = m_fsTransitions.find(surface);
    if (it != m_fsTransitions.end() && it->second.active)
        return &it->second;
    return nullptr;
}

// ============================================================================
// Task 52: Kinetic scrolling for niri strip
// ============================================================================

void AnimationEngine::setKineticScrollConfig(const KineticScrollConfig& config) {
    m_kineticConfig = config;
    m_kineticState.snapSpring.setStiffness(config.snapStiffness);
    m_kineticState.snapSpring.setDamping(config.snapDamping);
}

void AnimationEngine::kineticScrollBegin() {
    m_kineticState.flinging = false;
    m_kineticState.snapping = false;
    m_kineticState.rubberBanding = false;
    m_kineticState.velocity = 0.0f;
}

void AnimationEngine::kineticScrollUpdate(float delta) {
    // During active gesture, track velocity for momentum
    // Mix new delta into velocity with exponential smoothing
    float alpha = 0.6f;  // smoothing factor
    float instantVelocity = delta * 1000.0f;  // Convert to px/s (assuming 1ms frame)
    m_kineticState.velocity = alpha * instantVelocity +
                               (1.0f - alpha) * m_kineticState.velocity;

    m_kineticState.position += delta;

    // Check bounds for rubber-banding
    if (m_kineticState.position < m_kineticState.minBound) {
        m_kineticState.rubberBanding = true;
        float overscroll = m_kineticState.minBound - m_kineticState.position;
        // Resistance increases as you pull further
        float resistance = 1.0f / (1.0f + overscroll / m_kineticConfig.rubberBandMaxOverscroll);
        m_kineticState.position = m_kineticState.minBound - overscroll * resistance;
        m_kineticState.rubberBandOffset = m_kineticState.minBound - m_kineticState.position;
    } else if (m_kineticState.position > m_kineticState.maxBound) {
        m_kineticState.rubberBanding = true;
        float overscroll = m_kineticState.position - m_kineticState.maxBound;
        float resistance = 1.0f / (1.0f + overscroll / m_kineticConfig.rubberBandMaxOverscroll);
        m_kineticState.position = m_kineticState.maxBound + overscroll * resistance;
        m_kineticState.rubberBandOffset = m_kineticState.position - m_kineticState.maxBound;
    } else {
        m_kineticState.rubberBanding = false;
        m_kineticState.rubberBandOffset = 0.0f;
    }
}

void AnimationEngine::kineticScrollEnd() {
    // If we're past bounds, snap back with spring
    if (m_kineticState.rubberBanding) {
        m_kineticState.flinging = false;
        m_kineticState.snapping = true;
        float target = std::clamp(m_kineticState.position,
                                   m_kineticState.minBound,
                                   m_kineticState.maxBound);
        m_kineticState.snapSpring.setPosition(m_kineticState.position);
        m_kineticState.snapSpring.setVelocity(m_kineticState.velocity);
        m_kineticState.snapSpring.setTarget(target);
        m_kineticState.velocity = 0.0f;
        return;
    }

    // If velocity is significant, begin kinetic fling
    if (std::fabs(m_kineticState.velocity) > m_kineticConfig.minVelocity * 10.0f) {
        m_kineticState.flinging = true;
        m_kineticState.snapping = false;
    } else {
        // Low velocity: snap to nearest column
        m_kineticState.flinging = false;
        m_kineticState.snapping = true;
        float target = findNearestSnapTarget(m_kineticState.position);
        m_kineticState.snapSpring.setPosition(m_kineticState.position);
        m_kineticState.snapSpring.setVelocity(m_kineticState.velocity);
        m_kineticState.snapSpring.setTarget(target);
        m_kineticState.velocity = 0.0f;
    }
}

void AnimationEngine::updateKineticScroll(float delta_ms) {
    if (!m_kineticState.flinging && !m_kineticState.snapping)
        return;

    if (m_kineticState.flinging) {
        // Apply deceleration curve
        float decelFactor = std::pow(m_kineticConfig.deceleration, delta_ms);
        m_kineticState.velocity *= decelFactor;
        m_kineticState.position += m_kineticState.velocity * (delta_ms / 1000.0f);

        // Check if we hit boundaries
        if (m_kineticState.position < m_kineticState.minBound ||
            m_kineticState.position > m_kineticState.maxBound) {
            applyRubberBand(delta_ms);
        }

        // Check if velocity is below threshold
        if (std::fabs(m_kineticState.velocity) < m_kineticConfig.minVelocity) {
            // Transition to snap
            m_kineticState.flinging = false;
            m_kineticState.snapping = true;
            float target = findNearestSnapTarget(m_kineticState.position);
            m_kineticState.snapSpring.setPosition(m_kineticState.position);
            m_kineticState.snapSpring.setVelocity(m_kineticState.velocity);
            m_kineticState.snapSpring.setTarget(target);
            m_kineticState.velocity = 0.0f;
        }
    }

    if (m_kineticState.snapping) {
        m_kineticState.snapSpring.update(delta_ms);
        m_kineticState.position = m_kineticState.snapSpring.getPosition();

        if (m_kineticState.snapSpring.isAtRest()) {
            m_kineticState.position = m_kineticState.snapSpring.getTarget();
            m_kineticState.snapping = false;
        }
    }
}

void AnimationEngine::setScrollBounds(float minBound, float maxBound) {
    m_kineticState.minBound = minBound;
    m_kineticState.maxBound = maxBound;
}

void AnimationEngine::setScrollSnapTargets(const std::vector<float>& targets) {
    m_scrollSnapTargets = targets;
}

float AnimationEngine::getKineticScrollPosition() const {
    return m_kineticState.position;
}

float AnimationEngine::getKineticScrollVelocity() const {
    return m_kineticState.velocity;
}

bool AnimationEngine::isKineticScrollActive() const {
    return m_kineticState.flinging || m_kineticState.snapping;
}

float AnimationEngine::findNearestSnapTarget(float position) const {
    if (m_scrollSnapTargets.empty())
        return std::clamp(position, m_kineticState.minBound, m_kineticState.maxBound);

    float best = m_scrollSnapTargets[0];
    float bestDist = std::fabs(position - best);

    for (size_t i = 1; i < m_scrollSnapTargets.size(); ++i) {
        float dist = std::fabs(position - m_scrollSnapTargets[i]);
        if (dist < bestDist) {
            bestDist = dist;
            best = m_scrollSnapTargets[i];
        }
    }
    return best;
}

void AnimationEngine::applyRubberBand(float /*delta_ms*/) {
    // Clamp with rubber-band effect: exponentially resist further scrolling
    if (m_kineticState.position < m_kineticState.minBound) {
        float overscroll = m_kineticState.minBound - m_kineticState.position;
        // Apply spring force back to boundary
        m_kineticState.velocity += overscroll * m_kineticConfig.rubberBandStiffness;
        // Hard clamp the maximum overscroll
        if (overscroll > m_kineticConfig.rubberBandMaxOverscroll) {
            m_kineticState.position = m_kineticState.minBound -
                                       m_kineticConfig.rubberBandMaxOverscroll;
        }
    } else if (m_kineticState.position > m_kineticState.maxBound) {
        float overscroll = m_kineticState.position - m_kineticState.maxBound;
        m_kineticState.velocity -= overscroll * m_kineticConfig.rubberBandStiffness;
        if (overscroll > m_kineticConfig.rubberBandMaxOverscroll) {
            m_kineticState.position = m_kineticState.maxBound +
                                       m_kineticConfig.rubberBandMaxOverscroll;
        }
    }
}

// ============================================================================
// Task 53: Gesture 1:1 animation tracking
// ============================================================================

AnimationID AnimationEngine::startGestureAnimation(float startValue, float endValue,
                                                     float cancelThreshold) {
    AnimationID id = allocateId();

    GestureAnimationState state;
    state.gestureProgress = 0.0f;
    state.gestureActive = true;
    state.completionAnim = 0;
    state.startValue = startValue;
    state.endValue = endValue;
    state.cancelThreshold = cancelThreshold;

    m_gestureAnims[id] = state;
    return id;
}

void AnimationEngine::updateGestureAnimation(AnimationID id, float progress) {
    auto it = m_gestureAnims.find(id);
    if (it == m_gestureAnims.end() || !it->second.gestureActive)
        return;

    auto& state = it->second;
    state.gestureProgress = std::clamp(progress, 0.0f, 1.0f);

    // Direct 1:1 mapping, no easing during active gesture
    float value = state.startValue +
                  (state.endValue - state.startValue) * state.gestureProgress;

    if (state.onProgress)
        state.onProgress(value);
}

bool AnimationEngine::endGestureAnimation(AnimationID id,
                                            const std::string& completionCurve,
                                            float completionDuration_ms) {
    auto it = m_gestureAnims.find(id);
    if (it == m_gestureAnims.end())
        return false;

    auto& state = it->second;
    state.gestureActive = false;

    bool committed = (state.gestureProgress >= state.cancelThreshold);
    float currentValue = state.startValue +
                         (state.endValue - state.startValue) * state.gestureProgress;
    float targetValue = committed ? state.endValue : state.startValue;

    // Calculate proportional duration for remaining distance
    float distance = std::fabs(targetValue - currentValue);
    float totalDistance = std::fabs(state.endValue - state.startValue);
    float duration = completionDuration_ms;
    if (totalDistance > 0.001f) {
        duration *= (distance / totalDistance);
    }
    duration = std::max(duration, 30.0f);

    state.completionAnim = allocateId();
    AnimationID animId = create(state.completionAnim, currentValue, targetValue,
                                 duration, completionCurve);

    if (auto* anim = getAnimation(animId)) {
        auto onProgress = state.onProgress;
        auto onEnd = state.onEnd;
        bool comm = committed;

        anim->onUpdate = [onProgress](float val) {
            if (onProgress) onProgress(val);
        };
        anim->onComplete = [id, onEnd, comm, this]() {
            if (onEnd) onEnd(comm);
            m_gestureAnims.erase(id);
        };
    }

    return committed;
}

const GestureAnimationState* AnimationEngine::getGestureAnimation(AnimationID id) const {
    auto it = m_gestureAnims.find(id);
    if (it != m_gestureAnims.end())
        return &it->second;
    return nullptr;
}

// ============================================================================
// Task 54: Custom per-window shader hooks
// ============================================================================

void AnimationEngine::setWindowShader(Surface* surface, BuiltinShader shader) {
    if (!surface) return;
    auto& config = m_windowShaders[surface];
    config.builtinShader = shader;
    config.customShaderPath.clear();
    config.enabled = (shader != BuiltinShader::None);
    config.time = 0.0f;
}

void AnimationEngine::setWindowCustomShader(Surface* surface, const std::string& path) {
    if (!surface) return;
    auto& config = m_windowShaders[surface];
    config.builtinShader = BuiltinShader::None;
    config.customShaderPath = path;
    config.enabled = !path.empty();
    config.time = 0.0f;
}

void AnimationEngine::clearWindowShader(Surface* surface) {
    m_windowShaders.erase(surface);
}

void AnimationEngine::reloadCustomShaders() {
    // Mark all custom shaders as needing reload by resetting time.
    // The actual shader compilation is handled by the renderer when
    // it reads the config. Here we just reset state to trigger reload.
    for (auto& [surface, config] : m_windowShaders) {
        if (!config.customShaderPath.empty()) {
            config.time = 0.0f;
            // The renderer will detect the path and recompile
        }
    }
}

const WindowShaderConfig* AnimationEngine::getWindowShader(Surface* surface) const {
    auto it = m_windowShaders.find(surface);
    if (it != m_windowShaders.end() && it->second.enabled)
        return &it->second;
    return nullptr;
}

// ============================================================================
// Task 55: Dim inactive windows effect
// ============================================================================

void AnimationEngine::setDimInactiveConfig(const DimInactiveConfig& config) {
    m_dimConfig = config;
}

void AnimationEngine::onFocusChanged(Surface* focused) {
    if (!m_enabled || m_dimConfig.strength <= 0.0f)
        return;

    // Fade out old focused window (make it dim)
    if (m_lastFocused && m_lastFocused != focused) {
        if (!isExcludedFromDim(m_lastFocused)) {
            auto& state = m_dimStates[m_lastFocused];
            // Cancel any existing fade for this window
            if (state.fadeAnim != 0) {
                forceCancel(state.fadeAnim);
            }

            state.fadeAnim = allocateId();
            float fromDim = state.currentDim;
            float toDim = m_dimConfig.strength;

            Surface* surfPtr = m_lastFocused;
            AnimationID animId = create(state.fadeAnim, fromDim, toDim,
                                         m_dimConfig.fadeInDuration_ms,
                                         m_dimConfig.fadeCurve);
            if (auto* anim = getAnimation(animId)) {
                anim->onUpdate = [surfPtr, this](float val) {
                    auto it = m_dimStates.find(surfPtr);
                    if (it != m_dimStates.end()) {
                        it->second.currentDim = val;
                    }
                };
            }
            state.wasFocused = false;
        }
    }

    // Fade in new focused window (remove dim)
    if (focused) {
        auto& state = m_dimStates[focused];
        if (state.fadeAnim != 0) {
            forceCancel(state.fadeAnim);
        }

        state.fadeAnim = allocateId();
        float fromDim = state.currentDim;
        float toDim = 0.0f;

        Surface* surfPtr = focused;
        AnimationID animId = create(state.fadeAnim, fromDim, toDim,
                                     m_dimConfig.fadeOutDuration_ms,
                                     m_dimConfig.fadeCurve);
        if (auto* anim = getAnimation(animId)) {
            anim->onUpdate = [surfPtr, this](float val) {
                auto it = m_dimStates.find(surfPtr);
                if (it != m_dimStates.end()) {
                    it->second.currentDim = val;
                }
            };
        }
        state.wasFocused = true;
    }

    m_lastFocused = focused;
}

float AnimationEngine::getWindowDim(Surface* surface) const {
    auto it = m_dimStates.find(surface);
    if (it != m_dimStates.end())
        return it->second.currentDim;

    // If we've never tracked this surface and it's not focused, return full dim
    if (surface != m_lastFocused && m_dimConfig.strength > 0.0f)
        return m_dimConfig.strength;

    return 0.0f;
}

bool AnimationEngine::isExcludedFromDim(Surface* surface) const {
    if (!surface) return true;
    const auto& appId = surface->getAppId();
    return m_dimConfig.excludeAppIds.contains(appId);
}

} // namespace eternal
