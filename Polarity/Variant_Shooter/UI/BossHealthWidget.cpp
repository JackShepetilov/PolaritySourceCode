// BossHealthWidget.cpp
// HUD widget for displaying boss health bar

#include "BossHealthWidget.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"

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
	case EBossPhase::Aerial:
		PhaseName = TEXT("Aerial Phase");
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
}

void UBossHealthWidget::NativeDestruct()
{
	UnbindFromBoss();
	Super::NativeDestruct();
}
