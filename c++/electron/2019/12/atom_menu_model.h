// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_UI_ATOM_MENU_MODEL_H_
#define SHELL_BROWSER_UI_ATOM_MENU_MODEL_H_

#include <map>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "ui/base/models/simple_menu_model.h"

namespace electron {

class AtomMenuModel : public ui::SimpleMenuModel {
 public:
  class Delegate : public ui::SimpleMenuModel::Delegate {
   public:
    ~Delegate() override {}

    virtual bool GetAcceleratorForCommandIdWithParams(
        int command_id,
        bool use_default_accelerator,
        ui::Accelerator* accelerator) const = 0;

    virtual bool ShouldRegisterAcceleratorForCommandId(
        int command_id) const = 0;

    virtual bool ShouldCommandIdWorkWhenHidden(int command_id) const = 0;

   private:
    // ui::SimpleMenuModel::Delegate:
    bool GetAcceleratorForCommandId(
        int command_id,
        ui::Accelerator* accelerator) const override;
  };

  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override {}

    // Notifies the menu will open.
    virtual void OnMenuWillShow() {}

    // Notifies the menu has been closed.
    virtual void OnMenuWillClose() {}
  };

  explicit AtomMenuModel(Delegate* delegate);
  ~AtomMenuModel() override;

  void AddObserver(Observer* obs) { observers_.AddObserver(obs); }
  void RemoveObserver(Observer* obs) { observers_.RemoveObserver(obs); }

  void SetToolTip(int index, const base::string16& toolTip);
  base::string16 GetToolTipAt(int index);
  void SetRole(int index, const base::string16& role);
  base::string16 GetRoleAt(int index);
  void SetSublabel(int index, const base::string16& sublabel);
  base::string16 GetSublabelAt(int index) const override;
  bool GetAcceleratorAtWithParams(int index,
                                  bool use_default_accelerator,
                                  ui::Accelerator* accelerator) const;
  bool ShouldRegisterAcceleratorAt(int index) const;
  bool WorksWhenHiddenAt(int index) const;

  // ui::SimpleMenuModel:
  void MenuWillClose() override;
  void MenuWillShow() override;

  using SimpleMenuModel::GetSubmenuModelAt;
  AtomMenuModel* GetSubmenuModelAt(int index);

 private:
  Delegate* delegate_;  // weak ref.

  std::map<int, base::string16> toolTips_;   // command id -> tooltip
  std::map<int, base::string16> roles_;      // command id -> role
  std::map<int, base::string16> sublabels_;  // command id -> sublabel
  base::ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(AtomMenuModel);
};

}  // namespace electron

#endif  // SHELL_BROWSER_UI_ATOM_MENU_MODEL_H_
