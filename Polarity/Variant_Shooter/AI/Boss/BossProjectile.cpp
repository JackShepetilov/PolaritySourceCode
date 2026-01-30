// BossProjectile.cpp
// Specialized EMF projectile for boss with parry detection

#include "BossProjectile.h"
#include "BossCharacter.h"
#include "EMFVelocityModifier.h"
#include "DrawDebugHelpers.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ABossProjectile::ABossProjectile()
{
	// Set initial NPC force multiplier to 0 (ignore boss field)
	NPCForceMultiplier = 0.0f;

	// Disable physics force on hit (don't push the player)
	PhysicsForce = 0.0f;

	// Set collision to Overlap for Pawn so projectile passes through player
	// but still triggers overlap events for damage
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

void ABossProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Apply initial NPC force multiplier
	NPCForceMultiplier = InitialNPCForceMultiplier;

	// Bind overlap event for player damage (since we use Overlap instead of Block for Pawn)
	if (CollisionComponent)
	{
		CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &ABossProjectile::OnProjectileOverlap);
	}
}

void ABossProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Check for parry if not already parried
	if (bInitialized && !bWasParried)
	{
		CheckForParry();
	}

	// Debug visualization
	if (bDrawParryDebug && ParryTarget.IsValid())
	{
		DrawDebugSphere(
			GetWorld(),
			ParryTarget->GetActorLocation(),
			ParryDetectionRadius,
			16,
			bWasParried ? FColor::Green : FColor::Yellow,
			false,
			-1.0f,
			0,
			2.0f
		);
	}
}

void ABossProjectile::InitializeForBoss(ABossCharacter* Boss, AActor* Target)
{
	if (!Boss || !Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] InitializeForBoss failed - Boss or Target is null"));
		return;
	}

	OwnerBoss = Boss;
	ParryTarget = Target;

	// Get player's charge and set projectile to OPPOSITE
	UEMFVelocityModifier* PlayerEMF = Target->FindComponentByClass<UEMFVelocityModifier>();
	if (PlayerEMF)
	{
		float PlayerCharge = PlayerEMF->GetCharge();
		int32 PlayerSign = (PlayerCharge >= 0.0f) ? 1 : -1;

		// Set opposite charge (so it attracts to player initially)
		float BaseCharge = FMath::Abs(GetProjectileCharge());
		float OppositeCharge = -PlayerSign * BaseCharge;
		SetProjectileCharge(OppositeCharge);

		UE_LOG(LogTemp, Log, TEXT("[BossProjectile] Initialized: PlayerCharge=%.2f, ProjectileCharge=%.2f (opposite)"),
			PlayerCharge, OppositeCharge);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] Player has no EMFVelocityModifier, using default charge"));
	}

	// Set initial NPC force multiplier (ignore boss field)
	NPCForceMultiplier = InitialNPCForceMultiplier;

	bInitialized = true;
}

void ABossProjectile::CheckForParry()
{
	if (!ParryTarget.IsValid())
	{
		return;
	}

	// Check distance to player
	float Distance = FVector::Dist(GetActorLocation(), ParryTarget->GetActorLocation());
	if (Distance > ParryDetectionRadius)
	{
		return;
	}

	// Get player's current charge
	UEMFVelocityModifier* PlayerEMF = ParryTarget->FindComponentByClass<UEMFVelocityModifier>();
	if (!PlayerEMF)
	{
		return;
	}

	float PlayerCharge = PlayerEMF->GetCharge();
	float ProjectileCharge = GetProjectileCharge();

	// Same sign = repulsion = PARRY!
	// (Player changed polarity to match projectile, pushing it away)
	bool bSameSign = (PlayerCharge * ProjectileCharge) > 0.0f;

	if (bSameSign)
	{
		bWasParried = true;

		// Enable attraction to boss
		NPCForceMultiplier = ParriedNPCForceMultiplier;

		// Enable damage to owner (boss) so parried projectile can hurt it
		bDamageOwner = true;

		// Remove boss from ignore list so projectile can hit it now
		if (APawn* InstigatorPawn = GetInstigator())
		{
			CollisionComponent->IgnoreActorWhenMoving(InstigatorPawn, false);
			CollisionComponent->MoveIgnoreActors.Remove(InstigatorPawn);
		}

		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] PARRIED! PlayerCharge=%.2f, ProjectileCharge=%.2f, NPCForceMultiplier=%.2f, bDamageOwner=true"),
			PlayerCharge, ProjectileCharge, NPCForceMultiplier);

		// Notify boss
		if (OwnerBoss.IsValid())
		{
			OwnerBoss->OnProjectileParried(this);
		}
	}
}

void ABossProjectile::OnProjectileOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] OnProjectileOverlap with %s, bHit=%d, bWasParried=%d, bDamageOwner=%d"),
		OtherActor ? *OtherActor->GetName() : TEXT("NULL"), bHit, bWasParried, bDamageOwner);

	// Only process if we haven't hit something already
	if (bHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] Skipping - already hit"));
		return;
	}

	// Check if we hit a character
	ACharacter* HitCharacter = Cast<ACharacter>(OtherActor);
	if (!HitCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] Skipping - not a character"));
		return;
	}

	// Don't damage instigator (boss) unless bDamageOwner is true (after parry)
	if (HitCharacter == GetInstigator() && !bDamageOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] Skipping boss - bDamageOwner is false"));
		return;
	}

	// Mark as hit
	bHit = true;

	// Calculate tag-based damage multiplier
	float TagMultiplier = GetTagDamageMultiplier(OtherActor);
	float FinalDamage = HitDamage * TagMultiplier;

	UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] About to apply %.1f damage to %s (HitDamage=%.1f, TagMult=%.2f)"),
		FinalDamage, *HitCharacter->GetName(), HitDamage, TagMultiplier);

	// Apply damage
	AController* DamageInstigator = GetInstigator() ? GetInstigator()->GetController() : nullptr;
	UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] DamageInstigator: %s"), DamageInstigator ? *DamageInstigator->GetName() : TEXT("NULL"));

	UGameplayStatics::ApplyDamage(HitCharacter, FinalDamage, DamageInstigator, this, HitDamageType);

	UE_LOG(LogTemp, Warning, TEXT("[BossProjectile] ApplyDamage called for %s with %.1f damage"),
		*HitCharacter->GetName(), FinalDamage);

	// Call blueprint event
	FHitResult Hit;
	Hit.ImpactPoint = GetActorLocation();
	Hit.ImpactNormal = -GetVelocity().GetSafeNormal();
	Hit.Location = GetActorLocation();
	BP_OnProjectileHit(Hit);

	// Handle destruction
	if (DeferredDestructionTime > 0.0f)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, FTimerDelegate::CreateLambda([this]()
		{
			Destroy();
		}), DeferredDestructionTime, false);
	}
	else
	{
		Destroy();
	}
}
