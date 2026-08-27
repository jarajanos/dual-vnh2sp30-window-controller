# Window Controller — ESP32-C3 + Monster Moto Shield VNH2SP30

ESP-IDF firmware for controlling two motorized greenhouse windows. Each motor
supports one-button operation, MQTT commands, current monitoring, automatic
end-stop detection, and a movement timeout.

## Requirements

- ESP-IDF v6.x
- Target: `esp32c3`

## Build and flash

```bash
idf.py set-target esp32c3
idf.py menuconfig          # Optional; sdkconfig.defaults provides the defaults
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

Create your local configuration from the tracked template, then set at least
the Wi-Fi and MQTT credentials:

```bash
cp main/config.example.h main/config.h
```

Edit `main/config.h`:

```c
#define WIFI_SSID        "YourNetwork"
#define WIFI_PASSWORD    "YourPassword"
#define MQTT_BROKER_URI  "mqtt://192.168.1.100:1883"
```

The motor PWM duty, movement timeout, current detection, GPIO mapping, switch
debounce, and diagnostic interval are configured in the same file. The local
`main/config.h` is ignored by Git so credentials are not committed; update
`main/config.example.h` when adding shared configuration options.

## Window state machine

Each motor uses the following states:

- `unknown` — position is not known after startup
- `opening` — motor is opening the window
- `opened` — opening end stop was detected
- `closing` — motor is closing the window
- `closed` — closing end stop was detected
- `stopped` — movement was interrupted before reaching an end stop

A switch press performs an action based on the current state:

| Current state | Action |
|---|---|
| `unknown` or `closed` | Start opening |
| `opened` | Start closing |
| `opening` or `closing` | Stop |
| `stopped` | Move in the direction opposite to the previous movement |

Switch release does not trigger an action.

## Current monitoring

The VNH2SP30 current-sense output is sampled through the ESP32-C3 ADC. Four ADC
samples are averaged for each reading, and readings are taken every 50 ms.

The controller maintains an exponential moving average:

```text
new_average = (old_average * 7 + current_sample) / 8
```

Current monitoring starts after a 500 ms startup delay so the motor startup
surge does not trigger protection.

### Overcurrent detection

An overcurrent condition is detected when the measured current exceeds the
moving average by 10% for two consecutive readings. The motor is actively
braked and an MQTT alarm with reason `overcurrent` is published.

Relevant settings:

```c
#define CS_OVERCURRENT_MIN_RUN_MS  500
#define CS_OVERCURRENT_MIN_RAW     5
#define CS_OVERCURRENT_PERCENT     110
#define CS_OVERCURRENT_COUNT       2
#define CS_AVERAGE_HISTORY_WEIGHT  7
```

### End-stop detection

A mechanical end stop is recognized by a sharp current drop. When the measured
current falls to 45% or less of the moving average for three consecutive
readings, the motor is actively braked and its state changes to `opened` or
`closed`, depending on its direction.

Relevant settings:

```c
#define CS_ENDSTOP_MIN_RUN_MS    500
#define CS_ENDSTOP_MIN_RAW       5
#define CS_ENDSTOP_DROP_PERCENT  45
#define CS_ENDSTOP_COUNT         3
```

Both current thresholds should be tuned using measurements from the actual
motor and mechanism.

## Project structure

```text
window-esp-idf/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── config.example.h  # Tracked configuration template
    ├── config.h          # Local configuration ignored by Git
    ├── main.c            # app_main, tasks, and orchestration
    ├── motor.h / .c      # Motor control, state machine, and protection
    ├── switch.h / .c     # Active-low switch debounce
    ├── wifi.h / .c       # Wi-Fi station initialization
    └── mqtt.h / .c       # esp-mqtt wrapper with LWT support
```

## GPIO map

| GPIO | Function | Description |
|---:|---|---|
| 0 | CS2 | ADC1_CH0 — Motor 2 current sense |
| 1 | CS1 | ADC1_CH1 — Motor 1 current sense |
| 3 | PWM2 | LEDC channel 1 — Motor 2 |
| 4 | INA2 | Motor 2 direction |
| 5 | INA1 | Motor 1 direction |
| 6 | INB1 | Motor 1 direction |
| 7 | INB2 | Motor 2 direction |
| 10 | PWM1 | LEDC channel 0 — Motor 1 |
| 18 | SW1 | Motor 1 switch, active-low with pull-up |
| 19 | SW2 | Motor 2 switch, active-low with pull-up |

## MQTT

### Commands

Commands use QoS 1. Accepted payloads are `open`, `close`, and `stop`.

| Topic | Target |
|---|---|
| `home/greenhouse/window/motor1/cmd` | Motor 1 |
| `home/greenhouse/window/motor2/cmd` | Motor 2 |
| `home/greenhouse/window/all/cmd` | Both motors |

### State

State messages use QoS 1 and are retained. The payload is the state name as a
plain string, for example:

```text
Topic:   home/greenhouse/window/motor1/state
Payload: opening
```

### Alarm

Alarms are published for overcurrent and timeout stops:

```json
{
  "motor": "Motor1",
  "reason": "overcurrent",
  "cs_raw": 24,
  "cs_avg_raw": 20,
  "current_A": 7.29
}
```

### Diagnostics

Diagnostics are published every 10 seconds to
`home/greenhouse/window/diag`:

```json
{
  "online": true,
  "uptime_s": 3600,
  "motor1": "closed",
  "motor2": "opening",
  "cs1_raw": 0,
  "cs2_raw": 18,
  "cs1_avg": 20,
  "cs2_avg": 17,
  "i1_A": 0.00,
  "i2_A": 5.47
}
```

### Last will and testament

| Topic | Payload | Published when |
|---|---|---|
| `home/greenhouse/window/status` | `online` | The controller connects to the broker |
| `home/greenhouse/window/status` | `offline` | The broker detects an unexpected disconnect |

## Safety mechanisms

| Mechanism | Configuration | Behavior |
|---|---|---|
| Relative overcurrent | `CS_OVERCURRENT_*` | Active brake and alarm after repeated 10% excess |
| End-stop detection | `CS_ENDSTOP_*` | Active brake and final window state after a sharp current drop |
| Movement timeout | `MOTOR_TIMEOUT_MS=10000` | Active brake and alarm after 10 seconds |
| Active brake | INA=INB=LOW, PWM=0 | Stops the motor instead of coasting |
| Switch debounce | `SW_DEBOUNCE_MS=50` | Suppresses mechanical switch bounce |
| MQTT LWT | `TOPIC_LWT` | Reports an unexpected power or connection loss |

## Home Assistant example

```yaml
mqtt:
  button:
    - name: "Open window 1"
      command_topic: "home/greenhouse/window/motor1/cmd"
      payload_press: "open"
    - name: "Close window 1"
      command_topic: "home/greenhouse/window/motor1/cmd"
      payload_press: "close"

  sensor:
    - name: "Window 1 state"
      state_topic: "home/greenhouse/window/motor1/state"

  binary_sensor:
    - name: "Window controller"
      state_topic: "home/greenhouse/window/status"
      payload_on: "online"
      payload_off: "offline"
      device_class: connectivity
```
