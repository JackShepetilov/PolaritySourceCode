// BossHealthWidget.cpp
// HUD widget for displaying boss health bar

#include "BossHealthWidget.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Polarity/Arena/ArenaManager.h"

void UBossHealthWidget::ShowForBoss(ABossCharacter* Boss)
{
	if (!Boss)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossHealthWidget] ShowForBoss called with null boss"));
		return;
	}

	// Unbind from previous boss if any
	UnbindFromBoss();

	// Store reference
	TrackedBoss = Boss;

	// Cache max HP
	CachedMaxHP = Boss->CurrentHP;
	CurrentHealthPercent = 1.0f;

	// Bind to boss events
	Boss->OnDamageTaken.AddDynamic(this, &UBossHealthWidget::OnBossDamageTaken);
	Boss->OnPhaseChanged.AddDynamic(this, &UBossHealthWidget::OnBossPhaseChanged);
	Boss->OnBossDefeated.AddDynamic(this, &UBossHealthWidget::OnBossDefeated);

	// Bind to the boss's arena so the widget can drive the datacenter HP bar alongside Posture.
	// The datacenter is the *true* health pool — boss Posture (already-bound CurrentHP) only
	// gates the finisher window.
	if (AArenaManager* Arena = Boss->GetLinkedArena())
	{
		TrackedArena = Arena;
		// Map raw prop percent through the victory-threshold formula so the bar reads
		// "datacenter HP" instead of raw prop count: full at start, empty exactly when
		// DatacenterVictoryDestroyedPercent of the props are gone.
		const float RawPercent = Arena->GetCurrentPropPercent();
		const float Threshold = FMath::Clamp(Arena->DatacenterVictoryDestroyedPercent, KINDA_SMALL_NUMBER, 1.0f);
		const float Floor = 1.0f - Threshold;
		CurrentDatacenterPercent = FMath::Clamp((RawPercent - Floor) / Threshold, 0.0f, 1.0f);
		Arena->OnPropPercentChanged.AddDynamic(this, &UBossHealthWidget::OnDatacenterPropPercentChanged);
		UE_LOG(LogTemp, Log, TEXT("[BossHealthWidget] Subscribed to arena %s — raw=%.2f, effective datacenter HP=%.2f (threshold %.0f%%)"),
			*Arena->GetName(), RawPercent, CurrentDatacenterPercent, Threshold * 100.0f);
	}
	else
	{
		CurrentDatacenterPercent = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[BossHealthWidget] Boss %s has no LinkedArena — datacenter bar will stay at 1.0"),
			*Boss->GetName());
	}

	// Show the widget
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// Get boss name (can be overridden in Blueprint)
	FString BossName = Boss->GetName();
	// Strip prefixes like "BP_" if present
	if (BossName.StartsWith(TEXT("BP_")))
	{
		BossName = BossName.RightChop(3);
	}

	// Notify Blueprint
	BP_OnShow(BossName, CurrentHealthPercent);

	UE_LOG(LogTemp, Log, TEXT("[BossHealthWidget] Now tracking boss: %s (HP: %.0f)"), *BossName, CachedMaxHP);
}

void UBossHealthWidget::Hide()
{
	bool bWasDefeated = TrackedBoss.IsValid() && TrackedBoss->IsInFinisherPhase();

	// Notify Blueprint before hiding
	BP_OnHide(bWasDefeated);

	// Unbind and hide
	UnbindFromBoss();
	SetVisibility(ESlateVisibility::Collapsed);
}

float UBossHealthWidget::GetHealthPercent() const
{
	if (!TrackedBoss.IsValid())
	{
		return 0.0f;
	}

	return FMath::Clamp(TrackedBoss->CurrentHP / CachedMaxHP, 0.0f, 1.0f);
}

void UBossHealthWidget::OnBossDamageTaken(AShooterNPC* Boss, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser)
{
	if (!TrackedBoss.IsValid() || Boss != TrackedBoss.Get())
	{
		return;
	}

	float OldHealthPercent = CurrentHealthPercent;
	CurrentHealthPercent = GetHealthPercent();

	// Notify Blueprint
	BP_OnHealthChanged(CurrentHealthPercent, OldHealthPercent, Damage);
}

void UBossHealthWidget::OnBossPhaseChanged(EBossPhase OldPhase, EBossPhase NewPhase)
{
	// Convert phase enum to display name
	FString PhaseName;
	int32 PhaseIndex = static_cast<int32>(NewPhase);

	switch (NewPhase)
	{
	case EBossPhase::Ground:
		PhaseName = TEXT("Ground Phase");
		break;
	case EBossPhase::Finisher:
		PhaseName = TEXT("Finisher");
		break;
	default:
		PhaseName = TEXT("Unknown");
		break;
	}

	// Notify Blueprint
	BP_OnPhaseChanged(PhaseIndex, PhaseName);
}

void UBossHealthWidget::OnBossDefeated()
{
	// Boss was defeated - hide with victory animation
	BP_OnHide(true);

	// Unbind
	UnbindFromBoss();
}

void UBossHealthWidget::UnbindFromBoss()
{
	if (TrackedBoss.IsValid())
	{
		ABossCharacter* Boss = TrackedBoss.Get();
		Boss->OnDamageTaken.RemoveDynamic(this, &UBossHealthWidget::OnBossDamageTaken);
		Boss->OnPhaseChanged.RemoveDynamic(this, &UBossHealthWidget::OnBossPhaseChanged);
		Boss->OnBossDefeated.RemoveDynamic(this, &UBossHealthWidget::OnBossDefeated);
	}

	TrackedBoss.Reset();
	UnbindFromArena();
}

void UBossHealthWidget::UnbindFromArena()
{
	if (AArenaManager* Arena = TrackedArena.Get())
	{
		Arena->OnPropPercentChanged.RemoveDynamic(this, &UBossHealthWidget::OnDatacenterPropPercentChanged);
	}
	TrackedArena.Reset();
}

void UBossHealthWidget::OnDatacenterPropPercentChanged(float RemainingPercent, int32 AliveCount)
{
	const float OldPercent = CurrentDatacenterPercent;

	// Same threshold remap as ShowForBoss: bar reads "datacenter HP", reaching 0 when
	// DatacenterVictoryDestroyedPercent of the props have been destroyed.
	float EffectivePercent = FMath::Clamp(RemainingPercent, 0.0f, 1.0f);
	if (AArenaManager* Arena = TrackedArena.Get())
	{
		const float Threshold = FMath::Clamp(Arena->DatacenterVictoryDestroyedPercent, KINDA_SMALL_NUMBER, 1.0f);
		const float Floor = 1.0f - Threshold;
		EffectivePercent = FMath::Clamp((EffectivePercent - Floor) / Threshold, 0.0f, 1.0f);
	}

	CurrentDatacenterPercent = EffectivePercent;
	BP_OnDatacenterHealthChanged(CurrentDatacenterPercent, OldPercent, AliveCount);
}

void UBossHealthWidget::NativeDestruct()
{
	UnbindFromBoss();
	Super::NativeDestruct();
}
