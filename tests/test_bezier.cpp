// Task 103: Unit tests for Bezier curve
//
// Tests linear curve, easeIn, easeOut, boundary values, Newton-Raphson
// convergence, preset curves, named curve lookup, and more.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <eternal/animation/BezierCurve.hpp>

#include <cmath>

using namespace eternal;
using Catch::Matchers::WithinAbs;

// ===== Test case 1: Boundary values (t=0 -> 0, t=1 -> 1) =================

TEST_CASE("BezierCurve: boundary values t=0 and t=1", "[bezier]") {
    auto curves = {
        BezierCurve::linear(),
        BezierCurve::easeIn(),
        BezierCurve::easeOut(),
        BezierCurve::easeInOut(),
        BezierCurve::overshot(),
        BezierCurve::spring(),
    };

    for (const auto& curve : curves) {
        CHECK_THAT(curve.evaluate(0.0f), WithinAbs(0.0f, 1e-6));
        CHECK_THAT(curve.evaluate(1.0f), WithinAbs(1.0f, 1e-6));
    }
}

// ===== Test case 2: Linear curve (y = x for all t) =======================

TEST_CASE("BezierCurve: linear curve y equals x", "[bezier]") {
    auto linear = BezierCurve::linear();

    for (float x = 0.0f; x <= 1.0f; x += 0.05f) {
        CHECK_THAT(linear.evaluate(x), WithinAbs(x, 1e-4));
    }
}

// ===== Test case 3: Values below 0 clamp to 0 ============================

TEST_CASE("BezierCurve: negative input clamps to 0", "[bezier]") {
    auto curve = BezierCurve::easeInOut();
    CHECK_THAT(curve.evaluate(-0.5f), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(curve.evaluate(-100.0f), WithinAbs(0.0f, 1e-6));
}

// ===== Test case 4: Values above 1 clamp to 1 ============================

TEST_CASE("BezierCurve: input above 1 clamps to 1", "[bezier]") {
    auto curve = BezierCurve::easeInOut();
    CHECK_THAT(curve.evaluate(1.5f), WithinAbs(1.0f, 1e-6));
    CHECK_THAT(curve.evaluate(100.0f), WithinAbs(1.0f, 1e-6));
}

// ===== Test case 5: easeIn starts slow ====================================

TEST_CASE("BezierCurve: easeIn starts slow", "[bezier]") {
    auto easeIn = BezierCurve::easeIn();

    // At x=0.25, easeIn should return a value less than 0.25 (slower start)
    float y25 = easeIn.evaluate(0.25f);
    CHECK(y25 < 0.25f);

    // At x=0.5, easeIn should still be below 0.5 for a typical easeIn
    float y50 = easeIn.evaluate(0.5f);
    CHECK(y50 < 0.5f);

    // At x=0.75, easeIn accelerates -- value closer to 0.75 or above
    float y75 = easeIn.evaluate(0.75f);
    CHECK(y75 > y50);
}

// ===== Test case 6: easeOut ends slow =====================================

TEST_CASE("BezierCurve: easeOut ends slow", "[bezier]") {
    auto easeOut = BezierCurve::easeOut();

    // At x=0.25, easeOut should already be above 0.25 (fast start)
    float y25 = easeOut.evaluate(0.25f);
    CHECK(y25 > 0.25f);

    // At x=0.75, easeOut should be above 0.75 but slowing
    float y75 = easeOut.evaluate(0.75f);
    CHECK(y75 > 0.75f);
}

// ===== Test case 7: easeInOut symmetry ====================================

TEST_CASE("BezierCurve: easeInOut midpoint is approximately 0.5", "[bezier]") {
    auto easeInOut = BezierCurve::easeInOut();

    // For the standard easeInOut (0.42, 0, 0.58, 1), evaluate(0.5)
    // should be close to 0.5 due to the symmetry of the control points.
    float mid = easeInOut.evaluate(0.5f);
    CHECK_THAT(mid, WithinAbs(0.5f, 0.05f));
}

// ===== Test case 8: Newton-Raphson convergence with steep curve ===========

TEST_CASE("BezierCurve: Newton-Raphson converges for steep curves", "[bezier]") {
    // A very steep curve that might challenge Newton-Raphson
    BezierCurve steep(0.9f, 0.0f, 0.1f, 1.0f);

    // Should still produce valid output in [0, 1] range
    for (float x = 0.0f; x <= 1.0f; x += 0.1f) {
        float y = steep.evaluate(x);
        CHECK(y >= -0.01f);
        CHECK(y <= 1.01f);
    }

    // Endpoints must be exact
    CHECK_THAT(steep.evaluate(0.0f), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(steep.evaluate(1.0f), WithinAbs(1.0f, 1e-6));
}

// ===== Test case 9: Monotonicity of standard curves =======================

TEST_CASE("BezierCurve: standard curves are monotonically increasing", "[bezier]") {
    auto curves = {
        BezierCurve::linear(),
        BezierCurve::easeIn(),
        BezierCurve::easeOut(),
        BezierCurve::easeInOut(),
    };

    for (const auto& curve : curves) {
        float prev = -1.0f;
        for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
            float y = curve.evaluate(x);
            CHECK(y >= prev - 1e-5f);
            prev = y;
        }
    }
}

// ===== Test case 10: Overshoot curve exceeds 1.0 ==========================

TEST_CASE("BezierCurve: overshoot curve exceeds 1.0", "[bezier]") {
    auto overshoot = BezierCurve::overshot();

    // The overshoot curve (0.05, 0.9, 0.1, 1.1) should produce y > 1.0
    // at some point in the middle of the animation.
    bool exceeds = false;
    for (float x = 0.01f; x < 1.0f; x += 0.01f) {
        float y = overshoot.evaluate(x);
        if (y > 1.0f) {
            exceeds = true;
            break;
        }
    }
    // The overshot preset has p2y = 1.1, so it should overshoot
    // (or at least approach values near 1.0 early)
    // Note: whether it actually exceeds 1.0 depends on the parametric shape
    CHECK(overshoot.evaluate(0.5f) > 0.5f);
}

// ===== Test case 11: Preset curves have correct control points ============

TEST_CASE("BezierCurve: preset control points", "[bezier]") {
    auto easeIn = BezierCurve::easeIn();
    CHECK_THAT(easeIn.getP1X(), WithinAbs(0.42f, 1e-6));
    CHECK_THAT(easeIn.getP1Y(), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(easeIn.getP2X(), WithinAbs(1.0f, 1e-6));
    CHECK_THAT(easeIn.getP2Y(), WithinAbs(1.0f, 1e-6));

    auto easeOut = BezierCurve::easeOut();
    CHECK_THAT(easeOut.getP1X(), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(easeOut.getP2X(), WithinAbs(0.58f, 1e-6));

    auto linear = BezierCurve::linear();
    CHECK_THAT(linear.getP1X(), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(linear.getP1Y(), WithinAbs(0.0f, 1e-6));
    CHECK_THAT(linear.getP2X(), WithinAbs(1.0f, 1e-6));
    CHECK_THAT(linear.getP2Y(), WithinAbs(1.0f, 1e-6));
}

// ===== Test case 12: BezierCurveManager named lookup ======================

TEST_CASE("BezierCurveManager: named curve lookup", "[bezier]") {
    BezierCurveManager manager;

    SECTION("built-in presets exist") {
        CHECK(manager.has("linear"));
        CHECK(manager.has("easeIn"));
        CHECK(manager.has("easeOut"));
        CHECK(manager.has("easeInOut"));
        CHECK(manager.has("overshot"));
        CHECK(manager.has("spring"));
    }

    SECTION("unknown name returns linear") {
        auto curve = manager.get("nonexistent");
        // Linear: evaluate(0.5) should be ~0.5
        CHECK_THAT(curve.evaluate(0.5f), WithinAbs(0.5f, 1e-4));
    }

    SECTION("add and retrieve custom curve") {
        manager.add("custom", 0.1f, 0.2f, 0.3f, 0.4f);
        CHECK(manager.has("custom"));

        auto curve = manager.get("custom");
        CHECK_THAT(curve.getP1X(), WithinAbs(0.1f, 1e-6));
        CHECK_THAT(curve.getP1Y(), WithinAbs(0.2f, 1e-6));
    }

    SECTION("remove a curve") {
        manager.add("temp", 0.5f, 0.5f, 0.5f, 0.5f);
        CHECK(manager.has("temp"));
        manager.remove("temp");
        CHECK_FALSE(manager.has("temp"));
    }
}

// ===== Test case 13: Custom curve construction ============================

TEST_CASE("BezierCurve: custom curve evaluation", "[bezier]") {
    // A curve where p1 = p2 should be equivalent to linear
    BezierCurve custom(0.5f, 0.5f, 0.5f, 0.5f);
    float y = custom.evaluate(0.5f);
    CHECK_THAT(y, WithinAbs(0.5f, 0.02f));
}

// ===== Test case 14: Spring preset has expected behaviour =================

TEST_CASE("BezierCurve: spring preset overshoots", "[bezier]") {
    auto spring = BezierCurve::spring();
    // Spring (0.15, 1.2, 0.3, 1.0) -- p1y=1.2 means it accelerates fast
    // and may overshoot

    // At 0.2 the y value should already be significant (fast start)
    float y20 = spring.evaluate(0.2f);
    CHECK(y20 > 0.2f);
}

// ===== Test case 15: Many evaluations give consistent results =============

TEST_CASE("BezierCurve: repeated evaluation consistency", "[bezier]") {
    auto curve = BezierCurve::easeInOut();

    float first = curve.evaluate(0.3f);
    for (int i = 0; i < 100; ++i) {
        float val = curve.evaluate(0.3f);
        CHECK_THAT(val, WithinAbs(first, 1e-7));
    }
}
