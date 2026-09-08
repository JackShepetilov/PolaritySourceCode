// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "Coop/CoopPlayers.h"
#include "ProjectilePoolSubsystem.h"
#include "ShooterWeapon.h"
#include "Net/UnrealNetwork.h"
#include "ApexMovementComponent.h"
#include "Polarity/Variant_Shooter/ShootableButtonComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

AShooterProjectile::AShooterProjectile()
{
	// The ACTOR does not tick, and never did anything when it was ticking: this class has no Tick
	// override, so every round in the air paid tick-manager overhead for an empty call.
	//
	// This does NOT touch flight or collision, and the proof is already in the project rather than
	// in reasoning: SlowPuddleProjectile and SmokeCanisterProjectile have shipped with the actor
	// tick off and the same ProjectileMovement, and they fly and hit things. Movement and its
	// sweeps live on the COMPONENT's own tick function, which is registered independently of the
	// actor's; NotifyHit comes out of those sweeps, not out of AActor::Tick. The pooled path's
	// SetActorTickEnabled(true) is a documented no-op while bCanEverTick is false.
	//
	// The two subclasses that genuinely need a per-frame pass turn it back on in their own
	// constructors and always have: AEMFProjectile (charge homing) and AShieldBypassProjectile
	// (in-flight target scan). A new subclass that needs one does the same.
	PrimaryActorTick.bCanEverTick = false;

	// An ordinary round is NOT replicated. Every machine simulates its own copy from the fired event
	// instead, which is the only shape that survives hundreds of bullets in the air.
	//
	// What replication was costing: an actor channel per round per client, plus movement updates at
	// the actor default of 100 Hz. Three hundred bullets alive in a four player game is three
	// hundred channels being fed by the server, for a path that is entirely predictable from
	// "muzzle here, direction there, speed known". The server keeps ONE authoritative copy that
	// nobody watches and that decides every hit; each client draws its own from
	// AShooterWeapon::Multicast_SpawnCosmeticProjectile.
	//
	// It also unlocks pooling. The reason the pool was restricted to the shooter's stand-in was that
	// a pooled actor is reused rather than destroyed, so a client holding its channel open would see
	// it teleport back to the muzzle on the next shot. With nobody holding a channel, that objection
	// is gone and the real projectile can come from the pool too.
	//
	// A subclass whose clients must interact with the ACTOR, rather than just look at it, turns this
	// back on in its own constructor. AEMFProjectile does, because it is a live field source that
	// players push off.
	bReplicates = false;
	SetReplicateMovement(false);

	// create the collision component and assign it as the root
	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// create the projectile movement component. No need to attach it because it's not a Scene Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	// THE line that decides whether the hitbox is trustworthy, so it is set here rather than left to
	// a default a blueprint could quietly clear. A swept move tests the whole segment between last
	// frame and this one; an unswept one teleports and tests only the destination. At 25000 cm/s a
	// 60 fps frame is four metres of travel, so without this a round passes through a wall, and
	// through a person, without ever being asked.
	ProjectileMovement->bSweepCollision = true;

	// Sub-stepping, and it is worth being exact about what this does and does not buy, because it is
	// easy to mistake for the thing above.
	//
	// It does NOT make hit detection safer. The sweep already tests the whole segment between last
	// frame and this one, so a straight round cannot tunnel however long the step is. And a straight
	// round does not sub-step at all: UProjectileMovementComponent::ShouldUseSubStepping only says
	// yes under gravity or active homing.
	//
	// What it buys is the SHAPE of an arc across a bad frame. A lobbed shell integrated in one 200 ms
	// jump cuts the corner of its own trajectory and can pass beside cover it should have hit; in
	// 20 ms slices it follows the curve it was aimed along. So this is for the grenade launcher and
	// the homing bolt, not for the rifle.
	ProjectileMovement->MaxSimulationTimeStep = 0.02f;
	ProjectileMovement->MaxSimulationIterations = 12;

	// A decoration reports the body it touched through this. Bound once, in the constructor, so a
	// pooled actor keeps the binding across every reuse; the handler itself refuses to do anything
	// unless the round is actually a decoration.
	CollisionComponent->SetGenerateOverlapEvents(true);
	CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &AShooterProjectile::OnCosmeticOverlap);

	// set the default damage type
	HitDamageType = UDamageType::StaticClass();
}

void AShooterProjectile::BeginPlay()
{
	Super::BeginPlay();

	// If this projectile is being spawned for pool, skip normal initialization
	// InitializeForPool() will be called right after BeginPlay
	if (bIsPooled)
	{
		return;
	}

	// The hide-for-the-shooter block that used to live here is gone with replication. It asked
	// "am I the authority's copy on somebody else's machine", and no such thing exists any more:
	// an unreplicated round only ever exists on the machine that spawned it, and the shooter sees
	// exactly one bullet because only one was made for it.

	// Normal spawn path (not from pool): make the shooter and the round transparent to each other.
	SetShooterMoveIgnore(true);

	// Nothing hits everything. Arm the clock that ends a round which hits nothing at all.
	ArmFlightTimeout();

	// Spawn trail VFX if configured
	if (TrailFX)
	{
		TrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			TrailFX,
			CollisionComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false // Keep the component alive; pooled projectiles reactivate it on reuse.
		);
	}
}

void AShooterProjectile::IgnoreProjectileWhileFlying(AShooterProjectile* Other)
{
	if (!IsValid(Other) || Other == this || !CollisionComponent)
	{
		return;
	}

	CollisionComponent->IgnoreActorWhenMoving(Other, true);
	CollisionComponent->MoveIgnoreActors.AddUnique(Other);
}

void AShooterProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterProjectile, SourceWeapon);
}

bool AShooterProjectile::CanAffectWorld() const
{
	// See the header for why this is the net MODE and not HasAuthority(). Short version: an
	// unreplicated actor is owned by whatever machine made it, so a client's own bullet would
	// answer yes to HasAuthority() and the only thing standing between it and dealing damage on
	// that player's computer would be one flag nobody can see from here.
	return GetNetMode() != NM_Client && !bIsCosmeticOnly;
}

void AShooterProjectile::SetCosmeticOnly()
{
	bIsCosmeticOnly = true;

	// OVERLAP on the pawn channel, which is the one setting that gets both halves right.
	//
	// Block would be wrong: a blocking sphere is a real obstacle to character movement on whichever
	// machine owns it, so a bullet that exists nowhere else could shove or stop a live teammate.
	//
	// Ignore would also be wrong, and this is the half worth spelling out, because it is where the
	// blood goes. An ignored body is a body this copy never hears about, so a watcher's round would
	// sail on through and the only impact anyone else could see would be the authority's unreliable
	// multicast -- dropped exactly when a firefight is busiest. Overlapping tells the decoration
	// precisely where it touched without ever standing in anyone's way, so every machine can play
	// its own blood, from its own copy, with nothing to lose in transit.
	//
	// World geometry keeps Block, so a decoration still stops dead at a wall.
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		CollisionComponent->SetGenerateOverlapEvents(true);
	}
}

void AShooterProjectile::SetShooterMoveIgnore(bool bIgnore)
{
	APawn* const InstigatorPawn = GetInstigator();
	if (!InstigatorPawn || !CollisionComponent)
	{
		return;
	}

	CollisionComponent->IgnoreActorWhenMoving(InstigatorPawn, bIgnore);

	// The half that leaks. This writes into the CHARACTER's ignore list, which outlives the bullet
	// by the rest of the match, and that list is walked on every character move.
	if (UPrimitiveComponent* InstigatorRoot = Cast<UPrimitiveComponent>(InstigatorPawn->GetRootComponent()))
	{
		InstigatorRoot->IgnoreActorWhenMoving(this, bIgnore);
	}
}

void AShooterProjectile::ArmFlightTimeout()
{
	UWorld* const World = GetWorld();
	if (!World || MaxTravelDistance <= 0.0f)
	{
		return;
	}

	// The speed it is actually flying at. InitialSpeed is the configured one and Velocity is the one
	// a launch path handed over; whichever is larger is the honest answer, and taking the larger is
	// also the safe direction to be wrong in -- it shortens the timeout rather than extending it.
	const float Speed = ProjectileMovement
		? FMath::Max(ProjectileMovement->InitialSpeed, ProjectileMovement->Velocity.Size())
		: 0.0f;

	// A round with no speed configured would divide into an eternity, so the ceiling answers instead.
	const float Seconds = (Speed > KINDA_SMALL_NUMBER)
		? FMath::Min(MaxTravelDistance / Speed, MaxFlightSeconds)
		: MaxFlightSeconds;

	World->GetTimerManager().SetTimer(DestructionTimer, this,
		&AShooterProjectile::OnDeferredDestruction, Seconds, false);
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// Before Super, because Super tears the actor down and GetInstigator has to still answer.
	// Without this the shooter's own MoveIgnoreActors keeps a dead pointer per round fired.
	SetShooterMoveIgnore(false);

	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// ignore if we've already hit something else
	if (bHit)
	{
		return;
	}

	// A round never hits another round.
	//
	// One shotgun trigger pull puts eight of these in the air from ONE muzzle transform, and the
	// collision sphere blocks every channel, so on the first frame they blocked each other: eight
	// pellets stopped dead at the barrel and dropped on the floor, each one reporting a hit on its
	// neighbour. And it DID report -- AActor::TakeDamage hands back the damage it was given even
	// when the actor does nothing with it, so the funnel saw a live hit and played a hit marker.
	//
	// Ignored from here on rather than just this once, so the two stop blocking each other for good,
	// and WITHOUT consuming bHit: this round has not hit anything yet and must carry on flying.
	if (Cast<AShooterProjectile>(Other))
	{
		CollisionComponent->IgnoreActorWhenMoving(Other, true);
		return;
	}

	bHit = true;

	// disable collision on the projectile
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop trail VFX on hit
	if (IsValid(TrailComponent))
	{
		TrailComponent->Deactivate();
	}

	// The round landed somewhere, and that is worth seeing and hearing whether or not it hurt
	// anything: a projectile weapon with no impact is a gun that shoots at walls in silence. Same
	// split the trace's impact uses, and the two halves must not both fire on one machine.
	//
	//   the shooter's stand-in  -> plays it here, now, with no round trip
	//   the authority's copy    -> plays it locally and multicasts (the multicast skips the shooter)
	//   a watching client's copy-> nothing; the multicast already reached that machine
	//
	// The surface is resolved on whichever machine spawns it, which is the point of resolving it at
	// all: an observer re-deriving a shield state a round trip later answers differently.
	PlayImpactFeedback(Hit);

	// Everything below this line changes the world: damage, knockback, explosions, and the noise the
	// AI hears. Only the authority's projectile is allowed to do any of it. A client's stand-in has
	// already done its job by being seen, and a replicated copy on a watching client must not apply
	// the same hit a second time.
	if (!CanAffectWorld())
	{
		BP_OnProjectileHit(Hit);

		// Only the shooter's own stand-in clears itself away. A replicated copy belongs to the
		// server and goes when the server's copy goes: tearing it down here would fight replication
		// over an actor this machine does not own.
		if (bIsCosmeticOnly)
		{
			if (DeferredDestructionTime > 0.0f)
			{
				GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this,
					&AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);
			}
			else
			{
				ReturnToPoolOrDestroy();
			}
		}
		return;
	}

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (bExplodeOnHit)
	{
		
		// apply explosion damage centered on the projectile
		ExplosionCheck(GetActorLocation(), Other);

	} else {

		// The whole hit result, kept for the length of the call below: ProcessHit's signature carries
		// only an actor and two vectors (five subclasses override it), and the shared funnel needs
		// the bone name for headshots and the physical material for the impact surface.
		DirectHit = Hit;

		// single hit projectile. Process the collided actor
		ProcessHit(Other, OtherComp, Hit.ImpactPoint, -Hit.ImpactNormal);

		DirectHit = FHitResult();
	}

	// pass control to BP for any extra effects
	BP_OnProjectileHit(Hit);

	// check if we should schedule deferred destruction of the projectile
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this, &AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);
	}
	else
	{
		// Return to pool or destroy right away
		ReturnToPoolOrDestroy();
	}
}

void AShooterProjectile::EnsureHitPhysicalMaterial(FHitResult& Hit) const
{
	if (Hit.PhysMaterial.IsValid())
	{
		return;
	}

	if (UPrimitiveComponent* const HitComponent = Hit.GetComponent())
	{
		if (FBodyInstance* const Body = HitComponent->GetBodyInstance(Hit.BoneName))
		{
			Hit.PhysMaterial = Body->GetSimplePhysicalMaterial();
		}
	}
}

void AShooterProjectile::PlayImpactFeedback(const FHitResult& Hit)
{
	AShooterWeapon* const Weapon = SourceWeapon;
	if (!Weapon)
	{
		return;
	}

	// Nobody is looking at a dedicated server, and Niagara and sound cost the same there as anywhere.
	if (GetNetMode() == NM_DedicatedServer && !GetIsReplicated())
	{
		return;
	}

	// The material the sweep did not bring. Without this every surface is SurfaceType_Default and
	// every impact plays the same puff, whatever was struck. @see EnsureHitPhysicalMaterial.
	FHitResult ResolvedHit = Hit;
	EnsureHitPhysicalMaterial(ResolvedHit);

	// Who plays what, and it turns on one question: does every machine have its own copy of this
	// round?
	//
	// For an ordinary bullet the answer is yes, so each machine plays its own landing and there is
	// no impact RPC per bullet anywhere in the game. That is not only cheaper, it is the RELIABLE
	// option: the multicast is unreliable by design, so routing every impact through it would drop
	// blood precisely when a firefight is at its busiest and there is most of it to drop.
	//
	// A replicating class (AEMFProjectile) only exists on the authority, so it keeps the old split:
	// play here, multicast to everyone else.
	if (!GetIsReplicated())
	{
		Weapon->SpawnImpactEffectLocally(ResolvedHit.ImpactPoint, ResolvedHit.ImpactNormal,
			Weapon->ResolveImpactSurface(ResolvedHit));
	}
	else if (GetNetMode() != NM_Client)
	{
		Weapon->SpawnImpactEffect(ResolvedHit);
	}
}

void AShooterProjectile::OnCosmeticOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Decorations only. The authority's round blocks bodies and lands through NotifyHit, and letting
	// it also land here would run one hit twice.
	if (!bIsCosmeticOnly || bHit || !IsValid(OtherActor))
	{
		return;
	}

	// The shooter, and any round sharing this muzzle. MoveIgnoreActors stops them BLOCKING each
	// other; it says nothing about overlaps, so without this a shotgun blooms eight impacts inside
	// the barrel and the player is hosed in their own blood.
	if (OtherActor == GetInstigator() || OtherActor == GetOwner() || OtherActor->IsA<AShooterProjectile>())
	{
		return;
	}

	bHit = true;

	// Where it touched. A swept overlap carries the real contact point; without one the round's own
	// position is the closest thing to an answer.
	FHitResult ContactHit = bFromSweep ? SweepResult : FHitResult();
	if (!bFromSweep)
	{
		ContactHit.HitObjectHandle = FActorInstanceHandle(OtherActor);
		ContactHit.Component = OtherComp;
		ContactHit.ImpactPoint = GetActorLocation();
		ContactHit.Location = GetActorLocation();
		ContactHit.ImpactNormal = ProjectileMovement
			? -ProjectileMovement->Velocity.GetSafeNormal() : FVector::UpVector;
	}

	PlayImpactFeedback(ContactHit);

	// Stop where it landed. An overlap does not halt anything by itself, so without this the
	// decoration would sail on past the body it just splashed.
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}
	if (IsValid(TrailComponent))
	{
		TrailComponent->Deactivate();
	}
	SetActorHiddenInGame(true);

	// Same ending a decoration gets when it stops at a wall, rather than a second one written here:
	// the deferred timer exists so a trail can finish burning instead of being cut mid-particle.
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this,
			&AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);
	}
	else
	{
		ReturnToPoolOrDestroy();
	}
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter, AActor* DirectHitActor)
{
	// do a sphere overlap check look for nearby actors to damage
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner && !bEnableOwnerRocketJump)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
		QueryParams.AddIgnoredActor(GetOwner());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		AActor* HitActor = CurrentOverlap.GetActor();
		if (HitActor && DamagedActors.Find(HitActor) == INDEX_NONE)
		{
			DamagedActors.Add(HitActor);

			// push and/or damage the overlapped actor
			ProcessExplosionHit(HitActor, CurrentOverlap.GetComponent(), ExplosionCenter, DirectHitActor);
		}
			
	}
}

void AShooterProjectile::ProcessExplosionHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& ExplosionCenter, AActor* DirectHitActor)
{
	if (!HitActor)
	{
		return;
	}

	const bool bIsOwner =
		HitActor == GetInstigator() ||
		HitActor == GetOwner();

	if (bIsOwner && !bDamageOwner && !bEnableOwnerRocketJump)
	{
		return;
	}

	if (bRequireExplosionLineOfSight && HitActor != DirectHitActor)
	{
		FHitResult LOSHit;
		FCollisionQueryParams LOSParams;
		LOSParams.AddIgnoredActor(this);
		LOSParams.AddIgnoredActor(HitActor);

		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			LOSHit, ExplosionCenter, HitActor->GetActorLocation(), ECC_Visibility, LOSParams);
		if (bBlocked)
		{
			return;
		}
	}

	const float Distance = FVector::Dist(ExplosionCenter, HitActor->GetActorLocation());
	const float SplashScale = CalculateExplosionSplashScale(Distance, HitActor, DirectHitActor);
	if (SplashScale <= 0.0f)
	{
		return;
	}

	const FVector HitDirection = (HitActor->GetActorLocation() - ExplosionCenter).GetSafeNormal();

	ACharacter* HitCharacter = Cast<ACharacter>(HitActor);
	const bool bIsShootableButtonOwner =
		HitActor->FindComponentByClass<UShootableButtonComponent>() != nullptr;

	// Characters already receive splash damage here. Shootable buttons also need a
	// real TakeDamage call so their OnTakeAnyDamage binding can activate the button.
	// Keep other WorldDynamic actors unchanged.
	if ((HitCharacter || bIsShootableButtonOwner) && (!bIsOwner || bDamageOwner))
	{
		// Resolved, not raw: HitDamage is an override whose "defer to the weapon" value is negative,
		// and a negative base here would have the blast healing everyone inside the radius.
		const float SplashBaseDamage = ResolveDirectHitDamage();

		// With a weapon behind the blast the weapon's tag multipliers are the ones that count, and
		// they are applied inside the funnel. Applying the projectile's here as well would square
		// every tag on the victim. Same rule as the direct hit.
		float TagMultiplier = SourceWeapon ? 1.0f : GetTagDamageMultiplier(HitActor);
		float ProjectileMultiplier = GetProjectileDamageMultiplier(HitActor);
		float FinalDamage = SplashBaseDamage * TagMultiplier * ProjectileMultiplier * SplashScale;
		if (bIsOwner)
		{
			FinalDamage *= OwnerSelfDamageMultiplier;
		}

		if (FinalDamage > 0.0f)
		{
			FRadialDamageEvent RadialDamageEvent;
			RadialDamageEvent.DamageTypeClass = HitDamageType;
			RadialDamageEvent.Origin = ExplosionCenter;
			RadialDamageEvent.Params.BaseDamage = SplashBaseDamage;
			RadialDamageEvent.Params.OuterRadius = ExplosionRadius;

			// One entry, so the event is well formed. FRadialDamageEvent::GetBestHitInfo indexes
			// ComponentHits[0] with only an ensure in front of it, and every reader downstream that
			// wants to know WHERE the blast landed on this body goes through that function.
			//
			// Built from the blast rather than from a real sweep: the direction is what reactions
			// use, and origin-to-victim is exactly the direction the explosion pushed. Running a
			// proper component sweep here would be the thorough version, but it would also be a
			// second trace per victim per explosion for a value only used to orient a wince.
			{
				const FVector ToVictim = (HitActor->GetActorLocation() - ExplosionCenter).GetSafeNormal();

				FHitResult& BlastHit = RadialDamageEvent.ComponentHits.AddDefaulted_GetRef();
				BlastHit.bBlockingHit = true;
				BlastHit.HitObjectHandle = FActorInstanceHandle(HitActor);
				BlastHit.Component = Cast<UPrimitiveComponent>(HitActor->GetRootComponent());
				BlastHit.ImpactPoint = ExplosionCenter;
				BlastHit.Location = ExplosionCenter;
				BlastHit.ImpactNormal = -ToVictim;
				BlastHit.Normal = -ToVictim;
			}

			// Through the weapon's funnel, exactly like a direct hit, so a blast finally means the
			// same things a bullet from the same gun means: the shield gate, the class passive's
			// piercing damage, the upgrade and ability notifications, ionization, and a hit marker
			// for every victim. Until now splash wrote health directly and confirmed nothing, which
			// is why a rocket kill felt like the game had not noticed.
			//
			// Two things are deliberately handed over rather than left to the funnel. The radial
			// event, because the blast origin and radius inside it are what reactions read. And an
			// impulse of zero, because the launch below is scaled by the splash falloff and again by
			// the rocket jump multiplier, neither of which a per-bullet shove knows about.
			if (AShooterWeapon* Weapon = SourceWeapon)
			{
				Weapon->ApplyWeaponHit(RadialDamageEvent.ComponentHits[0], FinalDamage, HitDirection,
					/*ImpulseForce*/ 0.0f, Weapon->GetShotDamageMultiplierAgainst(HitActor),
					HitDamageType, bDamageOwner, &RadialDamageEvent);
			}
			else
			{
				AController* InstigatorController = GetInstigator() ? GetInstigator()->GetController() : nullptr;
				HitActor->TakeDamage(FinalDamage, RadialDamageEvent, InstigatorController, this);
			}
		}
	}

	if (HitCharacter)
	{
		const bool bCanAffectOwner = !bIsOwner || bDamageOwner || bEnableOwnerRocketJump;
		if (bCanAffectOwner)
		{
			// Any player's shot knocks characters back, not just player 0's.
			const bool bFiredByPlayer = CoopPlayers::IsPlayer(GetInstigator());
			if (CharacterKnockbackForce > 0.0f && bFiredByPlayer && (!bIsOwner || bEnableOwnerRocketJump))
			{
				FVector LaunchDir = HitDirection;
				LaunchDir.Z += KnockbackUpwardBias;
				LaunchDir = LaunchDir.GetSafeNormal();

				float LaunchMultiplier = SplashScale;
				if (bIsOwner)
				{
					LaunchMultiplier *= GetOwnerRocketJumpMultiplier(HitCharacter);
				}

				HitCharacter->LaunchCharacter(LaunchDir * CharacterKnockbackForce * LaunchMultiplier, true, true);
			}
		}
	}

	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce * SplashScale, ExplosionCenter);
	}
}

float AShooterProjectile::CalculateExplosionSplashScale(float Distance, const AActor* HitActor, const AActor* DirectHitActor) const
{
	if (bDirectHitIgnoresSplashFalloff && HitActor && HitActor == DirectHitActor)
	{
		return 1.0f;
	}

	if (ExplosionRadius <= 0.0f)
	{
		return 0.0f;
	}

	const float T = FMath::Clamp(Distance / ExplosionRadius, 0.0f, 1.0f);
	return FMath::Lerp(1.0f, ExplosionEdgeDamageMultiplier, FMath::Pow(T, ExplosionFalloffExponent));
}

float AShooterProjectile::GetOwnerRocketJumpMultiplier(const ACharacter* HitCharacter) const
{
	if (!HitCharacter)
	{
		return 1.0f;
	}

	const UCharacterMovementComponent* CharacterMovement = HitCharacter->GetCharacterMovement();
	if (!CharacterMovement || !CharacterMovement->IsFalling())
	{
		return OwnerGroundKnockbackMultiplier;
	}

	bool bCrouchHeld = CharacterMovement->IsCrouching();
	if (const UApexMovementComponent* ApexMovement = Cast<UApexMovementComponent>(CharacterMovement))
	{
		bCrouchHeld = bCrouchHeld ||
			ApexMovement->bIsCrouchedInAir ||
			ApexMovement->bWantsSlideOnLand ||
			ApexMovement->IsCrouchInputHeld();
	}

	return bCrouchHeld ? OwnerAirCrouchKnockbackMultiplier : OwnerAirKnockbackMultiplier;
}

float AShooterProjectile::GetProjectileDamageMultiplier(AActor* /*Target*/) const
{
	return 1.0f;
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	if (!IsValid(HitActor))
	{
		return;
	}

	// The owner gate is the projectile's own answer and stays here. The funnel is told about it
	// rather than deciding it: the weapon's equivalent checkbox belongs to the trace path.
	if (HitActor == GetOwner() && !bDamageOwner)
	{
		// Still worth a shove: bouncing a crate you are standing behind is not self-damage.
		if (HitComp && HitComp->IsSimulatingPhysics())
		{
			HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
		}
		return;
	}

	// Anything that can be hurt, not only characters. This used to test for ACharacter and drop
	// everything else on the floor, which is why a projectile could push a physics prop around all
	// day without ever damaging it while the same weapon firing traces destroyed it.
	if (!HitActor->CanBeDamaged())
	{
		if (HitComp && HitComp->IsSimulatingPhysics())
		{
			HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
		}
		return;
	}

	// The payload's own scaling (the airborne bonus subclass) is the only multiplier that belongs to
	// the round rather than the gun. Tags, heat, height and upgrades come from the weapon inside
	// ApplyDirectHit, so there is one place they can be wrong instead of two.
	const float BaseDamage = ResolveDirectHitDamage() * GetProjectileDamageMultiplier(HitActor);

	ApplyDirectHit(HitActor, HitComp, HitLocation, HitDirection, BaseDamage);
}

float AShooterProjectile::ResolveDirectHitDamage() const
{
	if (HitDamage >= 0.0f)
	{
		return HitDamage;
	}

	if (SourceWeapon)
	{
		const float WeaponDamage = SourceWeapon->GetShotDamage();
		if (WeaponDamage > 0.0f)
		{
			return WeaponDamage;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[PROJECTILE] %s defers its damage to %s, and that weapon has none configured. ")
			TEXT("Set the weapon's HitscanDamage (it is the number for BOTH firing modes now), ")
			TEXT("or give this projectile its own HitDamage."),
			*GetName(), *GetNameSafe(SourceWeapon));
		return 0.0f;
	}

	UE_LOG(LogTemp, Error,
		TEXT("[PROJECTILE] %s has no weapon behind it and no HitDamage of its own, so it does nothing. ")
		TEXT("A projectile spawned outside a weapon must set HitDamage."),
		*GetName());
	return 0.0f;
}

void AShooterProjectile::ApplyDirectHit(AActor* HitActor, UPrimitiveComponent* HitComp,
	const FVector& HitLocation, const FVector& HitDirection, float FinalDamage)
{
	if (!IsValid(HitActor))
	{
		return;
	}

	// Rebuild the whole hit. DirectHit is the real one when NotifyHit is what got us here, and it
	// carries the two things the loose arguments cannot: the bone that decides a headshot and the
	// physical material that decides how the impact sounds. A subclass calling this with some other
	// actor gets a synthesised result instead, which is honest about knowing neither.
	FHitResult Hit = DirectHit;
	if (Hit.GetActor() != HitActor)
	{
		Hit = FHitResult();
		Hit.HitObjectHandle = FActorInstanceHandle(HitActor);
		Hit.Component = HitComp;
		Hit.bBlockingHit = true;
	}
	Hit.ImpactPoint = HitLocation;
	Hit.Location = HitLocation;
	Hit.ImpactNormal = -HitDirection.GetSafeNormal();

	// A projectile's sphere blocks against the CAPSULE, which carries no bone, so a round through
	// somebody's head arrived as a body shot. Continue the flight line through the body and ask the
	// mesh which bone it actually passed through. @see AShooterWeapon::ResolveHitBone.
	if (Hit.BoneName.IsNone() && SourceWeapon)
	{
		const FVector Flight = HitDirection.GetSafeNormal();
		Hit.BoneName = SourceWeapon->ResolveHitBone(HitActor,
			HitLocation - Flight * 100.0f, HitLocation + Flight * 250.0f);
	}

	// With a gun behind it, the round lands in exactly the funnel a trace of that gun lands in:
	// foliage conversion, shield gate, class passive, headshot, upgrade and ability notifications,
	// ionization, hit marker, and knockback under the grounded rule. PhysicsForce is the one number
	// for both characters and props, as on the trace path -- which is what takes the launch off a
	// standing target, because an ordinary bullet's shove is below the threshold that lifts anyone.
	if (AShooterWeapon* Weapon = SourceWeapon)
	{
		Weapon->ApplyWeaponHit(Hit, FinalDamage, HitDirection, PhysicsForce,
			Weapon->GetShotDamageMultiplierAgainst(HitActor), HitDamageType, bDamageOwner);
		return;
	}

	// No gun behind this round. Plain damage and a shove, which is all a gunless projectile ever
	// meant: its own tag multipliers apply here and only here, because with a weapon present the
	// weapon's are the ones that count and applying both would square them.
	AController* const InstigatorController = GetInstigator() ? GetInstigator()->GetController() : nullptr;
	UGameplayStatics::ApplyDamage(HitActor, FinalDamage * GetTagDamageMultiplier(HitActor),
		InstigatorController, this, HitDamageType);

	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}
}

float AShooterProjectile::GetTagDamageMultiplier(AActor* Target) const
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

void AShooterProjectile::OnDeferredDestruction()
{
	// Return to pool or destroy
	ReturnToPoolOrDestroy();
}

// ==================== Pooling Implementation ====================

void AShooterProjectile::ActivateFromPool(const FTransform& SpawnTransform, AActor* NewOwner, APawn* NewInstigator)
{
	// Out of the free list before anything else touches it, so a return that arrives during
	// activation cannot park an actor that is already flying.
	bIsInPool = false;

	// Reset state
	ResetProjectileState();

	// Set owner and instigator
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);

	// Set transform
	SetActorTransform(SpawnTransform);

	// Enable collision
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Setup instigator ignore (same as BeginPlay)
	SetShooterMoveIgnore(true);

	// Reset and activate projectile movement
	ProjectileMovement->SetVelocityInLocalSpace(FVector(ProjectileMovement->InitialSpeed, 0.0f, 0.0f));
	ProjectileMovement->SetUpdatedComponent(CollisionComponent);
	ProjectileMovement->Activate(true);

	// Show actor
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);

	if (TrailComponent && !IsValid(TrailComponent))
	{
		TrailComponent = nullptr;
	}

	// Spawn trail VFX if configured
	if (TrailFX && !TrailComponent)
	{
		TrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			TrailFX,
			CollisionComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false // Keep the component alive; pooled projectiles reactivate it on reuse.
		);
	}
	else if (IsValid(TrailComponent))
	{
		TrailComponent->Activate(true);
	}

	// Last, because it reads the speed the launch just set.
	ArmFlightTimeout();
}

void AShooterProjectile::DeactivateToPool()
{
	// First, while GetInstigator still answers: take this round back out of the shooter's own
	// ignore list. Pooling bounds the leak (the same actors come round again) but it does not
	// remove it, and a stale entry is a pointer the character walks past on every move.
	SetShooterMoveIgnore(false);

	// Hide actor
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	// Disable collision
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop movement
	ProjectileMovement->Deactivate();
	ProjectileMovement->Velocity = FVector::ZeroVector;

	// Stop trail VFX
	if (IsValid(TrailComponent))
	{
		TrailComponent->Deactivate();
	}

	// Clear timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestructionTimer);
	}

	// Clear ignore actors for next use
	CollisionComponent->ClearMoveIgnoreActors();

	// Parked. Nothing may return it again until the pool hands it back out.
	bIsInPool = true;
}

void AShooterProjectile::ResetProjectileState()
{
	// Pool reuse: whatever this projectile was last time it flew, it is not that now.
	bIsCosmeticOnly = false;
	SourceWeapon = nullptr;
	DirectHit = FHitResult();

	// Reset hit flag
	bHit = false;

	// THE pooling hazard for the hitbox, and it only exists because decoration and live rounds now
	// come out of the same pool. SetCosmeticOnly drops the pawn response to Ignore; a round reused
	// afterwards as a REAL one would keep that and fly through every person it was aimed at, hitting
	// only walls. Silent, intermittent, and impossible to reproduce on demand -- it would depend on
	// which slot the pool handed back. So the response is restored here, on the one path every reuse
	// goes through, rather than trusted to be untouched.
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		CollisionComponent->UpdateOverlaps();
	}

	// Clear previous instigator ignores
	CollisionComponent->ClearMoveIgnoreActors();
}

void AShooterProjectile::ReturnToPoolOrDestroy()
{
	// Already parked. Two returns for one flight is the pool corruption described on bIsInPool, and
	// the cheapest place to stop it is the door.
	if (bIsInPool)
	{
		return;
	}

	if (bIsPooled)
	{
		// Return to pool for reuse
		if (UWorld* World = GetWorld())
		{
			if (UProjectilePoolSubsystem* Pool = World->GetSubsystem<UProjectilePoolSubsystem>())
			{
				Pool->ReturnProjectile(this);
				return;
			}
		}
	}

	// Not pooled or pool not found - destroy normally
	Destroy();
}
