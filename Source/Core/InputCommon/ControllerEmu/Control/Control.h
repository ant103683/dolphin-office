// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>

#include "InputCommon/ControlReference/ControlReference.h"

namespace ControllerEmu
{
enum class Translatability
{
  DoNotTranslate,
  Translate
};

class Control
{
public:
  virtual ~Control() = default;

  Control(Translatability translate, std::string name, std::string ui_name,
          std::unique_ptr<ControlReference> ref);
  Control(Translatability translate, std::string name, std::unique_ptr<ControlReference> ref);

  template <typename T = ControlState>
  T GetState() const
  {
    return control_ref->GetState<T>();
  }

  const std::string name;
  std::string ui_name;
  const Translatability translate;
  const std::unique_ptr<ControlReference> control_ref;
};
}  // namespace ControllerEmu
