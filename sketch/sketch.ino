#include <ArduinoJson.h>  // by Benoit Blanchon 7.x
#include <algorithm>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <FS.h>
#include <HTTPClient.h>
#include <limits>
#include <map>
#include <memory>
#include <SPIFFS.h>
#include <SPI.h>
#include <TFT_eSPI.h>            // by Bodmer 2.5.x
#include <time.h>
#include <unordered_map>
#include <vector>
#include <WebServer.h>
#include <WiFiManager.h>         // by tzapu 2.0.17
#include <WiFiClientSecure.h>
#include <XPT2046_Touchscreen.h> // by Paul Stoffregen 1.4

// The ESP32 Arduino core (3.x) ships Espressif's QR code encoder. When it is
// available the setup screens show a scannable code; otherwise text only.
#if __has_include(<qrcode.h>)
#include <qrcode.h>
#endif
#if defined(ESP_QRCODE_CONFIG_DEFAULT)
#define HAS_ESP_QRCODE 1
#else
#define HAS_ESP_QRCODE 0
#endif

#include "setup_page.h"

#define ENABLE_SERIAL_LOGS 1
//#define DEBUG_SERIAL_WIEN_MONITOR 0
#define TFT_BG tft.color565(0, 5, 0)
#define TFT_TEXT tft.color565(255, 240, 93)
#define TFT_ACCENT tft.color565(64, 158, 141)
#define MONITOR_URL "https://www.wienerlinien.at/ogd_realtime/monitor?"
#define OGD_URL "https://www.wienerlinien.at/ogd_realtime/doku/ogd/"
#define STOPS_INDEX_URL OGD_URL "wienerlinien-ogd-haltepunkte.csv"
#define LINES_URL OGD_URL "wienerlinien-ogd-linien.csv"
#define ROUTES_URL OGD_URL "wienerlinien-ogd-fahrwegverlaeufe.csv"
#define JSON_CONFIG_FILE "/sample_config.json"
#define STOPS_INDEX_FILE "/stops.csv"
#define STOPS_INDEX_TMP_FILE "/stops.tmp"
#define LINES_INDEX_FILE "/lines.csv"
#define LINES_INDEX_TMP_FILE "/lines.tmp"
#define DEVICE_HOSTNAME "wien-monitor"

// ----------------------------------------------------------------------------
// ESP32-2432S028R "Cheap Yellow Display" (CYD) hardware.
//
// The 2.8" ILI9341 panel is driven by TFT_eSPI (see User_Setup.h in the repo
// root). The XPT2046 resistive touch controller sits on a SEPARATE SPI bus, so
// it needs its own SPIClass instance and the XPT2046_Touchscreen library - it
// cannot share the display bus that TFT_eSPI uses.
// ----------------------------------------------------------------------------
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Raw touch values that map to the screen edges (rough, good enough to tell the
// left half from the right half of the panel).
#define TOUCH_RAW_MIN 300
#define TOUCH_RAW_MAX 3800

#ifndef TFT_BL
#define TFT_BL 21
#endif

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

// Forward declaration
class Screen;
class TraficManager;
class TraficClock;

struct Config;
struct ConfigFileHandler;
struct GlobalSettings;
struct Monitor;
struct ScreenEntity;
struct StopHit;
struct StopIndexInfo;
struct DisplayPrefs;
struct StringDatase;

std::vector<Monitor> GetMonitorsFromJson(JsonDocument&);
std::vector<Monitor> GetFilteredMonitors(const std::vector<Monitor>&, const String&);
std::vector<String> GetSplittedStrings(String, char);
String FixJsonMistake(String);
String TitleCaseSingleWord(String);
String GetRandomString(int);
bool HttpGetJson(const String&, JsonDocument&, const char*, String&);
bool FetchDepartures(const String&, std::vector<Monitor>&);
template<typename T> std::vector<T> cyclicSubset(const std::vector<T>&, size_t, size_t);
void PrintSystemInfo();
void PrintDebugMonitor(const Monitor&);
void PrintDegbugMonitors(const Monitor&);
void PrintDegbugMonitors(const std::vector<Monitor>&);
void LogInfo(const __FlashStringHelper*, const String& = "");
void LogWarn(const __FlashStringHelper*, const String& = "");
void LogError(const __FlashStringHelper*, const String& = "");
void LogProfile(const Config&, int);
bool CreateTaskChecked(TaskFunction_t, const char*, uint32_t, UBaseType_t);
void ConfigureClock();
String GetClockText();
void DrawHeaderBar();
void BacklightBegin();
void BacklightSetPct(int);
void SetDisplayAwake(bool);
bool WakeDisplayIfNeeded();
void EnterDisplaySleepPowerSave();
void ExitDisplaySleepPowerSave();
void SleepUntilDisplayWakes();
void RememberWifiLink();
void RestartMdns();
String GetActiveStopDisplayName();
void ShowFetchingStopScreen(const Config&);
bool GetCachedTraffic(int, std::vector<Monitor>&);
void SaveTrafficCache(int, const std::vector<Monitor>&);
void ClearTrafficCache();
void ShowStatusScreen(const String&, const String& = "");
void DrawSetupScreen();
void ShowSetupScreen();
void CloseSetupScreen();
void DrawWifiPortalScreen();
int DrawQrCode(const String&, int, int, int);
bool StartRuntimeConfigPortal();
void ResetActionsTask(void*);
void ScreenUpdateTask(void*);
void UpdateDataTask(void*);
void RuntimeConfigPortalTask(void*);
void TouchTask(void*);
void WebTask(void*);
void WiFiManagerTask(bool force_config_portal = false);
void ApplyProfileChange();

// Global variables
SemaphoreHandle_t dataMutex;
TFT_eSPI tft = TFT_eSPI();
Screen* p_screen = nullptr;
TraficManager* pTraficManager = nullptr;

// The XPT2046 touch controller lives on its own SPI bus on the CYD board. The
// library polls it over SPI; its IRQ line (pen-down, active low) is left to us
// so it can wake the chip from light sleep.
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS);

// Set by the touch handler when the active stop changes so UpdateDataTask can
// refetch immediately instead of waiting out its normal sleep interval.
volatile bool g_force_refresh = false;
String g_last_data_error;
bool g_status_screen_visible = false;
bool g_force_header_redraw = true;
volatile bool g_config_portal_open = false;
volatile bool g_display_awake = true;
volatile bool g_backlight_dimmed = false;
volatile unsigned long g_last_touch_ms = 0;
// Last request to the setup web app; keeps the display (and WiFi) awake while
// someone is configuring it from their phone.
volatile unsigned long g_last_web_ms = 0;
// Set when a touch woke the chip from light sleep, so that same touch is not
// also treated as a tap once the finger lifts.
volatile bool g_ignore_touch_until_release = false;
// The on-device setup screen (QR code + address) is showing.
volatile bool g_setup_screen_visible = false;
// Bumped on every stop switch/config change so an in-flight fetch for the old
// stop is not shown for the new one.
volatile uint32_t g_profile_generation = 0;
// Handshake between the web server task and the WiFi portal (both need port 80).
volatile bool g_web_server_stopped = true;
volatile bool g_request_wifi_portal = false;

// Survives a software reset: set before the scheduled maintenance reboot so the
// device comes back up with the display still off instead of lighting it up.
RTC_NOINIT_ATTR uint32_t g_quiet_boot_magic;
static constexpr uint32_t kQuietBootMagic = 0x51E3B007;


/**
 * @struct StringDatabase
 *
 * @brief Shared user-facing strings.
 */
struct StringDatabase {
  /**
     * @brief Name of the Wi-Fi hotspot the device opens for Wi-Fi setup.
     */
  static String GetWiFissid() {
    return "Wien Transport";
  }
};

struct Config {
  static const int cnt_min_lines = 1;
  // The CYD's 320x240 panel is taller than the original T-Display-S3, so it
  // comfortably fits an extra departure row.
  static const int cnt_max_lines = 4;
  static const int cnt_default_lines = 2;
  ///< Most platforms (RBL IDs) one stop profile may combine.
  static const int kMaxRblIds = 6;

  /**
     * @brief Default constructor.
     *
     * Sets the number of lines to the default of cnt_default_lines.
     */
  explicit Config()
    : cnt_lines(cnt_default_lines) {}

  /**
     * @brief Copy constructor.
     *
     * Copies all of the members of the other Config object to this one.
     *
     * @param other The Config object to copy from.
     */
  Config(const Config& other)
    : cnt_lines(other.cnt_lines),
      lines_filter(other.lines_filter),
      lines_rbl(other.lines_rbl),
      name(other.name),
      diva(other.diva),
      badges(other.badges) {}
  /**
     * @brief Assignment operator.
     *
     * Copies all of the members of the other Config object to this one.
     *
     * @param other The Config object to copy from.
     * @return A reference to this Config object.
     */
  Config& operator=(const Config& other) {
    if (this != &other) {
      lines_filter = other.lines_filter;
      lines_rbl = other.lines_rbl;
      cnt_lines = other.cnt_lines;
      name = other.name;
      diva = other.diva;
      badges = other.badges;
    }
    return *this;
  }

  /**
     * @brief Equality operator.
     *
     * Compares two Config objects for equality.
     *
     * @param other The Config object to compare to.
     * @return True if the two Config objects are equal, false otherwise.
     */
  bool operator==(const Config& other) const {
    return lines_filter == other.lines_filter
           && lines_rbl == other.lines_rbl
           && cnt_lines == other.cnt_lines
           && name == other.name
           && diva == other.diva
           && badges == other.badges;
  }
  /**
     * @brief Inequality operator.
     *
     * Compares two Config objects for inequality.
     *
     * @param other The Config object to compare to.
     * @return True if the two Config objects are not equal, false otherwise.
     */
  bool operator!=(const Config& other) const {
    return !(*this == other);
  }

  /**
     * @brief Sets the number of lines.
     *
     * Valid values are from cnt_min_lines to cnt_max_lines.
     * If an invalid value is passed, the default
     * value of cnt_default_lines will be used.
     *
     * @param count The number of lines to set.
     */
  void SetLinesCount(int c) {
    cnt_lines = verifyLinesCountInput(c);
  }
  /**
     * @brief Sets the number of lines.
     *
     * Valid values are from cnt_min_lines to cnt_max_lines.
     * If an invalid value is passed, the default
     * value of cnt_default_lines will be used.
     *
     * @param str The number of lines to set as a string.
     */
  void SetLinesCount(const String& c) {
    cnt_lines = verifyLinesCountInput(c.toInt());
  }

  /**
     * @brief Gets the number of lines as an integer.
     *
     * @return The number of lines.
     */
  int GetLinesCountAsInt() const {
    return cnt_lines;
  }
  /**
     * @brief Gets the number of lines as a string.
     *
     * @return The number of lines as a string.
     */
  const String GetLinesCountAsString() const {
    return String(cnt_lines);
  }

  /**
     * @brief Sets the lines filter.
     *
     * @param str The lines filter to set.
     */
  void SetLinesFilter(const String& str) {
    lines_filter = str;
    lines_filter.trim();
  }
  /**
     * @brief Gets the lines filter as a string.
     *
     * @return The lines filter as a string.
     */
  const String& GetLinesFilterAsString() const {
    return lines_filter;
  }

  /**
     * @brief Sets the stop's RBL / stop ID(s).
     *
     * One profile may combine several platforms (e.g. both directions of a
     * line), so this accepts a list separated by commas, spaces or semicolons
     * and stores it normalised as "4111,4118". Anything but digits is dropped.
     *
     * @param str The RBL list to set.
     */
  void SetLinesRBL(const String& str) {
    lines_rbl = "";
    int ids = 0;
    bool in_number = false;
    for (size_t i = 0; i < str.length(); ++i) {
      const char c = str[i];
      if (isdigit(static_cast<unsigned char>(c))) {
        if (!in_number) {
          if (ids == kMaxRblIds) {
            break;
          }
          if (ids > 0) {
            lines_rbl += ',';
          }
          ++ids;
          in_number = true;
        }
        lines_rbl += c;
      } else {
        in_number = false;
      }
    }
  }
  /**
     * @brief Sets the lines RBL.
     *
     * @param str The lines RBL to set as an integer.
     */
  void SetLinesRBL(int str) {
    lines_rbl = String(str, DEC);
  }
  /**
     * @brief Gets the lines RBL as a string.
     *
     * @return The lines RBL as a string.
     */
  const String& GetLinesRblAsString() const {
    return lines_rbl;
  }

  /**
     * @brief Sets the human-readable name of this stop profile.
     *
     * @param str The name to set (shown briefly when switching stops).
     */
  void SetName(const String& str) {
    name = str;
    name.trim();
  }
  /**
     * @brief Gets the human-readable name of this stop profile.
     *
     * @return The profile name as a string.
     */
  const String& GetName() const {
    return name;
  }

  /**
     * @brief Station (DIVA) this profile was picked from in the setup app.
     *
     * Only used by the web app to offer the direction picker again when the
     * stop is edited; the display itself works from the RBL list.
     */
  void SetDiva(const String& str) {
    diva = str;
    diva.trim();
  }
  const String& GetDiva() const {
    return diva;
  }

  /**
     * @brief Line names shown as badges in the setup app, e.g. "U1,13A".
     */
  void SetBadges(const String& str) {
    badges = str;
    badges.trim();
  }
  const String& GetBadges() const {
    return badges;
  }

  /**
     * @brief Whether this profile points at a real stop.
     *
     * A profile with an empty RBL/Stop ID is treated as an unused slot and is
     * skipped when cycling between stops.
     *
     * @return True if the profile has an RBL/Stop ID, false otherwise.
     */
  bool IsConfigured() const {
    return lines_rbl.length() > 0;
  }

private:
  /**
     * @brief Verifies that the lines count input is valid.
     *
     * Valid values are from cnt_min_lines to cnt_max_lines.
     * If an invalid value is passed, the default
     * value of cnt_default_lines will be used.
     *
     * @param count The lines count input.
     * @return The lines count input, if it is valid. Otherwise, the default
     * count.
     */
  static int verifyLinesCountInput(int count) {
    switch (count) {
      case cnt_min_lines ... cnt_max_lines:
        return count;
      default:
        return cnt_default_lines;
    }
  }

private:
  ///< The number of lines of text that can be displayed in each idx_row.
  int cnt_lines;

  ///< The raw lines filter. Example "D,2,6".
  String lines_filter;

  ///< The lines RBL.
  String lines_rbl;

  ///< Optional human-readable name for this stop. Example "Home".
  String name;

  ///< Station ID the stop was picked from (setup app metadata only).
  String diva;

  ///< Line names for the setup app's badges (metadata only).
  String badges;
};

////< Number of stop profiles the user can save and cycle between via touch.
static constexpr int kProfileCount = 20;

/**
 * @brief Display and battery preferences, editable from the setup web app.
 */
struct DisplayPrefs {
  ///< Backlight brightness while in use, in percent (10-100).
  int brightness_pct = 100;
  ///< Dim the backlight after this many seconds without touch (0 = never).
  int dim_after_s = 20;
  ///< Turn the display, WiFi and CPU off after this many idle seconds
  ///< (0 = never). This is by far the biggest battery saver.
  int off_after_s = 60;

  void Clamp() {
    brightness_pct = constrain(brightness_pct, 10, 100);
    dim_after_s = constrain(dim_after_s, 0, 3600);
    off_after_s = constrain(off_after_s, 0, 3600);
  }

  /**
     * @brief Backlight level used while dimmed (never brighter than normal).
     */
  int DimmedPct() const {
    return std::max(4, brightness_pct / 5);
  }

  bool operator==(const DisplayPrefs& other) const {
    return brightness_pct == other.brightness_pct
           && dim_after_s == other.dim_after_s
           && off_after_s == other.off_after_s;
  }
};

/**
 * @brief Reads a JSON value that older firmware stored as a string ("2") and
 * newer firmware stores as a number (2).
 */
int JsonToInt(JsonVariantConst value, int fallback) {
  if (value.is<int>()) {
    return value.as<int>();
  }
  if (value.is<const char*>()) {
    return String(value.as<const char*>()).toInt();
  }
  return fallback;
}

/**
 * @brief Class for handling configuration files.
 *
 * This class provides functions for saving and loading configuration files to
 * and from SPIFFS.
 */
struct ConfigFileHandler {
  /**
     * @brief Saves all stop profiles and display preferences to SPIFFS.
     *
     * @param profiles The list of stop profiles to save.
     * @param prefs The display/battery preferences to save.
     */
  static void SaveConfigFile(const std::vector<Config>& profiles,
                             const DisplayPrefs& prefs) {
    LogInfo(F("config-save-start"),
            "profiles=" + String(static_cast<int>(profiles.size())));
    JsonDocument json;
    JsonArray arr = json["profiles"].to<JsonArray>();
    for (const auto& cfg : profiles) {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = cfg.GetName();
      obj["lines_filter"] = cfg.GetLinesFilterAsString();
      obj["rbl_id"] = cfg.GetLinesRblAsString();
      obj["lines_count"] = cfg.GetLinesCountAsInt();
      if (cfg.GetDiva().length()) {
        obj["diva"] = cfg.GetDiva();
      }
      if (cfg.GetBadges().length()) {
        obj["badges"] = cfg.GetBadges();
      }
    }
    JsonObject display = json["display"].to<JsonObject>();
    display["brightness"] = prefs.brightness_pct;
    display["dim_after_s"] = prefs.dim_after_s;
    display["off_after_s"] = prefs.off_after_s;

    File file_config = SPIFFS.open(JSON_CONFIG_FILE, "w");
    if (!file_config) {
      LogError(F("config-open-write-failed"));
      return;
    }

#ifdef DEBUG_SERIAL_WIEN_MONITOR
    serializeJsonPretty(json, Serial);
#endif
    if (serializeJson(json, file_config) == 0) {
      LogError(F("config-write-failed"));
    } else {
      LogInfo(F("config-save-ok"));
    }
    file_config.close();
  }

  /**
     * @brief Loads all stop profiles and display preferences from SPIFFS.
     *
     * Accepts both the array format (`{"profiles":[...]}`) and the legacy
     * single-stop format so existing devices keep their settings after an
     * update.
     *
     * @param profiles The list to load profiles into (cleared on success).
     * @param prefs Filled from the file when present; left untouched otherwise.
     * @return True if at least one profile was loaded, false otherwise.
     */
  static bool LoadConfigFile(std::vector<Config>& profiles,
                             DisplayPrefs& prefs) {
    LogInfo(F("spiffs-mount-start"));

    if (SPIFFS.begin(false) || SPIFFS.begin(true)) {
      LogInfo(F("spiffs-mounted"));
      if (SPIFFS.exists(JSON_CONFIG_FILE)) {
        // file exists, reading and loading
        LogInfo(F("config-read-start"));
        File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
        if (configFile) {
          JsonDocument json;
          DeserializationError error = deserializeJson(json, configFile);
#ifdef DEBUG_SERIAL_WIEN_MONITOR
          serializeJsonPretty(json, Serial);
#endif
          configFile.close();
          if (!error) {
            profiles.clear();
            auto read_profile = [](JsonVariantConst obj) {
              Config cfg;
              cfg.SetName(obj["name"] | "");
              cfg.SetLinesFilter(obj["lines_filter"] | "");
              cfg.SetLinesRBL(obj["rbl_id"] | "");
              cfg.SetLinesCount(
                JsonToInt(obj["lines_count"], Config::cnt_default_lines));
              cfg.SetDiva(obj["diva"] | "");
              cfg.SetBadges(obj["badges"] | "");
              return cfg;
            };
            if (json["profiles"].is<JsonArray>()) {
              for (JsonVariantConst obj : json["profiles"].as<JsonArrayConst>()) {
                profiles.push_back(read_profile(obj));
              }
            } else if (!json["rbl_id"].isNull()) {
              // Legacy single-stop layout.
              profiles.push_back(read_profile(json.as<JsonVariantConst>()));
            }
            JsonVariantConst display = json["display"];
            if (!display.isNull()) {
              prefs.brightness_pct =
                JsonToInt(display["brightness"], prefs.brightness_pct);
              prefs.dim_after_s = JsonToInt(display["dim_after_s"], prefs.dim_after_s);
              prefs.off_after_s = JsonToInt(display["off_after_s"], prefs.off_after_s);
              prefs.Clamp();
            }
            LogInfo(F("config-read-ok"),
                    "profiles=" + String(static_cast<int>(profiles.size())));
            return !profiles.empty();
          } else {
            LogError(F("config-json-failed"), error.c_str());
          }
        }
        configFile.close();
      } else {
        LogWarn(F("config-missing"));
      }
    } else {
      LogError(F("spiffs-mount-failed"));
    }
    // end read
    return false;
  }

  /**
     * @brief Deletes the configuration file (and the cached stop list) from
     * SPIFFS.
     */
  static void DeleteConfigFile() {
    if (SPIFFS.begin()) {
      if (SPIFFS.exists(JSON_CONFIG_FILE)) {
        if (SPIFFS.remove(JSON_CONFIG_FILE)) {
          LogInfo(F("config-deleted"));
        } else {
          LogError(F("config-delete-failed"));
        }
      } else {
        LogWarn(F("config-delete-skipped"), String(F("file missing")));
      }
      SPIFFS.remove(STOPS_INDEX_FILE);
      SPIFFS.remove(LINES_INDEX_FILE);
      SPIFFS.end();
    } else {
      LogError(F("spiffs-mount-failed"));
    }
  }
};

struct GlobalSettings {
  ///< Number of lines of text that can be displayed on each idx_row of the
  ///< screen.
  static constexpr int cnt_screen_lines_per_rows = 2;

  ///< The number queue. Indicates how many countdown shows per cycle.
  static constexpr int cnt_shows_countdows = 2;

  ///< Number of seconds to press the soft reset button to restart the device.
  static constexpr int sec_press_soft_resset_button = 5;

  ///< Number of seconds to press the hard reset button to restart the device.
  static constexpr int sec_press_hard_resset_button = 30;

  ///< GPIO pin that the reset button is connected to.
  static constexpr int pin_reset_button = 0;

  ///< Number of milliseconds to delay before resetting the device if an error.
  static constexpr int ms_error_resset_delay = 10 * 1000;

  ///< Maintenance reboot interval. It only happens while the display is off,
  ///< and the device comes back with the display still off.
  static constexpr int ms_reboot_interval = 3 * 60 * 60 * 1000;

  ///< Number of milliseconds to delay between checking the reset button state.
  static constexpr int ms_task_delay_resset_button = 1000;

  ///< Number of milliseconds to delay between updating the data.
  static constexpr int ms_task_delay_data_update = 20 * 1000;

  ///< Frame delay while text is scrolling.
  static constexpr int ms_task_delay_screen_update = 10;

  ///< Frame delay while nothing on screen moves. Frames are skipped entirely
  ///< when nothing changed, so this only bounds how quickly changes show up.
  static constexpr int ms_task_delay_screen_idle = 50;

  ///< Hold a finger on the display this long to open the setup screen.
  static constexpr int ms_long_press = 1200;

  ///< CPU clock while awake. 160 MHz keeps TLS and drawing quick while using
  ///< noticeably less power than 240 MHz.
  static constexpr int mhz_cpu_awake = 160;

  ///< CPU clock while the display sleeps. The WiFi radio is powered down in
  ///< that state, so we can safely drop below the 80 MHz WiFi floor. Most of
  ///< that time is spent in light sleep anyway.
  static constexpr int mhz_cpu_asleep = 40;

  ///< real count down can be faster thet real ms_task_delay_data_update
  ///< this offset time help meke more smoth transition
  static constexpr int ms_additional_time_for_countdown = 50;

  ///< how much pixels scroll per frame
  static constexpr int px_scrool_per_frame = 2;

  ///< Height of the compact stop/time header bar at the top of the display.
  static constexpr int px_header_height = 22;

private:
  ///< The saved stop profiles (always kProfileCount entries; unused slots have
  ///< an empty RBL).
  std::vector<Config> profiles;

  ///< Display/battery preferences.
  DisplayPrefs prefs;

  ///< Index of the profile currently shown on screen.
  volatile int active_profile = 0;

  ///< Cached number of configured profiles, readable from any task without
  ///< touching the profile vector.
  volatile int configured_count = 0;

  ///< Whether or not the configuration file has been loaded.
  bool is_config_loaded = false;

  /**
     * @brief Loads profiles from flash once and pads the list to kProfileCount.
     */
  void EnsureLoaded() {
    if (is_config_loaded) {
      return;
    }
    ConfigFileHandler::LoadConfigFile(profiles, prefs);
    PadProfiles();
    // Start on the first profile that actually points at a stop.
    active_profile = FirstConfiguredIndex();
    is_config_loaded = true;
  }

  void PadProfiles() {
    while (static_cast<int>(profiles.size()) < kProfileCount) {
      profiles.push_back(Config());
    }
    if (profiles.size() > kProfileCount) {
      profiles.resize(kProfileCount);
    }
    int count = 0;
    for (const auto& cfg : profiles) {
      if (cfg.IsConfigured()) {
        ++count;
      }
    }
    configured_count = count;
  }

  /**
     * @brief Index of the first configured profile, or 0 if none are set.
     */
  int FirstConfiguredIndex() {
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
      if (profiles[i].IsConfigured()) {
        return i;
      }
    }
    return 0;
  }

public:
  /**
     * @brief Gets the configuration of the currently active stop profile.
     *
     * @return A reference to the active profile's configuration.
     */
  const Config& GetConfig() {
    EnsureLoaded();
    return profiles[active_profile];
  }

  /**
     * @brief Gets all saved stop profiles.
     *
     * @return A reference to the list of profiles.
     */
  const std::vector<Config>& GetProfiles() {
    EnsureLoaded();
    return profiles;
  }

  /**
     * @brief How many slots hold a configured stop. Safe to call from any task.
     *
     * @return The number of configured profiles.
     */
  int GetConfiguredCount() {
    EnsureLoaded();
    return configured_count;
  }

  /**
     * @brief Gets the active profile's 0-based index.
     */
  int GetActiveIndex() {
    EnsureLoaded();
    return active_profile;
  }

  /**
     * @brief Makes the given slot the active one, if it holds a stop.
     *
     * @return True if the active profile changed.
     */
  bool SetActiveIndex(int index) {
    EnsureLoaded();
    if (index < 0 || index >= static_cast<int>(profiles.size())
        || !profiles[index].IsConfigured() || index == active_profile) {
      return false;
    }
    active_profile = index;
    return true;
  }

  /**
     * @brief Advances the active profile to the next configured stop.
     *
     * @param direction +1 for the next stop, -1 for the previous one.
     * @return True if the active profile actually changed.
     */
  bool CycleProfile(int direction) {
    EnsureLoaded();
    if (GetConfiguredCount() <= 1) {
      return false;
    }
    int idx = active_profile;
    const int n = static_cast<int>(profiles.size());
    for (int step = 0; step < n; ++step) {
      idx = (idx + direction + n) % n;
      if (profiles[idx].IsConfigured()) {
        active_profile = idx;
        return true;
      }
    }
    return false;
  }

  /**
     * @brief Replaces all stop profiles and persists them to flash.
     *
     * @param new_profiles The new list of profiles.
     * @param preferred_active Slot to show afterwards; falls back to the first
     * configured stop when it is out of range or empty.
     */
  void SetProfiles(const std::vector<Config>& new_profiles,
                   int preferred_active = -1) {
    EnsureLoaded();
    profiles = new_profiles;
    PadProfiles();
    ConfigFileHandler::SaveConfigFile(profiles, prefs);
    if (preferred_active >= 0 && preferred_active < kProfileCount
        && profiles[preferred_active].IsConfigured()) {
      active_profile = preferred_active;
    } else {
      active_profile = FirstConfiguredIndex();
    }
  }

  /**
     * @brief Display/battery preferences (copied, so safe to read anywhere).
     */
  DisplayPrefs GetPrefs() {
    EnsureLoaded();
    return prefs;
  }

  /**
     * @brief Updates and persists the display/battery preferences.
     */
  void SetPrefs(DisplayPrefs new_prefs) {
    EnsureLoaded();
    new_prefs.Clamp();
    if (new_prefs == prefs) {
      return;
    }
    prefs = new_prefs;
    ConfigFileHandler::SaveConfigFile(profiles, prefs);
  }

  /**
     * @brief Deletes the configuration file.
     */
  void DeleteFile() {
    ConfigFileHandler::DeleteConfigFile();
  }
} global_settings;

void LogLine(const char* level, const __FlashStringHelper* event,
             const String& detail = "") {
#if ENABLE_SERIAL_LOGS
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.print(level);
  Serial.print(" ");
  Serial.print(event);
  if (detail.length()) {
    Serial.print(" | ");
    Serial.print(detail);
  }
  Serial.print(" | heap=");
  Serial.println(ESP.getFreeHeap());
#endif
}

void LogInfo(const __FlashStringHelper* event, const String& detail) {
  LogLine("INFO", event, detail);
}

void LogWarn(const __FlashStringHelper* event, const String& detail) {
  LogLine("WARN", event, detail);
}

void LogError(const __FlashStringHelper* event, const String& detail) {
  LogLine("ERROR", event, detail);
}

void LogProfile(const Config& cfg, int index) {
#if ENABLE_SERIAL_LOGS
  String detail = "slot=" + String(index + 1);
  detail += " name='" + cfg.GetName() + "'";
  detail += " rbl=" + cfg.GetLinesRblAsString();
  detail += " rows=" + cfg.GetLinesCountAsString();
  detail += " filter='" + cfg.GetLinesFilterAsString() + "'";
  LogInfo(F("profile"), detail);
#endif
}

bool CreateTaskChecked(TaskFunction_t task,
                       const char* name,
                       uint32_t stack_bytes,
                       UBaseType_t priority) {
  BaseType_t result =
    xTaskCreate(task, name, stack_bytes, NULL, priority, NULL);

  String detail = String(name) + " stack=" + String(stack_bytes)
                  + " priority=" + String(priority);
  if (result == pdPASS) {
    LogInfo(F("task-created"), detail);
    return true;
  }

  LogError(F("task-create-failed"), detail + " result=" + String(result));
  return false;
}

class SmartWatch {
public:
  SmartWatch(const char* function_name)
    : function_name(function_name), start_millis(millis()) {}

  unsigned long GetExecution_ms() {
    unsigned long end_millis = millis();
    return end_millis - start_millis;
  }
  ~SmartWatch() {
#ifdef DEBUG_SERIAL_WIEN_MONITOR
    Serial.print("Function '");
    Serial.print(function_name);
    Serial.print("' executed in ");
    Serial.print(GetExecution_ms());
    Serial.println(" milliseconds.");
#endif
  }

private:
  const char* function_name;
  unsigned long start_millis;
};

struct Monitor {
  String name;         // Line name 2, 72, D etc.
  String towards;      // Tram directions.
  String description;  // Line alarm
  std::vector<int> countdown;
};

struct CachedTraffic {
  bool valid = false;
  unsigned long fetched_ms = 0;
  std::vector<Monitor> monitors;
};

static constexpr unsigned long kTrafficCacheMaxAgeMs = 10UL * 60UL * 1000UL;
std::vector<CachedTraffic> g_traffic_cache(kProfileCount);

struct ScreenEntity {
  String right_txt;  // Line name 2, 72, D etc.
  std::vector<String> lines;
  String left_txt;
};

bool IsValidProfileIndex(int profile_index) {
  return profile_index >= 0 && profile_index < kProfileCount;
}

bool GetCachedTraffic(int profile_index, std::vector<Monitor>& out) {
  if (!IsValidProfileIndex(profile_index)) {
    return false;
  }

  const CachedTraffic& cache = g_traffic_cache[profile_index];
  if (!cache.valid || cache.monitors.empty()) {
    return false;
  }

  unsigned long age_ms = millis() - cache.fetched_ms;
  if (age_ms > kTrafficCacheMaxAgeMs) {
    LogWarn(F("cache-stale"), "slot=" + String(profile_index + 1));
    return false;
  }

  int age_minutes = static_cast<int>(age_ms / 60000UL);
  out = cache.monitors;
  if (age_minutes > 0) {
    for (auto& monitor : out) {
      for (auto& countdown : monitor.countdown) {
        countdown = std::max(0, countdown - age_minutes);
      }
    }
  }

  LogInfo(F("cache-hit"), "slot=" + String(profile_index + 1)
                            + " ageMin=" + String(age_minutes)
                            + " rows=" + String(out.size()));
  return true;
}

void SaveTrafficCache(int profile_index, const std::vector<Monitor>& monitors) {
  if (!IsValidProfileIndex(profile_index) || monitors.empty()) {
    return;
  }

  CachedTraffic& cache = g_traffic_cache[profile_index];
  cache.valid = true;
  cache.fetched_ms = millis();
  cache.monitors = monitors;
  LogInfo(F("cache-save"), "slot=" + String(profile_index + 1)
                            + " rows=" + String(monitors.size()));
}

void ClearTrafficCache() {
  for (auto& cache : g_traffic_cache) {
    cache.valid = false;
    cache.monitors.clear();
  }
  LogInfo(F("cache-clear"));
}


/**
 * @brief The `Screen` class represents a screen with multiple rows, each
 * displaying text and countdown information.
 *
 * This class is designed for use with TFT displays and provides methods for
 * setting and drawing text on each idx_row, as well as managing scrolling text
 * and drawing separator lines.
 */
class Screen {
public:
  /**
     * @brief Constructor to initialize a `Screen` object with the specified
     * number of rows and lines per idx_row.
     *
     * @param cnt_rows The number of rows on the screen.
     * @param cnt_lines_in_row The number of lines of text that can be displayed
     * in each idx_row.
     */
  Screen(int cnt_rows, int nLinesInRowCount)
    : px_max_width_name_text(0),
      px_max_width_countdown_text(0),
      px_separate_line_height(3),
      px_margin_lines(2),
      cnt_rows(cnt_rows),
      cnt_lines_in_row(nLinesInRowCount),
      px_margin(8),
      px_min_text_sprite(std::numeric_limits<int>::max()) {
    vec_scrolls_coords.resize(cnt_rows, std::vector<int>(nLinesInRowCount, 0));
    vec_init_scrolls_coords.resize(cnt_rows,
                                   std::vector<bool>(nLinesInRowCount, false));
  }


public:
  /**
     * @brief Sets the content for a specific idx_row on the screen and updates
     * its display.
     *
     * @param monitor The `Monitor` object containing the text and countdown
     * information for the idx_row.
     * @param idx_row The idx_row index to set the content for.
     */
  void SetRow(const ScreenEntity& monitor, int idx_row) {
    if (idx_row > cnt_rows - 1) {
      return;
    }
    const GFXfont* p_font = &FreeSansBold24pt7b;

    // Calculate max Sides text block size.
    int nameTextWidth_px = CalculateFontWidth_px(p_font, monitor.right_txt);
    SetMaxNameTextWidth_px(nameTextWidth_px);
    int countdownTextWidth_px = CalculateFontWidth_px(p_font, monitor.left_txt);
    SetMaxCountdownTextWidth_px(countdownTextWidth_px);

    // Draw Text;
    DrawName(monitor.right_txt, idx_row, p_font);
    DrawCountdown(monitor.left_txt, idx_row, p_font);
    DrawMiddleText(monitor.lines, idx_row);

    // Draw Separet Lines
    drawLines();
  }
  bool IsEnoughSpaceForMiddleText(const String& str) {
    const GFXfont* p_font = &FreeSansBold12pt7b;
    return GetMinTextSprite_px() >= CalculateFontWidth_px(p_font, str);
  }


  void PrintCordDebug() {
#ifdef DEBUG_SERIAL_WIEN_MONITOR
    // Print the contents of the vectors
    for (int i = 0; i < vec_scrolls_coords.size(); i++) {
      Serial.print("[");
      for (int j = 0; j < vec_scrolls_coords[i].size(); j++) {
        Serial.print(vec_scrolls_coords[i][j]);
        if (j != vec_scrolls_coords[i].size() - 1) {
          Serial.print(", ");
        }
      }
      Serial.print("]");
    }

    for (int i = 0; i < vec_init_scrolls_coords.size(); i++) {
      Serial.print("[");
      for (int j = 0; j < vec_init_scrolls_coords[i].size(); j++) {
        Serial.print(vec_init_scrolls_coords[i][j]);
        if (j != vec_init_scrolls_coords[i].size() - 1) {
          Serial.print(", ");
        }
      }
      Serial.print("]");
    }
    Serial.println("");
#endif
  }

  /**
     * @brief Converts a single German string to Latin alphabet.
     *
     * This function takes a single German string and converts it from German
     * characters (e.g., ä, Ä, ö, Ö, ü, Ü, ß) to their Latin alphabet equivalents
     * (a, A, o, O, u, U, s).
     *
     * @param input The German string to be converted.
     *
     * @return The converted string in Latin alphabet.
     */
  static String ConvertGermanToLatin(String input) {
    // TODO: optimize it.
    input.replace("ä", "a");
    input.replace("Ä", "A");
    input.replace("ö", "o");
    input.replace("Ö", "O");
    input.replace("ü", "u");
    input.replace("Ü", "U");
    input.replace("ß", "ss");
    return input;
  }


  int GetMinTextSprite_px() {
    return px_min_text_sprite;
  }

  /**
     * @brief Forces the next frame to be drawn even if nothing changed.
     */
  void Invalidate() {
    dirty_ = true;
  }

  /**
     * @brief Decides whether a frame with this content needs drawing.
     *
     * Drawing is skipped when the content is identical to the last frame and
     * nothing is scrolling, which keeps the CPU and SPI bus idle most of the
     * time.
     *
     * @param key A fingerprint of everything the frame shows.
     * @return True if the caller should draw the frame.
     */
  bool BeginFrame(const String& key) {
    if (!dirty_ && !animating_ && key == last_frame_key_) {
      return false;
    }
    last_frame_key_ = key;
    dirty_ = false;
    animating_ = false;  // set again by DrawMiddleText while text scrolls
    return true;
  }

  /**
     * @brief Whether the last drawn frame had moving (scrolling) text.
     */
  bool IsAnimating() const {
    return animating_;
  }

private:
  /**
     * @brief Converts a vector of German strings to Latin alphabet.
     *
     * Takes a vector of German strings and converts each string from
     * German characters (e.g., ä, Ä, ö, Ö, ü, Ü, ß) to their Latin alphabet
     * equivalents (a, A, o, O, u, U, s). The converted strings are then returned
     * in a new vector.
     *
     * @param vecGerman A vector of German strings to be converted.
     *
     * @return A vector of strings in Latin alphabet.
     */
  std::vector<String> ConvertGermanToLatin(
    const std::vector<String>& vecGerman) {
    std::vector<String> output;
    for (auto& str : vecGerman) {
      output.push_back(ConvertGermanToLatin(str));
      // Serial.println(ConvertGermanToLatin(str));
    }
    return output;
  }


private:
  int ContentTop_px() const {
    return global_settings.px_header_height;
  }

  int ContentHeight_px() const {
    return std::max(1, tft.height() - ContentTop_px());
  }

  int RowHeight_px() const {
    return std::max(1, ContentHeight_px() / cnt_rows);
  }

  /**
     * @brief Draws the countdown text on the screen for a specific idx_row.
     *
     * @param countdown The countdown text to be displayed.
     * @param idx_row The idx_row index where the countdown text should be drawn.
     * @param p_font The pointer to the p_font to be used for rendering the text.
     */
  void DrawCountdown(const String& countdown, int idx_row,
                     const GFXfont* pFont) const {
    int px_font_width = CalculateFontWidth_px(pFont, countdown);

    int px_dx_countdown_margin = tft.width() - px_font_width - px_margin;

    DrawTextOnSprite(countdown, idx_row, px_dx_countdown_margin, 0, pFont,
                     GetMaxCountdownTextWidth_px());
  }


  /**
     * @brief Draws the name text on the screen for a specific idx_row.
     *
     * @param name The name text to be displayed.
     * @param idx_row The idx_row index where the name text should be drawn.
     * @param p_font The pointer to the p_font to be used for rendering the text.
     */
  void DrawName(const String& name, int row, const GFXfont* pFont) const {
    int dxNameMargin_px = px_margin;
    DrawTextOnSprite(name, row, dxNameMargin_px, 0, pFont,
                     GetMaxNameTextWidth_px());
  }


  /**
     * @brief Sets and draws the middle lines and text content for a specific
     * idx_row on the screen.
     *
     * @param vec_text_lines The vector of text lines to be displayed in the
     * idx_row.
     * @param idx_row The idx_row index where the text content should be set and
     * drawn.
     */
  void DrawMiddleText(const std::vector<String>& vec_text_lines, int idx_row) {

    const GFXfont* p_font = &FreeSansBold12pt7b;

    // Calculate dimensions
    auto& vec_scroll_cord = vec_scrolls_coords[idx_row];
    auto& is_init_cords = vec_init_scrolls_coords[idx_row];
    int px_width_countdown = GetMaxCountdownTextWidth_px();
    int px_width_stopcode = GetMaxNameTextWidth_px();
    int px_height_font = CalculatefontHeight_px(p_font);
    // Minus two margins for name and two margins for countdown
    // 4 is count margin around Coundown and Stop code
    int px_width =
      tft.width() - px_width_countdown - px_width_stopcode - px_margin * 4;

    SetMinTextSprite_px(px_width);

    // Create and configure sprite
    TFT_eSprite sprite(&tft);
    sprite.createSprite(px_width, px_height_font);
    // sprite.setTextSize(0);

    int cnt_lines_actual = static_cast<int>(vec_text_lines.size());
    for (int i = 0; i < std::min(cnt_lines_in_row, cnt_lines_actual); ++i) {
      int px_height_full = RowHeight_px();
      int dy = CalculateLineDistance_px(px_height_full, px_margin_lines,
                                        px_height_font, cnt_lines_in_row, i);
      int px_full_string =
        CalculateFontWidth_px(p_font, vec_text_lines[i].c_str());
      const int px_scroll_before = vec_scroll_cord[i];

      // Reset to the right edge of the screen
      if (vec_scroll_cord[i] < (px_full_string * -1)) {
        vec_scroll_cord[i] = sprite.width();
      }


      if (px_full_string > px_width) {
        //scroll text 2 is mean px per frame
        vec_scroll_cord[i] -= global_settings.px_scrool_per_frame;
        if (!is_init_cords[i] && i > 0 && vec_text_lines[i].length()) {
          // Scroll if text is bigger than available space
          vec_scroll_cord[i] = sprite.width();
          is_init_cords[i] = true;
        }
      } else if (px_full_string < vec_scroll_cord[i]) {
        // reset sscrolls cord
        vec_scroll_cord[i] = 0;
        is_init_cords[i] = false;
      }

      if (vec_scroll_cord[i] != px_scroll_before) {
        animating_ = true;
      }

      // Draw text on the sprite
      sprite.setTextColor(TFT_TEXT);
      sprite.fillSprite(TFT_BG);
      sprite.setFreeFont(p_font);
      sprite.drawString(vec_text_lines[i].c_str(), vec_scroll_cord[i], 0);

      // Push sprite to display
      int x_cord = px_margin + px_margin + px_width_stopcode;
      int y_cord = ContentTop_px() + dy + (RowHeight_px() * idx_row);
      sprite.pushSprite(x_cord, y_cord);
    }
    sprite.deleteSprite();
  }


  /**
     * @brief Draws text on a sprite and pushes it to the screen at the specified
     * coordinates.
     *
     * @param text The text to be drawn on the sprite.
     * @param idx_row The idx_row index where the text should be displayed.
     * @param x The X-coordinate where the text should be drawn.
     * @param y The Y-coordinate where the text should be drawn.
     * @param p_font The pointer to the p_font to be used for rendering the text.
     */
  void DrawTextOnSprite(const String& text, int idx_row, int x, int y,
                        const GFXfont* p_font, int px_max_width) const {
    const int px_width_sprite = CalculateFontWidth_px(p_font, text);
    const int px_height_font = CalculatefontHeight_px(p_font);
    const int px_height_full = RowHeight_px();
    const int dy =
      CalculateLineDistance_px(px_height_full, 0, px_height_font, 1, 0);

    TFT_eSprite sprite(&tft);
    TFT_eSprite bg_left_sprite(&tft);
    TFT_eSprite bg_right_sprite(&tft);
    // TFT_eSprite bg_sprite(&tft);  // Separate sprite for squares

    // Set background colors based on DEBUG mode
    uint16_t color_left, color_middle, color_right;
    //#define DEBUG_DrawTextOnSprite
#ifdef DEBUG_DrawTextOnSprite
    leftColor = TFT_GREEN;
    middleColor = TFT_RED;
    rightColor = TFT_BLUE;
#else
    color_left = color_middle = color_right = TFT_BG;
#endif

    // Special symbols that draw manualy
    bool drawTopRightSquare = text == String("◱");
    bool drawBottomLeftSquare = text == String("◳");

    if (drawTopRightSquare || drawBottomLeftSquare) {
      sprite.createSprite(px_height_font, px_height_font);
      sprite.fillSprite(color_middle);
      int px_sqaure_size = (px_height_font / 2) - 6;
      if (drawTopRightSquare) {
        // down left
        sprite.fillRect(px_height_font / 2, 0, px_sqaure_size, px_sqaure_size,
                        TFT_YELLOW);
        // up right
        sprite.fillRect(0, px_height_font / 2, px_sqaure_size, px_sqaure_size,
                        TFT_YELLOW);
      } else if (drawBottomLeftSquare) {
        // left up
        sprite.fillRect(0, 0, px_sqaure_size, px_sqaure_size, TFT_YELLOW);
        // down right
        sprite.fillRect(px_height_font / 2, px_height_font / 2, px_sqaure_size,
                        px_sqaure_size, TFT_YELLOW);
      }

    } else {
      sprite.createSprite(px_width_sprite, px_height_font);
      sprite.fillSprite(color_middle);
      sprite.setTextColor(TFT_TEXT);
      sprite.setFreeFont(p_font);
      sprite.drawString(text, 0, 0);
    }
    // if (px_width_sprite < px_max_width) {
    //  Create and display left background sprite
    const int bg_width = std::max(0, px_max_width - px_width_sprite);
    if (bg_width > 0) {
      bg_left_sprite.createSprite(bg_width, px_height_font);
      bg_left_sprite.fillSprite(color_left);
      int bg_left_x_cord = x - bg_width;
      int bg_left_y_cord =
        ContentTop_px() + y + dy + (px_height_full * idx_row);
      bg_left_sprite.pushSprite(bg_left_x_cord, bg_left_y_cord);
      bg_left_sprite.deleteSprite();
    }

    // Create and display right background sprite
    const int bg_right_x = x + px_width_sprite;
    const int bg_right_width = bg_width;
    if (bg_right_width > 0) {
      bg_right_sprite.createSprite(bg_right_width, px_height_font);
      bg_right_sprite.fillSprite(color_right);
      bg_right_sprite.pushSprite(
        bg_right_x, ContentTop_px() + y + dy + (px_height_full * idx_row));
      bg_right_sprite.deleteSprite();
    }
    //}

    // Create and display text sprite
    /*sprite.createSprite(px_width_sprite, px_height_font);
            sprite.fillSprite(color_middle);
            sprite.setTextColor(TFT_TEXT);
            sprite.setFreeFont(p_font);
            sprite.drawString(text, 0, 0);*/
    sprite.pushSprite(x, ContentTop_px() + y + dy + (px_height_full * idx_row));
    sprite.deleteSprite();
  }


  /**
     * @brief Draws separator lines between rows on the screen.
     */
  void drawLines() const {
    // Create a TFT_eSprite for drawing separator lines
    TFT_eSprite lines_separator(&tft);
    // Create a N-pixel tall sprite
    lines_separator.createSprite(tft.width(), px_separate_line_height);

    for (int i = 1; i < cnt_rows; ++i) {
      int px_height_and_sprite =
        (ContentHeight_px() - (px_separate_line_height * i));
      int y_cord = ContentTop_px() + ((px_height_and_sprite / cnt_rows) * i);
      // Clear the sprite and draw a horizontal line
      lines_separator.fillSprite(TFT_BLACK);
      lines_separator.drawFastHLine(0, 0, tft.width(), TFT_BLACK);

      // Push the sprite to display at the specified Y-coordinate
      lines_separator.pushSprite(0, y_cord);
    }
    // Delete the sprite to free up memory
    lines_separator.deleteSprite();
  }


  /**
     * @brief Sets the maximum px_width of the name text in pixels.
     *
     * @param px_w Width of the name text in pixels to compare with the current
     * maximum.
     */
  void SetMaxNameTextWidth_px(int px_w) {
    if (px_w > px_max_width_name_text) {
      px_max_width_name_text = px_w;
      ClearContentForRelayout();
    }
  }


  /**
     * @brief Sets the maximum px_width of the countdown text in pixels.
     *
     * @param px_w The px_width of the countdown text in pixels to compare with
     * the current maximum.
     */
  void SetMaxCountdownTextWidth_px(int px_w) {
    if (px_w > px_max_width_countdown_text) {
      px_max_width_countdown_text = px_w;
      ClearContentForRelayout();
    }
  }


  /**
     * @brief Clears the row area after the column widths grew. Rows already
     * drawn this frame are wiped too, so the next frame must redraw.
     */
  void ClearContentForRelayout() {
    tft.fillRect(0, ContentTop_px(), tft.width(), ContentHeight_px(), TFT_BG);
    dirty_ = true;
  }

  /**
     * @brief Gets the maximum px_width of the countdown text in pixels.
     *
     * @return The maximum px_width of the countdown text in pixels.
     */
  int GetMaxCountdownTextWidth_px() const {
    return px_max_width_countdown_text;
  }


  /**
     * @brief Gets the maximum px_width of the name text in pixels.
     *
     * @return The maximum px_width of the name text in pixels.
     */
  int GetMaxNameTextWidth_px() const {
    return px_max_width_name_text;
  }


  /**
     * @brief Calculates the coordinate position of a line based on the specified
     * parameters.
     *
     * @param bodyLength The total length of the body where lines are positioned.
     * @param segmentMargin The margin between segments.
     * @param segmentLength The length of each segment.
     * @param segmentCount The total number of segments.
     * @param n The index of the line.
     *
     * @return The Coordinate position of the line.
     */
  int CalculateLineDistance_px(double bodyLength, double segmentMargin,
                               double segmentLength, int segmentCount,
                               int n) const {
    // Calculate the length occupied by segments
    double segmentsLength = segmentCount * segmentLength;

    // Calculate the length occupied by margins
    double marginsLength = (segmentCount - 1) * segmentMargin;

    // Add the lengths to get the total
    double totalSegmentsLength = segmentsLength + marginsLength;

    // Calculate the distance from the beginning of the body to the first line
    double x_firstLine = (bodyLength - totalSegmentsLength) / 2;

    // Calculate the final position based on the first line and segment spacing
    double finalPosition = x_firstLine + (n * (segmentLength + segmentMargin));

    // Convert the final position to an integer and return it
    return static_cast<int>(finalPosition);
  }


  /**
     * @brief Calculates the px_width of a text string using the specified p_font.
     *
     * @param p_font The p_font used for rendering the text.
     * @param str The text string to calculate the px_width for.
     *
     * @return The px_width of the text string in pixels.
     */
  int CalculateFontWidth_px(const GFXfont* p_font, const String& str) const {
    // This symbol not exist in p_font this squares draw manuany
    // In squeres Width == Height
    if (str == String("◱") || str == String("◳")) {
      return CalculatefontHeight_px(p_font);  // height == px_width in square
    }

    TFT_eSprite Calculator = TFT_eSprite(&tft);
    Calculator.setFreeFont(p_font);
    int px_w = Calculator.textWidth(str);
    Calculator.deleteSprite();
    return px_w;
  }


  /**
     * @brief Calculates the height of the text when rendered using the specified
     * p_font.
     *
     * This function creates a temporary sprite, sets the provided p_font, and
     * then retrieves the p_font height.
     *
     * @param p_font The p_font used for rendering the text.
     *
     * @return The height of the text when rendered with the specified p_font in
     * pixels.
     */
  int CalculatefontHeight_px(const GFXfont* p_font) const {
    // Getter V1
    // TFT_eSprite Calculator = TFT_eSprite(&tft);
    // Calculator.createSprite(tft.px_width() * 8, tft.height());
    // Calculator.setFreeFont(p_font);
    // return Calculator.px_height_font();

    // Getter V2
    // Check if the height has already been calculated for this p_font
    auto it = font_height_cache.find(p_font);
    if (it != font_height_cache.end()) {
      //   If yes, return the stored value
      return it->second;
    }

    const char* t =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789";
    TFT_eSprite Calculator = TFT_eSprite(&tft);
    Calculator.setFreeFont(p_font);

    uint16_t len = strlen(t);
    uint16_t maxH = 0;

    for (uint16_t i = 0; i < len; i++) {
      uint16_t unicode = t[i];
      uint16_t gNum = unicode - p_font->first;
      if (gNum >= p_font->last) continue;  // Skip undefined characters

      GFXglyph* glyph = &(((GFXglyph*)p_font->glyph))[gNum];
      uint16_t h = glyph->height;

      if (h > maxH) maxH = h;
    }

    int height = maxH ? maxH + 1 : 0;

    // Save the calculated value in the cache
    font_height_cache[p_font] = height;

    return height;
  }


  void SetMinTextSprite_px(int px_min) {
    // Serial.println(px_min_text_sprite);
    px_min_text_sprite = min(px_min_text_sprite, px_min);
  }


  ///< The maximum px_width of the name text in pixels.
  int px_max_width_name_text;

  ///< The maximum px_width of the countdown text in pixels.
  int px_max_width_countdown_text;

  ///< The px_width bewwen tables.
  int px_separate_line_height;

  ///< The px_width bewwen tables.
  int px_margin_lines;

  ///< The number of rows on the screen.
  int cnt_rows;

  ///< The number of lines of text that can be displayed in each idx_row.
  int cnt_lines_in_row;

  ///< The margin value for text and components.
  const int px_margin;

  ///< Coordinates for text scrolling in each idx_row.
  std::vector<std::vector<int>> vec_scrolls_coords;
  std::vector<std::vector<bool>> vec_init_scrolls_coords;

  int px_min_text_sprite;
  mutable std::map<const GFXfont*, int> font_height_cache;

  ///< Frame skipping state (see BeginFrame).
  bool dirty_ = true;
  bool animating_ = false;
  String last_frame_key_;

public:
  void FullResetScroll() {
    dirty_ = true;
    for (size_t i = 0; i < vec_scrolls_coords.size(); ++i) {
      for (size_t j = 0; j < vec_scrolls_coords[i].size(); ++j) {
        vec_scrolls_coords[i][j] = 0;
      }
    }

    for (size_t i = 0; i < vec_init_scrolls_coords.size(); ++i) {
      for (size_t j = 0; j < vec_init_scrolls_coords[i].size(); ++j) {
        vec_init_scrolls_coords[i][j] = false;
      }
    }
  }


  void SelectiveResetScroll(const std::vector<bool>& isNeedReset) {
    dirty_ = true;
    // printf("\n\n%d %d\n\n ", vec_init_scrolls_coords.size(),
    // isNeedReset.size());
    for (size_t i = 0; i < vec_scrolls_coords.size(); ++i) {
      for (size_t j = 0; j < vec_scrolls_coords[i].size(); ++j) {
        if (isNeedReset[i]) {
          vec_scrolls_coords[i][j] = 0;
        }
      }
    }

    for (size_t i = 0; i < vec_init_scrolls_coords.size(); ++i) {
      // printf("%d ", isNeedReset[i] ? 1 : 0);
      for (size_t j = 0; j < vec_init_scrolls_coords[i].size(); ++j) {
        if (isNeedReset[i]) {
          vec_init_scrolls_coords[i][j] = false;
        }
      }
    }
    // printf("\n\n");
  }
};

void ConfigureClock() {
  const char* vienna_tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  configTzTime(vienna_tz, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", vienna_tz, 1);
  tzset();

  for (int i = 0; i < 10; ++i) {
    if (time(nullptr) > 1600000000) {
      LogInfo(F("clock-synced"), GetClockText());
      return;
    }
    delay(200);
  }
  LogWarn(F("clock-not-synced"));
}

String GetClockText() {
  time_t now = time(nullptr);
  if (now <= 1600000000) {
    return "--:--";
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[6];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
  return String(buffer);
}

void DrawHeaderBar() {
  const int h = global_settings.px_header_height;
  const int pad_x = 6;
  const uint16_t bg = tft.color565(8, 18, 18);
  const uint16_t fg = tft.color565(210, 238, 223);
  const uint16_t accent = tft.color565(64, 158, 141);

  const Config& cfg = global_settings.GetConfig();
  String stop_name;
  if (cfg.GetName().length()) {
    stop_name = Screen::ConvertGermanToLatin(cfg.GetName());
  } else {
    stop_name = String("Stop ") + String(global_settings.GetActiveIndex() + 1);
  }
  String clock = GetClockText();
  String header_key = stop_name + "|" + clock;

  // Only redraw when the text changes (the clock ticks once a minute).
  static String last_header_key;
  if (!g_force_header_redraw && header_key == last_header_key) {
    return;
  }
  g_force_header_redraw = false;
  last_header_key = header_key;

  TFT_eSprite header(&tft);
  header.createSprite(tft.width(), h);
  header.fillSprite(bg);
  header.drawFastHLine(0, h - 1, tft.width(), accent);
  header.setTextFont(2);
  header.setTextColor(fg, bg);

  int clock_width = header.textWidth(clock);
  int max_name_width = tft.width() - clock_width - (pad_x * 3);
  while (stop_name.length() > 1 && header.textWidth(stop_name) > max_name_width) {
    stop_name.remove(stop_name.length() - 1);
  }

  header.setTextDatum(ML_DATUM);
  header.drawString(stop_name, pad_x, h / 2);
  header.setTextDatum(MR_DATUM);
  header.drawString(clock, tft.width() - pad_x, h / 2);
  header.pushSprite(0, 0);
  header.deleteSprite();
}

String GetActiveStopDisplayName() {
  const Config& cfg = global_settings.GetConfig();
  if (cfg.GetName().length()) {
    return Screen::ConvertGermanToLatin(cfg.GetName());
  }
  return String("Stop ") + String(global_settings.GetActiveIndex() + 1);
}

void ShowStatusScreen(const String& title, const String& subtitle) {
  g_status_screen_visible = true;
  g_force_header_redraw = true;
  tft.fillScreen(TFT_BG);
  tft.setTextColor(TFT_TEXT, TFT_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString(title, tft.width() / 2, (tft.height() / 2) - 18);

  if (subtitle.length()) {
    tft.setTextFont(2);
    tft.drawString(subtitle, tft.width() / 2, (tft.height() / 2) + 16);
  }

  tft.setTextDatum(TL_DATUM);
  DrawHeaderBar();
}

void ShowFetchingStopScreen(const Config& cfg) {
  String title = cfg.GetName().length()
                   ? Screen::ConvertGermanToLatin(cfg.GetName())
                   : String("Stop ") + String(global_settings.GetActiveIndex() + 1);
  ShowStatusScreen(title);
}



class TraficClock {
public:
  TraficClock(long ms_perCountdown, long cd_perIterations, long it_perHour)
    : kMillisecondsPerCountdown(ms_perCountdown),
      kCountdownsPerIteration(cd_perIterations),
      kIterationsPerHour(it_perHour) {
    Reset();
  }

private:
  const unsigned long kMillisecondsPerCountdown = 5000;
  const long kCountdownsPerIteration = 2;
  const long kIterationsPerHour = 2;
  unsigned long start_time_;


  unsigned long Milliseconds() const {
    return millis();
  }


  long GetTotalCountdown() const {
    unsigned long totalMilliseconds = Milliseconds();
    return totalMilliseconds / kMillisecondsPerCountdown;
  }


  long GetTotalIteration() const {
    return GetTotalCountdown() / kCountdownsPerIteration;
  }


public:
  void Reset() {
    start_time_ = millis();
  }


  long GetCountdown() const {
    long seconds = GetTotalCountdown() % kCountdownsPerIteration;
    return seconds;
  }


  long GetIteration() const {
    long iterations = GetTotalIteration() % kIterationsPerHour;
    return iterations;
  }


  long GetFullCycle() const {
    long cycle = GetTotalIteration() / kIterationsPerHour;
    return cycle;
  }


  void PrintTime() const {
#if 1
    Serial.print("Time: ");
    Serial.print(GetFullCycle());
    Serial.print(":");
    Serial.print(GetIteration());
    Serial.print(":");
    Serial.print(GetCountdown());
    Serial.print(".");
    Serial.println(Milliseconds());
#endif
  }
};

class TraficManager {
public:
  TraficManager()
    : shift_cnt(0), coundown_idx(0), p_trafic_clock(nullptr) {}


  ~TraficManager() {
    delete p_trafic_clock;
  }


  // Block 1: Update Traffic Data
  void update(const std::vector<Monitor>& vec) {
    //shift_cnt = 0;
    prev_iterations = 0;
    //SmartWatch sm(__FUNCTION__);
    bool had_data = HasData();

    if (vec.empty()) {
      all_trafic_set.clear();
      shift_cnt = 0;
      coundown_idx = 0;
      delete p_trafic_clock;
      p_trafic_clock = nullptr;
      if (p_screen) {
        p_screen->FullResetScroll();
      }
      return;
    }

    if (p_trafic_clock) {
      p_trafic_clock->Reset();
      const int& cnt_lines = global_settings.GetConfig().GetLinesCountAsInt();
      const int trafic_set_size = static_cast<int>(all_trafic_set.size());
      int rows_in_screen_cnt = std::min(cnt_lines, trafic_set_size);
      auto currentTraficSubset =
        cyclicSubset(all_trafic_set, rows_in_screen_cnt, shift_cnt);
      auto futureTraficSubset =
        cyclicSubset(vec, rows_in_screen_cnt, shift_cnt);

      sortTrafic(currentTraficSubset);
      sortTrafic(futureTraficSubset);

#ifdef DEBUG_SERIAL_WIEN_MONITOR

      Serial.println("updater_");
#endif
      SelectiveReset(currentTraficSubset, futureTraficSubset);
    }
    all_trafic_set = vec;
    if (!had_data || g_status_screen_visible) {
      ClearContentArea();
    }
  }


  /**
   * @brief Updates the screen with traffic information.
   */
  bool HasData() const {
    return !all_trafic_set.empty();
  }


  void ClearContentArea() const {
    tft.fillRect(0, global_settings.px_header_height, tft.width(),
                 tft.height() - global_settings.px_header_height, TFT_BG);
    g_status_screen_visible = false;
    if (p_screen) {
      p_screen->FullResetScroll();
    }
  }


  /**
   * @brief Updates the screen with traffic information.
   */
  void updateScreen() {
    if (all_trafic_set.empty()) {
      return;
    }

    const int& cnt_lines = global_settings.GetConfig().GetLinesCountAsInt();
    const int& cnt_countdows = global_settings.cnt_shows_countdows;
    const int& ms_additional = global_settings.ms_additional_time_for_countdown;
    const int& ms_full_cycle = global_settings.ms_task_delay_data_update;

    const int trafic_set_size = static_cast<int>(all_trafic_set.size());
    int rows_in_screen_cnt = std::min(cnt_lines, trafic_set_size);
    auto currentTraficSubset =
      cyclicSubset(all_trafic_set, rows_in_screen_cnt, shift_cnt);

    sortTrafic(currentTraficSubset);

    if (!p_trafic_clock) {
      double f_size = static_cast<double>(all_trafic_set.size());
      double f_sceen_cells = static_cast<double>(cnt_lines);

      long iterations_cnt = static_cast<long>(ceil(f_size / f_sceen_cells));

      long iteration_ms = ms_full_cycle / iterations_cnt;
      long countdown_ms = iteration_ms / cnt_countdows;
      p_trafic_clock = new TraficClock(countdown_ms + ms_additional,
                                       cnt_countdows, iterations_cnt);
      return;
    }
    //p_trafic_clock->PrintTime();
    if (p_trafic_clock) {
      // p_trafic_clock->PrintTime();
      long cur_iterations = p_trafic_clock->GetIteration();
      coundown_idx = p_trafic_clock->GetCountdown();
      if (cur_iterations != prev_iterations) {
        prev_iterations = cur_iterations;

        auto futureSubset = cyclicSubset(all_trafic_set, rows_in_screen_cnt,
                                         shift_cnt + cnt_lines);

        sortTrafic(futureSubset);
#ifdef DEBUG_SERIAL_WIEN_MONITOR
        Serial.println("inner_");
#endif
        SelectiveReset(currentTraficSubset, futureSubset);
        shift_cnt += global_settings.GetConfig().GetLinesCountAsInt();
      }
    }

    DrawTraficOnScreen(currentTraficSubset);
  }


  /**
   * @brief Resets the scrolling for monitors that have different descriptions.
   * @param currentTrafficSubset The current subset of traffic monitors.
   * @param futureSubset The future subset of traffic monitors.
   */
  void SelectiveReset(const std::vector<Monitor>& currentTraficSubset,
                      const std::vector<Monitor>& futureSubset) {
    // p_screen->PrintCordDebug();
    if (currentTraficSubset.size() == futureSubset.size()) {
      //PrintDegbugMonitors(currentTraficSubset);
      //PrintDegbugMonitors(futureSubset);

      size_t size = futureSubset.size();
      std::vector<bool> isNeedReset(size, true);
      for (size_t i = 0; i < size; ++i) {
        if (currentTraficSubset[i].description.isEmpty()
            || futureSubset[i].description.isEmpty()) {
          isNeedReset[i] = true;
        } else if (currentTraficSubset[i].description
                   == futureSubset[i].description) {
          isNeedReset[i] = false;
        }
      }

      // update only for difrent names
      if (currentTraficSubset.size() > 1) {
#ifdef DEBUG_SERIAL_WIEN_MONITOR
        for (size_t k = 0; k < isNeedReset.size(); k++) {
          Serial.print(isNeedReset[k]);
          Serial.print(" ");
        }
        Serial.println("");
#endif
        p_screen->SelectiveResetScroll(isNeedReset);
      }
    }

    // p_screen->PrintCordDebug();
  }


  /**
   * @brief Sorts traffic monitors based on countdown values.
   * @param v The vector of traffic monitors to be sorted.
   */
  void sortTrafic(std::vector<Monitor>& v) {
    std::sort(v.begin(), v.end(), [](const Monitor& a, const Monitor& b) {
      return a.countdown < b.countdown;
    });
  }


  /**
   * @brief Returns a valid countdown string at the given index.
   * @param c The vector of countdown values.
   * @param index The index to retrieve the countdown value from.
   * @return A valid countdown string.
   */
  String GetValidCountdown(const std::vector<int>& c, size_t index) {
    if (c.empty()) {
      return String();
    }

    size_t best_index = std::min(index, c.size() - 1);
    return String(c[best_index], DEC);
  }


  /**
   * @brief Draws traffic information on the screen using the provided data.
   * @param currentTrafficSubset A vector of Monitor objects representing the
   *                            current traffic data to display.
   */
  void DrawTraficOnScreen(const std::vector<Monitor>& currentTrafficSubset) {
    // Get the number of lines to display
    size_t numLines = global_settings.GetConfig().GetLinesCountAsInt();

    // Iterate through each line on the screen
    std::vector<ScreenEntity> vec_screen_entity;
    // generate entity
    for (size_t i = 0; i < numLines; ++i) {
      ScreenEntity monitor;
      std::vector<String> clean_str;
      for (size_t x = 0; x < global_settings.cnt_screen_lines_per_rows; ++x) {
        clean_str.push_back("");
      }
      //std::vector<String>(global_settings.cnt_screen_lines_per_rows, "");

      // Check if there's at least one monitor in the subset
      if (!currentTrafficSubset.empty()) {
        size_t monior_idx = i % currentTrafficSubset.size();
        // dont heve cyrcular arry
        if (i > (currentTrafficSubset.size() - 1)
            && currentTrafficSubset.size() > 1) {
          break;
        }

        const Monitor& currentMonitor = currentTrafficSubset[monior_idx];
        if (currentMonitor.countdown.empty()) {
          continue;
        }
        if (i >= currentMonitor.countdown.size()
            && currentTrafficSubset.size() == 1) {
          // break if this cyclig getting
          break;
        }
        size_t idx = currentTrafficSubset.size() == 1 ? i : coundown_idx;
        if (currentTrafficSubset.size() == 1
            && idx >= currentMonitor.countdown.size()) {
          // break if this single line and countdown for this line cycle
          break;
        }
        idx = std::min(idx, currentMonitor.countdown.size() - 1);
        // Set the right text
        monitor.right_txt = currentMonitor.name;

        if (!currentMonitor.countdown.empty()) {
          if (currentMonitor.countdown[idx] == 0) {
            if ((millis() / 1000) % 2) {
              monitor.left_txt = String("◱");
            } else {
              monitor.left_txt = String("◳");
            }
          } else {
            monitor.left_txt = GetValidCountdown(currentMonitor.countdown, idx);
          }
        }
        // Serial.println("block 2");
        // Set lines for display

        auto splitted_string = getSplittedStringFromCache(currentMonitor.towards);
        size_t cnt_sub_rows = min(
          static_cast<size_t>(global_settings.cnt_screen_lines_per_rows),
          splitted_string.size());
        size_t cnt_char_splitted_text = 0;
        for (size_t j = 0; j < cnt_sub_rows; ++j) {
          clean_str[j] = splitted_string[j];
          cnt_char_splitted_text += splitted_string[j].length();
        }
        if (!currentMonitor.towards.isEmpty()) {
          if ((static_cast<float>(cnt_char_splitted_text)
                 / static_cast<float>(currentMonitor.towards.length())
               < 0.5)) {
            // this mean that bigger that half information lossed
            // not enought space
            // just get bigger word
            // not used but if want to slool just add towards
            String str_max = getMaximumPosibleSingleNoScrollWord(
              currentMonitor.towards);
            clean_str[0] = str_max;
            clean_str[1].clear();
          }
        }

        // TODO: oprimize it
        if (currentMonitor.description.length()) {
          clean_str[1] = currentMonitor.description;
        }
      }
      monitor.lines = clean_str;
      // Serial.println("block 3");
      //  Set the idx_row on the screen
      vec_screen_entity.push_back(monitor);
      //p_screen->SetRow(monitor, i);
      // p_screen->PrintCordDebug();

      // Serial.println("roww setted;");
    }
    // Skip the (expensive) redraw when the frame would look the same.
    String frame_key;
    for (const auto& entity : vec_screen_entity) {
      frame_key += entity.right_txt;
      frame_key += '';
      frame_key += entity.left_txt;
      for (const auto& line : entity.lines) {
        frame_key += '';
        frame_key += line;
      }
      frame_key += '';
    }
    if (!p_screen->BeginFrame(frame_key)) {
      return;
    }

    // render entity
    for (size_t i = 0; i < vec_screen_entity.size(); ++i) {
      p_screen->SetRow(vec_screen_entity[i], i);
    }
  }

  int last_min_size = -1;  // Initialize last_min_size to an invalid value
  std::map<String, std::vector<String>> SplittedStringCache;

  /**
   * @brief Retrieves or generates a vector of split strings from a cache.
   * @param key_string The key string used to retrieve or generate split strings.
   * @return A vector of split strings either from the cache or generated.
   */
  std::vector<String> getSplittedStringFromCache(const String& key_string) {
    int min_size = p_screen->GetMinTextSprite_px();

    if (last_min_size != min_size) {
      // Clear the cache if min_size has changed
      SplittedStringCache.clear();
      last_min_size = min_size;
    }

    auto cacheIt = SplittedStringCache.find(key_string);
    if (cacheIt != SplittedStringCache.end()) {
      // Found in the cache, return it
      //Serial.println("return from cache");
      return cacheIt->second;
    }

    // Generate and cache the result
    std::vector<String> generatedStrings = splitToString(key_string);
    SplittedStringCache[key_string] = generatedStrings;
    return generatedStrings;
  }
  /**
 * Extracts the maximum substring from 'str' that fits the display.
 *
 * @param str The input string to process.
 * @return The maximum substring that fits, or an empty string if too short.
 */
  String getMaximumPosibleSingleNoScrollWord(const String& str) {
    String currentWord = "";
    size_t strLen = str.length();
    // Preallocate memory to prevent dynamic resizing
    currentWord.reserve(strLen);

    for (size_t i = 0; i < strLen; i++) {
      if (p_screen->IsEnoughSpaceForMiddleText(str.substring(0, i))) {
        currentWord += str[i];
      } else {
        break;
      }
    }

    if (currentWord.length() >= 2) {
      // Remove the last character
      currentWord.remove(currentWord.length() - 2);
    } else {
      currentWord = "";  // Handle the case when the string is too short
    }

    return currentWord;
  }

  /**
     * @brief Splits a string into individual words and stores them in a vector.
     * @param str The input string to be split.
     * @param words A vector to store the individual words.
     */
  void splitStringToWords(const String& str, std::vector<String>& words) {
    // SmartWatch sm(__FUNCTION__);
    String currentWord = "";
    words.reserve(32);
    for (size_t i = 0; i < str.length(); i++) {
      currentWord += str[i];

      // If the current character is a space or a hyphen
      if (str[i] == ' ' || str[i] == '-') {
        // Add the current word to the words vector
        if (p_screen->IsEnoughSpaceForMiddleText(currentWord)) {
          words.push_back(currentWord);
        }
        currentWord = "";
      }
    }

    // Add the remaining word if it's not empty
    if (!currentWord.isEmpty()
        && p_screen->IsEnoughSpaceForMiddleText(currentWord)) {
      words.push_back(currentWord);
    }
  }

  /**
     * @brief Splits a string into subtexts that fit within one line.
     * @param input The input string to be split into subtexts.
     * @return A vector of subtexts that fit within one line.
     */
  std::vector<String> splitToString(const String& input) {
    // SmartWatch sm(__FUNCTION__);
    std::vector<String> words;
    splitStringToWords(input, words);

    // Create a list of subtexts
    std::vector<String> subtexts;
    String currentSubtext = "";

    // Add words to the subtexts list as long as they fit in one line
    for (size_t i = 0; i < words.size(); i++) {
      if (p_screen->IsEnoughSpaceForMiddleText(currentSubtext + " " + words[i])) {
        currentSubtext += words[i];
      } else {
        if (!currentSubtext.isEmpty()) {
          subtexts.push_back(currentSubtext);
        }
        currentSubtext = words[i];
      }
    }

    if (!currentSubtext.isEmpty()) {
      subtexts.push_back(currentSubtext);
    }

    for (size_t i = 0; i < subtexts.size(); i++) {
      if (!subtexts[i].isEmpty() && (subtexts.size() - 1 != i)) {
        subtexts[i] = subtexts[i].substring(0, subtexts[i].length() - 1);
      }
    }

    return subtexts;
  }

private:
  std::vector<Monitor> all_trafic_set;
  int shift_cnt;
  int coundown_idx;
  // unsigned long previousMillisTraficSet;
  // unsigned long previousMillisCountDown;
  // const int INTERVAL_UPDATE = global_settings.ms_task_delay_data_update;
  TraficClock* p_trafic_clock;
  long prev_iterations = 0;
};

////////////////////////////Functions////////////////////////////////////////////


void PrintDebugMonitor(const Monitor& t) {
#ifdef DEBUG_SERIAL_WIEN_MONITOR
  Serial.println("==========");
  Serial.println("Name: " + t.name);

  // tft.setAddrWindow(10, 20, 100, 80);

  Serial.println("Description: " + t.description);
  Serial.print("Countdown: ");
  for (auto& c : t.countdown) {
    Serial.print(c);
    Serial.print(", ");
  }
  Serial.println();
  Serial.println("==========");
#endif
}


/**
 * @brief The API writes some destinations in capitals ("LEOPOLDAU"); turn a
 * single such word into "Leopoldau". Multi-word names are left alone.
 */
String TitleCaseSingleWord(String word) {
  word.trim();
  if (word.indexOf('-') == -1
      && word.indexOf(' ') == -1
      && word.length() > 1) {
    for (int i = 0; i < word.length(); i++) {
      word[i] = tolower(static_cast<unsigned char>(word[i]));
    }
    word[0] = toupper(static_cast<unsigned char>(word[0]));
  }
  return word;
}


String FixJsonMistake(String word) {
  word = Screen::ConvertGermanToLatin(word);
#if DEBUG_SERIAL_WIEN_MONITOR
  Serial.println(word);
#endif
  return TitleCaseSingleWord(word);
}


void PrintDegbugMonitors(const std::vector<Monitor>& monitors) {
  for (const auto& monitor : monitors) {
    PrintDegbugMonitors(monitor);
  }
}


// Only the fields the display needs are kept while parsing, which keeps the
// JSON document small no matter how busy the stop is.
static const char kMonitorFilterJson[] =
  "{\"data\":{"
  "\"monitors\":[{\"lines\":[{\"name\":true,\"towards\":true,"
  "\"departures\":{\"departure\":[{\"departureTime\":{\"countdown\":true},"
  "\"vehicle\":{\"name\":true,\"towards\":true}}]}}]}],"
  "\"trafficInfos\":[{\"description\":true,\"relatedLines\":true}]}}";


std::vector<Monitor> GetMonitorsFromJson(JsonDocument& root) {
  std::vector<Monitor> monitors_vec;

  JsonObject data = root["data"];
  if (data.isNull()) {
    g_last_data_error = "json-missing-data";
    LogWarn(F("json-missing-data"));
    return monitors_vec;
  }

  std::map<String, String> lines_to_description_map;
  for (JsonVariant traffic_info : data["trafficInfos"].as<JsonArray>()) {
    if (!traffic_info["description"].is<const char*>()) {
      continue;
    }
    const String description = Screen::ConvertGermanToLatin(
      traffic_info["description"].as<String>());
    for (JsonVariant related_line : traffic_info["relatedLines"].as<JsonArray>()) {
      lines_to_description_map[related_line.as<String>()] = description;
    }
  }

  auto add_departure = [&](JsonObject line, JsonVariant departureItem) {
    JsonVariant departureTime = departureItem["departureTime"];
    if (!departureTime["countdown"].is<int>()) {
      return;
    }

    Monitor cur_monitor;
    JsonVariant vehicle = departureItem["vehicle"];
    if (vehicle.is<JsonObject>()) {
      cur_monitor.name = vehicle["name"].as<String>();
      cur_monitor.towards = FixJsonMistake(vehicle["towards"].as<String>());
    } else {
      cur_monitor.name = line["name"].as<String>();
      cur_monitor.towards = FixJsonMistake(line["towards"].as<String>());
    }

    int countdown = departureTime["countdown"].as<int>();
    if (cur_monitor.name.isEmpty() || countdown < 0) {
      return;
    }

    cur_monitor.countdown.push_back(countdown);
    cur_monitor.description = lines_to_description_map[cur_monitor.name];

    auto it = std::find_if(monitors_vec.begin(), monitors_vec.end(),
                           [&cur_monitor](const Monitor& monitor) {
                             return monitor.name == cur_monitor.name
                                    && monitor.towards == cur_monitor.towards;
                           });

    if (it == monitors_vec.end()) {
      monitors_vec.push_back(cur_monitor);
    } else {
      it->countdown.push_back(countdown);
    }
  };

  for (JsonVariant monitor : data["monitors"].as<JsonArray>()) {
    for (JsonVariant lineVariant : monitor["lines"].as<JsonArray>()) {
      JsonObject line = lineVariant.as<JsonObject>();
      JsonVariant departure = line["departures"]["departure"];

      if (departure.is<JsonArray>()) {
        for (JsonVariant departureItem : departure.as<JsonArray>()) {
          add_departure(line, departureItem);
        }
      } else if (departure.is<JsonObject>()) {
        add_departure(line, departure);
      }
    }
  }

  LogInfo(F("json-parsed"), "monitors=" + String(monitors_vec.size()));
  return monitors_vec;
}


String GetRandomString(int maxLength) {
  // Define valid characters
  String validChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  // Get a random string length from 1 to maxLength
  int length = random(1, maxLength + 1);

  // Generate a random string
  String randomString = "";
  for (int i = 0; i < length; i++) {
    int randomIndex = random(validChars.length());
    randomString += validChars[randomIndex];
  }

  return randomString;
}


/**
 * @brief Splits a string by a specified separator character.
 * @param data The input string to be split.
 * @param separator The character used for splitting.
 * @return A vector of substrings resulting from the split operation.
 */
std::vector<String> GetSplittedStrings(String data, char separator) {
  std::vector<String> result;

  while (data.length()) {
    int separatorIndex = data.indexOf(separator);
    String chunk =
      separatorIndex == -1 ? data : data.substring(0, separatorIndex);
    chunk.trim();
    if (chunk.length()) {
      result.push_back(chunk);
    }
    if (separatorIndex == -1) {
      break;
    }
    data = data.substring(separatorIndex + 1);
  }

  return result;
}


template<typename T>
std::vector<T> cyclicSubset(const std::vector<T>& input,
                            size_t N,
                            size_t start) {
  std::vector<T> result;
  size_t size = input.size();

  // If the input vector is empty or N is 0, return an empty result.
  if (input.empty() || N == 0) {
    return result;
  }

  // Let's start with the start element and add elements to the result.
  for (size_t i = start; i < start + N; ++i) {
    result.push_back(input[i % size]);
  }

  return result;
}


/**
 * @brief Filters a vector of Monitor objects based on a filter string.
 * @param data The input vector of Monitor objects.
 * @param filter The filter string to match against Monitor names.
 * @return A vector containing Monitor objects that match the filter.
 */
std::vector<Monitor> GetFilteredMonitors(const std::vector<Monitor>& data,
                                         const String& filter) {
  if (filter.isEmpty()) {
    return data;
  }
  std::vector<String> custom_names = GetSplittedStrings(filter, ',');
  std::vector<Monitor> result;

  for (auto& monitor : data) {
    auto it = std::find(custom_names.begin(), custom_names.end(), monitor.name);
    if (it != custom_names.end()) {
      result.push_back(monitor);
    }
  }

  if (result.empty()) {
    LogWarn(F("filter-empty"),
            "filter='" + filter + "' fetched=" + String(data.size()));
  }

  return result;
}


void PrintDegbugMonitors(const Monitor& monitor) {
#ifdef DEBUG_SERIAL_WIEN_MONITOR
  Serial.print("Name: ");
  Serial.print(monitor.name);
  // Serial.print(" Towards: ");
  // Serial.print(monitor.towards);
  Serial.print(" Description: ");
  Serial.print(monitor.description);

  /*Serial.println("Countdown:");
    for (int i = 0; i < monitor.countdown.size(); i++) {
      Serial.print("  ");
      Serial.print(monitor.countdown[i]);
    }*/
  Serial.println();
#endif
}


// ---------------------------------------------------------------------------
// Power management.
//
// Awake:  PWM backlight at the chosen brightness, dimmed after a short idle
//         period, CPU at 160 MHz, frames only redrawn when something changed.
// Asleep: backlight off, ILI9341 in sleep mode, WiFi radio off and the chip in
//         light sleep. The XPT2046 pulls its IRQ line low on touch, which wakes
//         the chip; the BOOT button (reset gestures) wakes it too, and a slow
//         timer is kept as a fallback for boards without a wired touch IRQ.
// ---------------------------------------------------------------------------

// MIPI DCS commands understood by the ILI9341 (and most other TFT drivers).
static constexpr uint8_t kLcdSleepIn = 0x10;
static constexpr uint8_t kLcdSleepOut = 0x11;
static constexpr uint8_t kLcdDisplayOff = 0x28;
static constexpr uint8_t kLcdDisplayOn = 0x29;

static constexpr uint32_t kBacklightPwmHz = 5000;
static constexpr uint8_t kBacklightPwmBits = 8;
static constexpr uint8_t kBacklightPwmChannel = 0;  // ESP32 core 2.x only

///< Fallback wake interval while asleep, for boards whose touch IRQ line is
///< not wired. Each of these wakes lasts only a few tens of milliseconds.
static constexpr uint64_t kLightSleepHeartbeatUs = 2ULL * 1000ULL * 1000ULL;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define BACKLIGHT_PWM_TARGET TFT_BL
#else
#define BACKLIGHT_PWM_TARGET kBacklightPwmChannel
#endif

void BacklightBegin() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, kBacklightPwmHz, kBacklightPwmBits);
#else
  ledcSetup(kBacklightPwmChannel, kBacklightPwmHz, kBacklightPwmBits);
  ledcAttachPin(TFT_BL, kBacklightPwmChannel);
#endif
}

void BacklightSetPct(int pct) {
  const uint32_t max_duty = (1U << kBacklightPwmBits) - 1;
  uint32_t duty = (static_cast<uint32_t>(constrain(pct, 0, 100)) * max_duty + 50) / 100;
  if (TFT_BACKLIGHT_ON != HIGH) {
    duty = max_duty - duty;
  }
  ledcWrite(BACKLIGHT_PWM_TARGET, duty);
}

/**
 * @brief Switches the panel between normal operation and its sleep mode.
 *
 * The ILI9341 keeps its frame memory while asleep, so waking it shows the last
 * frame again without a full redraw.
 */
void SetDisplayAwake(bool awake) {
  const bool locked =
    dataMutex && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE;
  const bool changed = g_display_awake != awake;
  if (changed) {
    if (awake) {
      tft.writecommand(kLcdSleepOut);
      vTaskDelay(pdMS_TO_TICKS(120));  // required settle time after sleep-out
      tft.writecommand(kLcdDisplayOn);
      g_force_header_redraw = true;
      if (p_screen) {
        p_screen->Invalidate();
      }
    } else {
      BacklightSetPct(0);
      tft.writecommand(kLcdDisplayOff);
      tft.writecommand(kLcdSleepIn);
    }
    g_backlight_dimmed = false;
    g_display_awake = awake;
  }
  if (locked) {
    xSemaphoreGive(dataMutex);
  }

  if (!changed) {
    return;
  }
  if (awake) {
    // Give the screen task a moment to repaint before the light comes on, so
    // the stale frame never flashes up.
    vTaskDelay(pdMS_TO_TICKS(60));
    BacklightSetPct(global_settings.GetPrefs().brightness_pct);
    LogInfo(F("display-on"));
  } else {
    LogInfo(F("display-off"));
  }
}

/**
 * @brief Shows the cached departures (aged by the time spent asleep) right
 * away when the display wakes, instead of the stale frame from before.
 */
void RefreshTrafficFromCacheForWake() {
  if (!dataMutex || !pTraficManager
      || xSemaphoreTake(dataMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  if (!g_setup_screen_visible && global_settings.GetConfiguredCount() > 0) {
    std::vector<Monitor> cached;
    if (GetCachedTraffic(global_settings.GetActiveIndex(), cached)) {
      pTraficManager->update(cached);
    } else {
      pTraficManager->update(std::vector<Monitor>());
      ShowFetchingStopScreen(global_settings.GetConfig());
    }
  }
  xSemaphoreGive(dataMutex);
}

/**
 * @brief Registers user activity. Wakes the display, or brings a dimmed
 * backlight back to full brightness.
 *
 * @return True if the display was off or dimmed (the touch that caused this is
 * then "used up" and should not also switch stops).
 */
bool WakeDisplayIfNeeded() {
  g_last_touch_ms = millis();
  if (g_display_awake) {
    if (!g_backlight_dimmed) {
      return false;
    }
    g_backlight_dimmed = false;
    BacklightSetPct(global_settings.GetPrefs().brightness_pct);
    LogInfo(F("display-undim"));
    return true;
  }

  // Restore full CPU speed immediately so the wake feels instant. WiFi is
  // brought back by UpdateDataTask; raising the clock is always safe.
  setCpuFrequencyMhz(global_settings.mhz_cpu_awake);
  RefreshTrafficFromCacheForWake();
  SetDisplayAwake(true);
  return true;
}

// Last access point we were connected to, so waking up can skip the WiFi scan.
struct WifiLink {
  bool valid = false;
  String ssid;
  String psk;
  uint8_t bssid[6] = { 0 };
  int32_t channel = 0;
} g_wifi_link;

void RememberWifiLink() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  g_wifi_link.ssid = WiFi.SSID();
  g_wifi_link.psk = WiFi.psk();
  const uint8_t* bssid = WiFi.BSSID();
  if (bssid) {
    memcpy(g_wifi_link.bssid, bssid, sizeof(g_wifi_link.bssid));
  }
  g_wifi_link.channel = WiFi.channel();
  g_wifi_link.valid = g_wifi_link.ssid.length() > 0 && bssid != nullptr;
}

bool WaitForWifi(unsigned long timeout_ms) {
  const unsigned long start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_ms < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return WiFi.status() == WL_CONNECTED;
}

void RestartMdns() {
  MDNS.end();
  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  } else {
    LogWarn(F("mdns-failed"));
  }
}

// These run from UpdateDataTask - the only task that uses WiFi in the
// background - so the radio is never powered down from under an in-flight
// fetch.
void EnterDisplaySleepPowerSave() {
  LogInfo(F("powersave-enter"));
  MDNS.end();
  // Drop the link and power the radio off (keep the saved credentials).
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  // Radio is off, so we can safely fall below the WiFi-required 80 MHz.
  setCpuFrequencyMhz(global_settings.mhz_cpu_asleep);
}

void ExitDisplaySleepPowerSave() {
  // Back to full speed first so the reconnect and redraw are quick.
  setCpuFrequencyMhz(global_settings.mhz_cpu_awake);
  WiFi.mode(WIFI_STA);
  LogInfo(F("powersave-exit"), String(F("reconnecting wifi")));

  const char* psk = g_wifi_link.psk.length() ? g_wifi_link.psk.c_str() : nullptr;
  bool connected = false;
  if (g_wifi_link.valid) {
    // Fast path: go straight to the last access point and channel.
    WiFi.begin(g_wifi_link.ssid.c_str(), psk, g_wifi_link.channel,
               g_wifi_link.bssid);
    connected = WaitForWifi(4000);
    if (!connected) {
      LogWarn(F("wifi-fast-connect-failed"));
      WiFi.disconnect();
    }
  }
  if (!connected) {
    if (g_wifi_link.valid) {
      WiFi.begin(g_wifi_link.ssid.c_str(), psk);
    } else {
      // Credentials WiFiManager stored, so this never re-opens the portal.
      WiFi.begin();
    }
    connected = WaitForWifi(10000);
  }

  if (connected) {
    RememberWifiLink();
    RestartMdns();
    LogInfo(F("powersave-wifi-ok"), WiFi.localIP().toString());
  } else {
    LogWarn(F("powersave-wifi-timeout"));
  }
}

/**
 * @brief One light-sleep cycle while the display is off.
 *
 * Light sleep keeps RAM, tasks and timers intact, so everything simply resumes
 * where it was. A pen-down on the touch panel wakes the display directly.
 */
void LightSleepOnce() {
  const gpio_num_t touch_irq = static_cast<gpio_num_t>(XPT2046_IRQ);
  const gpio_num_t boot_button =
    static_cast<gpio_num_t>(global_settings.pin_reset_button);

  // A finger already on the panel, or the BOOT button held (reset gesture):
  // stay awake so TouchTask / ResetActionsTask can see it.
  if (digitalRead(XPT2046_IRQ) == LOW
      || digitalRead(global_settings.pin_reset_button) == LOW) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  gpio_wakeup_enable(touch_irq, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(boot_button, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(kLightSleepHeartbeatUs);
#if ENABLE_SERIAL_LOGS
  Serial.flush();
#endif
  esp_light_sleep_start();
  gpio_wakeup_disable(touch_irq);
  gpio_wakeup_disable(boot_button);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  // The IRQ line stays low while the pen is down; a few samples filter out
  // glitches.
  bool pen_down = false;
  for (int i = 0; i < 5 && !pen_down; ++i) {
    pen_down = digitalRead(XPT2046_IRQ) == LOW;
    if (!pen_down) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
  if (pen_down) {
    g_ignore_touch_until_release = true;
    WakeDisplayIfNeeded();
  } else {
    // Let TouchTask poll the panel once (fallback for boards without a wired
    // touch IRQ) and ResetActionsTask look at the BOOT button.
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

/**
 * @brief Parks the calling task (UpdateDataTask) in low-power mode until a
 * touch wakes the display.
 */
void SleepUntilDisplayWakes() {
  EnterDisplaySleepPowerSave();
  while (!g_display_awake) {
    LightSleepOnce();
  }
  ExitDisplaySleepPowerSave();
}


/**
 * @brief Fetches departures for the active stop in the background.
 *
 * While the display is off this task is also the one that powers WiFi down and
 * keeps the chip in light sleep (see SleepUntilDisplayWakes).
 *
 * @param pvParameters Pointer to task-specific parameters.
 */
void UpdateDataTask(void* pvParameters) {
  LogInfo(F("data-task-start"));
  const int delay_planned_ms = global_settings.ms_task_delay_data_update;

  while (true) {
    if (g_config_portal_open) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // While the display sleeps there is nothing on screen to keep fresh, so
    // park here in low-power mode. A touch wakes the display; we then restore
    // power and fall straight through to refetch the latest departures.
    if (!g_display_awake) {
      SleepUntilDisplayWakes();
      g_force_refresh = true;
    }

    // The setup screen owns the display (first run, or opened by long press).
    if (g_setup_screen_visible) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    // Clear the flag before fetching so a stop switch that happens *during*
    // the fetch still forces another immediate refetch afterwards.
    g_force_refresh = false;
    const unsigned long cycle_start_ms = millis();

    int profile_index = 0;
    uint32_t generation = 0;
    Config cfg;
    bool is_configured = true;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      profile_index = global_settings.GetActiveIndex();
      cfg = global_settings.GetConfig();
      generation = g_profile_generation;
      is_configured = cfg.IsConfigured();
      if (!is_configured) {
        DrawSetupScreen();
      } else if (!pTraficManager->HasData()) {
        pTraficManager->update(std::vector<Monitor>());
        ShowFetchingStopScreen(cfg);
      }
      xSemaphoreGive(dataMutex);
    }
    if (!is_configured) {
      continue;
    }

    LogInfo(F("data-cycle"), "rbl=" + cfg.GetLinesRblAsString()
                               + " slot=" + String(profile_index + 1)
                               + " filter='"
                               + cfg.GetLinesFilterAsString() + "'");

    std::vector<Monitor> fetchedTraffic;
    const bool fetch_ok =
      FetchDepartures(cfg.GetLinesRblAsString(), fetchedTraffic);
    const String& raw_filter = cfg.GetLinesFilterAsString();
    std::vector<Monitor> shownTraffic =
      GetFilteredMonitors(fetchedTraffic, raw_filter);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      if (generation != g_profile_generation || g_setup_screen_visible) {
        // The user switched stops (or opened setup) while we were fetching.
        LogInfo(F("data-skip-stale"), "slot=" + String(profile_index + 1));
        xSemaphoreGive(dataMutex);
        continue;
      }

      if (!shownTraffic.empty()) {
        SaveTrafficCache(profile_index, shownTraffic);
      }

      if (shownTraffic.empty() && pTraficManager->HasData()) {
        // Keep showing what we have rather than blanking on a hiccup.
        LogWarn(F("refresh-kept-cache"),
                g_last_data_error.length() ? g_last_data_error
                                           : String(F("empty refresh")));
      } else {
        pTraficManager->update(shownTraffic);
        if (shownTraffic.empty()) {
          if (g_last_data_error.startsWith("json-")) {
            ShowStatusScreen("Data parse failed", g_last_data_error);
          } else if (!fetch_ok) {
            ShowStatusScreen(g_last_data_error == "wifi-disconnected"
                               ? "No Wi-Fi connection"
                               : "Fetch failed",
                             g_last_data_error);
          } else if (fetchedTraffic.empty()) {
            ShowStatusScreen("No departures", "right now");
          } else {
            ShowStatusScreen("Filter matched nothing", raw_filter);
          }
        } else {
          LogInfo(F("data-updated"),
                  "shown=" + String(shownTraffic.size())
                    + " fetched=" + String(fetchedTraffic.size()));
        }
      }
      xSemaphoreGive(dataMutex);
    }

    // Sleep in short slices so a stop switch (touch) can break out early and
    // refetch immediately instead of waiting out the full interval.
    const int delay_real_ms =
      std::max(0, delay_planned_ms - static_cast<int>(millis() - cycle_start_ms));
    const int slice_ms = 100;
    int waited_ms = 0;
    while (waited_ms < delay_real_ms && !g_force_refresh && g_display_awake) {
      vTaskDelay(pdMS_TO_TICKS(slice_ms));
      waited_ms += slice_ms;
    }
  }
}


/**
 * @brief Draws frames. Scrolling text gets a fast frame rate; otherwise frames
 * are only drawn when something changed.
 *
 * @param pvParameters Pointer to task-specific parameters.
 */
void ScreenUpdateTask(void* pvParameters) {
  while (true) {
    if (!g_display_awake || g_config_portal_open || g_setup_screen_visible) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    bool animating = false;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      pTraficManager->updateScreen();
      DrawHeaderBar();
      animating = p_screen && p_screen->IsAnimating();
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(animating
                                ? global_settings.ms_task_delay_screen_update
                                : global_settings.ms_task_delay_screen_idle));
  }
}


/**
 * @brief GETs a URL and parses the JSON body while it streams in.
 *
 * @param url The URL to fetch.
 * @param doc Receives the parsed (filtered) JSON.
 * @param filter_json ArduinoJson filter: only these fields are kept.
 * @param error Set to a short error code on failure.
 * @return True on success.
 */
bool HttpGetJson(const String& url, JsonDocument& doc, const char* filter_json,
                 String& error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "wifi-disconnected";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // HTTP/1.0 avoids chunked transfer encoding, so the body can be parsed
  // straight from the socket instead of being buffered in RAM first.
  http.useHTTP10(true);
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    error = "http-begin-failed";
    return false;
  }

  const int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    error = http_code <= 0 ? String("http-") + http.errorToString(http_code)
                           : String("http-status-") + String(http_code);
    http.end();
    return false;
  }

  JsonDocument filter;
  deserializeJson(filter, filter_json);
  DeserializationError json_error =
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter),
                    DeserializationOption::NestingLimit(20));
  http.end();
  if (json_error) {
    error = String("json-") + json_error.c_str();
    return false;
  }
  return true;
}


/**
 * @brief Fetches live departures for one or more comma-separated RBL IDs.
 *
 * @param rbl_list e.g. "4111" or "4111,4118".
 * @param out Receives the departures.
 * @return True if the request succeeded (the result may still be empty).
 */
bool FetchDepartures(const String& rbl_list, std::vector<Monitor>& out) {
  out.clear();
  if (rbl_list.isEmpty()) {
    g_last_data_error = "no-rbl";
    LogWarn(F("fetch-skipped"), String(F("no RBL configured")));
    return false;
  }

  String url = MONITOR_URL "activateTrafficInfo=stoerunglang";
  for (const String& rbl : GetSplittedStrings(rbl_list, ',')) {
    url += "&rbl=";
    url += rbl;
  }

  LogInfo(F("fetch-start"), "rbl=" + rbl_list);
  JsonDocument doc;
  String error;
  if (!HttpGetJson(url, doc, kMonitorFilterJson, error)) {
    g_last_data_error = error;
    LogWarn(F("fetch-failed"), error);
    return false;
  }
  g_last_data_error = "";
  out = GetMonitorsFromJson(doc);
  return g_last_data_error.isEmpty();
}


// ---------------------------------------------------------------------------
// Setup screens on the display.
// ---------------------------------------------------------------------------

#if HAS_ESP_QRCODE
// esp_qrcode_generate() calls back without a user pointer, so the target
// rectangle is passed through here.
struct QrDrawTarget {
  int x = 0;
  int y = 0;
  int max_px = 0;
  int drawn_px = 0;
} g_qr_target;

void DrawQrCallback(esp_qrcode_handle_t qrcode) {
  const int size = esp_qrcode_get_size(qrcode);
  const int border = 2;  // quiet zone, in modules
  const int scale = std::max(1, g_qr_target.max_px / (size + 2 * border));
  const int total = (size + 2 * border) * scale;
  const int x0 = g_qr_target.x + border * scale;
  const int y0 = g_qr_target.y + border * scale;
  tft.fillRect(g_qr_target.x, g_qr_target.y, total, total, TFT_WHITE);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (esp_qrcode_get_module(qrcode, x, y)) {
        tft.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, TFT_BLACK);
      }
    }
  }
  g_qr_target.drawn_px = total;
}
#endif

/**
 * @brief Draws a QR code with a white quiet zone.
 *
 * @return The drawn edge length in pixels, or 0 if QR codes are unsupported.
 */
int DrawQrCode(const String& text, int x, int y, int max_px) {
#if HAS_ESP_QRCODE
  g_qr_target.x = x;
  g_qr_target.y = y;
  g_qr_target.max_px = max_px;
  g_qr_target.drawn_px = 0;
  esp_qrcode_config_t cfg = {};
  cfg.display_func = DrawQrCallback;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  if (esp_qrcode_generate(&cfg, text.c_str()) != ESP_OK) {
    return 0;
  }
  return g_qr_target.drawn_px;
#else
  return 0;
#endif
}

/**
 * @brief Common layout of the setup screens: title, QR code on the left.
 *
 * @return X coordinate of the text column right of the QR code.
 */
int DrawSetupLayout(const String& title, const String& qr_text) {
  tft.fillScreen(TFT_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_TEXT, TFT_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString(title, 12, 10);
  tft.drawFastHLine(12, 38, tft.width() - 24, TFT_ACCENT);
  const int qr_px = DrawQrCode(qr_text, 12, 48, 150);
  return qr_px ? 12 + qr_px + 12 : 12;
}

void DrawSetupTextLine(int x, int& y, const String& text, uint16_t color) {
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BG);
  tft.drawString(text, x, y);
  y += 17;
}

///< Touch area of the "Change Wi-Fi" button on the setup screen.
bool IsInSetupWifiButton(int x, int y) {
  return x >= tft.width() - 130 && y >= tft.height() - 48;
}

/**
 * @brief Shows how to reach the setup web app. Caller holds dataMutex (or no
 * other task is running yet).
 */
void DrawSetupScreen() {
  const bool was_visible = g_setup_screen_visible;
  g_setup_screen_visible = true;
  g_status_screen_visible = true;
  g_force_header_redraw = true;

  const bool first_run = global_settings.GetConfiguredCount() == 0;
  const String ip = WiFi.localIP().toString();
  const int x = DrawSetupLayout(first_run ? "Add your first stop" : "Setup",
                                "http://" + ip + "/");
  int y = 50;
  DrawSetupTextLine(x, y, "Scan with a phone", TFT_TEXT);
  DrawSetupTextLine(x, y, "on the same Wi-Fi", TFT_TEXT);
  DrawSetupTextLine(x, y, "to search stops and", TFT_TEXT);
  DrawSetupTextLine(x, y, "change settings.", TFT_TEXT);
  y += 8;
  DrawSetupTextLine(x, y, "Or open:", TFT_TEXT);
  DrawSetupTextLine(x, y, ip, TFT_ACCENT);
  DrawSetupTextLine(x, y, DEVICE_HOSTNAME ".local", TFT_ACCENT);

  const int bw = 118;
  const int bh = 28;
  const int bx = tft.width() - bw - 8;
  const int by = tft.height() - bh - 8;
  if (!first_run) {
    int hint_y = by + 6;
    DrawSetupTextLine(12, hint_y, "Tap to close", TFT_TEXT);
  }
  tft.drawRoundRect(bx, by, bw, bh, 6, TFT_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(TFT_TEXT, TFT_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Change Wi-Fi", bx + bw / 2, by + bh / 2);
  tft.setTextDatum(TL_DATUM);

  if (!was_visible) {
    LogInfo(F("setup-screen"), "http://" + ip + "/");
  }
}

void ShowSetupScreen() {
  if (dataMutex && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    DrawSetupScreen();
    xSemaphoreGive(dataMutex);
  } else {
    DrawSetupScreen();
  }
}

void CloseSetupScreen() {
  g_setup_screen_visible = false;
  ApplyProfileChange();
}

/**
 * @brief Shown while the Wi-Fi hotspot is open (first boot / Wi-Fi change).
 */
void DrawWifiPortalScreen() {
  const bool locked =
    dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE;
  g_status_screen_visible = true;
  g_force_header_redraw = true;

  const String ssid = StringDatabase::GetWiFissid();
  // Standard Wi-Fi QR payload: phones offer to join the open hotspot.
  const int x =
    DrawSetupLayout("Connect to Wi-Fi", "WIFI:T:nopass;S:" + ssid + ";;");
  int y = 50;
  DrawSetupTextLine(x, y, "1. Scan the code or", TFT_TEXT);
  DrawSetupTextLine(x, y, "   join the Wi-Fi", TFT_TEXT);
  DrawSetupTextLine(x, y, "   " + ssid, TFT_ACCENT);
  DrawSetupTextLine(x, y, "2. Pick your home", TFT_TEXT);
  DrawSetupTextLine(x, y, "   Wi-Fi and enter", TFT_TEXT);
  DrawSetupTextLine(x, y, "   its password.", TFT_TEXT);
  y += 8;
  DrawSetupTextLine(x, y, "Page not opening?", TFT_TEXT);
  DrawSetupTextLine(x, y, "Go to 192.168.4.1", TFT_ACCENT);

  if (locked) {
    xSemaphoreGive(dataMutex);
  }
}


// ---------------------------------------------------------------------------
// Wi-Fi setup (captive portal). Only Wi-Fi is configured here; stops and
// settings live in the setup web app on the home network.
// ---------------------------------------------------------------------------

static const char kPortalHead[] PROGMEM =
  "<style>"
  "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
  "background:#f4f5f7;color:#14171a}"
  ".wrap{max-width:440px}"
  "h1{font-size:1.6em;margin-bottom:.2em}"
  "button,input[type=submit]{background:#c8102e;border-radius:10px;"
  "font-weight:600}"
  "button.D{background:#6b7280}"
  "input,select{border-radius:10px;border:1px solid #d0d5dc;padding:10px;"
  "font-size:16px}"
  "a{color:#c8102e}"
  "@media(prefers-color-scheme:dark){body{background:#0b0d10;color:#f3f4f6}"
  "input,select{background:#16191e;color:#f3f4f6;border-color:#2a2f37}}"
  "</style>";

/**
 * @brief Connects to Wi-Fi, opening the Wi-Fi setup hotspot when needed.
 *
 * @param force_config_portal Open the hotspot even though Wi-Fi works (used to
 * change networks at runtime).
 */
void WiFiManagerTask(bool force_config_portal) {
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(ENABLE_SERIAL_LOGS);
  wifiManager.setTitle("Wien Monitor");
  wifiManager.setHostname(DEVICE_HOSTNAME);
  wifiManager.setCustomHeadElement(kPortalHead);
  std::vector<const char*> menu = { "wifi", "info", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setShowInfoUpdate(false);
  wifiManager.setConnectTimeout(20);
  wifiManager.setAPCallback([](WiFiManager*) {
    DrawWifiPortalScreen();
  });

  const String ssid = StringDatabase::GetWiFissid();
  bool connected = false;
  if (force_config_portal) {
    // Close the hotspot again if nobody uses it; it costs a lot of power.
    wifiManager.setConfigPortalTimeout(300);
    LogInfo(F("config-portal-open"));
    connected = wifiManager.startConfigPortal(ssid.c_str());
    if (WiFi.status() != WL_CONNECTED) {
      // Cancelled or timed out: go back to the previous network.
      WiFi.mode(WIFI_STA);
      WiFi.begin();
      connected = WaitForWifi(10000);
    }
  } else {
    if (wifiManager.getWiFiIsSaved()) {
      // With saved credentials, a failed connect (router still booting after
      // a power cut, ...) should retry via reboot, not leave the hotspot open.
      wifiManager.setConfigPortalTimeout(180);
      ShowStatusScreen("Connecting to Wi-Fi", "");
    }
    connected = wifiManager.autoConnect(ssid.c_str());
  }

  if (!connected) {
    LogError(F("wifi-config-failed"));
    if (!force_config_portal) {
      ESP.restart();
    }
  } else {
    LogInfo(F("wifi-ready"), WiFi.SSID());
  }
}


// ---------------------------------------------------------------------------
// Stop search index.
//
// Wiener Linien publish every platform ("Haltepunkt") as open data. A trimmed
// copy is kept in flash - one "diva;rbl;name;municipality" line per platform,
// plus a "#<timestamp>;<count>" footer - so the setup app can search stops by
// name instead of asking for RBL numbers.
// ---------------------------------------------------------------------------

static constexpr unsigned long kStopIndexMaxAgeS = 30UL * 24UL * 60UL * 60UL;
static constexpr unsigned long kStopIndexRetryMs = 10UL * 60UL * 1000UL;
unsigned long g_last_index_attempt_ms = 0;
bool g_index_attempted = false;

/**
 * @brief Calls fn(line) for every line of a file, reading in blocks.
 */
template<typename Fn>
void ForEachFileLine(File& file, Fn&& fn) {
  uint8_t buf[512];
  char line[200];
  size_t len = 0;
  while (true) {
    const int n = file.read(buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; ++i) {
      const char c = static_cast<char>(buf[i]);
      if (c == '\n') {
        line[len] = '\0';
        fn(line);
        len = 0;
      } else if (c != '\r' && len < sizeof(line) - 1) {
        line[len++] = c;
      }
    }
  }
  if (len) {
    line[len] = '\0';
    fn(line);
  }
}

/**
 * @brief Splits a line in place at ';'.
 *
 * @return Number of fields found (at most max_fields; the rest of the line is
 * cut off).
 */
int SplitFields(char* line, char** fields, int max_fields) {
  int count = 0;
  fields[count++] = line;
  for (char* p = line; *p; ++p) {
    if (*p == ';') {
      *p = '\0';
      if (count == max_fields) {
        break;
      }
      fields[count++] = p + 1;
    }
  }
  return count;
}

struct StopIndexInfo {
  bool exists = false;
  uint32_t stamp = 0;
  int count = 0;
};

/**
 * @brief Reads the "#<timestamp>;<count>" footer of an index file.
 */
StopIndexInfo ReadIndexInfo(const char* path) {
  StopIndexInfo info;
  if (!SPIFFS.exists(path)) {
    return info;
  }
  File file = SPIFFS.open(path, "r");
  if (!file) {
    return info;
  }
  const size_t size = file.size();
  file.seek(size > 48 ? size - 48 : 0);
  String tail = file.readString();
  file.close();
  const int hash = tail.lastIndexOf('#');
  if (hash >= 0) {
    info.exists = true;
    info.stamp = strtoul(tail.c_str() + hash + 1, nullptr, 10);
    const int sep = tail.indexOf(';', hash);
    if (sep > 0) {
      info.count = tail.substring(sep + 1).toInt();
    }
  }
  return info;
}

bool IndexNeedsRefresh(const char* path, int min_count) {
  const StopIndexInfo info = ReadIndexInfo(path);
  if (!info.exists || info.count < min_count) {
    return true;
  }
  const time_t now = time(nullptr);
  if (now < 1600000000) {
    return false;  // clock not set; can't tell the age
  }
  return info.stamp < 1600000000
         || static_cast<unsigned long>(now - info.stamp) > kStopIndexMaxAgeS;
}

/**
 * @brief Downloads a CSV file and calls fn(line) for every line without
 * holding the file in memory. fn returns false to abort (and sets error).
 */
template<typename Fn>
bool StreamCsvLines(const char* url, String& error, Fn&& fn) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "wifi-disconnected";
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    error = "http-begin-failed";
    return false;
  }
  const int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    error = http_code <= 0 ? String("http-") + http.errorToString(http_code)
                           : String("http-status-") + String(http_code);
    http.end();
    return false;
  }

  NetworkClient& stream = http.getStream();
  uint8_t buf[512];
  char line[200];
  size_t len = 0;
  bool ok = true;
  unsigned long last_data_ms = millis();
  while (ok) {
    const int available = stream.available();
    if (available > 0) {
      const int n = stream.read(buf, std::min(available, static_cast<int>(sizeof(buf))));
      for (int i = 0; i < n && ok; ++i) {
        const char c = static_cast<char>(buf[i]);
        if (c == '\n') {
          line[len] = '\0';
          ok = fn(line);
          len = 0;
        } else if (c != '\r' && len < sizeof(line) - 1) {
          line[len++] = c;
        }
      }
      last_data_ms = millis();
      vTaskDelay(1);  // let the idle task (watchdog) run
    } else if (!stream.connected()) {
      break;
    } else if (millis() - last_data_ms > 10000) {
      error = "timeout";
      ok = false;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  if (ok && len) {
    line[len] = '\0';
    ok = fn(line);
  }
  http.end();
  return ok;
}

/**
 * @brief Downloads the platform list and stores the trimmed search index.
 */
bool BuildStopIndex(String& error) {
  g_index_attempted = true;
  g_last_index_attempt_ms = millis();
  LogInfo(F("stops-index-start"));

  File out = SPIFFS.open(STOPS_INDEX_TMP_FILE, "w");
  if (!out) {
    error = "fs-open-failed";
    return false;
  }

  // Source columns: StopID;DIVA;StopText;Municipality;MunicipalityID;Lon;Lat
  int count = 0;
  const bool ok = StreamCsvLines(STOPS_INDEX_URL, error, [&](char* line) {
    char* f[5];
    const int n = SplitFields(line, f, 5);
    if (n < 3 || !isdigit(static_cast<unsigned char>(f[0][0]))
        || !isdigit(static_cast<unsigned char>(f[1][0]))) {
      return true;  // header or junk
    }
    const char* municipality = (n >= 4 && strcmp(f[3], "Wien") != 0) ? f[3] : "";
    out.printf("%s;%s;%s;%s\n", f[1], f[0], f[2], municipality);
    ++count;
    return true;
  });
  out.printf("#%lu;%d\n", static_cast<unsigned long>(time(nullptr)), count);
  out.close();

  if (!ok || count < 100) {
    if (ok) {
      error = "too-few-stops";
    }
    SPIFFS.remove(STOPS_INDEX_TMP_FILE);
    LogWarn(F("stops-index-failed"), error + " stops=" + String(count));
    return false;
  }
  SPIFFS.remove(STOPS_INDEX_FILE);
  SPIFFS.rename(STOPS_INDEX_TMP_FILE, STOPS_INDEX_FILE);
  LogInfo(F("stops-index-ok"), "stops=" + String(count));
  return true;
}


// ---------------------------------------------------------------------------
// Line index.
//
// One "name;type;dir1;dir2;other" line per line, where dir1/dir2 list the
// platforms (RBL) of the longest route in each direction in travel order, and
// "other" the platforms only served by short or special runs. The setup app
// uses it to browse lines, to show which lines stop at a station and to offer
// directions that have no departures right now.
// ---------------------------------------------------------------------------

struct LineRoute {
  uint16_t id = 0;
  String name;
  String type;
  std::vector<uint16_t> best[2];  ///< Longest pattern per direction, in order.
  std::vector<uint16_t> served;   ///< Every platform the line stops at, sorted.
};

void PrintIdList(File& out, const std::vector<uint16_t>& ids) {
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) {
      out.print(',');
    }
    out.print(ids[i]);
  }
}

/**
 * @brief Downloads the line list and every route, and stores the line index.
 */
bool BuildLineIndex(String& error) {
  LogInfo(F("lines-index-start"));

  // Source columns: LineID;LineText;SortingHelp;Realtime;MeansOfTransport
  std::vector<LineRoute> lines;
  if (!StreamCsvLines(LINES_URL, error, [&](char* line) {
        char* f[5];
        if (SplitFields(line, f, 5) == 5
            && isdigit(static_cast<unsigned char>(f[0][0])) && lines.size() < 400) {
          LineRoute route;
          route.id = atoi(f[0]);
          route.name = f[1];
          route.type = f[4];
          lines.push_back(std::move(route));
        }
        return true;
      })) {
    LogWarn(F("lines-index-failed"), error);
    return false;
  }
  if (lines.size() < 10) {
    error = "too-few-lines";
    LogWarn(F("lines-index-failed"), error);
    return false;
  }
  std::sort(lines.begin(), lines.end(),
            [](const LineRoute& a, const LineRoute& b) { return a.id < b.id; });
  auto find_line = [&](long id) -> LineRoute* {
    auto it = std::lower_bound(
      lines.begin(), lines.end(), id,
      [](const LineRoute& r, long value) { return r.id < value; });
    return it != lines.end() && it->id == id ? &*it : nullptr;
  };

  // Source columns: LineID;PatternID;StopSeqCount;StopID;Direction. The rows
  // of one pattern come in stop order; some rows lack the direction.
  LineRoute* route = nullptr;
  long cur_line = -1;
  long cur_pattern = -1;
  int cur_dir = -1;
  std::vector<uint16_t> pattern;
  pattern.reserve(64);
  auto flush = [&]() {
    if (route && cur_dir >= 0 && pattern.size() > route->best[cur_dir].size()) {
      route->best[cur_dir] = pattern;
    }
    pattern.clear();
  };
  const bool ok = StreamCsvLines(ROUTES_URL, error, [&](char* line) {
    char* f[5];
    const int n = SplitFields(line, f, 5);
    if (n < 4 || !isdigit(static_cast<unsigned char>(f[0][0]))
        || !isdigit(static_cast<unsigned char>(f[3][0]))) {
      return true;  // header or junk
    }
    const long line_id = atol(f[0]);
    const long pattern_id = atol(f[1]);
    const int dir = (n >= 5 && (f[4][0] == '1' || f[4][0] == '2')) ? f[4][0] - '1' : -1;
    if (line_id != cur_line || pattern_id != cur_pattern || dir != cur_dir) {
      flush();
      if (line_id != cur_line) {
        route = find_line(line_id);
      }
      cur_line = line_id;
      cur_pattern = pattern_id;
      cur_dir = dir;
    }
    const long stop = atol(f[3]);
    if (!route || stop <= 0 || stop > 65535) {
      return true;
    }
    if (ESP.getFreeHeap() < 30000) {
      error = "low-memory";
      return false;
    }
    const uint16_t id = static_cast<uint16_t>(stop);
    if (pattern.size() < 160) {
      pattern.push_back(id);
    }
    auto it = std::lower_bound(route->served.begin(), route->served.end(), id);
    if (it == route->served.end() || *it != id) {
      route->served.insert(it, id);
    }
    return true;
  });
  flush();
  if (!ok) {
    LogWarn(F("lines-index-failed"), error);
    return false;
  }

  File out = SPIFFS.open(LINES_INDEX_TMP_FILE, "w");
  if (!out) {
    error = "fs-open-failed";
    return false;
  }
  int count = 0;
  for (const LineRoute& r : lines) {
    if (r.served.empty()) {
      continue;  // e.g. S-Bahn lines, which come without route data
    }
    std::vector<uint16_t> other;
    for (uint16_t id : r.served) {
      if (std::find(r.best[0].begin(), r.best[0].end(), id) == r.best[0].end()
          && std::find(r.best[1].begin(), r.best[1].end(), id) == r.best[1].end()) {
        other.push_back(id);
      }
    }
    out.print(r.name);
    out.print(';');
    out.print(r.type);
    for (int d = 0; d < 2; ++d) {
      out.print(';');
      PrintIdList(out, r.best[d]);
    }
    out.print(';');
    PrintIdList(out, other);
    out.print('\n');
    ++count;
  }
  out.printf("#%lu;%d\n", static_cast<unsigned long>(time(nullptr)), count);
  out.close();

  if (count < 10) {
    error = "too-few-lines";
    SPIFFS.remove(LINES_INDEX_TMP_FILE);
    LogWarn(F("lines-index-failed"), error);
    return false;
  }
  SPIFFS.remove(LINES_INDEX_FILE);
  SPIFFS.rename(LINES_INDEX_TMP_FILE, LINES_INDEX_FILE);
  LogInfo(F("lines-index-ok"), "lines=" + String(count));
  return true;
}

/**
 * @brief Lower-case ASCII with umlauts folded, punctuation turned into spaces,
 * so "Straße" matches "strasse" and "Karlsplatz U" matches "karlsplatz".
 */
String NormalizeForSearch(const char* text) {
  String out;
  out.reserve(strlen(text) + 4);
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p;
       ++p) {
    const unsigned char c = *p;
    if (c == 0xC3 && p[1]) {
      switch (*++p) {
        case 0xA4:
        case 0x84: out += 'a'; break;  // ä Ä
        case 0xB6:
        case 0x96: out += 'o'; break;  // ö Ö
        case 0xBC:
        case 0x9C: out += 'u'; break;  // ü Ü
        case 0x9F: out += "ss"; break;  // ß
        case 0xA9:
        case 0x89: out += 'e'; break;  // é É
        default: out += ' '; break;
      }
    } else if (isalnum(c)) {
      out += static_cast<char>(tolower(c));
    } else {
      out += ' ';
    }
  }
  return out;
}

struct StopHit {
  String diva;
  String name;
  String municipality;
  int score;
};

/**
 * @brief Searches the stop index. Every word of the query must appear in the
 * stop name; names starting with the query rank first.
 */
std::vector<StopHit> SearchStops(const String& query, size_t limit) {
  std::vector<StopHit> hits;
  String normalized_query = NormalizeForSearch(query.c_str());
  normalized_query.trim();
  const std::vector<String> tokens = GetSplittedStrings(normalized_query, ' ');
  if (tokens.empty()) {
    return hits;
  }
  const String query_prefix = String(" ") + normalized_query;
  const String first_word = String(" ") + tokens[0];

  File file = SPIFFS.open(STOPS_INDEX_FILE, "r");
  if (!file) {
    return hits;
  }
  ForEachFileLine(file, [&](char* line) {
    if (line[0] == '#') {
      return;
    }
    char* f[4];
    const int n = SplitFields(line, f, 4);
    if (n < 3) {
      return;
    }
    const char* municipality = n >= 4 ? f[3] : "";
    String haystack = String(" ") + NormalizeForSearch(f[2]) + " "
                      + NormalizeForSearch(municipality) + " ";
    for (const String& token : tokens) {
      if (haystack.indexOf(token) < 0) {
        return;
      }
    }
    for (const StopHit& hit : hits) {
      if (hit.diva == f[0]) {
        return;  // another platform of a station we already have
      }
    }
    if (hits.size() >= 300) {
      return;
    }
    const int score = haystack.startsWith(query_prefix) ? 0
                      : haystack.indexOf(first_word) >= 0 ? 1
                                                          : 2;
    hits.push_back({ String(f[0]), String(f[2]), String(municipality), score });
  });
  file.close();

  std::sort(hits.begin(), hits.end(), [](const StopHit& a, const StopHit& b) {
    if (a.score != b.score) {
      return a.score < b.score;
    }
    if (a.name.length() != b.name.length()) {
      return a.name.length() < b.name.length();
    }
    return a.name < b.name;
  });
  if (hits.size() > limit) {
    hits.erase(hits.begin() + limit, hits.end());
  }
  return hits;
}

/**
 * @brief All platform IDs (RBL) the offline index lists for a station.
 */
std::vector<String> StopIndexRblsForDiva(const String& diva) {
  std::vector<String> rbls;
  File file = SPIFFS.open(STOPS_INDEX_FILE, "r");
  if (!file) {
    return rbls;
  }
  ForEachFileLine(file, [&](char* line) {
    char* f[3];
    if (SplitFields(line, f, 3) >= 2 && diva == f[0]) {
      rbls.push_back(String(f[1]));
    }
  });
  file.close();
  return rbls;
}


// ---------------------------------------------------------------------------
// Setup web app (http://wien-monitor.local or the IP shown on the display).
// ---------------------------------------------------------------------------

WebServer g_web(80);

// Only the fields the station picker needs.
static const char kStationFilterJson[] =
  "{\"data\":{\"monitors\":[{"
  "\"locationStop\":{\"properties\":{\"title\":true,\"attributes\":{\"rbl\":true}}},"
  "\"lines\":[{\"name\":true,\"towards\":true,\"type\":true,"
  "\"departures\":{\"departure\":[{\"departureTime\":{\"countdown\":true}}]}}]"
  "}],"
  "\"trafficInfos\":[{\"title\":true,\"description\":true,\"relatedLines\":true}]}}";

void NoteWebActivity() {
  g_last_web_ms = millis();
}

void SendJson(JsonDocument& doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  g_web.sendHeader("Cache-Control", "no-store");
  g_web.send(code, "application/json", body);
}

void SendJsonError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  SendJson(doc, code);
}

bool ParseJsonBody(JsonDocument& doc) {
  if (!g_web.hasArg("plain")) {
    return false;
  }
  return !deserializeJson(doc, g_web.arg("plain"));
}

void FillStateJson(JsonDocument& doc) {
  JsonArray profiles_json = doc["profiles"].to<JsonArray>();
  int active_pos = -1;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    const std::vector<Config>& profiles = global_settings.GetProfiles();
    const int active = global_settings.GetActiveIndex();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
      const Config& cfg = profiles[i];
      if (!cfg.IsConfigured()) {
        continue;
      }
      if (i == active) {
        active_pos = profiles_json.size();
      }
      JsonObject obj = profiles_json.add<JsonObject>();
      obj["name"] = cfg.GetName();
      obj["rbl"] = cfg.GetLinesRblAsString();
      obj["filter"] = cfg.GetLinesFilterAsString();
      obj["rows"] = cfg.GetLinesCountAsInt();
      obj["diva"] = cfg.GetDiva();
      obj["badges"] = cfg.GetBadges();

      // Last departures fetched for this stop, so the app can show them
      // without another request to Wiener Linien.
      const CachedTraffic& cache = g_traffic_cache[i];
      const unsigned long age_ms = millis() - cache.fetched_ms;
      if (cache.valid && age_ms <= kTrafficCacheMaxAgeMs) {
        const int age_min = static_cast<int>(age_ms / 60000UL);
        obj["age"] = static_cast<int>(age_ms / 1000UL);
        JsonArray live = obj["live"].to<JsonArray>();
        for (const Monitor& monitor : cache.monitors) {
          if (live.size() >= 6) {
            break;
          }
          JsonObject row = live.add<JsonObject>();
          row["l"] = monitor.name;
          row["t"] = monitor.towards;
          JsonArray next = row["c"].to<JsonArray>();
          for (int countdown : monitor.countdown) {
            if (next.size() >= 3) {
              break;
            }
            next.add(std::max(0, countdown - age_min));
          }
          if (monitor.description.length()) {
            row["d"] = monitor.description;
          }
        }
      }
    }
    xSemaphoreGive(dataMutex);
  }
  doc["active"] = active_pos;

  const DisplayPrefs prefs = global_settings.GetPrefs();
  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["brightness"] = prefs.brightness_pct;
  settings["dim"] = prefs.dim_after_s;
  settings["off"] = prefs.off_after_s;

  const StopIndexInfo index = ReadIndexInfo(STOPS_INDEX_FILE);
  const StopIndexInfo line_index = ReadIndexInfo(LINES_INDEX_FILE);
  JsonObject device = doc["device"].to<JsonObject>();
  device["ssid"] = WiFi.SSID();
  device["ip"] = WiFi.localIP().toString();
  device["host"] = DEVICE_HOSTNAME ".local";
  device["rssi"] = WiFi.RSSI();
  device["heap"] = ESP.getFreeHeap();
  device["uptime"] = millis() / 1000;
  device["stops"] = index.exists ? index.count : 0;
  device["stopsDate"] = index.stamp;
  device["lines"] = line_index.exists ? line_index.count : 0;
  device["linesDate"] = line_index.stamp;
  device["now"] = static_cast<uint32_t>(time(nullptr));

  JsonObject limits = doc["limits"].to<JsonObject>();
  limits["maxStops"] = kProfileCount;
  limits["maxRows"] = Config::cnt_max_lines;
  limits["maxIds"] = Config::kMaxRblIds;
}

void HandleRoot() {
  NoteWebActivity();
  g_web.sendHeader("Cache-Control", "no-cache");
  g_web.send_P(200, "text/html", kSetupPageHtml);
}

void HandleState() {
  NoteWebActivity();
  JsonDocument doc;
  FillStateJson(doc);
  SendJson(doc);
}

void HandlePing() {
  NoteWebActivity();
  JsonDocument doc;
  doc["ok"] = true;
  SendJson(doc);
}

void HandleSaveProfiles() {
  NoteWebActivity();
  JsonDocument body;
  if (!ParseJsonBody(body) || !body["profiles"].is<JsonArray>()) {
    SendJsonError(400, "invalid request");
    return;
  }

  std::vector<Config> list;
  for (JsonVariantConst obj : body["profiles"].as<JsonArrayConst>()) {
    if (static_cast<int>(list.size()) >= kProfileCount) {
      break;
    }
    Config cfg;
    String name = obj["name"] | "";
    if (name.length() > 40) {
      name = name.substring(0, 40);
    }
    cfg.SetName(name);
    cfg.SetLinesRBL(obj["rbl"] | "");
    cfg.SetLinesFilter(obj["filter"] | "");
    cfg.SetLinesCount(JsonToInt(obj["rows"], Config::cnt_default_lines));
    cfg.SetDiva(obj["diva"] | "");
    cfg.SetBadges(obj["badges"] | "");
    if (cfg.IsConfigured()) {
      list.push_back(cfg);
    }
  }
  const int active = JsonToInt(body["active"], 0);

  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    global_settings.SetProfiles(list, active);
    ClearTrafficCache();
    xSemaphoreGive(dataMutex);
  }
  LogInfo(F("web-profiles-saved"), "stops=" + String(list.size()));

  // Show the result right away (this also closes the first-run screen).
  g_setup_screen_visible = false;
  ApplyProfileChange();
  WakeDisplayIfNeeded();
  HandleState();
}

void HandleSaveSettings() {
  NoteWebActivity();
  JsonDocument body;
  if (!ParseJsonBody(body)) {
    SendJsonError(400, "invalid request");
    return;
  }
  DisplayPrefs prefs = global_settings.GetPrefs();
  prefs.brightness_pct = JsonToInt(body["brightness"], prefs.brightness_pct);
  prefs.dim_after_s = JsonToInt(body["dim"], prefs.dim_after_s);
  prefs.off_after_s = JsonToInt(body["off"], prefs.off_after_s);
  global_settings.SetPrefs(prefs);

  // Show the new brightness immediately.
  WakeDisplayIfNeeded();
  if (g_display_awake && !g_backlight_dimmed) {
    BacklightSetPct(global_settings.GetPrefs().brightness_pct);
  }
  HandleState();
}

void HandleShow() {
  NoteWebActivity();
  JsonDocument body;
  ParseJsonBody(body);
  const int position = JsonToInt(body["index"], -1);

  bool changed = false;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    const std::vector<Config>& profiles = global_settings.GetProfiles();
    int seen = 0;
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
      if (!profiles[i].IsConfigured()) {
        continue;
      }
      if (seen++ == position) {
        changed = global_settings.SetActiveIndex(i);
        break;
      }
    }
    xSemaphoreGive(dataMutex);
  }

  WakeDisplayIfNeeded();
  if (changed || g_setup_screen_visible) {
    g_setup_screen_visible = false;
    ApplyProfileChange();
  }
  HandleState();
}

void HandleSearch() {
  NoteWebActivity();
  if (!SPIFFS.exists(STOPS_INDEX_FILE)) {
    String error;
    if (!BuildStopIndex(error)) {
      SendJsonError(503, String("The stop list could not be downloaded (")
                           + error + ")");
      return;
    }
  }
  const std::vector<StopHit> hits = SearchStops(g_web.arg("q"), 12);
  JsonDocument doc;
  JsonArray results = doc["results"].to<JsonArray>();
  for (const StopHit& hit : hits) {
    JsonObject obj = results.add<JsonObject>();
    obj["diva"] = hit.diva;
    obj["name"] = hit.name;
    obj["muni"] = hit.municipality;
  }
  SendJson(doc);
}

void HandleStation() {
  NoteWebActivity();
  const String diva = g_web.arg("diva");
  bool valid = diva.length() > 0 && diva.length() <= 12;
  for (size_t i = 0; i < diva.length() && valid; ++i) {
    valid = isdigit(static_cast<unsigned char>(diva[i]));
  }
  if (!valid) {
    SendJsonError(400, "invalid station");
    return;
  }

  JsonDocument live;
  String error;
  const bool live_ok = HttpGetJson(String(MONITOR_URL) + "diva=" + diva, live,
                                   kStationFilterJson, error);

  JsonDocument doc;
  JsonArray platforms = doc["platforms"].to<JsonArray>();
  auto find_platform = [&](long rbl) {
    for (JsonObject platform : platforms) {
      if ((platform["rbl"] | 0L) == rbl) {
        return platform;
      }
    }
    return JsonObject();
  };

  if (live_ok) {
    for (JsonVariant monitor : live["data"]["monitors"].as<JsonArray>()) {
      JsonVariant properties = monitor["locationStop"]["properties"];
      const long rbl = properties["attributes"]["rbl"] | 0L;
      if (rbl <= 0) {
        continue;
      }
      JsonObject platform = find_platform(rbl);
      if (platform.isNull()) {
        platform = platforms.add<JsonObject>();
        platform["rbl"] = rbl;
        platform["title"] = properties["title"] | "";
        platform["lines"].to<JsonArray>();
      }
      JsonArray lines = platform["lines"];
      for (JsonVariant line : monitor["lines"].as<JsonArray>()) {
        JsonObject entry = lines.add<JsonObject>();
        entry["name"] = line["name"] | "";
        entry["towards"] = TitleCaseSingleWord(line["towards"] | "");
        entry["type"] = line["type"] | "";
        JsonArray next = entry["next"].to<JsonArray>();
        auto add_next = [&](JsonVariant item) {
          JsonVariant countdown = item["departureTime"]["countdown"];
          if (next.size() < 3 && countdown.is<int>()) {
            next.add(countdown.as<int>());
          }
        };
        JsonVariant departure = line["departures"]["departure"];
        if (departure.is<JsonArray>()) {
          for (JsonVariant item : departure.as<JsonArray>()) {
            add_next(item);
          }
        } else if (departure.is<JsonObject>()) {
          add_next(departure);
        }
      }
    }

    // Disruptions and notices for the lines at this station.
    JsonArray alerts = doc["alerts"].to<JsonArray>();
    for (JsonVariant info : live["data"]["trafficInfos"].as<JsonArray>()) {
      if (alerts.size() >= 8) {
        break;
      }
      JsonObject alert = alerts.add<JsonObject>();
      alert["title"] = info["title"] | "";
      alert["text"] = info["description"] | "";
      JsonArray related = alert["lines"].to<JsonArray>();
      for (JsonVariant line : info["relatedLines"].as<JsonArray>()) {
        related.add(line.as<const char*>());
      }
    }
  }

  // Platforms without departures right now only appear in the offline list.
  for (const String& rbl : StopIndexRblsForDiva(diva)) {
    const long id = rbl.toInt();
    if (id > 0 && find_platform(id).isNull()) {
      JsonObject platform = platforms.add<JsonObject>();
      platform["rbl"] = id;
      platform["lines"].to<JsonArray>();
    }
  }

  if (!live_ok && platforms.size() == 0) {
    SendJsonError(502, String("Live data unavailable (") + error + ")");
    return;
  }
  doc["live"] = live_ok;
  SendJson(doc);
}

void HandleStopsUpdate() {
  NoteWebActivity();
  String error;
  if (!BuildStopIndex(error)) {
    SendJsonError(502, String("Stop list download failed (") + error + ")");
    return;
  }
  if (!BuildLineIndex(error)) {
    SendJsonError(502, String("Line data download failed (") + error + ")");
    return;
  }
  HandleState();
}

/**
 * @brief Sends one of the offline index files as is; the app parses and
 * caches it so it can search and browse without further requests.
 */
void SendIndexFile(const char* path) {
  NoteWebActivity();
  File file = SPIFFS.open(path, "r");
  if (!file) {
    SendJsonError(404, "not downloaded yet");
    return;
  }
  g_web.sendHeader("Cache-Control", "no-store");
  g_web.streamFile(file, "text/plain; charset=utf-8");
  file.close();
}

void HandleStopsData() {
  SendIndexFile(STOPS_INDEX_FILE);
}

void HandleLinesData() {
  SendIndexFile(LINES_INDEX_FILE);
}

void HandleWifiPortal() {
  NoteWebActivity();
  JsonDocument doc;
  doc["ok"] = true;
  SendJson(doc);
  // WebTask starts the portal after this response has been sent.
  g_request_wifi_portal = true;
}

void HandleNotFound() {
  if (g_web.uri().startsWith("/api/")) {
    SendJsonError(404, "not found");
    return;
  }
  g_web.sendHeader("Location", "/");
  g_web.send(302, "text/plain", "");
}

void RegisterWebRoutes() {
  g_web.on("/", HTTP_GET, HandleRoot);
  g_web.on("/api/state", HTTP_GET, HandleState);
  g_web.on("/api/ping", HTTP_GET, HandlePing);
  g_web.on("/api/profiles", HTTP_POST, HandleSaveProfiles);
  g_web.on("/api/settings", HTTP_POST, HandleSaveSettings);
  g_web.on("/api/show", HTTP_POST, HandleShow);
  g_web.on("/api/search", HTTP_GET, HandleSearch);
  g_web.on("/api/station", HTTP_GET, HandleStation);
  g_web.on("/api/stops/update", HTTP_POST, HandleStopsUpdate);
  g_web.on("/api/data/stops", HTTP_GET, HandleStopsData);
  g_web.on("/api/data/lines", HTTP_GET, HandleLinesData);
  g_web.on("/api/wifi", HTTP_POST, HandleWifiPortal);
  g_web.onNotFound(HandleNotFound);
}

/**
 * @brief Runs the setup web server while the display is awake and WiFi is up,
 * and keeps the stop search index fresh.
 */
void WebTask(void* pvParameters) {
  bool running = false;
  const unsigned long started_ms = millis();

  while (true) {
    const bool can_run = g_display_awake && !g_config_portal_open
                         && WiFi.status() == WL_CONNECTED;
    if (can_run && !running) {
      g_web.begin();
      running = true;
      g_web_server_stopped = false;
      LogInfo(F("web-started"), "http://" + WiFi.localIP().toString() + "/");
    } else if (!can_run && running) {
      g_web.stop();
      running = false;
    }
    if (!running) {
      g_web_server_stopped = true;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    g_web.handleClient();

    if (g_request_wifi_portal) {
      g_request_wifi_portal = false;
      vTaskDelay(pdMS_TO_TICKS(300));  // let the response reach the phone
      g_web.stop();
      running = false;
      g_web_server_stopped = true;
      StartRuntimeConfigPortal();
      continue;
    }

    // Download the stop list and line data on first boot and refresh them
    // about monthly.
    if (millis() - started_ms > 5000
        && (!g_index_attempted
            || millis() - g_last_index_attempt_ms > kStopIndexRetryMs)) {
      g_index_attempted = true;
      g_last_index_attempt_ms = millis();
      String error;
      if (IndexNeedsRefresh(STOPS_INDEX_FILE, 100)) {
        BuildStopIndex(error);
      }
      if (IndexNeedsRefresh(LINES_INDEX_FILE, 10)) {
        BuildLineIndex(error);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


/**
 * @brief Prints system information including free heap memory.
 */
void PrintSystemInfo() {
  static unsigned long last_log_ms = 0;
  if (millis() - last_log_ms < 60000) {
    return;
  }
  last_log_ms = millis();

  // Get the free heap memory
  uint32_t freeHeap = ESP.getFreeHeap();
  LogInfo(F("system"), "freeHeap=" + String(freeHeap)
                         + " wifi=" + String(WiFi.status()));
}


/**
 * @brief Task to monitor and handle reset button presses.
 * @param pvParameters Pointer to task-specific parameters.
 */
void ResetActionsTask(void* pvParameters) {
  // Define the button pin
  const int buttonPin = global_settings.pin_reset_button;

  // Set the button pin as an input
  pinMode(buttonPin, INPUT);
  // Initialize button state to HIGH (assuming active LOW button)
  int buttonState = HIGH;
  // Counter for consecutive LOW readings
  int consecutiveLowReads = 0;
  const int& hard_threshold = global_settings.sec_press_hard_resset_button;
  const int& soft_threshold = global_settings.sec_press_soft_resset_button;

  while (1) {
    // Read the state of the button
    buttonState = digitalRead(buttonPin);

    // Check if the button is pressed (LOW)
    if (buttonState == LOW) {
      consecutiveLowReads++;
    } else {
      if (consecutiveLowReads > soft_threshold
          && consecutiveLowReads <= hard_threshold) {
        // Perform softReset here
        LogWarn(F("soft-reset"), String(F("clearing WiFi settings")));
        WiFiManager wifiManager;
        wifiManager.resetSettings();

        ESP.restart();
      }
      consecutiveLowReads = 0;  // Reset the consecutive LOW reads counter
    }

    if (consecutiveLowReads > hard_threshold) {
      // Reset WiFi settings and restart the ESP

      LogWarn(F("hard-reset"), String(F("clearing WiFi and stop config")));
      global_settings.DeleteFile();
      WiFiManager wifiManager;
      wifiManager.resetSettings();

      ESP.restart();
    }

    // Periodic maintenance reboot, only while nobody is looking. The device
    // comes back up with the display still off.
    if (millis() >= static_cast<unsigned long>(global_settings.ms_reboot_interval)
        && !g_display_awake && !g_config_portal_open) {
      LogInfo(F("scheduled-reboot"));
      g_quiet_boot_magic = kQuietBootMagic;
      ESP.restart();
    }

    int delay_ms = global_settings.ms_task_delay_resset_button;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));  // Delay for 1 second
    PrintSystemInfo();
  }
}


/**
 * @brief Switches the display to the currently active stop profile.
 *
 * Rebuilds the renderer and traffic manager (the new profile may show a
 * different number of rows), shows cached departures or a loading screen, and
 * asks the data task to refetch immediately. With no stop configured the setup
 * screen is shown instead.
 */
void ApplyProfileChange() {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    g_profile_generation = g_profile_generation + 1;
    g_force_header_redraw = true;

    delete pTraficManager;
    pTraficManager = new TraficManager();
    delete p_screen;
    p_screen = new Screen(global_settings.GetConfig().GetLinesCountAsInt(),
                          global_settings.cnt_screen_lines_per_rows);

    if (global_settings.GetConfiguredCount() == 0) {
      DrawSetupScreen();
    } else {
      g_setup_screen_visible = false;
      tft.fillScreen(TFT_BG);
      std::vector<Monitor> cachedTraffic;
      if (GetCachedTraffic(global_settings.GetActiveIndex(), cachedTraffic)) {
        pTraficManager->update(cachedTraffic);
        pTraficManager->updateScreen();
        pTraficManager->updateScreen();
        DrawHeaderBar();
      } else {
        ShowFetchingStopScreen(global_settings.GetConfig());
      }
    }

    xSemaphoreGive(dataMutex);
  }
  g_force_refresh = true;
}


bool StartRuntimeConfigPortal() {
  if (g_config_portal_open) {
    LogWarn(F("config-portal-already-open"));
    return false;
  }
  g_config_portal_open = true;
  if (!CreateTaskChecked(RuntimeConfigPortalTask, "PortalTask", 16384, 1)) {
    g_config_portal_open = false;
    return false;
  }
  return true;
}


void RuntimeConfigPortalTask(void* pvParameters) {
  // The setup web server must release port 80 before WiFiManager takes it.
  const unsigned long start_ms = millis();
  while (!g_web_server_stopped && millis() - start_ms < 3000) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  g_setup_screen_visible = false;
  WakeDisplayIfNeeded();

  WiFi.persistent(true);  // a newly chosen network must be stored
  WiFiManagerTask(true);
  WiFi.persistent(false);
  RememberWifiLink();
  RestartMdns();

  g_force_header_redraw = true;
  g_last_touch_ms = millis();
  g_config_portal_open = false;
  ApplyProfileChange();
  vTaskDelete(NULL);
}


/**
 * @brief Handles a short tap (acted on when the finger lifts).
 */
void HandleTap(int x, int y) {
  if (g_setup_screen_visible) {
    if (IsInSetupWifiButton(x, y)) {
      LogInfo(F("touch-wifi-setup"));
      StartRuntimeConfigPortal();
    } else if (global_settings.GetConfiguredCount() > 0) {
      CloseSetupScreen();
    }
    return;
  }

  const int direction = (x >= tft.width() / 2) ? 1 : -1;  // right=next
  bool changed = false;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    changed = global_settings.CycleProfile(direction);
    xSemaphoreGive(dataMutex);
  }
  if (changed) {
    LogInfo(F("touch-switch"),
            direction > 0 ? String(F("next stop")) : String(F("previous stop")));
    ApplyProfileChange();
  }
}


/**
 * @brief Dims the backlight and later switches the display off when idle.
 */
void UpdateIdlePowerState() {
  if (!g_display_awake || g_config_portal_open) {
    return;
  }
  const DisplayPrefs prefs = global_settings.GetPrefs();
  const unsigned long now = millis();
  const unsigned long idle_touch_ms = now - g_last_touch_ms;
  const unsigned long idle_ms = std::min(idle_touch_ms, now - g_last_web_ms);

  // Without any stop the setup screen must stay reachable, so never sleep.
  if (prefs.off_after_s > 0 && idle_ms > prefs.off_after_s * 1000UL
      && global_settings.GetConfiguredCount() > 0) {
    if (g_setup_screen_visible) {
      CloseSetupScreen();
    }
    SetDisplayAwake(false);
    return;
  }

  if (prefs.dim_after_s > 0 && !g_backlight_dimmed
      && idle_touch_ms > prefs.dim_after_s * 1000UL) {
    g_backlight_dimmed = true;
    BacklightSetPct(prefs.DimmedPct());
    LogInfo(F("display-dim"));
  }
}


/**
 * @brief Watches the touch panel.
 *
 * - Tap right/left half: next/previous stop.
 * - Hold ~1 s anywhere: setup screen (QR code for the setup web app).
 * - A touch that wakes or un-dims the display does nothing else.
 *
 * @param pvParameters Pointer to task-specific parameters (unused).
 */
void TouchTask(void* pvParameters) {
  bool pressed = false;
  bool tap_allowed = false;
  bool long_press_done = false;
  unsigned long press_start_ms = 0;
  int press_x = 0;
  int press_y = 0;
  int release_reads = 0;

  while (true) {
    UpdateIdlePowerState();

    const bool touched = ts.touched();
    const unsigned long now = millis();
    if (touched) {
      release_reads = 0;
      g_last_touch_ms = now;
      if (!pressed) {
        pressed = true;
        long_press_done = false;
        press_start_ms = now;
        tap_allowed = !g_ignore_touch_until_release && !WakeDisplayIfNeeded();
        if (tap_allowed) {
          TS_Point p = ts.getPoint();
          // Map raw touch values onto screen pixels (rough, but plenty to
          // tell the halves and the corner button apart).
          press_x = constrain(map(p.x, TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, tft.width()),
                              0, tft.width());
          press_y = constrain(map(p.y, TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, tft.height()),
                              0, tft.height());
        }
      } else if (!long_press_done && g_display_awake && !g_config_portal_open
                 && now - press_start_ms
                      >= static_cast<unsigned long>(global_settings.ms_long_press)) {
        long_press_done = true;
        tap_allowed = false;
        if (!g_setup_screen_visible) {
          LogInfo(F("touch-long-press"));
          ShowSetupScreen();
        }
      }
    } else if (release_reads < 2 && ++release_reads == 2) {
      // Two untouched reads in a row: the finger really lifted.
      g_ignore_touch_until_release = false;
      if (pressed) {
        pressed = false;
        if (tap_allowed && !g_config_portal_open) {
          HandleTap(press_x, press_y);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(g_display_awake ? 30 : 25));
  }
}


/**
 * @brief Setup function for initializing the application.
 */
void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  delay(200);

  // After the scheduled maintenance reboot, come back with the display off.
  const bool quiet_boot = esp_reset_reason() == ESP_RST_SW
                          && g_quiet_boot_magic == kQuietBootMagic;
  g_quiet_boot_magic = 0;
  LogInfo(F("app-start"), quiet_boot ? String(F("quiet")) : String());

  setCpuFrequencyMhz(global_settings.mhz_cpu_awake);
  dataMutex = xSemaphoreCreateMutex();

  // Initialize SPIFFS (SPI Flash File System)
  if (!SPIFFS.begin(true)) {
    LogError(F("spiffs-mount-failed"), String(F("setup")));
    delay(global_settings.ms_error_resset_delay);
    ESP.restart();  // Restart the ESP32
  }

  // Initialize TFT display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BG);
  BacklightBegin();
  BacklightSetPct(quiet_boot ? 0 : global_settings.GetPrefs().brightness_pct);
  g_last_touch_ms = millis();

  // Initialize the XPT2046 touch controller on its own SPI bus (CYD board).
  pinMode(XPT2046_IRQ, INPUT);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // Create a task for reading the reset button state
  CreateTaskChecked(ResetActionsTask, "ResetTask", 4096, 1);

  // Connect to WiFi (opens the Wi-Fi setup hotspot if needed).
  WiFiManagerTask();
  // From here on reconnects must not rewrite the stored credentials.
  WiFi.persistent(false);

  pTraficManager = new TraficManager;
  p_screen = new Screen(global_settings.GetConfig().GetLinesCountAsInt(),
                        global_settings.cnt_screen_lines_per_rows);

  // Check if WiFi is successfully connected
  if (WiFi.status() != WL_CONNECTED) {
    LogError(F("wifi-not-connected"));
    delay(global_settings.ms_error_resset_delay);
    ESP.restart();  // Restart the ESP32
  }
  LogInfo(F("wifi-connected"), WiFi.localIP().toString());
  RememberWifiLink();
  RestartMdns();
  ConfigureClock();
  RegisterWebRoutes();

  if (global_settings.GetConfiguredCount() == 0) {
    ShowSetupScreen();
  } else {
    tft.fillScreen(TFT_BG);
    if (quiet_boot) {
      SetDisplayAwake(false);
    }
  }

  // Create tasks for data updating and screen updating
  bool data_task_ok =
    CreateTaskChecked(UpdateDataTask, "DataTask", 16384, 2);
  bool screen_task_ok =
    CreateTaskChecked(ScreenUpdateTask, "ScreenTask", 12288, 1);
  // Touch: stop switching, long press for setup, waking the display.
  bool touch_task_ok = CreateTaskChecked(TouchTask, "TouchTask", 8192, 1);
  // Setup web app and stop search index.
  bool web_task_ok = CreateTaskChecked(WebTask, "WebTask", 16384, 1);

  if (!data_task_ok || !screen_task_ok || !touch_task_ok || !web_task_ok) {
    ShowStatusScreen("Task start failed", "Check serial log");
  } else {
    LogInfo(F("tasks-started"));
  }
}


/**
 * @brief Main loop function (not actively used in this application).
 */
void loop() {
  // The application is task-based; just keep this task out of the way.
  vTaskDelay(portMAX_DELAY);
}
