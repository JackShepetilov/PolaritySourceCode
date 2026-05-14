# Гайд по созданию апгрейдов

Шаблон создания нового апгрейда с поддержкой **многоуровневой прокачки** и **per-level настройки через DataAsset**. Reference-имплементация — `Upgrade_AirDash` + `UpgradeDefinition_AirDash`.

## Архитектура (TL;DR)

| Слой | Класс | Где живёт | Назначение |
|---|---|---|---|
| Данные | `UUpgradeDefinition` (base) + `UUpgradeDefinition_X` (subclass) | DataAsset в Content | Метаданные (имя/описание/иконка) + per-level tuning |
| Реестр | `UUpgradeRegistry.AllUpgrades` | DataAsset | Каталог всех доступных апгрейдов в игре |
| Runtime | `UUpgradeComponent` (base) + `UUpgrade_X` (subclass) | Дин. компонент на `ShooterCharacter` | Игровая логика апгрейда |
| Менеджер | `UUpgradeManagerComponent` | На `ShooterCharacter` | Грантит/удаляет апгрейды, держит уровни |
| UI | `UUpgradeChoiceWidget` + `UUpgradeCardWidget` | HUD | Модал выбора на level-up |
| XP | `UXPSubsystem` (категория `ESkillCategory`) | GameInstance | Триггерит level-up → choice |

**Ключевое правило:** на repeat-grant `UUpgradeManagerComponent::GrantUpgrade` повышает `CurrentLevel` и зовёт `OnLevelChanged(Old, New)`. На первом гранте — `OnUpgradeActivated` (CurrentLevel уже = 1).

---

## Шаги для нового апгрейда

### 1. Создай `UpgradeDefinition_X.h/.cpp`

Наследник `UUpgradeDefinition` с per-level tuning struct + массивом + helper'ом.

```cpp
// UpgradeDefinition_X.h
#pragma once
#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_X.generated.h"

USTRUCT(BlueprintType)
struct FXLevelData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "X", meta = (ClampMin = "0"))
    float SomeParam = 1.0f;
    // ... другие per-level параметры
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_X : public UUpgradeDefinition
{
    GENERATED_BODY()
public:
    /** Per-level tuning. Index 0 = Lv 1, index 1 = Lv 2. Length = MaxLevel (auto-synced). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "X")
    TArray<FXLevelData> LevelData;

    UFUNCTION(BlueprintPure, Category = "X")
    const FXLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
```

```cpp
// UpgradeDefinition_X.cpp
#include "UpgradeDefinition_X.h"

const FXLevelData& UUpgradeDefinition_X::GetLevelData(int32 Level) const
{
    static const FXLevelData FallbackEmpty;
    if (LevelData.Num() == 0) return FallbackEmpty;
    const int32 Idx = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
    return LevelData[Idx];
}

#if WITH_EDITOR
void UUpgradeDefinition_X::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    const int32 NewMaxLevel = FMath::Max(1, LevelData.Num());
    if (MaxLevel != NewMaxLevel) MaxLevel = NewMaxLevel;
}
#endif
```

### 2. Создай `Upgrade_X.h/.cpp`

Наследник `UUpgradeComponent` с тремя хуками.

```cpp
// Upgrade_X.h
#pragma once
#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_X.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "X"))
class POLARITY_API UUpgrade_X : public UUpgradeComponent
{
    GENERATED_BODY()
public:
    UUpgrade_X();
protected:
    virtual void OnUpgradeActivated() override;
    virtual void OnUpgradeDeactivated() override;
    virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;
private:
    void ApplyForLevel(int32 Level);

    // Если апгрейд модифицирует shared-asset (MovementSettings, MeleeAttackSettings и т.п.):
    void CaptureBaseline();
    void RevertToBaseline();
    /* ... cached baseline fields ... */
    bool bBaselineCaptured = false;
};
```

```cpp
// Upgrade_X.cpp
#include "Upgrade_X.h"
#include "UpgradeDefinition_X.h"
#include "ShooterCharacter.h"
// ... + конкретные includes для модифицируемых компонентов

void UUpgrade_X::OnUpgradeActivated()
{
    // Базовая активация (включить флаг, забиндить делегат и т.п.)
    CaptureBaseline();        // если модифицируем shared asset
    ApplyForLevel(CurrentLevel);
}

void UUpgrade_X::OnUpgradeDeactivated()
{
    // Откатить активацию
    RevertToBaseline();       // если был capture
}

void UUpgrade_X::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
    ApplyForLevel(NewLevel);
}

void UUpgrade_X::ApplyForLevel(int32 Level)
{
    const UUpgradeDefinition_X* Def = Cast<UUpgradeDefinition_X>(UpgradeDefinition);
    if (!Def) return;
    const FXLevelData& Data = Def->GetLevelData(Level);

    // Применить Data.* к компонентам персонажа
}
```

### 3. В редакторе

1. Создай `DA_X` Data Asset → класс `UpgradeDefinition_X`.
2. Заполни базовые поля: `UpgradeTag` (уникальный gameplay tag), `Category` (Movement/Melee/EMF/Weapon), `DisplayName`, `Description`, `Icon`, `Tier`, `ComponentClass = Upgrade_X`.
3. Заполни `LevelData` — по одной entry на уровень. `MaxLevel` подставится автоматически.
4. **Добавь `DA_X` в `UpgradeRegistry.AllUpgrades`** — без этого апгрейд не появится в choice-pool.

---

## Reference-имплементации

### AirDash (multi-level, modifies shared settings)

См. `UpgradeDefinition_AirDash.h/.cpp` и `Upgrade_AirDash.h/.cpp`.

- `FAirDashLevelData`: `MaxCharges`, `CooldownSeconds`, `ImpulseMultiplier`.
- `Upgrade_AirDash` модифицирует `ApexMovementComponent->MovementSettings` (shared DataAsset!), поэтому используется паттерн **CaptureBaseline + RevertToBaseline**.
- Дополнительно: при level-up синхронизируется `RemainingAirDashCount`, чтобы новые charges работали сразу, без приземления.

### Combo (multi-level, gameplay event subscriber)

См. `UpgradeDefinition_Combo.h/.cpp` и `Upgrade_Combo.h/.cpp`.

- `FComboLevelData`: `ResetWindow`, `ComboCountToMultiplier` (curve), `MaxMultiplier`, `bResetOnMiss`.
- Подписан на `OnMeleeHit` (fist) и `OnMeleeWeaponHit` (sword) + `OnMeleeAttackStarted/Ended` для детекции промахов.
- На `OnLevelChanged` пересчитывает multiplier с новой curve без сброса текущего combo.
- Передаёт multiplier downstream через `MeleeAttackComponent::ApplyComboSpeedMultiplier` и `AShooterWeapon_Melee::ApplyComboSpeedMultiplier`.

### Backstab (single-level в multi-level shape)

См. `UpgradeDefinition_Backstab.h` и `Upgrade_Backstab.h/.cpp`.

- Single-level упгрейд, но всё равно использует `TArray<FBackstabLevelData>` с одним элементом. Это позволяет дизайнеру добавить Lv 2 одной строкой массива позже, без рефактора кода.
- Демонстрирует override `GetMeleeDamageMultiplier(Target)` — возвращает `LD.DamageMultiplier` при выполнении условий (stunned + back cone), иначе 1.0f.
- Stateless: нет state'а в компоненте, все условия пересчитываются на каждый вызов multiplier'а.

### ChargedPunch (shared resource consumer)

См. `UpgradeDefinition_ChargedPunch.h` и `Upgrade_ChargedPunch.h/.cpp`.

- Потребитель **shared health-pickup pool** на `UUpgradeManagerComponent` (см. ниже).
- Подписан на `AShooterCharacter::OnMeleeChargeHoldStarted/Released` для hold-detection.
- Демонстрирует mesh-swap через `UMeleeAttackComponent::EnterMeleeMeshView/ExitMeleeMeshView` для проигрывания собственного монтажа в FP-виде.

---

## Доступные hooks (virtual на UUpgradeComponent)

Переопределяй только нужные тебе — все имеют пустые default-реализации.

### Lifecycle

| Hook | Когда вызывается | Назначение |
|---|---|---|
| `OnUpgradeActivated()` | Первый грант, `CurrentLevel = 1` | One-time setup (биндинги, флаги). Часто вызывай `ApplyForLevel(CurrentLevel)` в конце. |
| `OnUpgradeDeactivated()` | Removal / cleanup (death-reset, smerть) | Anti-setup: отвязать делегаты, восстановить shared values. |
| `OnLevelChanged(Old, New)` | Repeat grant (Lv N → N+1), **не** на первом | Применить новые per-level параметры. Не повторяй here то что уже делает OnUpgradeActivated. |

### Event hooks (вызываются UUpgradeManagerComponent::Notify*)

| Hook | Кто броадкастит | Использовать для |
|---|---|---|
| `OnWeaponFired()` | `AShooterWeapon::OnShotFired` через manager | Hitscan/projectile-based триггеры (Charge Flip, 360 Shot). |
| `OnWeaponChanged(Old, New)` | `ShooterCharacter` при swap | Re-bind на новое оружие (например, Combo для меча). |
| `OnOwnerTookDamage(Dmg, Causer)` | `ShooterCharacter::TakeDamage` | Defensive triggers (cooldown sense, panic effects). |
| `OnOwnerDealtDamage(Tgt, Dmg, Killed)` | `ShooterCharacter` при applied damage | On-kill бонусы, chain triggers. |
| `OnHealthPickupCollectedAtFullHP()` | `HealthPickup` через manager | Сейчас используется только для cosmetics — пул HP-пикапов теперь инкрементится централизованно в `Notify` сам, не в hook'е. |

### Damage multipliers (query'ятся при applied damage)

| Hook | Кто опрашивает | Когда |
|---|---|---|
| `GetDamageMultiplier(Target)` | `AShooterWeapon::ApplyHitscanDamage` через `UpgradeMgr->GetCombinedDamageMultiplier` | Перед `TakeDamage` хитсканом. |
| `GetMeleeDamageMultiplier(Target)` | `MeleeAttackComponent::ApplyDamage` + `ShooterWeapon_Melee::ApplyMeleeDamage` через `UpgradeMgr->GetCombinedMeleeDamageMultiplier` | Перед `TakeDamage` мили (кулак ИЛИ меч). Используется в **Backstab**. |

`1.0` = без изменения, `>1.0` = бонус, `<1.0` = штраф. Множители всех активных апгрейдов перемножаются.

### Делегаты на ShooterCharacter (для hold-based апгрейдов)

Не часть UpgradeComponent, но часто используются:

| Delegate | Когда фирится | Использует |
|---|---|---|
| `OnMeleeChargeHoldStarted` | Press melee button (Started) | ChargedPunch — старт hold-timer |
| `OnMeleeChargeHoldReleased` | Release melee button (Completed) | ChargedPunch — finalize/cancel |

---

## Shared resources

Если апгрейд использует **разделяемый между апгрейдами ресурс** (типа HP-пула):

- Храни ресурс на `UUpgradeManagerComponent` (или другом компоненте character'а), **не** в самом upgrade'е.
- Используй multicast delegate для UI/SFX subscribers.
- Каждый потребитель в `OnUpgradeActivated` может поднять cap ресурса если его definition требует больше дефолтного.

Reference: `UUpgradeManagerComponent::StoredHealthPickups` + `Add/Consume/ResetStoredHealthPickups` API. Используется `Upgrade_HealthBlast` (расходует весь пул на шотган) и `Upgrade_ChargedPunch` (тратит N в секунду).

---

## Правила и подводные камни

### MaxLevel = LevelData.Num()
`PostEditChangeProperty` автоматически синхронизирует `MaxLevel`. Никогда не редактируй `MaxLevel` руками — установится сам.

### `OnUpgradeActivated` vs `OnLevelChanged`
- **OnUpgradeActivated** зовётся **только на первом гранте**, когда `CurrentLevel = 1`. Используй для one-time setup (биндинги делегатов, флаги).
- **OnLevelChanged(Old, New)** зовётся **только на repeat-grants** (Lv 1→2, 2→3, ...), не на первом.
- В большинстве случаев в `OnUpgradeActivated` зови `ApplyForLevel(CurrentLevel)` после base-setup, чтобы не дублировать код применения.

### Shared assets (CRITICAL)
`MovementSettings`, `MeleeAttackSettings` и подобные — это **shared DataAsset**'ы, один экземпляр на проект. Если апгрейд их модифицирует:
- В `OnUpgradeActivated` сделай `CaptureBaseline()` — снимок исходных значений в private fields апгрейда.
- В `OnUpgradeDeactivated` сделай `RevertToBaseline()` — иначе при выходе в меню значения останутся искажёнными (в PIE — до перезапуска редактора, в shipping build — до нового запуска игры).

В singleplayer roguelite это OK. В coop с несколькими Character'ами — каждый персонаж будет ломать настройки другого; тогда нужна **per-actor копия Settings**, не модификация shared.

### Filter в Choice
`UUpgradeChoiceWidget::RollChoicesForCategory` исключает из пула те апгрейды, у которых `IsUpgradeMaxedOut = true` (т.е. `CurrentLevel >= MaxLevel`). Промежуточные уровни остаются в пуле для повышения.

### Event на UI карточке
`UUpgradeCardWidget::BP_OnInitialized` получает 6 параметров:
```
InName, InDescription, InIcon, InTier, InCurrentLevel, InMaxLevel
```
- `InCurrentLevel == 0` — апгрейд новый (показать "NEW" или "Lv 1").
- `InCurrentLevel >= 1` — апгрейд уже есть, выбор повысит до `InCurrentLevel + 1` (показать "Lv N → N+1").
- При `InCurrentLevel + 1 == InMaxLevel` — следующий уровень последний (можно выделить "MAX NEXT").

### Save / Load
`UUpgradeManagerComponent::GetUpgradeTagsForSave / RestoreUpgradesFromTags` сейчас сохраняют **только теги**, не уровни. Если понадобится save mid-run — расширить до `TMap<FGameplayTag, int32>` (тег → уровень).

---

## Чек-лист перед коммитом нового апгрейда

- [ ] `UpgradeDefinition_X.h/.cpp` — struct + TArray + GetLevelData + PostEditChangeProperty
- [ ] `Upgrade_X.h/.cpp` — OnUpgradeActivated/Deactivated/LevelChanged + ApplyForLevel
- [ ] Если модифицируется shared asset — CaptureBaseline + RevertToBaseline
- [ ] `DA_X` создан, `UpgradeTag` уникальный, `Category` правильная, `ComponentClass = Upgrade_X`
- [ ] `LevelData` заполнен N entries (= MaxLevel)
- [ ] `DA_X` добавлен в `UpgradeRegistry.AllUpgrades`
- [ ] DataAsset кривых XP (`XPConfig.SkillCurves`) содержит entry для соответствующей `ESkillCategory`
