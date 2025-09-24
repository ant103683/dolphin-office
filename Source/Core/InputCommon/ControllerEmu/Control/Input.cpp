// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerEmu/Control/Input.h"

#include <memory>
#include <string>
#include "InputCommon/ControlReference/ControlReference.h"

namespace ControllerEmu
{
Input::Input(Translatability translate, std::string name, std::string ui_name)
    : Control(translate, std::move(name), std::move(ui_name),
              std::make_unique<InputReference>())
{
}

Input::Input(Translatability translate, std::string name)
    : Control(translate, std::move(name), std::make_unique<InputReference>())
{
}
}  // namespace ControllerEmu
