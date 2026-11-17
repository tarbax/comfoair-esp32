import math
from enum import Enum

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.cpp_generator as cppg
from esphome import pins
from esphome.components import binary_sensor, climate, fan, select, sensor, text_sensor
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ID,
    CONF_NAME,
    CONF_RX_PIN,
    CONF_TX_PIN,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_VOLUME_FLOW_RATE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_CUBIC_METER_PER_HOUR,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
)
from esphome.core import ID

DEPENDENCIES = ["climate"]
AUTO_LOAD = ["text_sensor", "binary_sensor", "sensor", "climate", "fan", "select"]
MULTI_CONF = True

CONF_GLOBAL_FILTERS = "global_filters"
CONF_FAN = "fan"
CONF_BOOST = "boost"
CONF_BYPASS = "bypass"
CONF_DURATIONS = "durations"

comfoair_ns = cg.esphome_ns.namespace("comfoair")
Comfoair = comfoair_ns.class_("Comfoair", cg.Component, climate.Climate)
ComfoairFan = comfoair_ns.class_("ComfoairFan", fan.Fan)
ComfoairTimerSelect = comfoair_ns.class_("ComfoairTimerSelect", select.Select)

# Must match the TimerAction / TimerKind enums in comfoair.h
TIMER_ACTION_RESET = 0
TIMER_ACTION_ON = 1
TIMER_ACTION_OFF = 2
TIMER_KIND_BOOST = 0
TIMER_KIND_BYPASS = 1
TIMER_PERMANENT = 0xFFFFFFFF


class ComfoNumConvs(Enum):
    UINT8 = 0
    UINT16 = 1
    UINT32 = 2
    INT16 = 3


# ---------------------------------------------------------------------------
# Entity definitions
#
# The default names are chosen so that their slug equals the YAML key
# (e.g. "Supply fan flow" -> supply_fan_flow). That keeps the ESPHome
# object_id - and therefore the Home Assistant unique_id / entity_id -
# identical to previous versions of this component, which used the raw key
# as name. Only the friendly name in Home Assistant changes.
#
# Keys:
#   name      default entity name
#   PDO       ComfoAir PDO number
#   CONV      raw value conversion
#   div       divider (also determines accuracy_decimals)
#   unit/device_class/state_class/icon/category  HA metadata
#   disabled  created but disabled by default in Home Assistant
# ---------------------------------------------------------------------------
_TEMP = {"unit": UNIT_CELSIUS, "device_class": DEVICE_CLASS_TEMPERATURE,
         "state_class": STATE_CLASS_MEASUREMENT, "CONV": ComfoNumConvs.INT16, "div": 10}
_HUM = {"unit": UNIT_PERCENT, "device_class": DEVICE_CLASS_HUMIDITY,
        "state_class": STATE_CLASS_MEASUREMENT, "CONV": ComfoNumConvs.UINT8}
_TIMER = {"unit": UNIT_SECOND, "device_class": DEVICE_CLASS_DURATION,
          "category": ENTITY_CATEGORY_DIAGNOSTIC, "CONV": ComfoNumConvs.UINT32}
_ENERGY = {"unit": UNIT_KILOWATT_HOURS, "device_class": DEVICE_CLASS_ENERGY,
           "state_class": STATE_CLASS_TOTAL_INCREASING, "CONV": ComfoNumConvs.UINT16}
_POWER = {"unit": UNIT_WATT, "device_class": DEVICE_CLASS_POWER,
          "state_class": STATE_CLASS_MEASUREMENT, "CONV": ComfoNumConvs.UINT16}
_FLOW = {"unit": UNIT_CUBIC_METER_PER_HOUR, "device_class": DEVICE_CLASS_VOLUME_FLOW_RATE,
         "state_class": STATE_CLASS_MEASUREMENT, "icon": "mdi:weather-windy",
         "CONV": ComfoNumConvs.UINT16}
_DUTY = {"unit": UNIT_PERCENT, "state_class": STATE_CLASS_MEASUREMENT, "icon": "mdi:fan",
         "category": ENTITY_CATEGORY_DIAGNOSTIC, "CONV": ComfoNumConvs.UINT8}
_RPM = {"unit": "rpm", "state_class": STATE_CLASS_MEASUREMENT, "icon": "mdi:fan",
        "category": ENTITY_CATEGORY_DIAGNOSTIC, "CONV": ComfoNumConvs.UINT16}
_ANALOG = {"unit": UNIT_VOLT, "device_class": DEVICE_CLASS_VOLTAGE,
           "state_class": STATE_CLASS_MEASUREMENT, "category": ENTITY_CATEGORY_DIAGNOSTIC,
           "disabled": True, "CONV": ComfoNumConvs.UINT8, "div": 10}

sensors = {
    "fan_speed": {"name": "Fan speed", "PDO": 65, "CONV": ComfoNumConvs.UINT8,
                  "icon": "mdi:fan", "state_class": STATE_CLASS_MEASUREMENT},
    "next_fan_change": {**_TIMER, "name": "Next fan change", "PDO": 81},
    "next_bypass_change": {**_TIMER, "name": "Next bypass change", "PDO": 82},
    "next_supply_change": {**_TIMER, "name": "Next supply change", "PDO": 86, "disabled": True},
    "next_exhaust_change": {**_TIMER, "name": "Next exhaust change", "PDO": 87, "disabled": True},

    "exhaust_fan_duty": {**_DUTY, "name": "Exhaust fan duty", "PDO": 117},
    "supply_fan_duty": {**_DUTY, "name": "Supply fan duty", "PDO": 118},
    "exhaust_fan_flow": {**_FLOW, "name": "Exhaust fan flow", "PDO": 119},
    "supply_fan_flow": {**_FLOW, "name": "Supply fan flow", "PDO": 120},
    "exhaust_fan_speed": {**_RPM, "name": "Exhaust fan speed", "PDO": 121},
    "supply_fan_speed": {**_RPM, "name": "Supply fan speed", "PDO": 122},

    "power_consumption_current": {**_POWER, "name": "Power consumption current", "PDO": 128},
    "energy_consumption_ytd": {**_ENERGY, "name": "Energy consumption YTD", "PDO": 129},
    "energy_consumption_since_start": {**_ENERGY, "name": "Energy consumption since start", "PDO": 130},

    "days_remaining_filter": {"name": "Days remaining filter", "PDO": 192, "CONV": ComfoNumConvs.UINT16,
                              "unit": "d", "icon": "mdi:air-filter",
                              "state_class": STATE_CLASS_MEASUREMENT},

    "avoided_heating_actual": {**_POWER, "name": "Avoided heating actual", "PDO": 213, "icon": "mdi:radiator"},
    "avoided_heating_ytd": {**_ENERGY, "name": "Avoided heating YTD", "PDO": 214, "icon": "mdi:radiator"},
    "avoided_heating_total": {**_ENERGY, "name": "Avoided heating total", "PDO": 215, "icon": "mdi:radiator"},
    "avoided_cooling_actual": {**_POWER, "name": "Avoided cooling actual", "PDO": 216, "icon": "mdi:snowflake"},
    "avoided_cooling_ytd": {**_ENERGY, "name": "Avoided cooling YTD", "PDO": 217, "icon": "mdi:snowflake"},
    "avoided_cooling_total": {**_ENERGY, "name": "Avoided cooling total", "PDO": 218, "icon": "mdi:snowflake"},

    "bypass_state": {"name": "Bypass state", "PDO": 227, "CONV": ComfoNumConvs.UINT8,
                     "unit": UNIT_PERCENT, "icon": "mdi:valve", "state_class": STATE_CLASS_MEASUREMENT},

    # temperatures
    "rmot": {**_TEMP, "name": "RMOT", "PDO": 209, "icon": "mdi:home-thermometer-outline"},
    "target_temp": {**_TEMP, "name": "Target temp", "PDO": 212, "CONV": ComfoNumConvs.UINT16},
    "pre_heater_temp_before": {**_TEMP, "name": "Pre heater temp before", "PDO": 220},
    # PDO 221 is what the ComfoConnect app shows as the supply air temperature.
    "post_heater_temp_before": {**_TEMP, "name": "Post heater temp before", "PDO": 221},
    "extract_air_temp": {**_TEMP, "name": "Extract air temp", "PDO": 274},
    "exhaust_air_temp": {**_TEMP, "name": "Exhaust air temp", "PDO": 275},
    "outdoor_air_temp": {**_TEMP, "name": "Outdoor air temp", "PDO": 276},
    "pre_heater_temp_after": {**_TEMP, "name": "Pre heater temp after", "PDO": 277, "disabled": True},
    "post_heater_temp_after": {**_TEMP, "name": "Post heater temp after", "PDO": 278, "disabled": True},

    # humidity
    "extract_air_humidity": {**_HUM, "name": "Extract air humidity", "PDO": 290},
    "exhaust_air_humidity": {**_HUM, "name": "Exhaust air humidity", "PDO": 291},
    "outdoor_air_humidity": {**_HUM, "name": "Outdoor air humidity", "PDO": 292},
    "pre_heater_hum_after": {**_HUM, "name": "Pre heater hum after", "PDO": 293, "disabled": True},
    "supply_air_humidity": {**_HUM, "name": "Supply air humidity", "PDO": 294},

    # analog ports
    "analog_input_0_10v_1": {**_ANALOG, "name": "Analog input 0 10V 1", "PDO": 369},
    "analog_input_0_10v_2": {**_ANALOG, "name": "Analog input 0 10V 2", "PDO": 370},
    "analog_input_0_10v_3": {**_ANALOG, "name": "Analog input 0 10V 3", "PDO": 371},
    "analog_input_0_10v_4": {**_ANALOG, "name": "Analog input 0 10V 4", "PDO": 372},
}

textSensors = {
    "operating_mode": {
        "name": "Operating mode", "PDO": 49, "icon": "mdi:cog-outline",
        "code": 'return vals[0] == 1 ? "limited_manual" : (vals[0] == 0xff ? "auto" : "unlimited_manual");',
    },
    "bypass_activation_mode": {
        "name": "Bypass activation mode", "PDO": 66, "icon": "mdi:valve",
        "code": 'return vals[0] == 0 ? "auto" : (vals[0] == 1 ? "activated" : "deactivated");',
    },
    "temp_profile": {
        "name": "Temp profile", "PDO": 67, "icon": "mdi:thermometer-lines",
        "code": 'return vals[0] == 0 ? "auto" : (vals[0] == 1 ? "cold" : "warm");',
    },
}

binarySensors = {
    "heating_season": {"name": "Heating season", "PDO": 210, "icon": "mdi:radiator",
                       "code": "return vals[0] == 1;"},
    "cooling_season": {"name": "Cooling season", "PDO": 211, "icon": "mdi:snowflake",
                       "code": "return vals[0] == 1;"},
    "manual_mode": {"name": "Manual mode", "PDO": 56, "icon": "mdi:hand-back-right-outline",
                    "category": ENTITY_CATEGORY_DIAGNOSTIC, "code": "return vals[0] == 1;"},
    "away": {"name": "Away", "PDO": 16, "icon": "mdi:home-export-outline",
             "code": "return vals[0] == 0x08;"},
    "fan_supply_only": {"name": "Fan supply only", "PDO": 70, "icon": "mdi:import",
                        "code": "return vals[0] != 0;"},
    "fan_exhaust_only": {"name": "Fan exhaust only", "PDO": 71, "icon": "mdi:export",
                         "code": "return vals[0] != 0;"},
}


def _opt(value, key):
    return value.get(key, cv.UNDEFINED)


def _default_conf(value):
    conf = {CONF_NAME: value["name"]}
    if value.get("disabled"):
        conf[CONF_DISABLED_BY_DEFAULT] = True
    return conf


GEN_SENSORS_SCHEMA = {
    cv.Optional(key, default=_default_conf(value)): cv.maybe_simple_value(
        sensor.sensor_schema(
            unit_of_measurement=_opt(value, "unit"),
            icon=_opt(value, "icon"),
            accuracy_decimals=math.trunc(math.log10(value["div"])) if "div" in value else 0,
            device_class=_opt(value, "device_class"),
            state_class=_opt(value, "state_class"),
            entity_category=_opt(value, "category"),
        ),
        key=CONF_NAME,
    )
    for key, value in sensors.items()
}

GEN_TEXTSENSORS_SCHEMA = {
    cv.Optional(key, default=_default_conf(value)): cv.maybe_simple_value(
        text_sensor.text_sensor_schema(
            icon=_opt(value, "icon"),
            entity_category=_opt(value, "category"),
        ),
        key=CONF_NAME,
    )
    for key, value in textSensors.items()
}

GEN_BINARYSENSORS_SCHEMA = {
    cv.Optional(key, default=_default_conf(value)): cv.maybe_simple_value(
        binary_sensor.binary_sensor_schema(
            icon=_opt(value, "icon"),
            entity_category=_opt(value, "category"),
        ),
        key=CONF_NAME,
    )
    for key, value in binarySensors.items()
}


# ---------------------------------------------------------------------------
# Fan / boost / bypass entities
# ---------------------------------------------------------------------------
def _validate_durations(value):
    value = cv.ensure_list(cv.positive_time_period_seconds)(value)
    seconds = []
    for period in value:
        s = period.total_seconds
        if s < 60:
            raise cv.Invalid("Durations must be at least 1 minute")
        if s % 60:
            raise cv.Invalid("Durations must be whole minutes")
        if s not in seconds:
            seconds.append(s)
    if not seconds:
        raise cv.Invalid("At least one duration is required")
    return sorted(seconds)


def _duration_label(seconds):
    if seconds % 3600 == 0:
        return f"{seconds // 3600} h"
    if seconds > 3600:
        return f"{seconds // 3600} h {(seconds % 3600) // 60} min"
    return f"{seconds // 60} min"


def _timer_select_schema(icon, default_durations):
    return cv.maybe_simple_value(
        select.select_schema(ComfoairTimerSelect, icon=icon).extend(
            {cv.Optional(CONF_DURATIONS, default=default_durations): _validate_durations}
        ),
        key=CONF_NAME,
    )


FAN_SCHEMA = cv.maybe_simple_value(
    fan.fan_schema(ComfoairFan, icon="mdi:hvac"),
    key=CONF_NAME,
)

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(Comfoair).extend({
        cv.GenerateID(): cv.declare_id(Comfoair),
        cv.Optional(CONF_RX_PIN, default=21): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_TX_PIN, default=25): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_GLOBAL_FILTERS): sensor.validate_filters,
        cv.Optional(CONF_FAN, default={CONF_NAME: "Ventilation"}): FAN_SCHEMA,
        cv.Optional(CONF_BOOST, default={CONF_NAME: "Boost"}): _timer_select_schema(
            "mdi:fan-plus",
            ["10min", "20min", "30min", "45min", "1h", "2h", "3h", "4h", "6h", "8h"],
        ),
        cv.Optional(CONF_BYPASS, default={CONF_NAME: "Bypass"}): _timer_select_schema(
            "mdi:valve",
            ["1h", "2h", "4h", "8h", "12h", "24h"],
        ),
    })
    .extend(GEN_SENSORS_SCHEMA)
    .extend(GEN_TEXTSENSORS_SCHEMA)
    .extend(GEN_BINARYSENSORS_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
)


async def _timer_select_to_code(parent, conf, kind):
    # (label, action, seconds)
    entries = []
    if kind == TIMER_KIND_BOOST:
        entries.append(("Off", TIMER_ACTION_RESET, 0))
        for s in conf[CONF_DURATIONS]:
            entries.append((_duration_label(s), TIMER_ACTION_ON, s))
    else:
        entries.append(("Auto", TIMER_ACTION_RESET, 0))
        for s in conf[CONF_DURATIONS]:
            entries.append((f"Open {_duration_label(s)}", TIMER_ACTION_ON, s))
        entries.append(("Open (permanent)", TIMER_ACTION_ON, TIMER_PERMANENT))
        for s in conf[CONF_DURATIONS]:
            entries.append((f"Closed {_duration_label(s)}", TIMER_ACTION_OFF, s))
        entries.append(("Closed (permanent)", TIMER_ACTION_OFF, TIMER_PERMANENT))

    sel = await select.new_select(conf, parent, kind, options=[e[0] for e in entries])
    for _, action, seconds in entries:
        cg.add(sel.add_entry(action, seconds))
    return sel


async def to_code(config):
    # collin80/esp32_can@6d835ae + drop unused deprecated ADC includes (issue #97)
    cg.add_library("esp32_can", None, "https://github.com/waverider93/ESP32_CAN.git#9867695cab86ca4b83a33794aea1202df1642be2")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_rx(config[CONF_RX_PIN]))
    cg.add(var.set_tx(config[CONF_TX_PIN]))

    for key, value in sensors.items():
        sens = await sensor.new_sensor(config[key])

        filterconf = []
        if config.get(CONF_GLOBAL_FILTERS):
            for flt in config[CONF_GLOBAL_FILTERS]:
                tmp = flt.copy()
                tmp["type_id"] = ID(tmp["type_id"].id + key, type=tmp["type_id"].type)
                filterconf.append(tmp)

        if filterconf:
            cg.add_define("USE_SENSOR_FILTER")
            filters = await sensor.build_filters(filterconf)
            cg.add(sens.set_filters(filters))

        cg.add(var.register_sensor(sens, key, value["PDO"], value["CONV"].value, value.get("div", 1)))

    for key, value in textSensors.items():
        sens = await text_sensor.new_text_sensor(config[key])
        lamda = cppg.RawExpression(f"[](uint8_t *vals) -> std::string {{ {value['code']} }}")
        cg.add(var.register_textSensor(sens, key, value["PDO"], lamda))

    for key, value in binarySensors.items():
        sens = await binary_sensor.new_binary_sensor(config[key])
        lamda = cppg.RawExpression(f"[](uint8_t *vals) -> bool {{ {value['code']} }}")
        cg.add(var.register_binarySensor(sens, key, value["PDO"], lamda))

    fan_var = cg.new_Pvariable(config[CONF_FAN][CONF_ID], var)
    await fan.register_fan(fan_var, config[CONF_FAN])
    cg.add(var.set_fan(fan_var))

    boost_sel = await _timer_select_to_code(var, config[CONF_BOOST], TIMER_KIND_BOOST)
    cg.add(var.set_boost_select(boost_sel))

    bypass_sel = await _timer_select_to_code(var, config[CONF_BYPASS], TIMER_KIND_BYPASS)
    cg.add(var.set_bypass_select(bypass_sel))

    await climate.register_climate(var, config)
