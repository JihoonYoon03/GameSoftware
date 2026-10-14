#pragma once
#include "ModelLibrary.h"

namespace models
{
// Fill owned storage directly; never return the large fixed-size models by value.
void GenerateVehicle(VehicleModel &result, VehicleType type);
void GenerateCharacter(Model &result, CharacterType type);
void GenerateBuilding(Model &result, BuildingType type);
bool ValidateModel(const Model &model);
} // namespace models
