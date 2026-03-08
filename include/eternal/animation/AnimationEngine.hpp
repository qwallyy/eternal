#pragma once

#include <eternal/animation/Animation.hpp>
#include <eternal/animation/BezierCurve.hpp>
#include <eternal/animation/SpringAnimation.hpp>
#include <eternal/core/Surface.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eternal {

// ============================================================================
// Task 48: Window open/close animation styles
// ============================================================================

enum class WindowAnimStyle : uint8_t {
    Slide,      ///< Slide in from bottom / out to bottom
    Fade,       ///< Pure fade-in / fade-out
    Zoom,       ///< Scale from center
    PopIn,      ///< Quick scale + fade (bouncy)
    SlideFade,  ///< Combined slide + fade + scale (default)
    None,       ///< Instant, no animation
};

/// Configuration for open/close window animations.
struct WindowAnimConfig {
    WindowAnimStyle openStyle  = WindowAnimStyle::SlideFade;
    WindowAnimStyle closeStyle = WindowAnimStyle::SlideFade;
    float openDuration_ms  = 250.0f;
    float closeDuration_ms = 200.0f;
    std::string openCurve  = "easeOut";
    std::string closeCurve = "easeIn";
};

/// Ghost surface state for close animation: keeps rendering a snapshot
/// of the window surface until the close animation completes.
struct GhostSurface {
    AnimationID animId = 0;
    Surface* originalSurface = nullptr;
    int x = 0, y = 0, width = 0, height = 0;
    float opacity = 1.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool finished = false;
};

// ============================================================================
// Task 49: Window move/resize smooth animation
// ============================================================================

/// Tracked geometry animation for a single surface.
struct GeometryAnimation {
    AnimationID xAnim = 0;
    AnimationID yAnim = 0;
    AnimationID wAnim = 0;
    AnimationID hAnim = 0;
    Surface* surface = nullptr;
    int targetX = 0, targetY = 0, targetW = 0, targetH = 0;
    bool active = false;
};

struct MoveResizeConfig {
    float moveDuration_ms   = 200.0f;
    float resizeDuration_ms = 150.0f;
    std::string moveCurve   = "easeInOut";
    std::string resizeCurve = "easeInOut";
    bool enableMoveAnim     = true;
    bool enableResizeAnim   = true;
};

// ============================================================================
// Task 50: Workspace transition animations
// ============================================================================

enum class WorkspaceTransitionStyle : uint8_t {
    SlideHorizontal,  ///< Old slides out, new slides in (left/right)
    SlideVertical,    ///< Niri-style vertical workspace switch
    Fade,             ///< Crossfade between workspaces
    None,
};

struct WorkspaceTransition {
    AnimationID animId = 0;
    float progress = 0.0f;       ///< 0 = fully on old, 1 = fully on new
    bool gestureActive = false;  ///< True during 1:1 finger tracking
    int fromWorkspace = 0;
    int toWorkspace = 0;
    int direction = 0;           ///< -1 = left/up, +1 = right/down
    bool active = false;
};

struct WorkspaceTransitionConfig {
    WorkspaceTransitionStyle style = WorkspaceTransitionStyle::SlideHorizontal;
    float duration_ms = 300.0f;
    std::string curve = "easeInOut";
    float gestureThreshold = 0.3f;  ///< 30% of viewport to commit switch
};

// ============================================================================
// Task 51: Fullscreen crossfade
// ============================================================================

struct FullscreenTransition {
    AnimationID animId = 0;
    Surface* surface = nullptr;
    int fromX = 0, fromY = 0, fromW = 0, fromH = 0;
    int toX = 0, toY = 0, toW = 0, toH = 0;
    float progress = 0.0f;
    bool active = false;
};

struct FullscreenTransitionConfig {
    float duration_ms = 300.0f;
    std::string curve = "easeInOut";
    bool enableForMaximize = true;
};

// ============================================================================
// Task 52: Kinetic scrolling for niri strip
// ============================================================================

struct KineticScrollState {
    float velocity = 0.0f;         ///< Current scroll velocity (px/s)
    float position = 0.0f;         ///< Current scroll position
    float targetPosition = 0.0f;   ///< Snap target after fling
    bool flinging = false;         ///< True during kinetic deceleration
    bool rubberBanding = false;    ///< True when past scroll bounds
    float rubberBandOffset = 0.0f; ///< How far past the boundary
    float minBound = 0.0f;
    float maxBound = 0.0f;
    SpringAnimation snapSpring;    ///< Spring for snap-to-column
    bool snapping = false;
};

struct KineticScrollConfig {
    float deceleration = 0.998f;       ///< Per-ms velocity multiplier (< 1)
    float minVelocity = 0.5f;          ///< Stop threshold (px/s)
    float rubberBandStiffness = 0.15f; ///< How quickly rubber band pulls back
    float rubberBandMaxOverscroll = 200.0f;
    float snapStiffness = 300.0f;      ///< Spring stiffness for snap
    float snapDamping = 25.0f;
};

// ============================================================================
// Task 53: Gesture 1:1 tracking
// ============================================================================

struct GestureAnimationState {
    float gestureProgress = 0.0f;  ///< Raw gesture delta mapped to [0,1]
    bool gestureActive = false;
    AnimationID completionAnim = 0;
    float startValue = 0.0f;
    float endValue = 0.0f;
    float cancelThreshold = 0.3f;
    std::function<void(float)> onProgress;
    std::function<void(bool committed)> onEnd;
};

// ============================================================================
// Task 54: Custom per-window shader hooks
// ============================================================================

enum class BuiltinShader : uint8_t {
    None,
    Grayscale,
    Sepia,
    Invert,
    ChromaticAberration,
};

struct WindowShaderConfig {
    BuiltinShader builtinShader = BuiltinShader::None;
    std::string customShaderPath;     ///< Path to custom GLSL fragment shader
    bool enabled = false;
    float time = 0.0f;               ///< Accumulated time for shader uniform
};

// ============================================================================
// Task 55: Dim inactive windows effect
// ============================================================================

struct DimInactiveConfig {
    float strength = 0.2f;           ///< 0.0 = none, 1.0 = fully dimmed
    float fadeInDuration_ms = 150.0f;
    float fadeOutDuration_ms = 150.0f;
    std::string fadeCurve = "easeInOut";
    float dimR = 0.0f;               ///< Dim overlay color
    float dimG = 0.0f;
    float dimB = 0.0f;
    std::unordered_set<std::string> excludeAppIds;  ///< e.g., video players
};

struct WindowDimState {
    AnimationID fadeAnim = 0;
    float currentDim = 0.0f;
    bool wasFocused = false;
};

// ============================================================================
// AnimationEngine
// ============================================================================

class AnimationEngine {
public:
    AnimationEngine();
    ~AnimationEngine();

    // ------------------------------------------------------------------
    // Task 46: Core animation manager and tick loop
    // ------------------------------------------------------------------

    /// Create a new animation. Returns its AnimationID.
    AnimationID create(AnimationID id, float from, float to,
                       float duration_ms, const std::string& curve);

    /// Create an animation with priority.
    AnimationID create(AnimationID id, float from, float to,
                       float duration_ms, const std::string& curve,
                       AnimationPriority priority);

    /// Cancel an active animation (respects priority: high can't be
    /// cancelled by lower-priority requests).
    void cancel(AnimationID id);

    /// Force-cancel regardless of priority.
    void forceCancel(AnimationID id);

    /// Tick all active animations forward by delta_ms (real frame time).
    void update(float delta_ms);

    /// Check whether a specific animation is still running.
    [[nodiscard]] bool isAnimating(AnimationID id) const;

    /// Get the current interpolated value of an animation.
    [[nodiscard]] float getValue(AnimationID id) const;

    /// Get a mutable reference to an animation (nullptr if not found).
    [[nodiscard]] Animation* getAnimation(AnimationID id);
    [[nodiscard]] const Animation* getAnimation(AnimationID id) const;

    /// Number of currently active (non-finished) animations.
    [[nodiscard]] std::size_t getActiveCount() const;

    /// Allocate an animation ID.
    [[nodiscard]] AnimationID allocateId();

    /// Global speed multiplier (1.0 = normal).
    void setGlobalSpeed(float speed);

    /// Enable / disable the engine entirely.
    void setEnabled(bool enabled);

    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] float getGlobalSpeed() const { return m_globalSpeed; }

    /// Access to the bezier curve manager.
    BezierCurveManager& curveManager() { return m_curveManager; }
    const BezierCurveManager& curveManager() const { return m_curveManager; }

    // ------------------------------------------------------------------
    // Task 48: Window open/close animations
    // ------------------------------------------------------------------

    /// Start a window open animation. Returns the animation group ID.
    AnimationID animateWindowOpen(Surface* surface, int x, int y, int w, int h);

    /// Start a window close animation. Creates a ghost surface.
    AnimationID animateWindowClose(Surface* surface, int x, int y, int w, int h);

    /// Set the global open/close animation config.
    void setWindowAnimConfig(const WindowAnimConfig& config);

    /// Override animation style for a specific window (via window rules).
    void setWindowAnimStyleOverride(Surface* surface, WindowAnimStyle style);

    /// Get all active ghost surfaces (for rendering during close anim).
    [[nodiscard]] const std::vector<GhostSurface>& getGhostSurfaces() const;

    /// Clean up finished ghost surfaces.
    void cleanupGhosts();

    // ------------------------------------------------------------------
    // Task 49: Window move/resize smooth animation
    // ------------------------------------------------------------------

    /// Animate a surface to a new geometry. Cancels existing geometry
    /// animation for this surface and starts a new one (retargeting).
    void animateGeometry(Surface* surface,
                         int newX, int newY, int newW, int newH);

    /// Skip animation for direct manipulation (interactive resize/move).
    void setGeometryImmediate(Surface* surface,
                              int x, int y, int w, int h);

    /// Cancel any ongoing geometry animation for a surface.
    void cancelGeometryAnimation(Surface* surface);

    /// Check if a surface has an active geometry animation.
    [[nodiscard]] bool hasGeometryAnimation(Surface* surface) const;

    /// Get the current animated geometry for a surface.
    [[nodiscard]] std::optional<SurfaceBox> getAnimatedGeometry(Surface* surface) const;

    void setMoveResizeConfig(const MoveResizeConfig& config);

    // ------------------------------------------------------------------
    // Task 50: Workspace transition animations
    // ------------------------------------------------------------------

    /// Start a workspace transition from one workspace to another.
    void startWorkspaceTransition(int fromWs, int toWs, int direction);

    /// Begin gesture-driven workspace transition (1:1 tracking).
    void startWorkspaceGesture(int fromWs, int direction);

    /// Update gesture-driven transition progress.
    void updateWorkspaceGesture(float delta);

    /// End gesture: commit or cancel based on threshold.
    /// Returns true if the switch was committed, false if cancelled.
    bool endWorkspaceGesture();

    /// Get current workspace transition state.
    [[nodiscard]] const WorkspaceTransition& getWorkspaceTransition() const;
    [[nodiscard]] bool isWorkspaceTransitionActive() const;

    void setWorkspaceTransitionConfig(const WorkspaceTransitionConfig& config);

    // ------------------------------------------------------------------
    // Task 51: Fullscreen crossfade
    // ------------------------------------------------------------------

    /// Start a crossfade transition for fullscreen/maximize.
    void startFullscreenTransition(Surface* surface,
                                   int fromX, int fromY, int fromW, int fromH,
                                   int toX, int toY, int toW, int toH);

    /// Get the current fullscreen transition for a surface.
    [[nodiscard]] const FullscreenTransition* getFullscreenTransition(Surface* surface) const;

    void setFullscreenTransitionConfig(const FullscreenTransitionConfig& config);

    // ------------------------------------------------------------------
    // Task 52: Kinetic scrolling
    // ------------------------------------------------------------------

    /// Feed a scroll delta (from gesture or scroll wheel).
    void kineticScrollUpdate(float delta);

    /// End the scroll gesture, begin kinetic fling.
    void kineticScrollEnd();

    /// Begin a new scroll gesture (resets state for new momentum).
    void kineticScrollBegin();

    /// Update kinetic scroll physics each frame.
    void updateKineticScroll(float delta_ms);

    /// Set scroll bounds for rubber-banding.
    void setScrollBounds(float minBound, float maxBound);

    /// Set the snap targets (column positions) for snap-after-fling.
    void setScrollSnapTargets(const std::vector<float>& targets);

    /// Get current kinetic scroll position.
    [[nodiscard]] float getKineticScrollPosition() const;

    /// Get current scroll velocity.
    [[nodiscard]] float getKineticScrollVelocity() const;

    [[nodiscard]] bool isKineticScrollActive() const;

    void setKineticScrollConfig(const KineticScrollConfig& config);

    // ------------------------------------------------------------------
    // Task 53: Gesture 1:1 animation tracking
    // ------------------------------------------------------------------

    /// Start gesture-driven animation tracking.
    AnimationID startGestureAnimation(float startValue, float endValue,
                                       float cancelThreshold);

    /// Update gesture progress (0..1 maps from startValue to endValue).
    void updateGestureAnimation(AnimationID id, float progress);

    /// End gesture: if progress > threshold, animate to end; else snap back.
    bool endGestureAnimation(AnimationID id, const std::string& completionCurve,
                              float completionDuration_ms);

    [[nodiscard]] const GestureAnimationState* getGestureAnimation(AnimationID id) const;

    // ------------------------------------------------------------------
    // Task 54: Custom per-window shader hooks
    // ------------------------------------------------------------------

    /// Attach a built-in shader to a window.
    void setWindowShader(Surface* surface, BuiltinShader shader);

    /// Attach a custom shader path to a window (via window rules).
    void setWindowCustomShader(Surface* surface, const std::string& path);

    /// Remove shader from a window.
    void clearWindowShader(Surface* surface);

    /// Hot-reload all custom shaders from disk.
    void reloadCustomShaders();

    /// Get shader config for a window (nullptr if none).
    [[nodiscard]] const WindowShaderConfig* getWindowShader(Surface* surface) const;

    // ------------------------------------------------------------------
    // Task 55: Dim inactive windows
    // ------------------------------------------------------------------

    /// Notify that focus changed to the given surface.
    void onFocusChanged(Surface* focused);

    /// Get the current dim value for a surface (0..strength).
    [[nodiscard]] float getWindowDim(Surface* surface) const;

    /// Check if a surface is excluded from dimming.
    [[nodiscard]] bool isExcludedFromDim(Surface* surface) const;

    void setDimInactiveConfig(const DimInactiveConfig& config);

private:
    // -- Core state (Task 46) --
    std::vector<Animation> m_animations;
    std::vector<Animation> m_pool;          ///< Animation object pool
    float m_globalSpeed = 1.0f;
    bool m_enabled = true;
    AnimationID m_nextId = 1;
    BezierCurveManager m_curveManager;

    /// Get an animation from pool or create new.
    Animation& acquireAnimation();

    /// Return finished animation to pool.
    void recycleAnimation(Animation& anim);

    /// Build an easing function from curve name.
    EasingFunction makeEasing(const std::string& curveName);

    // -- Window open/close (Task 48) --
    WindowAnimConfig m_windowAnimConfig;
    std::vector<GhostSurface> m_ghosts;
    std::unordered_map<Surface*, WindowAnimStyle> m_windowAnimOverrides;

    WindowAnimStyle resolveOpenStyle(Surface* surface) const;
    WindowAnimStyle resolveCloseStyle(Surface* surface) const;

    // -- Geometry animation (Task 49) --
    MoveResizeConfig m_moveResizeConfig;
    std::unordered_map<Surface*, GeometryAnimation> m_geometryAnims;

    // -- Workspace transition (Task 50) --
    WorkspaceTransitionConfig m_wsTransConfig;
    WorkspaceTransition m_wsTransition;

    // -- Fullscreen crossfade (Task 51) --
    FullscreenTransitionConfig m_fsTransConfig;
    std::unordered_map<Surface*, FullscreenTransition> m_fsTransitions;

    // -- Kinetic scroll (Task 52) --
    KineticScrollConfig m_kineticConfig;
    KineticScrollState m_kineticState;
    std::vector<float> m_scrollSnapTargets;

    float findNearestSnapTarget(float position) const;
    void applyRubberBand(float delta_ms);

    // -- Gesture tracking (Task 53) --
    std::unordered_map<AnimationID, GestureAnimationState> m_gestureAnims;

    // -- Per-window shaders (Task 54) --
    std::unordered_map<Surface*, WindowShaderConfig> m_windowShaders;

    // -- Dim inactive (Task 55) --
    DimInactiveConfig m_dimConfig;
    std::unordered_map<Surface*, WindowDimState> m_dimStates;
    Surface* m_lastFocused = nullptr;
};

} // namespace eternal
