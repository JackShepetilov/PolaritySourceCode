// JumpSlotParams.h
// What the upgrade in the jump slot tells the movement simulation.

#pragma once

#include "CoreMinimal.h"

/** Which of the jump-slot upgrades is equipped. The slot holds one at a time, so the three never
 *  have to agree with each other about what a press of jump means. */
enum class EJumpSlotMode : uint8
{
	/** Nothing in the slot: one jump, no air jump. */
	None,
	/** Extra jumps in the air. */
	ExtraJump,
	/** Hold jump on the ground, let go to launch higher. */
	ChargedJump,
	/** Jump pressed in the air dashes instead of jumping. */
	AirDash,
};

/**
 * One read of the jump slot, handed to the movement simulation.
 *
 * A plain struct, not a USTRUCT: the authored copy lives in the upgrade's data asset, and this is
 * the movement side's private mirror of it. The movement asks it on the owning client, on the
 * server and on every replay, so the upgrade answering it must be a pure read of its level data.
 */
struct FJumpSlotParams
{
	EJumpSlotMode Mode = EJumpSlotMode::None;

	/** Seconds the slot is locked after use. Extra jump: after the first air jump of a flight.
	 *  Charged jump: after a jump that came out charged. Air dash: after a dash ends. */
	float Cooldown = 0.0f;

	// ==================== ExtraJump ====================

	/** Jumps allowed in the air on top of the ground one. 1 = double jump, 2 = triple. */
	int32 ExtraJumps = 1;

	// ==================== ChargedJump ====================

	/** Upward speed at a full charge. No charge gives MovementSettings::JumpZVelocity. */
	float ChargeMaxZVelocity = 1400.0f;

	/** How long the charge takes to fill. Holding past it adds nothing. */
	float ChargeTime = 0.55f;

	/** Horizontal speed added along the input direction at a full charge, scaled with it. */
	float ChargeForwardBoost = 400.0f;

	/** Ground speed multiplier while charging. */
	float ChargeMoveScale = 1.0f;

	// ==================== AirDash ====================

	/** Dashes per flight, refilled on landing. */
	int32 AirDashCharges = 1;

	/** Multiplier on MovementSettings::AirDashSpeed. */
	float AirDashSpeedMultiplier = 1.0f;
};
