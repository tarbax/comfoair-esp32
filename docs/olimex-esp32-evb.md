# OLIMEX ESP32-EVB

The [OLIMEX ESP32-EVB](https://www.olimex.com/Products/IoT/ESP32/ESP32-EVB/) is an easy
board for this project: the **CAN transceiver is already on the board**, and it has a wired
**Ethernet** port. You only need the board, a 12 V → 5 V converter and an Ethernet cable.

Ready-to-use config: [esphome_olimex_esp32_evb.yaml](esphome_olimex_esp32_evb.yaml)

## What is different from a plain ESP32

| | Plain ESP32 + SN65HVD230 | OLIMEX ESP32-EVB |
|---|---|---|
| CAN transceiver | separate module | on board |
| CAN pins | `tx_pin: 25`, `rx_pin: 21` (default) | **`tx_pin: 5`, `rx_pin: 35`** |
| ESPHome board | `esp32dev` | **`esp32-evb`** |
| Network | Wi-Fi | Wi-Fi, Ethernet, **or both with automatic failover** |

> ⚠️ **Always set `tx_pin: 5` and `rx_pin: 35` on this board.** The default pins (21 / 25)
> are used by the on-board Ethernet chip, so with the defaults you get no CAN data and Ethernet
> will not work.

## Wiring

```
 ComfoAir RJ45                  DC-DC converter            OLIMEX ESP32-EVB
-----------------+        +-------------------+         +------------------+
(orange/red) 12V o--------o IN+          OUT+ o---------o 5 V power input  |
(brown)      GND o--------o IN-          OUT- o---------o GND              |
                 |        +-------------------+         |                  |
(blue)       H   o------------------------------------- o CAN H            |
(white/blue) L   o------------------------------------- o CAN L            |
-----------------+                                      |                  |
                                                        |  LAN  <- router  |
                                                        +------------------+
```

- Connect CAN_H and CAN_L to the board's CAN connector (check the silkscreen for the H/L markings).
- Power the board with **5 V**. Set the DC-DC converter to 5 V *before* connecting it.
  Check the power input specification for your board revision on the OLIMEX website.
- While you are flashing over USB, you don't need the external power supply.

## Configuration

Minimal `comfoair` part:

```yaml
esp32:
  board: esp32-evb
  framework:
    type: arduino

comfoair:
  - id: comfo
    name: "ComfoAir"
    tx_pin: 5
    rx_pin: 35
```

### Network: Ethernet, Wi-Fi or both

Recent ESPHome versions (tested with 2026.10) can use **Ethernet and Wi-Fi at the same time**.
List them under `network: priority:`. The device uses the first interface in the list that
is connected:

- cable plugged in → Ethernet
- cable unplugged or switch down → Wi-Fi takes over by itself
- cable back → back to Ethernet

Home Assistant keeps reaching the device through `comfoair.local` either way.

```yaml
network:
  priority:          # first connected interface wins
    - ethernet
    - wifi

ethernet:
  type: LAN8720
  mdc_pin: GPIO23
  mdio_pin: GPIO18
  clk:
    mode: CLK_EXT_IN
    pin: 0
  phy_addr: 0

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

- **Prefer Wi-Fi?** Put `wifi` first in the list.
- **Only one of them?** Remove the other block *and* the `network:` block. Without
  `network: priority:`, ESPHome refuses a config with both `ethernet:` and `wifi:`.
- **Older ESPHome** (error *"cannot be used together"*, or *`clk` is an invalid option*):
  update ESPHome, or use Ethernet only with the old `clk_mode: GPIO0_IN` notation.
  `clk_mode` is deprecated and will be removed in ESPHome 2026.11.

> ESPHome warns that GPIO0 and GPIO5 are *strapping pins*. On this board that is expected:
> GPIO0 is the Ethernet clock and GPIO5 is CAN TX by design. You can ignore the warning.

### Bonus: relays and button

The board has two relays and a user button. They are not needed for the ComfoAir, but you can
use them for something else:

```yaml
switch:
  - platform: gpio
    name: "Relay 1"
    pin: GPIO32
  - platform: gpio
    name: "Relay 2"
    pin: GPIO33

binary_sensor:
  - platform: gpio
    name: "Button"
    pin:
      number: GPIO34
      inverted: true
```

## Troubleshooting

| Problem | Fix |
|---|---|
| No values, log shows nothing from CAN | Check `tx_pin: 5` / `rx_pin: 35`, and swap CAN H / L. |
| Ethernet does not come up | Make sure you are not using pins 21/25 for CAN. With both Ethernet and Wi-Fi, check the `network: priority:` block. |
| *"ethernet cannot be used together with wifi"* | Add the `network: priority:` block, or update ESPHome. |
| Board resets when the ventilation starts | Your power supply is too weak or the converter is not set to 5 V. |

For everything else see the main [README](../README.md#troubleshooting).
