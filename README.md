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

GPIO mapping and shared defaults are configured in the same file. The local
`main/config.h` is ignored by Git so credentials are not committed; update
`main/config.example.h` when adding shared configuration options.

Each motor has independent startup defaults (`MOTOR1_*` and `MOTOR2_*`) for PWM,
movement timeout, overcurrent detection, and end-stop detection. Saved values
in NVS take precedence over these defaults.

## Web configuration

After connecting to Wi-Fi, open the IP address printed in the serial log, for
example `http://192.168.1.123/`. The page edits and validates both motor
configurations independently and stores them in NVS, so they survive a restart
or firmware update. A motor which is already moving finishes that movement with
its previous configuration; new values apply when the motor starts again.

The JSON representation is available at `GET /api/config`. The web UI saves
through `POST /api/config` using URL-encoded fields. The server listens on
`WEB_CONFIG_PORT` (80 by default) and currently has no authentication, so it
should only be exposed on a trusted local network.

Accepted per-motor ranges:

| Field | Range |
|---|---:|
| `pwm_duty` | 1–255 |
| `timeout_ms` | 100–3,600,000 ms |
| `overcurrent_min_run_ms` | 0–`timeout_ms` |
| `overcurrent_min_raw` | 0–4095 |
| `overcurrent_percent` | 101–1000% |
| `overcurrent_count` | 1–255 samples |
| `endstop_min_run_ms` | 0–`timeout_ms` |
| `endstop_min_raw` | 0–4095 |
| `endstop_drop_percent` | 1–99% |
| `endstop_count` | 1–255 samples |

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

Relevant per-motor settings (shown for motor 1):

```c
#define MOTOR1_OVERCURRENT_MIN_RUN_MS  500
#define MOTOR1_OVERCURRENT_MIN_RAW     5
#define MOTOR1_OVERCURRENT_PERCENT     110
#define MOTOR1_OVERCURRENT_COUNT       2
#define CS_AVERAGE_HISTORY_WEIGHT  7
```

### End-stop detection

A mechanical end stop is recognized by a sharp current drop. When the measured
current falls to 45% or less of the moving average for three consecutive
readings, the motor is actively braked and its state changes to `opened` or
`closed`, depending on its direction.

Relevant per-motor settings (shown for motor 1):

```c
#define MOTOR1_ENDSTOP_MIN_RUN_MS    500
#define MOTOR1_ENDSTOP_MIN_RAW       5
#define MOTOR1_ENDSTOP_DROP_PERCENT  45
#define MOTOR1_ENDSTOP_COUNT         3
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
    ├── mqtt.h / .c       # esp-mqtt wrapper with LWT support
    ├── settings.h / .c   # Per-motor NVS persistence
    └── web_config.h / .c # HTTP configuration UI and API
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
| Relative overcurrent | `MOTOR1/2_OVERCURRENT_*` or web UI | Active brake and alarm after a configured excess |
| End-stop detection | `MOTOR1/2_ENDSTOP_*` or web UI | Active brake and final window state after a configured drop |
| Movement timeout | `MOTOR1/2_TIMEOUT_MS` or web UI | Active brake and alarm after the configured time |
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
