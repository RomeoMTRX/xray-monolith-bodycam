#pragma once
// Classic bodycam simulation: plain data in, plain data out, no engine types.
//
// Every feature is one unit: a settings struct that carries its own enable
// flag, a state struct, and an update that reads only its own settings, its
// own state and the shared frame signals. A feature therefore works with every
// other feature off. The few places where features meet are explicit:
//  - a shot notifies both lowering (recovery timers) and the fire impulse;
//  - the sprint sway goes out to the camera rig, whose position spring carries it;
//  - the footstep camera, when it owns sprint motion, silences the sprint sway.
// Camera Decoupling lives in the camera rig (bodycam_camera_rig.h).

#include <cstdint>

namespace Bodycam
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDefaultStalker2MovementResponse = 5.f;
// The arm springs run at response * kArmSpringHzPerResponse Hz, limited to 1..6 Hz.
constexpr float kArmSpringHzPerResponse = 0.12f;
// The arm follow response of the slowest spring, 1 Hz.
constexpr float kArmFollowResponseMin = 1.f / kArmSpringHzPerResponse;

struct SVec3
{
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	void Set(float nx, float ny, float nz);
	void Add(const SVec3& value);
	void Add(float nx, float ny, float nz);
	void Mul(float value);
	float Magnitude() const;
	void NormalizeSafe();
};

enum ESimMoveFlags : std::uint32_t
{
	smfForward = 1u << 0,
	smfBack = 1u << 1,
	smfLeft = 1u << 2,
	smfRight = 1u << 3,
	smfCrouch = 1u << 4,
	smfSprint = 1u << 5,
	smfFall = 1u << 6,
	smfJump = 1u << 7,
	smfAnyMove = smfForward | smfBack | smfLeft | smfRight,
};

struct AdsState
{
	bool active = false;
	float blend = 0.f;
};

// Frame input.
struct SimulationInput
{
	float dt = 0.f;
	std::uint32_t move_flags = 0;
	bool ads = false;
	float ads_blend = 0.f; // 0..1
	bool weapon_lowered = false;
	bool combat = false;
	bool accelerated = false;
	bool firearm_equipped = true;
	// Speed over its target, 0..1. 1 while moving without movement inertia.
	float actor_speed_fraction = 0.f;
	// Another system (the footstep camera) plays the sprint camera motion.
	bool sprint_camera_owned_elsewhere = false;
	// The camera rig's gun-first follow owns the camera lag: the mouse is read
	// unfiltered.
	bool gun_first_follow = false;
	// The aim as accumulated from mouse deltas; equals the camera's aim within rounding.
	float mouse_aim_yaw = 0.f;
	float mouse_aim_pitch = 0.f;
	// Tarkov Handling: horizontal velocity in the view's frame (m/s, x right,
	// z forward), ground contact, the native footstep phase (0..1 per stride,
	// negative when there is none) and the active weapon's weight (kg, 0 unknown).
	float velocity_right = 0.f;
	float velocity_forward = 0.f;
	bool grounded = true;
	float foot_phase = -1.f;
	float weapon_weight = 0.f;
};

// Signals every feature may read: smoothed mouse speed and movement intent.
struct InputSignalSettings
{
	float mouse_filter = 18.f;
	float move_filter = 8.f;
};

struct InputSignals
{
	SVec3 mouse_speed; // rad/s; x yaw, y pitch.
	float mouse_aim_yaw = 0.f;
	float mouse_aim_pitch = 0.f;
	bool mouse_aim_initialized = false;
	SVec3 move_intent; // x right, z forward; scaled by gait.
};

// Sprint camera sway.
struct SprintSwaySettings
{
	bool enable = true;
	float strength = 1.5f;
	float smoothness = 1.f;
	float enter_time = 0.32f;
	float exit_time = 0.48f;
	float camera_pitch = -0.25f;
	float camera_pos = 0.004f;
};

struct SprintSwayState
{
	float amount = 0.f;
	float settle = 0.f;
	float phase = 0.f;
};

// What the sway adds to the camera this frame.
struct SprintSway
{
	SVec3 offset; // Added to the camera position target.
	float pitch = 0.f; // Added to the camera pitch.
	float reach = 0.f; // Share of the camera position limit the sway may use.
};

// Viewmodel lowering.
struct LoweringSettings
{
	bool enable = true;
	bool disable_in_combat = true;
	float weight = 1.f;
	float pitch = 0.f;
	float yaw = 0.f;
	float roll = 0.f;
	float x = 0.f;
	float y = -0.08f;
	float z = 0.1f;
	float holster_offset = 0.015f;
	float slow_walk = 0.5f;
	float walk = 1.f;
	float move = 1.f;
	float fire_timeout = 0.03f;
	float aim_timeout = 0.005f;
	float speed = 0.05f;
	float return_speed = 0.4f;
	float combat_timeout = 2.f;
};

struct LoweringState
{
	float amount = 0.f;
	float target = 0.f;
	float holster = 0.f;
	float fire_recovery = 0.f;
	float ads_recovery = 0.f;
	float combat_timer = 0.f;
	bool prev_ads = false;
	SVec3 pos;
	SVec3 rot;
};

// Fire impulse.
struct FireImpulseSettings
{
	bool enable = true;
	float hip = 5.f;
	float ads = 1.35f;
	float ads_scale = 0.18f; // Extra scale on the kick at full ADS.
	float follow_speed = 9.f;
	float damping = 0.78f;
	float decay = 8.f;
	float pos_cap = 0.08f;
	float rot_cap = 8.f;
	// Per unit of power the weapon drops and comes back (metres), pitches up
	// and rolls away from the direction the mouse is moving (degrees).
	float kick_drop = 0.0015f;
	float kick_back = 0.0040f;
	float kick_pitch = 0.32f;
	float kick_roll = 0.10f;
};

struct FireImpulseState
{
	SVec3 impulse_pos; // Decaying kick target.
	SVec3 impulse_rot;
	SVec3 pos; // Viewmodel offset that follows the target.
	SVec3 rot;
};

// STALKER 2 style arm IK.
struct Stalker2ArmSettings
{
	bool enable = true;
	float weight = 1.f;
	float strength = 1.5f;
	float response = 20.f;
	float arm_follow_response = kArmFollowResponseMin;
	float arm_follow_scale = 0.f;
	float ads_scale = 0.2f;
	float mouse_strength = 1.f;
	float mouse_sensitivity = 1.5f;
	float mouse_max_yaw = 0.f;
	float mouse_max_pitch = 18.f;
	float mouse_max_roll = 15.5f;
	float movement_strength = 1.f;
	float movement_response = kDefaultStalker2MovementResponse;
	float slow_walk_scale = 0.5f;
	float mouse_pitch = 18.f;
	float mouse_yaw = 0.f;
	float mouse_roll = 28.f;
	float wrist_scale = 1.f;
};

struct Stalker2ArmState
{
	SVec3 wrist_rot;
	SVec3 wrist_rot_vel;
	SVec3 arm_follow_rot;
	SVec3 arm_follow_rot_vel;
	float weight = 0.f;
	float movement_weight = 0.f;
};

// Tarkov style handling: the gun and both hands hang on one soft, underdamped
// spring. A mouse turn drags the gun behind the view and lets it swing past
// when the turn stops, every stride swings it through a figure eight, strafing
// and walking lean it against the movement, and a heavier weapon swings wider
// and settles slower. Angles in degrees, offsets in metres.
struct TarkovHandlingSettings
{
	bool enable = true;
	float strength = 1.f;
	float frequency = 1.8f; // Hz of the spring for a weapon of the reference weight.
	float damping = 0.45f; // 1 settles without overshoot, lower swings past.
	float ads_scale = 0.35f;
	// Mouse drag.
	float drag_strength = 1.f;
	float drag_full_speed = 220.f; // deg/s of turn that reaches the full drag.
	float drag_yaw = 4.f;
	float drag_pitch = 3.f;
	float drag_roll = 2.5f;
	float drag_shift_x = 0.012f;
	float drag_shift_y = 0.010f;
	// Walk figure eight, once per stride sideways and twice up and down.
	float walk_strength = 1.f;
	float walk_x = 0.009f;
	float walk_y = 0.010f; // The spring halves this at walking pace.
	float walk_roll = 1.6f;
	float walk_yaw = 0.6f;
	float walk_pitch = 0.8f;
	float walk_speed = 2.f; // m/s at which the walk motion is full.
	float crouch_scale = 0.6f;
	float sprint_scale = 0.f;
	// Leaning against the movement.
	float strafe_shift = 0.012f;
	float strafe_roll = 3.f;
	float forward_dip = 0.8f;
	float forward_pull = 0.004f;
	// Weapon weight: 0 handles every weapon like the reference weight.
	float weight_influence = 1.f;
	float reference_weight = 3.5f; // kg
};

struct TarkovHandlingState
{
	SVec3 pos; // x right, y up, z forward
	SVec3 pos_vel;
	SVec3 rot; // x heading (left +), y pitch (up +), z bank
	SVec3 rot_vel;
	float weight = 0.f;
	float walk = 0.f;
	float phase = 0.f; // Own stride phase when the footstep phase is missing.
	float mass = 1.f; // Smoothed weight over the reference weight.
	bool mass_initialized = false;
};

// Whole simulation.
struct SimulationSettings
{
	InputSignalSettings input;
	SprintSwaySettings sprint;
	LoweringSettings lowering;
	FireImpulseSettings fire;
	Stalker2ArmSettings stalker2_arm;
	TarkovHandlingSettings tarkov;
};

struct SimulationState
{
	InputSignals signals;
	SprintSwayState sprint;
	LoweringState lowering;
	FireImpulseState fire;
	Stalker2ArmState stalker2_arm;
	TarkovHandlingState tarkov;
};

struct SimulationOutput
{
	SprintSway sprint_sway;
	// Viewmodel offset: fire impulse plus lowering.
	bool viewmodel_active = false;
	SVec3 viewmodel_pos;
	SVec3 viewmodel_rot;
	SVec3 lowering_pos;
	SVec3 lowering_rot;
	float lower_amount = 0.f;
	// STALKER 2 style arm controller, radians.
	bool stalker2_arm_active = false;
	SVec3 stalker2_wrist_rot;
	SVec3 stalker2_arm_follow_rot;
	float stalker2_movement_weight = 0.f;
	float stalker2_movement_response = kDefaultStalker2MovementResponse;
	// Tarkov Handling viewmodel offset, metres and degrees (heading, pitch, bank),
	// turned about the grip by the caller.
	bool tarkov_active = false;
	SVec3 tarkov_pos;
	SVec3 tarkov_rot;
};

void ResetSimulation(SimulationState& state, float yaw, float pitch, float ads_blend);
void RebaseSimulationLook(SimulationState& state, float yaw_delta, float pitch_delta);
AdsState ResolveAdsState(bool weapon_zoomed, float weapon_blend);

void UpdateSimulation(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output);
// A shot: lowering starts its recovery timers and the fire impulse kicks.
void NotifyShot(const SimulationSettings& settings, SimulationState& state, float power, bool ads);

// Authored walk motion that the STALKER 2 controller adds where a weapon's own
// animation has little.
struct AuthoredMotionMetrics
{
	float lead_rotation = 0.f;
	float lead_translation = 0.f;
	float wrist_rotation = 0.f;
	float forearm_rotation = 0.f;
	float upperarm_rotation = 0.f;
};

struct AuthoredMotionGains
{
	float controller = 0.f;
	float wrist = 0.f;
	float arm = 0.f;
};

struct AuthoredWalkMotion
{
	SVec3 rotation; // radians
	SVec3 translation; // metres
};

AuthoredMotionGains CalculateAuthoredMotionGains(const AuthoredMotionMetrics& metrics);
AuthoredWalkMotion CalculateAuthoredWalkMotion(float phase, float weight, const AuthoredMotionGains& gains);
// Share of the authored walk rotation that the arms follow.
float CalculateAuthoredArmFollow(float arm_gain);
// Wraps to -pi..pi.
float NormalizeAngle(float value);
// Shortest signed turn from current to target.
float AngleDifference(float target, float current);
float DegToRad(float value);
float RadToDeg(float value);
} // namespace Bodycam
