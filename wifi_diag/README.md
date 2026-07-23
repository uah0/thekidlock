# ESP32-S3 Wi-Fi diagnostic

Минимальная отдельная прошивка ESP-IDF 5.4.1 для проверки SoftAP без BLE,
Web UI и компонентов основного проекта.

- SSID: `ESP-DIAG`
- Пароль: `123456798`
- WPA2-PSK
- Канал: 1

Сборка и прошивка:

```powershell
cd C:\Users\User\Downloads\ble_lock_stage1\AP\wifi_diag
idf.py set-target esp32s3
idf.py build
idf.py -p COM29 -b 115200 flash
```

После старта в UART должна появиться строка `READY`. Наличие SSID агент
проверяет независимо командой `netsh wlan show networks mode=bssid`.
