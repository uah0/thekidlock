# Контракт служебного BLE GATT v1

Реклама: manufacturer marker LOCK, mode=0x83, существующие device_id/session_id. Команды и credentials не передаются в рекламе.
Запрос 001b (encrypted read): 7 байт: version=1, flags (Wi-Fi=1, Web IP=2, Hold=4), command (0=query mode, 1=hold on, 2=hold off), request_id uint32 big-endian. При отсутствии Hold command/id=0.
Ответ 001c (encrypted write): 7 байт: version=1, request_id uint32 big-endian, result (0=success, 1=denied, 2=queue failure, 3=fault/timeout), hold_open (0/1). ID защищает UI от устаревшего ответа, явная установка состояния обеспечивает идемпотентность.
Порядок: защищённый proof → проверка владельца → запрос → hold/result → Wi-Fi или Web IP. Wi-Fi требует переподключения для получения нового IP после DHCP; реклама остаётся единой.
AUTO-proof использует прежний путь и прежний RSSI-порог. Служебный путь не вызывает автоматическое открытие/продление таймера А. Алгоритм автомата/таймеры, OTA, Wi-Fi, Web UI, scanner, стандартное BLE bonding/encryption сохраняются.
