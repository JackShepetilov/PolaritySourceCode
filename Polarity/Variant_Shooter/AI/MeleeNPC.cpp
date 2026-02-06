// MeleeNPC.cpp
// Implementation of melee combat NPC

#include "MeleeNPC.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../../AI/Components/MeleeRetreatComponent.h"
#include "EMFVelocityModifier.h"

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

	// Обновление интерполяции рывка если активен
	if (bIsDashing)
	{
		UpdateDashInterpolation(DeltaTime);
	}
	// Attack magnetism (only if not dashing or in knockback)
	else if (bIsAttacking && !bIsInKnockback)
	{
		UpdateAttackMagnetism(DeltaTime);
	}

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
	bHasDealtDamage = false;
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
	// Нельзя атаковать если мёртв, уже атакует, в knockback или в dash
	if (bIsDead || bIsAttacking || bIsInKnockback || bIsDashing)
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
	bool bInRange = Distance <= AttackRange;

	UE_LOG(LogTemp, Warning, TEXT("IsTargetInAttackRange: Distance=%.2f, AttackRange=%.2f, InRange=%s"),
		Distance, AttackRange, bInRange ? TEXT("YES") : TEXT("NO"));

	return bInRange;
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

	// Stop magnetism when damage window ends (same flag used when damage is dealt)
	bHasDealtDamage = true;

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

	// Mark that damage was dealt - stops magnetism
	bHasDealtDamage = true;

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

void AMeleeNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	// Парирование: если NPC в dash, умножаем knockback
	float DistanceMultiplier = 1.0f;
	if (bIsDashing)
	{
		DistanceMultiplier = DashKnockbackMultiplier;
		EndDash();

#if WITH_EDITOR
		if (bDebugMeleeTraces && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
				FString::Printf(TEXT("%s: PARRIED! Knockback x%.1f"), *GetName(), DashKnockbackMultiplier));
		}
#endif
	}

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

	// Применить knockback с множителем
	Super::ApplyKnockback(InKnockbackDirection, Distance * DistanceMultiplier, Duration, AttackerLocation, bKeepEMFEnabled);
}

// ==================== Attack Magnetism ====================

void AMeleeNPC::UpdateAttackMagnetism(float DeltaTime)
{
	// Check if magnetism is enabled, we have a valid target, and haven't dealt damage yet
	if (!bEnableAttackMagnetism || !CurrentMeleeTarget.IsValid() || bHasDealtDamage)
	{
		return;
	}

	AActor* Target = CurrentMeleeTarget.Get();
	if (!Target)
	{
		return;
	}

	// Get current positions (2D - horizontal only)
	FVector CurrentPos = GetActorLocation();
	FVector TargetPos = Target->GetActorLocation();

	// Calculate horizontal distance to target
	float DistanceToTarget = FVector::Dist2D(CurrentPos, TargetPos);

	// Stop if already close enough
	if (DistanceToTarget <= MagnetismStopDistance)
	{
		return;
	}

	// Calculate movement this frame
	float MoveDistance = MagnetismSpeed * DeltaTime;

	// Don't overshoot - clamp to remaining distance minus stop distance
	float RemainingDistance = DistanceToTarget - MagnetismStopDistance;
	MoveDistance = FMath::Min(MoveDistance, RemainingDistance);

	// Calculate direction to target (horizontal only)
	FVector DirectionToTarget = (TargetPos - CurrentPos).GetSafeNormal2D();

	// Calculate new position
	FVector NewPos = CurrentPos + DirectionToTarget * MoveDistance;

	// Keep original Z height
	NewPos.Z = CurrentPos.Z;

	// Move using SetActorLocation with sweep for collision detection
	SetActorLocation(NewPos, true);
}

// ==================== Dash Implementation ====================

bool AMeleeNPC::CanDash() const
{
	// Нельзя рывок если мёртв, уже в рывке, в knockback или атакует
	if (bIsDead || bIsDashing || bIsInKnockback || bIsAttacking)
	{
		return false;
	}

	// Проверка кулдауна
	if (LastDashTime > 0.0f)
	{
		float CurrentTime = GetWorld()->GetTimeSeconds();
		if ((CurrentTime - LastDashTime) < DashCooldown)
		{
			return false;
		}
	}

	return true;
}

bool AMeleeNPC::StartDash(const FVector& Direction, float Distance, AActor* TargetActor)
{
	// Проверка возможности рывка
	if (!CanDash())
	{
		return false;
	}

	// Нормализация направления (только горизонтальная плоскость)
	FVector DashDir = Direction.GetSafeNormal2D();
	if (DashDir.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("MeleeNPC::StartDash - Invalid direction (zero vector)"));
		return false;
	}

	// Вычисление начальной позиции
	FVector StartPos = GetActorLocation();

	// Вычисление конечной позиции
	FVector EndPos;
	if (bDashTracksTarget && IsValid(TargetActor))
	{
		// Dash к цели: вычисляем точку на расстоянии (AttackRange - Buffer) от цели
		FVector ToTarget = TargetActor->GetActorLocation() - StartPos;
		float DistanceToTarget = ToTarget.Size2D();
		float DesiredDistance = AttackRange - DashAttackRangeBuffer;

		// Если уже в нужной дистанции, не делаем dash
		if (DistanceToTarget <= DesiredDistance)
		{
			UE_LOG(LogTemp, Verbose, TEXT("MeleeNPC::StartDash - Already at desired distance from target"));
			return false;
		}

		// Вычисляем конечную точку: от цели в направлении к нам на расстояние DesiredDistance
		FVector DirFromTarget = (StartPos - TargetActor->GetActorLocation()).GetSafeNormal2D();
		EndPos = TargetActor->GetActorLocation() + DirFromTarget * DesiredDistance;

		// Сохраняем цель для tracking
		DashTargetActor = TargetActor;
	}
	else
	{
		// Статичный dash в направлении
		EndPos = StartPos + DashDir * Distance;
		DashTargetActor = nullptr;
	}

	// Валидация пути (NavMesh + коллизии)
	if (!ValidateDashPath(StartPos, EndPos))
	{
		UE_LOG(LogTemp, Verbose, TEXT("MeleeNPC::StartDash - Path validation failed"));
		return false;
	}

	// Сохранение параметров рывка
	DashStartPosition = StartPos;
	DashTargetPosition = EndPos;
	DashDirection = DashDir;
	DashElapsedTime = 0.0f;
	DashTotalDuration = DashDuration;
	LastDashTime = GetWorld()->GetTimeSeconds();
	bIsDashing = true;

	// Остановить AI pathfinding
	if (AController* MyController = GetController())
	{
		if (AAIController* AIController = Cast<AAIController>(MyController))
		{
			if (UPathFollowingComponent* PathComp = AIController->GetPathFollowingComponent())
			{
				PathComp->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Reset);
			}
			AIController->StopMovement();
		}
	}

	// Отключить EMF силы во время рывка (как в knockback)
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	// Остановить текущее движение
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->StopActiveMovement();
		CharMovement->Velocity = FVector::ZeroVector;
	}

	// Воспроизвести анимацию рывка если указана
	if (DashMontage)
	{
		if (USkeletalMeshComponent* TPMesh = GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				// Рассчитать скорость воспроизведения чтобы соответствовать длительности рывка
				float MontageLength = DashMontage->GetPlayLength();
				float PlayRate = (MontageLength > 0.0f) ? MontageLength / DashTotalDuration : 1.0f;
				AnimInstance->Montage_Play(DashMontage, PlayRate);
			}
		}
	}

	// Повернуть в направлении рывка
	if (!DashDir.IsNearlyZero())
	{
		SetActorRotation(DashDir.Rotation());
	}

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Blue,
			FString::Printf(TEXT("%s: Started DASH - Dir=(%.2f,%.2f,%.2f), Dist=%.0f, Duration=%.2f"),
				*GetName(), DashDir.X, DashDir.Y, DashDir.Z, Distance, DashTotalDuration));
	}
#endif

	return true;
}

void AMeleeNPC::UpdateDashInterpolation(float DeltaTime)
{
	if (!bIsDashing || DashTotalDuration <= 0.0f)
	{
		return;
	}

	// Обновить прошедшее время
	DashElapsedTime += DeltaTime;

	// Обновить целевую позицию если tracking включен
	if (bDashTracksTarget && DashTargetActor.IsValid())
	{
		AActor* Target = DashTargetActor.Get();
		FVector CurrentPos = GetActorLocation();
		FVector ToTarget = Target->GetActorLocation() - CurrentPos;
		float DistanceToTarget = ToTarget.Size2D();
		float DesiredDistance = AttackRange - DashAttackRangeBuffer;

		// Обновляем целевую точку: от цели к нам на расстояние DesiredDistance
		FVector DirFromTarget = (CurrentPos - Target->GetActorLocation()).GetSafeNormal2D();
		DashTargetPosition = Target->GetActorLocation() + DirFromTarget * DesiredDistance;

		// Если уже достигли нужной дистанции, завершаем dash
		if (DistanceToTarget <= DesiredDistance * 1.1f) // 10% tolerance
		{
			EndDash();
			return;
		}
	}

	// Вычислить альфу интерполяции
	float Alpha = FMath::Clamp(DashElapsedTime / DashTotalDuration, 0.0f, 1.0f);

	// Ease-out для плавного завершения (как в knockback)
	// Линейная скорость для первых 90%, замедление для последних 10%
	float EasedAlpha;
	if (Alpha < 0.9f)
	{
		EasedAlpha = Alpha;
	}
	else
	{
		float LastSegmentAlpha = (Alpha - 0.9f) / 0.1f;
		float EasedSegment = FMath::InterpEaseOut(0.0f, 0.1f, LastSegmentAlpha, 2.0f);
		EasedAlpha = 0.9f + EasedSegment;
	}

	// Вычислить следующую позицию
	FVector CurrentPos = GetActorLocation();
	FVector NextPos = FMath::Lerp(DashStartPosition, DashTargetPosition, EasedAlpha);

	// Проверка коллизий по пути
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	FHitResult Hit;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.0f;
	float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f;

	bool bBlocked = GetWorld()->SweepSingleByChannel(
		Hit,
		CurrentPos,
		NextPos,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
		QueryParams
	);

	if (bBlocked && Hit.bBlockingHit)
	{
		// Столкнулись с препятствием - остановить рывок
		NextPos = Hit.Location;
		EndDash();

#if WITH_EDITOR
		if (bDebugMeleeTraces && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("%s: Dash blocked by %s"),
					*GetName(), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("World")));
		}
#endif
		return;
	}

	// Переместить персонажа
	SetActorLocation(NextPos, true);

	// Обновить velocity для визуала и анимаций
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		FVector FrameVelocity = (NextPos - CurrentPos) / DeltaTime;
		CharMovement->Velocity = FrameVelocity;
	}

	// Проверка завершения рывка
	if (Alpha >= 1.0f)
	{
		EndDash();
	}
}

void AMeleeNPC::EndDash()
{
	if (!bIsDashing)
	{
		return;
	}

	bIsDashing = false;

	// Остановить velocity
	if (UCharacterMovementComponent* CharMovement = GetCharacterMovement())
	{
		CharMovement->Velocity = FVector::ZeroVector;
	}

	// Включить EMF обратно
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(true);
	}

	// Остановить анимацию рывка если играет
	if (DashMontage)
	{
		if (USkeletalMeshComponent* TPMesh = GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				if (AnimInstance->Montage_IsPlaying(DashMontage))
				{
					AnimInstance->Montage_Stop(0.2f, DashMontage);
				}
			}
		}
	}

#if WITH_EDITOR
	if (bDebugMeleeTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Cyan,
			FString::Printf(TEXT("%s: Dash ENDED"), *GetName()));
	}
#endif
}

bool AMeleeNPC::ValidateDashPath(const FVector& StartPos, const FVector& EndPos) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. Проверка NavMesh - конечная точка должна быть на навигационной сетке
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (NavSys)
	{
		FNavLocation NavLoc;
		// Расширенный query extent для учёта высоты
		FVector QueryExtent(50.0f, 50.0f, 100.0f);
		bool bOnNavMesh = NavSys->ProjectPointToNavigation(EndPos, NavLoc, QueryExtent);

		if (!bOnNavMesh)
		{
			UE_LOG(LogTemp, Verbose, TEXT("ValidateDashPath: End position not on NavMesh"));
			return false;
		}
	}

	// 2. Проверка коллизий по пути с помощью sphere trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	FHitResult Hit;
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.0f;

	bool bBlocked = World->SweepSingleByChannel(
		Hit,
		StartPos,
		EndPos,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(CapsuleRadius),
		QueryParams
	);

	if (bBlocked && Hit.bBlockingHit)
	{
		UE_LOG(LogTemp, Verbose, TEXT("ValidateDashPath: Path blocked by %s at distance %.1f"),
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("World"),
			FVector::Dist(StartPos, Hit.Location));
		return false;
	}

	return true;
}
