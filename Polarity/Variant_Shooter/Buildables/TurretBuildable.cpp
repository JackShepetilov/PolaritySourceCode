// TurretBuildable.cpp

#include "TurretBuildable.h"

#include "AI/AimPoints.h"
#include "AI/PolarityTeams.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Feedback/HitFeedbackSet.h"
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

void ATurretBuildable::BeginPlay()
{
	Super::BeginPlay();

	RestRelativeRotations.SetNum(MaxVices);
	for (int32 Index = 0; Index < MaxVices; ++Index)
	{
		USceneComponent* const Mount = ViceMounts.IsValidIndex(Index) ? ViceMounts[Index] : nullptr;
		RestRelativeRotations[Index] = Mount ? Mount->GetRelativeRotation() : FRotator::ZeroRotator;
		Vices[Index].BarrelRotation = Mount
			? Mount->GetComponentQuat().RotateVector(BarrelLocalDirection.GetSafeNormal()).Rotation()
			: GetActorRotation();
	}

	SyncNetOwner();
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

int32 ATurretBuildable::ReserveCapacityOf(int32 ViceIndex) const
{
	const AShooterWeapon* const Weapon = GetViceWeapon(ViceIndex);
	return Weapon ? FMath::Max(0, ReserveMagazines) * FMath::Max(1, Weapon->GetMagazineSize()) : 0;
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

	int32 ViceIndex = INDEX_NONE;
	if (!FindViceFor(Held->GetClass(), ViceIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s refused %s: no free vice for it (level %d, heavy %d)"),
			*GetName(), *Held->GetClass()->GetName(), GetBuildLevel(), IsRocketClass(Held->GetClass()) ? 1 : 0);
		return false;
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
	ViceReserve[ViceIndex] = FMath::Min(Reserve, ReserveCapacityOf(ViceIndex));

	UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s took %s from %s into vice %d: %d loaded, %d reserve (cap %d), range %.0f cm, %s, refire %.2f s, drop %s"),
		*GetName(), *WeaponClass->GetName(), *Donor->GetName(), ViceIndex, ViceRounds[ViceIndex], ViceReserve[ViceIndex],
		ReserveCapacityOf(ViceIndex), Vice.Range, Mounted->IsHitscan() ? TEXT("hitscan") : TEXT("projectile"),
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

bool ATurretBuildable::OnWrenchHitExtra(AShooterPlayerState* Hitter, int32& RemainingBudget)
{
	if (!Hitter || MetalPerRound <= 0)
	{
		return false;
	}
	// TF2's restock: one hit buys up to RoundsPerWrenchHit rounds at MetalPerRound each, as far as
	// the hitter's metal and the vices' room go. Into the reserve; a vice that ran dry starts its
	// reload right away, so the first hit after silence is also the one that brings the gun back.
	int32 Bought = 0;
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < GetUnlockedViceCount() && Bought < RoundsPerWrenchHit; ++Index)
	{
		if (!GetViceWeapon(Index))
		{
			continue;
		}
		// Room counts the magazine too: a vice with a full reserve and a half-spent magazine can still
		// take rounds, and the reload moves them down when the magazine runs dry.
		const AShooterWeapon* const ViceWeapon = GetViceWeapon(Index);
		const int32 Magazine = ViceWeapon ? FMath::Max(1, ViceWeapon->GetMagazineSize()) : 0;
		const int32 Room = ReserveCapacityOf(Index) + Magazine - (ViceRounds[Index] + ViceReserve[Index]);
		const int32 Affordable = RemainingBudget / MetalPerRound;
		const int32 Rounds = FMath::Min3(RoundsPerWrenchHit - Bought, Room, Affordable);
		if (Rounds <= 0)
		{
			continue;
		}
		if (!Hitter->TrySpendMetal(Rounds * MetalPerRound))
		{
			continue;
		}
		ViceReserve[Index] += Rounds;
		RemainingBudget -= Rounds * MetalPerRound;
		Bought += Rounds;
		if (ViceRounds[Index] <= 0 && Vices[Index].ReloadEndTime < 0.0f)
		{
			StartReload(Index, Now);
		}
	}
	if (Bought > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TURRET_DEBUG] %s restocked %d round(s) for %d metal by %s"),
			*GetName(), Bought, Bought * MetalPerRound, *Hitter->GetPlayerName());
		OnBuildableChanged.Broadcast(this);
	}
	return Bought > 0;
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

	ViceRounds[ViceIndex] = FMath::Max(0, ViceRounds[ViceIndex] - 1);
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
