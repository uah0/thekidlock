# AP configuration-mode project

This is a separate ESP32 project copy. The current working project is not
modified by this plan.

## GPIO0 mode switch

- Short press in `NORMAL_MODE`: enter `CONFIGURATION_MODE`.
- In configuration mode, start a temporary Wi-Fi access point and Web UI.
- Allow phone registration and configuration changes.
- Short press again: save configuration, close registration, stop Web UI/AP,
  and return to normal BLE lock operation.
- Persist all lock settings in NVS. The temporary configuration mode itself
  is not persisted.

## Configuration AP

- SSID: `lock`
- Password: `[пароль удалён]` (WPA2-PSK)
- Use the existing RGB LED for mode indication. The exact configuration-mode
  color will be assigned without changing the existing lock colors.

## Confirmed interaction

- GPIO0 short press switches to configuration mode.
- Configuration mode is indicated by the blue RGB LED.
- After every reboot or power loss, the device starts in normal mode.
- GPIO0 long press deletes all bonded phones.
- The buzzer is not used for mode indication.

## Normal mode

- BLE advertising and bonded-phone automatic opening remain active.
- Wi-Fi is disabled.
- No Web UI is required for normal operation.

## Confirmed configuration

- WPA2-PSK AP SSID: `lock`, password: `[пароль удалён]`.
- Configuration indication: blue RGB LED on GPIO48.
- Long press on GPIO0 (3 seconds): erase all bonds without switching mode.
- Configuration mode never survives reboot or power loss.
- Buzzer is not used for mode indication.
