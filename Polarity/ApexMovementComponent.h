// ApexMovementComponent.h
// Titanfall 2 / Apex Legends style movement system

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ApexMovementComponent.generated.h"

class UMovementSettings;
class IVelocityModifier;

UENUM(BlueprintType)
enum class EPolarityMovementState : uint8
{
	None,
	Walking,
	Sprinting,
	Crouching,
	Sliding,
	Falling,
	Mantling,
	WallRunning,
	GroundDashing
};

UENUM(BlueprintType)
enum class EWallSide : uint8
{
	None,
	Left,
	Right
};

/** How the wallrun ended - determines if player can double jump after */
UENUM(BlueprintType)
enum class EWallRunEndReason : uint8
{
	None,
	/** Player jumped off wall - no double jump allowed */
	JumpedOff,
	/** Wallrun time expired - one jump allowed */
	TimeExpired,
	/** Lost contact with wall or other reason */
	LostWall
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMovementStateChanged, EPolarityMovementState, PreviousState, EPolarityMovementState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWallRunChanged, bool, bIsWallRunning, EWallSide, Side);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLanded_Movement, const FHitResult&, Hit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWallrunStarted, EWallSide, Side);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWallrunEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWallBounce, FVector, BounceDirection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMantleStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMantleEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAirDashStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAirDashEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGroundDashStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGroundDashEnded);
/** Broadcast whenever a dash cooldown, charge count, or active state changes. HUD clients re-read the snapshot getters. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDashStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJumpPerformed, bool, bIsDoubleJump);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPreVelocityUpdate, float, FVector&);

/**
 * What this character decided to do during one simulated move.
 *
 * The engine gives a movement component four spare bits inside the move it already sends every
 * frame (FSavedMove_Character::FLAG_Custom_0..3). Sprint, slide, wallrun and slide-on-land filled
 * all four, and dash and mantle had nowhere to go, so this project sends its own word instead. It
 * costs two bytes per move and only when something is actually happening, because zero is omitted
 * from the stream entirely — a character that is walking normally pays one bit.
 *
 * Each entry is a DECISION the owning client made, never a measurement it took. The server re-derives
 * every number for itself: told "wall running", it runs its own trace and finds its own wall, and if
 * it finds none, no wallrun happens. Geometry from a client is not trusted.
 *
 * The melee lunge is the one exception, and a deliberate one: the target POSITION travels alongside
 * the flags (see FCharacterNetworkMoveData_Polarity::MeleeLungeTarget). Letting the server pick its
 * own lunge target from the view rotation would have the two ends fly at different enemies whenever
 * two of them stand close together, which is the exact desync this is meant to remove.
 */
enum class EPolarityMoveFlag : uint16
{
	None             = 0,
	WantsToSprint    = 1 << 0,
	Sliding          = 1 << 1,
	WallRunning      = 1 << 2,
	/** Crouch was pressed in the air, so landing turns into a slide instead of a stop. */
	WantsSlideOnLand = 1 << 3,
	GroundDashing    = 1 << 4,
	AirDashing       = 1 << 5,
	/** An air dash that is being steered mid-flight rather than flying straight. */
	AirDashRedirect  = 1 << 6,
	Mantling         = 1 << 7,
	/** A melee swing is driving velocity this move: the character holds the speed the swing started
	 *  with instead of braking, which is what makes a punch at 2000 u/s stay a punch at 2000 u/s. */
	MeleeLunging     = 1 << 8,
	/** The swing found somebody. Gravity is off for the flight, and Z is restored from the swing's
	 *  start speed rather than left wherever the flight put it. */
	MeleeLungeHasTarget = 1 << 9,
	/** Flying at MeleeLungeTarget right now, as opposed to holding momentum next to a target that has
	 *  already been hit or knocked back. */
	MeleeLungeHoming = 1 << 10,
	/** The swing missed: hand the character back the momentum it started with when the lunge ends. */
	MeleeLungeRestore = 1 << 11,
	/** This flight is a drop kick dive rather than a lunge: constant speed at the target instead of
	 *  closing speed, gravity left on, and a forward exit when it ends. It reuses the lunge's target
	 *  and target actor rather than sending a second set — it is the same shape of flight. */
	MeleeDropKick     = 1 << 12,
	/** The player was pushing forward as the dive ended, so they carry momentum out of it instead of
	 *  stopping dead. A decision, not a measurement: the server reads a remote pawn's input as
	 *  nothing at all, so it has to travel or every client dropkick would end in a dead stop. */
	MeleeDropKickForward = 1 << 13,
};
ENUM_CLASS_FLAGS(EPolarityMoveFlag);

/**
 * One client move on the wire, with our byte appended to what the engine already sends.
 *
 * @see UApexMovementComponent::ServerMove_PerformMovement, which unpacks it on the server.
 */
struct FCharacterNetworkMoveData_Polarity : public FCharacterNetworkMoveData
{
	typedef FCharacterNetworkMoveData Super;

	uint16 PolarityFlags = 0;

	/** Where the melee lunge is flying, in world space. On the wire only while MeleeLungeHoming is
	 *  set — that flag is its presence bit, so it costs nothing at all the rest of the time. */
	FVector_NetQuantize10 MeleeLungeTarget = FVector::ZeroVector;

	/** WHO it is flying at, on the same terms. Needed as well as the position because the two ends
	 *  have to agree about move-collision: the flight parks the character inside the target's
	 *  capsule, so both sides must ignore it for the duration or the server keeps colliding while
	 *  the client passes through, and they disagree about where the character ended up.
	 *  Sent the way the engine sends MovementBase. */
	TObjectPtr<UObject> MeleeLungeTargetActor = nullptr;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar,
		UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

/**
 * Holds the three moves the engine sends together: the newest one, the one before it when two are
 * combined, and an old unacknowledged one being resent. All three have to be our type, which is the
 * only reason this struct exists.
 */
struct FCharacterNetworkMoveDataContainer_Polarity : public FCharacterNetworkMoveDataContainer
{
	FCharacterNetworkMoveDataContainer_Polarity()
	{
		NewMoveData     = &PolarityMoveData[0];
		PendingMoveData = &PolarityMoveData[1];
		OldMoveData     = &PolarityMoveData[2];
	}

	FCharacterNetworkMoveData_Polarity PolarityMoveData[3];
};

/**
 * Titanfall 2 / Apex Legends style movement component.
 * Features: Slide with proper friction, WallRun (now slide-style), WallBounce, Mantle, Air Dash, Double Jump
 */
UCLASS(ClassGroup = "Movement", meta = (BlueprintSpawnableComponent))
class POLARITY_API UApexMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UApexMovementComponent();

	// ==================== Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Settings")
	TObjectPtr<UMovementSettings> MovementSettings;

	/** Camera shake on Jump */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> JumpCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> LandCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SlideStartCameraShake;

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SlideEndCameraShake;


	// ==================== State ====================

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EPolarityMovementState CurrentMovementState = EPolarityMovementState::None;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	int32 CurrentJumpCount = 0;

	/** The sprint decision this move was simulated with, suppression already folded in.
	 *
	 *  This is the value that travels to the server, so it must be the *whole* client-side answer:
	 *  it is packed into the saved move's compressed flags and unpacked by the server, which then
	 *  replays the move at the same speed instead of correcting the client back (the rubber band).
	 *  Do not set it from input directly — set bSprintKeyHeld and let the per-frame fold do it,
	 *  or the server and the client will disagree the moment something vetoes a sprint.
	 *
	 *  Replicated SimulatedOnly on top of that: the flags only go client -> server, so without this
	 *  copy a teammate's sprint is invisible to everyone else and their character keeps aiming its
	 *  weapon at the camera while running. The owner is excluded because it decided the value. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bWantsToSprint = false;

	/** Raw input: the sprint key is held. Purely local, never networked — the server learns the
	 *  outcome through bWantsToSprint, not the keypress. */
	bool bSprintKeyHeld = false;

	/** World time until which sprint stays suppressed. Kept separate from the key state on
	 *  purpose: the key is the player's *intent*, suppression is a temporary veto from something
	 *  else. Clearing the intent instead would force the player to release and press sprint again
	 *  after every shot. */
	float SprintSuppressedUntil = -1000.0f;

	/** World time sprinting last ended. The weapon raise gate measures from here. */
	float SprintEndTime = -1000.0f;

	/** Replicated so that machines which only watch this character can shrink its capsule and pick
	 *  the right pose. The owner and the server agree through the saved move instead; this copy is
	 *  for everyone else, which is why it is SimulatedOnly. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bIsSliding = false;

	/** Replicated for the same reason bIsSliding is: the owner and the server agree through the move,
	 *  but a machine that only watches this character learns nothing from a move it never receives,
	 *  and its anim blueprint would keep a dashing teammate in a plain fall. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bIsMantling = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bIsAirDashing = false;

	/** True while the short ground dash speed-maintenance window is active. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bIsGroundDashing = false;

	/** Replicated for the same reason as bIsSliding: observers need it for the pose. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Apex|State")
	bool bIsWallRunning = false;

	/** Set by external systems (e.g. ShooterCharacter when a riot shield is equipped) to fully gate wallrun. */
	UPROPERTY()
	bool bWallRunExternallyDisabled = false;

	/** True during the start-of-run sea-toss flight. Suppresses jump / air dash / wallrun / mantle so
	 *  the arc stays deterministic. Crouch->slide-on-land is intentionally still allowed. */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bRunLaunchActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EWallSide WallRunSide = EWallSide::None;

	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	int32 RemainingAirDashCount = 1;

	/** True when player holds crouch in air - will slide on landing (Titanfall 2 mechanic) */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bWantsSlideOnLand = false;

	/** True when player is crouched in air (reduces hitbox) */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	bool bIsCrouchedInAir = false;

	/** How the last wallrun ended - affects post-wallrun jump availability */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	EWallRunEndReason LastWallRunEndReason = EWallRunEndReason::None;

	// ==================== Delegates ====================

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnMovementStateChanged OnMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallRunChanged OnWallRunChanged;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnLanded_Movement OnLanded_Movement;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnSlideStarted OnSlideStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnSlideEnded OnSlideEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallrunStarted OnWallrunStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallrunEnded OnWallrunEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnWallBounce OnWallBounce;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnMantleStarted OnMantleStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnMantleEnded OnMantleEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnAirDashStarted OnAirDashStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnAirDashEnded OnAirDashEnded;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnGroundDashStarted OnGroundDashStarted;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnGroundDashEnded OnGroundDashEnded;

	/** UI notification for dash cooldown, charges, and context changes. */
	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnDashStateChanged OnDashStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Apex|Events")
	FOnJumpPerformed OnJumpPerformed;

	FOnPreVelocityUpdate OnPreVelocityUpdate;

	// ==================== Overrides ====================

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Network prediction ====================
	// Sprint changes max speed, and max speed decides where the character ends up, so the server
	// has to know about it or it will replay the client's moves slower than the client did and
	// yank it back. Every such decision therefore rides along inside the saved move rather than
	// being replicated after the fact. See EPolarityMoveFlag for what travels and why.

	/** Reads our byte off the wire and applies it, then lets the engine run the move.
	 *  Server side only: a replaying client restores the same state through PrepMoveFor instead. */
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;

	/** Where the stashed byte is actually applied, with this move's acceleration already in place. */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags,
		const FVector& NewAccel) override;

	/** The flags from the move currently being unpacked. Server side only, valid for the length of
	 *  one ServerMove_PerformMovement call. */
	uint16 PendingPolarityFlags = 0;

	/** The lunge target from that same move. Applied together with the flags in MoveAutonomous. */
	FVector PendingMeleeLungeTarget = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PendingMeleeLungeTargetActor;

	/** What this character is doing right now, as the flags that go on the wire. */
	uint16 PackPolarityMoveFlags() const;

	/** Turns a received byte into actual state. Assignment for the plain intents, and real entry and
	 *  exit calls for anything that has to set up more than a bool: a side that only flipped
	 *  bIsSliding kept braking normally and finished the move somewhere else, which in game read as
	 *  "sliding works for one player and stutters for the other". Wallrun, dash and mantle are the
	 *  same shape, so they go through their own Start/End here too. The geometry is re-derived
	 *  locally, never taken from the client. */
	void ApplyPolarityMoveFlags(uint16 Flags);

	/** The replay counterpart: plain assignment, no entry or exit calls.
	 *
	 *  A replay re-runs a move that was already simulated once from exactly this state, so calling
	 *  StartSlide and friends again would apply their entry effects a second time on top of a
	 *  velocity that already contains them. */
	void ApplyPolarityMoveFlagsForReplay(uint16 Flags);

	/** The character's own mechanics that write velocity before the move is integrated: mantle,
	 *  wallrun, dash and the wall checks feeding them. The engine calls this inside PerformMovement,
	 *  which is what makes them part of the simulation the server replays. It is also where the
	 *  engine itself resolves crouching. */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Being carried by a teammate, run from inside the simulated move.
	 *
	 *  It lives here rather than in whoever is doing the carrying because this is the ONLY place that
	 *  works for a player: a character predicts its own movement, so anything moving it from another
	 *  machine is corrected away every update. Driven from AShooterCharacter::HeldByCharacter, which
	 *  is replicated, so the held client, the server and a replay all reach the same answer.
	 *
	 *  The held player loses control for the duration: input is discarded here rather than blocked at
	 *  the input layer, so the server's replay of their move discards it too. */
	void UpdateHeldByAlly(float DeltaSeconds);

	/** How hard a carried teammate is pulled to the hold point (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Carry", meta = (ClampMin = "1.0"))
	float AllyHoldSpringRate = 12.0f;

	/** Ceiling on the speed that pull may reach, so a carrier turning fast cannot fling the carried
	 *  player through the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Carry", meta = (ClampMin = "100.0", Units = "cm/s"))
	float AllyHoldMaxSpeed = 3000.0f;

	/** True while this character was moved by the carry last frame, so the exit can put the movement
	 *  mode back exactly once instead of every frame. */
	bool bIsHeldByAlly = false;

	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime = 0.f) override;

	/** Air strafe lives here and not in the tick. Everything that changes velocity has to run inside
	 *  the movement simulation, because that is the only code the server re-runs when it replays a
	 *  client's move and the only code a client re-runs after a correction. Done from the tick it
	 *  happened on exactly one machine and got corrected away everywhere else. */
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

	/** Runs once per simulated move on every machine, replays included. Timers that gate movement
	 *  (slide cooldowns, slide fatigue) live here and not in TickComponent for the same reason air
	 *  strafe does: a tick-driven timer counts down at the local frame rate and does not rewind
	 *  during a replay, so client and server end up gating different frames. */
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

	/** Enforces "momentum only" for slide, wallrun and ground dash. Doing it here instead of in
	 *  GetMaxAcceleration keeps the input direction alive in Acceleration, which is what steers a
	 *  slide and the only input a server ever sees. */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

	// ==================== Input ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StopSprint();

	/** Veto sprinting for a while without touching the player's intent. Firing and aiming call
	 *  this every frame they are active, so sprint resumes on its own SprintSuppressionTime after
	 *  the last shot rather than requiring the key to be pressed again.
	 *  Duration < 0 uses SprintSuppressionTime. Extends an existing suppression, never shortens it. */
	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void SuppressSprint(float Duration = -1.0f);

	/** How long sprint stays vetoed after the last shot, or after aim is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Sprint", meta = (ClampMin = "0.0"))
	float SprintSuppressionTime = 0.3f;

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void TryCrouchSlide();

	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void StopCrouchSlide();

	// ==================== Slide ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanSlide() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartSlide();

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void EndSlide();

	/** Start slide from air landing - preserves and boosts momentum (Titanfall 2 mechanic) */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartSlideFromAir(float FallSpeed);

	// ==================== WallRun (slide-style) ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanWallRun() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartWallRun(const FHitResult& WallHit, EWallSide Side);

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void EndWallRun(EWallRunEndReason Reason = EWallRunEndReason::LostWall);

	/** External gate: when true, CanWallRun() always returns false and an active wallrun is interrupted. */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void SetWallRunExternallyDisabled(bool bDisabled);

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsWallRunExternallyDisabled() const { return bWallRunExternallyDisabled; }

	/** Temporary gameplay override for abilities that need CharacterMovement collision with a higher speed cap. */
	void SetExternalMaxSpeedOverride(float MaxSpeed);
	void ClearExternalMaxSpeedOverride();

	/** Temporary per-character slide burst override used by weapon-specific upgrades. */
	void SetExternalSlideSpeedBurstOverride(float MinBurst, float MaxBurst);
	void ClearExternalSlideSpeedBurstOverride();

	/** Enable/disable the start-of-run launch state (suppresses air abilities while the toss arc plays). */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void SetRunLaunchActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsWallRunning() const { return bIsWallRunning; }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	FRotator GetWallRunCameraTilt() const { return CurrentWallRunCameraTilt; }

	// ==================== Wall Bounce ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanWallBounce() const;

	// ==================== Mantle ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanMantle() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void TryMantle();

	// ==================== Dash ====================

	/** Ground-only Doom-style dash. Uses CharacterMovement velocity and collision, never direct actor movement. */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanGroundDash() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool TryGroundDash();

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool CanAirDash() const;

	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool TryAirDash();

	// ==================== Smooth Crouch ====================

	/** Start crouching with smooth capsule interpolation */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StartCrouching();

	/** Stop crouching with smooth capsule interpolation (checks for clearance) */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	void StopCrouching();

	/** Check if there's enough room to stand up */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool CanStandUp() const;

	/** Speed of capsule height interpolation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Crouch")
	float CapsuleInterpSpeed = 15.0f;

	// ==================== Queries ====================

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsSprintSuppressed() const;

	/** Suppression is deliberately NOT re-tested here: it is already folded into bWantsToSprint
	 *  once per frame, and that folded value is what the server received. Testing it again would
	 *  let the local answer drift from the one the move was packed with. */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsSprinting() const { return bWantsToSprint && !bIsSliding && !IsCrouching() && IsMovingOnGround(); }

	/** World time sprinting last ended, for whoever needs to measure from it (the weapon raise
	 *  gate does). Far in the past when the character has never sprinted. */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	float GetSprintEndTime() const { return SprintEndTime; }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsSliding() const { return bIsSliding; }

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsGroundDashing() const { return bIsGroundDashing; }

	UFUNCTION(BlueprintPure, Category = "Apex|Dash")
	float GetGroundDashCooldownRemaining() const { return FMath::Max(0.0f, GroundDashCooldownRemaining); }

	UFUNCTION(BlueprintPure, Category = "Apex|Dash")
	float GetGroundDashCooldownDuration() const;

	UFUNCTION(BlueprintPure, Category = "Apex|Dash")
	float GetAirDashCooldownRemaining() const { return FMath::Max(0.0f, AirDashCooldownRemaining); }

	UFUNCTION(BlueprintPure, Category = "Apex|Dash")
	float GetAirDashCooldownDuration() const;

	UFUNCTION(BlueprintPure, Category = "Apex|Dash")
	int32 GetMaxAirDashCount() const;

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	float GetSpeedRatio() const;

	/** Get current slide duration in seconds */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	float GetSlideDuration() const { return SlideDuration; }

	/** Get current slide fatigue level (0-5) */
	UFUNCTION(BlueprintPure, Category = "Apex|State")
	int32 GetSlideFatigue() const { return SlideFatigueCounter; }

	/** Reset all movement states (for respawn) */
	UFUNCTION(BlueprintCallable, Category = "Apex|State")
	void ResetMovementState();

	// ==================== Input Tracking ====================

	/** Set current move input for jump lurch calculations */
	UFUNCTION(BlueprintCallable, Category = "Apex|Input")
	void SetMoveInput(const FVector2D& Input) { CurrentMoveInput = Input; }

	/** Get current move input */
	UFUNCTION(BlueprintPure, Category = "Apex|Input")
	FVector2D GetMoveInput() const { return CurrentMoveInput; }

	/** Check if forward input is held */
	UFUNCTION(BlueprintPure, Category = "Apex|Input")
	bool IsForwardHeld() const { return CurrentMoveInput.Y > 0.5f; }

	/** The same question as IsForwardHeld, asked in a way the server can answer.
	 *
	 *  CurrentMoveInput is written by the local input handler, so on the server a client's pawn
	 *  always reads "nothing held". Acceleration is the one piece of input the network carries:
	 *  the client packs it into the saved move, the server rebuilds it from ServerMove and a replay
	 *  restores it. Comparing it against the view direction gives back "pressing forward". */
	bool IsAccelerationForward() const;

	/** Check if crouch input is held (regardless of actual crouch state) */
	UFUNCTION(BlueprintPure, Category = "Apex|Input")
	bool IsCrouchInputHeld() const { return bWantsToCrouchSmooth; }

	/** Try to perform jump with all checks */
	UFUNCTION(BlueprintCallable, Category = "Apex|Actions")
	bool TryJump();

	/** Fire the jump feedback: camera shake for whoever is locally driving this character, plus
	 *  the OnJumpPerformed event. Called from every successful path inside DoJump, so it works
	 *  the same whether the jump resolved from local input or from a replicated move. */
	void NotifyJumpPerformed(bool bWasAirJump);

	// ==================== Camera State (for Character to read) ====================

	/** Current camera roll from wallrun - ONLY roll is used */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunCameraRoll = 0.0f;

	/** Current camera offset from wallrun */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FVector CurrentWallRunCameraOffset = FVector::ZeroVector;

	/** Current mesh roll from wallrun - same logic as camera */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunMeshRoll = 0.0f;

	/** Current mesh pitch from wallrun */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float CurrentWallRunMeshPitch = 0.0f;

	/** [DEPRECATED] Use CurrentWallRunCameraRoll instead */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FRotator CurrentCameraTilt = FRotator::ZeroRotator;

	/** [DEPRECATED] Use CurrentWallRunCameraOffset instead */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	FVector CurrentCameraOffset = FVector::ZeroVector;

	/** Last fall velocity before landing (for camera shake) */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float LastFallVelocity = 0.0f;

	// ==================== Damage Speed Modifier ====================

	/** Speed multiplier applied when taking damage (0-1, where 1 = no slowdown).
	 *  Set by ShooterCharacter's damage slowdown system. */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float DamageSpeedMultiplier = 1.0f;

	/** External speed multiplier driven by upgrades (e.g. the full-HP bonus). 1.0 = no change.
	 *  Multiplied into GetMaxSpeed() alongside DamageSpeedMultiplier. This is a per-component
	 *  value (not a shared DataAsset), so an upgrade may write it directly and reset to 1.0 on removal. */
	UPROPERTY(BlueprintReadOnly, Category = "Apex|State")
	float ExternalSpeedMultiplier = 1.0f;

	UPROPERTY()
	bool bExternalMaxSpeedOverride = false;

	UPROPERTY()
	float ExternalMaxSpeedOverride = 0.0f;

	UPROPERTY()
	bool bExternalSlideSpeedBurstOverride = false;

	UPROPERTY()
	float ExternalSlideMinSpeedBurst = 0.0f;

	UPROPERTY()
	float ExternalSlideMaxSpeedBurst = 0.0f;

	// ==================== Melee lunge ====================
	// UMeleeAttackComponent decides WHETHER to lunge and WHERE to; the flight itself lives here,
	// because everything that writes Velocity has to run inside the simulated move or the server
	// never re-runs it. Driven from the melee component's tick on the owning client and from the
	// received flags on the server — the two never both write, because a remote client's melee
	// component on the server is idle (it is fed by local input, which the server does not have).

	/** Publish this frame's lunge decision. Safe to call every frame with the same values.
	 *
	 *  @param bLunging    the swing is in a phase that drives velocity (windup / active)
	 *  @param bHasTarget  the swing acquired somebody, so gravity stays off for the flight
	 *  @param bHoming     fly at InTarget right now, rather than just holding momentum
	 *  @param InTarget    world position to fly to; ignored unless bHoming */
	void SetMeleeLungeIntent(bool bLunging, bool bHasTarget, bool bHoming, const FVector& InTarget,
		AActor* InTargetActor, bool bDropKick = false, bool bDropKickForwardHeld = false);

	/** The swing ended without connecting: the momentum it started with is handed back when the
	 *  lunge stops, so a miss does not cost the player their run. Cleared by the next lunge. */
	void SetMeleeLungeRestoreOnEnd(bool bRestore) { bMeleeLungeRestoreOnEnd = bRestore; }

	/** Tunables mirrored from FMeleeAttackSettings so this side does not have to reach into the melee
	 *  component. Mirrored once in UMeleeAttackComponent::BeginPlay, which runs on every machine from
	 *  the same Blueprint defaults, so both ends fly at the same speed. */
	void SetMeleeLungeTuning(float MaxSpeed, float MomentumRatio, bool bDisableGravity, float DropKickSpeed);

	UFUNCTION(BlueprintPure, Category = "Apex|State")
	bool IsMeleeLunging() const { return bIsMeleeLunging; }

	// ==================== EMF ====================

	UFUNCTION(BlueprintCallable, Category = "Apex|EMF")
	void SetEMFForce(const FVector& Force);

	UFUNCTION(BlueprintPure, Category = "Apex|EMF")
	FVector GetEMFForce() const { return CurrentEMFForce; }

	// ==================== Velocity Modifiers ====================

	void RegisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier);
	void UnregisterVelocityModifier(TScriptInterface<IVelocityModifier> Modifier);

protected:
	/** The saved move has to read and write the predicted state below to record a move and to put
	 *  the component back the way it was before replaying one. */
	friend class FSavedMove_Polarity;

	/** Storage the engine packs client moves into. It has to outlive every send, which is why it is
	 *  a member here rather than a local; the constructor hands its address to the engine with
	 *  SetNetworkMoveDataContainer and nothing else ever touches it. */
	FCharacterNetworkMoveDataContainer_Polarity PolarityMoveDataContainer;

	// Velocity Modifiers
	UPROPERTY()
	TArray<TScriptInterface<IVelocityModifier>> VelocityModifiers;


	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;
	// Input tracking for jump lurch
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;

	// ==================== Melee lunge state ====================
	// All of it is per-move state, restored by FSavedMove_Polarity::PrepMoveFor before a replay.

	/** The decision: the melee swing wants velocity this move. This is what travels, set either by
	 *  the melee component here or by the flags off the wire on the server. */
	bool bMeleeLungeWanted = false;

	/** The state: a lunge is actually running. Kept apart from the decision above so that entry and
	 *  exit are resolved in UpdateCharacterStateBeforeMovement, i.e. INSIDE the simulated move, on
	 *  every machine at the same simulated moment. Doing it where the decision is made would put the
	 *  captured momentum and the miss impulse one frame apart on the two ends. */
	bool bIsMeleeLunging = false;

	/** This lunge has a target, so gravity is off and Z is owned by the flight. */
	bool bMeleeLungeHasTarget = false;

	/** The flight is a drop kick. @see EPolarityMoveFlag::MeleeDropKick */
	bool bMeleeDropKick = false;

	/** Forward was held as it ended. Consumed by EndMeleeLunge. */
	bool bMeleeDropKickForward = false;

	/** Flying at MeleeLungeTarget, as opposed to holding momentum beside a target already hit. */
	bool bMeleeLungeHoming = false;

	/** The swing missed, so EndMeleeLunge gives the entry momentum back. */
	bool bMeleeLungeRestoreOnEnd = false;

	/** THIS lunge is the one holding gravity at zero, decided once at entry and not re-derived after.
	 *  bMeleeLungeHasTarget can go false in the middle of a flight — the enemy dies, or gets knocked
	 *  out of the world — and deriving the restore from it would leave the character with gravity
	 *  switched off for the rest of the run. */
	bool bMeleeLungeGravityOff = false;

	/** Where the flight is going, world space. Recomputed every frame by the melee component on the
	 *  owning client and taken off the wire on the server — never re-derived locally, see
	 *  EPolarityMoveFlag. */
	FVector MeleeLungeTarget = FVector::ZeroVector;

	/** Who it is flying at. Both ends drop move-collision with this actor for the flight and put it
	 *  back afterwards, so neither can end up somewhere the other cannot reach. */
	TWeakObjectPtr<AActor> MeleeLungeTargetActor;

	/** Whether this side currently has that ignore in place, so it is removed exactly once and only
	 *  by the flight that added it. */
	bool bMeleeLungeIgnoringTarget = false;

	/** The speed the swing started at, captured on this side when the lunge flag rises. A measurement,
	 *  so each machine takes its own: by then both have simulated every earlier move identically. */
	FVector MeleeLungeStartVelocity = FVector::ZeroVector;

	/** How close counts as arrived, in cm. This is a NETWORK tolerance, not a design knob.
	 *
	 *  The flight closes the remaining distance in one frame, so its speed is distance divided by
	 *  delta time — which multiplies any disagreement between two machines by about sixty. Measured
	 *  on the bench: the server sat 7 cm from the stop point and the client under 1, an ordinary gap
	 *  for a body moving at 1100 u/s, and that produced 440 u/s against 0. The client braked, the
	 *  server pulled, and the pull felt broken.
	 *
	 *  A radius rather than a point fixes both halves: the speed ramps to zero as the flight reaches
	 *  the edge of it, so there is no cliff to land on different sides of, and the two ends only have
	 *  to agree to within this many cm instead of within one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apex|Melee Lunge", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float MeleeLungeArrivalRadius = 25.0f;

	/** Mirrored from FMeleeAttackSettings. @see SetMeleeLungeTuning */
	float MeleeLungeMaxSpeed = 3000.0f;
	float MeleeDropKickSpeed = 2500.0f;
	float MeleeLungeMomentumRatio = 1.0f;
	bool bMeleeLungeDisablesGravity = true;

	/** Entry: capture the momentum to hold and kill gravity if the swing found somebody. */
	void StartMeleeLunge();

	/** Exit: gravity back, and the entry momentum back too if the swing missed. */
	void EndMeleeLunge();

	/** The flight itself, run from UpdateCharacterStateBeforeMovement. */
	void UpdateMeleeLunge(float DeltaSeconds);

	/** Drop or restore mutual move-collision with MeleeLungeTargetActor. Idempotent. */
	void SetMeleeLungeTargetIgnored(bool bIgnore);

	/** Turns gravity off for a flight that wants it off, and nothing else — the way back is
	 *  EndMeleeLunge. GravityScale is plain component state and no part of the saved move, so a
	 *  replay would otherwise keep whatever the last real move left there. */
	void SyncMeleeLungeGravity();

	// Slide state
	float SlideCooldownRemaining = 0.0f;
	float SlideBoostCooldownRemaining = 0.0f;
	float SlideDuration = 0.0f;
	int32 SlideFatigueCounter = 0;
	float SlideFatigueDecayTimer = 0.0f;
	FVector SlideDirection;

	// Saved default values (restored after slide)
	float DefaultGroundFriction = 8.0f;
	float DefaultBrakingDeceleration = 2048.0f;

	// Smooth Crouch state
	float StandingCapsuleHalfHeight = 0.0f;  // Cached from capsule on init
	float TargetCapsuleHalfHeight = 0.0f;
	bool bWantsToCrouchSmooth = false;  // Our internal crouch flag

	// WallRun state (Titanfall 2 style - acceleration -> peak -> deceleration)
	float WallRunElapsedTime = 0.0f;       // Time since wallrun started
	float WallRunSameWallCooldown = 0.0f;
	FVector WallRunNormal;
	FVector WallRunDirection;
	float WallRunEntrySpeed = 0.0f;        // Speed when entering wallrun (for peak calculation)
	float WallRunPeakSpeed = 0.0f;         // Calculated peak speed
	float WallRunCurrentSpeed = 0.0f;      // Current speed along wall
	float WallRunDistanceTraveled = 0.0f;  // Accumulated distance for headbob
	float WallRunHeadbobRoll = 0.0f;       // Current headbob roll offset
	float WallRunBaseCameraRoll = 0.0f;    // Base camera roll (without headbob, for interpolation)
	FRotator CurrentWallRunCameraTilt = FRotator::ZeroRotator;  // Internal, use GetWallRunCameraTilt()
	TWeakObjectPtr<AActor> LastWallRunActor;

	// WallRun capsule state (Titanfall 2 style - smaller capsule, NO TILT to avoid mesh rotation)
	float WallRunOriginalCapsuleHalfHeight = 0.0f;
	float WallRunOriginalCapsuleRadius = 0.0f;
	bool bWallRunCapsuleModified = false;

	// Wall Bounce state
	float WallBounceCooldownRemaining = 0.0f;

	// Jump state
	float JumpHoldTimeRemaining = 0.0f;
	bool bJumpHeld = false;

#if ENABLE_DRAW_DEBUG
	// Jump metrics debug
	float JumpStartZ = 0.0f;
	float JumpMaxZ = 0.0f;
	bool bTrackingJump = false;
	FString LastJumpType;
#endif

	// Air crouch state
	float AirCrouchHoldTime = 0.0f;

	// Air Dash state
	float AirDashCooldownRemaining = 0.0f;
	float AirDashDecayTimeRemaining = 0.0f;

	// Ground Dash state
	float GroundDashCooldownRemaining = 0.0f;
	float GroundDashTimeRemaining = 0.0f;
	float GroundDashSpeed = 0.0f;
	FVector GroundDashDirection = FVector::ZeroVector;

	// Air Dash Redirect state
	bool bIsRedirecting = false;
	float AirDashRedirectTimeRemaining = 0.0f;
	FVector AirDashRedirectStartDirection = FVector::ZeroVector;
	FVector AirDashRedirectTargetDirection = FVector::ZeroVector;
	float AirDashRedirectSpeed = 0.0f;

	// Mantle state
	FVector MantleStartLocation;
	FVector MantleTargetLocation;
	float MantleAlpha = 0.0f;

	// EMF
	FVector CurrentEMFForce = FVector::ZeroVector;

	// ==================== Internal Methods ====================

	void UpdateMovementState();
	void SetMovementState(EPolarityMovementState NewState);

	// Slide
	void UpdateSlide(float DeltaTime);
	float GetSlopeAngle() const;

	// WallRun (slide-style)
	void CheckForWallRun();
	void UpdateWallRun(float DeltaTime);
	bool TraceForWall(EWallSide Side, FHitResult& OutHit) const;
	bool IsValidWallRunSurface(const FHitResult& Hit) const;
	bool IsAboveGround() const;
	void UpdateWallRunCameraTilt(float DeltaTime);

	// WallRun capsule (Titanfall 2 style - size only, no tilt)
	void ApplyWallRunCapsule();
	void RestoreWallRunCapsule();

	// WallRun speed boost calculation
	float CalculateWallRunBoost(float ParallelSpeed) const;

	// Wall Bounce
	void CheckForWallBounce();
	void PerformWallBounce(const FHitResult& WallHit);

	// Mantle
	void UpdateMantle(float DeltaTime);
	bool TraceMantleSurface(FHitResult& OutHit) const;

	// Air
	void ApplyAirStrafe(float DeltaTime);
	void UpdateJumpHold(float DeltaTime);
	void UpdateGroundDash(float DeltaTime);
	void EndGroundDash();
	void UpdateAirDash(float DeltaTime);
	void UpdateAirDashDecay(float DeltaTime);
	void UpdateAirDashRedirect(float DeltaTime);

	// EMF & Modifiers
	void ApplyEMFForces(float DeltaTime);
	void ApplyVelocityModifiers(float DeltaTime);

	// Utility
	void ResetAirAbilities();

	// Smooth Crouch
	void UpdateCapsuleHeight(float DeltaTime);

	/** Play camera shake */
	void PlayCameraShake(TSubclassOf<UCameraShakeBase>);
};

/**
 * One recorded move of this project's movement, plus the state the engine's own move does not know
 * about. Right now that is sprint alone; wallrun, slide and the jump counter belong here too and
 * are the reason this class exists rather than a one-off hack.
 *
 * The client records what it did, the server replays it from the same data. Anything that changes
 * where the character ends up and lives outside the engine's move will otherwise be invisible to
 * the server, which then disagrees about the destination and corrects the client.
 */
class POLARITY_API FSavedMove_Polarity : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	/** Everything the character decided to do during this move, as the flags that go on the wire.
	 *  @see EPolarityMoveFlag */
	uint16 SavedPolarityFlags;

	/** The melee lunge's destination for this move. Unlike everything below this line it is not just
	 *  replay state: it travels to the server too, because a lunge target is the one piece of geometry
	 *  the server is not allowed to pick for itself. @see EPolarityMoveFlag */
	FVector SavedMeleeLungeTarget;
	TWeakObjectPtr<AActor> SavedMeleeLungeTargetActor;

	/** The rest of the lunge, replay state only — the server derives these for itself, one from the
	 *  flags and one by capturing its own velocity when the lunge starts. */
	FVector SavedMeleeLungeStartVelocity;
	uint8 bSavedMeleeLunging : 1;
	uint8 bSavedMeleeLungeGravityOff : 1;

	/** Predicted state below this line. It is NOT sent to the server: the server derives its own
	 *  copy by running the same slides and jumps. These exist so that a *replay* on the client starts
	 *  from the same numbers the move was first simulated with. Without them every correction re-ran
	 *  DoJump with a fatigue counter that had already been incremented and a cooldown that had kept
	 *  ticking, so the slide jump came out at a different strength each time and the client got
	 *  yanked back. */
	uint8 bSavedJumpHeld : 1;
	int32 SavedSlideFatigueCounter;

	/** The wall a run was happening on, and how far into it the character was.
	 *
	 *  None of this is derivable from the flags: the flag says "wall running", and the geometry says
	 *  which wall, which way along it and at what speed. A replay that restored only the flag re-ran
	 *  UpdateWallRun against a stale normal and a clock that had already run out, so it ended the run
	 *  it was supposed to be reproducing. Both ends showed it plainly: on a bench session the client
	 *  logged 13 wallrun starts against 32 ends, the extras all coming out of replays. */
	float SavedWallRunElapsedTime;
	FVector SavedWallRunNormal;
	FVector SavedWallRunDirection;
	float SavedWallRunEntrySpeed;
	float SavedWallRunPeakSpeed;
	float SavedWallRunCurrentSpeed;
	EWallSide SavedWallRunSide;
	float SavedSlideFatigueDecayTimer;
	float SavedSlideBoostCooldown;
	float SavedSlideCooldown;
	float SavedJumpHoldTimeRemaining;
	int32 SavedCurrentJumpCount;

	FSavedMove_Polarity();

	virtual void Clear() override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
		class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};

/** Hands the engine our move type instead of the stock one. */
class POLARITY_API FNetworkPredictionData_Client_Polarity : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	FNetworkPredictionData_Client_Polarity(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};
