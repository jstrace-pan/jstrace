// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BLUETOOTH_BLUETOOTH_CHOOSER_DESKTOP_H_
#define CHROME_BROWSER_UI_BLUETOOTH_BLUETOOTH_CHOOSER_DESKTOP_H_

#include "base/macros.h"
#include "content/public/browser/bluetooth_chooser.h"

class BluetoothChooserController;

// Represents a Bluetooth chooser to ask the user to select a Bluetooth
// device from a list of options. This implementation is for desktop.
// BluetoothChooserAndroid implements the mobile part.
class BluetoothChooserDesktop : public content::BluetoothChooser {
 public:
  explicit BluetoothChooserDesktop(
      BluetoothChooserController* bluetooth_chooser_controller);
  ~BluetoothChooserDesktop() override;

  // BluetoothChooser:
  void SetAdapterPresence(AdapterPresence presence) override;
  void ShowDiscoveryState(DiscoveryState state) override;
  void AddDevice(const std::string& device_id,
                 const base::string16& device_name) override;
  void RemoveDevice(const std::string& device_id) override;

 private:
  // Weak. ChooserContentView[Cocoa] owns it.
  BluetoothChooserController* bluetooth_chooser_controller_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothChooserDesktop);
};

#endif  // CHROME_BROWSER_UI_BLUETOOTH_BLUETOOTH_CHOOSER_DESKTOP_H_
