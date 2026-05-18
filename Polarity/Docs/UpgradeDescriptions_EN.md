# Upgrade Descriptions (EN)

Ready-to-use `DisplayName`, `Description` and suggested **Stat Labels** for all 14 upgrades. Style — short, "gigachad", Hades + ULTRAKILL energy. Descriptions don't spell out **numbers** — numbers come from `LevelData` and surface separately via `GetDisplayedStats(Level)` as stat rows on the card.

**How to use:**
1. Copy `DisplayName` / `Description` into the corresponding `DA_*` asset.
2. For each `UpgradeDefinition_X`, override `GetDisplayedStats(Level)` to emit the recommended **stat rows** (or fill `StaticStats` in the DataAsset directly). Reference implementation — `UpgradeDefinition_AirDash`.

---

## 360Shot — `Spin to Win`

**Description:** _Pull off a full 360° camera spin and your next hitscan shot punches through anything you're aiming at. Want more damage — spin harder._

**Stat rows:**
- `Bonus Damage` → `BonusDamage`
- `Spin Window` → `SpinTimeWindow` ("Xs")
- `Charge Duration` → `ChargedDuration` ("Xs")
- `Cooldown` → `CooldownDuration` ("Xs")

---

## AirDash — `Air Dash`

**Description:** _The air is your highway. Chain dashes mid-flight without ever touching the ground._

**Stat rows:**
- `Air Dashes` → `MaxCharges`
- `Cooldown` → `CooldownSeconds` ("Xs")
- `Impulse` → `ImpulseMultiplier` ("xX.X" — hide if 1.0)

> Reference implementation already in `UpgradeDefinition_AirDash.cpp`.

---

## AirKick — `Air Mail`

**Description:** _Kick a flying prop mid-air and it travels exactly where you're looking. Level 2 — it detonates on impact._

**Stat rows:**
- `Impact Damage` → `ImpactDamage` (Lv1) **or** `Explosion Damage` → `FixedExplosionDamage` (Lv2)
- `Explosion Radius` → `FixedExplosionRadius` (Lv2 only)

---

## Backstab — `Cheap Shot`

**Description:** _Hit a stunned enemy from behind — crit. Sneak in, finish it. Nobody saw a thing._

**Stat rows:**
- `Damage Multiplier` → `DamageMultiplier` ("xX.X")
- `Back Cone` → `BackConeHalfAngle` ("X°")
- `Requires Explosion Stun` → `bRequireStunnedByExplosion` ("Yes/No")

---

## Bandolier — `Bandolier`

**Description:** _Carry multiple copies of the same gun in your pocket. Run dry on one — the next is already in your hand._

**Stat rows:**
- `Max Copies` → `MaxCopies`

---

## ChargeFlip — `Coinflip`

**Description:** _Shoot your own EMF projectile and it detonates with scorching beams across every visible target. Each extra projectile in the chain — another blast._

> Direct nod to the coin from ULTRAKILL.

**Stat rows:**
- `Damage Multiplier` → `DamageMultiplier` ("xX.X")
- `Charge per Hit` → `IonizationChargePerHit`
- `Max Charge` → `MaxIonizationCharge`
- `Chain Depth` → `MaxChainDepth`

---

## ChargedPunch — `Falcon Punch`

**Description:** _Hold the punch button — your fist gets denser. Release and you ram through everything in your path. Eats your stored full-HP pickups._

**Stat rows:**
- `Min Hold` → `MinHoldTime` ("Xs")
- `Max Hold` → `MaxHoldTime` ("Xs")
- `Pickups per Sec` → `PickupsPerSecond` ("X/s")
- `Max Distance` → `MaxDistance`
- `Max Bonus Damage` → `MaxBonusDamage`

---

## Combo — `Killing Spree`

**Description:** _Every successful hit in the chain — the next one faster. Miss or pause — the counter burns. Don't slow down._

**Stat rows:**
- `Reset Window` → `ResetWindow` ("Xs")
- `Max Multiplier` → `MaxMultiplier` ("xX.X")
- `Reset on Miss` → `bResetOnMiss` ("Yes/No")
- The `ComboCountToMultiplier` curve is better visualized separately (BP can render a small graph) or just omit it from the card.

---

## DropKick — `Drop Kick`

**Description:** _A boot from a falling stance. Jumped, aimed, punished._

**Stat rows:** (no per-level data — separate tuning lives in `MeleeAttackComponent`)
Optional info row:
- `Unlocked` → "Yes"

Or leave the card stat-less and let the description carry it.

---

## ForwardMomentum — `Testosterone Boost`

**Description:** _Run at the enemy — hit harder. Run away — weaker. Always forward._

> `Testosterone Boost` is the locked-in DisplayName (from `CombatMechanics.md`).

**Stat rows:**
- `Forward Bonus` → `ForwardBonusMultiplier` ("+X%")
- `Backward Penalty` → `BackwardPenaltyMultiplier` ("-X%")
- `Min Speed` → `MinSpeedThreshold`
- `Full Effect Speed` → `MaxSpeedForFullEffect`

---

## HealthBlast — `Vitality Cannon`

**Description:** _Health pickups at full HP don't go to waste — they stack. Trigger an empty channel and they fan out like a shotgun across whatever's in front of you._

**Stat rows:**
- `Damage / Pickup` → `DamagePerPickup`
- `Max Stored` → `MaxStoredPickups`
- `Spread` → `SpreadHalfAngle` ("X°")
- `Cooldown` → `Cooldown` ("Xs")

---

## PistolStun — `Static Lock`

**Description:** _Ionized — stunned. Your pistol doesn't just mark the target anymore — it locks it down._

**Stat rows:**
- `Stun Duration` → `StunDuration` ("Xs")
- `Per-Target Cooldown` → `StunCooldownPerTarget` ("Xs")

---

## SuppressionFire — `Plot Armor`

**Description:** _Run and gun — enemies stop hitting you. The script won't let the protagonist die on stream in front of millions._

> `Plot Armor` is the locked-in DisplayName (from memory). Speedrunner-focused.

**Stat rows:**
- `Min Suppression` → `MinSuppressionDuration` ("Xs")
- `Max Suppression` → `MaxSuppressionDuration` ("Xs")
- `Min Speed` → `MinSpeedThreshold`
- `Full Effect Speed` → `MaxSpeedForFullEffect`
- `Diminishing Returns` → `DiminishingReturnsFactor`

---

## TractorBeam — `Tractor Beam`

**Description:** _Every pistol hit reels the enemy toward you. Level 2 — full winch until pinned, plus a bonus melee on the dragged target._

**Stat rows:**
- `Pull Distance` → `PullDistance`
- `Pull Duration` → `PullDuration` ("Xs")
- `Max Pull Range` → `MaxPullDistance`
- `Melee Bonus` → `MeleeDamageMultiplier` ("xX.X", Lv2 only)
- `Melee Knockback` → `MeleeKnockbackMultiplier` ("xX.X", Lv2 only)

---

## Category breakdown

For even Choice-pool distribution:

- **Movement** (2): AirDash, DropKick
- **Melee** (5): AirKick, Backstab, ChargedPunch, Combo, ForwardMomentum
- **EMF** (3): ChargeFlip, HealthBlast, TractorBeam
- **Weapon** (4): 360Shot, Bandolier, PistolStun, SuppressionFire

If you want the categories balanced — some can be reclassified (e.g. `ForwardMomentum` is Movement+Melee hybrid; `Combo` accelerates weapons too — viable as Weapon).

---

## `FUpgradeStat::Value` formatting conventions

For consistent-looking cards:

| Data type | Format | Example |
|---|---|---|
| Integer (count) | `FText::AsNumber(N)` | `3` |
| Seconds | `FString::Printf(TEXT("%.2fs"), F)` | `0.50s` |
| Multiplier | `FString::Printf(TEXT("x%.2f"), F)` | `x1.50` |
| Percent | `FString::Printf(TEXT("+%.0f%%"), F * 100.f)` | `+25%` |
| Angle | `FString::Printf(TEXT("%.0f°"), F)` | `45°` |
| Bool | `Yes` / `No` via `FText::FromString` | `Yes` |

Reference in code — `UpgradeDefinition_AirDash::GetDisplayedStats`.
