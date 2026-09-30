// TurretBuildable.h
// The engineer's sentry, with a twist: it has no gun of its own.
//
// A turret is a set of vices. A player walks up with a gun in hand, presses the feed key, and the
// gun goes into a vice; from then on the turret fires THAT gun, with its damage, its magazine, its
// rate of fire and its projectile. Level 1 is one vice, level 2 a second, level 3 a third that only
// takes the heavy, rocket-class weapons on its list, which is TF2's own progression (more fire,
// then splash) without a single number of the turret's own.
//
// Aiming is TF2's: the nearest enemy in sight, the barrel turning at a limited rate, no shot until
// the barrel is actually on the body. On top of that the mount refuses targets it cannot track
// (angular speed above its turn rate) and, for a projectile gun, leads the target and only fires
// when the lead has a fair chance of landing. A vice does not shake, so the gun has no spread here;
// instead each vice has a RANGE read off the spread the gun would have in a player's hands, so a
// shotgun keeps a short leash and a rifle a long one.
//
// Everything a hit means keeps meaning it, because the gun fires through its own hit path
// (AShooterWeapon::FireMounted): shield gate, headshots, ionization, the kill tally of the owner
// (the gun's instigator is the owner's pawn), and the owner's hit marker, which arrives flagged as
// remote so it draws small and quiet.
//
// Server-authoritative like every building: the server picks targets, turns the vices and fires;
// clients get the guns (replicated actors that attach themselves), the ammunition counts for the
// HUD and the current target, and turn their own copies of the vices toward it for the look of it.
//
// The articulated head. With a skeletal mesh on TurretMesh (root > turret_yaw > turret_pitch >
// jaw_slide, weapon_mount), vice 0 stops being a free-floating mount: it rides the WeaponMount
// socket, and aiming it means turning the head. The turret keeps two joint angles (yaw about the
// mesh's +Z, pitch about the head's +Y, 0/0 = the reference pose looking down +X) and the moving
// jaw's offset along the head's +Y; UTurretAnimInstance turns those into bones. Everything is done
// in the mesh's component space against the reference pose, so the bones' own axes (whatever the
// FBX export made of them) never enter the math. The other vices stay free mounts until their
// level has a mesh of its own.

#pragma once

#include "CoreMinimal.h"
#include "BuildableActor.h"
#include "Variant_Shooter/Weapons/ShooterWeaponHolder.h"
#include "TurretBuildable.generated.h"

class ADroppedRangedWeapon;
class AShooterCharacter;
class AShooterWeapon;
class UHitFeedbackSet;
class USceneComponent;
class USkeletalMeshComponent;

/** Server-side state of one vice. The parts a client needs (the gun, the counts) live in the
 *  replicated arrays on the turret, indexed the same way. */
USTRUCT()
struct FTurretVice
{
	GENERATED_BODY()

	/** How far this vice trusts its gun, cm. From the gun's spread; see ComputeRangeFor. */
	float Range = 0.0f;

	/** Rounds the gun fires at once before the next one is allowed, seconds. */
	float NextShotTime = 0.0f;

	/** World time the current reload finishes, or below zero when not reloading. */
	float ReloadEndTime = -1.0f;

	/** The drop's EMF charge, carried through so the pickup comes back the way it went in. */
	float DropCharge = 0.0f;

	/** Where the barrel points right now, world space. Simulated on every machine. */
	FRotator BarrelRotation = FRotator::ZeroRotator;
};

UCLASS(Blueprintable)
class POLARITY_API ATurretBuildable : public ABuildableActor, public IShooterWeaponHolder
{
	GENERATED_BODY()

public:

	ATurretBuildable();

	/** Vices a turret can ever have: one per level. */
	static constexpr int32 MaxVices = 3;

	// ==================== Components ====================

	/** Where each gun sits: the mount is a virtual hand. A gun attaches by its grip socket exactly
	 *  as it does in a player's hand, so every gun of the pack lies the same way relative to the
	 *  mount and one BarrelLocalDirection serves them all. Place them on the mesh in the Blueprint;
	 *  their default rotation is the rest pose the vice returns to with nothing to shoot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<USceneComponent>> ViceMounts;

	/** The articulated turret (SK_T01 and later levels). Rides the building mesh, so it rises with
	 *  the construction. Leave it empty and the turret is the old static one with free mounts. When
	 *  it has a mesh with the head bones and the WeaponMount socket, the static Mesh is hidden in
	 *  game (it stays as the placement ghost, the hitbox size and the collision) and vice 0 is
	 *  seated on the socket. Keep its scale at 1: the asset is authored in real centimetres. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> TurretMesh;

	// ==================== Settings: articulated head ====================

	/** Socket on TurretMesh the head's vice sits on. Put it on the weapon_mount bone in the skeletal
	 *  mesh asset; its offset and rotation are where a gripped gun lies. The gun's barrel should come
	 *  out along the head's +X at rest, and a small error is compensated (the log says how much). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head")
	FName WeaponMountSocket = TEXT("WeaponMount");

	/** Turns about the mesh's vertical axis, carrying everything above the base. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head")
	FName YawBoneName = TEXT("turret_yaw");

	/** Tilts about the head's crosswise axis, carrying the vice and the weapon mount. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head")
	FName PitchBoneName = TEXT("turret_pitch");

	/** Slides along the head's +Y to close on the gun. None = the jaw does not move. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head")
	FName JawBoneName = TEXT("jaw_slide");

	/** Fallback clamp: where the moving jaw goes from its rest (open) position when a gun is in the
	 *  vice and its width could not be measured, cm along the head's +Y. Negative closes. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (Units = "cm"))
	float JawClampOffsetCm = -3.0f;

	/** The moving jaw's pad face at rest, along the mesh's +Y, cm. The jaw closes by bringing this
	 *  face down onto the gun's side. SK_Polarity_T01: 4.5 (jaw pivot 7.5, pad 3.0 in front of it). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (Units = "cm"))
	float JawFaceRestYCm = 4.5f;

	/** The jaw's travel from rest: fully closed (pads touching) and fully open. SK_Polarity_T01:
	 *  -9.9 and +4.1 (9.9 cm open at rest, 0 to 14 cm in all). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (Units = "cm"))
	float JawClosedLimitCm = -9.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (Units = "cm"))
	float JawOpenLimitCm = 4.1f;

	/** How far the pad goes past the gun's surface, so the grip reads as a squeeze, not a touch. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (ClampMin = "0.0", Units = "cm"))
	float JawSqueezeCm = 0.15f;

	/** The part of the gun the pads actually bear on: this far along the barrel either side of the
	 *  mount, and this far up and down. Only the gun's surface inside it decides where the jaw stops,
	 *  so a scope or a side lever elsewhere does not hold the jaw open. SK_Polarity_T01's pads are
	 *  5.2 cm long. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (ClampMin = "0.1", Units = "cm"))
	float JawContactHalfLengthCm = 2.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (ClampMin = "0.1", Units = "cm"))
	float JawContactHalfHeightCm = 4.0f;

	/** How fast the jaw travels, cm per second. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Head", meta = (ClampMin = "0.1", Units = "cm/s"))
	float JawSpeedCmPerSec = 20.0f;

	// ==================== Settings: vices ====================

	/** Direction the barrel points in a mount's space once a gun is gripped by it. Set once, with
	 *  any gun in the vice and polarity.turret.debug 1 drawing the ray. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Vices")
	FVector BarrelLocalDirection = FVector(1.0f, 0.0f, 0.0f);

	/** The gun's up in the mount's space, so the vice keeps it upright while it turns. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Vices")
	FVector BarrelLocalUp = FVector(0.0f, 0.0f, 1.0f);

	/** Which vice is the heavy one. Only guns on RocketViceWeaponClasses go there, and they go
	 *  nowhere else. Unlocked by level like the others (index 2 = level 3). */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Vices", meta = (ClampMin = "0", ClampMax = "2"))
	int32 RocketViceIndex = 2;

	/** The heavy guns. A parent class covers its children. Empty = the heavy vice takes nothing. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Vices")
	TArray<TSubclassOf<AShooterWeapon>> RocketViceWeaponClasses;

	/** How far a player may stand from the turret to feed it a gun, cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Vices", meta = (ClampMin = "50.0", Units = "cm"))
	float FeedReachCm = 300.0f;

	// ==================== Settings: aiming ====================

	/** Degrees per second a vice turns, per level (index 0 = level 1; the last entry serves every
	 *  level past it). A target crossing faster than this, in angle, cannot be tracked and is not
	 *  chosen. Defaults 150, 200, 260 from the constructor. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim")
	TArray<float> TurnRateDegPerSecByLevel;

	/** How far a vice may pitch up or down. For the articulated head it is the joint's own limit
	 *  (relative to the base, not the horizon): set it to the safe range the asset report gives. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float MaxPitchDegrees = 60.0f;

	/** Half the width of the body the turret counts on hitting, cm. Sets both the range (how far a
	 *  gun's spread still fits inside a body) and the angular size a barrel has to be within. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "5.0", Units = "cm"))
	float TargetHalfWidthCm = 40.0f;

	/** Floor and ceiling on a vice's range, cm, whatever the gun says. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "100.0", Units = "cm"))
	float MinRangeCm = 600.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxRangeCm = 3000.0f;

	/** Multiplier on the computed range, for tuning without touching the formula. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "0.1"))
	float RangeScale = 1.0f;

	/** For a projectile gun: the least estimated chance of landing the vice fires at. The estimate
	 *  is how far the target's angular speed can carry it during the flight, against its angular
	 *  size. 0 fires at anything in range, 1 only at what is standing still. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinProjectileHitChance = 0.5f;

	/** A new target has to be this fraction of the current one's distance before the turret
	 *  switches. TF2's 0.75: nearer by a quarter. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float RetargetCloserFraction = 0.75f;

	/** Seconds between two looks for a target. Tracking the chosen one is every frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Aim", meta = (ClampMin = "0.05", Units = "s"))
	float TargetScanInterval = 0.2f;

	// ==================== Settings: ammunition ====================

	/** Most metal one wrench hit may invest. Only the upgrade spends metal now. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Ammo", meta = (ClampMin = "0"))
	int32 WrenchMetalBudget = 28;

	/** What a gun with an endless reserve in a player's hands (the class weapon) brings with it, in
	 *  magazines. It had no reserve to hand over, and the turret will not pretend it did. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Ammo", meta = (ClampMin = "0"))
	int32 InfiniteReserveMagazinesGranted = 1;

	/** Reload pause for a gun that has no reload time of its own. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Ammo", meta = (ClampMin = "0.1", Units = "s"))
	float ReloadFallbackSeconds = 2.0f;

	// ==================== Settings: feedback and returns ====================

	/** What the owner hears when the turret lands a hit: quieter, different, all seven cues
	 *  authored. A cue this set leaves empty falls back on the owner's ordinary sound. */
	UPROPERTY(EditDefaultsOnly, Category = "Turret|Feedback")
	TObjectPtr<UHitFeedbackSet> OwnerFeedbackSet;

	// ==================== Queries ====================

	/** Vices open at the current level. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	int32 GetUnlockedViceCount() const;

	UFUNCTION(BlueprintPure, Category = "Turret")
	AShooterWeapon* GetViceWeapon(int32 ViceIndex) const;

	UFUNCTION(BlueprintPure, Category = "Turret")
	int32 GetViceRounds(int32 ViceIndex) const { return ViceRounds.IsValidIndex(ViceIndex) ? ViceRounds[ViceIndex] : 0; }

	UFUNCTION(BlueprintPure, Category = "Turret")
	int32 GetViceReserve(int32 ViceIndex) const { return ViceReserve.IsValidIndex(ViceIndex) ? ViceReserve[ViceIndex] : 0; }

	/** Range of a vice, cm; 0 for an empty one. Server value, the clients see 0. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	float GetViceRange(int32 ViceIndex) const;

	UFUNCTION(BlueprintPure, Category = "Turret")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	/** Degrees per second the vices turn at the current level. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	float TurnRateDegPerSec() const;

	/** Whether a gun of this class has a vice to go into right now, and which. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	bool FindViceFor(TSubclassOf<AShooterWeapon> WeaponClass, int32& OutViceIndex) const;

	/** A vice already holding a gun of exactly this one's class, which Weapon can top up with its
	 *  rounds while the player keeps it; INDEX_NONE when there is none. A self-refilling gun (the
	 *  class weapon) never tops up: it would feed the turret forever. Checked before FindViceFor. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	int32 FindTopUpViceFor(const AShooterWeapon* Weapon) const;

	/** A taken vice Weapon may replace, when there is no free one: open, of the right kind (heavy or
	 *  ordinary) and holding a gun of another class. The old gun drops to the floor with its rounds,
	 *  or goes back to the donor's hands when it has no floor version. INDEX_NONE when none. */
	UFUNCTION(BlueprintPure, Category = "Turret")
	int32 FindSwapViceFor(const AShooterWeapon* Weapon) const;

	UFUNCTION(BlueprintPure, Category = "Turret")
	bool IsRocketClass(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** True when TurretMesh carries a usable skeleton and vice 0 is aimed by turning the head. */
	UFUNCTION(BlueprintPure, Category = "Turret|Head")
	bool HasArticulatedHead() const { return bArticulatedHead; }

	/** Head joint angles, degrees: yaw about the mesh's +Z, pitch about the head's +Y (positive is
	 *  up). 0/0 is the reference pose. What UTurretAnimInstance puts on the bones. */
	UFUNCTION(BlueprintPure, Category = "Turret|Head")
	float GetHeadYaw() const { return HeadRotation.Yaw; }

	UFUNCTION(BlueprintPure, Category = "Turret|Head")
	float GetHeadPitch() const { return HeadRotation.Pitch; }

	/** The moving jaw's offset from its rest (open) position along the head's +Y, cm. */
	UFUNCTION(BlueprintPure, Category = "Turret|Head")
	float GetJawOffsetCm() const { return JawOffsetCm; }

	// ==================== Actions (server) ====================

	/** Take a gun from Donor into a vice: Weapon when given (one of the donor's own), else the gun
	 *  in their hands. ReportedLoadedRounds is the donor's own count of the magazine (a client's is
	 *  more current than the server's copy); below zero uses the server's. False, with a reason in
	 *  the log, when there is no gun, no room, or the turret is not standing. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Turret")
	bool AcceptWeaponFrom(AShooterCharacter* Donor, int32 ReportedLoadedRounds = -1, AShooterWeapon* Weapon = nullptr);

	// ==================== IShooterWeaponHolder ====================

	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;
	virtual void PlayFiringMontage(UAnimMontage* Montage) override {}
	virtual void AddWeaponRecoil(float Recoil) override {}
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override {}
	virtual FVector GetWeaponTargetLocation() override;
	virtual AActor* GetWeaponAimActor() const override { return CurrentTarget; }
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override {}
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override {}
	virtual void OnSemiWeaponRefire() override {}
	virtual void OnWeaponHitFeedback(const FHitFeedbackContext& Context) override;

	// ==================== AActor ====================

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void PostInitializeComponents() override;

protected:

	virtual void BeginPlay() override;

	// ==================== ABuildableActor hooks ====================

	virtual void TickActive(float DeltaSeconds) override;
	virtual int32 GetWrenchMetalBudget() const override { return WrenchMetalBudget; }
	virtual void OnLevelChanged(int32 NewLevel) override;
	virtual void OnDestroyed_Native() override;

	// ==================== Replicated state ====================

	/** The gun in each vice, or null. The actors replicate on their own and attach themselves to
	 *  the turret in their BeginPlay; this array tells a client which vice, which can arrive after
	 *  the gun did, so OnRep re-seats them. */
	UPROPERTY(ReplicatedUsing = OnRep_ViceWeapons)
	TArray<TObjectPtr<AShooterWeapon>> ViceWeapons;

	/** Loaded rounds per vice. */
	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	TArray<int32> ViceRounds;

	/** Rounds in reserve per vice, waiting for a reload. */
	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	TArray<int32> ViceReserve;

	/** What the turret is tracking, for the clients' copies of the vices to turn toward. */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> CurrentTarget;

	UFUNCTION()
	void OnRep_ViceWeapons();

	UFUNCTION()
	void OnRep_Ammo();

	/** The owner's hit marker for a hit the turret landed, on the owner's own machine. The turret
	 *  is net-owned by the owner's controller so this reaches them. */
	UFUNCTION(Client, Unreliable)
	void Client_OwnerHitFeedback(const FHitFeedbackContext& Context);

private:

	// ==================== Server: targets ====================

	void ScanForTarget();
	bool IsEligibleTarget(const APawn* Pawn, float MaxRange, float& OutDistance) const;
	bool HasLineOfSightTo(const FVector& From, const AActor* Target, const FVector& Point) const;

	// ==================== Server: firing ====================

	void TickVice(int32 ViceIndex, float Now);
	bool IsBarrelOnHostile(int32 ViceIndex, AActor*& OutHitActor) const;
	void FireVice(int32 ViceIndex, const FVector& AimPoint, float Now);
	void StartReload(int32 ViceIndex, float Now);
	void FinishReload(int32 ViceIndex);
	void DropViceWeapon(int32 ViceIndex);
	void ClearVice(int32 ViceIndex);

	/** Empty ViceIndex for a swap: its gun to the floor, or to Donor's hands without a floor version. */
	void EjectViceWeapon(int32 ViceIndex, AShooterCharacter* Donor);

	/** Pour Weapon's rounds into ViceIndex's reserve; the donor keeps the emptied gun. */
	bool TopUpVice(AShooterCharacter* Donor, AShooterWeapon* Weapon, int32 ViceIndex, int32 ReportedLoadedRounds);

	// ==================== Every machine: aiming ====================

	void UpdateAim(float DeltaSeconds);
	void UpdateHeadAim(float DeltaSeconds, const AActor* Target);
	void UpdateJaw(float DeltaSeconds);
	FVector ComputeAimPointFor(int32 ViceIndex, const AActor* Target, float& OutFlightTime) const;
	FRotator BarrelRotationFor(const FVector& WorldDirection) const;
	FVector BarrelDirectionOf(int32 ViceIndex) const;
	FVector MuzzleLocationOf(int32 ViceIndex) const;

	// ==================== Every machine: the articulated head ====================

	/** Check TurretMesh, seat vice 0 on the socket, read the barrel's rest direction. */
	void SetupArticulatedHead();
	bool IsHeadVice(int32 ViceIndex) const { return bArticulatedHead && ViceIndex == HeadViceIndex; }

	/** The joint angles that point the barrel down Direction, given in the mesh's space. Exact for a
	 *  barrel that is not quite along +X at rest. */
	FRotator SolveHeadAngles(const FVector& Direction) const;

	/** Measure this gun's width where the pads bear, lay it against the fixed jaw and set where the
	 *  moving jaw stops to squeeze it. Off the gun's mesh as it sits in the vice. */
	void MeasureJawClamp(const AShooterWeapon* Weapon);

	// ==================== Helpers ====================

	float ComputeRangeFor(const AShooterWeapon* Weapon) const;
	static float ProjectileSpeedOf(const AShooterWeapon* Weapon);
	static float ExplosionRadiusOf(const AShooterWeapon* Weapon);
	APawn* GetOwnerPawn() const;
	AShooterCharacter* GetOwnerCharacter() const;
	void SyncNetOwner();
	int32 FindViceOf(const AShooterWeapon* Weapon) const;
	FVector EyeLocation() const;
	void DrawDebug(float Now) const;

	/** Server-only per-vice state, parallel to the replicated arrays. */
	UPROPERTY()
	TArray<FTurretVice> Vices;

	/** Rest pose of each mount, relative to the root, taken at BeginPlay. */
	TArray<FRotator> RestRelativeRotations;

	/** The vice the head carries. */
	static constexpr int32 HeadViceIndex = 0;

	bool bArticulatedHead = false;

	/** Head joint angles (Pitch, Yaw; Roll always 0). Simulated on every machine like the mounts. */
	FRotator HeadRotation = FRotator::ZeroRotator;

	/** The jaw's offset along the head's +Y, cm. */
	float JawOffsetCm = 0.0f;

	/** Where a gripped gun's barrel points with the head at rest, in the mesh's space. */
	FVector HeadBarrelDirection = FVector::ForwardVector;

	/** The head's vice (the socket) at rest, in the mesh's space. */
	FTransform MountAtRest = FTransform::Identity;

	/** The jaw offset that squeezes the gun now in the head's vice; JawClampOffsetCm until measured. */
	float JawClampTargetCm = 0.0f;
	bool bJawClampMeasured = false;

	float NextScanTime = 0.0f;
};
