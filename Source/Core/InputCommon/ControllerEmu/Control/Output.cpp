// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerEmu/Control/Output.h"

#include <memory>
#include <string>
#include "InputCommon/ControlReference/ControlReference.h"

namespace ControllerEmu
{
Output::Output(Translatability translate, std::string name)
    : Control(translate, std::move(name), std::make_unique<OutputReference>())
{
}
}  // namespace ControllerEmu
