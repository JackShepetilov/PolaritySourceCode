# Описания апгрейдов

Готовые `DisplayName`, `Description` и предлагаемые **Stat Labels** для всех 14 апгрейдов. Стилистически — короткие, "гигачадские", в духе Hades + ULTRAKILL. Описания не объясняют **числа** — числа дизайнер настраивает в `LevelData` и они проявляются через `GetDisplayedStats(Level)` отдельной строкой.

**Как использовать:**
1. Копируй `DisplayName` / `Description` в соответствующий `DA_*` ассет.
2. Для каждого Definition_X переопредели `GetDisplayedStats(Level)` чтобы выдать рекомендованные **stat rows** (либо набивай `StaticStats` в DataAsset напрямую). Reference-имплементация — `UpgradeDefinition_AirDash`.

---

## 360Shot — `Spin to Win`

**Description:** _Прокрути камерой полный оборот — и следующий хитскан-выстрел пробьёт всё, во что прицелишься. Хочешь больше урона — больше вращайся._

**Stat rows:**
- `Bonus Damage` → `BonusDamage`
- `Spin Window` → `SpinTimeWindow` ("Xs")
- `Charge Duration` → `ChargedDuration` ("Xs")
- `Cooldown` → `CooldownDuration` ("Xs")

---

## AirDash — `Air Dash`

**Description:** _Воздух — твой коридор. Цепочка рывков подряд, без касания пола._

**Stat rows:**
- `Air Dashes` → `MaxCharges`
- `Cooldown` → `CooldownSeconds` ("Xs")
- `Impulse` → `ImpulseMultiplier` ("xX.X", опционально — скрыть если 1.0)

> Reference-имплементация уже есть в `UpgradeDefinition_AirDash.cpp`.

---

## AirKick — `Air Mail`

**Description:** _Пни летящий проп в воздухе — он улетит ровно туда, куда смотришь. На втором уровне детонирует при ударе._

**Stat rows:**
- `Impact Damage` → `ImpactDamage` (Lv1) **или** `Explosion Damage` → `FixedExplosionDamage` (Lv2)
- `Explosion Radius` → `FixedExplosionRadius` (Lv2 only)

---

## Backstab — `Cheap Shot`

**Description:** _Удар сзади по оглушённому — крит. Подкрался — добил. Никто не узнает._

**Stat rows:**
- `Damage Multiplier` → `DamageMultiplier` ("xX.X")
- `Back Cone` → `BackConeHalfAngle` ("X°")
- `Requires Explosion Stun` → `bRequireStunnedByExplosion` ("Yes/No")

---

## Bandolier — `Bandolier`

**Description:** _Носи несколько копий одного ствола в кармане. Кончатся патроны в одном — следующий уже в руке._

**Stat rows:**
- `Max Copies` → `MaxCopies`

---

## ChargeFlip — `Coinflip`

**Description:** _Выстрели по своему же EMF-снаряду — он детонирует жгучими лучами по всем видимым целям. Каждый дополнительный снаряд в пределах цепи — новый взрыв._

> Прямая отсылка к монетке из ULTRAKILL.

**Stat rows:**
- `Damage Multiplier` → `DamageMultiplier` ("xX.X")
- `Charge per Hit` → `IonizationChargePerHit`
- `Max Charge` → `MaxIonizationCharge`
- `Chain Depth` → `MaxChainDepth`

---

## ChargedPunch — `Falcon Punch`

**Description:** _Удержи кулак — он становится плотнее. Отпусти — и ты пройдёшь сквозь всё на пути, тараня врагов. Жрёт твои хилки в полной жизни._

**Stat rows:**
- `Min Hold` → `MinHoldTime` ("Xs")
- `Max Hold` → `MaxHoldTime` ("Xs")
- `Pickups per Sec` → `PickupsPerSecond` ("X/s")
- `Max Distance` → `MaxDistance`
- `Max Bonus Damage` → `MaxBonusDamage`

---

## Combo — `Killing Spree`

**Description:** _Каждый успешный удар в цепочке — следующий быстрее. Промах или пауза — счётчик сгорает. Не сбавляй темп._

**Stat rows:**
- `Reset Window` → `ResetWindow` ("Xs")
- `Max Multiplier` → `MaxMultiplier` ("xX.X")
- `Reset on Miss` → `bResetOnMiss` ("Yes/No")
- Кривую `ComboCountToMultiplier` лучше визуализировать отдельно (в BP можно нарисовать график) или не показывать в карточке вовсе.

---

## DropKick — `Drop Kick`

**Description:** _Пинок ногой из падающей позиции. Прыгнул — нацелился — наказал._

**Stat rows:** (нет per-level данных — отдельный набор настроек живёт в `MeleeAttackComponent`)
Можно вывести один информационный row:
- `Unlocked` → "Yes"

Либо оставить карточку без stat-rows и положиться только на Description.

---

## ForwardMomentum — `Testosterone Boost`

**Description:** _Бежишь прямо на врага — бьёшь сильнее. Бежишь от врага — слабее. Беги вперёд. Всегда._

> Эта и есть `Testosterone Boost` из `CombatMechanics.md` — DisplayName зафиксирован.

**Stat rows:**
- `Forward Bonus` → `ForwardBonusMultiplier` ("+X%")
- `Backward Penalty` → `BackwardPenaltyMultiplier` ("-X%")
- `Min Speed` → `MinSpeedThreshold`
- `Full Effect Speed` → `MaxSpeedForFullEffect`

---

## HealthBlast — `Vitality Cannon`

**Description:** _Хилки в полной жизни не пропадают — они копятся. На пустой канализации они разлетаются дробовиком и валят всё, что окажется впереди._

**Stat rows:**
- `Damage / Pickup` → `DamagePerPickup`
- `Max Stored` → `MaxStoredPickups`
- `Spread` → `SpreadHalfAngle` ("X°")
- `Cooldown` → `Cooldown` ("Xs")

---

## PistolStun — `Static Lock`

**Description:** _Ионизированный — оглушённый. Пистолет теперь не просто метит цель — он её парализует._

**Stat rows:**
- `Stun Duration` → `StunDuration` ("Xs")
- `Per-Target Cooldown` → `StunCooldownPerTarget` ("Xs")

---

## SuppressionFire — `Plot Armor`

**Description:** _Беги и стреляй — враги перестают попадать по тебе. Сценарий не отпустит главного героя в эфире у миллионов._

> Зафиксированный DisplayName — `Plot Armor` (memory). Для спидранеров.

**Stat rows:**
- `Min Suppression` → `MinSuppressionDuration` ("Xs")
- `Max Suppression` → `MaxSuppressionDuration` ("Xs")
- `Min Speed` → `MinSpeedThreshold`
- `Full Effect Speed` → `MaxSpeedForFullEffect`
- `Diminishing Returns` → `DiminishingReturnsFactor`

---

## TractorBeam — `Tractor Beam`

**Description:** _Каждое попадание из пистолета подтягивает врага к тебе. На втором уровне — режим лебёдки до упора плюс бонусный мили на пристёгнутом._

**Stat rows:**
- `Pull Distance` → `PullDistance`
- `Pull Duration` → `PullDuration` ("Xs")
- `Max Pull Range` → `MaxPullDistance`
- `Melee Bonus` → `MeleeDamageMultiplier` ("xX.X", Lv2 only)
- `Melee Knockback` → `MeleeKnockbackMultiplier` ("xX.X", Lv2 only)

---

## Категории — итоговая разбивка

Чтобы level-up choice выпадал равномерно по скиллам:

- **Movement** (2): AirDash, DropKick
- **Melee** (4): AirKick, Backstab, ChargedPunch, Combo, ForwardMomentum
- **EMF** (3): ChargeFlip, HealthBlast, TractorBeam
- **Weapon** (3): 360Shot, Bandolier, PistolStun, SuppressionFire

Если категорий хочется одинаково — можно реклассифицировать (например, `ForwardMomentum` это Movement + Melee hybrid; `Combo` ускоряет оружие тоже — можно перевести в Weapon).

---

## Соглашения по форматированию `Value` в `FUpgradeStat`

Чтобы карточки выглядели единообразно:

| Тип данных | Формат | Пример |
|---|---|---|
| Целое (count) | `FText::AsNumber(N)` | `3` |
| Секунды | `FString::Printf(TEXT("%.2fs"), F)` | `0.50s` |
| Множитель | `FString::Printf(TEXT("x%.2f"), F)` | `x1.50` |
| Процент | `FString::Printf(TEXT("+%.0f%%"), F * 100.f)` | `+25%` |
| Угол | `FString::Printf(TEXT("%.0f°"), F)` | `45°` |
| Bool | `Yes` / `No` через `FText::FromString` | `Yes` |

Reference в коде — `UpgradeDefinition_AirDash::GetDisplayedStats`.
