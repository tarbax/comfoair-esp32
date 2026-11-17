Need help? Join the [Discord](https://discord.gg/dUSSRezJXC) or open an issue.

# Comfoair Q 350 ESPHome bridge

This software let you use a ESP32 + CAN Transceiver to interact with the Comfoair Q 350 unit.

It does expose all known informations of your Comfoair unit via ESPHOME and let you control the air flow via Home Assistant as well.

It does allow you to integrate the unit on Home Assistant as depicted below:
![Comfoair Q 350 Home Assistant](docs/homeassistant.png?raw=true "Comfoair Q 350 Home Assistant")

## Custom PCB

Inside the [docs/pcb folder](docs/pcb) you can find both gerber file and BOM. Files are provided as is. It's up to you to check data and schematic correctness.

This repository is meant to be working with the custom pcb above. But you can always buy components separately such as [Waveshare SN65HVD230](https://www.banggood.com/Waveshare-SN65HVD230-CAN-Bus-Module-Communication-CAN-Bus-Transceiver-Development-Board-p-1693712.html?rmmds=myorder&cur_warehouse=CN) and a voltage regulator as well as an ESP32 of your choice and it will work.

The pcb will look like this once soldered and mounted.

![PCB](docs/pic.jpg?raw=true "Comfoair Q 350 3D Print")

## DIY

Prerequisites for custom build:

* `ESP32` -> [link](https://amzn.to/3pe0XVP)
* `DC-DC converter` -> [link](https://amzn.to/39ar22v)
* `RJ45 Female` -> [link](https://amzn.to/3sNx3tH)
* `Can Transceiver` -> [Waveshare SN65HVD230](https://www.banggood.com/Waveshare-SN65HVD230-CAN-Bus-Module-Communication-CAN-Bus-Transceiver-Development-Board-p-1693712.html?rmmds=myorder&cur_warehouse=CN)
+ Some ethernet cable


Here a simple schematic. made by @mat3u

```
---------------+        +---------------+                 +-------------+
(oran/red)12V  o--------o IN+      OUT+ o-----------------o VIN         |
               |        |   [LM2596S]   |                 |             |
(brown)   GND  o--------o IN-      OUT- o-----------------o GND         |
               |        +---------------+                 |             |
    [RJ45]     |                                          |   [ESP32]   |
  [ComfoAir]   |                                          |             |
               |        +-------------------------+       |             |
(w/blue) CAN_L o--------o CAN_L               3v3 o-------o 3v3         |
(blue)   CAN_H o--------o CAN_H               GND o-------o GND         |
---------------+        |                         |       |             |
                        |   [SN65HVD230]   CAN TX o-------o 25          |
                        |                  CAN RX o-------o 21          |
                        +-------------------------+       +-------------+
``````

## esphome conf

To include this repo use the external_components configuration like so:

```yaml
# ...
external_components:
  - source: github://tarbax/comfoair-esp32
    components: [ comfoair ]
api:
  custom_services: true # <--- This is needed

comfoair:
  - id: comfo
    name: "ComfoAir Q350"

# ...
```

See [docs/esphome_sample.yaml](docs/esphome_sample.yaml) for a minimal working config.

# Entities

Out of the box the component creates:

| Entity | Type | Notes |
|---|---|---|
| `Ventilation` | fan | Off = away (level 0), speed 1–3 = level 1–3, preset `Auto` hands control back to the unit's schedule |
| `Boost` | select | `Off` + one option per configured duration |
| `Bypass` | select | `Auto`, `Open <duration>`, `Open`, `Closed <duration>`, `Closed` |
| climate (`name:` of the hub) | climate | temperature profile, fan modes, current temperature/humidity |
| all known PDO values | sensor / text_sensor / binary_sensor | with unit, device class, state class and icon |

All sensors come with a readable name, the correct Home Assistant `device_class` / `state_class`
(so energy sensors work in the Energy dashboard and temperatures/humidity show up properly),
an icon, and technical values (fan duty/RPM, timers, analog inputs, …) are put in the
*Diagnostic* category. Rarely useful values (analog inputs, pre/post heater "after" values,
supply/exhaust timers) are created but **disabled by default** in Home Assistant.

The default names slugify to the YAML key (e.g. `Supply fan flow` → `supply_fan_flow`), so
existing Home Assistant entity IDs are kept when you upgrade. Everything can be overridden per
entity as usual:

```yaml
comfoair:
  - id: comfo
    name: "ComfoAir Q350"
    supply_fan_speed:
      name: "Supply fan RPM"
      internal: true          # hide from Home Assistant
    fan:
      name: "Ventilatie"
```

## Fan entity

The fan entity is the recommended way to control the ventilation level from Home Assistant:

- **off** → level 0 (away; the unit keeps a minimal air flow)
- **speed 1/2/3** (33/66/100 %) → ventilation level 1/2/3
- **turn on** without speed → last used level
- **preset `Auto`** → back to automatic mode; the preset is shown while the unit reports
  operating mode `auto` (PDO 49)

## Boost and bypass durations

Both selects take a free list of durations (minutes or hours):

```yaml
comfoair:
  - id: comfo
    name: "ComfoAir Q350"
    boost:
      name: "Boost"
      durations: [10min, 20min, 30min, 45min, 1h, 2h, 3h, 4h, 6h, 8h]   # default
    bypass:
      name: "Bypass"
      durations: [1h, 2h, 4h, 8h, 12h, 24h]                            # default
```

- Boost: the unit does not report whether a boost is running, so the select is optimistic and
  falls back to `Off` when the chosen time has elapsed (or after `boost_end`).
- Bypass: follows the unit's bypass mode (PDO 66), so it returns to `Auto` by itself once a
  timed bypass ends. `Open` / `Closed` without a duration stay active until changed.


# Home Assistant Services

The component exposes these services:

- comfoair_send_command
- comfoair_send_hex
- comfoair_update_all
- comfoair_req_update_service
- comfoair_boost (`minutes`, 0 = end boost)
- comfoair_bypass (`mode`: `auto` / `open` / `closed`, `minutes`: 0 = until changed)
- comfoair_enable_can_trace / comfoair_disable_can_trace
- comfoair_set_can_trace_pdo_filter / comfoair_clear_can_trace_pdo_filter

**NOTE**: the prefix could differ depending on your esphome device name.

**comfoair_send_command**: allows one "command" parameter. The value of the param can be one of the following pre-bundled commands:

- ventilation_level_0
- ventilation_level_1
- ventilation_level_2
- ventilation_level_3
- boost_10_min, boost_20_min, boost_30_min, boost_60_min, … — **any** duration: `boost_<N>_min` or `boost_<N>h` (e.g. `boost_45_min`, `boost_3h`)
- boost_end
- auto
- manual
- bypass_activate_1h, bypass_deactivate_1h, … — **any** duration: `bypass_activate_<N>h`, `bypass_activate_<N>_min`, `bypass_activate_permanent` (same for `bypass_deactivate_…`)
- bypass_auto
- ventilation_supply_only
- ventilation_supply_only_reset
- ventilation_extract_only
- ventilation_extract_only_reset
- temp_profile_normal
- temp_profile_cool
- temp_profile_warm

In case you need more flexibility you can use the `comfoair_send_hex` service like follows:
```
service: esphome.comfoair_send_hex
data:
  hexSequence: "8415070100000000100e000000"
```
^^ the above is equivalent to ventilation_extract_only (which has an inner timer of 1h)

**comfoair_update_all**: this service will request an update of all the values from the Comfoair unit. This is useful in case you want to force an update of all the values.

**comfoair_req_update_service**: this service will request an update of a specific value from the Comfoair unit. This is useful in case you want to force an update of a specific value. You're required to pass the `PDOID` of the service to update

**comfoair_boost** / **comfoair_bypass**: start a boost or set the bypass for an arbitrary number of minutes:
```yaml
service: esphome.comfoair_boost
data:
  minutes: 90
---
service: esphome.comfoair_bypass
data:
  mode: open      # auto | open | closed
  minutes: 300    # 0 = until changed
```

From ESPHome lambdas you can also call `id(comfo).boost(seconds)`, `id(comfo).boost_end()`,
`id(comfo).set_ventilation_level(0..3)` and `id(comfo).send_command("bypass_activate_4h")`.

Commands are sent once immediately and repeated once after 1 s, without blocking the main loop.

## Climate Entity

The component will expose a climate entity with heat,cool,auto modes and all the fan speeds.
(For controlling the ventilation level the dedicated fan entity is usually nicer.)

Furthermore current indoor humidity and temperature as well as current target temperature will be shon as in the picture below.

![climate.png](docs/climate.png)


The climate entity can also be used to set the fan speed, and the mode (heat, cool, auto).

## Global filters

Since this component posts data as soon as it receives it, sometimes it might be useful to avoid spamming home assistant with updates.

To avoid this, you can set the `global_filters`. This accepts the same filters as the `filters` parameter in the `sensor` component.

For example, let's say you want to throttle the sensors for 10s.

```yaml

comfoair:
  global_filters:
    - throttle: 10s
```

Obviously, you can always set the filter on a per-sensor basis.

```yaml
comfoair:
  supply_fan_speed:
    name: supply_fan_speed
    disabled_by_default: false
    force_update: false
    unit_of_measurement: rpm
    accuracy_decimals: 0
    filters:
      - or:
          - throttle: 300s
          - delta: 100.0
```

**NOTE**: when setting `global_filters` that **does** takes precedence over the `filters` set on a per-sensor basis.
## Upgrading from earlier versions

- Entity IDs stay the same; only the friendly names in Home Assistant change (from `fan_speed`
  to `Fan speed`, etc.). If you set your own `name:` nothing changes.
- New entities `Ventilation` (fan), `Boost` and `Bypass` (select) are added. If you built these
  yourself with template buttons/selects you can remove those, or hide the new ones with
  `internal: true`.
- The unit of `days_remaining_filter` is now `d` (Home Assistant standard) instead of `days`.
- Some diagnostic sensors are now disabled by default; enable them in Home Assistant if you use them.
- Note on naming: PDO 221 (`post_heater_temp_before`) is what the ComfoConnect app shows as
  supply air temperature; `extract_air_*` is air coming *from* the rooms, `exhaust_air_*` is
  air going outside.

## Credits

A lot of this repo was inspired by the reverse engineering [here](https://github.com/marco-hoyer/zcan/issues/1).
If you'd like to know more how the unit communicates, head over

* [here](https://github.com/michaelarnauts/aiocomfoconnect/blob/master/docs/PROTOCOL-RMI.md)
* [and here](https://github.com/michaelarnauts/aiocomfoconnect/blob/master/docs/PROTOCOL-PDO.md)

