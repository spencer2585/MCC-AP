#pragma once

#include <unordered_map>
#include <cstdint>
#include "../Data/Skull.h"

struct SkullData
{
    std::unordered_map<Skull, uint64_t> bindings;
};
