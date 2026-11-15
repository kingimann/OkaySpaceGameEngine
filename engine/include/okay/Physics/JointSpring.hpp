#pragma once
// The force a Spring joint (Joint2D / Joint3D, Mode Spring) pushes along its axis in one
// physics step, kept inside what one step can follow.
//
// The solvers apply  impulse = (spring * stretch + damper * stretchSpeed) * dt  to the two
// bodies along the joint axis, and the bodies then move with their new speed. Write
// imSum = 1/massA + 1/massB (0 for a static or kinematic side). In one step:
//   * the damper changes the stretch speed by  b = damper * dt * imSum  of itself, and
//   * the spring moves the next step's stretch by  a = spring * dt * dt * imSum  of itself.
// Worked through for one step (stretch x, speed u = dt * stretchSpeed):
//   u' = (1 - b) u - a x,   x' = x + u'
// whose two growth factors stay below 1 in size only while b < 2 and a + 2b < 4. Past that
// every step overshoots by more than it removes and the body flies off (Grok finding 16,
// issue #888: a 0.05 kg body on damper 8 reached about 1e19 m/s, then NaN, in about a second).
//
// So each part is capped at what one step can do: the damper at b <= 1 (it can at most stop
// the stretching motion, never reverse it) and the spring at a <= 1 (it can at most close the
// whole stretch). With both caps a + 2b <= 3, inside the stable region. Below the caps the
// numbers are untouched, so an ordinary spring behaves exactly as it did before.

namespace okay {

/// The capped force along the joint axis (A -> B): positive pulls the two bodies together.
/// `stretch` = length - rest length, `stretchSpeed` = rate of change of the length,
/// `imSum` = sum of the two inverse masses, `dt` = the step.
inline float JointSpringForce(float spring, float damper, float stretch, float stretchSpeed,
                              float imSum, float dt) {
    float k = spring, c = damper;
    const float a = k * dt * dt * imSum;   // the spring's share of the stretch closed in one step
    const float b = c * dt * imSum;        // the damper's share of the stretch speed removed in one step
    if (a > 1.0f) k /= a;                  // cap: close at most the whole stretch
    if (b > 1.0f) c /= b;                  // cap: stop the motion at most, never reverse it
    return k * stretch + c * stretchSpeed;
}

} // namespace okay
