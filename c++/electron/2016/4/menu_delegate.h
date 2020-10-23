// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_UI_VIEWS_MENU_DELEGATE_H_
#define ATOM_BROWSER_UI_VIEWS_MENU_DELEGATE_H_

#include "base/memory/scoped_ptr.h"
#include "ui/views/controls/menu/menu_delegate.h"

namespace views {
class MenuRunner;
}

namespace ui {
class MenuModel;
}

namespace atom {

class MenuBar;

class MenuDelegate : public views::MenuDelegate {
 public:
  explicit MenuDelegate(MenuBar* menu_bar);
  virtual ~MenuDelegate();

  void RunMenu(ui::MenuModel* model, views::MenuButton* button);

 protected:
  // views::MenuDelegate:
  void ExecuteCommand(int id) override;
  void ExecuteCommand(int id, int mouse_event_flags) override;
  bool IsTriggerableEvent(views::MenuItemView* source,
                          const ui::Event& e) override;
  bool GetAccelerator(int id, ui::Accelerator* accelerator) const override;
  base::string16 GetLabel(int id) const override;
  const gfx::FontList* GetLabelFontList(int id) const override;
  bool IsCommandEnabled(int id) const override;
  bool IsCommandVisible(int id) const override;
  bool IsItemChecked(int id) const override;
  void SelectionChanged(views::MenuItemView* menu) override;
  void WillShowMenu(views::MenuItemView* menu) override;
  void WillHideMenu(views::MenuItemView* menu) override;
  views::MenuItemView* GetSiblingMenu(
      views::MenuItemView* menu,
      const gfx::Point& screen_point,
      views::MenuAnchorPosition* anchor,
      bool* has_mnemonics,
      views::MenuButton** button) override;

 private:
  MenuBar* menu_bar_;
  int id_;
  scoped_ptr<views::MenuDelegate> adapter_;
  scoped_ptr<views::MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(MenuDelegate);
};

}  // namespace atom

#endif  // ATOM_BROWSER_UI_VIEWS_MENU_DELEGATE_H_
