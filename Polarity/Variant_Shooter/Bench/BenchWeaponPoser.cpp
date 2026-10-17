// BenchWeaponPoser.cpp

#include "Variant_Shooter/Bench/BenchWeaponPoser.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "TimerManager.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeaponHolder.h"

namespace
{
	const TCHAR* StepName(EBenchPoseStep Step)
	{
		switch (Step)
		{
		case EBenchPoseStep::Idle:             return TEXT("idle");
		case EBenchPoseStep::SingleShots:      return TEXT("single shots");
		case EBenchPoseStep::Burst:            return TEXT("burst");
		case EBenchPoseStep::ReloadRoundsLeft: return TEXT("reload, rounds left");
		case EBenchPoseStep::ReloadEmpty:      return TEXT("reload, empty");
		case EBenchPoseStep::Holster:          return TEXT("put away");
		case EBenchPoseStep::Draw:             return TEXT("take out");
		default:                               return TEXT("?");
		}
	}
}

ABenchWeaponPoser::ABenchWeaponPoser()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 230.0f));
	// Faces back along the poser's forward, towards whoever watches the soldier from the front.
	Label->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(16.0f);
	Label->SetTextRenderColor(FColor(35, 25, 15));
	Label->SetText(FText::FromString(TEXT("WEAPON POSER")));
}

void ABenchWeaponPoser::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SpawnSoldier();
	}
}

void ABenchWeaponPoser::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StepTimer);
	GetWorldTimerManager().ClearTimer(TriggerTimer);
	GetWorldTimerManager().ClearTimer(BurstEndTimer);
	if (Soldier)
	{
		Soldier->Destroy();
		Soldier = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ABenchWeaponPoser::ShowLabel(const FString& Text)
{
	if (Label)
	{
		Label->SetText(FText::FromString(Text));
	}
}

void ABenchWeaponPoser::SpawnSoldier()
{
	UWorld* const World = GetWorld();
	if (!World || !NPCClass)
	{
		ShowLabel(TEXT("WEAPON POSER: set NPCClass"));
		return;
	}
	if (Soldier)
	{
		Soldier->Destroy();
		Soldier = nullptr;
	}

	// Deferred: the gun and "no brain" have to be set before BeginPlay spawns the gun and possesses.
	const FTransform Where = GetActorTransform();
	AShooterNPC* const NPC = World->SpawnActorDeferred<AShooterNPC>(NPCClass, Where, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!NPC)
	{
		return;
	}
	NPC->AutoPossessAI = EAutoPossessAI::Disabled;
	NPC->SetCanBeDamaged(false);
	if (Weapons.IsValidIndex(WeaponIndex) && Weapons[WeaponIndex])
	{
		NPC->SetBenchWeaponOverride(Weapons[WeaponIndex]);
	}
	NPC->FinishSpawning(Where);
	Soldier = NPC;
	// Cannot be hurt (above), but still reports who shot it: a player's shot skips to the next gun.
	NPC->OnDamageTaken.AddDynamic(this, &ABenchWeaponPoser::OnSoldierShot);

	UE_LOG(LogTemp, Log, TEXT("[BENCH] poser: %s with %s"), *NPC->GetName(), *GetNameSafe(NPC->GetWeapon()));
	Step = EBenchPoseStep::Idle;
	RunStep();
}

float ABenchWeaponPoser::PlayOnBody(UAnimMontage* Montage) const
{
	USkeletalMeshComponent* const Body = Soldier ? Soldier->GetMesh() : nullptr;
	UAnimInstance* const Anim = Body ? Body->GetAnimInstance() : nullptr;
	if (!Montage || !Anim)
	{
		return 0.0f;
	}
	return Anim->Montage_Play(Montage, 1.0f);
}

void ABenchWeaponPoser::PullTrigger()
{
	AShooterWeapon* const Gun = Soldier ? Soldier->GetWeapon() : nullptr;
	if (!Gun)
	{
		return;
	}
	// A shot needs a round: the poser never runs dry outside the reload steps.
	if (Gun->GetBulletCount() <= 1)
	{
		Gun->SetBulletCount(Gun->GetMagazineSize());
	}
	Gun->StopFiring();
	Gun->StartFiring();

	// Single shots let go after each; a gun that is not full auto is re-pulled through the burst too.
	if (Step == EBenchPoseStep::SingleShots)
	{
		GetWorldTimerManager().SetTimer(TriggerTimer, this, &ABenchWeaponPoser::ReleaseTrigger, 0.08f, false);
	}
	else if (!Gun->IsFullAuto())
	{
		GetWorldTimerManager().SetTimer(TriggerTimer, this, &ABenchWeaponPoser::PullTrigger, 0.3f, false);
	}
}

void ABenchWeaponPoser::ReleaseTrigger()
{
	GetWorldTimerManager().ClearTimer(TriggerTimer);
	if (AShooterWeapon* const Gun = Soldier ? Soldier->GetWeapon() : nullptr)
	{
		Gun->StopFiring();
	}
	if (Step == EBenchPoseStep::SingleShots && --ShotsLeft > 0)
	{
		GetWorldTimerManager().SetTimer(TriggerTimer, this, &ABenchWeaponPoser::PullTrigger, 0.45f, false);
	}
}

void ABenchWeaponPoser::RunStep()
{
	AShooterWeapon* const Gun = Soldier ? Soldier->GetWeapon() : nullptr;
	if (!Gun)
	{
		ShowLabel(TEXT("WEAPON POSER: the soldier has no gun"));
		return;
	}

	// A gun without an Infima third person kit holds its first person mesh as it is, on its own
	// skeleton, with no third person reload: it shows crooked, and the poser says why.
	FString GunName = Gun->GetClass()->GetName().Replace(TEXT("BP_"), TEXT("")).Replace(TEXT("_C"), TEXT(""));
	if (!Gun->GetThirdPersonReloadMontage(EWeaponReloadStage::Primary))
	{
		GunName += TEXT(" (NO TP KIT)");
	}
	if (Weapons.Num() > 1)
	{
		GunName = FString::Printf(TEXT("%d/%d %s"), WeaponIndex + 1, Weapons.Num(), *GunName);
	}
	ShowLabel(FString::Printf(TEXT("%s\n%s\nshoot me: next gun"), *GunName, StepName(Step)));

	float Duration = Pause;
	switch (Step)
	{
	case EBenchPoseStep::Idle:
		Duration = Pause * 1.5f;
		break;

	case EBenchPoseStep::SingleShots:
		ShotsLeft = FMath::Max(1, SingleShots);
		PullTrigger();
		Duration = ShotsLeft * 0.55f + Pause;
		break;

	case EBenchPoseStep::Burst:
		PullTrigger();
		GetWorldTimerManager().SetTimer(BurstEndTimer, this, &ABenchWeaponPoser::ReleaseTrigger, BurstSeconds, false);
		Duration = BurstSeconds + Pause;
		break;

	case EBenchPoseStep::ReloadRoundsLeft:
	case EBenchPoseStep::ReloadEmpty:
		ReleaseTrigger();
		Gun->SetBulletCount(Step == EBenchPoseStep::ReloadEmpty ? 0 : FMath::Max(1, Gun->GetMagazineSize() / 2));
		if (Gun->StartReload())
		{
			Duration = Gun->GetReloadStageDuration(Gun->GetCurrentReloadStage()) + Pause;
		}
		else
		{
			ShowLabel(FString::Printf(TEXT("%s\n%s: the gun refused"), *GunName, StepName(Step)));
		}
		break;

	case EBenchPoseStep::Holster:
		Duration = PlayOnBody(Gun->GetHolsterMontageTP()) + Pause;
		if (!Gun->GetHolsterMontageTP())
		{
			ShowLabel(FString::Printf(TEXT("%s\n%s: no third person montage"), *GunName, StepName(Step)));
		}
		break;

	case EBenchPoseStep::Draw:
		Duration = PlayOnBody(Gun->GetDrawMontageTP()) + Pause;
		if (!Gun->GetDrawMontageTP())
		{
			ShowLabel(FString::Printf(TEXT("%s\n%s: no third person montage"), *GunName, StepName(Step)));
		}
		break;

	default:
		break;
	}

	GetWorldTimerManager().SetTimer(StepTimer, this, &ABenchWeaponPoser::NextStep, FMath::Max(0.2f, Duration), false);
}

void ABenchWeaponPoser::NextStep()
{
	Step = static_cast<EBenchPoseStep>(static_cast<uint8>(Step) + 1);
	if (Step >= EBenchPoseStep::Count)
	{
		// The round is over: the next gun, a new soldier holding it.
		if (Weapons.Num() > 1)
		{
			WeaponIndex = (WeaponIndex + 1) % Weapons.Num();
			SpawnSoldier();
			return;
		}
		Step = EBenchPoseStep::Idle;
	}
	RunStep();
}

void ABenchWeaponPoser::OnSoldierShot(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType,
	FVector HitLocation, AActor* DamageCauser)
{
	if (!HasAuthority() || DamagedNPC != Soldier || !DamageCauser || Weapons.Num() < 2)
	{
		return;
	}

	// Who fired: a gun or a projectile carries its shooter as instigator or owner. Only a player's shot
	// counts, so the soldier's own rounds and a passing enemy's never turn the page.
	const AActor* Shooter = DamageCauser->GetInstigator();
	if (!Shooter)
	{
		Shooter = DamageCauser->GetOwner();
	}
	if (!Shooter)
	{
		Shooter = DamageCauser;
	}
	if (!CoopPlayers::IsPlayer(Shooter))
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastSkipTime < SkipCooldown)
	{
		return;
	}
	LastSkipTime = Now;

	UE_LOG(LogTemp, Log, TEXT("[BENCH] poser: shot by %s, next gun"), *GetNameSafe(Shooter));
	// Next frame: this runs inside the soldier's TakeDamage, and the weapon still works with the soldier
	// (hit reaction, knockback) after it returns. Destroying it here would pull it from under them.
	GetWorldTimerManager().SetTimerForNextTick(this, &ABenchWeaponPoser::SkipToNextGun);
}

void ABenchWeaponPoser::SkipToNextGun()
{
	GetWorldTimerManager().ClearTimer(StepTimer);
	GetWorldTimerManager().ClearTimer(TriggerTimer);
	GetWorldTimerManager().ClearTimer(BurstEndTimer);
	if (AShooterWeapon* const Gun = Soldier ? Soldier->GetWeapon() : nullptr)
	{
		Gun->StopFiring();
	}
	WeaponIndex = (WeaponIndex + 1) % FMath::Max(1, Weapons.Num());
	SpawnSoldier();
}
