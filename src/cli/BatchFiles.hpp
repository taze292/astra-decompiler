#pragma once
#include "Astra/Decompiler.hpp"

namespace Astra
{
int DecompileFiles(const std::vector<std::string> &Inputs, const std::string &Directory, const Options &Settings, unsigned Workers,
                   bool Overwrite);
}
