// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeapon.h"
#include "Coop/CoopPlayers.h"
#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "Variant_Shooter/AI/NPCRiotShieldComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SphereComponent.h"
#include "EMFProjectile.h"
#include "ProjectilePoolSubsystem.h"
#include "ShooterWeaponHolder.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/SniperTurretNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "TutorialSubsystem.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "EMFPhysicsProp.h"
#include "Foliage/FoliageConversionLibrary.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "EnemyBeamBoltSubsystem.h"
#include "VFX/VFXVariantSequenceSubsystem.h"

void AShooterWeapon::PlayFireEffectsLocally()
{
	SpawnMuzzleFlashEffect();
	PlayFireSound();

	// The gun's own moving parts. Here rather than in Fire() because this function is what every
	// machine runs -- the shooter directly and everybody else through Multicast_PlayFireEffects --
	// so the action of the weapon is seen by the people watching it too, not only by its owner.
	PlayWeaponMeshAnimation(WeaponMeshFireAnimation);
}

void AShooterWeapon::PlayWeaponMeshAnimation(UAnimationAsset* Animation)
{
	if (!Animation)
	{
		return;
	}

	USkeletalMeshComponent* Meshes[] = { FirstPersonMesh, ThirdPersonMesh };
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->GetSkeletalMeshAsset())
		{
			continue;
		}

		// A weapon that runs an anim blueprint of its own keeps it: play the montage through the
		// instance so the graph can blend it and its notifies still fire. PlayAnimation would throw
		// the graph away for a single-node player and the weapon would freeze in that pose.
		if (UAnimMontage* AsMontage = Cast<UAnimMontage>(Animation))
		{
			if (UAnimInstance* MeshAnimInstance = Mesh->GetAnimInstance())
			{
				MeshAnimInstance->Montage_Play(AsMontage);
				continue;
			}
		}

		// No graph, or a plain sequence: play it straight on the component. This is the usual case
		// for a weapon mesh, which has nothing else to animate it.
		Mesh->PlayAnimation(Animation, /*bLooping*/ false);
	}
}

void AShooterWeapon::ResolveADSAnchorAttachment()
{
	if (!ADSCameraComponent || !FirstPersonMesh)
	{
		return;
	}

	const FAttachmentTransformRules Rules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;

	// --- 1. A sight attachment that carries its own eye point ---
	// The best of the three by a distance, and the only one authored FOR aiming: the socket sits
	// behind the glass, oriented down the sight line, and it travels with the scope when the scope
	// is swapped, so nothing needs re-tuning per attachment. This is the Low Poly Shooter Pack
	// convention (SOCKET_Aim on SM_*_Scope_Default and on every SM_ATT_Scope_*), and iron sights
	// there are simply "scope number zero" rather than a separate case.
	//
	// The whole subtree is searched, not just direct children, so a sight mounted on a rail that is
	// itself mounted on the weapon still counts.
	if (!SightAimSocketName.IsNone())
	{
		// Not "Children": AActor already has a member by that name and warnings are errors here.
		TArray<USceneComponent*> AttachedChildren;
		FirstPersonMesh->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			if (!Child || Child == ADSCameraComponent)
			{
				continue;
			}

			if (Child->DoesSocketExist(SightAimSocketName))
			{
				ADSCameraComponent->AttachToComponent(Child, Rules, SightAimSocketName);
				UE_LOG(LogTemp, Log, TEXT("[ADS] %s: anchor on sight attachment '%s', socket '%s'."),
					*GetName(), *Child->GetName(), *SightAimSocketName.ToString());
				return;
			}
		}
	}

	// --- 2. A socket on the weapon mesh itself ---
	// ADSSocketName first: that one is meant to BE the eye point, so if it exists it is as good as
	// case 1. ScopeMountSocketName is the consolation prize and it is worth being clear about why:
	// a mount socket sits on the rail, below the sight line and oriented to the rail rather than
	// down the barrel. It puts the anchor in roughly the right place and almost certainly the wrong
	// rotation, so expect to need SightRotationOffset, or to turn bAlignSightRotation off, on any
	// weapon that lands here.
	const FName Candidates[] = { ADSSocketName, ScopeMountSocketName };
	for (const FName& SocketName : Candidates)
	{
		if (SocketName.IsNone() || !FirstPersonMesh->DoesSocketExist(SocketName))
		{
			continue;
		}

		ADSCameraComponent->AttachToComponent(FirstPersonMesh, Rules, SocketName);

		if (SocketName == ScopeMountSocketName)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ADS] %s: no eye-point socket found, falling back to the "
				"MOUNT socket '%s' on the weapon mesh. That is a rail position, not a sight line: "
				"check the aim with a trace and expect to need SightRotationOffset."),
				*GetName(), *SocketName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ADS] %s: anchor on weapon mesh socket '%s'."),
				*GetName(), *SocketName.ToString());
		}
		return;
	}

	// --- 3. Nothing authored: keep whatever the Blueprint set ---
	// Deliberately no attach call. The ADS camera keeps the parent and relative transform it was
	// given in the Blueprint, which for a hand-placed anchor is exactly what the designer meant.
	UE_LOG(LogTemp, Log, TEXT("[ADS] %s: no sight socket anywhere, keeping the anchor where the "
		"Blueprint placed it (parent '%s')."),
		*GetName(), *GetNameSafe(ADSCameraComponent->GetAttachParent()));
}

void AShooterWeapon::PropagateRenderVisibilityToChildren()
{
	// A sight, suppressor or laser added in the Blueprint as a child of one of the weapon meshes
	// keeps the default render visibility, which means the first person pass and the world pass
	// disagree about it: bolted to the FP gun it would still be drawn with the WORLD field of view
	// and world depth, so it swims against the weapon it is attached to and clips into the scene.
	// Nothing attached under a weapon mesh ever wants to differ from that mesh here, so it is
	// inherited rather than left as a checkbox to remember on every new attachment.
	auto Inherit = [](USkeletalMeshComponent* Parent)
	{
		if (!Parent)
		{
			return;
		}

		TArray<USceneComponent*> AttachedChildren;
		Parent->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Child);
			if (!Prim || Prim == Parent)
			{
				continue;
			}

			// Type first: SetFirstPersonPrimitiveType(WorldSpaceRepresentation) forces bOwnerNoSee
			// on internally, so setting the see flags afterwards is what makes ours the last word.
			Prim->SetFirstPersonPrimitiveType(Parent->FirstPersonPrimitiveType);
			Prim->SetOnlyOwnerSee(Parent->bOnlyOwnerSee != 0);
			Prim->SetOwnerNoSee(Parent->bOwnerNoSee != 0);
		}
	};

	Inherit(FirstPersonMesh);
	Inherit(ThirdPersonMesh);
}

void AShooterWeapon::PlayReloadEffectsLocally()
{
	// Both meshes: PlayWeaponMeshAnimation already covers first and third person, so the machine
	// that runs this shows the reload on whichever copy of the weapon it can see.
	PlayWeaponMeshAnimation(WeaponMeshReloadAnimation);

	if (ReloadSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetActorLocation());
	}
}

void AShooterWeapon::Multicast_PlayReloadEffects_Implementation()
{
	// Whoever started the reload already played these the moment they started it. For an NPC that
	// is the server, which is where its AI lives.
	const bool bIsReloader = PawnOwner && PawnOwner->IsLocallyControlled();
	if (!bIsReloader)
	{
		PlayReloadEffectsLocally();
	}
}

void AShooterWeapon::Multicast_PlayFireEffects_Implementation()
{
	// The shooter already played these locally the moment they pulled the trigger.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (!bIsShooter)
	{
		PlayFireEffectsLocally();
	}
}

float AShooterWeapon::GetMaxReportedSingleHitDamage() const
{
	// Projectile weapons report through the projectile, which the server owns, so the hitscan number
	// is the only one a client ever hands over. A weapon with no hitscan damage configured still
	// needs a non-zero ceiling or every reported hit would clamp to nothing.
	// No class-passive term here on purpose. The Sniper's passive does not scale this number at all:
	// it deals its OWN damage, decided and applied on the server, and never travels in a client's
	// report. @see AShooterWeapon::ApplyPassivePierceDamage.
	const float BaseDamage = HitscanDamage > 0.0f ? HitscanDamage : 1.0f;
	return BaseDamage * FMath::Max(HeadshotMultiplier, 1.0f) * FMath::Max(MaxReportedDamageMultiplier, 1.0f);
}

float AShooterWeapon::PredictDamageAgainst(AActor* Target) const
{
	if (!IsValid(Target) || HitscanDamage <= 0.0f)
	{
		return 0.0f;
	}

	// The same product the bolt path assembles, minus the two factors that only a real shot can
	// know: whether it lands on a head, and how much energy it has left after passing through
	// anything. So this is a body shot at full energy -- the honest baseline, and the one the player
	// can compare two of against each other, which is the whole point of showing it.
	const float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

	float ZFactorMult = 1.0f;
	if (bUseZFactor && PawnOwner)
	{
		ZFactorMult = CalculateZFactorMultiplier(PawnOwner->GetActorLocation().Z, Target->GetActorLocation().Z);
	}

	const float TagMult = GetTagDamageMultiplier(Target);

	float UpgradeMult = 1.0f;
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(Target);
		}
	}

	float Total = HitscanDamage * HeatMult * ZFactorMult * TagMult * UpgradeMult;

	// The shield gate IS applied, unlike an earlier version of this: the readout answers "what will
	// this shot do to that enemy", and a finisher weapon against an intact shield does nothing at
	// all with its own damage. Saying otherwise would be a lie the player then has to unlearn.
	if (bRequiresBrokenShieldToDamage && !IsTargetShieldDown(Target))
	{
		Total = 0.0f;
	}

	// The class passive's own damage is added on top, and it is NOT gated: passing through a shield
	// that is still up is the entire point of it. @see UAbilityHandler::GetBonusPierceDamage.
	if (PawnOwner)
	{
		if (const UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Total += Abilities->GetPredictedPierceDamage(Target);
		}
	}

	return Total;
}

bool AShooterWeapon::IsTargetShieldDown(AActor* Target) const
{
	if (!IsValid(Target))
	{
		return false;
	}

	// Same components, same order and same ceilings as ApplyHitscanIonization uses to FILL the
	// meter, so the thing that charges a target and the gate that opens when it is full can never
	// be reading two different numbers.
	if (const UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		return TargetModifier->IsAtMaxCharge();
	}

	if (const AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		return Prop->IsAtMaxCharge();
	}

	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		const float CurrentCharge = TargetField->GetSourceDescription().PointChargeParams.Charge;
		return IsIonizationCapReached(CurrentCharge, MaxIonizationCharge);
	}

	// Carries no charge at all, so it has no shield to be down. An ordinary target, hurt normally.
	return true;
}

float AShooterWeapon::ApplyDamageToTarget(AActor* HitActor, float FinalDamage, const FDamageEvent& DamageEvent)
{
	if (!IsValid(HitActor) || FinalDamage <= 0.0f)
	{
		return 0.0f;
	}

	// A finisher weapon: nothing it hits loses health from the WEAPON until that target's shield is
	// down. The hit still happened -- ionization, knockback and the hit marker all run in the
	// callers, which is what makes charging a target up feel like progress rather than like missing.
	//
	// Read here and acted on twice below, because a class passive may have damage of its own that
	// goes past this gate, and that damage still has to be applied and still has to be reported.
	const bool bShieldGated = IsShieldGateBlocking(HitActor);

	// A player firing from a client cannot write health itself: AShooterCharacter::TakeDamage is
	// authority-only now, so the direct call below would silently do nothing on that machine while
	// still looking like a hit locally. Route it through the character, which reports it upstream.
	// NPC weapons are unaffected: AI runs on the server, where this branch is never taken.
	if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		if (!OwnerCharacter->HasAuthority())
		{
			// Reported even when the gate is closed, which it did NOT used to be. The report is what
			// tells the server a hit landed at all, and the server has its own reason to care about
			// one that the weapon cannot pay for: the shield-piercing half of a class passive. The
			// server re-checks the gate itself, so nothing is granted by reporting.
			OwnerCharacter->DealDamage(HitActor, FinalDamage, DamageEvent.DamageTypeClass, this);

			// Report the requested damage so local hit feedback still fires immediately. Kill
			// feedback will not, because the client cannot know yet: it learns the outcome from
			// replicated health a round trip later.
			return bShieldGated ? 0.0f : FinalDamage;
		}
	}

	// Authority. The passive's own damage first and unconditionally: it is the half that is supposed
	// to reach health through a shield that is still up.
	const float PierceDamage = ApplyPassivePierceDamage(HitActor);

	if (bShieldGated)
	{
		return PierceDamage;
	}

	return HitActor->TakeDamage(FinalDamage, DamageEvent,
		PawnOwner ? PawnOwner->GetController() : nullptr, this) + PierceDamage;
}

bool AShooterWeapon::IsShieldGateBlocking(AActor* HitActor) const
{
	return bRequiresBrokenShieldToDamage && !IsTargetShieldDown(HitActor);
}

float AShooterWeapon::ApplyPassivePierceDamage(AActor* HitActor)
{
	// Authority only, and said out loud: this writes health, and a client that ran it would change
	// nothing anywhere else while looking to itself like it had.
	if (!IsValid(HitActor) || !PawnOwner || !PawnOwner->HasAuthority())
	{
		return 0.0f;
	}

	UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>();
	if (!Abilities)
	{
		return 0.0f;
	}

	const float Amount = Abilities->GetPierceDamageForShot(HitActor);
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	// Deliberately NOT routed through the gate above, and deliberately its own damage event: this is
	// the passive's damage, not the weapon's, and the whole mechanic is that it does not wait for
	// the shield to come off. AShooterNPC::TakeDamage subtracts from health directly and has no
	// shield term of its own, so this arrives where it is meant to.
	FPointDamageEvent PierceEvent;
	PierceEvent.DamageTypeClass = HitscanDamageType ? HitscanDamageType : TSubclassOf<UDamageType>(UDamageType::StaticClass());

	return HitActor->TakeDamage(Amount, PierceEvent, PawnOwner->GetController(), this);
}

namespace
{
	/** Check if actor is dead after TakeDamage (synchronous check via HP/bIsDead flags) */
	bool IsActorDeadAfterDamage(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		// ShooterNPC covers ShooterNPC, FlyingDrone, MeleeNPC, BossCharacter
		if (AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			return NPC->IsDead();
		}

		// Player character
		if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(Actor))
		{
			return ShooterChar->IsDead();
		}

		// Training dummies
		if (AShooterDummy* Dummy = Cast<AShooterDummy>(Actor))
		{
			return Dummy->IsDead();
		}

		// Physics props
		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor))
		{
			return Prop->IsDead();
		}

		// Fallback for unknown actor types
		return Actor->IsPendingKillPending();
	}

	/** Above this launch speed a grounded character is knocked off its feet on purpose.
	 *  Below it an ordinary bullet must not lift anyone. @see ApplyHitscanKnockback. */
	constexpr float HitscanGroundedLaunchThreshold = 400.0f;

	/**
	 * Single entrance for hitscan knockback on characters.
	 *
	 * LaunchCharacter ALWAYS forces MOVE_Falling (CharacterMovementComponent::HandlePendingLaunch),
	 * even for a purely horizontal impulse. For a character standing on the ground that reads to the
	 * anim graph as "airborne", so every bullet made the NPC hop instead of playing its flinch.
	 * Rule: a grounded character only gets launched by a deliberately strong impulse; ordinary
	 * bullet forces are dropped and the hit shows up as the flinch reaction alone. A character
	 * already in the air is launched as before — it is falling anyway, nothing to break.
	 */
	void ApplyHitscanKnockback(ACharacter* HitCharacter, const FVector& LaunchVelocity, bool bIonizerWeapon)
	{
		if (!IsValid(HitCharacter))
		{
			return;
		}

		// Stationary turret never takes hit impulses: with its GravityScale=0 the forced
		// MOVE_Falling would push it into permanent flight.
		if (Cast<ASniperTurretNPC>(HitCharacter))
		{
			return;
		}

		// The boss opts out of the ionizer weapon's knockback (it still takes damage + ionization).
		if (bIonizerWeapon && Cast<ABossCharacter>(HitCharacter))
		{
			return;
		}

		const UCharacterMovementComponent* Movement = HitCharacter->GetCharacterMovement();
		if (Movement && Movement->IsMovingOnGround() && LaunchVelocity.Size() < HitscanGroundedLaunchThreshold)
		{
			return;
		}

		HitCharacter->LaunchCharacter(LaunchVelocity, false, false);
	}
}

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Teammates have to see what you are holding. Without this the weapon actor simply does not
	// exist on anyone else's machine: the third-person mesh has nothing to attach to, so a remote
	// player stands there in a pistol pose with empty hands. Movement is not replicated because the
	// weapon is attached to the character and rides along with it.
	bReplicates = true;
	SetReplicatingMovement(false);

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// Always tick pose & refresh bones every frame, and disable Update Rate Optimizations.
	// Default settings can skip bone updates when the mesh isn't on screen / not playing montage,
	// which makes child components (sights, suppressors, lasers attached to sockets) lag a frame
	// behind the weapon's animated pose.
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonMesh->bEnableUpdateRateOptimizations = false;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;

	// Create ADS camera component on the first person mesh
	// It will be attached to the Sight socket in BeginPlay after meshes are set up
	ADSCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("ADS Camera"));
	ADSCameraComponent->SetupAttachment(FirstPersonMesh);
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// A weapon belongs to whoever is holding it, and every spawn path sets that owner. One thing
	// does not: a weapon actor dragged straight into a level. It has nobody to attach its meshes
	// to, nobody to fire it, and no HUD to update, so the only sane thing it can do is sit there
	// and say so instead of taking the editor down on the null owner.
	AActor* OwningActor = GetOwner();
	if (!OwningActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON] %s has no owner. A weapon has to be given to a character, "
			"not placed in the level: use a pickup for that. This one will do nothing."), *GetName());
		return;
	}

	// subscribe to the owner's destroyed delegate
	OwningActor->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(OwningActor);
	PawnOwner = Cast<APawn>(OwningActor);

	// Cache movement component for Heat System speed calculations
	if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
	{
		CachedMovementComponent = CharOwner->GetCharacterMovement();
	}

	// NPC optimization: hide first person mesh for non-player owners
	if (!PawnOwner || !PawnOwner->IsPlayerControlled())
	{
		if (FirstPersonMesh)
		{
			FirstPersonMesh->SetVisibility(false);
			FirstPersonMesh->SetComponentTickEnabled(false);
		}
	}

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// attach the meshes to the owner. An owner that is not a weapon holder at all (a prop, a
	// spawner) fails the cast above and would crash here the same way the null one did.
	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON] %s is owned by %s, which is not a weapon holder. "
			"Nothing to attach to."), *GetName(), *GetNameSafe(OwningActor));
		return;
	}

	WeaponOwner->AttachWeaponMeshes(this);

	ResolveADSAnchorAttachment();
	PropagateRenderVisibilityToChildren();

	// Force every component attached to the weapon's meshes (sights, suppressors, lasers,
	// rails, etc.) to tick AFTER the mesh's animation has been evaluated AND in a later tick
	// group, so they cannot read stale bone/socket transforms and lag a frame behind the weapon.
	auto ForceLateTickOnChildren = [](USkeletalMeshComponent* Parent)
	{
		if (!Parent) return;

		TArray<USceneComponent*> AttachedChildren;
		Parent->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			if (!Child || Child == Parent) continue;

			// 1. Hard prerequisite — child can never tick before the parent.
			Child->AddTickPrerequisiteComponent(Parent);

			// 2. Push tick into TG_PostPhysics so it runs after PrePhysics anim work AND
			//    any DuringPhysics simulation. If the component doesn't tick at all this is
			//    harmless; if it does (animated, particle, dynamic), it picks up the latest pose.
			if (Child->PrimaryComponentTick.bCanEverTick)
			{
				Child->PrimaryComponentTick.TickGroup = TG_PostPhysics;
			}
		}
	};
	ForceLateTickOnChildren(FirstPersonMesh);
	ForceLateTickOnChildren(ThirdPersonMesh);

	// === Diagnostic dump of attachment transforms after equip ===
	// Filter Output Log by [ATTACH_DEBUG] to read it.
	if (FirstPersonMesh)
	{
		const FTransform MeshWorld = FirstPersonMesh->GetComponentTransform();
		const FTransform MeshRelative = FirstPersonMesh->GetRelativeTransform();
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG] === %s FirstPersonMesh ==="), *GetName());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   AttachSocket=%s"), *FirstPersonMesh->GetAttachSocketName().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   Rel  T=%s R=%s S=%s"),
			*MeshRelative.GetLocation().ToString(),
			*MeshRelative.GetRotation().Rotator().ToString(),
			*MeshRelative.GetScale3D().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   World T=%s R=%s S=%s"),
			*MeshWorld.GetLocation().ToString(),
			*MeshWorld.GetRotation().Rotator().ToString(),
			*MeshWorld.GetScale3D().ToString());

		TArray<USceneComponent*> AllChildren;
		FirstPersonMesh->GetChildrenComponents(true, AllChildren);
		for (USceneComponent* Child : AllChildren)
		{
			if (!Child) continue;
			const FTransform Rel = Child->GetRelativeTransform();
			const FTransform World = Child->GetComponentTransform();
			UE_LOG(LogTemp, Warning,
				TEXT("[ATTACH_DEBUG] Child=%s class=%s ParentSocket=%s | Rel T=%s R=%s S=%s | World T=%s"),
				*Child->GetName(),
				*Child->GetClass()->GetName(),
				*Child->GetAttachSocketName().ToString(),
				*Rel.GetLocation().ToString(),
				*Rel.GetRotation().Rotator().ToString(),
				*Rel.GetScale3D().ToString(),
				*World.GetLocation().ToString());
		}
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);

	// and the reload timer, which would otherwise fire into a destroyed weapon
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
}

void AShooterWeapon::PushLeftHandIK(UAnimInstance* AnimInstance, const FTransform& Transform, float Alpha)
{
	if (!AnimInstance)
	{
		return;
	}

	static const FName LeftHandIKTransformName(TEXT("LeftHandIKTransform"));
	if (FProperty* TransformProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKTransformName))
	{
		FStructProperty* StructProp = CastField<FStructProperty>(TransformProperty);
		if (StructProp && StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			if (void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(AnimInstance))
			{
				*static_cast<FTransform*>(ValuePtr) = Transform;
			}
		}
	}

	static const FName LeftHandIKAlphaName(TEXT("LeftHandIKAlpha"));
	FProperty* AlphaProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKAlphaName);
	if (!AlphaProperty)
	{
		return;
	}

	// Blueprint "float" is a double in UE5, but hand-authored C++ AnimBPs may still use float.
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(AlphaProperty))
	{
		if (void* ValuePtr = FloatProp->ContainerPtrToValuePtr<void>(AnimInstance))
		{
			*static_cast<float*>(ValuePtr) = Alpha;
		}
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(AlphaProperty))
	{
		if (void* ValuePtr = DoubleProp->ContainerPtrToValuePtr<void>(AnimInstance))
		{
			*static_cast<double*>(ValuePtr) = static_cast<double>(Alpha);
		}
	}
}

// ==================== Grip alignment ====================

const FName AShooterWeapon::OptionalGripSocketName(TEXT("OptionalGrip"));
const FName AShooterWeapon::ThirdPersonSocketSuffix(TEXT("_TP"));

FName AShooterWeapon::PickThirdPersonSocket(const USkeletalMeshComponent* WeaponMesh, const FName BaseSocket)
{
	if (!WeaponMesh || BaseSocket.IsNone())
	{
		return BaseSocket;
	}

	const FName ThirdPersonSocket(*(BaseSocket.ToString() + ThirdPersonSocketSuffix.ToString()));
	return WeaponMesh->DoesSocketExist(ThirdPersonSocket) ? ThirdPersonSocket : BaseSocket;
}

void AShooterWeapon::AlignMeshToGripSocket(USkeletalMeshComponent* WeaponMesh, const FName GripSocket)
{
	const FString OwnerNameForLog = WeaponMesh && WeaponMesh->GetOwner()
		? WeaponMesh->GetOwner()->GetName()
		: FString(TEXT("<no owner>"));

	if (!WeaponMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] AlignMeshToGripSocket: WeaponMesh is null — skipping"));
		return;
	}

	const FString MeshNameForLog = WeaponMesh->GetName();

	if (!WeaponMesh->DoesSocketExist(GripSocket))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] %s %s: grip socket '%s' NOT FOUND — alignment skipped"),
			*OwnerNameForLog, *MeshNameForLog, *GripSocket.ToString());
		return;
	}

	const FTransform SocketComponent = WeaponMesh->GetSocketTransform(GripSocket, RTS_Component);
	const FQuat    InverseRotation = SocketComponent.GetRotation().Inverse();
	const FVector  MeshScale       = WeaponMesh->GetRelativeScale3D();

	// Scaled socket offset in mesh-local axes, then unrotated into mesh-relative axes.
	const FVector ScaledSocketLocation = SocketComponent.GetLocation() * MeshScale;
	const FVector NewRelativeLocation  = -InverseRotation.RotateVector(ScaledSocketLocation);

	// === BEFORE state ===
	const FTransform MeshRelBefore = WeaponMesh->GetRelativeTransform();
	const FVector    OptionalGripWorldBefore = WeaponMesh->GetSocketLocation(GripSocket);

	// Where the hand socket lives in world (the attach parent + attach socket on it).
	FVector HandSocketWorld = FVector::ZeroVector;
	FString HandSocketLog = TEXT("<no parent>");
	if (USceneComponent* AttachParent = WeaponMesh->GetAttachParent())
	{
		const FName AttachSock = WeaponMesh->GetAttachSocketName();
		HandSocketWorld = AttachSock.IsNone() ? AttachParent->GetComponentLocation()
		                                     : AttachParent->GetSocketLocation(AttachSock);
		HandSocketLog = FString::Printf(TEXT("%s.%s"), *AttachParent->GetName(), *AttachSock.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] === %s %s (grip socket '%s') ==="),
		*OwnerNameForLog, *MeshNameForLog, *GripSocket.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Attached to: %s"), *HandSocketLog);
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Socket S (component space):  Loc=%s  Rot=%s  Scale=%s"),
		*SocketComponent.GetLocation().ToString(),
		*SocketComponent.GetRotation().Rotator().ToString(),
		*SocketComponent.GetScale3D().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Mesh BEFORE: RelLoc=%s  RelRot=%s  RelScale=%s"),
		*MeshRelBefore.GetLocation().ToString(),
		*MeshRelBefore.GetRotation().Rotator().ToString(),
		*MeshScale.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Computed:    NewRelLoc=%s  NewRelRot=%s"),
		*NewRelativeLocation.ToString(),
		*InverseRotation.Rotator().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Hand world = %s"), *HandSocketWorld.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   OptGrip world BEFORE = %s  (delta=%s)"),
		*OptionalGripWorldBefore.ToString(),
		*(OptionalGripWorldBefore - HandSocketWorld).ToString());

	// Apply location + rotation atomically; leave scale untouched (KeepRelative scale
	// from AttachmentRule preserved the BP-set scale and we don't want to clobber it).
	WeaponMesh->SetRelativeLocationAndRotation(NewRelativeLocation, InverseRotation);

	// === AFTER state — verify OptionalGrip actually lands at hand socket ===
	const FVector OptionalGripWorldAfter = WeaponMesh->GetSocketLocation(GripSocket);
	const FVector DeltaAfter = OptionalGripWorldAfter - HandSocketWorld;
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   OptGrip world AFTER  = %s  (delta=%s  len=%.4f)"),
		*OptionalGripWorldAfter.ToString(),
		*DeltaAfter.ToString(),
		DeltaAfter.Size());

	// Log every child's world position so we can see if scope/sight ended up where BP intended.
	TArray<USceneComponent*> Children;
	WeaponMesh->GetChildrenComponents(/*bIncludeAllDescendants*/ true, Children);
	for (USceneComponent* Child : Children)
	{
		if (!Child || Child == WeaponMesh) continue;
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Child=%s ParentSocket=%s  RelLoc=%s  WorldLoc=%s"),
			*Child->GetName(),
			*Child->GetAttachSocketName().ToString(),
			*Child->GetRelativeLocation().ToString(),
			*Child->GetComponentLocation().ToString());
	}
}

void AShooterWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update Heat System
	if (bUseHeatSystem)
	{
		UpdateHeat(DeltaTime);
	}

	UpdateSpread(DeltaTime);

	// The third person weapon is not steered from here any more. It used to have its world rotation
	// rebuilt from GetBaseAimRotation every frame, because the character's body faced its movement
	// direction and the gun in its hand therefore pointed anywhere but at the target. That was
	// papering over the real problem: the body was never turned. The character now yaws with the
	// camera (bUseControllerRotationYaw in AShooterCharacter) and pitches through the aim offset, so
	// the weapon points where the owner aims for the same reason a real one does, by being held by
	// someone facing that way.
}

// ==================== Spread ====================
//
// One number, read by the bullets and by the crosshair, so the ring on screen is the region the
// shot can land in rather than a decoration that happens to grow at the same time.

float AShooterWeapon::ResolveStateSpreadMultiplier() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(PawnOwner);
	if (!OwnerCharacter)
	{
		// Turrets, drones, anything that is not a character: no states to read, so the weapon
		// simply shoots at its base spread.
		return SpreadConfig.StillMultiplier;
	}

	const UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement();
	if (!Move)
	{
		return SpreadConfig.StillMultiplier;
	}

	// Exactly ONE state wins, resolved by priority. Multiplying them together would make
	// crouch-sprinting quieter than sprinting for no reason a player could read off the screen.
	float Multiplier = SpreadConfig.StillMultiplier;

	const UApexMovementComponent* Apex = Cast<UApexMovementComponent>(Move);
	const bool bSliding = Apex && Apex->IsSliding();
	const bool bSprinting = Apex && Apex->IsSprinting();

	if (Move->IsFalling())
	{
		// Airborne covers the jump, the fall and the wall-run: the feet are not planted.
		Multiplier = SpreadConfig.AirMultiplier;
	}
	else if (bSliding)
	{
		Multiplier = SpreadConfig.SlideMultiplier;
	}
	else if (bSprinting)
	{
		Multiplier = SpreadConfig.SprintMultiplier;
	}
	else if (Move->IsCrouching())
	{
		Multiplier = SpreadConfig.CrouchMultiplier;
	}
	else
	{
		// On foot: interpolate between standing still and walking by how fast the owner actually
		// is, so a nudge of the stick is not the full walking penalty.
		const FVector Velocity = OwnerCharacter->GetVelocity();
		const float Speed2D = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
		const float SpeedAlpha = FMath::Clamp(Speed2D / FMath::Max(1.0f, SpreadConfig.WalkFullSpeed), 0.0f, 1.0f);
		Multiplier = FMath::Lerp(SpreadConfig.StillMultiplier, SpreadConfig.WalkMultiplier, SpeedAlpha);
	}

	// Aiming down sights scales whatever came out, blended by the ADS alpha so the spread tightens
	// over the same time the sight comes up instead of snapping at the button press.
	if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(OwnerCharacter))
	{
		const float ADSAlpha = FMath::Clamp(ShooterOwner->GetADSAlpha(), 0.0f, 1.0f);
		Multiplier *= FMath::Lerp(1.0f, SpreadConfig.AdsMultiplier, ADSAlpha);
	}

	return FMath::Max(0.0f, Multiplier);
}

void AShooterWeapon::UpdateSpread(float DeltaTime)
{
	// The state part chases its target: standing still after a sprint has to settle, which is the
	// whole reason a player stops before shooting.
	const float TargetMultiplier = ResolveStateSpreadMultiplier();
	CurrentStateMultiplier = FMath::FInterpTo(CurrentStateMultiplier, TargetMultiplier, DeltaTime, SpreadConfig.StateInterpSpeed);

	// The firing bloom bleeds off, but only after a quiet moment: a delay of about one refire
	// interval keeps a held trigger from recovering between its own shots.
	if (CurrentBloomDegrees > 0.0f)
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		if (Now - TimeOfLastSpreadShot >= SpreadConfig.BloomRecoveryDelay)
		{
			CurrentBloomDegrees = FMath::Max(0.0f, CurrentBloomDegrees - SpreadConfig.BloomRecoveryRate * DeltaTime);
		}
	}
}

void AShooterWeapon::AddShotSpread()
{
	float PerShot = SpreadConfig.PerShotDegrees;

	if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
	{
		const float ADSAlpha = FMath::Clamp(ShooterOwner->GetADSAlpha(), 0.0f, 1.0f);
		PerShot *= FMath::Lerp(1.0f, SpreadConfig.AdsBloomMultiplier, ADSAlpha);
	}

	CurrentBloomDegrees = FMath::Min(CurrentBloomDegrees + PerShot, SpreadConfig.MaxBloomDegrees);

	const UWorld* World = GetWorld();
	TimeOfLastSpreadShot = World ? World->GetTimeSeconds() : 0.0f;
}

float AShooterWeapon::GetCurrentSpreadDegrees() const
{
	// An AI holding this weapon shoots at the plain base spread unless the weapon opts in: enemy
	// accuracy is tuned through the NPC's own aim variance, and moving it from here would be a
	// difficulty change wearing a crosshair feature's clothes.
	if (!SpreadConfig.bApplyToAIOwners && PawnOwner && !PawnOwner->IsPlayerControlled())
	{
		return AimVariance;
	}

	const float Total = AimVariance * CurrentStateMultiplier + CurrentBloomDegrees;
	return FMath::Clamp(Total, 0.0f, SpreadConfig.MaxSpreadDegrees);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);

	// Show first-equip tutorial slide (if configured and not yet completed)
	// Skip if owner has tutorial debug mode enabled
	if (!FirstEquipTutorialID.IsNone())
	{
		bool bSkip = false;
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			bSkip = ShooterOwner->bTutorialDebugMode;
		}

		if (!bSkip)
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
				{
					APlayerController* PC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
					TutorialSub->ShowSlide(FirstEquipTutorialID, FirstEquipSlideData, PC);
				}
			}
		}
	}
}

float AShooterWeapon::GetSwitchPlayRate(const UAnimMontage* Montage, float Duration)
{
	// No montage, or no duration asked for, means "play it as the animator made it". A duration of
	// zero must never read as "instant": that would make an unfilled field silently delete the
	// animation instead of leaving it alone.
	if (!Montage || Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float Length = Montage->GetPlayLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return Length / Duration;
}

float AShooterWeapon::GetHolsterLength() const
{
	if (!HolsterMontage)
	{
		return 0.0f;
	}

	const float Rate = GetHolsterPlayRate();
	return Rate > KINDA_SMALL_NUMBER ? HolsterMontage->GetPlayLength() / Rate : 0.0f;
}

float AShooterWeapon::GetDrawLength() const
{
	if (!DrawMontage)
	{
		return 0.0f;
	}

	const float Rate = GetDrawPlayRate();
	return Rate > KINDA_SMALL_NUMBER ? DrawMontage->GetPlayLength() / Rate : 0.0f;
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// a reload the player switched away from does not finish behind their back
	CancelReload();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;

	// ==================== Sprint-out gate ====================
	// Coming out of a sprint the weapon is still being raised, so the shot waits for the raise to
	// finish instead of going off from the sprint pose. It is deferred rather than dropped: hold
	// the trigger through the raise and it fires the moment the gate opens, release and StopFiring
	// clears the timer. Deliberately measured from when sprinting *ended*, which the movement
	// component stamps, and not from this call: the two differ when the player was already out of
	// the sprint before pulling the trigger.
	if (SprintToFireTime > 0.0f)
	{
		if (const APolarityCharacter* PolarityOwner = Cast<APolarityCharacter>(PawnOwner))
		{
			if (const UApexMovementComponent* Apex = PolarityOwner->GetApexMovement())
			{
				const float GateOpensAt = Apex->GetSprintEndTime() + SprintToFireTime;
				const float Remaining = GateOpensAt - GetWorld()->GetTimeSeconds();
				if (Remaining > 0.0f)
				{
					GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, Remaining, false);
					return;
				}
			}
		}
	}

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	const float CurrentRefireRate = GetCurrentRefireRate();

	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s] StartFiring: TimeSinceLastShot=%.3f, RefireRate=%.3f, bFullAuto=%d, Owner=%s"),
		*GetName(), TimeSinceLastShot, CurrentRefireRate, bFullAuto,
		PawnOwner ? *PawnOwner->GetName() : TEXT("NULL"));

	if (TimeSinceLastShot > CurrentRefireRate)
	{
		// fire the weapon right away
		UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   -> Firing immediately"), *GetName());
		Fire();

	}
	else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   -> Deferred (full auto): scheduling in %.3f sec"), *GetName(), TimeSinceLastShot);
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   -> SKIPPED: not full auto and refire rate not met!"), *GetName());
		}

	}
}

void AShooterWeapon::StopFiring()
{
	const bool bHadPendingRefire = GetWorld()->GetTimerManager().IsTimerActive(RefireTimer);
	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s] StopFiring: bIsFiring was %d, hadPendingRefire=%d"),
		*GetName(), bIsFiring, bHadPendingRefire);

	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

bool AShooterWeapon::OnSecondaryAction()
{
	return false;
}

void AShooterWeapon::OnSecondaryActionReleased()
{
}

void AShooterWeapon::FireOnce()
{
	// Single shot through the normal Fire() path (handles aim/ammo/charge/OnShotFired), then clear
	// the scheduled refire so the firing cadence is driven entirely by the animation notify.
	bIsFiring = true;
	Fire();
	StopFiring();
}

void AShooterWeapon::Fire()
{
	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s] Fire() called: bIsFiring=%d, bUseChargeFiring=%d, WeaponOwner=%d"),
		*GetName(), bIsFiring, bUseChargeFiring, WeaponOwner != nullptr);

	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   Fire() ABORTED: bIsFiring is false!"), *GetName());
		return;
	}

	// A weapon with a real magazine cannot fire while it is being filled, and an empty one starts
	// filling itself rather than clicking forever. bIsFiring is deliberately left alone: holding the
	// trigger through a reload should resume fire when it finishes, which FinishReload does.
	if (bUseReload)
	{
		if (bIsReloading)
		{
			return;
		}

		if (CurrentBullets <= 0)
		{
			// Empty. That is all this is: the trigger was pulled and there was nothing to fire.
			// Turning it into a reload is a separate decision the weapon only makes if it was told
			// to -- otherwise the gun clicks and stays up, and the player chooses when to reload.
			if (DryFireSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, DryFireSound, GetActorLocation());
			}

			if (bReloadOnEmptyTriggerPull)
			{
				StartReload();
			}

			return;
		}
	}

	// Limited-ammo guard: yanked weapons that ran dry should not fire phantom shots in the
	// brief window between magazine depletion and the deferred DropYankedWeaponIfAny tick.
	if (bHasLimitedAmmo && CurrentBullets <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: Fire() ABORTED — empty yanked weapon, awaiting discard"), *GetName());
		StopFiring();
		return;
	}

	// Check charge requirements if enabled
	float ChargeMultiplier = 1.0f;
	if (bUseChargeFiring)
	{
		if (!TryConsumeCharge(ChargeMultiplier))
		{
			// Not enough charge - play dry fire click sound
			if (DryFireSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, DryFireSound, GetActorLocation());
			}
			UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   Fire() ABORTED: charge requirements not met!"), *GetName());
			StopFiring();
			return;
		}
	}

	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   Fire() ABORTED: WeaponOwner is NULL!"), *GetName());
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   Fire() PROCEEDING: spawning effects, getting target..."), *GetName());

	// Muzzle flash and fire sound.
	//
	// Always play them here so the shooter sees and hears the shot at once, with no round trip.
	// Then tell everyone else: the effects used to be purely local, which is why a teammate's gun
	// fired in total silence with no tracer.
	// Tell the class passive that a shot is leaving, BEFORE any of this shot's damage is worked out.
	// Ordering is the whole point and it is not obvious: on the host, Fire() computes the damage and
	// only then broadcasts OnShotFired, while a client's shot reaches the server as two separate
	// RPCs with the fire report arriving FIRST. A passive that spends something on the shot can only
	// be right on both routes if the spending happens here, at the start, on every machine that
	// runs Fire. @see UAbilityHandler::OnOwnerFiredWeapon
	if (PawnOwner)
	{
		if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Abilities->NotifyOwnerFiredWeapon();
		}
	}

	PlayFireEffectsLocally();

	if (HasAuthority())
	{
		Multicast_PlayFireEffects();
	}
	else
	{
		// A client's shot reaches the server through AShooterCharacter::Server_ReportDamage, but a
		// missed shot has no damage to report, so the effects need their own path upstream.
		if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
		{
			OwnerCharacter->Server_ReportWeaponFired(this);
		}
	}

	// Add heat from firing
	if (bUseHeatSystem)
	{
		AddHeat(HeatPerShot);
	}

	// Get target location
	const FVector TargetLocation = WeaponOwner->GetWeaponTargetLocation();

	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   TargetLocation: (%.1f, %.1f, %.1f), bUseHitscan=%d"),
		*GetName(), TargetLocation.X, TargetLocation.Y, TargetLocation.Z, bUseHitscan);

	// Fire based on mode
	if (bUseHitscan)
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   >>> FireHitscan <<<"), *GetName());
		FireHitscan(TargetLocation);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   >>> FireProjectile <<<"), *GetName());
		FireProjectile(TargetLocation, ChargeMultiplier);
	}

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Error, TEXT("[Weapon:%s]   Shot complete. TimeOfLastShot=%.2f"), *GetName(), TimeOfLastShot);

	// One trigger pull opens the spread once. Deliberately here and not in the Fire* functions: a
	// shotgun puts several pellets in the air per pull, and charging them each with a full bloom
	// would make it the widest weapon in the game after two shots.
	AddShotSpread();

	// Notify listeners that a shot was fired (for NPC burst counting)
	OnShotFired.Broadcast();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// are we full auto?
	// Use current refire rate which factors in heat
	const float ActualRefireRate = GetCurrentRefireRate();

	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, ActualRefireRate, false);
	}
	else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, ActualRefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

AShooterProjectile* AShooterWeapon::SpawnProjectileAtTransform(const FTransform& ProjectileTransform,
	float ChargeMultiplier, bool bCosmeticOnly)
{
	AShooterProjectile* Projectile = nullptr;

	// The authority's projectile is replicated, and a pooled actor is reused rather than destroyed:
	// clients would keep its channel open and watch it teleport back to a muzzle on the next shot.
	// So the real one is spawned outright and only the shooter's local stand-in comes from the pool,
	// which is where the pool was earning its keep anyway.
	UProjectilePoolSubsystem* Pool = bCosmeticOnly ? GetWorld()->GetSubsystem<UProjectilePoolSubsystem>() : nullptr;
	if (Pool)
	{
		Projectile = Pool->GetProjectile(ProjectileClass, ProjectileTransform, GetOwner(), PawnOwner);
	}
	else
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = PawnOwner;
		Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
	}

	if (Projectile && bCosmeticOnly)
	{
		Projectile->SetCosmeticOnly();
	}

	// If charge-based firing, scale projectile charge and match player polarity
	if (bUseChargeFiring && Projectile)
	{
		if (AEMFProjectile* EMFProj = Cast<AEMFProjectile>(Projectile))
		{
			// Get player's charge sign
			AActor* WeaponOwnerActor = GetOwner();
			if (WeaponOwnerActor)
			{
				UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
				if (EMFMod)
				{
					float PlayerCharge = EMFMod->GetCharge();
					float PlayerSign = FMath::Sign(PlayerCharge);

					// Set projectile charge with same sign as player
					float BaseCharge = FMath::Abs(EMFProj->GetProjectileCharge());
					EMFProj->SetProjectileCharge(PlayerSign * BaseCharge * ChargeMultiplier);

					UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Projectile charge set to %.2f (player sign: %.0f, multiplier: %.2f)"),
						PlayerSign * BaseCharge * ChargeMultiplier, PlayerSign, ChargeMultiplier);
				}
			}
		}
	}

	return Projectile;
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation, float ChargeMultiplier)
{
	const FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	if (HasAuthority())
	{
		SpawnProjectileAtTransform(ProjectileTransform, ChargeMultiplier, /*bCosmeticOnly*/ false);
	}
	else
	{
		// The shot leaves the barrel now, on this screen, and asks the server for the real one in the
		// same breath. Waiting for the round trip instead would put half a ping between the trigger
		// and the projectile, which is the one thing a player notices immediately.
		SpawnProjectileAtTransform(ProjectileTransform, ChargeMultiplier, /*bCosmeticOnly*/ true);

		if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
		{
			OwnerCharacter->Server_FireProjectile(this, ProjectileTransform, ChargeMultiplier);
		}
	}

	ConsumeRoundAfterShot();
}

FVector AShooterWeapon::GetMuzzleWorldLocation() const
{
	// Same mesh choice the projectile spawn makes, so "where the shot comes from" cannot mean two
	// different places depending on who asks.
	USkeletalMeshComponent* const MuzzleMesh =
		(PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;

	if (MuzzleMesh && MuzzleMesh->DoesSocketExist(MuzzleSocketName))
	{
		return MuzzleMesh->GetSocketLocation(MuzzleSocketName);
	}

	return GetActorLocation();
}

bool AShooterWeapon::CanShotReach(const FVector& TargetLocation) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = GetMuzzleWorldLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShotClearance), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(this);
	if (PawnOwner)
	{
		Params.AddIgnoredActor(PawnOwner);
	}

	// The shell's own radius, taken from the class that will fly. Zero for hitscan, which turns the
	// sweep below back into the line trace it used to be.
	float ShotRadius = 0.0f;
	if (!bUseHitscan && ProjectileClass)
	{
		if (const AShooterProjectile* const ProjectileCDO = ProjectileClass->GetDefaultObject<AShooterProjectile>())
		{
			if (const USphereComponent* const Collision = ProjectileCDO->FindComponentByClass<USphereComponent>())
			{
				ShotRadius = Collision->GetUnscaledSphereRadius();
			}
		}
	}

	FHitResult Blocked;

	if (ShotRadius > KINDA_SMALL_NUMBER)
	{
		return !World->SweepSingleByChannel(Blocked, Start, TargetLocation, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(ShotRadius), Params);
	}

	return !World->LineTraceSingleByChannel(Blocked, Start, TargetLocation, ECC_Visibility, Params);
}

FVector AShooterWeapon::SolveBallisticAim(const FVector& LaunchLocation, const FVector& TargetLocation) const
{
	// Players are left alone. See the header: correcting their aim is aim assist, not ballistics.
	if (!PawnOwner || PawnOwner->IsPlayerControlled())
	{
		return FVector::ZeroVector;
	}

	if (!ProjectileClass)
	{
		return FVector::ZeroVector;
	}

	// Asked of the projectile class itself rather than configured on the weapon. The two numbers
	// that decide whether a shot needs an arc - does it fall, and how fast does it go - already live
	// on the thing that flies, and a second copy on the weapon is a second copy to get wrong.
	const AShooterProjectile* const ProjectileCDO = ProjectileClass->GetDefaultObject<AShooterProjectile>();
	if (!ProjectileCDO)
	{
		return FVector::ZeroVector;
	}

	// Found on the CDO rather than read off a member: AShooterProjectile keeps its movement
	// component protected, and a getter added purely so the weapon could peek at two numbers would
	// be public API bought for one caller. The default subobject is on the CDO exactly as it will be
	// on the spawned actor, so this reads the same values the projectile will fly with.
	const UProjectileMovementComponent* const ProjectileMove =
		ProjectileCDO->FindComponentByClass<UProjectileMovementComponent>();
	if (!ProjectileMove)
	{
		return FVector::ZeroVector;
	}

	const float GravityScale = ProjectileMove->ProjectileGravityScale;
	const float LaunchSpeed = ProjectileMove->InitialSpeed;

	// No gravity means the straight line already hits. This is the whole switch: a rocket configured
	// to fly flat never enters the solver, and nothing anywhere has to be told that it is a rocket.
	if (FMath::IsNearlyZero(GravityScale) || LaunchSpeed <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	const float GravityZ = World->GetGravityZ() * GravityScale;

	// Aim at the BODY, not at the aim point that was passed in.
	//
	// The caller's TargetLocation comes from GetWeaponTargetLocation, which for an AI is the far end
	// of an accuracy-spread ray traced out to AimRange - and pawns do not block that channel here,
	// so it lands on a wall behind the enemy or in open space thousands of units past them. Solving
	// an arc onto that point yields the launch angle that REACHES that point, so the shell flies
	// over the target and comes down somewhere in the distance. Every "стреляет в воздух далеко за
	// игрока" was this, and every "стреляет в стену" was the same ray ending on a wall instead.
	//
	// A hitscan does not care, because it only wants the direction and stops at the first thing it
	// hits. A ballistic shot is the one caller that needs the real distance.
	FVector BodyLocation = TargetLocation;
	if (const AActor* const AimActor = WeaponOwner ? WeaponOwner->GetWeaponAimActor() : nullptr)
	{
		BodyLocation = AimActor->GetActorLocation();
	}

	if (bLogBallistics)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BALLISTIC_DEBUG] %s solve: passedTarget=%s bodyTarget=%s dist=%.0f speed=%.0f gravScale=%.2f lob=%d"),
			*GetNameSafe(PawnOwner), *TargetLocation.ToCompactString(), *BodyLocation.ToCompactString(),
			FVector::Dist(LaunchLocation, BodyLocation), LaunchSpeed, GravityScale, bLobProjectiles ? 1 : 0);
	}

	// The engine's own solver, and the reason not to write one: it takes the speed as FIXED, which
	// is the constraint that actually applies here. AShooterProjectile launches at InitialSpeed
	// along its spawn rotation, so the only free variable is the angle - exactly the problem
	// SuggestProjectileVelocity solves, and it answers false when the target is simply out of range
	// rather than returning a direction that quietly falls short.
	UGameplayStatics::FSuggestProjectileVelocityParameters SolverParams(
		this, LaunchLocation, BodyLocation, LaunchSpeed);

	SolverParams.bFavorHighArc = bLobProjectiles;
	SolverParams.OverrideGravityZ = GravityZ;

	// No trace along the path. The solver's tracing modes are for picking a trajectory that misses
	// the scenery, which is a different question from "what angle reaches this point" and costs a
	// sweep of the whole arc per shot. Whether the shell clears the wall in front of it is already
	// decided by the cover the NPC chose to fire from.
	SolverParams.TraceOption = ESuggestProjVelocityTraceOption::DoNotTrace;

	FVector TossVelocity = FVector::ZeroVector;
	bool bSolved = UGameplayStatics::SuggestProjectileVelocity(SolverParams, TossVelocity);

	// A high arc only means "lob" while the projectile is slow enough for the range to be a real
	// constraint. Give the solver far more speed than the shot needs and the high solution walks
	// towards vertical, because straight up and straight down also reaches a target a thousand units
	// away - so a 3000 u/s shell fired at a nearby corner came out as a mortar round aimed at the
	// sky, which is what "стреляют вверх в воздух" was.
	//
	// So the lob is a preference, not an instruction: if the high answer comes out steeper than a
	// shot anybody would recognise, take the flat one instead. The flat solution still arcs - the
	// projectile still falls - it simply stops pretending the weapon is artillery.
	if (bSolved && bLobProjectiles)
	{
		const float LaunchPitchDeg = FMath::RadiansToDegrees(FMath::Asin(
			FMath::Clamp(TossVelocity.GetSafeNormal().Z, -1.0f, 1.0f)));

		if (LaunchPitchDeg > MaxLobPitchDegrees)
		{
			SolverParams.bFavorHighArc = false;
			bSolved = UGameplayStatics::SuggestProjectileVelocity(SolverParams, TossVelocity);
		}
	}

	// Out of range. Falling back to the straight line is deliberate: the shot still leaves, still
	// lands somewhere short, and still makes noise and splash. An enemy that silently refuses to
	// fire at a target it can see reads as broken, and the splash radius is generous enough that a
	// short round is not a wasted one.
	if (!bSolved)
	{
		return FVector::ZeroVector;
	}

	const FVector LaunchDir = TossVelocity.GetSafeNormal();

	if (bLogBallistics)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BALLISTIC_DEBUG] %s launch pitch %.1f deg"),
			*GetNameSafe(PawnOwner),
			FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(LaunchDir.Z, -1.0f, 1.0f))));
	}

	// Is there room to launch at all. An arc leaves the muzzle at an angle, so a shooter tucked under
	// a lintel or hugging the inside of a corner can have a clean line to the target and still put
	// the shell into the ceiling half a metre up - which looks exactly like the enemy shooting the
	// wall for no reason.
	//
	// Short trace, because this is asking "can the round get out", not "does the whole trajectory
	// clear". The rest of the flight is the arc's business.
	if (const UWorld* const TraceWorld = GetWorld())
	{
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(BallisticMuzzleClearance), /*bTraceComplex*/ false);
		ClearanceParams.AddIgnoredActor(this);
		ClearanceParams.AddIgnoredActor(PawnOwner);

		FHitResult Blocked;
		if (TraceWorld->LineTraceSingleByChannel(Blocked, LaunchLocation,
			LaunchLocation + LaunchDir * MuzzleClearanceDistance, ECC_Visibility, ClearanceParams))
		{
			// Blocked. Falling back to the straight line rather than refusing the shot: the straight
			// line is what this weapon did before ballistics existed, it is aimed at something the
			// NPC can actually see, and a silent enemy reads worse than a low round.
			if (bLogBallistics)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[BALLISTIC_DEBUG] %s muzzle BLOCKED by %s -> straight line"),
					*GetNameSafe(PawnOwner), *GetNameSafe(Blocked.GetActor()));
			}

			return FVector::ZeroVector;
		}
	}

	return LaunchDir;
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;

	// find the muzzle location
	const FVector MuzzleLoc = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// Turn the aim line inside the spread cone. This used to nudge the TARGET POINT by AimVariance
	// centimetres, which is not a spread at all: the same number was a wide scatter at arm's length
	// and nothing at range, and it could not agree with a crosshair drawn from an angle. The cone is
	// the same one the hitscan path uses, so a projectile weapon and a hitscan weapon of equal
	// spread now shoot equally wide.
	//
	// The straight line is the PLAYER's answer and the fallback. An AI firing a projectile that
	// falls gets the ballistic one instead, below.
	const float ConeDegrees = GetAimConeDegrees();

	// Ballistic shots scatter their TARGET, everything else scatters its DIRECTION, and the two are
	// not interchangeable once gravity is involved.
	//
	// Range off a ballistic launch goes as sin(2*theta), so pitch error turns into range error
	// nonlinearly and brutally. Measured from this log: a 2750 unit shot at 3300 u/s solves to a
	// 7.2 degree launch, and tilting that by five degrees of ordinary cone spread lands the round at
	//
	//     2.2 deg -> 852 units        12.2 deg -> 4591 units
	//
	// against a target at 2750. The short end of that is not a miss, it is the shell going off
	// practically at the shooter's feet - in the corner it just leaned out of. Every "стреляет в
	// стену" that survived the aim fix was one of those short rounds.
	//
	// Displacing the aim POINT instead keeps the range honest: the arc is solved to wherever the
	// scatter put the point, so the round always travels about the right distance and lands around
	// the target rather than somewhere between here and there. The cone is then deliberately NOT
	// applied on top - it has already been spent.
	FVector AimDir = FVector::ZeroVector;
	bool bUsedBallistic = false;

	{
		FVector ScatteredTarget = TargetLocation;
		if (ConeDegrees > 0.0f)
		{
			// The cone half-angle read as a lateral offset at this range, which is the same spread a
			// hitscan of equal cone would show on the same target.
			const float Reach = FVector::Dist(SpawnLoc, TargetLocation);
			const float Radius = Reach * FMath::Tan(FMath::DegreesToRadians(ConeDegrees));

			ScatteredTarget += FMath::VRand() * FMath::FRand() * Radius;
		}

		if (const FVector BallisticDir = SolveBallisticAim(SpawnLoc, ScatteredTarget); !BallisticDir.IsNearlyZero())
		{
			AimDir = BallisticDir;
			bUsedBallistic = true;
		}
	}

	if (!bUsedBallistic)
	{
		// The straight line is the PLAYER's answer and the fallback for anything that does not fall.
		// Here the cone is the right tool, for the reason the original comment gives: it is
		// range-independent, unlike nudging a point by a fixed number of centimetres.
		const FVector StraightDir = (TargetLocation - SpawnLoc).GetSafeNormal();
		AimDir = (ConeDegrees > 0.0f)
			? UKismetMathLibrary::RandomUnitVectorInConeInDegrees(StraightDir, ConeDegrees)
			: StraightDir;
	}

	const FRotator AimRot = AimDir.Rotation();

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

// ==================== Hitscan Implementation ====================

void AShooterWeapon::ResolveHitscanRay(const FVector& TargetLocation, FVector& OutStart, FVector& OutDirection) const
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;
	const FVector MuzzleLocation = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ViewDir = FVector::ForwardVector;
	FVector ViewLocation = MuzzleLocation; // Fallback

	if (PawnOwner)
	{
		ViewDir = PawnOwner->GetBaseAimRotation().Vector();
		ViewLocation = PawnOwner->GetPawnViewLocation();
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ToTargetVector = TargetLocation - MuzzleLocation;
	float DistanceToTarget = ToTargetVector.Size();
	FVector ToTargetDir = ToTargetVector.GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	// 1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾, 0.0 = 90 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â², -1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´
	float DotP = FVector::DotProduct(ToTargetDir, ViewDir);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â§ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ===

	// 1. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¯ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â· ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), ViewLocation, TargetLocation, FColor::Green, false, 3.0f, 0, 1.0f);

	// 2. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ "ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾" ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â«ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), MuzzleLocation, TargetLocation, FColor::Red, false, 3.0f, 0, 1.0f);

	// 3. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°), ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡
	//DrawDebugSphere(GetWorld(), MuzzleLocation, 10.0f, 12, FColor::Blue, false, 3.0f);

	// 4. ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FString DebugMsg = FString::Printf(TEXT("Dist: %.1f | Dot: %.3f | Fix Applied: %s"),
		DistanceToTarget,
		DotP,
		(DistanceToTarget < 100.0f || DotP < 0.5f) ? TEXT("YES") : TEXT("NO"));

	//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, DebugMsg);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ ===

	FVector Direction;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ 100 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ (1 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬) ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (< 0.5 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ 60 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²)
	if (DistanceToTarget < 100.0f || DotP < 0.5f)
	{
		// FIX: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		Direction = ViewDir;
		//UE_LOG(LogTemp, Warning, TEXT("FireHitscan: FIXED direction used (Too close or bad angle)"));
	}
	else
	{
		// STANDARD: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		Direction = ToTargetDir;
	}

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ 2 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	//DrawDebugLine(GetWorld(), MuzzleLocation, MuzzleLocation + (Direction * 200.0f), FColor::Yellow, false, 3.0f, 0, 2.0f);


	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (AimVariance)
	// === Classic hitscan (WaveDivergence == 0): trace from the CAMERA, not the muzzle ===
	// The aim point from GetWeaponTargetLocation() lies BEHIND the enemy (pawn profiles ignore
	// ECC_Visibility), so a muzzle-based ray keeps up to the full camera->muzzle offset of
	// parallax at the enemy's depth — a thin ray can miss a capsule the crosshair is dead on.
	// Re-basing to the camera viewpoint makes crosshair == bullet path; the tracer is still
	// drawn from the muzzle (see PerformClassicHitscan).
	FVector HitscanStart = MuzzleLocation;
	const bool bClassicHitscan = (WaveDivergence * MaxDivergenceAngle <= KINDA_SMALL_NUMBER);
	if (bClassicHitscan && PawnOwner && PawnOwner->IsPlayerControlled())
	{
		if (AController* OwnerController = PawnOwner->GetController())
		{
			FVector ViewPointLoc;
			FRotator ViewPointRot;
			OwnerController->GetPlayerViewPoint(ViewPointLoc, ViewPointRot);

			const FVector RebasedDir = (TargetLocation - ViewPointLoc).GetSafeNormal();
			HitscanStart = ViewPointLoc;
			Direction = RebasedDir.IsNearlyZero() ? ViewPointRot.Vector() : RebasedDir;
		}
	}

	// Spread: turn the aim line inside a cone of the weapon's CURRENT spread, not its authored base
	// value. A uniform direction in the cone rather than a uniform vector added to it, so the shot
	// is even across the circle instead of piling up in the middle.
	const float AimConeDegrees = GetAimConeDegrees();
	if (AimConeDegrees > 0.0f)
	{
		Direction = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(Direction, AimConeDegrees);
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»
	// [HITSCAN_DEBUG] Inputs of the shot: where the muzzle is, where the camera-aim point landed,
	// and how far it is. AimDist ~= MaxAimDistance means the aim trace hit NOTHING (open area) —
	// worst case for muzzle parallax.
	UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] FireHitscan: Muzzle=%s AimPoint=%s AimDist=%.0f DotP=%.3f dirMode=%s"),
		*MuzzleLocation.ToCompactString(), *TargetLocation.ToCompactString(), DistanceToTarget, DotP,
		(DistanceToTarget < 100.0f || DotP < 0.5f) ? TEXT("ViewDir(override)") : TEXT("Muzzle->AimPoint"));

	OutStart = HitscanStart;
	OutDirection = Direction;
}

FVector AShooterWeapon::GetFirstPersonMuzzleRenderLocation() const
{
	const FVector MuzzleWorld = FirstPersonMesh
		? FirstPersonMesh->GetSocketLocation(MuzzleSocketName)
		: GetActorLocation();

	const APlayerController* PC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return MuzzleWorld;
	}

	const FMinimalViewInfo& POV = PC->PlayerCameraManager->GetCameraCacheView();

	const float Scale = (POV.FirstPersonScale > 0.0f) ? POV.FirstPersonScale : 1.0f;
	const float FOVCorrection = POV.CalculateFirstPersonFOVCorrectionFactor();

	// Nothing to correct: the weapon is drawn with the same field of view as the world and at its
	// own size, so where it stands is where it is seen.
	if (FMath::IsNearlyEqual(Scale, 1.0f) && FMath::IsNearlyEqual(FOVCorrection, 1.0f))
	{
		return MuzzleWorld;
	}

	const FQuat ViewRotation = POV.Rotation.Quaternion();
	const FVector Forward = ViewRotation.GetForwardVector();
	const FVector Right = ViewRotation.GetRightVector();
	const FVector Up = ViewRotation.GetUpVector();

	const FVector Relative = MuzzleWorld - POV.Location;

	// The renderer's ScaleVector, rebuilt in world space: depth takes the plain scale, the screen
	// plane takes the FOV correction on top of it.
	const double Depth = FVector::DotProduct(Relative, Forward) * Scale;
	const double Lateral = FVector::DotProduct(Relative, Right) * Scale * FOVCorrection;
	const double Vertical = FVector::DotProduct(Relative, Up) * Scale * FOVCorrection;

	return POV.Location + Forward * Depth + Right * Lateral + Up * Vertical;
}

void AShooterWeapon::FireHitscan(const FVector& TargetLocation)
{
	FVector Start;
	FVector Direction;
	ResolveHitscanRay(TargetLocation, Start, Direction);

	// NPC: simple line trace instead of cone hitscan.
	// The cone system was designed for the player (camera and muzzle co-located in FPS).
	// For NPCs, camera (eyes) and muzzle (weapon) have ~40-50u parallax offset,
	// exceeding the 5deg cone half-angle, causing valid hits to be rejected.
	if (PawnOwner && !PawnOwner->IsPlayerControlled())
	{
		PerformSimpleHitscan(Start, Direction, 1.0f);
	}
	else
	{
		PerformHitscan(Start, Direction, 1.0f, 0);
	}

	ConsumeRoundAfterShot();
}

void AShooterWeapon::ConsumeRoundAfterShot()
{

	//ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	WeaponOwner->PlayFiringMontage(FiringMontage);
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	--CurrentBullets;

	// Magazine depleted: yanked weapons get discarded (player can't reload), others auto-refill
	if (CurrentBullets <= 0)
	{
		AShooterCharacter* PlayerOwner = Cast<AShooterCharacter>(PawnOwner);
		if (bHasLimitedAmmo && PlayerOwner)
		{
			// Defer discard to next tick — DropYankedWeaponIfAny destroys this weapon actor,
			// can't be done synchronously inside Fire().
			GetWorld()->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(PlayerOwner, &AShooterCharacter::ThrowYankedWeaponIfEmpty));
			UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: magazine empty, scheduled discard for next tick"), *GetName());
		}
		else if (bUseReload)
		{
			// The magazine is real: it stays empty until somebody fills it, and by default that
			// somebody is the player. This branch is the weapon taking that decision for them, which
			// it only does when explicitly told to.
			if (bAutoReloadWhenEmpty)
			{
				StartReload();
			}
		}
		else
		{
			CurrentBullets = MagazineSize;  // Auto-refill: starter weapons, NPC drops, NPC owners
		}
	}

	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

// ========================================
// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
// (compile-time DEBUG_CONE_HITSCAN replaced by the runtime bDrawHitscanDebug weapon flag)
// ========================================

void AShooterWeapon::PerformHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount)
{
	// Zero divergence: the cone math degenerates and its filter rejects legitimate hits —
	// route to the classic thin-ray path (see PerformClassicHitscan for details).
	if (WaveDivergence * MaxDivergenceAngle <= KINDA_SMALL_NUMBER)
	{
		PerformClassicHitscan(Start, Direction, RemainingEnergy, ReflectionCount);
		return;
	}

	float SegmentMaxDistance = MaxHitscanRange * RemainingEnergy;
	FVector End = Start + Direction * SegmentMaxDistance;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float ConeHalfAngleRad = FMath::DegreesToRadians(DivergenceAngle);
	float CosHalfAngle = FMath::Cos(ConeHalfAngleRad);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 1: Line trace ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹) =====
	FHitResult WallHitResult;
	FCollisionQueryParams WallQueryParams;
	WallQueryParams.AddIgnoredActor(this);
	WallQueryParams.AddIgnoredActor(GetOwner());
	WallQueryParams.bReturnPhysicalMaterial = true;

	// Используем ECC_Visibility channel вместо ObjectType - 
	// он корректно учитывает collision responses и игнорирует triggers/overlaps
	bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHitResult,
		Start,
		End,
		ECC_Visibility,
		WallQueryParams
	);

	float MaxDistance = bHitWall ? WallHitResult.Distance : SegmentMaxDistance;
	FVector BeamEnd = bHitWall ? WallHitResult.ImpactPoint : End;

	// [HITSCAN_DEBUG] Shot summary: divergence + sweep sphere radius + what the Visibility (wall) trace hit.
	// SweepR is the sphere radius used by the pawn sweep below — if it's tiny (divergence ~0) the
	// sweep behaves like a thin ray and muzzle parallax can make it miss entirely.
	UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] === Shot: Start=%s Dir=%s | Diverg=%.2fdeg SweepR=%.1f MaxDist=%.0f | Wall=%s comp=%s dist=%.0f"),
		*Start.ToCompactString(), *Direction.ToCompactString(),
		DivergenceAngle, CalculateWaveRadius(MaxDistance), MaxDistance,
		bHitWall ? *GetNameSafe(WallHitResult.GetActor()) : TEXT("none"),
		bHitWall ? *GetNameSafe(WallHitResult.GetComponent()) : TEXT("-"),
		bHitWall ? WallHitResult.Distance : SegmentMaxDistance);

	// DEBUG: Log what the Visibility line trace hit (helps diagnose EMFPhysicsProp hits)
	if (bHitWall && WallHitResult.GetActor())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hitscan DEBUG] Visibility trace hit: %s (Class: %s) at dist=%.0f"),
			*WallHitResult.GetActor()->GetName(),
			*WallHitResult.GetActor()->GetClass()->GetName(),
			WallHitResult.Distance);
	}

	// Direct damage to non-Pawn physics actors hit by Visibility trace (e.g. EMFPhysicsProp).
	// The cone sweep only queries ECC_Pawn, so PhysicsActor objects are invisible to it.
	// Apply damage via the existing ApplyHitscanDamage path for these actors.
	if (bHitWall && WallHitResult.GetActor() && !Cast<APawn>(WallHitResult.GetActor()))
	{
		AActor* WallActor = WallHitResult.GetActor();
		if (WallActor->CanBeDamaged())
		{
			ApplyHitscanDamage(WallHitResult, RemainingEnergy, WallHitResult.Distance, 0.0f);
		}
	}

	if (bDrawHitscanDebug)
	{
	// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	const float DebugDuration = 2.0f;
	const bool bPersistent = false;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	DrawDebugLine(GetWorld(), Start, BeamEnd, FColor::Green, bPersistent, DebugDuration, 0, 2.0f);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	DrawDebugSphere(GetWorld(), Start, 5.0f, 8, FColor::Blue, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	DrawDebugSphere(GetWorld(), BeamEnd, 10.0f, 8, bHitWall ? FColor::Red : FColor::Green, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	const int32 NumConeLines = 16; // ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	FVector Right = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
	}
	FVector Up = FVector::CrossProduct(Right, Direction).GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦
	TArray<float> DebugDistances = { 100.0f, 500.0f, 1000.0f, MaxDistance * 0.5f, MaxDistance };

	for (float DebugDist : DebugDistances)
	{
		if (DebugDist > MaxDistance) continue;

		float ConeRadius = CalculateWaveRadius(DebugDist);
		FVector ConeCenter = Start + Direction * DebugDist;

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		FVector PrevPoint = ConeCenter + Right * ConeRadius;
		for (int32 i = 1; i <= NumConeLines; i++)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
			DrawDebugLine(GetWorld(), PrevPoint, PointOnCircle, FColor::Yellow, bPersistent, DebugDuration, 0, 1.0f);

			PrevPoint = PointOnCircle;
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ 4-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½)
		for (int32 i = 0; i < NumConeLines; i += 4)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ)
			DrawDebugLine(GetWorld(), Start, PointOnCircle, FColor::Orange, bPersistent, DebugDuration, 0, 0.5f);
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (wireframe)
		DrawDebugCircle(GetWorld(), ConeCenter, ConeRadius, 32, FColor::Cyan, bPersistent, DebugDuration, 0, 1.0f, Up, Right, false);
	}
	// ===== END DEBUG =====
	}

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 2: Multi Sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¥ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	TArray<FHitResult> SweepHits;
	TArray<AActor*> HitTargets;

	FCollisionQueryParams SweepQueryParams;
	SweepQueryParams.AddIgnoredActor(this);
	SweepQueryParams.AddIgnoredActor(GetOwner());
	SweepQueryParams.bReturnPhysicalMaterial = true;

	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	float MaxConeRadius = CalculateWaveRadius(MaxDistance);

	// Multi sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	GetWorld()->SweepMultiByObjectType(
		SweepHits,
		Start,
		BeamEnd,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(MaxConeRadius),
		SweepQueryParams
	);

	UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan: Sweep found %d hits, MaxRadius=%.1f, MaxDist=%.0f, Angle=%.1f"),
		SweepHits.Num(), MaxConeRadius, MaxDistance, DivergenceAngle);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 3: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// Best target tracking (single-target: only damage the most central enemy)
	AActor* BestTarget = nullptr;
	FHitResult BestHit;
	FVector BestHitLocation = FVector::ZeroVector;
	float BestHitDistance = 0.0f;
	float BestAngle = MAX_FLT;
	bool bBestIsHeadshot = false;
	FVector BestToHitDir = FVector::ZeroVector;

	for (const FHitResult& Hit : SweepHits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitTargets.Contains(HitActor))
		{
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		FVector HitLocation = Hit.ImpactPoint;
		float HitDistance = Hit.Distance;

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ Distance ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ 0 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		if (HitDistance < 1.0f)
		{
			HitDistance = FVector::Dist(Start, HitActor->GetActorLocation());
			HitLocation = HitActor->GetActorLocation();
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector ToHit = HitLocation - Start;
		FVector ToHitDir = ToHit.GetSafeNormal();

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float DotProduct = FVector::DotProduct(Direction, ToHitDir);
		float AngleToHit = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		float ConeRadiusAtDistance = CalculateWaveRadius(HitDistance);

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector PointOnAxis = Start + Direction * HitDistance;
		float DistanceFromAxis = FVector::Dist(HitLocation, PointOnAxis);

		UE_LOG(LogTemp, Warning, TEXT("  - %s: Dist=%.0f, Angle=%.1fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°, DistFromAxis=%.1f, ConeRadius=%.1f"),
			*HitActor->GetName(), HitDistance, AngleToHit, DistanceFromAxis, ConeRadiusAtDistance);

		// [HITSCAN_DEBUG] Full candidate info: which component was swept (capsule vs mesh), bone,
		// raw sweep distance (can be << real distance for fat sweep spheres) and both cone-filter inputs.
		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG]   cand=%s comp=%s bone=%s | rawDist=%.0f fixDist=%.0f | dot=%.4f cosHalf=%.4f | axisDist=%.1f coneR=%.1f"),
			*HitActor->GetName(), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(),
			Hit.Distance, HitDistance, DotProduct, CosHalfAngle, DistanceFromAxis, ConeRadiusAtDistance);

	if (bDrawHitscanDebug)
	{
		// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ =====
		const float DebugDuration = 2.0f;
		const bool bPersistent = false;
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugLine(GetWorld(), Start, HitLocation, FColor::White, bPersistent, DebugDuration, 0, 1.0f);
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), PointOnAxis, 8.0f, 6, FColor::Magenta, bPersistent, DebugDuration);
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ DistanceFromAxis)
		DrawDebugLine(GetWorld(), PointOnAxis, HitLocation, FColor::Magenta, bPersistent, DebugDuration, 0, 2.0f);
		// ===== END DEBUG =====
	}

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
		// 1) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ
		// 2) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		bool bInsideCone = (DotProduct >= CosHalfAngle) || (DistanceFromAxis <= ConeRadiusAtDistance);

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬)
		if (HitDistance < 200.0f)
		{
			bInsideCone = true; // ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
		}

		if (!bInsideCone)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG]     -> REJECTED: OUTSIDE CONE (dot=%.4f < cosHalf=%.4f AND axisDist=%.1f > coneR=%.1f)"),
				DotProduct, CosHalfAngle, DistanceFromAxis, ConeRadiusAtDistance);
	if (bDrawHitscanDebug)
	{
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Red, false, 2.0f);
	}
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½
		FHitResult BlockCheck;
		FCollisionQueryParams BlockQueryParams;
		BlockQueryParams.AddIgnoredActor(this);
		BlockQueryParams.AddIgnoredActor(GetOwner());
		BlockQueryParams.AddIgnoredActor(HitActor);

		bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			BlockCheck,
			Start,
			HitLocation,
			ECC_Visibility,
			BlockQueryParams
		);

		if (bBlocked && BlockCheck.Distance < HitDistance - 50.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG]     -> REJECTED: BLOCKED by %s comp=%s at %.0f (threshold %.0f)"),
				*GetNameSafe(BlockCheck.GetActor()), *GetNameSafe(BlockCheck.GetComponent()),
				BlockCheck.Distance, HitDistance - 50.0f);
	if (bDrawHitscanDebug)
	{
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Orange, false, 2.0f);
	}
			continue;
		}

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
		HitTargets.Add(HitActor);

	if (bDrawHitscanDebug)
	{
		// DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), HitLocation, 25.0f, 12, FColor::Green, false, 2.0f);
	}

		// Track best (most central) target
		if (AngleToHit < BestAngle)
		{
			BestAngle = AngleToHit;
			BestTarget = HitActor;
			BestHit = Hit;
			BestHitLocation = HitLocation;
			BestHitDistance = HitDistance;
			bBestIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
			BestToHitDir = ToHitDir;
		}
	}

	// ===== PASS 2: Apply damage to the best (most central) target only =====
	if (BestTarget)
	{
		// Calculate wave radius at target distance
		float WaveRadiusAtTarget = CalculateWaveRadius(BestHitDistance);
		float TotalDistance = BestHitDistance;

		if (ReflectionCount > 0)
		{
			float OriginalEnergy = 1.0f;
			for (int32 i = 0; i < ReflectionCount; i++)
			{
				OriginalEnergy *= (1.0f - ReflectionEnergyLoss);
			}
			float PreviousDistance = MaxHitscanRange * (1.0f - RemainingEnergy / OriginalEnergy);
			TotalDistance = PreviousDistance + BestHitDistance;
		}

		float AreaMultiplier = CalculateDamageMultiplier(TotalDistance, WaveRadiusAtTarget);

		// Headshot check
		float HeadshotMult = bBestIsHeadshot ? HeadshotMultiplier : 1.0f;

		// Heat System multiplier
		float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

		// Z-Factor multiplier
		float ZFactorMult = 1.0f;
		if (bUseZFactor && PawnOwner)
		{
			float ShooterZ = PawnOwner->GetActorLocation().Z;
			float TargetZ = BestTarget->GetActorLocation().Z;
			ZFactorMult = CalculateZFactorMultiplier(ShooterZ, TargetZ);
		}

		// Tag-based damage multiplier
		float TagMult = GetTagDamageMultiplier(BestTarget);

		// Upgrade damage multiplier (e.g. Forward Momentum)
		float UpgradeMult = 1.0f;
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(BestTarget);
			}
		}

		float FinalDamage = HitscanDamage * RemainingEnergy * AreaMultiplier * HeadshotMult * HeatMult * ZFactorMult * TagMult * UpgradeMult;

		UE_LOG(LogTemp, Warning, TEXT("    BEST TARGET HIT: %s | Damage: %.1f x Energy:%.2f x Area:%.2f x HS:%.1f x Heat:%.2f x Z:%.2f x Tag:%.2f x Upg:%.2f = %.1f"),
			*BestTarget->GetName(), HitscanDamage, RemainingEnergy, AreaMultiplier, HeadshotMult, HeatMult, ZFactorMult, TagMult, UpgradeMult, FinalDamage);

		// The shield as it stood when the bullet arrived, read before anything touches the target.
		// @see ApplyHitscanDamage.
		const bool bShieldDownBefore = IsTargetShieldDown(BestTarget);

		// Apply damage
		FDamageEvent DamageEvent;
		if (HitscanDamageType)
		{
			DamageEvent.DamageTypeClass = HitscanDamageType;
		}

		float ActualDamage = ApplyDamageToTarget(BestTarget, FinalDamage, DamageEvent);
		const bool bKilled = IsActorDeadAfterDamage(BestTarget);

		// [HITSCAN_DEBUG] dealt = what we sent into TakeDamage, applied = what TakeDamage returned.
		// applied=0 with dealt>0 means the TARGET swallowed it (friendly-fire guard / dead / immune).
		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] APPLIED: target=%s dealt=%.1f applied=%.1f killed=%d"),
			*BestTarget->GetName(), FinalDamage, ActualDamage, bKilled ? 1 : 0);

		// Feedback goes out once, at the end of this block, when the shield reading after the shot
		// is known. @see ApplyHitscanDamage for why it is not sent here.

		// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits.
		// Suppression Fire / future hitscan-on-hit upgrades depend on this firing for the pistol.
		// Per-upgrade filters (e.g. SF's IsHitscan + ShooterNPC check) live in OnOwnerDealtDamage.
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMgr->NotifyWeaponDealtDamage(this, BestTarget, ActualDamage, bKilled);
			}
			// And the class passive, which is the other thing on this pawn with an interest in what
			// its shots land on. @see UAbilityHandler::OnOwnerDealtDamage.
			if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
			{
				Abilities->NotifyOwnerDealtDamage(BestTarget, ActualDamage, bKilled);
			}
		}

		// Apply physics impulse / knockback
		FVector ImpulseDirection = BestToHitDir;
		float ImpulseForce = HitscanPhysicsForce * RemainingEnergy * AreaMultiplier;
		if (ACharacter* HitCharacter = Cast<ACharacter>(BestTarget))
		{
			// Exceptions (turret, ionizer vs boss) and the grounded rule live in the helper.
			ApplyHitscanKnockback(HitCharacter, ImpulseDirection * ImpulseForce, bUseHitscanIonization);
		}
		else if (UPrimitiveComponent* HitComp = BestHit.GetComponent())
		{
			if (HitComp->IsSimulatingPhysics())
			{
				HitComp->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, BestHitLocation);
			}
		}

		// Apply ionization (add positive charge to target). HitComponent gates the NPC-shield rule.
		const bool bIonized = ApplyHitscanIonization(BestTarget, BestHit.GetComponent());

		if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
		{
			const bool bShieldDownAfter = IsTargetShieldDown(BestTarget);

			FHitFeedbackContext Feedback;
			Feedback.HitLocation = BestHitLocation;
			Feedback.HitDirection = BestToHitDir;
			Feedback.Damage = ActualDamage;
			Feedback.bHeadshot = bBestIsHeadshot;
			Feedback.bKilled = bKilled;
			Feedback.bShieldHit = !bShieldDownBefore;
			Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
			Feedback.bZeroDamage = (ActualDamage <= 0.0f);
			Feedback.HitActor = BestTarget;
			Feedback.FeedbackSet = FeedbackSet;

			WeaponOwner->OnWeaponHitFeedback(Feedback);
		}
	}

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 4: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ =====
	UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan RESULT: %d targets hit"), HitTargets.Num());

	// [HITSCAN_DEBUG] The "tracer hit but no damage" case lands exactly here:
	// sweepHits=0           -> pawn sweep never found the enemy (parallax / object type / collision off)
	// sweepHits>0 passed=0  -> all candidates rejected (see REJECTED lines above for the reason)
	if (!BestTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] NO DAMAGE THIS SHOT: sweepHits=%d passedFilter=%d (beam drawn to %s)"),
			SweepHits.Num(), HitTargets.Num(),
			bHitWall ? *GetNameSafe(WallHitResult.GetActor()) : TEXT("max range"));
	}

	// If we hit a pawn, shorten beam to the pawn hit location
	FVector EffectiveBeamEnd = BeamEnd;
	if (BestTarget)
	{
		// Use actor location as fallback if hit location is zero
		FVector PawnEndPoint = BestHitLocation.IsNearlyZero() ? BestTarget->GetActorLocation() : BestHitLocation;
		float DistToPawn = FVector::Dist(Start, PawnEndPoint);
		float DistToWall = FVector::Dist(Start, BeamEnd);

		if (DistToPawn < DistToWall)
		{
			EffectiveBeamEnd = PawnEndPoint;
		}

		UE_LOG(LogTemp, Warning, TEXT("Pawn beam end: Target=%s, PawnDist=%.0f, WallDist=%.0f, Using=%s"),
			*BestTarget->GetName(), DistToPawn, DistToWall,
			DistToPawn < DistToWall ? TEXT("PAWN") : TEXT("WALL"));
	}

	// --- Visuals: the trace starts at the socket's REAL position, the beam must start where the
	// barrel is SEEN. The first-person mesh is drawn through a transform of its own (FP FOV
	// correction + FirstPersonScale), so a tracer put at the raw socket location leaves the gun
	// from somewhere off to the side. Same correction the classic path already applies; see
	// GetFirstPersonMuzzleRenderLocation. Reflected segments start at the bounce point instead.
	FVector VisualStart = Start;
	if (ReflectionCount == 0 && PawnOwner && PawnOwner->IsPlayerControlled() && FirstPersonMesh)
	{
		VisualStart = GetFirstPersonMuzzleRenderLocation();
	}

	// Where the tracer is claimed to start vs where the socket actually is. If the yellow sphere
	// sits on the barrel tip on screen but the tracer leaves from somewhere else, the offset is
	// inside the Niagara asset (local-space beam), not here. If the yellow sphere is ALSO in the
	// wrong place, MuzzleSocketName does not exist on this mesh and GetSocketLocation quietly
	// returned the component origin.
	if (bDrawHitscanDebug && FirstPersonMesh)
	{
		const bool bSocketExists = FirstPersonMesh->DoesSocketExist(MuzzleSocketName);
		DrawDebugSphere(GetWorld(), FirstPersonMesh->GetSocketLocation(MuzzleSocketName), 3.0f, 8,
			bSocketExists ? FColor::Yellow : FColor::Red, false, 5.0f);
		DrawDebugSphere(GetWorld(), VisualStart, 2.0f, 8, FColor::Magenta, false, 5.0f);
		DrawDebugLine(GetWorld(), VisualStart, EffectiveBeamEnd, FColor::White, false, 5.0f, 0, 0.25f);
		UE_LOG(LogTemp, Warning, TEXT("[MUZZLE_DEBUG] socketExists=%d socket=%s visualStart=%s comp=%s beamEnd=%s"),
			bSocketExists ? 1 : 0,
			*FirstPersonMesh->GetSocketLocation(MuzzleSocketName).ToCompactString(),
			*VisualStart.ToCompactString(),
			*FirstPersonMesh->GetComponentLocation().ToCompactString(),
			*EffectiveBeamEnd.ToCompactString());
	}

	SpawnBeamEffect(VisualStart, EffectiveBeamEnd, RemainingEnergy);

	if (bUseWaveVisualization)
	{
		SpawnWaveFronts(VisualStart, EffectiveBeamEnd);
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â­ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	// Decide impact target: pawn (if hit closer than wall) or wall.
	// The wall trace uses ECC_Visibility which passes through pawns, so without this check
	// the impact would always appear on the surface BEHIND a hit pawn.
	bool bImpactOnPawn = false;
	if (BestTarget)
	{
		const FVector PawnEndPoint = BestHitLocation.IsNearlyZero() ? BestTarget->GetActorLocation() : BestHitLocation;
		const float DistToPawn = FVector::Dist(Start, PawnEndPoint);
		const float DistToWall = bHitWall ? FVector::Dist(Start, WallHitResult.ImpactPoint) : TNumericLimits<float>::Max();
		bImpactOnPawn = (DistToPawn < DistToWall);
	}

	if (bImpactOnPawn)
	{
		SpawnImpactEffect(BestHit);
	}
	else if (bHitWall)
	{
		SpawnImpactEffect(WallHitResult);
	}

	// Reflection happens only off a wall, and only if the shot wasn't intercepted by a pawn.
	if (bHitWall && !bImpactOnPawn)
	{
		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		if (MaxReflections > 0 && IsMetal(WallHitResult) && ReflectionCount < MaxReflections)
		{
			FVector ReflectedDir = CalculateReflection(Direction, WallHitResult.ImpactNormal);
			float NewEnergy = RemainingEnergy * (1.0f - ReflectionEnergyLoss);

			UE_LOG(LogTemp, Warning, TEXT("Cone Hitscan: Reflecting off %s (NewEnergy: %.2f)"),
				*WallHitResult.GetActor()->GetName(), NewEnergy);

			SpawnReflectionEffect(WallHitResult.ImpactPoint, Direction, ReflectedDir);

			if (ReflectionSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, ReflectionSound, WallHitResult.ImpactPoint, NewEnergy);
			}

			FVector ReflectionStart = WallHitResult.ImpactPoint + ReflectedDir * 1.0f;
			PerformHitscan(ReflectionStart, ReflectedDir, NewEnergy, ReflectionCount + 1);
		}
	}

	// === DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	/*
	DrawDebugCone(GetWorld(), Start, Direction, MaxDistance, ConeHalfAngleRad, ConeHalfAngleRad,
		12, FColor::Yellow, false, 2.0f, 0, 1.0f);
	DrawDebugSphere(GetWorld(), BeamEnd, ConeRadiusAtEnd, 16, FColor::Cyan, false, 2.0f);
	*/
}

bool AShooterWeapon::IsMetal(const FHitResult& Hit) const
{
	if (MetalMaterials.Num() == 0)
	{
		return false;
	}

	UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
	if (!PhysMat)
	{
		return false;
	}

	return MetalMaterials.Contains(PhysMat);
}

FVector AShooterWeapon::CalculateReflection(const FVector& Direction, const FVector& Normal) const
{
	// R = D - 2(DÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·N)N
	return Direction - 2.0f * FVector::DotProduct(Direction, Normal) * Normal;
}

void AShooterWeapon::ApplyHitscanDamage(const FHitResult& Hit, float EnergyMultiplier, float Distance, float WaveRadius,
	float ExtraDamageMultiplier)
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return;
	}

	// EMF Foliage->Prop conversion: if the trace struck a UEMFConvertibleFoliageType
	// instance, swap the foliage instance for a freshly spawned EMFPhysicsProp
	// before any damage/ionization runs. From here on HitActor refers to the new prop.
	if (AEMFPhysicsProp* ConvertedProp = UFoliageConversionLibrary::TryConvertFoliageInstance(Hit, HitscanDamage))
	{
		HitActor = ConvertedProp;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢
	if (!bHitscanDamageOwner && HitActor == GetOwner())
	{
		return;
	}

	// Read before anything touches the target: the shield's state at the moment the bullet arrived
	// is what the feedback has to describe. Compared against the same reading taken after damage and
	// ionization have run, it also identifies the single shot that took the shield down -- which is
	// the one event in a firefight that tells the player their job just changed.
	const bool bShieldDownBefore = IsTargetShieldDown(HitActor);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	float AreaMultiplier = CalculateDamageMultiplier(Distance, WaveRadius);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° headshot
	bool bIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
	float HeadshotMult = bIsHeadshot ? HeadshotMultiplier : 1.0f;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	float FinalDamage = HitscanDamage * EnergyMultiplier * AreaMultiplier * HeadshotMult * ExtraDamageMultiplier;

	UE_LOG(LogTemp, Warning, TEXT("Hitscan Damage: Base=%.1f x Energy=%.2f x Area=%.2f x HS=%.1f = %.1f to %s (WaveR=%.1f, TargetR=%.1f)"),
		HitscanDamage, EnergyMultiplier, AreaMultiplier, HeadshotMult, FinalDamage,
		*HitActor->GetName(), WaveRadius, TargetEffectiveRadius);


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FDamageEvent DamageEvent;
	if (HitscanDamageType)
	{
		DamageEvent.DamageTypeClass = HitscanDamageType;
	}

	float ActualDamage = ApplyDamageToTarget(HitActor, FinalDamage, DamageEvent);

	bool bKilled = IsActorDeadAfterDamage(HitActor);


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	// Feedback is sent once, at the end of this function, when the shield reading after the shot is
	// known. It used to fire here and again further down for the zero-damage case, which is how the
	// ionizer ended up with a second entrance into the hit marker that no other caller went through.

	// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits.
	// Suppression Fire / future hitscan-on-hit upgrades depend on this firing for the pistol.
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMgr->NotifyWeaponDealtDamage(this, HitActor, ActualDamage, bKilled);
		}
		if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Abilities->NotifyOwnerDealtDamage(HitActor, ActualDamage, bKilled);
		}
	}

	//ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	FVector ImpulseDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();
	float ImpulseForce = HitscanPhysicsForce * EnergyMultiplier * AreaMultiplier;
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// Exceptions (turret, ionizer vs boss) and the grounded rule live in the helper.
		ApplyHitscanKnockback(HitCharacter, ImpulseDirection * ImpulseForce, bUseHitscanIonization);
	}
	else
	{
		// After foliage->prop conversion HitActor != Hit.GetActor(), and the original
		// component is the foliage HISM (no physics). Route the impulse to the freshly
		// spawned prop's PropMesh instead. For non-converted hits this stays Hit.GetComponent().
		UPrimitiveComponent* ImpulseTarget = Hit.GetComponent();
		if (HitActor != Hit.GetActor())
		{
			if (AEMFPhysicsProp* AsProp = Cast<AEMFPhysicsProp>(HitActor))
			{
				ImpulseTarget = AsProp->PropMesh;
			}
		}
		if (ImpulseTarget && ImpulseTarget->IsSimulatingPhysics())
		{
			ImpulseTarget->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, Hit.ImpactPoint);
		}
	}

	// Apply ionization (add positive charge to target). HitComponent gates the NPC-shield rule.
	const bool bIonized = ApplyHitscanIonization(HitActor, Hit.GetComponent());

	// One door for every kind of connection this shot could have been: damage, a headshot, a kill,
	// a shield taken down, or the ionizer's zero-damage charge transfer. Landing on a shield counts
	// on its own: a held shield absorbs the damage entirely, so a weapon that neither hurt nor
	// charged would otherwise hit a shield in total silence.
	if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
	{
		const bool bShieldDownAfter = IsTargetShieldDown(HitActor);

		FHitFeedbackContext Feedback;
		Feedback.HitLocation = Hit.ImpactPoint;
		Feedback.HitDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();
		Feedback.Damage = ActualDamage;
		Feedback.bHeadshot = bIsHeadshot;
		Feedback.bKilled = bKilled;
		Feedback.bShieldHit = !bShieldDownBefore;
		Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
		Feedback.bZeroDamage = (ActualDamage <= 0.0f);
		Feedback.HitActor = HitActor;
		Feedback.FeedbackSet = FeedbackSet;

		WeaponOwner->OnWeaponHitFeedback(Feedback);
	}
}

void AShooterWeapon::PerformSimpleHitscan(const FVector& Start, const FVector& Direction, float EnergyMultiplier)
{
	float TraceDistance = MaxHitscanRange * EnergyMultiplier;
	FVector End = Start + Direction * TraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = true;

	// --- Trace 1: World geometry (walls, floors, damageable props) ---
	// Pawn profile ignores ECC_Visibility, so this only finds world geometry
	FHitResult WallHit;
	bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHit, Start, End, ECC_Visibility, QueryParams);

	float WallDistance = bHitWall ? WallHit.Distance : TraceDistance;

	// Damage non-Pawn damageable actors (e.g. EMFPhysicsProp)
	if (bHitWall && WallHit.GetActor() && !Cast<APawn>(WallHit.GetActor()) && WallHit.GetActor()->CanBeDamaged())
	{
		ApplyHitscanDamage(WallHit, EnergyMultiplier, WallHit.Distance, 0.0f);
	}

	// --- Trace 2: Pawns (player) via ObjectType query ---
	// Pawn collision profile blocks ObjectType queries for ECC_Pawn
	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FVector PawnTraceEnd = Start + Direction * WallDistance; // only trace up to wall
	bool bHitPawn = GetWorld()->LineTraceSingleByObjectType(
		PawnHit, Start, PawnTraceEnd, PawnObjectParams, QueryParams);

	// --- Always fire a dodgeable traveling BOLT (down the aim line) instead of an instant hitscan ---
	// EVERY enemy hitscan shot becomes a projectile-like bolt travelling down the aim line at
	// HitscanBoltSpeed (fast by default). Damage lands only if the player's CURRENT position is
	// inside the moving window when it passes — so the player can dodge by stepping off the line.
	// The Low-Health Defense upgrade slows the bolt via the player's EnemyBoltSlowMultiplier
	// (curve-scaled), making it progressively dodgeable as HP drops.
	// The bolt belongs to the player being shot at: prefer whoever the pawn trace actually hit,
	// and fall back to the player closest to where the shot lands. Using player 0 would apply one
	// teammate's Low-Health Defense to bolts aimed at everybody.
	AShooterCharacter* TargetPlayer = Cast<AShooterCharacter>(PawnHit.GetActor());
	if (!TargetPlayer)
	{
		TargetPlayer = Cast<AShooterCharacter>(
			CoopPlayers::GetNearest(GetWorld(), bHitWall ? WallHit.ImpactPoint : End));
	}

	if (TargetPlayer)
	{
		const float SpeedMult = FMath::Max(TargetPlayer->GetEnemyBoltSlowMultiplier(), 0.01f);
		const float EffSpeed = HitscanBoltSpeed * SpeedMult;
		const float EffVariance = HitscanBoltSpeedVariance * SpeedMult;

		const FVector BoltBeamEnd = bHitWall ? WallHit.ImpactPoint : End;
		const float RandomSeed = FMath::FRand() * 1000.0f;
		const float RandSpeed = FMath::Max(EffSpeed + EffVariance * FMath::Sin(RandomSeed), 1.0f);

		if (UEnemyBeamBoltSubsystem* BoltSys = GetWorld() ? GetWorld()->GetSubsystem<UEnemyBeamBoltSubsystem>() : nullptr)
		{
			BoltSys->RegisterBolt(this, TargetPlayer, Start, Direction, WallDistance,
				RandSpeed, HitscanBoltLength, HitscanBoltRadius, EnergyMultiplier);
		}

		// Tracer matches the bolt exactly (same effective Speed/Variance + RandomSeed pushed to Niagara).
		SpawnBeamEffect(Start, BoltBeamEnd, EnergyMultiplier,
			EffSpeed, EffVariance, HitscanBoltLength, RandomSeed);

		if (bHitWall)
		{
			SpawnImpactEffect(WallHit);
		}

		UE_LOG(LogTemp, Verbose, TEXT("[BOLT_DEBUG] Enemy bolt down aim line — speedMult=%.2f randSpeed=%.0f maxDist=%.0f"),
			SpeedMult, RandSpeed, WallDistance);
		return; // damage is deferred to the bolt subsystem (dodgeable)
	}
	// (No local player resolved → fall through to the instant hitscan path below as a safety fallback.)

	// --- Determine beam endpoint and apply pawn damage ---
	FVector BeamEnd;
	bool bPawnWasHit = false;

	if (bHitPawn && PawnHit.GetActor() && PawnHit.GetActor()->CanBeDamaged())
	{
		// Pawn hit is always closer than wall (we traced up to wall distance)
		ApplyHitscanDamage(PawnHit, EnergyMultiplier, PawnHit.Distance, 0.0f);
		bPawnWasHit = true;

		// Beam goes to wall (or max range), not stopping at pawn
		BeamEnd = bHitWall ? WallHit.ImpactPoint : End;

		UE_LOG(LogTemp, Warning, TEXT("[NPC Hitscan] HIT PAWN: %s at dist=%.0f"),
			*PawnHit.GetActor()->GetName(), PawnHit.Distance);
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			FString::Printf(TEXT("[NPC] DMG -> %s (%.0f)"), *PawnHit.GetActor()->GetName(), PawnHit.Distance));
	}
	else
	{
		BeamEnd = bHitWall ? WallHit.ImpactPoint : End;

		if (bHitWall)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC Hitscan] Hit wall: %s at dist=%.0f (no pawn hit)"),
				*WallHit.GetActor()->GetName(), WallHit.Distance);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPC Hitscan] MISS: nothing hit"));
		}
	}

	// Visual effects
	SpawnBeamEffect(Start, BeamEnd, EnergyMultiplier);

	// Spawn impact: prefer pawn hit (closer), otherwise the wall behind it.
	// Without this, impact would always appear on the wall — even when a pawn intercepted the shot.
	if (bPawnWasHit)
	{
		SpawnImpactEffect(PawnHit);
	}
	else if (bHitWall)
	{
		SpawnImpactEffect(WallHit);
	}
}

void AShooterWeapon::PerformClassicHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount)
{
	const float SegmentMaxDistance = MaxHitscanRange * RemainingEnergy;
	const FVector End = Start + Direction * SegmentMaxDistance;
	const float SweepRadius = FMath::Max(InitialWaveRadius, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = true;

	// --- Trace 1: world geometry (pawn profiles ignore ECC_Visibility) ---
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, QueryParams);
	const float WallDistance = bHitWall ? WallHit.Distance : SegmentMaxDistance;

	// Damage non-pawn damageable actors (EMFPhysicsProp, convertible foliage) — same rule as the cone path.
	// A travelling shot does not do this here: the prop is only hit when the bolt reaches it, so both
	// the damage and the impact effect wait and are done by the bolt on arrival.
	if (!bHitscanTravelsAsBolt
		&& bHitWall && WallHit.GetActor() && !Cast<APawn>(WallHit.GetActor()) && WallHit.GetActor()->CanBeDamaged())
	{
		ApplyHitscanDamage(WallHit, RemainingEnergy, WallHit.Distance, 0.0f);
	}

	// --- Trace 2: thin pawn sweep up to the wall ---
	// The swept volume is a thin capsule along the ray: SweepRadius units of forgiveness.
	TArray<FHitResult> PawnHits;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	GetWorld()->SweepMultiByObjectType(
		PawnHits,
		Start,
		Start + Direction * WallDistance,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(SweepRadius),
		QueryParams);

	UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] === ClassicShot: Start=%s Dir=%s | SweepR=%.1f | Wall=%s dist=%.0f | pawnHits=%d refl=%d"),
		*Start.ToCompactString(), *Direction.ToCompactString(), SweepRadius,
		bHitWall ? *GetNameSafe(WallHit.GetActor()) : TEXT("none"),
		WallDistance, PawnHits.Num(), ReflectionCount);

	// --- Pick the NEAREST pawn on the ray: a bullet stops at the first body ---
	int32 BestIndex = INDEX_NONE;
	float BestDistance = MAX_FLT;
	for (int32 i = 0; i < PawnHits.Num(); ++i)
	{
		const FHitResult& Hit = PawnHits[i];
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || !HitActor->CanBeDamaged())
		{
			continue;
		}

		// Initial-overlap sweep hits report Distance = 0 — use the actor location instead
		float Dist = Hit.Distance;
		if (Dist < 1.0f)
		{
			Dist = FVector::Dist(Start, HitActor->GetActorLocation());
		}

		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG]   cand=%s comp=%s bone=%s dist=%.0f"),
			*HitActor->GetName(), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(), Dist);

		if (Dist < BestDistance)
		{
			BestDistance = Dist;
			BestIndex = i;
		}
	}

	// One seed for this shot. The bolt's speed and the tracer's speed are derived from it the same
	// way (Speed + Variance * sin(Seed)), which is what keeps the streak sitting on the damage
	// region instead of merely resembling it. Negative means this weapon does not travel.
	const float BoltRandomSeed = bHitscanTravelsAsBolt ? FMath::FRand() * 1000.0f : -1.0f;

	// Filled in below when the shot travels, then handed to the bolt in one place, so a shot on
	// course to hit nobody is registered the same way as one that is: it still has to arrive
	// somewhere before it is allowed to mark the wall.
	AActor* BoltVictim = nullptr;
	float BoltDamageMultiplier = 1.0f;
	FName BoltHitBone = NAME_None;

	// --- Apply damage to the nearest pawn with the full player multiplier stack (as in the cone path) ---
	bool bPawnWasHit = false;
	FVector PawnHitLocation = FVector::ZeroVector;
	FHitResult PawnHit;
	if (BestIndex != INDEX_NONE)
	{
		PawnHit = PawnHits[BestIndex];
		AActor* HitActor = PawnHit.GetActor();
		bPawnWasHit = true;
		PawnHitLocation = PawnHit.ImpactPoint.IsNearlyZero() ? HitActor->GetActorLocation() : FVector(PawnHit.ImpactPoint);

		const bool bIsHeadshot = (PawnHit.BoneName == FName("head") || PawnHit.BoneName == FName("Head"));
		const float HeadshotMult = bIsHeadshot ? HeadshotMultiplier : 1.0f;
		const float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

		float ZFactorMult = 1.0f;
		if (bUseZFactor && PawnOwner)
		{
			ZFactorMult = CalculateZFactorMultiplier(PawnOwner->GetActorLocation().Z, HitActor->GetActorLocation().Z);
		}

		const float TagMult = GetTagDamageMultiplier(HitActor);

		float UpgradeMult = 1.0f;
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(HitActor);
			}
		}

		// A weapon whose hits travel does not land this one now. Everything the shot knows and the
		// arrival cannot work out for itself is written down here and handed to the bolt below: the
		// multiplier stack, and the bone the pellet was on course for so a headshot is still a
		// headshot when it gets there.
		if (bHitscanTravelsAsBolt)
		{
			BoltVictim = HitActor;
			BoltDamageMultiplier = HeatMult * ZFactorMult * TagMult * UpgradeMult;
			BoltHitBone = PawnHit.BoneName;
		}
		else
		{
			// No distance falloff: at zero divergence the wave radius never exceeds the target
			// radius, so the cone path's area multiplier would be 1.0 anyway.
			const float FinalDamage = HitscanDamage * RemainingEnergy * HeadshotMult * HeatMult * ZFactorMult * TagMult * UpgradeMult;

			FDamageEvent DamageEvent;
			if (HitscanDamageType)
			{
				DamageEvent.DamageTypeClass = HitscanDamageType;
			}

			// The shield as it stood when the bullet arrived. @see ApplyHitscanDamage.
			const bool bShieldDownBefore = IsTargetShieldDown(HitActor);

			const float ActualDamage = ApplyDamageToTarget(HitActor, FinalDamage, DamageEvent);
			const bool bKilled = IsActorDeadAfterDamage(HitActor);

			UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] APPLIED(classic): target=%s dist=%.0f dealt=%.1f applied=%.1f killed=%d"),
				*HitActor->GetName(), BestDistance, FinalDamage, ActualDamage, bKilled ? 1 : 0);

			// Feedback goes out once, at the end of this block. @see ApplyHitscanDamage.

			// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits
			if (PawnOwner)
			{
				if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
				{
					UpgradeMgr->NotifyWeaponDealtDamage(this, HitActor, ActualDamage, bKilled);
				}
				if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
				{
					Abilities->NotifyOwnerDealtDamage(HitActor, ActualDamage, bKilled);
				}
			}

			// Knockback / physics impulse — same rules as the cone path, see ApplyHitscanKnockback.
			const float ImpulseForce = HitscanPhysicsForce * RemainingEnergy;
			if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
			{
				ApplyHitscanKnockback(HitCharacter, Direction * ImpulseForce, bUseHitscanIonization);
			}
			else if (UPrimitiveComponent* HitComp = PawnHit.GetComponent())
			{
				if (HitComp->IsSimulatingPhysics())
				{
					HitComp->AddImpulseAtLocation(Direction * ImpulseForce, PawnHitLocation);
				}
			}

			// Ionization (charge transfer); HitComponent gates the NPC riot-shield rule
			const bool bIonized = ApplyHitscanIonization(HitActor, PawnHit.GetComponent());

			if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
			{
				const bool bShieldDownAfter = IsTargetShieldDown(HitActor);

				FHitFeedbackContext Feedback;
				Feedback.HitLocation = PawnHitLocation;
				Feedback.HitDirection = Direction;
				Feedback.Damage = ActualDamage;
				Feedback.bHeadshot = bIsHeadshot;
				Feedback.bKilled = bKilled;
				Feedback.bShieldHit = !bShieldDownBefore;
				Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
				Feedback.bZeroDamage = (ActualDamage <= 0.0f);
				Feedback.HitActor = HitActor;
				Feedback.FeedbackSet = FeedbackSet;

				WeaponOwner->OnWeaponHitFeedback(Feedback);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] NO DAMAGE THIS SHOT (classic): pawnHits=%d (beam to %s)"),
			PawnHits.Num(), bHitWall ? *GetNameSafe(WallHit.GetActor()) : TEXT("max range"));
	}

	// --- Visuals: the trace runs from the camera, but the beam must leave the MUZZLE ---
	// Reflected segments (ReflectionCount > 0) start at the bounce point instead.
	FVector BeamStart = Start;
	if (ReflectionCount == 0 && PawnOwner && PawnOwner->IsPlayerControlled() && FirstPersonMesh)
	{
		// Where the barrel LOOKS like it is, not where it stands: the first-person mesh is rendered
		// through a transform of its own, and a tracer put at the socket's real position starts off
		// to the side of the gun you can see. See GetFirstPersonMuzzleRenderLocation.
		BeamStart = GetFirstPersonMuzzleRenderLocation();
	}

	// Does the socket's WORLD position land on the barrel you can see? The yellow sphere is drawn
	// as ordinary world geometry, so if it sits on the barrel tip on screen, the first-person mesh
	// is rendered where its transform says and the tracer start is right; if the sphere is off to
	// the side of the gun, the mesh is being drawn through a transform this code does not know
	// about. Red sphere = MuzzleSocketName does not exist on this mesh and GetSocketLocation
	// quietly handed back the component origin.
	if (bDrawHitscanDebug && FirstPersonMesh)
	{
		const bool bSocketExists = FirstPersonMesh->DoesSocketExist(MuzzleSocketName);
		const FVector SocketWorld = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
		DrawDebugSphere(GetWorld(), SocketWorld, 3.0f, 8,
			bSocketExists ? FColor::Yellow : FColor::Red, false, 5.0f);
		DrawDebugSphere(GetWorld(), BeamStart, 2.0f, 8, FColor::Magenta, false, 5.0f);

		const APlayerController* DebugPC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
		const FMinimalViewInfo DebugPOV = (DebugPC && DebugPC->PlayerCameraManager)
			? DebugPC->PlayerCameraManager->GetCameraCacheView()
			: FMinimalViewInfo();
		UE_LOG(LogTemp, Warning, TEXT("[MUZZLE_DEBUG] socketExists=%d socket=%s beamStart=%s | POV loc=%s useFPParams=%d fpScale=%.3f fov=%.1f fpFov=%.1f"),
			bSocketExists ? 1 : 0,
			*SocketWorld.ToCompactString(),
			*BeamStart.ToCompactString(),
			*DebugPOV.Location.ToCompactString(),
			DebugPOV.bUseFirstPersonParameters ? 1 : 0,
			DebugPOV.FirstPersonScale,
			DebugPOV.FOV,
			DebugPOV.FirstPersonFOV);
	}

	// A travelling shot is drawn along its whole line for the same reason it flies it: where it
	// actually stops is not decided yet. An instant one still stops at the body it hit.
	const FVector BeamEnd = (bPawnWasHit && !bHitscanTravelsAsBolt)
		? PawnHitLocation
		: (bHitWall ? FVector(WallHit.ImpactPoint) : End);

	// --- Visual debug: the real bullet path vs the visual tracer ---
	if (bDrawHitscanDebug)
	{
		const float DebugDuration = 5.0f;

		// Actual trace ray from the camera (this is where damage is decided)
		DrawDebugLine(GetWorld(), Start, BeamEnd, FColor::Cyan, false, DebugDuration, 0, 0.5f);

		// Thin sweep corridor: green = pawn damaged, red = nothing damaged.
		// Capped in length on purpose. Drawn to the full trace distance it is a 12-sided cylinder
		// a kilometre long whose near end sits a metre from the eye, and its side lines fill the
		// whole screen with a red starburst that hides everything else in here.
		const float DebugCorridorLength = FMath::Min(WallDistance, 1000.0f);
		DrawDebugCylinder(GetWorld(), Start, Start + Direction * DebugCorridorLength, SweepRadius, 12,
			bPawnWasHit ? FColor::Green : FColor::Red, false, DebugDuration, 0, 0.75f);

		// Visual tracer line from the muzzle — the gap to the cyan ray is the muzzle parallax
		DrawDebugLine(GetWorld(), BeamStart, BeamEnd, FColor::White, false, DebugDuration, 0, 0.25f);

		// Wall hit point
		if (bHitWall)
		{
			DrawDebugSphere(GetWorld(), WallHit.ImpactPoint, 8.0f, 8, FColor::Red, false, DebugDuration);
		}

		// Pawn candidates: orange = found by the sweep, green = the one that took the damage
		for (int32 i = 0; i < PawnHits.Num(); ++i)
		{
			const FHitResult& Hit = PawnHits[i];
			if (!Hit.GetActor())
			{
				continue;
			}
			const FVector Loc = Hit.ImpactPoint.IsNearlyZero() ? Hit.GetActor()->GetActorLocation() : FVector(Hit.ImpactPoint);
			if (i == BestIndex)
			{
				DrawDebugSphere(GetWorld(), Loc, 14.0f, 12, FColor::Green, false, DebugDuration);
			}
			else
			{
				DrawDebugSphere(GetWorld(), Loc, 8.0f, 8, FColor::Orange, false, DebugDuration);
			}
		}
	}

	// --- Send the shot on its way, tracer and damage as one thing ---
	//
	// The tracer is timed off the bolt: same speed, same variance, same length, same seed, so it is
	// not a streak that resembles the pellet, it IS the pellet. The bolt is then handed that streak
	// and puts it out where the pellet actually stops, which is the only place that knows.
	//
	// The line it flies is the whole line, not just as far as whoever happens to be standing on it:
	// that pawn may step aside before it arrives, and then the pellet carries on into the wall
	// behind them and marks that instead.
	if (bHitscanTravelsAsBolt)
	{
		const float RandSpeed = FMath::Max(
			HitscanBoltSpeed + HitscanBoltSpeedVariance * FMath::Sin(BoltRandomSeed), 1.0f);

		UNiagaraComponent* Tracer = SpawnBeamEffect(BeamStart, BeamEnd, RemainingEnergy,
			HitscanBoltSpeed, HitscanBoltSpeedVariance, HitscanBoltLength, BoltRandomSeed);

		if (UEnemyBeamBoltSubsystem* BoltSys = GetWorld()->GetSubsystem<UEnemyBeamBoltSubsystem>())
		{
			BoltSys->RegisterBolt(this, BoltVictim, Start, Direction, WallDistance, RandSpeed,
				HitscanBoltLength, HitscanBoltRadius, RemainingEnergy,
				BoltDamageMultiplier, BoltHitBone, WallHit, bHitWall, Tracer);
		}

		UE_LOG(LogTemp, Warning, TEXT("[BOLT_DEBUG] %s: pellet away, target=%s line=%.0f speed=%.0f arrives in %.3fs"),
			*GetName(), *GetNameSafe(BoltVictim), WallDistance, RandSpeed, WallDistance / RandSpeed);
	}
	else
	{
		SpawnBeamEffect(BeamStart, BeamEnd, RemainingEnergy);
	}

	if (bUseWaveVisualization)
	{
		SpawnWaveFronts(BeamStart, BeamEnd);
	}

	// The impact of a travelling shot is the bolt's business: it plays where the pellet stops and
	// at the moment it stops there. Playing it now would put a hole in a wall the pellet has not
	// reached, on a target that may yet step out of the way.
	if (!bHitscanTravelsAsBolt)
	{
		if (bPawnWasHit)
		{
			SpawnImpactEffect(PawnHit);
		}
		else if (bHitWall)
		{
			SpawnImpactEffect(WallHit);
		}
	}

	// --- Metal reflection: only off a wall and only if no pawn intercepted the ray ---
	if (bHitWall && !bPawnWasHit && MaxReflections > 0 && IsMetal(WallHit) && ReflectionCount < MaxReflections)
	{
		const FVector ReflectedDir = CalculateReflection(Direction, WallHit.ImpactNormal);
		const float NewEnergy = RemainingEnergy * (1.0f - ReflectionEnergyLoss);

		UE_LOG(LogTemp, Warning, TEXT("[HITSCAN_DEBUG] Classic reflection off %s (NewEnergy: %.2f)"),
			*GetNameSafe(WallHit.GetActor()), NewEnergy);

		SpawnReflectionEffect(WallHit.ImpactPoint, Direction, ReflectedDir);

		if (ReflectionSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ReflectionSound, WallHit.ImpactPoint, NewEnergy);
		}

		PerformClassicHitscan(WallHit.ImpactPoint + ReflectedDir * 1.0f, ReflectedDir, NewEnergy, ReflectionCount + 1);
	}
}

float AShooterWeapon::GetTagDamageMultiplier(AActor* Target) const
{
	if (!Target || TagDamageMultipliers.Num() == 0)
	{
		return 1.0f;
	}

	float Multiplier = 1.0f;

	for (const auto& Pair : TagDamageMultipliers)
	{
		if (Target->ActorHasTag(Pair.Key))
		{
			Multiplier *= Pair.Value;
		}
	}

	return Multiplier;
}

// ==================== Reload ====================
//
// Ammunition is counted by whichever machine pulls the trigger: CurrentBullets is not replicated,
// and the server's copy of a client's weapon never decrements (see Server_ReportDamage). A reload
// follows the same rule -- it runs where the shooting is being counted, and needs no RPC of its own.
// What the authority alone decides, a granted magazine, still comes down through Client_SyncAmmoState.

bool AShooterWeapon::CanReload() const
{
	// A yanked weapon is thrown away when it runs dry rather than reloaded, whatever bUseReload says.
	return bUseReload
		&& !bHasLimitedAmmo
		&& !bIsReloading
		&& CurrentBullets < MagazineSize;
}

bool AShooterWeapon::StartReload()
{
	if (!CanReload())
	{
		return false;
	}

	bIsReloading = true;

	// The refire timer would fire mid-reload and Fire() would bounce off bIsReloading, but a pending
	// shot surviving the reload is confusing to debug. Clear it and let FinishReload restart fire.
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);

	if (ReloadMontage && WeaponOwner)
	{
		WeaponOwner->PlayReloadMontage(ReloadMontage);
	}

	// The weapon's own reload: the magazine coming out, the pump, the shells going in. Played here
	// first, so the reloading player gets it with no round trip, then sent to everyone else, whose
	// only copy of this weapon is the third person mesh.
	PlayReloadEffectsLocally();

	if (HasAuthority())
	{
		Multicast_PlayReloadEffects();
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		// A client reloads on its own copy of the weapon (ammo is counted by whoever pulls the
		// trigger), so the server has to be told before it can show anyone else.
		OwnerCharacter->Server_ReportWeaponReloaded(this);
	}

	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &AShooterWeapon::FinishReload, ReloadTime, false);

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: reload started, %d/%d rounds, %.2fs"),
		*GetName(), CurrentBullets, MagazineSize, ReloadTime);

	return true;
}

void AShooterWeapon::FinishReload()
{
	bIsReloading = false;
	CurrentBullets = MagazineSize;

	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: reload finished, %d rounds"), *GetName(), CurrentBullets);

	// The trigger was never released, so the weapon picks up where it left off. Semi-automatic
	// weapons deliberately do not: one pull is one shot, reload or no reload.
	if (bIsFiring && bFullAuto)
	{
		Fire();
	}
}

void AShooterWeapon::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	bIsReloading = false;
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: reload cancelled at %d/%d rounds"),
		*GetName(), CurrentBullets, MagazineSize);
}

float AShooterWeapon::GetReloadProgress() const
{
	if (!bIsReloading || ReloadTime <= 0.0f)
	{
		return 0.0f;
	}

	const float Remaining = GetWorld()->GetTimerManager().GetTimerRemaining(ReloadTimer);
	return FMath::Clamp(1.0f - (Remaining / ReloadTime), 0.0f, 1.0f);
}

void AShooterWeapon::SetBulletCount(int32 NewCount)
{
	CurrentBullets = FMath::Clamp(NewCount, 0, MagazineSize);

	// The authority handing out a magazine is the one case the owning client cannot work out for
	// itself. Push it, along with whether this weapon refills or runs dry.
	if (HasAuthority())
	{
		Client_SyncAmmoState(CurrentBullets, bHasLimitedAmmo);
	}
}

void AShooterWeapon::Client_SyncAmmoState_Implementation(int32 InBullets, bool bInHasLimitedAmmo)
{
	CurrentBullets = FMath::Clamp(InBullets, 0, MagazineSize);
	bHasLimitedAmmo = bInHasLimitedAmmo;

	// The HUD reads this through the character, and only for the weapon actually in hand.
	if (PawnOwner)
	{
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			if (ShooterOwner->GetCurrentWeapon() == this)
			{
				ShooterOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
			}
		}
	}
}

bool AShooterWeapon::IsIonizationCapReached(float CurrentCharge, float Cap) const
{
	return IsIonizationCapReached(CurrentCharge, Cap, IonizationChargePerHit);
}

bool AShooterWeapon::IsIonizationCapReached(float CurrentCharge, float Cap, float ChargePerHit) const
{
	// A cap is a MAGNITUDE, and this used to be tested as "CurrentCharge >= Max". With a negative
	// IonizationChargePerHit -- the default for the electrifying weapons -- a target's charge only
	// ever went down, so that test was never true and the Min() alongside it never clamped anything:
	// the charge ran away with no ceiling at all. "Fully charged" was therefore not a state anything
	// could reach, which is what made props grabbable at charges nowhere near maximum.
	const float AbsCap = FMath::Abs(Cap);
	if (AbsCap <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const bool bSameDirection = (CurrentCharge * ChargePerHit) > 0.0f;
	return bSameDirection && FMath::Abs(CurrentCharge) >= AbsCap;
}

float AShooterWeapon::ApplyIonizationStep(float CurrentCharge, float Cap) const
{
	return ApplyIonizationStep(CurrentCharge, Cap, IonizationChargePerHit);
}

float AShooterWeapon::ApplyIonizationStep(float CurrentCharge, float Cap, float ChargePerHit) const
{
	const float AbsCap = FMath::Abs(Cap);
	const float Stepped = CurrentCharge + ChargePerHit;
	return AbsCap > KINDA_SMALL_NUMBER ? FMath::Clamp(Stepped, -AbsCap, AbsCap) : Stepped;
}

bool AShooterWeapon::ShouldWithholdDamageForShield(AActor* Target) const
{
	return bRequiresBrokenShieldToDamage && !IsTargetShieldDown(Target);
}

bool AShooterWeapon::ApplyHitscanIonization(AActor* Target, UPrimitiveComponent* HitComponent)
{
	UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ApplyHitscanIonization called: target=%s hitComp=%s bUseHitscanIonization=%d"),
		*GetNameSafe(Target),
		HitComponent ? *HitComponent->GetName() : TEXT("null"),
		bUseHitscanIonization);

	if (!bUseHitscanIonization)
	{
		return false;
	}

	return ApplyIonizationToTarget(Target, HitComponent, IonizationChargePerHit);
}

bool AShooterWeapon::ApplyIonizationToTarget(AActor* Target, UPrimitiveComponent* HitComponent, float ChargePerHit)
{
	if (!Target)
	{
		return false;
	}

	// NPC riot-shield rule: hit on body while shield is up → no charge transfer.
	// Player must hit the shield mesh to electrify the NPC behind it.
	if (UNPCRiotShieldComponent::ShouldBlockBodyIonization(Target, HitComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ApplyHitscanIonization: BLOCKED by shield rule"));
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ION_DEBUG] ApplyHitscanIonization: PASSED, applying charge to %s"), *Target->GetName());

	// Charging something is a change to the world, and until now a client only ever made it to its
	// own copy: ionization carries no damage, so it never travelled with a damage report, and the
	// starting weapon deals no damage at all. Tell the server. The local application below still
	// runs, so the shooter sees the prop light up immediately, and the authority's value replicates
	// back over the top of it a round trip later.
	if (PawnOwner && !PawnOwner->HasAuthority())
	{
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			ShooterOwner->Server_ReportIonization(Target, this);
		}
	}

	// Notify upgrade system of ionization-eligible hit. Fires once per valid hit regardless
	// of whether the target was already at max charge — upgrades (e.g. PistolStun) gate
	// per-target spam themselves via their own cooldowns.
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMgr->NotifyOwnerHitscanIonized(Target);
		}
	}

	// While an enemy is opened by the Wizard's bolt, the ionization this shot would have put into its
	// shield goes into its health instead.
	//
	// This is the SECOND place that has to know: the laser has its own per-second ionization path and
	// every other weapon comes through here per hit. Hooking only the beam meant the mechanic worked
	// for exactly one weapon nobody was holding.
	if (AShooterNPC* OpenedNPC = Cast<AShooterNPC>(Target))
	{
		if (OpenedNPC->IsShieldBypassed())
		{
			if (HasAuthority())
			{
				// Magnitude: ionization is signed, and a negative rate multiplied through produces
				// negative damage, which TakeDamage silently discards.
				const float RedirectedDamage = FMath::Abs(ChargePerHit)
					* OpenedNPC->ShieldBypassDamageMultiplier;

				FPointDamageEvent DamageEvent;
				DamageEvent.DamageTypeClass = UDamageType::StaticClass();
				OpenedNPC->TakeDamage(RedirectedDamage, DamageEvent, GetInstigatorController(), this);

				UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Redirected %.1f ionization into health on %s"),
					RedirectedDamage, *OpenedNPC->GetName());
			}
			return true;
		}
	}

	// Try UEMFVelocityModifier first (for characters/NPCs)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		// Use GetCharge() to read actual FieldComponent charge (not BaseCharge which may be stale
		// after melee's SetCharge() calls that bypass BaseCharge tracking)
		const float CurrentCharge = TargetModifier->GetCharge();

		// The NPC's own ceiling, the same one IsAtMaxCharge() and the grab gate read.
		const float Cap = TargetModifier->MaxBaseCharge;
		if (IsIonizationCapReached(CurrentCharge, Cap, ChargePerHit))
		{
			return false;
		}

		TargetModifier->SetCharge(ApplyIonizationStep(CurrentCharge, Cap, ChargePerHit));
		return true;
	}

	// Route through SetCharge() for props (enables physics on first charge)
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		// The prop's own ceiling. What the weapon can push to is the prop's business, not the weapon's.
		const float CurrentCharge = Prop->GetCharge();
		if (IsIonizationCapReached(CurrentCharge, Prop->MaxCharge, ChargePerHit))
		{
			return false;
		}
		Prop->SetCharge(ApplyIonizationStep(CurrentCharge, Prop->MaxCharge, ChargePerHit));
		return true;
	}

	// Generic fallback: raw UEMF_FieldComponent
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;

		// A bare field component has no cap of its own, so the weapon's number is the only ceiling
		// available on this path.
		if (IsIonizationCapReached(CurrentCharge, MaxIonizationCharge, ChargePerHit))
		{
			return false;
		}

		Desc.PointChargeParams.Charge = ApplyIonizationStep(CurrentCharge, MaxIonizationCharge, ChargePerHit);
		TargetField->SetSourceDescription(Desc);
		return true;
	}

	return false;
}

float AShooterWeapon::CalculateWaveRadius(float Distance) const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ WaveDivergence
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 0, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = 0 (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 1, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = MaxDivergenceAngle
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â + ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	float Radius = InitialWaveRadius + Distance * TangentAngle;

	return Radius;
}

float AShooterWeapon::CalculateDamageMultiplier(float Distance, float WaveRadius) const
{
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ <= ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢
	if (WaveRadius <= TargetEffectiveRadius)
	{
		return 1.0f;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ = (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ / ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ~ RÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â², ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ = (TargetRadius / WaveRadius)ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â²
	float AreaRatio = (TargetEffectiveRadius * TargetEffectiveRadius) / (WaveRadius * WaveRadius);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	return FMath::Max(AreaRatio, MinDamageMultiplier);
}

// ==================== VFX ====================

float AShooterWeapon::GetOwnerCharge() const
{
	if (!PawnOwner)
	{
		return 0.0f;
	}

	UEMF_FieldComponent* FieldComp = PawnOwner->FindComponentByClass<UEMF_FieldComponent>();
	if (!FieldComp)
	{
		return 0.0f;
	}

	return FieldComp->GetSourceDescription().PointChargeParams.Charge;
}

void AShooterWeapon::SpawnMuzzleFlashEffect()
{
	// Determine which VFX to use
	UNiagaraSystem* VFXToSpawn = MuzzleFlashFX;

	// Check if charge-based muzzle flash is enabled
	if (bUseChargeMuzzleFlash)
	{
		float OwnerCharge = GetOwnerCharge();

		if (OwnerCharge > 0.0f && PositiveMuzzleFlashFX)
		{
			VFXToSpawn = PositiveMuzzleFlashFX;
		}
		else if (OwnerCharge < 0.0f && NegativeMuzzleFlashFX)
		{
			VFXToSpawn = NegativeMuzzleFlashFX;
		}
		// If charge is neutral or appropriate VFX is not set, fall back to default MuzzleFlashFX
	}

	if (!VFXToSpawn)
	{
		return;
	}

	// Spawn attached to muzzle socket so VFX follows weapon movement
	UNiagaraComponent* MuzzleComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFXToSpawn,
		FirstPersonMesh,
		MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(MuzzleFlashScale),
		EAttachLocation::SnapToTarget,
		true,
		ENCPoolMethod::None
	);

	if (MuzzleComp)
	{
		// Set muzzle flash parameters
		MuzzleComp->SetColorParameter(FName("FlashColor"), MuzzleFlashColor);
		MuzzleComp->SetFloatParameter(FName("Intensity"), MuzzleFlashIntensity);
		MuzzleComp->SetFloatParameter(FName("Duration"), MuzzleFlashDuration);

		// Pass wave-specific parameters if using wave visualization
		if (bUseWaveVisualization)
		{
			MuzzleComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			MuzzleComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			MuzzleComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			MuzzleComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}

		// Pass beam color for consistency
		MuzzleComp->SetColorParameter(FName("BeamColor"), BeamColor);
	}
}

UNiagaraComponent* AShooterWeapon::SpawnBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier,
	float OverrideBoltSpeed, float OverrideBoltSpeedVariance, float OverrideBoltLength, float OverrideRandomSeed)
{
	// Draw it here immediately, then make sure everyone else draws it too. Same split as the
	// muzzle flash: the shooter must not wait a round trip to see their own tracer.
	UNiagaraComponent* LocalBeam = SpawnBeamEffectLocally(Start, End, EnergyMultiplier,
		OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);

	if (HasAuthority())
	{
		Multicast_PlayBeamEffect(Start, End, EnergyMultiplier,
			OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		OwnerCharacter->Server_ReportBeamEffect(this, Start, End, EnergyMultiplier,
			OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
	}

	return LocalBeam;
}

void AShooterWeapon::Multicast_PlayBeamEffect_Implementation(const FVector& Start, const FVector& End,
	float EnergyMultiplier, float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
	float OverrideBoltLength, float OverrideRandomSeed)
{
	// The shooter already drew it the instant they fired.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (bIsShooter)
	{
		return;
	}

	// Start over from OUR muzzle. The incoming Start came from the shooter's FIRST-person mesh,
	// which hangs off their camera and is only ever visible to them: replayed here it puts the
	// tracer somewhere around a teammate's head instead of at the gun we can actually see. The
	// endpoint is genuine world data and is kept as sent.
	FVector ObserverStart = Start;
	if (ThirdPersonMesh)
	{
		ObserverStart = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
	}

	SpawnBeamEffectLocally(ObserverStart, End, EnergyMultiplier,
		OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
}

UNiagaraComponent* AShooterWeapon::SpawnBeamEffectLocally(const FVector& Start, const FVector& End, float EnergyMultiplier,
	float OverrideBoltSpeed, float OverrideBoltSpeedVariance, float OverrideBoltLength, float OverrideRandomSeed)
{
	if (!BeamFX)
	{
		return nullptr;
	}

	// Spawned INACTIVE on purpose. Activating first and setting the endpoints afterwards is what the
	// engine turns into SetVariable_Deferred (NiagaraComponent.cpp): once a system instance exists,
	// a parameter write only lands on the NEXT tick, so the first simulated frame runs on the
	// asset's defaults -- BeamStart and BeamEnd both zero. That is the stray tracer that starts
	// nowhere near the muzzle and runs off to the horizon, and it shows up on some shots and not
	// others because it depends on where the frame boundary falls. Set everything, then activate.
	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		BeamFX,
		Start,
		(End - Start).Rotation(),
		FVector::OneVector,
		/*bAutoDestroy*/ true,
		/*bAutoActivate*/ false,
		ENCPoolMethod::None
	);

	if (BeamComp)
	{
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		BeamComp->SetVectorParameter(FName("BeamStart"), Start);
		BeamComp->SetVectorParameter(FName("BeamEnd"), End);
		BeamComp->SetFloatParameter(FName("Energy"), EnergyMultiplier);
		BeamComp->SetColorParameter(FName("BeamColor"), BeamColor);
		BeamComp->SetFloatParameter(FName("Distance"), FVector::Dist(Start, End));

		const float UsedRandomSeed = (OverrideRandomSeed >= 0.0f) ? OverrideRandomSeed : (FMath::FRand() * 1000.0f);
		BeamComp->SetFloatParameter(FName("RandomSeed"), UsedRandomSeed);

		// Low-HP dodgeable bolt: override the tracer's Speed / SpeedVariance / beamLength so the
		// visible bolt matches the C++ damage region (UEnemyBeamBoltSubsystem). Requires the enemy
		// beam Niagara asset to read these as User parameters and feed them into its HLSL node.
		if (OverrideBoltSpeed >= 0.0f)
		{
			BeamComp->SetFloatParameter(FName("Speed"), OverrideBoltSpeed);
			BeamComp->SetFloatParameter(FName("SpeedVariance"), OverrideBoltSpeedVariance);
			BeamComp->SetFloatParameter(FName("beamLength"), OverrideBoltLength);

			// And it goes out when it gets there. The streak flies at the speed the HLSL will
			// compute from this seed, so how long the flight takes is known here: past that moment
			// the bolt has either hit or been dodged, and a streak still crawling along an empty
			// line is a lie either way. In the tracer asset "BeamFadeTime" is the particle lifetime
			// and "FadeTime" the emitter's loop duration, so both are the flight.
			const float RandSpeed = FMath::Max(
				OverrideBoltSpeed + OverrideBoltSpeedVariance * FMath::Sin(UsedRandomSeed), 1.0f);
			const float FlightTime = FVector::Dist(Start, End) / RandSpeed;

			BeamComp->SetFloatParameter(FName("BeamFadeTime"), FlightTime);
			BeamComp->SetFloatParameter(FName("FadeTime"), FlightTime);
		}
		UE_LOG(LogTemp, Warning, TEXT("BeamFX Distance: %.1f, Start: %s, End: %s"), FVector::Dist(Start, End), *Start.ToString(), *End.ToString());

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
		FVector UpVector = FVector::UpVector;
		FVector RightVector = FVector::RightVector;

		if (PawnOwner)
		{
			if (AController* Controller = PawnOwner->GetController())
			{
				FRotator CameraRotation;
				FVector CameraLocation;
				Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);

				UpVector = CameraRotation.Quaternion().GetUpVector();
				RightVector = CameraRotation.Quaternion().GetRightVector();
			}
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		BeamComp->SetVectorParameter(FName("UpVector"), UpVector);
		BeamComp->SetVectorParameter(FName("RightVector"), RightVector);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float BeamDistance = FVector::Distance(Start, End);
		float StartRadius = CalculateWaveRadius(0.0f);
		float EndRadius = CalculateWaveRadius(BeamDistance);

		BeamComp->SetFloatParameter(FName("StartRadius"), StartRadius);
		BeamComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		BeamComp->SetFloatParameter(FName("MaxDivergenceAngle"), MaxDivergenceAngle);
		BeamComp->SetFloatParameter(FName("TargetRadius"), TargetEffectiveRadius);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
		BeamComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		BeamComp->SetFloatParameter(FName("MaxRange"), MaxHitscanRange);
		BeamComp->SetFloatParameter(FName("MinEnergy"), MinDamageMultiplier);

		// Wave-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		if (bUseWaveVisualization)
		{
			BeamComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			BeamComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			BeamComp->SetFloatParameter(FName("FadeTime"), BeamFadeTime);
			BeamComp->SetFloatParameter(FName("WavePacketLength"), WavePacketLength);
			BeamComp->SetFloatParameter(FName("WavePacketDelay"), WavePacketDelay);
			BeamComp->SetFloatParameter(FName("WavePacketSpeed"), WavePacketSpeed);
			BeamComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			BeamComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}

		// Everything is set, so now it may run. Activating any earlier is what turns these writes
		// into deferred ones (see the spawn call above).
		BeamComp->Activate(true);
	}

	return BeamComp;
}

void AShooterWeapon::SpawnWaveFronts(const FVector& Start, const FVector& End)
{
	if (!WaveFrontFX)
	{
		return;
	}

	FVector Direction = (End - Start).GetSafeNormal();
	float Distance = FVector::Distance(Start, End);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	float StartRadius = CalculateWaveRadius(0.0f);  // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (InitialWaveRadius)
	float EndRadius = CalculateWaveRadius(Distance); // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ Niagara ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	UNiagaraComponent* ConeComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		WaveFrontFX,
		Start,
		Direction.Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ConeComp)
	{
		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
		ConeComp->SetVectorParameter(FName("BeamStart"), Start);
		ConeComp->SetVectorParameter(FName("BeamEnd"), End);
		ConeComp->SetVectorParameter(FName("BeamDirection"), Direction);
		ConeComp->SetFloatParameter(FName("MaxDistance"), Distance);
		ConeComp->SetFloatParameter(FName("InitialRadius"), StartRadius);
		ConeComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		ConeComp->SetFloatParameter(FName("DivergenceAngle"), DivergenceAngle);

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ===
		ConeComp->SetFloatParameter(FName("TravelSpeed"), WavePacketSpeed);
		ConeComp->SetFloatParameter(FName("Lifetime"), BeamFadeTime);
		ConeComp->SetFloatParameter(FName("ExpansionSpeed"), WaveFrontExpansionSpeed);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ===
		ConeComp->SetColorParameter(FName("WaveColor"), EFieldColor);
		ConeComp->SetFloatParameter(FName("Wavelength"), Wavelength);
		ConeComp->SetFloatParameter(FName("Energy"), 1.0f);

		// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°) ===
		FVector RightVector = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
		if (RightVector.IsNearlyZero())
		{
			RightVector = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
		}
		FVector UpVector = FVector::CrossProduct(RightVector, Direction).GetSafeNormal();

		ConeComp->SetVectorParameter(FName("UpVector"), UpVector);
		ConeComp->SetVectorParameter(FName("RightVector"), RightVector);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ===
		ConeComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		ConeComp->SetFloatParameter(FName("MinDamageMultiplier"), MinDamageMultiplier);
	}
}

EPhysicalSurface AShooterWeapon::ResolveImpactSurface(const FHitResult& Hit) const
{
	// Resolve surface type from hit's physical material (null-safe).
	// Requires the trace to be done with bReturnPhysicalMaterial = true.
	EPhysicalSurface Surface = SurfaceType_Default;
	if (UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get())
	{
		Surface = PhysMat->SurfaceType;
	}

	// An NPC that opted in answers for its own surface, so an enemy sounds like a shield while the
	// shield holds and like a body once it is down -- without a physical material per body part.
	// Which of the two it is comes from IsTargetShieldDown, the same gate ApplyDamageToTarget uses,
	// so the effect the player sees can never disagree with whether the shot actually hurt.
	if (AActor* HitActor = Hit.GetActor())
	{
		if (const AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor))
		{
			if (HitNPC->UsesImpactSurfaceOverride())
			{
				Surface = HitNPC->GetImpactSurface(IsTargetShieldDown(HitActor));
			}
		}
	}

	return Surface;
}

void AShooterWeapon::SpawnImpactEffect(const FHitResult& Hit)
{
	const EPhysicalSurface Surface = ResolveImpactSurface(Hit);

	// Here first, so the shooter sees their own bullet land with no round trip, then everyone else.
	// Identical split to the muzzle flash and the tracer above.
	SpawnImpactEffectLocally(Hit.ImpactPoint, Hit.ImpactNormal, Surface);

	if (HasAuthority())
	{
		Multicast_PlayImpactEffect(Hit.ImpactPoint, Hit.ImpactNormal, static_cast<uint8>(Surface));
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		// A client's shot only reaches the server as damage, and a shot that hit a wall has no
		// damage to report -- so the impact needs its own way upstream, like the muzzle flash does.
		OwnerCharacter->Server_ReportImpactEffect(this, Hit.ImpactPoint, Hit.ImpactNormal,
			static_cast<uint8>(Surface));
	}
}

void AShooterWeapon::Multicast_PlayImpactEffect_Implementation(FVector_NetQuantize100 Location,
	FVector_NetQuantizeNormal Normal, uint8 SurfaceByte)
{
	// The shooter already played it the instant the trace came back.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (bIsShooter)
	{
		return;
	}

	SpawnImpactEffectLocally(Location, Normal, static_cast<EPhysicalSurface>(SurfaceByte));
}

void AShooterWeapon::SpawnImpactEffectLocally(const FVector& Location, const FVector& Normal, EPhysicalSurface Surface)
{
	// This weapon's own per-surface entry wins where it is filled in, so a gun that was set up by
	// hand keeps exactly what it had. The set answers for everything the weapon says nothing about.
	const FImpactFeedback* SetEntry = FeedbackSet ? &FeedbackSet->FindImpact(Surface) : nullptr;

	// Pick VFX: per-surface override, otherwise the set, otherwise default ImpactFX.
	UNiagaraSystem* ResolvedFX = ImpactFX;
	if (SetEntry && SetEntry->HasVFX())
	{
		ResolvedFX = SetEntry->VFX;
	}
	if (TObjectPtr<UNiagaraSystem>* FoundFX = ImpactFXBySurface.Find(Surface))
	{
		if (*FoundFX)
		{
			ResolvedFX = *FoundFX;
		}
	}

	if (ResolvedFX)
	{
		UNiagaraComponent* ImpactComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			ResolvedFX,
			Location,
			Normal.Rotation(),
			FVector::OneVector,
			true,
			false,
			ENCPoolMethod::None
		);

		if (ImpactComp)
		{
			ImpactComp->SetColorParameter(FName("ImpactColor"), BeamColor);

			if (bUseWaveVisualization)
			{
				ImpactComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			}

			if (UWorld* World = GetWorld())
			{
				if (UVFXVariantSequenceSubsystem* VariantSubsystem =
					World->GetSubsystem<UVFXVariantSequenceSubsystem>())
				{
					VariantSubsystem->ConfigureVariantForComponent(ImpactComp);
				}
			}

			ImpactComp->Activate(true);
		}
	}

	// Pick sound: per-surface override, otherwise the set, otherwise DefaultImpactSound. The pitch,
	// volume and attenuation travel with whichever of the three answered, so a set's entry is not
	// left being mixed by numbers that belong to a different sound.
	USoundBase* ResolvedSound = DefaultImpactSound;
	float PitchMin = ImpactSoundPitchMin;
	float PitchMax = ImpactSoundPitchMax;
	float Volume = ImpactSoundVolume;
	USoundAttenuation* Attenuation = ImpactSoundAttenuation;

	if (SetEntry && SetEntry->HasSound())
	{
		ResolvedSound = SetEntry->Sound;
		PitchMin = SetEntry->PitchMin;
		PitchMax = SetEntry->PitchMax;
		Volume = SetEntry->Volume;
		Attenuation = SetEntry->Attenuation ? SetEntry->Attenuation.Get() : ImpactSoundAttenuation.Get();
	}

	if (TObjectPtr<USoundBase>* FoundSound = ImpactSoundBySurface.Find(Surface))
	{
		if (*FoundSound)
		{
			ResolvedSound = *FoundSound;
			PitchMin = ImpactSoundPitchMin;
			PitchMax = ImpactSoundPitchMax;
			Volume = ImpactSoundVolume;
			Attenuation = ImpactSoundAttenuation;
		}
	}

	if (ResolvedSound)
	{
		const float Pitch = FMath::FRandRange(FMath::Min(PitchMin, PitchMax), FMath::Max(PitchMin, PitchMax));
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ResolvedSound,
			Location,
			Volume,
			Pitch,
			0.0f,
			Attenuation
		);
	}
}

void AShooterWeapon::SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection)
{
	if (!ReflectionFX)
	{
		return;
	}

	UNiagaraComponent* ReflectionComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ReflectionFX,
		Location,
		FRotator::ZeroRotator,
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ReflectionComp)
	{
		ReflectionComp->SetVectorParameter(FName("IncomingDirection"), IncomingDirection);
		ReflectionComp->SetVectorParameter(FName("ReflectedDirection"), ReflectedDirection);
		ReflectionComp->SetColorParameter(FName("FlashColor"), BeamColor);
	}
}

// ==================== SFX ====================

void AShooterWeapon::PlayFireSound()
{
	if (!FireSound)
	{
		return;
	}

	// Get muzzle location for 3D sound
	// Use ThirdPersonMesh for NPCs (visible to player), FirstPersonMesh for local player
	FVector MuzzleLocation;

	bool bIsLocalPlayer = false;
	if (PawnOwner)
	{
		APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController());
		bIsLocalPlayer = PC && PC->IsLocalController();
	}

	if (bIsLocalPlayer && FirstPersonMesh)
	{
		MuzzleLocation = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else if (ThirdPersonMesh)
	{
		MuzzleLocation = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else
	{
		// Fallback to owner location
		MuzzleLocation = GetOwner()->GetActorLocation();
	}

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(FireSoundPitchMin, FireSoundPitchMax);

	// Play sound at muzzle location with attenuation for proper 3D spatialization
	UGameplayStatics::SpawnSoundAtLocation(
		this,
		FireSound,
		MuzzleLocation,
		FRotator::ZeroRotator,
		FireSoundVolume,
		RandomPitch,
		0.0f,  // StartTime
		FireSoundAttenuation
	);
}

float AShooterWeapon::GetOptimalDamageRange() const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ WaveRadius == TargetEffectiveRadius
	// WaveRadius = InitialWaveRadius + Distance * tan(DivergenceAngle)
	// TargetRadius = InitialRadius + OptimalDistance * tan(Angle)
	// OptimalDistance = (TargetRadius - InitialRadius) / tan(Angle)

	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	if (TangentAngle <= KINDA_SMALL_NUMBER)
	{
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		return MaxHitscanRange;
	}

	float OptimalDistance = (TargetEffectiveRadius - InitialWaveRadius) / TangentAngle;
	return FMath::Max(0.0f, OptimalDistance);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

void AShooterWeapon::PlayADSInSound()
{
	if (!ADSInSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSInSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

void AShooterWeapon::PlayADSOutSound()
{
	if (!ADSOutSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSOutSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

// ==================== Heat System ====================

void AShooterWeapon::UpdateHeat(float DeltaTime)
{
	if (CurrentHeat <= 0.0f)
	{
		// Deactivate VFX when cold
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
		return;
	}

	// Calculate decay rate based on owner speed
	float SpeedRatio = FMath::Clamp(GetOwnerSpeed() / MaxSpeedForHeatBonus, 0.0f, 1.0f);
	float SpeedBonus = 1.0f + (SpeedHeatDecayBonus * SpeedRatio);
	float DecayRate = BaseHeatDecayRate * SpeedBonus;

	// Apply decay
	CurrentHeat = FMath::Max(0.0f, CurrentHeat - DecayRate * DeltaTime);

	// Update Heat VFX
	UpdateHeatVFX();
}

void AShooterWeapon::UpdateHeatVFX()
{
	// Skip if no VFX system configured
	if (!HeatVFX)
	{
		return;
	}

	// Check if heat is above threshold
	if (CurrentHeat >= HeatVFXThreshold)
	{
		// Spawn VFX if not active
		if (!HeatVFXComponent)
		{
			USkeletalMeshComponent* AttachMesh = FirstPersonMesh;

			HeatVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				HeatVFX,
				AttachMesh,
				HeatVFXSocket,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget,
				false // Don't auto-destroy, we manage lifecycle
			);
		}
		else if (!HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Activate();
		}

		// Update heat parameter
		if (HeatVFXComponent)
		{
			HeatVFXComponent->SetFloatParameter(HeatParameterName, CurrentHeat);
		}
	}
	else
	{
		// Below threshold - deactivate VFX
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
	}
}

void AShooterWeapon::AddHeat(float Amount)
{
	CurrentHeat = FMath::Clamp(CurrentHeat + Amount, 0.0f, 1.0f);
}

float AShooterWeapon::GetOwnerSpeed() const
{
	if (CachedMovementComponent)
	{
		return CachedMovementComponent->Velocity.Size();
	}

	if (PawnOwner)
	{
		return PawnOwner->GetVelocity().Size();
	}

	return 0.0f;
}

float AShooterWeapon::CalculateHeatDamageMultiplier() const
{
	// Lerp from 1.0 (no heat) to MinHeatDamageMultiplier (max heat)
	return FMath::Lerp(1.0f, MinHeatDamageMultiplier, CurrentHeat);
}

float AShooterWeapon::CalculateHeatFireRateMultiplier() const
{
	if (!bUseHeatSystem)
	{
		return 1.0f;
	}
	// Lerp from 1.0 (no heat, normal fire rate) to MaxHeatFireRateMultiplier (max heat, slower fire rate)
	return FMath::Lerp(1.0f, MaxHeatFireRateMultiplier, CurrentHeat);
}

float AShooterWeapon::GetCurrentRefireRate() const
{
	// Base refire rate multiplied by heat penalty and any external multiplier (e.g. turret spin-up)
	return RefireRate * CalculateHeatFireRateMultiplier() * ExternalFireRateMultiplier;
}

// ==================== Z-Factor ====================

float AShooterWeapon::CalculateZFactorMultiplier(float ShooterZ, float TargetZ) const
{
	// Calculate height difference (positive = shooter is above)
	float HeightDiff = ShooterZ - TargetZ;

	// No bonus if shooter is below or at same level
	if (HeightDiff <= ZFactorMinHeightDiff)
	{
		return 1.0f;
	}

	// Calculate normalized height difference
	float EffectiveHeightDiff = HeightDiff - ZFactorMinHeightDiff;
	float MaxEffectiveHeightDiff = ZFactorMaxHeightDiff - ZFactorMinHeightDiff;
	float HeightRatio = FMath::Clamp(EffectiveHeightDiff / MaxEffectiveHeightDiff, 0.0f, 1.0f);

	// Lerp from 1.0 to ZFactorMaxMultiplier based on height
	return FMath::Lerp(1.0f, ZFactorMaxMultiplier, HeightRatio);
}

// ==================== ADS Camera ====================

void AShooterWeapon::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// This is called by PlayerCameraManager when this weapon is the ViewTarget (during ADS).
	// We provide the sight socket's WORLD POSITION but use ControlRotation for camera direction.
	// This way the camera sits at the weapon's sight, but does NOT inherit visual recoil kick
	// from the hands mesh — only the spring-smoothed camera recoil via AddPitchInput affects it.

	if (!ADSCameraComponent || !PawnOwner)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}

	// Use the ADS camera component's world position (attached to Sight socket on FP mesh).
	// This position includes the recoil visual kick (since FP mesh is moved by it).
	// We subtract the recoil offset to get the "clean" sight position.
	FVector SightWorldLocation = ADSCameraComponent->GetComponentLocation();

	// Subtract recoil visual kick from the sight position.
	// The weapon owner's RecoilComponent applies offsets to the FP Mesh,
	// which moves the ADS camera too. We want the camera without that kick.
	if (ACharacter* CharOwner = Cast<ACharacter>(PawnOwner))
	{
		if (UWeaponRecoilComponent* Recoil = CharOwner->FindComponentByClass<UWeaponRecoilComponent>())
		{
			// GetWeaponOffset returns offset in world-logical space (X=forward, Y=right, Z=up)
			FVector RecoilWorldOffset = Recoil->GetWeaponOffset();
			SightWorldLocation -= RecoilWorldOffset;
		}
	}

	// Use ControlRotation — this includes spring camera recoil (via AddPitchInput) but NOT
	// the visual weapon kick (which only affects FP Mesh relative transform).
	FRotator CameraRotation = PawnOwner->GetControlRotation();

	OutResult.Location = SightWorldLocation;
	OutResult.Rotation = CameraRotation;

	// FOV — the same ADSZoom the normal ADS path uses, applied to whatever the view is currently at.
	// NOTE this whole function is dormant: it only runs while the weapon is the ViewTarget, and
	// nothing sets that any more (ADS stopped moving the camera, see AShooterCharacter::UpdateADS).
	// It is kept correct rather than deleted so reviving SetViewTarget(Weapon) does not silently
	// resurrect a second, disagreeing zoom.
	OutResult.FOV = ApplyZoomToFOV(OutResult.FOV, ADSZoom);
}

float AShooterWeapon::ApplyZoomToFOV(float BaseFOVDegrees, float Zoom)
{
	const float SafeZoom = FMath::Max(Zoom, 0.01f);
	const float BaseTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(BaseFOVDegrees, 1.0f, 179.0f) * 0.5f));
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(BaseTan / SafeZoom));
}

// ==================== Charge-Based Firing ====================

bool AShooterWeapon::TryConsumeCharge(float& OutChargeMultiplier)
{
	OutChargeMultiplier = 1.0f;

	if (!bUseChargeFiring)
	{
		return true; // Not using charge system
	}

	// Find owner's EMFVelocityModifier
	AActor* WeaponOwnerActor = GetOwner();
	if (!WeaponOwnerActor)
	{
		return false;
	}

	UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
	if (!EMFMod)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Owner has no UEMFVelocityModifier for charge-based firing"));
		return false;
	}

	// Get current total charge (base + bonus)
	float ChargeModule = FMath::Abs(EMFMod->GetCharge());

	// Check if we can afford full shot
	if (ChargeModule >= ChargePerShot + MinimumBaseCharge)
	{
		// Full power shot - deduct charge (bonus first, then base)
		OutChargeMultiplier = 1.0f;
		EMFMod->DeductCharge(ChargePerShot);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Full power shot, charge module: %.2f -> %.2f"),
			ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
	else
	{
		// Not enough for full shot
		float AvailableCharge = FMath::Max(0.0f, ChargeModule - MinimumBaseCharge);

		if (AvailableCharge <= 0.0f || bBlockFiringBelowMinimum)
		{
			// Can't fire at all
			UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Not enough charge to fire (have %.2f, need %.2f + %.2f minimum)"),
				ChargeModule, ChargePerShot, MinimumBaseCharge);
			return false;
		}

		// Fire weakened shot
		OutChargeMultiplier = AvailableCharge / ChargePerShot;

		// Deduct all available charge (bonus first, then base, down to minimum)
		EMFMod->DeductCharge(AvailableCharge);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Weakened shot (%.1f%% power), charge module: %.2f -> %.2f"),
			OutChargeMultiplier * 100.0f, ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
}
