// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_TOOL_MANAGER_H_
#define ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_TOOL_MANAGER_H_

#include <map>
#include <memory>
#include <vector>

#include "ash/ash_export.h"
#include "ash/common/system/chromeos/palette/palette_ids.h"
#include "ash/common/system/chromeos/palette/palette_tool.h"
#include "base/callback.h"
#include "base/macros.h"

namespace views {
class View;
}

namespace ash {

class PaletteTool;
enum class PaletteGroup;
enum class PaletteToolId;
class WmWindow;

struct ASH_EXPORT PaletteToolView {
  PaletteGroup group;
  PaletteToolId tool_id;
  views::View* view;
};

class ASH_EXPORT PaletteToolManager : public PaletteTool::Delegate {
 public:
  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    // Hide the palette (if shown).
    virtual void HidePalette() = 0;

    // Called when the active tool has changed.
    virtual void OnActiveToolChanged() = 0;

    // Return the window associated with this palette.
    virtual WmWindow* GetWindow() = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // Creates the tool manager.
  PaletteToolManager(Delegate* delegate);
  ~PaletteToolManager() override;

  // Adds the given |tool| to the tool manager. The tool is assumed to be in a
  // deactivated state. This class takes ownership over |tool|.
  void AddTool(std::unique_ptr<PaletteTool> tool);

  // Activates tool_id and deactivates any other active tool in the same
  // group as tool_id.
  void ActivateTool(PaletteToolId tool_id);

  // Deactivates the given tool.
  void DeactivateTool(PaletteToolId tool_id);

  // Optional methods that are not likely to be needed, but will be
  // implemented if necessary.
  bool IsToolActive(PaletteToolId tool_id);
  PaletteToolId GetActiveTool(PaletteGroup group);

  // Fetch the active tray icon for the given tool. Returns
  // gfx::VectorIconId::VECTOR_ICON_NONE if not available.
  gfx::VectorIconId GetActiveTrayIcon(PaletteToolId tool_id);

  // Create views for all of the registered tools.
  std::vector<PaletteToolView> CreateViews();

  // Called when the views returned by CreateViews have been destroyed. This
  // should clear any (now) stale references.
  void NotifyViewsDestroyed();

 private:
  // PaleteTool::Delegate overrides.
  void EnableTool(PaletteToolId tool_id) override;
  void DisableTool(PaletteToolId tool_id) override;
  void HidePalette() override;
  WmWindow* GetWindow() override;

  PaletteTool* FindToolById(PaletteToolId tool_id);

  // Unowned pointer to the delegate to provide external functionality.
  Delegate* delegate_;

  // Unowned pointer to the active tool / group.
  std::map<PaletteGroup, PaletteTool*> active_tools_;

  // Owned list of all tools.
  std::vector<std::unique_ptr<PaletteTool>> tools_;

  DISALLOW_COPY_AND_ASSIGN(PaletteToolManager);
};

}  // namespace ash

#endif  // ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_TOOL_MANAGER_H_
