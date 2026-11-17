# ComfoAir Q ESPHome bridge

Control and monitor a **Zehnder ComfoAir Q** ventilation unit (Q350 / Q450 / Q600) from
**Home Assistant**, using an ESP32 and a CAN transceiver connected to the unit's RJ45 port.
No ComfoConnect gateway, no cloud.

![Home Assistant dashboard](docs/homeassistant.png?raw=true "ComfoAir in Home Assistant")

**What you get in Home Assistant**

- 🌀 a **Ventilation** fan: off (away), speed 1–3, or *Auto*
- 🚀 a **Boost** selector: boost for 10 min … 8 h, then back to normal by itself
- 🌡️ a **Bypass** selector: auto, open or closed, for a while or permanently
- 📈 ~50 sensors: temperatures, humidity, air flow, power and energy (Energy dashboard ready),
  filter days remaining, and more
- 🛠️ services for automations and anything that has no entity

Questions? Join the [Discord](https://discord.gg/dUSSRezJXC) or open an issue.

---

**Contents:**
[Quick start](#quick-start) ·
[Hardware](#hardware) ·
[Configuration](#configuration) ·
[Entities](#entities) ·
[Automations & services](#automations--services) ·
[Troubleshooting](#troubleshooting) ·
[Upgrading](#upgrading) ·
[Credits](#credits)

---

## Quick start

1. **Build the hardware**: an ESP32 plus a CAN transceiver, powered from the unit's
   12 V ([details below](#hardware)).
2. **Create an ESPHome device** with this configuration (fill in your own Wi-Fi and API key):

   ```yaml
   esphome:
     name: comfoair

   esp32:
     board: esp32dev
     framework:
       type: arduino

   external_components:
     - source: github://tarbax/comfoair-esp32
       components: [comfoair]

   logger:

   api:
     encryption:
       key: !secret api_key
     custom_services: true      # required, the component registers its own services

   ota:
     - platform: esphome

   wifi:
     ssid: !secret wifi_ssid
     password: !secret wifi_password

   comfoair:
     - id: comfo
       name: "ComfoAir"        # name of the climate entity
       rx_pin: 21               # ESP32 pin connected to the transceiver's CAN RX
       tx_pin: 25               # ESP32 pin connected to the transceiver's CAN TX
   ```

3. **Flash it**, plug it into the ComfoAir and add the device in Home Assistant
   (*Settings → Devices & services*). All entities appear by themselves. Values arrive
   within about a minute after boot.

That's all. Everything below is optional.
A slightly larger example lives in [docs/esphome_sample.yaml](docs/esphome_sample.yaml).

## Hardware

### Option A: custom PCB

The [docs/pcb](docs/pcb) folder contains Gerber files and a BOM for a small board that plugs
straight into the unit. The files are provided as is; check the schematic yourself before ordering.

![PCB](docs/pic.jpg?raw=true "Soldered PCB")

### Option B: off-the-shelf modules

| Part | Example |
|---|---|
| ESP32 dev board | [link](https://amzn.to/3pe0XVP) |
| 12 V → 5 V DC-DC converter (e.g. LM2596S) | [link](https://amzn.to/39ar22v) |
| CAN transceiver, 3.3 V (SN65HVD230) | [Waveshare SN65HVD230](https://www.banggood.com/Waveshare-SN65HVD230-CAN-Bus-Module-Communication-CAN-Bus-Transceiver-Development-Board-p-1693712.html) |
| RJ45 female socket + a piece of Ethernet cable | [link](https://amzn.to/3sNx3tH) |

Wiring (by @mat3u). Colours are the wires of a standard Ethernet cable on the ComfoAir side:

```
 ComfoAir RJ45                  DC-DC converter               ESP32
-----------------+        +-------------------+         +-------------+
(orange/red) 12V o--------o IN+          OUT+ o---------o VIN (5 V)   |
(brown)      GND o--------o IN-          OUT- o---------o GND         |
                 |        +-------------------+         |             |
                 |                                      |             |
                 |        +-------------------------+   |             |
(white/blue)   L o--------o CAN_L               3V3 o---o 3V3         |
(blue)         H o--------o CAN_H               GND o---o GND         |
-----------------+        |                         |   |             |
                          |   SN65HVD230     CAN TX o---o GPIO 25     |
                          |                  CAN RX o---o GPIO 21     |
                          +-------------------------+   +-------------+
```

> ⚠️ Set the DC-DC converter to 5 V **before** connecting the ESP32.
> Other GPIOs work too; set them with `rx_pin` / `tx_pin`.

### Option C: OLIMEX ESP32-EVB

This board has the CAN transceiver built in and wired Ethernet (with automatic Wi-Fi
failover), so you only need a 12 V → 5 V converter. It needs different pins (`tx_pin: 5`, `rx_pin: 35`).
See **[docs/olimex-esp32-evb.md](docs/olimex-esp32-evb.md)** for wiring and a ready-made config.

## Configuration

Everything except `id` and `name` is optional. The defaults are:

```yaml
comfoair:
  - id: comfo
    name: "ComfoAir"            # climate entity
    rx_pin: 21
    tx_pin: 25

    fan:                        # Ventilation fan entity
      name: "Ventilation"

    boost:                      # Boost select
      name: "Boost"
      durations: [10min, 20min, 30min, 45min, 1h, 2h, 3h, 4h, 6h, 8h]

    bypass:                     # Bypass select
      name: "Bypass"
      durations: [1h, 2h, 4h, 8h, 12h, 24h]

    # global_filters:           # applied to every sensor, see below
    #   - throttle: 10s
```

Durations can be any whole number of minutes (`15min`, `90min`, `5h`, ...).

### Renaming or hiding entities

Every entity in the [Entities](#entities) tables can be configured with its key,
just like a normal ESPHome sensor:

```yaml
comfoair:
  - id: comfo
    name: "ComfoAir"
    fan:
      name: "Ventilatie"         # rename
    supply_fan_speed:
      internal: true             # don't send to Home Assistant
    outdoor_air_temp:
      name: "Outside temperature"
      filters:
        - sliding_window_moving_average:
            window_size: 5
```

### Fewer updates: `global_filters`

The unit sends new values all the time. To reduce the traffic to Home Assistant, add filters
to all sensors at once. They take the same filters as an ESPHome `sensor`:

```yaml
comfoair:
  - id: comfo
    name: "ComfoAir"
    global_filters:
      - throttle: 10s
```

> `global_filters` takes precedence over filters set on an individual sensor.

### More examples

| File | What it shows |
|---|---|
| [docs/esphome_sample.yaml](docs/esphome_sample.yaml) | Minimal working config with comments |
| [docs/esphome_olimex_esp32_evb.yaml](docs/esphome_olimex_esp32_evb.yaml) | OLIMEX ESP32-EVB, Ethernet with Wi-Fi failover ([guide](docs/olimex-esp32-evb.md)) |
| [docs/esphome_sample_with_climate.yaml](docs/esphome_sample_with_climate.yaml) | Q450 on an ESP32-EVB over Wi-Fi, with extra template selects |
| [docs/esphome_complete_reference.yaml](docs/esphome_complete_reference.yaml) | Every option and sensor (comments in German) |

## Entities

### Controls

| Entity | Type | What it does |
|---|---|---|
| **Ventilation** | fan | **Off** = away (level 0, minimal airflow) · **speed 1/2/3** = level 1/2/3 · **turn on** = last level · preset **Auto** = back to the unit's own schedule (shown while the unit is in auto mode) |
| **Boost** | select | `Off` or a duration. Falls back to `Off` when the time is up. The unit does not report a running boost, so this is tracked by the ESP |
| **Bypass** | select | `Auto` · `Open <duration>` · `Open (permanent)` · `Closed <duration>` · `Closed (permanent)`. Follows the unit, so it returns to `Auto` by itself when a timed bypass ends |
| **ComfoAir** (the `name:`) | climate | Temperature profile (heat = warm, cool = cool, auto = normal), fan modes, indoor temperature and humidity. For the fan level the *Ventilation* fan is easier to use |

### Sensors

All sensors have the right unit, device class and icon. Energy sensors work in the Energy
dashboard. Technical values are in the *Diagnostic* section of the device page, and a few rarely
used ones are created **disabled**: enable them in Home Assistant if you need them.

<details>
<summary><b>Sensor list</b> (key · default name · unit · PDO)</summary>

| Key | Name | Unit | PDO | Notes |
|---|---|---|---|---|
| `fan_speed` | Fan speed | | 65 | 0–3 |
| `next_fan_change` | Next fan change | s | 81 | diagnostic |
| `next_bypass_change` | Next bypass change | s | 82 | diagnostic |
| `next_supply_change` | Next supply change | s | 86 | diagnostic, disabled |
| `next_exhaust_change` | Next exhaust change | s | 87 | diagnostic, disabled |
| `exhaust_fan_duty` | Exhaust fan duty | % | 117 | diagnostic |
| `supply_fan_duty` | Supply fan duty | % | 118 | diagnostic |
| `exhaust_fan_flow` | Exhaust fan flow | m³/h | 119 | |
| `supply_fan_flow` | Supply fan flow | m³/h | 120 | |
| `exhaust_fan_speed` | Exhaust fan speed | rpm | 121 | diagnostic |
| `supply_fan_speed` | Supply fan speed | rpm | 122 | diagnostic |
| `power_consumption_current` | Power consumption current | W | 128 | |
| `energy_consumption_ytd` | Energy consumption YTD | kWh | 129 | |
| `energy_consumption_since_start` | Energy consumption since start | kWh | 130 | |
| `days_remaining_filter` | Days remaining filter | d | 192 | |
| `rmot` | RMOT | °C | 209 | running mean outdoor temperature |
| `target_temp` | Target temp | °C | 212 | |
| `avoided_heating_actual` | Avoided heating actual | W | 213 | |
| `avoided_heating_ytd` | Avoided heating YTD | kWh | 214 | |
| `avoided_heating_total` | Avoided heating total | kWh | 215 | |
| `avoided_cooling_actual` | Avoided cooling actual | W | 216 | |
| `avoided_cooling_ytd` | Avoided cooling YTD | kWh | 217 | |
| `avoided_cooling_total` | Avoided cooling total | kWh | 218 | |
| `pre_heater_temp_before` | Pre heater temp before | °C | 220 | |
| `post_heater_temp_before` | Post heater temp before | °C | 221 | = supply air temperature in the ComfoConnect app |
| `bypass_state` | Bypass state | % | 227 | 0 = closed, 100 = fully open |
| `extract_air_temp` | Extract air temp | °C | 274 | air coming *from* the rooms |
| `exhaust_air_temp` | Exhaust air temp | °C | 275 | air going *outside* |
| `outdoor_air_temp` | Outdoor air temp | °C | 276 | |
| `pre_heater_temp_after` | Pre heater temp after | °C | 277 | disabled |
| `post_heater_temp_after` | Post heater temp after | °C | 278 | disabled |
| `extract_air_humidity` | Extract air humidity | % | 290 | |
| `exhaust_air_humidity` | Exhaust air humidity | % | 291 | |
| `outdoor_air_humidity` | Outdoor air humidity | % | 292 | |
| `pre_heater_hum_after` | Pre heater hum after | % | 293 | disabled |
| `supply_air_humidity` | Supply air humidity | % | 294 | |
| `analog_input_0_10v_1` … `_4` | Analog input 0 10V 1 … 4 | V | 369–372 | diagnostic, disabled |

**Text sensors**

| Key | Name | PDO | Values |
|---|---|---|---|
| `operating_mode` | Operating mode | 49 | `auto`, `limited_manual`, `unlimited_manual` |
| `bypass_activation_mode` | Bypass activation mode | 66 | `auto`, `activated`, `deactivated` |
| `temp_profile` | Temp profile | 67 | `auto`, `cold`, `warm` |

**Binary sensors**

| Key | Name | PDO |
|---|---|---|
| `away` | Away | 16 |
| `manual_mode` | Manual mode | 56 (diagnostic) |
| `fan_supply_only` | Fan supply only | 70 |
| `fan_exhaust_only` | Fan exhaust only | 71 |
| `heating_season` | Heating season | 210 |
| `cooling_season` | Cooling season | 211 |

</details>

## Automations & services

For most automations the entities are enough. For example: boost for 30 minutes when the
bathroom humidity gets high:

```yaml
automation:
  - alias: "Shower boost"
    trigger:
      - platform: numeric_state
        entity_id: sensor.bathroom_humidity
        above: 75
    action:
      - service: select.select_option
        target:
          entity_id: select.comfoair_boost
        data:
          option: "30 min"
```

The component also registers services. In Home Assistant they are named
`esphome.<device name>_<service>`, e.g. `esphome.comfoair_boost` for a device called `comfoair`.

| Service | Parameters | What it does |
|---|---|---|
| `boost` | `minutes` (0 = stop) | Boost for any number of minutes |
| `bypass` | `mode`: `auto` / `open` / `closed`, `minutes` (0 = permanent) | Set the bypass |
| `send_command` | `command` | Send one of the [commands below](#commands) |
| `send_hex` | `hexSequence` | Send a raw command (advanced) |
| `update_all` | none | Ask the unit for all values again |
| `req_update_service` | `PDOID` | Ask the unit for one value |
| `enable_can_trace` / `disable_can_trace` | none | Log all CAN frames (stops by itself after 10 min) |
| `set_can_trace_pdo_filter` / `clear_can_trace_pdo_filter` | `pdo` | Trace only one PDO |

```yaml
service: esphome.comfoair_boost
data:
  minutes: 90
---
service: esphome.comfoair_bypass
data:
  mode: open       # auto | open | closed
  minutes: 300     # 0 = until changed
---
service: esphome.comfoair_send_command
data:
  command: ventilation_supply_only
```

### Commands

Usable with the `send_command` service, or `id(comfo).send_command("...")` in ESPHome lambdas.

| Command | Effect |
|---|---|
| `ventilation_level_0` … `ventilation_level_3` | Set ventilation level (0 = away) |
| `auto` / `manual` | Automatic schedule / manual mode |
| `boost_<N>_min`, `boost_<N>h` | Boost, e.g. `boost_20_min`, `boost_3h` |
| `boost_end` | Stop boost |
| `bypass_activate_<N>h`, `bypass_activate_<N>_min`, `bypass_activate_permanent` | Open bypass |
| `bypass_deactivate_<N>h`, `bypass_deactivate_<N>_min`, `bypass_deactivate_permanent` | Close bypass |
| `bypass_auto` | Bypass back to automatic |
| `ventilation_supply_only` / `ventilation_supply_only_reset` | Supply fan only (1 h) / undo |
| `ventilation_extract_only` / `ventilation_extract_only_reset` | Extract fan only (1 h) / undo |
| `temp_profile_normal` / `temp_profile_cool` / `temp_profile_warm` | Temperature profile |

Commands are sent twice (once right away, once 1 s later) for reliability.

**ESPHome lambdas:** `id(comfo).set_ventilation_level(0..3)`, `id(comfo).boost(seconds)`,
`id(comfo).boost_end()`, `id(comfo).send_command("bypass_activate_4h")`.

**Raw commands:** `send_hex` sends any frame, for example
`8415070100000000100e000000` is the same as `ventilation_extract_only`. See the protocol
documentation linked under [Credits](#credits).

## Troubleshooting

| Problem | What to check |
|---|---|
| **No values at all** | Swap CAN_H / CAN_L. Check the RX/TX pins against your wiring. Look at the ESPHome log, or run `enable_can_trace` to see whether any CAN frames arrive. |
| **Config error about services / `custom_services`** | Add `custom_services: true` under `api:` (needed since ESPHome 2026.3). |
| **Fan speed shows 0 right after a reboot** | The unit only sends the real value when it changes. The component ignores this boot-time 0 and re-polls everything 30 s after boot. |
| **Boost select shows `Off` while the unit still boosts** | The boost was started elsewhere (panel, app). The unit doesn't report boosts, so the select can't know. |
| **Too many updates in the HA history** | Use [`global_filters`](#fewer-updates-global_filters). |
| **Build error `can_common.h: No such file or directory`** | Update to the latest version of this component; it pins a CAN library that declares this dependency. Clear the build files in ESPHome (*Clean build files*) and build again. |

## Upgrading

From versions before the fan/boost/bypass entities:

- **Entity IDs stay the same.** Only the friendly names change (`fan_speed` → `Fan speed`).
  Names you set yourself are kept.
- **New entities:** *Ventilation* (fan), *Boost* and *Bypass* (select). If you made these yourself
  with templates you can remove those, or hide the new ones with `internal: true`.
- `days_remaining_filter` now uses unit `d` (the Home Assistant standard) instead of `days`.
- Some diagnostic sensors are now disabled by default. Enable them in Home Assistant if you
  use them.
- Add `custom_services: true` under `api:` if you don't have it yet.

## Credits

Originally created by [vekexasia](https://github.com/vekexasia/comfoair-esp32).
The protocol was reverse engineered [here](https://github.com/marco-hoyer/zcan/issues/1).
More about how the unit communicates:

- [RMI protocol](https://github.com/michaelarnauts/aiocomfoconnect/blob/master/docs/PROTOCOL-RMI.md)
- [PDO protocol](https://github.com/michaelarnauts/aiocomfoconnect/blob/master/docs/PROTOCOL-PDO.md)
