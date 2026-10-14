# Блок-схема работы ULP

Диаграмма описывает текущий алгоритм ULP-контроллера соленоида.

```mermaid
flowchart TD
    A[Пробуждение ULP] --> B[Прочитать command и command_sequence]
    B --> C{Новая команда принята?}
    C -- Нет --> D[Использовать active_command]
    C -- Да --> E[Записать command_ack_sequence]
    E --> D

    D --> F{Команда}
    F -- OPEN --> O{Замок LOCKED?}
    F -- CLOSE --> L{Замок LOCKED?}
    F -- Другая --> X[FAULT: STATE_INVALID]

    O -- Нет --> S[Выключить соленоид]
    O -- Да --> O1[Состояние OPEN_DISCHARGE]
    O1 --> O2[Ждать разряд катушки]
    O2 --> O3[Измерить остаточный ток]
    O3 --> O4{Остаточный ток <= допустимого?}
    O4 -- Нет --> OF[FAULT: COIL_SHORT]
    O4 -- Да --> O5[Состояние UNLOCKED]
    O5 --> O6[Событие UNLOCKED]
    O6 --> P[Диагностический импульс 2 мс]

    L -- Да --> L0[Оставить состояние LOCKED]
    L -- Нет --> L1[Выключить соленоид]
    L1 --> M[Измерить базу, VBAT и ток]
    M --> M1{ADC и VBAT исправны?}
    M1 -- Нет --> LF[FAULT: ADC/VBAT/SENSE]
    M1 -- Да --> P1[Включить соленоид на 2 мс]
    P1 --> P2[Измерить ток и выключить соленоид]
    P2 --> P3[Вычислить delta и normalized_feature]
    P3 --> P4[Определить plate_state]
    P4 --> P5{CONTACT или GAP_1?}
    P5 -- Нет --> LR[Событие CLOSE_REJECTED]
    P5 -- Да --> L2[Включить соленоид на 300 мс]
    L2 --> L3[Состояние CLOSE_VERIFY]
    L3 --> L4{Положение перед закрытием допустимо?}
    L4 -- Да --> L5[Состояние LOCKED]
    L5 --> L6[Событие LOCKED]
    L4 -- Нет --> LR
    LR --> L7[Выключить соленоид; состояние UNLOCKED]

    S --> Q[Измерить положение механизма]
    Q --> P

    OF --> Z[Выключить соленоид]
    LF --> Z
    X --> Z
    Z --> R[Записать fault_flags и event_flags]

    O6 --> W[Записать mailbox]
    L0 --> W
    L6 --> W
    L7 --> W
    R --> W
    Q --> W

    W --> W1[sequence нечётный: запись данных]
    W1 --> W2[Записать state, FSM, ADC, plate и faults]
    W2 --> W3[event_sequence при новом событии]
    W3 --> W4[sequence чётный: данные готовы]
    W4 --> T[Следующее пробуждение ULP]
    T --> A
```

## Основные данные mailbox

| Группа | Поля |
|---|---|
| Команда от CPU | `command`, `command_sequence` |
| Подтверждение команды | `command_ack_sequence` |
| Состояние | `lock_state`, `fsm_state`, `plate_state` |
| Измерения | `baseline_raw`, `vbat_raw`, `sense_raw`, `delta_raw`, `normalized_feature` |
| Ошибки | `fault_flags` |
| События | `event_flags`, `event_sequence` |
| Диагностика закрытия | `close_verify_state`, `close_verify_feature`, `close_verify_delta_raw` |
| Счётчики | `measurement_counter`, `close_attempt_counter`, `close_reject_counter` |

`sequence` используется как защита от чтения неполного набора данных: основной процессор принимает snapshot только при одинаковом чётном значении `sequence` до и после чтения.
