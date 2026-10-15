#pragma once
#include "Taze/Decompiler.hpp"

namespace Taze
{
int DecompileFiles(const std::vector<std::string> &Inputs, const std::string &Directory, const Options &Settings, unsigned Workers,
                   bool Overwrite);
}
