#pragma once
#include "Firearm.h"

namespace combat
{
class Pistol final : public Firearm
{
  public:
    Pistol();
    const char *GetName() const override;
};
} // namespace combat
