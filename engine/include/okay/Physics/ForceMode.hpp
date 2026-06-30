#pragma once

namespace okay {

/// How a force is applied to a Rigidbody (matches Unity's ForceMode). Shared by
/// the 2D and 3D bodies.
///   * Force          — continuous force, mass-dependent, integrated over the step.
///   * Acceleration   — continuous acceleration, mass-independent (ignores mass).
///   * Impulse        — instantaneous momentum change, mass-dependent.
///   * VelocityChange — instantaneous velocity change, mass-independent.
enum class ForceMode { Force, Acceleration, Impulse, VelocityChange };

} // namespace okay
