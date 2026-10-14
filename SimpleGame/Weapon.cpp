#include "stdafx.h"
#include "Weapon.h"
#include <algorithm>
#include <cmath>

namespace combat
{

// ============================================================================
// Firearm
// ============================================================================
Firearm::Firearm(FirearmSettings settings) : settings(settings)
{
    Reset();
}

void Firearm::Update(float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0)
    {
        return;
    }
    cooldown = (std::max)(0.f, cooldown - deltaSeconds);
    spread = (std::max)(settings.minimumSpread, spread - settings.spreadRecovery * deltaSeconds);
    if (reloadRemaining > 0)
    {
        reloadRemaining = (std::max)(0.f, reloadRemaining - deltaSeconds);
        if (reloadRemaining == 0)
        {
            rounds = settings.magazineSize;
        }
    }
}

bool Firearm::TryFire(float aimAngle, Shot &shot)
{
    if (!std::isfinite(aimAngle) || cooldown > 0 || IsReloading())
    {
        return false;
    }
    if (rounds == 0)
    {
        Reload();
        return false;
    }
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    float randomOffset = (float(randomState & 0xffffu) / 65535.f * 2 - 1) * spread;
    shot = {aimAngle + randomOffset, settings.bulletSpeed, settings.range, settings.damage};
    --rounds;
    cooldown = settings.fireInterval;
    spread = (std::min)(settings.maximumSpread, spread + settings.spreadPerShot);
    return true;
}

bool Firearm::Reload()
{
    if (IsReloading() || rounds == settings.magazineSize)
    {
        return false;
    }
    // Spare magazines are unlimited for the repeatable mine investigation.
    reloadRemaining = settings.reloadSeconds;
    return true;
}

void Firearm::Reset()
{
    rounds = settings.magazineSize;
    cooldown = reloadRemaining = 0;
    spread = settings.minimumSpread;
    randomState = 0x81b437u;
}
int Firearm::GetRounds() const
{
    return rounds;
}
int Firearm::GetMagazineSize() const
{
    return settings.magazineSize;
}
float Firearm::GetSpread() const
{
    return spread;
}
float Firearm::GetReloadProgress() const
{
    return IsReloading() ? 1 - reloadRemaining / settings.reloadSeconds : 0;
}
bool Firearm::IsReloading() const
{
    return reloadRemaining > 0;
}
// ============================================================================
// Pistol
// ============================================================================
Pistol::Pistol() : Firearm({12, 18, .2f, 1.4f, 38.f, 24.f, .008f, .16f, .033f, .09f})
{
}
const char *Pistol::GetName() const
{
    return "개척자 권총";
}
} // namespace combat
