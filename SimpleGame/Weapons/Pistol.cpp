#include "stdafx.h"
#include "Pistol.h"

namespace combat
{
Pistol::Pistol() : Firearm({12, 18, .2f, 1.4f, 38.f, 24.f, .008f, .16f, .033f, .09f})
{
}
const char *Pistol::GetName() const
{
    return "개척자 권총";
}
} // namespace combat
