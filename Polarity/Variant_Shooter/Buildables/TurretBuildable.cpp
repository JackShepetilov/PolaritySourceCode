// TurretBuildable.cpp

#include "TurretBuildable.h"

#include "AI/AimPoints.h"
#include "AI/PolarityTeams.h"
#include "AnimationRuntime.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TurretAnimInstance.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Feedback/HitFeedbackSet.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

static TAutoConsoleVariable<int32> CVarTurretDebug(
	TEXT("polarity.turret.debug"),
	0,
	TEXT("1: draw every turret's barrel rays (green = on a target), ranges and the line to its target, and log its decisions with [TURRET_DEBUG]: target changes, every shot, and why a vice is holding fire."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTurretHeadTest(
	TEXT("polarity.turret.headtest"),
	0,
	TEXT("1: every articulated turret ignores its target and runs the skeleton check in place: the head turns round at its turn rate, nods through the full pitch range and the jaw opens and closes. Visual only; the guns still fire on their own checks, so test without enemies."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTurretInfiniteAmmo(
	TEXT("polarity.turret.infiniteammo"),
	0,
	TEXT("1: turrets never spend a round, so a test fight is not decided by the magazine. The turret bench turns it on for its run and off again after."),
	ECVF_Cheat);

namespace TurretGate
{
	/** Why a vice did not fire this frame. Logged under polarity.turret.debug when it changes, so
	 *  "the turret is not shooting" has a one-line answer instead of a guess. Debug bookkeeping
	 *  only, kept out of the class so it costs no header. */
	enum EGate : uint8 { None, NoGun, Reloading, Empty, NoTarget, Refire, OutOfRange, BarrelOff, NotAligned, LowChance, BlastTooClose, NoSight, Fired };
	static const TCHAR* Names[] = { TEXT("-"), TEXT("no gun"), TEXT("reloading"), TEXT("empty"), TEXT("no target"), TEXT("refire"),
		TEXT("out of range"), TEXT("barrel not on a body"), TEXT("barrel not aligned with the led point"), TEXT("hit chance too low"),
		TEXT("target inside own blast radius"), TEXT("no line of sight"), TEXT("FIRED") };
	static TMap<TWeakObjectPtr<const ATurretBuildable>, TArray<uint8>> LastGates;

	static void Report(const ATurretBuildable* Turret, int32 ViceIndex, EGate Gate, const TCHAR* Extra = TEXT(""))
	{
		if (CVarTurretDebug.GetValueOnGameThread() == 0)
		{
			return;
		}
		TArray<uint8>& Gates = LastGates.FindOrAdd(Turret);
		if (Gates.Num() <= ViceIndex)
		{
			Gates.SetNumZeroed(ViceIndex + 1);
		}
		if (Gates[ViceIndex] == Gate && Gate != Fired)
		{
			return;
		}
		Gates[ViceIndex] = Gate;
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s vice %d: %s%s"), *GetNameSafe(Turret), ViceIndex, Names[Gate], Extra);
	}
}

ATurretBuildable::ATurretBuildable()
{
	// The turret has something to do every frame once built: look, turn, shoot.
	bTickWhileActive = true;

	TurnRateDegPerSecByLevel = { 150.0f, 200.0f, 260.0f };

	// The articulated body. Under the building mesh so it rises with the construction and hides with
	// the death; purely visual (the static mesh and the hitbox keep the collision). The pose is
	// refreshed even off screen: the server reads the muzzle off the gun seated on it.
	TurretMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(Mesh);
	TurretMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretMesh->SetGenerateOverlapEvents(false);
	TurretMesh->SetCanEverAffectNavigation(false);
	TurretMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TurretMesh->AnimClass = UTurretAnimInstance::StaticClass();

	// One mount per possible vice, all created up front: a component cannot be added to a class
	// later, and which ones are in use is a matter of level, not of existence. Their transforms
	// are the Blueprint's to set on the real mesh. Under the root, not the mesh: the mesh carries
	// the Blueprint's scale (the placeholder is 0.8 wide) and a gun hung under it would inherit
	// that and come out squashed, since the attach keeps the gun's own relative scale.
	static const TCHAR* MountNames[MaxVices] = { TEXT("Vice0"), TEXT("Vice1"), TEXT("Vice2") };
	for (int32 Index = 0; Index < MaxVices; ++Index)
	{
		USceneComponent* const Mount = CreateDefaultSubobject<USceneComponent>(MountNames[Index]);
		Mount->SetupAttachment(GetRootComponent());
		Mount->SetRelativeLocation(FVector(0.0f, (Index - 1) * 30.0f, 60.0f + Index * 10.0f));
		ViceMounts.Add(Mount);
	}

	ViceWeapons.SetNum(MaxVices);
	ViceRounds.SetNum(MaxVices);
	ViceReserve.SetNum(MaxVices);
	Vices.SetNum(MaxVices);
}

void ATurretBuildable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATurretBuildable, ViceWeapons);
	DOREPLIFETIME(ATurretBuildable, ViceRounds);
	DOREPLIFETIME(ATurretBuildable, ViceReserve);
	DOREPLIFETIME(ATurretBuildable, CurrentTarget);
}

void ATurretBuildable::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Before BeginPlay and before any gun: a gun's BeginPlay seats it on vice 0, and vice 0 has to
	// be on the socket by then.
	SetupArticulatedHead();
}

void ATurretBuildable::BeginPlay()
{
	Super::BeginPlay();

	RestRelativeRotations.SetNum(MaxVices);
	for (int32 Index = 0; Index < MaxVices; ++Index)
	{
		USceneComponent* const Mount = ViceMounts.IsValidIndex(Index) ? ViceMounts[Index] : nullptr;
		RestRelativeRotations[Index] = Mount ? Mount->GetRelativeRotation() : FRotator::ZeroRotator;
		Vices[Index].BarrelRotation = Mount ? BarrelDirectionOf(Index).Rotation() : GetActorRotation();
	}

	if (bArticulatedHead)
	{
		// The static body is the placement ghost, the hitbox size and the collision; the skeleton is
		// what is seen. Hidden rather than cleared, so those three keep working.
		Mesh->SetHiddenInGame(true, false);
		// The pose reads this frame's angles, not last frame's.
		TurretMesh->AddTickPrerequisiteActor(this);
	}

	SyncNetOwner();
}

// ==================== The articulated head ====================

void ATurretBuildable::SetupArticulatedHead()
{
	bArticulatedHead = false;
	const USkeletalMesh* const Asset = TurretMesh ? TurretMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
	{
		// The placeholder turret: free mounts, as before.
		return;
	}

	const FReferenceSkeleton& RefSkeleton = Asset->GetRefSkeleton();
	const USkeletalMeshSocket* const Socket = Asset->FindSocket(WeaponMountSocket);
	const int32 YawIndex = RefSkeleton.FindBoneIndex(YawBoneName);
	const int32 PitchIndex = RefSkeleton.FindBoneIndex(PitchBoneName);
	const int32 SocketBone = Socket ? RefSkeleton.FindBoneIndex(Socket->BoneName) : INDEX_NONE;
	if (YawIndex == INDEX_NONE || PitchIndex == INDEX_NONE || SocketBone == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: %s is not an articulated turret (yaw bone %s %s, pitch bone %s %s, socket %s %s); vices stay free mounts"),
			*GetName(), *Asset->GetName(),
			*YawBoneName.ToString(), YawIndex != INDEX_NONE ? TEXT("ok") : TEXT("MISSING"),
			*PitchBoneName.ToString(), PitchIndex != INDEX_NONE ? TEXT("ok") : TEXT("MISSING"),
			*WeaponMountSocket.ToString(), SocketBone != INDEX_NONE ? TEXT("ok") : TEXT("MISSING (add it on weapon_mount in the mesh asset)"));
		return;
	}
	if (!JawBoneName.IsNone() && RefSkeleton.FindBoneIndex(JawBoneName) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: jaw bone %s not in %s, the jaw will not move"),
			*GetName(), *JawBoneName.ToString(), *Asset->GetName());
	}
	if (!RefSkeleton.BoneIsChildOf(SocketBone, PitchIndex) && SocketBone != PitchIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: socket %s hangs off %s, which does not follow the pitch bone %s; the gun will not tilt with the head"),
			*GetName(), *WeaponMountSocket.ToString(), *Socket->BoneName.ToString(), *PitchBoneName.ToString());
	}

	// The barrel's direction with the head at rest, in the mesh's space: the socket in the reference
	// pose, then the gripped gun's barrel inside the mount. Everything the aim needs from the asset.
	MountAtRest = Socket->GetSocketLocalTransform() * FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, SocketBone);
	HeadBarrelDirection = MountAtRest.GetRotation().RotateVector(BarrelLocalDirection.GetSafeNormal()).GetSafeNormal();
	if (HeadBarrelDirection.IsNearlyZero())
	{
		HeadBarrelDirection = FVector::ForwardVector;
	}

	// Vice 0 becomes the socket. Location and rotation from the socket, scale absolute: a gun keeps
	// its authored size whatever scale the meshes above it carry.
	if (USceneComponent* const Mount = ViceMounts.IsValidIndex(HeadViceIndex) ? ViceMounts[HeadViceIndex].Get() : nullptr)
	{
		Mount->AttachToComponent(TurretMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponMountSocket);
		Mount->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
		Mount->SetAbsolute(false, false, true);
		Mount->SetRelativeScale3D(FVector::OneVector);
	}

	HeadRotation = FRotator::ZeroRotator;
	JawOffsetCm = 0.0f;
	bArticulatedHead = true;

	// A barrel along the pitch axis cannot be tilted onto anything: the head turns but never lines
	// up, and the vice never fires. Seen 2026-09-23 with BarrelLocalDirection +X while the pack's
	// guns lie along the mount's +Y. Say it loudly instead of failing quietly.
	if (FMath::Abs(HeadBarrelDirection.Y) > 0.9f)
	{
		UE_LOG(LogTemp, Error, TEXT("[TURRET_DEBUG] %s: the barrel at rest lies along the head's pitch axis (%s): the head cannot aim it and the vice will not fire. Fix BarrelLocalDirection or the %s socket rotation."),
			*GetName(), *HeadBarrelDirection.ToCompactString(), *WeaponMountSocket.ToString());
	}
	else if (HeadBarrelDirection.X < 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: the gun points out of the BACK of the head (%s); it aims, but the head faces away from the target. Turn the %s socket 180 deg in yaw."),
			*GetName(), *HeadBarrelDirection.ToCompactString(), *WeaponMountSocket.ToString());
	}

	const FRotator Bias = HeadBarrelDirection.Rotation();
	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s articulated head: %s, vice %d on socket %s (bone %s), barrel at rest %s = %.1f deg yaw, %.1f deg pitch off the head's +X%s"),
		*GetName(), *Asset->GetName(), HeadViceIndex, *WeaponMountSocket.ToString(), *Socket->BoneName.ToString(),
		*HeadBarrelDirection.ToCompactString(), Bias.Yaw, Bias.Pitch,
		(FMath::Abs(Bias.Yaw) > 5.0f || FMath::Abs(Bias.Pitch) > 5.0f) ? TEXT(" (compensated, but check the socket rotation or BarrelLocalDirection)") : TEXT(""));
}

FRotator ATurretBuildable::SolveHeadAngles(const FVector& Direction) const
{
	// Head frame = Yaw(about Z) * Pitch(about Y), the barrel at rest is B. Pitch first: yaw never
	// changes Z, so pitch alone has to bring B's height to the wanted one. In the XZ plane B has
	// length R and angle Phi, and FRotator's pitch moves that angle directly: z = R sin(pitch + Phi).
	// Then yaw turns what pitch left in the XY plane onto the wanted heading.
	const FVector Wanted = Direction.GetSafeNormal();
	const FVector& B = HeadBarrelDirection;
	const float R = FMath::Sqrt(B.X * B.X + B.Z * B.Z);
	if (R < KINDA_SMALL_NUMBER || Wanted.IsNearlyZero())
	{
		return Wanted.Rotation();
	}
	const float Phi = FMath::Atan2(B.Z, B.X);
	// sin(pitch + Phi) = z has two roots; take the one with the smaller tilt. With the barrel along
	// -X the principal root is a pitch near 180, which the limit then clamps to -60 and the barrel
	// never lines up (2026-09-23: not a single shot). The other root is the same aim, level.
	const float Asin = FMath::Asin(FMath::Clamp(Wanted.Z / R, -1.0f, 1.0f));
	const float PitchA = FMath::UnwindRadians(Asin - Phi);
	const float PitchB = FMath::UnwindRadians(UE_PI - Asin - Phi);
	const float Pitch = FMath::Abs(PitchA) <= FMath::Abs(PitchB) ? PitchA : PitchB;
	const float XAfterPitch = B.X * FMath::Cos(Pitch) - B.Z * FMath::Sin(Pitch);
	const float Yaw = FMath::Atan2(Wanted.Y, Wanted.X) - FMath::Atan2(B.Y, XAfterPitch);
	return FRotator(FMath::RadiansToDegrees(Pitch), FMath::RadiansToDegrees(Yaw), 0.0f).GetNormalized();
}

void ATurretBuildable::UpdateHeadAim(float DeltaSeconds, const AActor* Target)
{
	const float TurnRate = TurnRateDegPerSec();
	FRotator Goal = FRotator::ZeroRotator;

	if (CVarTurretHeadTest.GetValueOnGameThread() != 0)
	{
		// The skeleton check: round and round at the turn rate, nodding end to end every 4 s.
		const float Time = GetWorld()->GetTimeSeconds();
		HeadRotation.Yaw = FRotator::NormalizeAxis(HeadRotation.Yaw + TurnRate * DeltaSeconds);
		HeadRotation.Pitch = MaxPitchDegrees * FMath::Sin(Time * UE_TWO_PI / 4.0f);
		HeadRotation.Roll = 0.0f;
		Vices[HeadViceIndex].BarrelRotation = BarrelDirectionOf(HeadViceIndex).Rotation();
		return;
	}

	// The rest pose is the reference pose: looking down the mesh's +X, level.
	if (Target && GetViceWeapon(HeadViceIndex))
	{
		float FlightTime = 0.0f;
		const FVector WantedDirection = (ComputeAimPointFor(HeadViceIndex, Target, FlightTime) - MuzzleLocationOf(HeadViceIndex)).GetSafeNormal();
		Goal = WantedDirection.IsNearlyZero()
			? HeadRotation
			: SolveHeadAngles(TurretMesh->GetComponentQuat().UnrotateVector(WantedDirection));
	}
	Goal.Pitch = FMath::Clamp(Goal.Pitch, -MaxPitchDegrees, MaxPitchDegrees);
	Goal.Roll = 0.0f;

	// Joint angles at the same constant rate as a free mount. RInterpConstantTo takes the short way
	// round in yaw; the yaw is kept in (-180, 180], which is the same pose either side of the seam.
	HeadRotation = FMath::RInterpConstantTo(HeadRotation, Goal, DeltaSeconds, TurnRate);
	HeadRotation.Yaw = FRotator::NormalizeAxis(HeadRotation.Yaw);
	HeadRotation.Pitch = FMath::Clamp(HeadRotation.Pitch, -MaxPitchDegrees, MaxPitchDegrees);
	HeadRotation.Roll = 0.0f;

	Vices[HeadViceIndex].BarrelRotation = BarrelDirectionOf(HeadViceIndex).Rotation();
}

void ATurretBuildable::UpdateJaw(float DeltaSeconds)
{
	if (!bArticulatedHead)
	{
		return;
	}
	const float Clamp = bJawClampMeasured ? JawClampTargetCm : JawClampOffsetCm;
	float Goal = GetViceWeapon(HeadViceIndex) ? Clamp : 0.0f;
	if (CVarTurretHeadTest.GetValueOnGameThread() != 0)
	{
		// Open, closed, open, every 2 s.
		Goal = FMath::Frac(GetWorld()->GetTimeSeconds() / 2.0f) < 0.5f ? Clamp : 0.0f;
	}
	JawOffsetCm = FMath::FInterpConstantTo(JawOffsetCm, Goal, DeltaSeconds, JawSpeedCmPerSec);
}

void ATurretBuildable::MeasureJawClamp(const AShooterWeapon* Weapon)
{
	bJawClampMeasured = false;
	const USceneComponent* const Mount = ViceMounts.IsValidIndex(HeadViceIndex) ? ViceMounts[HeadViceIndex].Get() : nullptr;
	const USkeletalMeshComponent* const Gun = Weapon ? Weapon->GetThirdPersonMesh() : nullptr;
	const USkeletalMesh* const GunAsset = Gun ? Gun->GetSkeletalMeshAsset() : nullptr;
	if (!bArticulatedHead || !Mount || !GunAsset)
	{
		return;
	}

	// The gun's mesh space -> the vice -> the head at rest. The first step is read off the live
	// transforms (both hang on the same socket, so the head's pose cancels out), the second is the
	// socket in the reference pose. In head space the jaw moves along +Y and the pads bear on the
	// box around the mount given by JawContactHalfLength/Height.
	const FTransform GunToHead = Gun->GetComponentTransform().GetRelativeTransform(Mount->GetComponentTransform()) * MountAtRest;
	const FVector Center = MountAtRest.GetLocation();

	// The gun's side toward the moving jaw (+Y) and away from it, inside the contact box. From the
	// vertices where the CPU still has them (always in the editor), else from the bounds.
	float Near = -UE_BIG_NUMBER;
	float Far = UE_BIG_NUMBER;
	int32 Counted = 0;
	const FSkeletalMeshRenderData* const Render = GunAsset->GetResourceForRendering();
	if (Render && Render->LODRenderData.Num() > 0)
	{
		const FPositionVertexBuffer& Positions = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
		if (Positions.GetVertexData() && Positions.GetNumVertices() > 0)
		{
			for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
			{
				const FVector Point = GunToHead.TransformPosition(FVector(Positions.VertexPosition(Index)));
				if (FMath::Abs(Point.X - Center.X) > JawContactHalfLengthCm || FMath::Abs(Point.Z - Center.Z) > JawContactHalfHeightCm)
				{
					continue;
				}
				Near = FMath::Max(Near, Point.Y);
				Far = FMath::Min(Far, Point.Y);
				++Counted;
			}
		}
	}
	const TCHAR* Source = TEXT("vertices");
	if (Counted == 0)
	{
		// No vertex data, or nothing of the gun where the pads are: the whole gun's box, which is
		// wider than the grip at worst, so the jaw stops early rather than inside the gun.
		const FBox Box = GunAsset->GetBounds().GetBox().TransformBy(GunToHead);
		if (!Box.IsValid)
		{
			return;
		}
		Near = Box.Max.Y;
		Far = Box.Min.Y;
		Source = TEXT("bounds");
	}

	// A vise holds a gun between both jaws: first lay the gun against the fixed jaw (its face is where
	// the moving one stops fully closed), then bring the moving jaw down onto the other side. The shift
	// is across the vice only, so the barrel's direction does not change, only where it starts.
	const float FixedFace = JawFaceRestYCm + JawClosedLimitCm;
	const float Seat = FixedFace - Far;
	const FVector SeatInMount = MountAtRest.InverseTransformVectorNoScale(FVector(0.0f, Seat, 0.0f));
	if (USkeletalMeshComponent* const TPMesh = Weapon->GetThirdPersonMesh())
	{
		TPMesh->AddRelativeLocation(SeatInMount);
	}
	if (USkeletalMeshComponent* const FPMesh = Weapon->GetFirstPersonMesh())
	{
		FPMesh->AddRelativeLocation(SeatInMount);
	}
	const float GunFar = Far + Seat;
	const float GunNear = Near + Seat;

	// Bring the pad face down onto the near side, a hair into it, and never past the travel.
	JawClampTargetCm = FMath::Clamp(GunNear - JawSqueezeCm - JawFaceRestYCm, JawClosedLimitCm, JawOpenLimitCm);
	bJawClampMeasured = true;
	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s jaw on %s: %.2f cm wide at the pads (%s, %d pts), gun seated %.2f cm across onto the fixed jaw -> spans %.2f..%.2f, jaw %.2f cm (face at %.2f)%s"),
		*GetName(), *GetNameSafe(GunAsset), Near - Far, Source, Counted, Seat, GunFar, GunNear, JawClampTargetCm,
		JawFaceRestYCm + JawClampTargetCm,
		Near - Far > JawOpenLimitCm - JawClosedLimitCm ? TEXT(" | WIDER than the vice opens: jaw stays fully open") : TEXT(""));
}

// ==================== Queries ====================

int32 ATurretBuildable::GetUnlockedViceCount() const
{
	return FMath::Clamp(GetBuildLevel(), 1, MaxVices);
}

AShooterWeapon* ATurretBuildable::GetViceWeapon(int32 ViceIndex) const
{
	AShooterWeapon* const Weapon = ViceWeapons.IsValidIndex(ViceIndex) ? ViceWeapons[ViceIndex].Get() : nullptr;
	return IsValid(Weapon) ? Weapon : nullptr;
}

float ATurretBuildable::GetViceRange(int32 ViceIndex) const
{
	return (Vices.IsValidIndex(ViceIndex) && GetViceWeapon(ViceIndex)) ? Vices[ViceIndex].Range : 0.0f;
}

bool ATurretBuildable::IsRocketClass(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	if (!WeaponClass)
	{
		return false;
	}
	for (const TSubclassOf<AShooterWeapon>& Heavy : RocketViceWeaponClasses)
	{
		if (Heavy && WeaponClass->IsChildOf(Heavy))
		{
			return true;
		}
	}
	return false;
}

bool ATurretBuildable::FindViceFor(TSubclassOf<AShooterWeapon> WeaponClass, int32& OutViceIndex) const
{
	OutViceIndex = INDEX_NONE;
	if (!WeaponClass)
	{
		return false;
	}
	const int32 Unlocked = GetUnlockedViceCount();
	const bool bRocket = IsRocketClass(WeaponClass);
	for (int32 Index = 0; Index < Unlocked; ++Index)
	{
		// A heavy gun goes to the heavy vice and nowhere else; an ordinary one never goes there.
		if ((Index == RocketViceIndex) != bRocket)
		{
			continue;
		}
		if (!GetViceWeapon(Index))
		{
			OutViceIndex = Index;
			return true;
		}
	}
	return false;
}

int32 ATurretBuildable::FindTopUpViceFor(const AShooterWeapon* Weapon) const
{
	// A gun that refills itself or never runs out has no rounds to give: it would feed the turret
	// forever, and an empty turret is meant to be silent until someone brings it a gun.
	if (!Weapon || Weapon->IsMeleeWeapon() || Weapon->IsEnergyClass() || Weapon->HasInfiniteReserve())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < GetUnlockedViceCount(); ++Index)
	{
		const AShooterWeapon* const ViceWeapon = GetViceWeapon(Index);
		if (ViceWeapon && ViceWeapon->GetClass() == Weapon->GetClass())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ATurretBuildable::FindSwapViceFor(const AShooterWeapon* Weapon) const
{
	if (!Weapon || Weapon->IsMeleeWeapon())
	{
		return INDEX_NONE;
	}
	const bool bRocket = IsRocketClass(Weapon->GetClass());
	for (int32 Index = 0; Index < GetUnlockedViceCount(); ++Index)
	{
		// The same heavy/ordinary rule as a free vice. A gun of the same class is never a swap: it
		// either tops the vice up or, refilling itself, has nothing to add.
		if ((Index == RocketViceIndex) != bRocket)
		{
			continue;
		}
		const AShooterWeapon* const ViceWeapon = GetViceWeapon(Index);
		if (ViceWeapon && ViceWeapon->GetClass() != Weapon->GetClass())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ATurretBuildable::FindViceOf(const AShooterWeapon* Weapon) const
{
	for (int32 Index = 0; Index < ViceWeapons.Num(); ++Index)
	{
		if (Weapon && ViceWeapons[Index] == Weapon)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

APawn* ATurretBuildable::GetOwnerPawn() const
{
	const AShooterPlayerState* const OwnerState = GetOwnerPlayerState();
	return OwnerState ? OwnerState->GetPawn() : nullptr;
}

AShooterCharacter* ATurretBuildable::GetOwnerCharacter() const
{
	return Cast<AShooterCharacter>(GetOwnerPawn());
}

void ATurretBuildable::SyncNetOwner()
{
	// Net-owned by the owner's controller, not their pawn: the pawn dies and is replaced, the
	// controller stays for the whole session, and the client RPC that carries the hit marker has
	// to keep finding its way to that player.
	if (!HasAuthority())
	{
		return;
	}
	const AShooterPlayerState* const OwnerState = GetOwnerPlayerState();
	AController* const Controller = OwnerState ? Cast<AController>(OwnerState->GetOwner()) : nullptr;
	if (Controller && GetOwner() != Controller)
	{
		SetOwner(Controller);
	}
}

FVector ATurretBuildable::EyeLocation() const
{
	const USceneComponent* const Mount = ViceMounts.IsValidIndex(0) ? ViceMounts[0] : nullptr;
	return Mount ? Mount->GetComponentLocation() : GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
}

float ATurretBuildable::TurnRateDegPerSec() const
{
	if (TurnRateDegPerSecByLevel.Num() == 0)
	{
		return 180.0f;
	}
	const int32 Index = FMath::Clamp(GetBuildLevel() - 1, 0, TurnRateDegPerSecByLevel.Num() - 1);
	return FMath::Max(1.0f, TurnRateDegPerSecByLevel[Index]);
}

// ==================== Range and the gun's numbers ====================

float ATurretBuildable::ComputeRangeFor(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return 0.0f;
	}
	// A gun that says its range outright is taken at its word: the spread numbers of the pack's guns
	// are not tuned per gun (2026-09-13: all of them 2 deg, 0.25 per shot), so the formula below
	// gives them all the same short leash until somebody tunes AimVariance.
	if (Weapon->GetMountedRangeCm() > 0.0f)
	{
		return Weapon->GetMountedRangeCm();
	}
	// The spread the gun would have in a player's hands, standing still and emptying the magazine:
	// the base times the still multiplier, plus the bloom averaged over the magazine (bloom after k
	// shots is min(k * per shot, cap); the recovery delay is about one refire, so a held trigger
	// never gets any back). The range is where that cone is still narrower than a body. A shotgun
	// keeps its spread in the pellet pattern rather than in AimVariance, and GetCrosshairSpreadDegrees
	// is where it says so (an ordinary gun answers 0 there while mounted).
	const FWeaponSpreadConfig& Config = Weapon->GetSpreadConfig();
	const float Base = FMath::Max(Weapon->GetAimVariance() * Config.StillMultiplier, Weapon->GetCrosshairSpreadDegrees());
	const int32 Rounds = FMath::Max(1, Weapon->GetMagazineSize());
	float BloomSum = 0.0f;
	for (int32 Shot = 0; Shot < Rounds; ++Shot)
	{
		BloomSum += FMath::Min(Shot * Config.PerShotDegrees, Config.MaxBloomDegrees);
	}
	const float Expected = FMath::Clamp(Base + BloomSum / Rounds, 0.0f, Config.MaxSpreadDegrees);

	float Range = MaxRangeCm;
	if (Expected > 0.01f)
	{
		Range = TargetHalfWidthCm / FMath::Tan(FMath::DegreesToRadians(Expected));
	}
	return FMath::Clamp(Range * RangeScale, MinRangeCm, MaxRangeCm);
}

float ATurretBuildable::ProjectileSpeedOf(const AShooterWeapon* Weapon)
{
	if (!Weapon || Weapon->IsHitscan() || !Weapon->GetProjectileClass())
	{
		return 0.0f;
	}
	const AShooterProjectile* const CDO = Weapon->GetProjectileClass()->GetDefaultObject<AShooterProjectile>();
	const UProjectileMovementComponent* const Move = CDO ? CDO->FindComponentByClass<UProjectileMovementComponent>() : nullptr;
	return Move ? FMath::Max(0.0f, Move->InitialSpeed) : 0.0f;
}

float ATurretBuildable::ExplosionRadiusOf(const AShooterWeapon* Weapon)
{
	if (!Weapon || Weapon->IsHitscan() || !Weapon->GetProjectileClass())
	{
		return 0.0f;
	}
	const AShooterProjectile* const CDO = Weapon->GetProjectileClass()->GetDefaultObject<AShooterProjectile>();
	return CDO ? CDO->GetExplosionRadius() : 0.0f;
}

TSubclassOf<ADroppedRangedWeapon> ATurretBuildable::ResolveDropClass(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return nullptr;
	}
	if (Weapon->SourceYankDropClass)
	{
		return Weapon->SourceYankDropClass;
	}
	// Nearest ancestor in the fallback table wins, so a table entry for a base class covers the
	// family while a specific entry still beats it.
	TSubclassOf<ADroppedRangedWeapon> Best;
	int32 BestDepth = -1;
	for (const TPair<TSubclassOf<AShooterWeapon>, TSubclassOf<ADroppedRangedWeapon>>& Pair : DropClassFallbacks)
	{
		if (!Pair.Key || !Pair.Value || !Weapon->GetClass()->IsChildOf(Pair.Key))
		{
			continue;
		}
		int32 Depth = 0;
		for (const UClass* Walk = Pair.Key; Walk; Walk = Walk->GetSuperClass())
		{
			++Depth;
		}
		if (Depth > BestDepth)
		{
			BestDepth = Depth;
			Best = Pair.Value;
		}
	}
	return Best;
}

// ==================== Feeding ====================

bool ATurretBuildable::AcceptWeaponFrom(AShooterCharacter* Donor, int32 ReportedLoadedRounds, AShooterWeapon* Weapon)
{
	if (!HasAuthority() || !Donor)
	{
		return false;
	}
	// While rising or standing. A turret placed with its gun takes it at once, before the first
	// frame of construction; a destroyed one takes nothing.
	if (GetBuildableState() != EBuildableState::Active && GetBuildableState() != EBuildableState::Constructing)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused a gun: not standing (state %d)"), *GetName(), static_cast<int32>(GetBuildableState()));
		return false;
	}
	if (PolarityTeams::GetTeam(Donor) != TeamByte)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused a gun from %s: other side"), *GetName(), *Donor->GetName());
		return false;
	}

	// The gun named by the menu, or the one in hand. Either way it has to be the donor's own: the
	// client names an actor, and an actor is not a proof of ownership.
	AShooterWeapon* const Held = Weapon ? Weapon : Donor->GetCurrentWeapon();
	if (!Held || Held->IsMeleeWeapon() || !Donor->GetOwnedWeapons().Contains(Held))
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused: %s has no such gun (%s)"), *GetName(), *Donor->GetName(), *GetNameSafe(Held));
		return false;
	}
	// Taking the gun out of the hands mid-swing or mid-holster would leave the switch machinery
	// pointing at nothing; a gun that is not in the hands leaves without touching it. The player's
	// own holster is the exception: placing a building puts the gun away, and the placement hands
	// it over while it is still away (ReleaseWeaponToMount leaves the next gun for the draw).
	const EWeaponSwitchPhase Phase = Donor->GetWeaponSwitchPhase();
	const bool bHolsteredByPlayer = Phase == EWeaponSwitchPhase::StowedByPlayer || Phase == EWeaponSwitchPhase::StowingByPlayer;
	if (Held == Donor->GetCurrentWeapon() && Phase != EWeaponSwitchPhase::None && !bHolsteredByPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused: %s is mid-switch or holstered (phase %d)"),
			*GetName(), *Donor->GetName(), static_cast<int32>(Donor->GetWeaponSwitchPhase()));
		return false;
	}

	// Rounds come only from guns (Docs/Dispenser_Core_Refinery_Plan_2026-09-22.md, section 0.3):
	// a gun of a class a vice already holds hands over its rounds and stays with the player; a new
	// gun goes into a free vice with every round it carries. Metal buys none.
	const int32 TopUpIndex = FindTopUpViceFor(Held);
	if (TopUpIndex != INDEX_NONE)
	{
		return TopUpVice(Donor, Held, TopUpIndex, ReportedLoadedRounds);
	}

	// No free vice: a taken one of the right kind is swapped, its gun put down once the new one has
	// actually left the donor's hands (the author's call, 2026-09-24).
	int32 ViceIndex = INDEX_NONE;
	bool bSwap = false;
	if (!FindViceFor(Held->GetClass(), ViceIndex))
	{
		ViceIndex = FindSwapViceFor(Held);
		if (ViceIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused %s: no free vice and nothing to swap (level %d, heavy %d)"),
				*GetName(), *Held->GetClass()->GetName(), GetBuildLevel(), IsRocketClass(Held->GetClass()) ? 1 : 0);
			return false;
		}
		bSwap = true;
	}

	// Everything the new copy has to inherit, read before the donor's gun is destroyed.
	const TSubclassOf<AShooterWeapon> WeaponClass = Held->GetClass();
	const TSubclassOf<ADroppedRangedWeapon> DropClass = ResolveDropClass(Held);
	const float DropCharge = Held->SourceDropCharge;
	const int32 Magazine = FMath::Max(1, Held->GetMagazineSize());

	int32 Loaded = 0;
	int32 Reserve = -1;
	if (!Donor->ReleaseWeaponToMount(Held, Loaded, Reserve))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: %s would not release %s"), *GetName(), *Donor->GetName(), *GetNameSafe(Held));
		return false;
	}
	if (ReportedLoadedRounds >= 0)
	{
		Loaded = ReportedLoadedRounds;
	}
	Loaded = FMath::Clamp(Loaded, 0, Magazine);
	if (bSwap)
	{
		EjectViceWeapon(ViceIndex, Donor);
	}
	if (Reserve < 0)
	{
		Reserve = FMath::Max(0, InfiniteReserveMagazinesGranted) * Magazine;
	}

	// The turret's own copy of the gun. Owned by the turret, which is what makes the turret the
	// weapon holder the gun attaches to and reports hits to; instigated by the owner's pawn, which
	// is what makes a kill the owner's. Into the vice array BEFORE FinishSpawning, because the
	// gun's BeginPlay asks AttachWeaponMeshes which vice it belongs to.
	const USceneComponent* const Mount = ViceMounts.IsValidIndex(ViceIndex) ? ViceMounts[ViceIndex] : nullptr;
	const FTransform SpawnTransform = Mount ? Mount->GetComponentTransform() : GetActorTransform();
	AShooterWeapon* const Mounted = GetWorld()->SpawnActorDeferred<AShooterWeapon>(WeaponClass, SpawnTransform,
		this, GetOwnerPawn(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Mounted)
	{
		UE_LOG(LogTemp, Error, TEXT("[TURRET_DEBUG] %s could not spawn %s; the donor's gun is gone"), *GetName(), *WeaponClass->GetName());
		return false;
	}
	Mounted->SetMounted(true);
	Mounted->SourceYankDropClass = DropClass;
	Mounted->SourceDropCharge = DropCharge;
	ViceWeapons[ViceIndex] = Mounted;
	Mounted->FinishSpawning(SpawnTransform);
	Mounted->ActivateWeapon();

	FTurretVice& Vice = Vices[ViceIndex];
	Vice.Range = ComputeRangeFor(Mounted);
	Vice.NextShotTime = 0.0f;
	Vice.ReloadEndTime = -1.0f;
	Vice.DropClass = DropClass;
	Vice.DropCharge = DropCharge;
	ViceRounds[ViceIndex] = Loaded;
	// No ceiling: every round the gun brought is kept, the same as a looted gun in the hands.
	ViceReserve[ViceIndex] = FMath::Max(0, Reserve);

	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s took %s from %s into vice %d: %d loaded, %d reserve, range %.0f cm, %s, refire %.2f s, drop %s"),
		*GetName(), *WeaponClass->GetName(), *Donor->GetName(), ViceIndex, ViceRounds[ViceIndex], ViceReserve[ViceIndex],
		Vice.Range, Mounted->IsHitscan() ? TEXT("hitscan") : TEXT("projectile"),
		Mounted->GetActualRefireRate(), *GetNameSafe(DropClass));

	OnBuildableChanged.Broadcast(this);
	return true;
}

// ==================== IShooterWeaponHolder ====================

void ATurretBuildable::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}
	// On a client the gun can arrive before the array that says which vice it is in; it goes to
	// the first mount for now and OnRep_ViceWeapons seats it properly when the array lands.
	int32 ViceIndex = FindViceOf(Weapon);
	if (ViceIndex == INDEX_NONE)
	{
		ViceIndex = 0;
	}
	USceneComponent* const Mount = ViceMounts.IsValidIndex(ViceIndex) ? ViceMounts[ViceIndex] : nullptr;
	if (!Mount)
	{
		return;
	}

	// The explicit form: scale kept, or the gun's authored size is replaced by the mount's (the
	// same trap the NPC path fell into, Weapons.md).
	const FAttachmentTransformRules Rules(
		EAttachmentRule::SnapToTarget,
		EAttachmentRule::SnapToTarget,
		EAttachmentRule::KeepRelative,
		false);
	Weapon->AttachToActor(this, Rules);

	// The mount is a hand: the gun's grip socket is seated on it and oriented with it, exactly as
	// AShooterNPC::AttachWeaponMeshes seats a gun in a hand, so every gun of the pack lies the same
	// way in every vice.
	if (USkeletalMeshComponent* const TPMesh = Weapon->GetThirdPersonMesh())
	{
		TPMesh->AttachToComponent(Mount, Rules);
		AShooterWeapon::AlignMeshToGripSocket(TPMesh,
			AShooterWeapon::PickThirdPersonSocket(TPMesh, AShooterWeapon::OptionalGripSocketName));
	}
	// The first person mesh rides along hidden, the way it does on an NPC: the muzzle flash and the
	// beam start are read off it, so it has to be where the gun is even though nobody sees it.
	if (USkeletalMeshComponent* const FPMesh = Weapon->GetFirstPersonMesh())
	{
		FPMesh->AttachToComponent(Mount, Rules);
		AShooterWeapon::AlignMeshToGripSocket(FPMesh, AShooterWeapon::OptionalGripSocketName);
		FPMesh->SetVisibility(false, true);
	}

	// Now that the gun lies where it will stay, find where the jaw has to stop to hold it.
	if (IsHeadVice(ViceIndex) && FindViceOf(Weapon) == HeadViceIndex)
	{
		MeasureJawClamp(Weapon);
	}
}

FVector ATurretBuildable::GetWeaponTargetLocation()
{
	// Nothing calls this on a mounted gun (FireMounted takes the point as an argument), but the
	// interface asks for an answer: the target, or a point down the first barrel.
	if (CurrentTarget)
	{
		return PolarityAim::ResolveAimPointExact(CurrentTarget);
	}
	return MuzzleLocationOf(0) + BarrelDirectionOf(0) * 1000.0f;
}

void ATurretBuildable::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// A gun gets into a turret by being fed, never by being granted.
	UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: AddWeaponClass(%s) ignored, feed the turret instead"),
		*GetName(), *GetNameSafe(WeaponClass));
}

void ATurretBuildable::OnWeaponHitFeedback(const FHitFeedbackContext& Context)
{
	// Arrives on the server straight from ApplyWeaponHit (there is no pawn owner to route around).
	// It is the owner's confirmation, marked as coming from the turret, in the turret's voice.
	if (!HasAuthority())
	{
		return;
	}
	AShooterCharacter* const OwnerCharacter = GetOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	FHitFeedbackContext Remote = Context;
	Remote.bRemote = true;
	if (OwnerFeedbackSet)
	{
		Remote.FeedbackSet = OwnerFeedbackSet;
	}

	if (OwnerCharacter->IsLocallyControlled())
	{
		OwnerCharacter->OnWeaponHitFeedback(Remote);
	}
	else
	{
		SyncNetOwner();
		Client_OwnerHitFeedback(Remote);
	}
}

void ATurretBuildable::Client_OwnerHitFeedback_Implementation(const FHitFeedbackContext& Context)
{
	// On the owner's machine. Their pawn is the one behind the controller that owns this turret.
	const APlayerController* const PC = Cast<APlayerController>(GetOwner());
	AShooterCharacter* const OwnerCharacter = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : GetOwnerCharacter();
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		OwnerCharacter->OnWeaponHitFeedback(Context);
	}
}

// ==================== Replication echoes ====================

void ATurretBuildable::OnRep_ViceWeapons()
{
	// Seat every gun on its own mount now that the array says which is which.
	for (int32 Index = 0; Index < ViceWeapons.Num(); ++Index)
	{
		if (AShooterWeapon* const Weapon = GetViceWeapon(Index))
		{
			AttachWeaponMeshes(Weapon);
		}
	}
	OnBuildableChanged.Broadcast(this);
}

void ATurretBuildable::OnRep_Ammo()
{
	OnBuildableChanged.Broadcast(this);
}

// ==================== Hooks ====================

void ATurretBuildable::OnLevelChanged(int32 NewLevel)
{
	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s level %d: %d vice(s) open"), *GetName(), NewLevel, GetUnlockedViceCount());
}

void ATurretBuildable::OnDestroyed_Native()
{
	// Whatever way it goes, the guns come back out: a destroyed turret is not also a lost gun.
	for (int32 Index = 0; Index < ViceWeapons.Num(); ++Index)
	{
		DropViceWeapon(Index);
	}
	CurrentTarget = nullptr;
}

void ATurretBuildable::DropViceWeapon(int32 ViceIndex)
{
	AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	if (!Weapon)
	{
		return;
	}
	const FTurretVice& Vice = Vices[ViceIndex];
	const TSubclassOf<ADroppedRangedWeapon> DropClass = Vice.DropClass ? Vice.DropClass : ResolveDropClass(Weapon);
	if (DropClass)
	{
		const USceneComponent* const Mount = ViceMounts.IsValidIndex(ViceIndex) ? ViceMounts[ViceIndex] : nullptr;
		const FVector SpawnLocation = (Mount ? Mount->GetComponentLocation() : GetActorLocation()) + FVector(0.0f, 0.0f, 30.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADroppedRangedWeapon* const Drop = GetWorld()->SpawnActor<ADroppedRangedWeapon>(DropClass, SpawnLocation, GetActorRotation(), Params);
		if (Drop)
		{
			// The rounds go with the gun, written the way the pickup reads them: an energy gun takes
			// a loaded count and a reserve, a cells gun takes one number for everything it holds.
			Drop->bCanBeCaptured = true;
			if (Weapon->IsEnergyClass())
			{
				Drop->SpawnedBulletCount = ViceRounds[ViceIndex];
				Drop->CarriedEnergyReserve = ViceReserve[ViceIndex];
			}
			else
			{
				Drop->SpawnedBulletCount = FMath::Max(1, ViceRounds[ViceIndex] + ViceReserve[ViceIndex]);
			}
			Drop->SetCharge(Vice.DropCharge);
			if (UEMFChargeWidgetSubsystem* const WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
			{
				WidgetSub->UnregisterDroppedRangedWeapon(Drop);
			}
			if (UStaticMeshComponent* const DropMesh = Drop->WeaponMesh)
			{
				DropMesh->AddImpulse(FVector(FMath::FRandRange(-150.0f, 150.0f), FMath::FRandRange(-150.0f, 150.0f), 250.0f), NAME_None, true);
			}
			UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s put %s down as %s with %d + %d rounds"),
				*GetName(), *Weapon->GetClass()->GetName(), *Drop->GetName(), ViceRounds[ViceIndex], ViceReserve[ViceIndex]);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: no drop class for %s (no SourceYankDropClass and no DropClassFallbacks entry), the gun is lost"),
			*GetName(), *Weapon->GetClass()->GetName());
	}

	Weapon->Destroy();
	ClearVice(ViceIndex);
}

void ATurretBuildable::EjectViceWeapon(int32 ViceIndex, AShooterCharacter* Donor)
{
	AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	if (!Weapon)
	{
		return;
	}

	// A gun with a floor version goes down next to the turret with its rounds, the same way a
	// destroyed turret lets its guns go.
	const TSubclassOf<ADroppedRangedWeapon> DropClass = Vices[ViceIndex].DropClass ? Vices[ViceIndex].DropClass : ResolveDropClass(Weapon);
	if (DropClass || !Donor)
	{
		DropViceWeapon(ViceIndex);
		return;
	}

	// No floor version (a gun that only ever came from a level pickup): it would vanish, so it goes
	// back into the donor's hands instead, as loot, with every round it had left.
	const TSubclassOf<AShooterWeapon> WeaponClass = Weapon->GetClass();
	const int32 Loaded = ViceRounds[ViceIndex];
	const int32 Reserve = ViceReserve[ViceIndex];
	Weapon->Destroy();
	ClearVice(ViceIndex);

	Donor->AddWeaponClass(WeaponClass);
	if (AShooterWeapon* const Returned = Donor->FindWeaponOfType(WeaponClass))
	{
		Returned->ConfigureFiniteEnergyReserve();
		Returned->SetBulletCount(Loaded);
		Returned->SetEnergyReserve(Reserve);
	}
	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s: swapped %s out of vice %d back to %s's hands (no floor version) with %d + %d rounds"),
		*GetName(), *WeaponClass->GetName(), ViceIndex, *Donor->GetName(), Loaded, Reserve);
}

bool ATurretBuildable::TopUpVice(AShooterCharacter* Donor, AShooterWeapon* Weapon, int32 ViceIndex, int32 ReportedLoadedRounds)
{
	if (!Donor || !Weapon || !ViceReserve.IsValidIndex(ViceIndex))
	{
		return false;
	}

	// Loaded: the donor's own count when it sent one, the way a mounted gun is read. Spare: off the
	// gun for a looted (energy) one, out of the cells for a cell gun, which hold the loaded part too.
	const int32 Magazine = FMath::Max(1, Weapon->GetMagazineSize());
	const int32 Loaded = FMath::Clamp(ReportedLoadedRounds >= 0 ? ReportedLoadedRounds : Weapon->GetBulletCount(), 0, Magazine);
	int32 Spare = 0;
	if (Weapon->UsesEnergyReserve())
	{
		Spare = Weapon->GetEnergyReserve();
		Weapon->SetEnergyReserve(0);
	}
	else if (Weapon->OwnsAmmoCells())
	{
		if (UInventoryComponent* const Inventory = Donor->GetInventoryComponent())
		{
			Spare = FMath::Max(0, Inventory->TakeAllAmmo() - Loaded);
		}
	}
	Weapon->SetBulletCount(0);

	const int32 Rounds = Loaded + Spare;
	if (Rounds <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s: %s's %s is empty, nothing to top up vice %d with"),
			*GetName(), *Donor->GetName(), *Weapon->GetClass()->GetName(), ViceIndex);
		return false;
	}

	ViceReserve[ViceIndex] += Rounds;
	// A vice that ran dry starts its reload at once, so the rounds that bring it back are also the
	// ones that make it fire.
	if (ViceRounds[ViceIndex] <= 0 && Vices[ViceIndex].ReloadEndTime < 0.0f)
	{
		StartReload(ViceIndex, GetWorld()->GetTimeSeconds());
	}

	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s: %s topped up vice %d with %d round(s) from %s (%d loaded + %d spare), reserve now %d; the gun stays with them"),
		*GetName(), *Donor->GetName(), ViceIndex, Rounds, *Weapon->GetClass()->GetName(), Loaded, Spare, ViceReserve[ViceIndex]);
	OnBuildableChanged.Broadcast(this);
	return true;
}

void ATurretBuildable::ClearVice(int32 ViceIndex)
{
	if (!Vices.IsValidIndex(ViceIndex))
	{
		return;
	}
	ViceWeapons[ViceIndex] = nullptr;
	ViceRounds[ViceIndex] = 0;
	ViceReserve[ViceIndex] = 0;
	Vices[ViceIndex].Range = 0.0f;
	Vices[ViceIndex].ReloadEndTime = -1.0f;
	Vices[ViceIndex].DropClass = nullptr;
	OnBuildableChanged.Broadcast(this);
}

// ==================== Aiming, every machine ====================

void ATurretBuildable::Tick(float DeltaSeconds)
{
	// Before the base tick, so the server's fire check inside TickActive reads this frame's aim.
	if (IsActive())
	{
		UpdateAim(DeltaSeconds);
	}
	// The jaw works while the turret rises too: a turret placed with its gun takes it at once.
	UpdateJaw(DeltaSeconds);
	Super::Tick(DeltaSeconds);
}

FRotator ATurretBuildable::BarrelRotationFor(const FVector& WorldDirection) const
{
	// The mount rotation that points the gripped gun's barrel down WorldDirection with the gun
	// upright: Qd takes X to the direction, Qb takes X to the barrel in mount space, so Qd * Qb^-1
	// takes the barrel to the direction.
	const FQuat ToBarrel = FRotationMatrix::MakeFromXZ(BarrelLocalDirection.GetSafeNormal(), BarrelLocalUp.GetSafeNormal()).ToQuat();
	const FQuat ToDirection = FRotationMatrix::MakeFromXZ(WorldDirection.GetSafeNormal(), FVector::UpVector).ToQuat();
	return (ToDirection * ToBarrel.Inverse()).Rotator();
}

FVector ATurretBuildable::BarrelDirectionOf(int32 ViceIndex) const
{
	// The head's barrel from the joint angles, not from the socket: the bones are posed after this
	// actor ticks, so the socket still shows last frame's aim while the angles are already this one's.
	if (IsHeadVice(ViceIndex))
	{
		return TurretMesh->GetComponentQuat().RotateVector(HeadRotation.Quaternion().RotateVector(HeadBarrelDirection));
	}
	const USceneComponent* const Mount = ViceMounts.IsValidIndex(ViceIndex) ? ViceMounts[ViceIndex] : nullptr;
	return Mount ? Mount->GetComponentQuat().RotateVector(BarrelLocalDirection.GetSafeNormal()) : GetActorForwardVector();
}

FVector ATurretBuildable::MuzzleLocationOf(int32 ViceIndex) const
{
	const USceneComponent* const Mount = ViceMounts.IsValidIndex(ViceIndex) ? ViceMounts[ViceIndex] : nullptr;
	if (const AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex))
	{
		const USkeletalMeshComponent* const TPMesh = Weapon->GetThirdPersonMesh();
		if (TPMesh && TPMesh->DoesSocketExist(Weapon->GetMuzzleSocketName()))
		{
			return TPMesh->GetSocketLocation(Weapon->GetMuzzleSocketName());
		}
	}
	return Mount ? Mount->GetComponentLocation() : EyeLocation();
}

FVector ATurretBuildable::ComputeAimPointFor(int32 ViceIndex, const AActor* Target, float& OutFlightTime) const
{
	OutFlightTime = 0.0f;
	const FVector Point = PolarityAim::ResolveAimPointExact(Target);
	const AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	const float Speed = ProjectileSpeedOf(Weapon);
	if (Speed <= 0.0f)
	{
		return Point;
	}
	// Lead: where the body will be when the round arrives, with one refinement so the flight time
	// is measured to the led point rather than to where the body is now.
	const FVector Muzzle = MuzzleLocationOf(ViceIndex);
	const FVector Velocity = Target->GetVelocity();
	float FlightTime = FVector::Dist(Point, Muzzle) / Speed;
	FVector Lead = Point + Velocity * FlightTime;
	FlightTime = FVector::Dist(Lead, Muzzle) / Speed;
	Lead = Point + Velocity * FlightTime;
	OutFlightTime = FlightTime;
	return Lead;
}

void ATurretBuildable::UpdateAim(float DeltaSeconds)
{
	const float TurnRate = TurnRateDegPerSec();
	const FQuat BaseQuat = GetActorQuat();
	const AActor* const Target = IsValid(CurrentTarget) ? CurrentTarget.Get() : nullptr;

	for (int32 Index = 0; Index < MaxVices; ++Index)
	{
		if (IsHeadVice(Index))
		{
			// This mount is the head's socket: it is aimed by the joints, never rotated directly.
			UpdateHeadAim(DeltaSeconds, Target);
			continue;
		}
		USceneComponent* const Mount = ViceMounts.IsValidIndex(Index) ? ViceMounts[Index] : nullptr;
		if (!Mount)
		{
			continue;
		}
		FTurretVice& Vice = Vices[Index];

		// Where this barrel wants to point: the (led) target for a loaded vice, the rest pose for
		// an empty one or when there is nothing to shoot.
		FVector WantedDirection;
		if (Target && GetViceWeapon(Index))
		{
			float FlightTime = 0.0f;
			WantedDirection = (ComputeAimPointFor(Index, Target, FlightTime) - MuzzleLocationOf(Index)).GetSafeNormal();
		}
		else
		{
			const FQuat RestQuat = BaseQuat * RestRelativeRotations[Index].Quaternion();
			WantedDirection = RestQuat.RotateVector(BarrelLocalDirection.GetSafeNormal());
		}
		if (WantedDirection.IsNearlyZero())
		{
			continue;
		}

		FRotator Goal = WantedDirection.Rotation();
		Goal.Pitch = FMath::Clamp(Goal.Pitch, -MaxPitchDegrees, MaxPitchDegrees);
		Goal.Roll = 0.0f;

		// Constant rate, TF2's way: the barrel is either on the target or still getting there, and
		// a body crossing faster than this is the one thing a turret cannot hit.
		Vice.BarrelRotation = FMath::RInterpConstantTo(Vice.BarrelRotation, Goal, DeltaSeconds, TurnRate);
		Vice.BarrelRotation.Roll = 0.0f;
		Mount->SetWorldRotation(BarrelRotationFor(Vice.BarrelRotation.Vector()));
	}
}

// ==================== Server: targets ====================

void ATurretBuildable::TickActive(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextScanTime)
	{
		ScanForTarget();
		NextScanTime = Now + FMath::Max(0.05f, TargetScanInterval);
	}
	if (CurrentTarget && !IsValid(CurrentTarget))
	{
		CurrentTarget = nullptr;
	}
	for (int32 Index = 0; Index < GetUnlockedViceCount(); ++Index)
	{
		TickVice(Index, Now);
	}
	if (CVarTurretDebug.GetValueOnGameThread() != 0)
	{
		DrawDebug(Now);
	}
}

bool ATurretBuildable::HasLineOfSightTo(const FVector& From, const AActor* Target, const FVector& Point) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretSight), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(this);
	if (Target)
	{
		Params.AddIgnoredActor(Target);
	}
	for (const TObjectPtr<AShooterWeapon>& Weapon : ViceWeapons)
	{
		if (Weapon)
		{
			Params.AddIgnoredActor(Weapon);
		}
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, From, Point, ECC_Visibility, Params);
}

bool ATurretBuildable::IsEligibleTarget(const APawn* Pawn, float MaxRange, float& OutDistance) const
{
	OutDistance = 0.0f;
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->CanBeDamaged())
	{
		return false;
	}
	if (const AShooterNPC* const NPC = Cast<AShooterNPC>(Pawn); NPC && NPC->IsDead())
	{
		return false;
	}

	const FVector Eye = EyeLocation();
	const FVector Point = PolarityAim::ResolveAimPointExact(Pawn);
	const float Distance = FVector::Dist(Point, Eye);
	if (Distance > MaxRange || Distance < 1.0f)
	{
		return false;
	}

	// Angular speed across the sight line: what the barrel would have to turn at to stay on the
	// body. Above the turn rate it cannot, so it is not a target, whatever its distance.
	const FVector ToTarget = (Point - Eye) / Distance;
	const FVector Velocity = Pawn->GetVelocity();
	const FVector Across = Velocity - ToTarget * FVector::DotProduct(Velocity, ToTarget);
	const float AngularDegPerSec = FMath::RadiansToDegrees(Across.Size() / Distance);
	if (AngularDegPerSec > TurnRateDegPerSec())
	{
		return false;
	}

	if (!HasLineOfSightTo(Eye, Pawn, Point))
	{
		return false;
	}
	OutDistance = Distance;
	return true;
}

void ATurretBuildable::ScanForTarget()
{
	// The reach of the turret is the longest reach among vices that have anything to fire.
	float MaxRange = 0.0f;
	for (int32 Index = 0; Index < GetUnlockedViceCount(); ++Index)
	{
		if (GetViceWeapon(Index) && (ViceRounds[Index] > 0 || ViceReserve[Index] > 0))
		{
			MaxRange = FMath::Max(MaxRange, Vices[Index].Range);
		}
	}
	AActor* const OldTarget = CurrentTarget;
	if (MaxRange <= 0.0f)
	{
		CurrentTarget = nullptr;
	}
	else
	{
		TArray<APawn*> Hostiles;
		PolarityTeams::GatherHostilePawns(this, Hostiles);

		APawn* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		bool bCurrentStillGood = false;
		float CurrentDistance = 0.0f;
		for (APawn* const Candidate : Hostiles)
		{
			float Distance = 0.0f;
			if (!IsEligibleTarget(Candidate, MaxRange, Distance))
			{
				continue;
			}
			if (Candidate == CurrentTarget)
			{
				bCurrentStillGood = true;
				CurrentDistance = Distance;
			}
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Candidate;
			}
		}

		// TF2's hysteresis: the current target is kept until something is nearer by a quarter, so a
		// turret between two enemies does not saw back and forth.
		if (!bCurrentStillGood)
		{
			CurrentTarget = Best;
		}
		else if (Best && Best != CurrentTarget && BestDistance < CurrentDistance * RetargetCloserFraction)
		{
			CurrentTarget = Best;
		}
	}

	if (CurrentTarget != OldTarget && CVarTurretDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s target: %s -> %s (reach %.0f)"), *GetName(),
			*GetNameSafe(OldTarget), *GetNameSafe(CurrentTarget), MaxRange);
	}
}

// ==================== Server: firing ====================

void ATurretBuildable::StartReload(int32 ViceIndex, float Now)
{
	const AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	if (!Weapon || ViceReserve[ViceIndex] <= 0)
	{
		return;
	}
	const float ReloadTime = Weapon->GetReloadTime() > 0.05f ? Weapon->GetReloadTime() : ReloadFallbackSeconds;
	Vices[ViceIndex].ReloadEndTime = Now + ReloadTime;
}

void ATurretBuildable::FinishReload(int32 ViceIndex)
{
	const AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	Vices[ViceIndex].ReloadEndTime = -1.0f;
	if (!Weapon)
	{
		return;
	}
	const int32 Loaded = FMath::Min(FMath::Max(1, Weapon->GetMagazineSize()), ViceReserve[ViceIndex]);
	ViceRounds[ViceIndex] += Loaded;
	ViceReserve[ViceIndex] -= Loaded;
	OnBuildableChanged.Broadcast(this);
}

bool ATurretBuildable::IsBarrelOnHostile(int32 ViceIndex, AActor*& OutHitActor) const
{
	OutHitActor = nullptr;
	const FVector Start = MuzzleLocationOf(ViceIndex);
	const FVector Direction = BarrelDirectionOf(ViceIndex);
	const float Reach = Vices[ViceIndex].Range + TargetHalfWidthCm * 2.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretBarrel), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(this);
	for (const TObjectPtr<AShooterWeapon>& Weapon : ViceWeapons)
	{
		if (Weapon)
		{
			Params.AddIgnoredActor(Weapon);
		}
	}

	// The same two traces the shot itself will make: a wall ends the ray, and a body is found by
	// object type before that wall.
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(WallHit, Start, Start + Direction * Reach, ECC_Visibility, Params);
	const float WallDistance = bHitWall ? WallHit.Distance : Reach;

	FCollisionObjectQueryParams PawnObjects;
	PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult PawnHit;
	if (!GetWorld()->LineTraceSingleByObjectType(PawnHit, Start, Start + Direction * WallDistance, PawnObjects, Params))
	{
		return false;
	}
	AActor* const HitActor = PawnHit.GetActor();
	if (!HitActor || !HitActor->CanBeDamaged() || !PolarityTeams::AreHostile(this, HitActor))
	{
		return false;
	}
	if (const AShooterNPC* const NPC = Cast<AShooterNPC>(HitActor); NPC && NPC->IsDead())
	{
		return false;
	}
	OutHitActor = HitActor;
	return true;
}

void ATurretBuildable::TickVice(int32 ViceIndex, float Now)
{
	AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	if (!Weapon)
	{
		if (ViceWeapons.IsValidIndex(ViceIndex) && ViceWeapons[ViceIndex] != nullptr)
		{
			// The gun went away under us (destroyed from outside). Forget it cleanly.
			ClearVice(ViceIndex);
		}
		TurretGate::Report(this, ViceIndex, TurretGate::NoGun);
		return;
	}
	FTurretVice& Vice = Vices[ViceIndex];

	if (Vice.ReloadEndTime >= 0.0f)
	{
		if (Now < Vice.ReloadEndTime)
		{
			TurretGate::Report(this, ViceIndex, TurretGate::Reloading);
			return;
		}
		FinishReload(ViceIndex);
	}
	if (ViceRounds[ViceIndex] <= 0)
	{
		if (ViceReserve[ViceIndex] > 0)
		{
			StartReload(ViceIndex, Now);
		}
		TurretGate::Report(this, ViceIndex, TurretGate::Empty);
		return;
	}

	AActor* const Target = IsValid(CurrentTarget) ? CurrentTarget.Get() : nullptr;
	if (!Target)
	{
		TurretGate::Report(this, ViceIndex, TurretGate::NoTarget);
		return;
	}
	if (Now < Vice.NextShotTime)
	{
		TurretGate::Report(this, ViceIndex, TurretGate::Refire);
		return;
	}

	float FlightTime = 0.0f;
	const FVector AimPoint = ComputeAimPointFor(ViceIndex, Target, FlightTime);
	const FVector Muzzle = MuzzleLocationOf(ViceIndex);
	const float Distance = FVector::Dist(AimPoint, Muzzle);
	if (Distance > Vice.Range || Distance < 1.0f)
	{
		TurretGate::Report(this, ViceIndex, TurretGate::OutOfRange,
			*FString::Printf(TEXT(" (%.0f of %.0f cm)"), Distance, Vice.Range));
		return;
	}

	if (Weapon->IsHitscan())
	{
		// The honest test: the shot goes where the barrel points, so fire only when the barrel is
		// on a body. No wasted rounds while the vice is still turning.
		AActor* OnActor = nullptr;
		if (!IsBarrelOnHostile(ViceIndex, OnActor))
		{
			TurretGate::Report(this, ViceIndex, TurretGate::BarrelOff);
			return;
		}
		TurretGate::Report(this, ViceIndex, TurretGate::Fired,
			*FString::Printf(TEXT(" hitscan at %s, %.0f cm, %d round(s) left"), *GetNameSafe(OnActor), Distance, ViceRounds[ViceIndex] - 1));
		FireVice(ViceIndex, AimPoint, Now);
		return;
	}

	// A projectile: the barrel has to be on the led point within the body's angular size, the body
	// must not be able to walk out of the round's way during the flight, and a blast must not come
	// back to the turret.
	const float AngularRadius = FMath::Atan2(TargetHalfWidthCm, Distance);
	const FVector ToAim = (AimPoint - Muzzle) / Distance;
	const float CosToAim = FVector::DotProduct(BarrelDirectionOf(ViceIndex), ToAim);
	if (CosToAim < FMath::Cos(AngularRadius))
	{
		TurretGate::Report(this, ViceIndex, TurretGate::NotAligned,
			*FString::Printf(TEXT(" (off by %.1f deg, body is %.1f deg wide)"),
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosToAim, -1.0f, 1.0f))), FMath::RadiansToDegrees(AngularRadius)));
		return;
	}
	const FVector Velocity = Target->GetVelocity();
	const FVector Across = Velocity - ToAim * FVector::DotProduct(Velocity, ToAim);
	const float Drift = (Across.Size() / Distance) * FlightTime;
	const float HitChance = FMath::Clamp(1.0f - Drift / FMath::Max(AngularRadius, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	if (HitChance < MinProjectileHitChance)
	{
		TurretGate::Report(this, ViceIndex, TurretGate::LowChance,
			*FString::Printf(TEXT(" (%.0f%% < %.0f%%, flight %.2f s, across %.0f cm/s)"), HitChance * 100.0f, MinProjectileHitChance * 100.0f, FlightTime, Across.Size()));
		return;
	}
	const float Blast = ExplosionRadiusOf(Weapon);
	if (Blast > 0.0f && Distance < Blast * 1.25f)
	{
		TurretGate::Report(this, ViceIndex, TurretGate::BlastTooClose,
			*FString::Printf(TEXT(" (%.0f cm, blast %.0f)"), Distance, Blast));
		return;
	}
	if (!HasLineOfSightTo(Muzzle, Target, AimPoint))
	{
		TurretGate::Report(this, ViceIndex, TurretGate::NoSight);
		return;
	}
	TurretGate::Report(this, ViceIndex, TurretGate::Fired,
		*FString::Printf(TEXT(" projectile at %s, %.0f cm, lead %.2f s, chance %.0f%%, %d round(s) left"),
			*GetNameSafe(Target), Distance, FlightTime, HitChance * 100.0f, ViceRounds[ViceIndex] - 1));
	FireVice(ViceIndex, AimPoint, Now);
}

void ATurretBuildable::FireVice(int32 ViceIndex, const FVector& AimPoint, float Now)
{
	AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	if (!Weapon)
	{
		return;
	}
	// The owner answers for the shot: the kill tally reads the instigator. Set every shot rather
	// than once, because the owner's pawn is not the same actor after a death.
	Weapon->SetInstigator(GetOwnerPawn());
	Weapon->FireMounted(AimPoint);

	if (CVarTurretInfiniteAmmo.GetValueOnGameThread() == 0)
	{
		ViceRounds[ViceIndex] = FMath::Max(0, ViceRounds[ViceIndex] - 1);
	}
	Vices[ViceIndex].NextShotTime = Now + FMath::Max(0.05f, Weapon->GetActualRefireRate());
	if (ViceRounds[ViceIndex] <= 0 && ViceReserve[ViceIndex] > 0)
	{
		StartReload(ViceIndex, Now);
	}
	OnBuildableChanged.Broadcast(this);
}

// ==================== Debug ====================

void ATurretBuildable::DrawDebug(float Now) const
{
	const UWorld* const World = GetWorld();
	for (int32 Index = 0; Index < GetUnlockedViceCount(); ++Index)
	{
		const FVector Muzzle = MuzzleLocationOf(Index);
		const FVector Direction = BarrelDirectionOf(Index);
		const bool bLoaded = GetViceWeapon(Index) != nullptr;
		AActor* OnActor = nullptr;
		const bool bOn = bLoaded && IsBarrelOnHostile(Index, OnActor);
		const float Length = bLoaded ? Vices[Index].Range : 200.0f;
		DrawDebugLine(World, Muzzle, Muzzle + Direction * Length, bOn ? FColor::Green : (bLoaded ? FColor::Yellow : FColor::Silver), false, -1.0f, 0, bOn ? 2.0f : 1.0f);
		if (bLoaded)
		{
			DrawDebugCircle(World, GetActorLocation() + FVector(0.0f, 0.0f, 5.0f), Vices[Index].Range, 48, FColor::Cyan, false, -1.0f, 0, 1.0f, FVector::ForwardVector, FVector::RightVector, false);
			DrawDebugString(World, Muzzle + FVector(0.0f, 0.0f, 20.0f),
				FString::Printf(TEXT("%d: %d+%d %s"), Index, ViceRounds[Index], ViceReserve[Index],
					Vices[Index].ReloadEndTime >= 0.0f ? TEXT("reloading") : TEXT("")),
				nullptr, FColor::White, 0.0f, true);
		}
	}
	if (IsValid(CurrentTarget))
	{
		DrawDebugLine(World, EyeLocation(), PolarityAim::ResolveAimPointExact(CurrentTarget), FColor::Red, false, -1.0f, 0, 1.0f);
	}
}

// ==================== Console ====================

namespace TurretDebug
{
	/** The turret the local player is looking at, within FeedReachCm, else their nearest owned one. */
	static ATurretBuildable* FindTurretFor(AShooterCharacter* Character)
	{
		if (!Character)
		{
			return nullptr;
		}
		FVector Start, End;
		Character->GetAimRay(400.0f, Start, End);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretFeed), false);
		Params.AddIgnoredActor(Character);
		FHitResult Hit;
		if (Character->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			if (ATurretBuildable* const Aimed = Cast<ATurretBuildable>(Hit.GetActor()))
			{
				return Aimed;
			}
		}
		AShooterPlayerState* const State = Character->GetPlayerState<AShooterPlayerState>();
		if (!State)
		{
			return nullptr;
		}
		ATurretBuildable* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (ABuildableActor* const Buildable : State->GetOwnedBuildables())
		{
			ATurretBuildable* const Turret = Cast<ATurretBuildable>(Buildable);
			if (!Turret || Turret->IsDestroyed())
			{
				continue;
			}
			const float Dist = FVector::DistSquared(Turret->GetActorLocation(), Character->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Turret;
			}
		}
		return Best;
	}

	static AShooterCharacter* LocalAuthoritativeCharacter(UWorld* World)
	{
		APlayerController* const PC = CoopPlayers::GetLocalController(World);
		AShooterCharacter* const Character = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		if (!Character || !Character->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] turret commands only work on the host or in standalone"));
			return nullptr;
		}
		return Character;
	}

	static void CmdFeed(const TArray<FString>& Args, UWorld* World)
	{
		AShooterCharacter* const Character = LocalAuthoritativeCharacter(World);
		ATurretBuildable* const Turret = Character ? FindTurretFor(Character) : nullptr;
		if (!Turret)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] no turret under the aim and none owned"));
			return;
		}
		Turret->AcceptWeaponFrom(Character, -1);
	}

	static void CmdStatus(const TArray<FString>& Args, UWorld* World)
	{
		AShooterCharacter* const Character = LocalAuthoritativeCharacter(World);
		ATurretBuildable* const Turret = Character ? FindTurretFor(Character) : nullptr;
		if (!Turret)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] no turret under the aim and none owned"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s level %d, %d vice(s), target %s, turn %.0f deg/s"),
			*Turret->GetName(), Turret->GetBuildLevel(), Turret->GetUnlockedViceCount(),
			*GetNameSafe(Turret->GetCurrentTarget()), Turret->TurnRateDegPerSec());
		if (Turret->HasArticulatedHead())
		{
			UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG]   head: yaw %.1f, pitch %.1f (limit %.0f), jaw %.2f cm"),
				Turret->GetHeadYaw(), Turret->GetHeadPitch(), Turret->MaxPitchDegrees, Turret->GetJawOffsetCm());
		}
		for (int32 Index = 0; Index < ATurretBuildable::MaxVices; ++Index)
		{
			const AShooterWeapon* const Weapon = Turret->GetViceWeapon(Index);
			UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG]   vice %d: %s, %d loaded, %d reserve, range %.0f%s"),
				Index, Weapon ? *Weapon->GetClass()->GetName() : TEXT("empty"),
				Turret->GetViceRounds(Index), Turret->GetViceReserve(Index), Turret->GetViceRange(Index),
				Index < Turret->GetUnlockedViceCount() ? TEXT("") : TEXT(" (locked)"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdTurretFeed(
	TEXT("polarity.turret.feed"),
	TEXT("Put the local player's held gun into the turret under the aim (or the nearest owned one). Host or standalone only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TurretDebug::CmdFeed)
);

static FAutoConsoleCommandWithWorldAndArgs CmdTurretStatus(
	TEXT("polarity.turret.status"),
	TEXT("Log the vices of the turret under the aim (or the nearest owned one)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TurretDebug::CmdStatus)
);
