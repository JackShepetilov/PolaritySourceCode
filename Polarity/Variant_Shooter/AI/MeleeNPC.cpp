// MeleeNPC.cpp
// Implementation of melee combat NPC

#include "MeleeNPC.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "AIController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../../AI/Components/MeleeRetreatComponent.h"

AMeleeNPC::AMeleeNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Melee NPCs don't need the retreat component - they want to be close
	if (MeleeRetreatComponent)
	{
		MeleeRetreatComponent->SetActive(false);
	}
}

void AMeleeNPC::BeginPlay()
{
	// Don't spawn ranged weapon - clear the weapon class before parent BeginPlay
	WeaponClass = nullptr;

	Super::BeginPlay();

	// Disable melee retreat component (this NPC fights in melee)
	if (MeleeRetreatComponent)
	{
		MeleeRetreatComponent->SetActive(false);
	}

	// Spawn melee weapon if specified
	SpawnMeleeWeapon();
}

void AMeleeNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Perform melee trace if damage window is active
	if (bDamageWindowActive && !bIsDead)
	{
		PerformMeleeTrace();
	}
}

void AMeleeNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear all timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DamageWindowStartTimer);
		World->GetTimerManager().ClearTimer(DamageWindowEndTimer);
		World->GetTimerManager().ClearTimer(AttackCooldownTimer);
	}

	// Destroy melee weapon
	if (MeleeWeaponActor)
	{
		MeleeWeaponActor->Destroy();
		MeleeWeaponActor = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AMeleeNPC::StartMeleeAttack(AActor* Target)
{
	// Validate
	if (!CanAttack() || !Target || bIsDead)
	{
		return;
	}

	// Set state
	bIsAttacking = true;
	CurrentMeleeTarget = Target;
	HitActorsThisAttack.Empty();
	LastAttackTime = GetWorld()->GetTimeSeconds();

	// Face the target
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (!ToTarget.IsNearlyZero())
	{
		SetActorRotation(ToTarget.Rotation());
	}

	// Select random attack montage
	UAnimMontage* MontageToPlay = nullptr;
	if (AttackMontages.Num() > 0)
	{
		int32 RandomIndex = FMath::RandRange(0, AttackMontages.Num() - 1);
		MontageToPlay = AttackMontages[RandomIndex];
	}

	// Play attack animation
	if (MontageToPlay)
	{
		USkeletalMeshComponent* TPMesh = GetMesh();
		if (TPMesh)
		{
			UAnimInstance* AnimInstance = TPMesh->GetAnimInstance();
			if (AnimInstance)
			{
				float MontageLength = AnimInstance->Montage_Play(MontageToPlay);

				// Bind to montage end
				FOnMontageEnded EndDelegate;
				EndDelegate.BindUObject(this, &AMeleeNPC::OnAttackMontageEnded);
				AnimInstance->Montage_SetEndDelegate(EndDelegate, MontageToPlay);

				// Set up timer-based damage window if enabled
				if (bUseTimerDamageWindow && MontageLength > 0.0f)
				{
					// Start damage window timer
					GetWorld()->GetTimerManager().SetTimer(
						DamageWindowStartTimer,
						this,
						&AMeleeNPC::OnDamageWindowStart,
						DamageWindowStartTime,
						false
					);
				}
			}
		}
	}
	else
	{
		// No montage - just do instant damage window
		OnDamageWindowStart();

		// End damage window after duration
		GetWorld()->GetTimerManager().SetTimer(
			DamageWindowEndTimer,
			this,
			&AMeleeNPC::OnDamageWindowEnd,
			DamageWindowDuration,
			false
		);

		// End attack after a short delay
		FTimerHandle TempTimer;
		GetWorld()->GetTimerManager().SetTimer(
			TempTimer,
			[this]()
			{
				bIsAttacking = false;
				OnAttackCooldownEnd();
			},
			DamageWindowStartTime + DamageWindowDuration + 0.1f,
			false
		);
	}

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
			FString::Printf(TEXT("%s: Starting melee attack on %s"),
				*GetName(), *Target->GetName()));
	}
#endif
}

bool AMeleeNPC::CanAttack() const
{
	if (bIsDead || bIsAttacking || bIsInKnockback)
	{
		return false;
	}

	// Check cooldown
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (LastAttackTime > 0.0f && (CurrentTime - LastAttackTime) < AttackCooldown)
	{
		return false;
	}

	return true;
}

bool AMeleeNPC::IsTargetInAttackRange(AActor* Target) const
{
	if (!Target)
	{
		return false;
	}

	float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	return Distance <= AttackRange;
}

void AMeleeNPC::NotifyDamageWindowStart()
{
	// Only use if timer-based is disabled
	if (!bUseTimerDamageWindow)
	{
		OnDamageWindowStart();
	}
}

void AMeleeNPC::NotifyDamageWindowEnd()
{
	// Only use if timer-based is disabled
	if (!bUseTimerDamageWindow)
	{
		OnDamageWindowEnd();
	}
}

void AMeleeNPC::OnDamageWindowStart()
{
	bDamageWindowActive = true;

	// Set up end timer if using timer-based
	if (bUseTimerDamageWindow)
	{
		GetWorld()->GetTimerManager().SetTimer(
			DamageWindowEndTimer,
			this,
			&AMeleeNPC::OnDamageWindowEnd,
			DamageWindowDuration,
			false
		);
	}

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Green,
			FString::Printf(TEXT("%s: Damage window OPEN"), *GetName()));
	}
#endif
}

void AMeleeNPC::OnDamageWindowEnd()
{
	bDamageWindowActive = false;

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Red,
			FString::Printf(TEXT("%s: Damage window CLOSED"), *GetName()));
	}
#endif
}

void AMeleeNPC::PerformMeleeTrace()
{
	if (!GetWorld() || bIsDead)
	{
		return;
	}

	// Calculate trace start and end
	FVector CharacterLocation = GetActorLocation();
	FVector ForwardVector = GetActorForwardVector();

	// Start trace slightly in front of character at specified height
	FVector TraceStart = CharacterLocation + FVector(0.0f, 0.0f, TraceHeightOffset);
	FVector TraceEnd = TraceStart + (ForwardVector * TraceDistance);

	// Set up trace parameters
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (MeleeWeaponActor)
	{
		QueryParams.AddIgnoredActor(MeleeWeaponActor);
	}
	QueryParams.bTraceComplex = false;

	// Perform sphere sweep
	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(TraceRadius),
		QueryParams
	);

	// Debug visualization
#if WITH_EDITOR
	if (bDebugMeleeTraces)
	{
		FColor DebugColor = bHit ? FColor::Red : FColor::Green;

		// Draw sphere at start
		DrawDebugSphere(
			GetWorld(),
			TraceStart,
			TraceRadius,
			12,
			DebugColor,
			false,
			DebugTraceDuration
		);

		// Draw sphere at end
		DrawDebugSphere(
			GetWorld(),
			TraceEnd,
			TraceRadius,
			12,
			DebugColor,
			false,
			DebugTraceDuration
		);

		// Draw line between
		DrawDebugLine(
			GetWorld(),
			TraceStart,
			TraceEnd,
			DebugColor,
			false,
			DebugTraceDuration,
			0,
			2.0f
		);

		// Draw capsule to show full sweep volume
		DrawDebugCapsule(
			GetWorld(),
			(TraceStart + TraceEnd) * 0.5f,
			FVector::Dist(TraceStart, TraceEnd) * 0.5f,
			TraceRadius,
			FRotationMatrix::MakeFromZ(ForwardVector).ToQuat(),
			DebugColor,
			false,
			DebugTraceDuration
		);
	}
#endif

	// Process hits
	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor)
			{
				continue;
			}

			// Skip if already hit this attack
			if (HitActorsThisAttack.Contains(HitActor))
			{
				continue;
			}

			// Skip other MeleeNPCs and ShooterNPCs (friendly fire prevention)
			if (Cast<AShooterNPC>(HitActor))
			{
				continue;
			}

			// Mark as hit and apply damage
			HitActorsThisAttack.Add(HitActor);
			ApplyMeleeDamage(HitActor, Hit);

#if WITH_EDITOR
			if (bDebugMeleeTraces)
			{
				// Draw hit marker
				DrawDebugSphere(
					GetWorld(),
					Hit.ImpactPoint,
					15.0f,
					8,
					FColor::Yellow,
					false,
					DebugTraceDuration * 2.0f
				);

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
						FString::Printf(TEXT("%s: HIT %s for %.1f damage"),
							*GetName(), *HitActor->GetName(), AttackDamage));
				}
			}
#endif
		}
	}
}

void AMeleeNPC::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// End damage window if still active
	if (bDamageWindowActive)
	{
		OnDamageWindowEnd();
	}

	// Clear timers
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);

	// End attack state
	bIsAttacking = false;

	// Start cooldown timer (attack cooldown starts after animation ends)
	// Note: Cooldown is measured from LastAttackTime which is set at attack start,
	// so we don't need an additional timer here - CanAttack() handles it

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Cyan,
			FString::Printf(TEXT("%s: Attack ended (interrupted: %s)"),
				*GetName(), bInterrupted ? TEXT("YES") : TEXT("NO")));
	}
#endif
}

void AMeleeNPC::OnAttackCooldownEnd()
{
	bAttackOnCooldown = false;
}

void AMeleeNPC::ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor)
	{
		return;
	}

	// Create damage event
	FPointDamageEvent DamageEvent;
	DamageEvent.Damage = AttackDamage;
	DamageEvent.DamageTypeClass = UDamageType_Melee::StaticClass();
	DamageEvent.HitInfo = HitResult;
	DamageEvent.ShotDirection = GetActorForwardVector();

	// Apply damage
	HitActor->TakeDamage(
		AttackDamage,
		DamageEvent,
		GetController(),
		this
	);
}

void AMeleeNPC::SpawnMeleeWeapon()
{
	if (!MeleeWeaponClass)
	{
		return;
	}

	// Spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	MeleeWeaponActor = GetWorld()->SpawnActor<AActor>(MeleeWeaponClass, GetActorTransform(), SpawnParams);

	if (MeleeWeaponActor)
	{
		// Attach to socket
		const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);
		MeleeWeaponActor->AttachToComponent(GetMesh(), AttachmentRule, MeleeWeaponSocket);

		UE_LOG(LogTemp, Log, TEXT("MeleeNPC %s: Spawned melee weapon %s attached to socket %s"),
			*GetName(), *MeleeWeaponActor->GetName(), *MeleeWeaponSocket.ToString());
	}
}

void AMeleeNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation)
{
	// End damage window if still active
	if (bDamageWindowActive)
	{
		OnDamageWindowEnd();
	}

	// Clear timers
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);

	// End attack state
	bIsAttacking = false;
	

	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation);
}
