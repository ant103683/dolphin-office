// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerEmu/Control/Control.h"

#include <utility>
#include "InputCommon/ControlReference/ControlReference.h"

namespace ControllerEmu
{
Control::Control(Translatability translate, std::string name, std::string ui_name,
                 std::unique_ptr<ControlReference> ref)
    : name(std::move(name)), ui_name(std::move(ui_name)), translate(translate),
      control_ref(std::move(ref))
{
}

Control::Control(Translatability translate, std::string name, std::unique_ptr<ControlReference> ref)
    : Control(translate, std::move(name), name, std::move(ref))
{
}

}  // namespace ControllerEmu
