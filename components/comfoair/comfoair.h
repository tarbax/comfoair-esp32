#pragma once
#include <inttypes.h>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <esp32_can.h>

#include "esphome/components/api/custom_api_device.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esphome/components/climate/climate_traits.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/select/select.h"
#include "CanAddress.h"
#include "commands.h"
#define amin(a,b) ((a) < (b) ? (a): (b))

namespace esphome {
namespace comfoair {

static const char *const TAG = "comfoair.component";

/// Duration value the unit interprets as "until changed".
static const uint32_t TIMER_PERMANENT = 0xFFFFFFFF;

// Must match TIMER_ACTION_* / TIMER_KIND_* in __init__.py
enum TimerAction : uint8_t {
  TIMER_ACTION_RESET = 0,  // boost: end, bypass: back to auto
  TIMER_ACTION_ON = 1,     // boost: start, bypass: open (activated)
  TIMER_ACTION_OFF = 2,    // bypass: closed (deactivated)
};
enum TimerKind : uint8_t {
  TIMER_KIND_BOOST = 0,
  TIMER_KIND_BYPASS = 1,
};

template <typename T, typename CONV>
class ComfoSensor {
  public:
    T* sensor;
    float divider;
    CONV conversion;
};

class Comfoair;

/**
 * Native fan entity: off = ventilation level 0 (away), speeds 1..3 map to
 * ventilation levels 1..3, preset "Auto" puts the unit back into its
 * automatic schedule.
 */
class ComfoairFan : public fan::Fan {
 public:
  static constexpr const char *PRESET_AUTO = "Auto";

  explicit ComfoairFan(Comfoair *parent) : parent_(parent) {
    this->set_supported_preset_modes({PRESET_AUTO});
  }

  fan::FanTraits get_traits() override {
    fan::FanTraits traits(false, true, false, 3);
    this->wire_preset_modes_(traits);
    return traits;
  }

  /// Called when the unit reports a new ventilation level (PDO 65).
  void update_level(int level);
  /// Called when the unit reports its operating mode (PDO 49).
  void update_auto_mode(bool is_auto);

 protected:
  void control(const fan::FanCall &call) override;

  Comfoair *parent_;
  int last_on_level_{2};
};

/**
 * Select entity whose options each map to a timed command
 * (boost for N minutes, bypass open/closed for N hours, ...).
 */
class ComfoairTimerSelect : public select::Select {
 public:
  struct Entry {
    uint8_t action;
    uint32_t seconds;
  };

  ComfoairTimerSelect(Comfoair *parent, uint8_t kind) : parent_(parent), kind_(kind) {}

  void add_entry(uint8_t action, uint32_t seconds) { this->entries_.push_back({action, seconds}); }
  uint8_t kind() const { return this->kind_; }
  const Entry &entry(size_t index) const { return this->entries_[index]; }

  /// Index of the option with exactly this action and duration.
  optional<size_t> find(uint8_t action, uint32_t seconds) const {
    for (size_t i = 0; i < this->entries_.size(); i++) {
      if (this->entries_[i].action == action && this->entries_[i].seconds == seconds)
        return i;
    }
    return {};
  }
  /// Index of the first option with this action (the "reset" option, typically).
  optional<size_t> find_action(uint8_t action) const {
    for (size_t i = 0; i < this->entries_.size(); i++) {
      if (this->entries_[i].action == action)
        return i;
    }
    return {};
  }
  /// Action of the currently shown option, if any.
  optional<uint8_t> current_action() const {
    auto idx = this->active_index();
    if (!idx.has_value() || *idx >= this->entries_.size())
      return {};
    return this->entries_[*idx].action;
  }
  void publish_index(size_t index) {
    auto cur = this->active_index();
    if (!cur.has_value() || *cur != index)
      this->publish_state(index);
  }

 protected:
  void control(size_t index) override;

  Comfoair *parent_;
  uint8_t kind_;
  std::vector<Entry> entries_;
};

class Comfoair: public Component, public climate::Climate, public esphome::api::CustomAPIDevice {

 public:
  void set_rx(int rx) { rx_ = rx; }
  void set_tx(int tx) { tx_ = tx; }
  void set_fan(ComfoairFan *fan) { fan_ = fan; }
  void set_boost_select(ComfoairTimerSelect *sel) { boost_select_ = sel; }
  void set_bypass_select(ComfoairTimerSelect *sel) { bypass_select_ = sel; }

  void register_sensor(sensor::Sensor *obj, std::string key, int PDOID, int conversionType, int divider) {
    ComfoSensor<sensor::Sensor, int> cs;
    cs.sensor = obj;
    cs.divider = divider;
    cs.conversion = conversionType;
    sensors[PDOID] = cs;
    this->PDOs.push_back(PDOID);
    this->PDOsMap[PDOID] = key;
  }
  void register_textSensor(text_sensor::TextSensor *obj, std::string key, int PDOID, std::string (*convLambda)(uint8_t *) ) {
    ComfoSensor<text_sensor::TextSensor, std::string (*)(uint8_t *)> cs;
    cs.sensor = obj;
    cs.divider = 1;
    cs.conversion = convLambda;
    textSensors[PDOID] = cs;
    this->PDOs.push_back(PDOID);
    this->PDOsMap[PDOID] = key;
  }
  void register_binarySensor(binary_sensor::BinarySensor *obj, std::string key, int PDOID, bool (*convLambda)(uint8_t *) ) {
    ComfoSensor<binary_sensor::BinarySensor, bool (*)(uint8_t *)> cs;
    cs.sensor = obj;
    cs.divider = 1;
    cs.conversion = convLambda;
    binarySensors[PDOID] = cs;
    this->PDOs.push_back(PDOID);
    this->PDOsMap[PDOID] = key;
  }

  // -------------------------------------------------------------------------
  // High level commands
  // -------------------------------------------------------------------------

  /// Set ventilation level 0 (away) .. 3.
  void set_ventilation_level(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    this->send_twice_({0x84, 0x15, COMFO_SUBUNIT_VENTILATION, 0x01, 0x00, 0x00, 0x00, 0x00,
                       0x01, 0x00, 0x00, 0x00, (uint8_t) level});
  }

  /// Start boost (ventilation level 3) for the given number of seconds.
  void boost(uint32_t seconds) {
    if (seconds == 0) {
      this->boost_end();
      return;
    }
    ESP_LOGI(TAG, "Boost for %" PRIu32 " s", seconds);
    std::vector<uint8_t> cmd = {0x84, 0x15, COMFO_SUBUNIT_VENTILATION, 0x06, 0x00, 0x00, 0x00, 0x00};
    append_u32_(cmd, seconds);
    cmd.insert(cmd.end(), {0x03, 0x00, 0x00, 0x00});
    this->send_twice_(cmd);

    if (this->boost_select_ != nullptr) {
      auto idx = this->boost_select_->find(TIMER_ACTION_ON, seconds);
      if (idx.has_value())
        this->boost_select_->publish_index(*idx);
      // The unit does not report boost state, so fall back to "Off" once the timer has elapsed.
      if (seconds < UINT32_MAX / 1000) {
        this->set_timeout("boost_select_reset", seconds * 1000, [this]() { this->publish_boost_off_(); });
      }
    }
  }

  void boost_end() {
    ESP_LOGI(TAG, "Boost end");
    this->send_twice_(CMD_boost_end);
    this->cancel_timeout("boost_select_reset");
    this->publish_boost_off_();
  }

  /**
   * Control the bypass.
   * action: TIMER_ACTION_RESET (auto), TIMER_ACTION_ON (open/activated) or TIMER_ACTION_OFF (closed/deactivated)
   * seconds: duration, TIMER_PERMANENT for "until changed"
   */
  void set_bypass(uint8_t action, uint32_t seconds) {
    if (action == TIMER_ACTION_RESET) {
      ESP_LOGI(TAG, "Bypass auto");
      this->send_twice_(CMD_bypass_auto);
    } else {
      if (seconds == 0)
        seconds = TIMER_PERMANENT;
      ESP_LOGI(TAG, "Bypass %s for %" PRIu32 " s", action == TIMER_ACTION_ON ? "open" : "closed", seconds);
      std::vector<uint8_t> cmd = {0x84, 0x15, COMFO_SUBUNIT_BYPASS, 0x01, 0x00, 0x00, 0x00, 0x00};
      append_u32_(cmd, seconds);
      cmd.push_back(action == TIMER_ACTION_ON ? 0x01 : 0x02);
      this->send_twice_(cmd);
    }
    if (this->bypass_select_ != nullptr) {
      auto idx = this->bypass_select_->find(action, action == TIMER_ACTION_RESET ? 0 : seconds);
      // Durations that are not in the option list are shown as the permanent option.
      if (!idx.has_value() && action != TIMER_ACTION_RESET)
        idx = this->bypass_select_->find(action, TIMER_PERMANENT);
      if (idx.has_value())
        this->bypass_select_->publish_index(*idx);
    }
  }

  void handle_timer_select(ComfoairTimerSelect *sel, size_t index) {
    const auto &e = sel->entry(index);
    if (sel->kind() == TIMER_KIND_BOOST) {
      if (e.action == TIMER_ACTION_RESET) {
        this->boost_end();
      } else {
        this->boost(e.seconds);
      }
    } else {
      this->set_bypass(e.action, e.seconds);
    }
    sel->publish_index(index);
  }

  /**
   * Send a command to the ComfoAir.
   *
   * Besides the fixed commands below, timed commands accept any duration:
   *   boost_<N>_min, boost_<N>h
   *   bypass_activate_<N>_min, bypass_activate_<N>h, bypass_activate_permanent
   *   bypass_deactivate_<N>_min, bypass_deactivate_<N>h, bypass_deactivate_permanent
   */
  void send_command(std::string command) {
    uint32_t seconds;
    if (command == "boost_end") {
      this->boost_end();
      return;
    }
    if (command == "bypass_auto") {
      this->set_bypass(TIMER_ACTION_RESET, 0);
      return;
    }
    if (parse_timed_command_(command, "boost_", seconds)) {
      if (seconds == TIMER_PERMANENT) {
        ESP_LOGW(TAG, "Permanent boost is not supported, use a duration");
        return;
      }
      this->boost(seconds);
      return;
    }
    if (parse_timed_command_(command, "bypass_activate_", seconds)) {
      this->set_bypass(TIMER_ACTION_ON, seconds);
      return;
    }
    if (parse_timed_command_(command, "bypass_deactivate_", seconds)) {
      this->set_bypass(TIMER_ACTION_OFF, seconds);
      return;
    }

    #define CMDIF(name) if (command == #name) { this->send_twice_( CMD_ ## name ); } else
    CMDIF(ventilation_level_0)
    CMDIF(ventilation_level_1)
    CMDIF(ventilation_level_2)
    CMDIF(ventilation_level_3)
    CMDIF(auto)
    CMDIF(manual)
    CMDIF(ventilation_supply_only)
    CMDIF(ventilation_supply_only_reset)
    CMDIF(ventilation_extract_only)
    CMDIF(ventilation_extract_only_reset)
    CMDIF(temp_profile_normal)
    CMDIF(temp_profile_cool)
    CMDIF(temp_profile_warm)
    {
      ESP_LOGW(TAG, "Unknown command '%s'", command.c_str());
    }
    #undef CMDIF
  }

  /// HA service: boost for N minutes (0 = end boost).
  void boost_service(int32_t minutes) {
    if (minutes <= 0) {
      this->boost_end();
    } else {
      this->boost((uint32_t) minutes * 60);
    }
  }

  /// HA service: mode = auto | open | closed, minutes <= 0 means "until changed".
  void bypass_service(std::string mode, int32_t minutes) {
    uint32_t seconds = minutes > 0 ? (uint32_t) minutes * 60 : TIMER_PERMANENT;
    if (mode == "auto") {
      this->set_bypass(TIMER_ACTION_RESET, 0);
    } else if (mode == "open" || mode == "on" || mode == "activate") {
      this->set_bypass(TIMER_ACTION_ON, seconds);
    } else if (mode == "closed" || mode == "close" || mode == "off" || mode == "deactivate") {
      this->set_bypass(TIMER_ACTION_OFF, seconds);
    } else {
      ESP_LOGW(TAG, "Unknown bypass mode '%s' (use auto, open or closed)", mode.c_str());
    }
  }

  void sendHex(std::string hexSequenceToSend) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hexSequenceToSend.length() / 2);
    for (unsigned int i = 0; i + 1 < hexSequenceToSend.length(); i += 2) {
        std::string byteString = hexSequenceToSend.substr(i, 2);
        uint8_t byte = (uint8_t) strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }
    this->sendRaw(bytes.size(), bytes.data());
  }
  void sendVector(std::vector<uint8_t> *data) {
    this->sendRaw(data->size(), data->data());
  }
  void sendRaw(uint8_t length, uint8_t *buf){
     sequence++;
     sequence = sequence & 0x3;
     CAN_FRAME message;
     message.extended = true;
     message.rtr = 0;

     if (length > 8) {
         CanAddress addr = CanAddress(0x3a, 0x1, 0, 1, 0, 1, this->sequence);
         uint8_t dataGrams = length / 7;
         if (dataGrams * 7 == length)  {
             dataGrams--;
         }
         for (uint8_t i = 0; i < dataGrams; i++) {
             memset(message.data.byte, 0, 8);
             message.data.uint8[0] = i;
             message.length = amin((i*7)+7, length) - i*7 + 1;

             message.id = addr.canID();
             memcpy(& message.data.uint8[1], &buf[i * 7], message.length - 1);
             this->log_can_frame_("TX", message);
             CAN0.sendFrame(message);
         }
         // Send last packet
         memset(message.data.uint8, 0, 8);
         message.data.uint8[0] = dataGrams | 0x80;
         message.length = length - dataGrams * 7 + 1;
         memcpy(& message.data.uint8[1], &buf[dataGrams * 7], length - dataGrams * 7);
         this->log_can_frame_("TX", message);
         CAN0.sendFrame(message);

     } else {
         CanAddress addr = CanAddress(0x3a, 0x1, 0, 0, 0, 1, this->sequence);
         message.id = addr.canID();
         message.length = length;
         memcpy(message.data.uint8, buf, length);
         this->log_can_frame_("TX", message);
         CAN0.sendFrame(message);

     }
  }

  /**
   * Enable CAN trace mode: logs RX and TX frames via ESP_LOGI.
   * Automatically disables itself after 10 minutes.
   */
  void enable_can_trace() {
    can_trace_enabled_ = true;
    can_trace_frame_count_ = 0;

    ESP_LOGI(TAG, "CAN trace enabled");

    this->set_timeout("can_trace_auto_disable", 10 * 60 * 1000, [this]() {
      this->disable_can_trace();
    });
  }

  void disable_can_trace() {
    this->cancel_timeout("can_trace_auto_disable");

    if (can_trace_enabled_) {
      ESP_LOGI(
        TAG,
        "CAN trace disabled after %" PRIu32 " frames",
        can_trace_frame_count_
      );
    }

    can_trace_enabled_ = false;
  }

  /**
   * Optionally restrict tracing to a single PDO id. Pass -1 to disable filtering.
   */
  void set_can_trace_pdo_filter(float pdo) {
    can_trace_pdo_filter_ = (int) pdo;
    ESP_LOGI(TAG, "CAN trace PDO filter set to %d", can_trace_pdo_filter_);
  }

  void clear_can_trace_pdo_filter() {
    can_trace_pdo_filter_ = -1;
    ESP_LOGI(TAG, "CAN trace PDO filter cleared");
  }

  /**
   * Returns whether CAN trace mode is currently active. Useful to back a
   * template switch entity in ESPHome (turn_on/turn_off + lambda for state).
   */
  bool is_can_trace_enabled() const {
    return can_trace_enabled_;
  }

  void setup() override{
     CAN0.setCANPins( (gpio_num_t) this->rx_, (gpio_num_t) this->tx_);
     CAN0.begin(50000);
     CAN0.watchFor();
     register_service(&Comfoair::send_command, "send_command", {"command"});
     register_service(&Comfoair::sendHex, "send_hex", {"hexSequence"});
     register_service(&Comfoair::req_update_service, "req_update_service", {"PDOID"});
     register_service(&Comfoair::update_next, "update_all", {});
     register_service(&Comfoair::boost_service, "boost", {"minutes"});
     register_service(&Comfoair::bypass_service, "bypass", {"mode", "minutes"});
     register_service(&Comfoair::enable_can_trace, "enable_can_trace", {});
     register_service(&Comfoair::disable_can_trace, "disable_can_trace", {});
     register_service(&Comfoair::set_can_trace_pdo_filter, "set_can_trace_pdo_filter", {"pdo"});
     register_service(&Comfoair::clear_can_trace_pdo_filter, "clear_can_trace_pdo_filter", {});
     // The unit does not report boost state; assume no boost is running after a restart.
     this->publish_boost_off_();
     this->update_next();
  }

  /**
   * Logs a CAN frame (RX or TX) via ESP_LOGI when trace mode is enabled.
   * Honors an optional PDO filter set via set_can_trace_pdo_filter().
   */
  void log_can_frame_(const char *direction, const CAN_FRAME &frame) {
    if (!can_trace_enabled_)
      return;

    uint16_t pdo_id =
        static_cast<uint16_t>((frame.id & 0x01FFF000) >> 14);

    if (can_trace_pdo_filter_ >= 0 &&
        pdo_id != can_trace_pdo_filter_) {
      return;
    }

    can_trace_frame_count_++;

    char data_buf[3 * 8 + 1];
    size_t pos = 0;

    for (uint8_t i = 0; i < frame.length && i < 8; i++) {
      int written = snprintf(
        data_buf + pos,
        sizeof(data_buf) - pos,
        "%02X%s",
        frame.data.uint8[i],
        i + 1 < frame.length ? " " : ""
      );

      if (written <= 0)
        break;

      pos += static_cast<size_t>(written);

      if (pos >= sizeof(data_buf))
        break;
    }

    data_buf[sizeof(data_buf) - 1] = '\0';

    ESP_LOGI(
      TAG,
      "CAN %s id=0x%08" PRIX32
      " pdo=%u ext=%u rtr=%u dlc=%u data=[%s]",
      direction,
      static_cast<uint32_t>(frame.id),
      pdo_id,
      frame.extended ? 1 : 0,
      frame.rtr ? 1 : 0,
      frame.length,
      data_buf
    );

    ESP_LOGI(
      TAG,
      "CANCSV,%" PRIu32 ",%s,%08" PRIX32 ",%u,%u,%u,%s",
      millis(),
      direction,
      static_cast<uint32_t>(frame.id),
      pdo_id,
      frame.rtr ? 1 : 0,
      frame.length,
      data_buf
    );
  }

  void update_next() {
    if (update_iterator_ >= (int)PDOs.size()) {
        update_iterator_ = 0;
        if (!boot_recheck_done_) {
            // First pass done — schedule a second pass after 30s to fix
            // boot-time CAN artifacts (e.g. fan_speed PDO returning 0 briefly).
            boot_recheck_done_ = true;
            set_timeout("update_recheck", 30000, [this](){this->update_next();});
        }
        return;
    }

    int currentPDO = PDOs[update_iterator_];
    this->request_data(currentPDO);
    update_iterator_++;
    ESP_LOGD(TAG, "update_next %d - iterator %d", currentPDO, update_iterator_);
    set_timeout("update_next", 1000, [this](){this->update_next();});
  }
  void req_update_service(float pdo){
    this->request_data((int)pdo);
  }
  void request_data(int PDOID) {
    CAN_FRAME message;
    message.extended = true;
    message.rtr = 1;
    message.id = (PDOID << 14) + 0x40 + 0x3a;

    message.length = 0;
    CAN0.sendFrame(message);
  }

  void loop() override {
    if (CAN0.read(canMessage)) {
        this->log_can_frame_("RX", canMessage);

        uint16_t PDOID = (canMessage.id & 0x01fff000) >> 14;
        uint8_t *vals = &canMessage.data.uint8[0];

        auto sensor_it = sensors.find(PDOID);
        if (sensor_it != sensors.end()) {
            const auto& el = sensor_it->second;
            float sensorVal = 0;

            switch (el.conversion) {
                case 0: // UINT8
                    sensorVal = vals[0];
                    break;
                case 1: // UINT16
                    sensorVal = (vals[0] + (vals[1]<<8));
                    break;
                case 2: // UINT32
                    sensorVal = (uint32_t) (vals[0] | (vals[1] << 8) | (vals[2] << 16) | ((uint32_t) vals[3] << 24));
                    break;
                case 3: // INT16
                    sensorVal = (int16_t) (vals[0] | (vals[1] << 8));
                    break;
            }
            if (el.divider > 1) {
                sensorVal = sensorVal / el.divider;
            }
            // PDO 65 (fan_speed) always returns 0 via RTR at boot; the real
            // value only arrives as a spontaneous TPDO when the speed changes.
            // Skip the zero until we've seen at least one non-zero reading so
            // we don't incorrectly set the climate fan_mode to OFF on restart.
            const std::string &sKey = this->key_for_(PDOID);
            if (sKey == "fan_speed") {
                if (sensorVal == 0.0f && !fan_speed_initialized_) {
                    ESP_LOGD(TAG, "fan_speed: ignoring boot-time RTR zero (not yet initialized)");
                    return;
                }
                if (sensorVal > 0.0f) fan_speed_initialized_ = true;
            }
            el.sensor->publish_state(sensorVal);
            this->update_derived_(sKey, sensorVal);
        } else {
            auto text_it = textSensors.find(PDOID);
            if (text_it != textSensors.end()) {
                const auto& el = text_it->second;
                std::string value = el.conversion(vals);
                el.sensor->publish_state(value);
                this->update_derived_(this->key_for_(PDOID), value);
            } else {
                auto binary_it = binarySensors.find(PDOID);
                if (binary_it != binarySensors.end()) {
                    const auto& el = binary_it->second;
                    el.sensor->publish_state(el.conversion(vals));
                }
            }
        }
    }

  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "ComfoAir:");
    ESP_LOGCONFIG(TAG, "  RX pin: %d", this->rx_);
    ESP_LOGCONFIG(TAG, "  TX pin: %d", this->tx_);
    ESP_LOGCONFIG(TAG, "  Polled PDOs: %u", (unsigned) this->PDOs.size());
  }

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY);
    traits.set_supported_modes({climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT});
    traits.set_supported_presets({climate::CLIMATE_PRESET_HOME});
    traits.set_visual_min_temperature(10);
    traits.set_visual_max_temperature(35);
    traits.set_visual_temperature_step(1);
    traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
      climate::CLIMATE_FAN_OFF
    });
    return traits;
  }

  void control(const climate::ClimateCall &instruction) override {
    if (instruction.get_fan_mode().has_value()){
        switch (instruction.get_fan_mode().value()) {
            case climate::CLIMATE_FAN_AUTO:
            this->send_command("auto");
            break;
            case climate::CLIMATE_FAN_LOW:
            this->set_ventilation_level(1);
            break;
            case climate::CLIMATE_FAN_MEDIUM:
            this->set_ventilation_level(2);
            break;
            case climate::CLIMATE_FAN_HIGH:
            this->set_ventilation_level(3);
            break;
            case climate::CLIMATE_FAN_OFF:
            this->set_ventilation_level(0);
            break;
            default:
            break;
        }
    }
    if (instruction.get_mode().has_value()){
        switch (instruction.get_mode().value()) {
            case climate::CLIMATE_MODE_AUTO:
            this->send_command("temp_profile_normal");
            break;
            case climate::CLIMATE_MODE_COOL:
            this->send_command("temp_profile_cool");
            break;
            case climate::CLIMATE_MODE_HEAT:
            this->send_command("temp_profile_warm");
            break;
            default:
            break;
        }

    }
  }

 protected:
  static void append_u32_(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 24) & 0xFF);
  }

  /// Parses "<prefix><N>_min", "<prefix><N>min", "<prefix><N>h", "<prefix><N>_h" or "<prefix>permanent".
  static bool parse_timed_command_(const std::string &command, const char *prefix, uint32_t &seconds) {
    size_t plen = strlen(prefix);
    if (command.compare(0, plen, prefix) != 0)
      return false;
    std::string rest = command.substr(plen);
    if (rest == "permanent") {
      seconds = TIMER_PERMANENT;
      return true;
    }
    char *end = nullptr;
    unsigned long n = strtoul(rest.c_str(), &end, 10);
    if (end == rest.c_str() || n == 0)
      return false;
    std::string unit(end);
    if (unit == "_min" || unit == "min") {
      seconds = n * 60;
    } else if (unit == "h" || unit == "_h") {
      seconds = n * 3600;
    } else {
      return false;
    }
    return true;
  }

  /// Sends a frame now and once more after 1 s (the unit occasionally misses a single frame).
  void send_twice_(std::vector<uint8_t> data) {
    this->sendVector(&data);
    this->set_timeout(1000, [this, data]() mutable { this->sendVector(&data); });
  }

  void publish_boost_off_() {
    if (this->boost_select_ == nullptr)
      return;
    auto idx = this->boost_select_->find_action(TIMER_ACTION_RESET);
    if (idx.has_value())
      this->boost_select_->publish_index(*idx);
  }

  const std::string &key_for_(int pdo) const {
    static const std::string empty;
    auto it = this->PDOsMap.find(pdo);
    return it != this->PDOsMap.end() ? it->second : empty;
  }

  void update_derived_(const std::string &key, const std::string &value) {
    if (key == "temp_profile") {
        if (value == "auto") {
            this->mode = climate::CLIMATE_MODE_AUTO;
        } else if (value == "cold") {
            this->mode = climate::CLIMATE_MODE_COOL;
        } else if (value == "warm") {
            this->mode = climate::CLIMATE_MODE_HEAT;
        }
        this->publish_state();
    } else if (key == "operating_mode") {
        if (this->fan_ != nullptr)
            this->fan_->update_auto_mode(value == "auto");
    } else if (key == "bypass_activation_mode") {
        if (this->bypass_select_ == nullptr)
            return;
        uint8_t action = value == "auto" ? TIMER_ACTION_RESET
                       : (value == "activated" ? TIMER_ACTION_ON : TIMER_ACTION_OFF);
        auto current = this->bypass_select_->current_action();
        if (current.has_value() && *current == action)
            return;  // already showing a matching option (keeps the selected duration)
        auto idx = action == TIMER_ACTION_RESET ? this->bypass_select_->find_action(action)
                                                : this->bypass_select_->find(action, TIMER_PERMANENT);
        if (idx.has_value())
            this->bypass_select_->publish_index(*idx);
    }
  }

  void update_derived_(const std::string &key, float value) {
    if (key == "fan_speed") {
        int fanLevel = static_cast<int>(value + 0.5f);
        if (fanLevel == 0) {
            this->fan_mode = (climate::CLIMATE_FAN_OFF);
        } else if (fanLevel == 1) {
            this->fan_mode = (climate::CLIMATE_FAN_LOW);
        } else if (fanLevel == 2) {
            this->fan_mode = (climate::CLIMATE_FAN_MEDIUM);
        } else if (fanLevel == 3) {
            this->fan_mode = (climate::CLIMATE_FAN_HIGH);
        }
        this->publish_state();
        if (this->fan_ != nullptr)
            this->fan_->update_level(fanLevel);
    } else if (key == "extract_air_temp") {
       this->current_temperature = value;
       this->publish_state();
    } else if (key == "extract_air_humidity") {
       this->current_humidity = value;
       this->publish_state();
    } else if (key == "target_temp") {
       this->target_temperature = value;
       this->publish_state();
    }
  }

  CAN_FRAME canMessage;

  int rx_{-1};
  int tx_{-1};
  uint8_t sequence = 0;
  int update_iterator_ = 0;
  bool boot_recheck_done_ = false;
  bool fan_speed_initialized_ = false;
  bool can_trace_enabled_{false};
  uint32_t can_trace_frame_count_{0};
  int can_trace_pdo_filter_{-1};
  ComfoairFan *fan_{nullptr};
  ComfoairTimerSelect *boost_select_{nullptr};
  ComfoairTimerSelect *bypass_select_{nullptr};
  /* List of PDOs to request */
  std::vector<int> PDOs;
  /* map between pdoid and string key */
  std::map<int, std::string> PDOsMap;
  std::map<int, ComfoSensor<sensor::Sensor, int>> sensors;
  std::map<int, ComfoSensor<text_sensor::TextSensor,  std::string (*)(uint8_t *)>> textSensors;
  std::map<int, ComfoSensor<binary_sensor::BinarySensor, bool (*)(uint8_t *)>> binarySensors;

};

// ---------------------------------------------------------------------------
// ComfoairFan / ComfoairTimerSelect implementation (needs the full Comfoair type)
// ---------------------------------------------------------------------------

inline void ComfoairFan::update_level(int level) {
  bool changed = false;
  bool on = level > 0;
  if (this->state != on) {
    this->state = on;
    changed = true;
  }
  if (on) {
    this->last_on_level_ = level;
    if (this->speed != level) {
      this->speed = level;
      changed = true;
    }
  }
  if (changed)
    this->publish_state();
}

inline void ComfoairFan::update_auto_mode(bool is_auto) {
  bool changed;
  if (is_auto) {
    changed = this->set_preset_mode_(PRESET_AUTO);
  } else {
    changed = this->has_preset_mode();
    this->clear_preset_mode_();
  }
  if (changed)
    this->publish_state();
}

inline void ComfoairFan::control(const fan::FanCall &call) {
  if (call.has_preset_mode()) {
    // Only "Auto" exists: hand control back to the unit's schedule.
    this->parent_->send_command("auto");
    this->set_preset_mode_(call.get_preset_mode());
    this->publish_state();
    return;
  }

  int level = -1;
  auto state = call.get_state();
  auto speed = call.get_speed();
  if (state.has_value() && !*state) {
    level = 0;
  } else if (speed.has_value()) {
    level = *speed < 1 ? 1 : (*speed > 3 ? 3 : *speed);
  } else if (state.has_value() && *state && !this->state) {
    level = this->last_on_level_;
  }
  if (level < 0)
    return;

  this->parent_->set_ventilation_level(level);
  // Optimistic update; the unit confirms via PDO 65 / PDO 49.
  this->state = level > 0;
  if (level > 0) {
    this->speed = level;
    this->last_on_level_ = level;
  }
  this->apply_preset_mode_(call);
  this->publish_state();
}

inline void ComfoairTimerSelect::control(size_t index) {
  this->parent_->handle_timer_select(this, index);
}

} //namespace comfoair
} //namespace esphome
