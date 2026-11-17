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
| Network | Wi-Fi | Wi-Fi **or Ethernet** |

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

### Ethernet instead of Wi-Fi

Use one or the other: remove the `wifi:` block if you use `ethernet:`.

```yaml
ethernet:
  type: LAN8720
  mdc_pin: GPIO23
  mdio_pin: GPIO18
  clk_mode: GPIO0_IN
  phy_addr: 0
```

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
| Ethernet does not come up | Remove the `wifi:` block. Make sure you are not using pins 21/25 for CAN. |
| Board resets when the ventilation starts | Your power supply is too weak or the converter is not set to 5 V. |

For everything else see the main [README](../README.md#troubleshooting).
