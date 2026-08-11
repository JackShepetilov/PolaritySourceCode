// SportsBall.cpp

#include "Arena/SportsBall.h"

#include "ApexMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ShooterCharacter.h"
#include "UObject/ConstructorHelpers.h"

ASportsBall::ASportsBall()
{
	PrimaryActorTick.bCanEverTick = false;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	SetRootComponent(BallMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}

	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	BallMesh->SetCollisionObjectType(ECC_PhysicsBody);
	BallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BallMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BallMesh->BodyInstance.bUseCCD = true;
	BallMesh->BodyInstance.bNotifyRigidBodyCollision = true;
	BallMesh->SetGenerateOverlapEvents(true);

	Tags.AddUnique(TEXT("SportsBall"));
}

void ASportsBall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyBallSettings();
}

void ASportsBall::BeginPlay()
{
	Super::BeginPlay();

	ApplyBallSettings();

	if (BallMesh)
	{
		BallMesh->OnComponentHit.AddDynamic(this, &ASportsBall::OnBallHit);
	}
}

bool ASportsBall::HandleMeleeAttackHit(AActor* Attacker, const FHitResult& HitResult, EMeleeAttackType AttackType, const FVector& AttackDirection, const FVector& AttackerVelocity)
{
	LastAttacker = Attacker;
	LastMeleeAttackType = AttackType;
	const FVector ImpactPoint(HitResult.ImpactPoint.X, HitResult.ImpactPoint.Y, HitResult.ImpactPoint.Z);
	LastHitLocation = ImpactPoint.IsNearlyZero() ? GetActorLocation() : ImpactPoint;
	LastAppliedImpulse = FVector::ZeroVector;
	bLastHitAppliedKick = false;

	const bool bIsKick = IsKickAttackType(AttackType);
	if (!bIsKick)
	{
		++PunchHitCount;
		OnSportsBallMeleeHit.Broadcast(this, Attacker, AttackType, false, LastHitLocation, FVector::ZeroVector);

		if (bLogMeleeHits)
		{
			UE_LOG(LogTemp, Log, TEXT("[SportsBall] Registered punch on %s by %s. No impulse applied."),
				*GetName(), Attacker ? *Attacker->GetName() : TEXT("None"));
		}

		return false;
	}

	++KickHitCount;

	if (!BallMesh || !BallMesh->IsSimulatingPhysics())
	{
		OnSportsBallMeleeHit.Broadcast(this, Attacker, AttackType, false, LastHitLocation, FVector::ZeroVector);
		return false;
	}

	const FVector KickDirection = BuildKickDirection(Attacker, AttackDirection);
	const float AttackerSpeedAlongKick = FMath::Max(0.0f, FVector::DotProduct(AttackerVelocity, KickDirection));
	const float VelocityBonus = FMath::Min(MaxPlayerVelocityBonus, AttackerSpeedAlongKick * PlayerVelocityToKickScale);
	float FinalVelocityChange = KickVelocityChange + VelocityBonus;
	if (AttackType == EMeleeAttackType::Sliding)
	{
		FinalVelocityChange += SlidingKickForwardVelocityChange;
	}
	const float Mass = FMath::Max(1.0f, BallMesh->GetMass());

	FVector FinalKickVelocityChange = KickDirection * FinalVelocityChange;
	if (AttackType == EMeleeAttackType::Sliding)
	{
		FinalKickVelocityChange.Z = FMath::Max(FinalKickVelocityChange.Z, SlidingKickUpVelocityChange);
	}

	LastAppliedImpulse = FinalKickVelocityChange * Mass;
	bLastHitAppliedKick = !LastAppliedImpulse.IsNearlyZero();

	if (bLastHitAppliedKick)
	{
		BallMesh->AddImpulseAtLocation(LastAppliedImpulse, LastHitLocation);
		HealPlayerFromInteraction(Attacker, KickHealAmount, TEXT("kick"));

		if (KickSpinVelocityChange > 0.0f)
		{
			FVector SpinAxis = FVector::CrossProduct(FVector::UpVector, KickDirection).GetSafeNormal();
			if (!SpinAxis.IsNearlyZero())
			{
				BallMesh->AddAngularImpulseInDegrees(SpinAxis * KickSpinVelocityChange, NAME_None, true);
			}
		}
	}

	OnSportsBallMeleeHit.Broadcast(this, Attacker, AttackType, bLastHitAppliedKick, LastHitLocation, LastAppliedImpulse);

	if (bLogMeleeHits)
	{
		UE_LOG(LogTemp, Log, TEXT("[SportsBall] Kick on %s by %s. AttackType=%d VelocityChange=%.0f Impulse=(%.0f, %.0f, %.0f)"),
			*GetName(),
			Attacker ? *Attacker->GetName() : TEXT("None"),
			static_cast<int32>(AttackType),
			FinalVelocityChange,
			LastAppliedImpulse.X,
			LastAppliedImpulse.Y,
			LastAppliedImpulse.Z);
	}

	return bLastHitAppliedKick;
}

bool ASportsBall::IsKickAttackType(EMeleeAttackType AttackType)
{
	return AttackType == EMeleeAttackType::Airborne || AttackType == EMeleeAttackType::Sliding;
}

void ASportsBall::ApplyBallSettings()
{
	if (!BallMesh)
	{
		return;
	}

	const float SafeDiameter = FMath::Max(10.0f, BallDiameter);
	constexpr float EngineSphereDiameter = 100.0f;
	const float UniformScale = SafeDiameter / EngineSphereDiameter;
	BallMesh->SetRelativeScale3D(FVector(UniformScale));

	BallMesh->BodyInstance.bUseCCD = bUseCCD;
	BallMesh->SetLinearDamping(LinearDamping);
	BallMesh->SetAngularDamping(AngularDamping);
	BallMesh->SetMassOverrideInKg(NAME_None, FMath::Max(0.1f, BallMassKg), true);
	BallMesh->SetPhysMaterialOverride(BallPhysicalMaterial);
	BallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BallMesh->SetGenerateOverlapEvents(true);
}

FVector ASportsBall::BuildKickDirection(AActor* Attacker, const FVector& AttackDirection) const
{
	FVector KickDirection = AttackDirection.GetSafeNormal();

	if (KickDirection.IsNearlyZero() && Attacker)
	{
		KickDirection = GetActorLocation() - Attacker->GetActorLocation();
	}

	if (KickDirection.IsNearlyZero())
	{
		KickDirection = GetActorForwardVector();
	}

	KickDirection.Z = FMath::Max(KickDirection.Z, KickUpwardBias);
	return KickDirection.GetSafeNormal();
}


bool ASportsBall::IsSlidingCharacter(AActor* Actor, FVector& OutVelocity) const
{
	ACharacter* Character = Cast<ACharacter>(Actor);
	if (!Character)
	{
		return false;
	}

	UApexMovementComponent* ApexMovement = Character->FindComponentByClass<UApexMovementComponent>();
	if (!ApexMovement || !ApexMovement->IsSliding())
	{
		return false;
	}

	OutVelocity = Character->GetVelocity();
	return true;
}

void ASportsBall::HealPlayerFromInteraction(AActor* PlayerActor, float Amount, const TCHAR* Source)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(PlayerActor);
	if (!Player || Player->IsDead() || Amount <= 0.0f)
	{
		return;
	}

	const float MissingHealth = FMath::Max(0.0f, Player->GetMaxHP() - Player->GetCurrentHP());
	const float AppliedHealing = FMath::Min(Amount, MissingHealth);
	Player->RestoreHealth(Amount);

	if (bLogMeleeHits && AppliedHealing > 0.0f)
	{
		UE_LOG(LogTemp, Log, TEXT("[SportsBall] Healed %s by %.1f from %s."),
			*Player->GetName(), AppliedHealing, Source ? Source : TEXT("interaction"));
	}
}

void ASportsBall::OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!BallMesh || !BallMesh->IsSimulatingPhysics() || !OtherActor || OtherActor == this)
	{
		return;
	}

	if (AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor))
	{
		FVector ToBall = GetActorLocation() - Player->GetActorLocation();
		ToBall.Z = 0.0f;
		const FVector ToBallDirection = ToBall.GetSafeNormal();
		const float ClosingSpeed = ToBallDirection.IsNearlyZero()
			? 0.0f
			: FMath::Max(0.0f, FVector::DotProduct(Player->GetVelocity(), ToBallDirection));

		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (ClosingSpeed >= ContactPushHealMinSpeed && Now - LastContactPushHealTime >= ContactPushHealCooldown)
		{
			LastContactPushHealTime = Now;
			HealPlayerFromInteraction(Player, ContactPushHealAmount, TEXT("contact push"));
		}
	}

	FVector SliderVelocity = FVector::ZeroVector;
	if (!IsSlidingCharacter(OtherActor, SliderVelocity))
	{
		return;
	}

	FVector PushDirection = SliderVelocity;
	PushDirection.Z = 0.0f;
	if (PushDirection.IsNearlyZero())
	{
		return;
	}
	PushDirection.Normalize();

	FVector BallDirection = GetActorLocation() - OtherActor->GetActorLocation();
	BallDirection.Z = 0.0f;
	if (!BallDirection.Normalize())
	{
		BallDirection = PushDirection;
	}

	const float ClosingSpeed = FMath::Max(0.0f, FVector::DotProduct(SliderVelocity, BallDirection));
	if (ClosingSpeed < SlidingBodyPushMinSpeed)
	{
		return;
	}

	const float CurrentBallSpeedAlongPush = FVector::DotProduct(BallMesh->GetPhysicsLinearVelocity(), PushDirection);
	const float TargetBallSpeed = FMath::Min(SlidingBodyPushMaxBallSpeed, ClosingSpeed);
	const float NeededVelocityChange = FMath::Max(0.0f, TargetBallSpeed - CurrentBallSpeedAlongPush);
	const float VelocityChange = FMath::Min(SlidingBodyPushMaxVelocityChange, NeededVelocityChange * SlidingBodyPushVelocityScale);
	if (VelocityChange <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector ImpactPoint = Hit.ImpactPoint.IsNearlyZero() ? GetActorLocation() : FVector(Hit.ImpactPoint);
	BallMesh->AddImpulseAtLocation(PushDirection * VelocityChange * FMath::Max(1.0f, BallMesh->GetMass()), ImpactPoint);
}
