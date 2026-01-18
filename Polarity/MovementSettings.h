// MovementSettings.h
// DataAsset ÃÂ´ÃÂ»Ã‘Â ÃÂ½ÃÂ°Ã‘ÂÃ‘â€šÃ‘â‚¬ÃÂ¾ÃÂ¹ÃÂºÃÂ¸ ÃÂ¿ÃÂ°Ã‘â‚¬ÃÂ°ÃÂ¼ÃÂµÃ‘â€šÃ‘â‚¬ÃÂ¾ÃÂ² ÃÂ´ÃÂ²ÃÂ¸ÃÂ¶ÃÂµÃÂ½ÃÂ¸Ã‘Â ÃÂ² Ã‘â‚¬ÃÂµÃÂ´ÃÂ°ÃÂºÃ‘â€šÃÂ¾Ã‘â‚¬ÃÂµ

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MovementSettings.generated.h"

UCLASS(BlueprintType)
class POLARITY_API UMovementSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== Ground ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float WalkSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float SprintSpeed = 1150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float CrouchSpeed = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float GroundAcceleration = 2048.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float GroundFriction = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float BrakingDeceleration = 2048.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float SpeedCap = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float DefaultGravityScale = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float StandingCapsuleHalfHeight = 96.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ground")
	float CrouchingCapsuleHalfHeight = 50.0f;

	// ==================== Air ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float AirAcceleration = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float AirControl = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float AirStrafeMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float MaxAirStrafeSpeed = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float AirSpeedCap = 2000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	bool bUseSourceAirAcceleration = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air")
	float SVAccelerate = 10.0f;

	// ==================== Air Dive (camera-directed descent) ====================

	/** Enable camera-directed dive when looking down in air */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air|Dive")
	bool bEnableAirDive = true;

	/** Camera pitch angle threshold to activate dive (negative = looking down). E.g. -15 means 15 degrees below horizon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air|Dive", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float AirDiveAngleThreshold = -15.0f;

	/** Multiplier for Z component of acceleration when diving. 1.0 = full camera direction, 0.0 = no vertical acceleration */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air|Dive", meta = (ClampMin = "0.0"))
	float AirDiveZMultiplier = 0.5f;

	// ==================== Jump ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	float JumpZVelocity = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	int32 MaxJumpCount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	float JumpHoldTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	float JumpHoldForce = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	bool bEnableCoyoteTime = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump")
	float CoyoteTime = 0.165f;

	// ==================== Jump Lurch ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	bool bEnableJumpLurch = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchGracePeriodMin = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchGracePeriodMax = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchVelocity = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchStrength = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchMax = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jump|Lurch")
	float JumpLurchSpeedLoss = 0.0f;

	// ==================== Slide ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Speed at which slide automatically ends. Titanfall default: 225"))
	float SlideMinSpeed = 225.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Minimum speed required to start sliding. Titanfall default: 850"))
	float SlideMinStartSpeed = 850.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlideBoostSpeed = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Ground friction during slide. Titanfall uses 0!"))
	float SlideFriction = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlideSlopeAcceleration = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlideCooldown = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Time before you get another speed boost from sliding. Titanfall default: 2.0s"))
	float SlideboostCooldown = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlideJumpBoost = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlidehopJumpZVelocity = 450.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Braking at slide start. Increases over time. Titanfall default: 375"))
	float SlideBrakingDecelerationMin = 375.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Braking after ~2s of sliding. Titanfall default: 750"))
	float SlideBrakingDecelerationMax = 750.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Speed boost at high entry speed. Titanfall default: 100"))
	float SlideMinSpeedBurst = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Speed boost at minimum entry speed. Titanfall default: 400"))
	float SlideMaxSpeedBurst = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "How much slopes affect slide. Higher = steeper slopes matter more"))
	float SlideFloorInfluenceForce = 850.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Extra slowdown on flat ground. Works with progressive braking"))
	float SlideFlatDeceleration = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ToolTip = "Additional slowdown when sliding uphill"))
	float SlideUphillDeceleration = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide")
	float SlideEndSpeed = 225.0f;

	// ==================== Mantle ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mantle")
	float MantleReachHeight = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mantle")
	float MantleTraceDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mantle")
	float MantleDuration = 0.4f;

	// ==================== Wallrun (now uses slide-style momentum) ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "Master switch for wallrun"))
	bool bEnableWallRun = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "How far to trace for walls. 50-100 cm typical"))
	float WallRunCheckDistance = 75.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "Must be this high to start/continue wallrun"))
	float WallRunMinHeight = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "Need at least this speed to initiate wallrun"))
	float WallRunMinSpeed = 300.0f;

	/** [NEW] Minimum speed to END wallrun - ends when below this */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "Wallrun ends when speed drops below this"))
	float WallRunEndSpeed = 150.0f;

	/** [DEPRECATED] Was constant speed - now momentum-based like floor slide */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "Legacy: was constant speed. Now uses entry momentum"))
	float WallRunSpeed = 900.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "How long gravity stays disabled"))
	float WallRunMaxDuration = 1.5f;

	/** [NEW] Deceleration during wallrun - applied after peak time */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "How fast you slow down on wall after peak"))
	float WallRunDeceleration = 300.0f;

	// ==================== Wallrun Speed Curve (Titanfall 2 style) ====================

	/** Acceleration during initial wallrun phase (before peak) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|SpeedCurve", meta = (ToolTip = "How fast speed builds up at start"))
	float WallRunAcceleration = 600.0f;

	/** Time to reach peak speed (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|SpeedCurve", meta = (ToolTip = "How long until max speed is reached"))
	float WallRunPeakTime = 0.4f;

	/** Peak speed multiplier (1.0 = entry speed, 1.5 = 50% faster than entry) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|SpeedCurve", meta = (ToolTip = "Max speed = entry speed * this"))
	float WallRunPeakSpeedMultiplier = 1.4f;

	/** Speed boost when jumping off wall */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|SpeedCurve", meta = (ToolTip = "Extra speed added when wall jumping"))
	float WallRunExitBoost = 150.0f;

	/** Minimum input dot product to wall direction to maintain wallrun (0.3 = ~70 degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|SpeedCurve", meta = (ToolTip = "Must hold input roughly parallel to wall"))
	float WallRunInputThreshold = 0.3f;

	// ==================== Wallrun Headbob ====================

	/** Maximum camera roll amplitude for wallrun headbob (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Headbob", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float WallRunHeadbobRollAmount = 3.0f;

	/** Distance traveled per full headbob cycle (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Headbob")
	float WallRunHeadbobStepLength = 150.0f;

	// ==================== Wallrun Speed Boost (Entry) ====================

	/** Minimum speed boost when entering wallrun at high speed (near BoostCap) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Boost", meta = (ToolTip = "Boost given at speeds near BoostCap"))
	float WallRunMinBoost = 50.0f;

	/** Maximum speed boost when entering wallrun at low speed (near MinSpeed) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Boost", meta = (ToolTip = "Boost given at speeds near MinSpeed"))
	float WallRunMaxBoost = 200.0f;

	/** Speed above which player gets MinBoost. Below this, interpolate towards MaxBoost */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Boost", meta = (ToolTip = "Speed threshold for minimum boost"))
	float WallRunBoostCap = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun", meta = (ToolTip = "0 = no gravity during wallrun"))
	float WallRunGravityScale = 0.0f;

	/** Camera roll angle during wall run (positive value, direction auto-applied based on wall side) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	float WallRunCameraRoll = 15.0f;

	/** First person mesh roll angle during wall run (positive value, direction auto-applied) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	float WallRunMeshTiltRoll = 8.0f;

	/** First person mesh pitch angle during wall run */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	float WallRunMeshTiltPitch = 3.0f;

	/** Camera offset when wall is on LEFT side */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	FVector WallRunCameraOffsetLeft = FVector(0.0f, -10.0f, 5.0f);

	/** Camera offset when wall is on RIGHT side */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	FVector WallRunCameraOffsetRight = FVector(0.0f, 10.0f, 5.0f);

	/** [DEPRECATED] Use WallRunCameraRoll instead */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera", meta = (DeprecatedProperty))
	FRotator WallRunCameraTilt = FRotator(0.0f, 0.0f, 15.0f);

	/** [DEPRECATED] Use WallRunCameraOffsetLeft/Right instead */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera", meta = (DeprecatedProperty))
	FVector WallRunCameraOffset = FVector(0.0f, 0.0f, 5.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Camera")
	float WallRunCameraTiltSpeed = 10.0f;

	/** Enable Titanfall 2 style capsule shrink + tilt during wallrun */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Capsule")
	bool bEnableWallRunCapsuleTilt = true;

	/** Capsule half-height during wallrun (default = CrouchingCapsuleHalfHeight) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun|Capsule", meta = (EditCondition = "bEnableWallRunCapsuleTilt"))
	float WallRunCapsuleHalfHeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallJumpUpForce = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallJumpSideForce = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallRunSameWallCooldown = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	bool bUseWallrunGravity = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallrunGravityCounterAcceleration = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallrunSpeedLossDelay = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallrunCameraTiltInterpSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float ExitWallTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wallrun")
	float WallrunKickoffDuration = 0.3f;

	// ==================== Wall Bounce ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Bounce", meta = (ToolTip = "Bounce off walls when flying into them with forward held"))
	bool bEnableWallBounce = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Bounce", meta = (ToolTip = "Must be flying into wall at least this fast to bounce"))
	float WallBounceMinSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Bounce", meta = (ClampMin = "0.0", ClampMax = "1.5", ToolTip = "0.8 = lose 20% energy"))
	float WallBounceElasticity = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Bounce", meta = (ClampMin = "15.0", ClampMax = "90.0", ToolTip = "Must hit wall at least this perpendicular"))
	float WallBounceMinAngle = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Bounce")
	float WallBounceCooldown = 0.3f;

	// ==================== Ledge Grab ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	bool bEnableLedgeGrab = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float LedgegrabCheckDistance = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float LedgegrabSphereCastRadius = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float LedgegrabMaxSpeed = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float MaxLedgegrabDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float MoveToLedgeAcceleration = 2000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float MinTimeOnLedge = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float ExitLedgeTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float LedgegrabJumpBackForce = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ledge Grab")
	float LedgegrabJumpUpForce = 600.0f;

	// ==================== Air Dash ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash")
	float AirDashSpeed = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash")
	float AirDashCooldown = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash")
	int32 MaxAirDashCount = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash|Decay", meta = (ToolTip = "Above this height - no decay"))
	float AirDashDecayMaxHeight = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash|Decay", meta = (ToolTip = "At this height - maximum decay"))
	float AirDashDecayMinHeight = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash|Decay", meta = (ToolTip = "Speed loss per second at min height"))
	float AirDashDecayRate = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash|Decay", meta = (ToolTip = "How long decay applies after dash"))
	float AirDashDecayDuration = 0.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air Dash|Decay", meta = (ToolTip = "Speed won't drop below this"))
	float AirDashMinSpeed = 1000.0f;

	// ==================== EMF ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMF")
	float EMFForceMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMF")
	float MaxEMFVelocity = 2000.0f;

	// ==================== Camera|General ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|General")
	bool bEnableCameraShake = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|General")
	float CameraShakeIntensity = 1.0f;

	// ==================== Camera|Headbob ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	bool bEnableHeadbob = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobWalkAmplitudeZ = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobWalkAmplitudeY = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobWalkRoll = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobWalkFrequency = 1.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobSprintMultiplier = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobSprintFrequencyMultiplier = 1.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Headbob")
	float HeadbobInterpSpeed = 8.0f;

	// ==================== Camera|Landing ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	bool bEnableLandingShake = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeMinVelocity = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeMaxVelocity = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeMaxPitch = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeZAmplitude = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeFrequency = 18.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Landing")
	float LandingShakeDamping = 6.0f;

	// ==================== Camera|Jump ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Jump")
	bool bEnableJumpShake = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Jump")
	float JumpCameraKick = -3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Jump")
	float DoubleJumpKickMultiplier = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Jump")
	float JumpShakeFrequency = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Jump")
	float JumpShakeDamping = 8.0f;

	// ==================== Camera|Slide ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	bool bEnableSlideShake = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	float SlideShakeIntensity = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	float SlideShakeFrequency = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	float SlideCameraPitch = 3.0f;

	// ==================== Camera|Wallrun ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	bool bEnableWallrunBob = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	float WallrunBobAmplitude = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	float WallrunBobFrequency = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	bool bEnableWallrunFOV = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	float WallrunFOVAdd = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Wallrun")
	float WallrunFOVInterpSpeed = 8.0f;

	// ==================== Camera|Air Dash ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Air Dash")
	bool bEnableAirDashShake = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Air Dash")
	float AirDashFOVAdd = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Air Dash")
	float AirDashFOVDuration = 0.3f;

	// ==================== First Person View ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	bool bEnableFirstPersonOffset = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	FVector CrouchCameraOffset = FVector(0.0f, 0.0f, -40.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	FVector SlideCameraOffset = FVector(0.0f, 0.0f, -50.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float CrouchCameraZOffset = -40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float SlideCameraZOffset = -50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float CameraZOffsetInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	bool bEnableWeaponTilt = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float CrouchWeaponTiltRoll = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float CrouchWeaponTiltPitch = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float SlideWeaponTiltRoll = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float SlideWeaponTiltPitch = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View")
	float WeaponTiltInterpSpeed = 10.0f;

	// ==================== ADS ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	bool bEnableADS = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	float ADSInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	float ADSCameraFOV = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	float ADSFirstPersonFOV = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	FVector DefaultADSOffset = FVector(15.0f, 0.0f, 5.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ADS")
	float ADSMovementSpeedMultiplier = 0.5f;

	// ==================== Procedural Footsteps ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	bool bEnableProceduralFootsteps = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepWalkInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepSprintInterval = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepWallrunInterval = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepMinSpeedRatio = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepVolume = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	float FootstepPitchVariation = 0.1f;

	// ==================== Weapon Run Sway ====================

	/** Enable procedural weapon sway during running */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run")
	bool bEnableWeaponRunSway = true;

	/**
	 * Curve defining weapon rotation during run cycle.
	 * X axis: 0-1 normalized step phase (0=step start, 0.5=mid-stride, 1=next step)
	 * Y axis: Rotation multiplier (-1 to 1)
	 *
	 * Use CurveVector with:
	 * - X channel = Roll (left/right tilt)
	 * - Y channel = Pitch (up/down)
	 * - Z channel = Yaw (optional, usually 0)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run")
	TObjectPtr<class UCurveVector> WeaponRunSwayCurve;

	/** Maximum Roll angle during run sway (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float WeaponRunSwayRollAmount = 4.0f;

	/** Maximum Pitch angle during run sway (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float WeaponRunSwayPitchAmount = 2.0f;

	/** Maximum Yaw angle during run sway (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WeaponRunSwayYawAmount = 0.5f;

	/** Distance traveled per full sway cycle (cm) - matches footstep cadence */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "50.0", ClampMax = "500.0"))
	float WeaponRunSwayStepDistance = 150.0f;

	/** Speed at which sway is at full intensity (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "100.0"))
	float WeaponRunSwayMaxSpeedRef = 600.0f;

	/** Minimum speed to start sway (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.0"))
	float WeaponRunSwayMinSpeed = 200.0f;

	/** Sprint multiplier for sway intensity */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WeaponRunSwaySprintMultiplier = 1.3f;

	/** Sprint multiplier for sway frequency (faster steps = faster sway) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WeaponRunSwaySprintFrequencyMultiplier = 1.2f;

	/** Interpolation speed for sway intensity changes */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float WeaponRunSwayInterpSpeed = 8.0f;

	/** Optional position offset curve (X=Right, Y=Forward, Z=Up) - small amounts recommended */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run")
	TObjectPtr<class UCurveVector> WeaponRunSwayPositionCurve;

	/** Maximum position offset during run sway (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Run", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float WeaponRunSwayPositionAmount = 1.5f;

	// ==================== Weapon Aim Offset ====================

	/** Enable aim offset during running (shifts where weapon points) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Aim Offset")
	bool bEnableRunAimOffset = true;

	/**
	 * Aim offset during running in camera-local space (cm).
	 * X = Forward (usually 0)
	 * Y = Right (positive = aim right)
	 * Z = Up (positive = aim up)
	 *
	 * This shifts the IK target point, making the weapon point slightly off-center.
	 * Recommended values: Y = -20 to -50, Z = -20 to -50 for "lowered weapon" look.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Aim Offset")
	FVector RunAimOffset = FVector(0.0f, -30.0f, -40.0f);

	/** Aim offset during sprinting (usually more pronounced) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Aim Offset")
	FVector SprintAimOffset = FVector(0.0f, -50.0f, -60.0f);

	/** Interpolation speed for aim offset transitions */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Aim Offset", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float AimOffsetInterpSpeed = 8.0f;

	/** Minimum speed to apply run aim offset (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Sway|Aim Offset", meta = (ClampMin = "0.0"))
	float AimOffsetMinSpeed = 200.0f;
};