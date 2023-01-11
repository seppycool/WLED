#include "wled.h"

/*
 * Usermods allow you to add own functionality to WLED more easily
 * See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
 * 
 * This is Stairway-Wipe as a v2 usermod.
 * 
 * Using this usermod:
 * 1. Copy the usermod into the sketch folder (same folder as wled00.ino)
 * 2. Register the usermod by adding #include "stairway-wipe-usermod-v2.h" in the top and registerUsermod(new StairwayWipeUsermod()) in the bottom of usermods_list.cpp
 */

class StairwayWipeUsermod : public Usermod {
  private:
     /* configuration (available in API and stored in flash) */
    bool enabled = false;                   // Enable this usermod
    unsigned long on_time_ms       = 10000; // The time for the light to stay on
    int8_t topPIRorTriggerPin      = 16;    // disabled
    int8_t bottomPIRorTriggerPin   = 17;    // disabled

    /* runtime variables */
    bool initDone = false;

    // Time between checking of the sensors
    const unsigned int scanDelay = 100;

    // Lights on or off.
    // Flipping this will start a transition.
    bool on = false;

    // Swipe direction for current transition
  #define SWIPE_UP true
  #define SWIPE_DOWN false
    bool swipe = SWIPE_UP;

    // Indicates which Sensor was seen last (to determine
    // the direction when swiping off)
  #define LOWER false
  #define UPPER true
    bool lastSensor = LOWER;

    // Time of the last sensor check
    unsigned long lastScanTime = 0;

    // Last time the lights were switched on or off
    unsigned long lastSwitchTime = 0;

    // These values are used by the API to read the
    // last sensor state, or trigger a sensor
    // through the API
    bool topSensorRead     = false;
    bool topSensorWrite    = false;
    bool bottomSensorRead  = false;
    bool bottomSensorWrite = false;
    bool topSensorState    = false;
    bool bottomSensorState = false;

    byte wipeState = 0; //0: inactive 1: wiping 2: solid
    unsigned long timeStaticStart = 0;
    uint16_t previousUserVar0 = 0;  

    static const char _name[];
    static const char _enabled[];
    static const char _onTime[];
    static const char _topPIRorTrigger_pin[];
    static const char _bottomPIRorTrigger_pin[];

    void publishMqtt(bool bottom, const char* state) {
      //Check if MQTT Connected, otherwise it will crash the 8266
      if (WLED_MQTT_CONNECTED){
        char subuf[64];
        sprintf_P(subuf, PSTR("%s/motion/%d"), mqttDeviceTopic, (int)bottom);
        mqtt->publish(subuf, 0, false, state);
      }
    }
#define STAIRCASE_WIPE_OFF
    void turnOff()
    {
    #ifdef STAIRCASE_WIPE_OFF
    transitionDelayTemp = 0; //turn off immediately after wipe completed
    #else
    transitionDelayTemp = 4000; //fade out slowly
    #endif
    bri = 0;
    stateUpdated(CALL_MODE_NOTIFICATION);
    wipeState = 0;
    userVar0 = 0;
    previousUserVar0 = 0;
    }

    bool checkSensors() {
      bool sensorChanged = false;

      if ((millis() - lastScanTime) > scanDelay) {
        lastScanTime = millis();

        bottomSensorRead = digitalRead(bottomPIRorTriggerPin);
        topSensorRead = digitalRead(topPIRorTriggerPin);

        if (bottomSensorRead != bottomSensorState) {
          bottomSensorState = bottomSensorRead; // change previous state
          sensorChanged = true;
          publishMqtt(true, bottomSensorState ? "on" : "off");
        }

        if (topSensorRead != topSensorState) {
          topSensorState = topSensorRead; // change previous state
          sensorChanged = true;
          publishMqtt(false, topSensorState ? "on" : "off");
        }

        // Values read, reset the flags for next API call
        topSensorWrite = false;
        bottomSensorWrite = false;

        if (topSensorRead != bottomSensorRead) {
          lastSwitchTime = millis();

          if (on) {
            lastSensor = topSensorRead;
          } else {
            // If the bottom sensor triggered, we need to swipe up, ON
            swipe = bottomSensorRead;

            on = true;
          }
        }
      }
      return sensorChanged;
    }
//comment this out if you want the turn off effect to be just fading out instead of reverse wipe
    void updateSwipe() {
      if(bottomSensorRead) userVar0 = 1;
      else if(topSensorRead) userVar0 = 2;

      if (userVar0 > 0)
      {
        if ((previousUserVar0 == 1 && userVar0 == 2) || (previousUserVar0 == 2 && userVar0 == 1)) wipeState = 3; //turn off if other PIR triggered
        previousUserVar0 = userVar0;
      
        if (wipeState == 0) {
          startWipe();
          wipeState = 1;
        } else if (wipeState == 1) { //wiping
          uint32_t cycleTime = 360 + (255 - effectSpeed)*75; //this is how long one wipe takes (minus 25 ms to make sure we switch in time)
          if (millis() + strip.timebase > (cycleTime - 25)) { //wipe complete
            effectCurrent = FX_MODE_STATIC;
            timeStaticStart = millis();
            colorUpdated(CALL_MODE_NOTIFICATION);
            wipeState = 2;
          }
        } else if (wipeState == 2) { //static
          if (on_time_ms > 0) //if U1 is not set, the light will stay on until second PIR or external command is triggered
          {
            if (millis() - timeStaticStart > on_time_ms) wipeState = 3;
          }
        } else if (wipeState == 3) { //switch to wipe off
          #ifdef STAIRCASE_WIPE_OFF
          effectCurrent = FX_MODE_COLOR_WIPE;
          strip.timebase = 360 + (255 - effectSpeed)*75 - millis(); //make sure wipe starts fully lit
          colorUpdated(CALL_MODE_NOTIFICATION);
          wipeState = 4;
          #else
          turnOff();
          #endif
        } else { //wiping off
          if (millis() + strip.timebase > (725 + (255 - effectSpeed)*150)) turnOff(); //wipe complete
        }
      } else {
        wipeState = 0; //reset for next time
        if (previousUserVar0) {
          #ifdef STAIRCASE_WIPE_OFF
          userVar0 = previousUserVar0;
          wipeState = 3;
          #else
          turnOff();
          #endif
        }
      previousUserVar0 = 0;
      }
    }

    // send sesnor values to JSON API
    void writeSensorsToJson(JsonObject& staircase) {
      staircase[F("top-sensor")]    = topSensorRead;
      staircase[F("bottom-sensor")] = bottomSensorRead;
    }

    // allow overrides from JSON API
    void readSensorsFromJson(JsonObject& staircase) {
      bottomSensorWrite = bottomSensorState || (staircase[F("bottom-sensor")].as<bool>());
      topSensorWrite    = topSensorState    || (staircase[F("top-sensor")].as<bool>());
    }

    void enable(bool enable) {
      enabled = enable;
    }


  public:

  void setup(){
      //     // standardize invalid pin numbers to -1
      // if (topPIRorTriggerPin    < 0) topPIRorTriggerPin    = -1;
      // if (bottomPIRorTriggerPin < 0) bottomPIRorTriggerPin = -1;
      // // allocate pins
      // PinManagerPinType pins[2] = {
      //   { topPIRorTriggerPin, false },
      //   { bottomPIRorTriggerPin, false },
      // };
      // // NOTE: this *WILL* return TRUE if all the pins are set to -1.
      // //       this is *BY DESIGN*.
      // if (!pinManager.allocateMultiplePins(pins, 2, PinOwner::UM_AnimatedStaircase)) {
      //   topPIRorTriggerPin = -1;
      //   bottomPIRorTriggerPin = -1;
      //   enabled = false;
      // }
    enable(enabled);
      initDone = true;
  }

  void loop() {
    if (!enabled) return;
    //userVar0 (U0 in HTTP API):
    //has to be set to 1 if movement is detected on the PIR that is the same side of the staircase as the ESP8266
    //has to be set to 2 if movement is detected on the PIR that is the opposite side
    //can be set to 0 if no movement is detected. Otherwise LEDs will turn off after a configurable timeout (userVar1 seconds)

    checkSensors();
    updateSwipe();
    
  }

  uint16_t getId() { return USERMOD_ID_ANIMATED_STAIRCASE; }



  void addToJsonState(JsonObject& root) {
      JsonObject staircase = root[FPSTR(_name)];
      if (staircase.isNull()) {
        staircase = root.createNestedObject(FPSTR(_name));
      }
      writeSensorsToJson(staircase);
      DEBUG_PRINTLN(F("Staircase sensor state exposed in API."));
    }

    /*
    * Reads configuration settings from the json API.
    * See void addToJsonState(JsonObject& root)
    */
    void readFromJsonState(JsonObject& root) {
       userVar0 = root["user0"] | userVar0; //if "user0" key exists in JSON, update, else keep old value
      if (!initDone) return;  // prevent crash on boot applyPreset()
      bool en = enabled;
      JsonObject staircase = root[FPSTR(_name)];
      if (!staircase.isNull()) {
        if (staircase[FPSTR(_enabled)].is<bool>()) {
          en = staircase[FPSTR(_enabled)].as<bool>();
        } else {
          String str = staircase[FPSTR(_enabled)]; // checkbox -> off or on
          en = (bool)(str!="off"); // off is guaranteed to be present
        }
        if (en != enabled) enable(en);
        readSensorsFromJson(staircase);
        DEBUG_PRINTLN(F("Staircase sensor state read from API."));
      }
    }

    void appendConfigData() {
      //oappend(SET_F("dd=addDropdown('staircase','selectfield');"));
      //oappend(SET_F("addOption(dd,'1st value',0);"));
      //oappend(SET_F("addOption(dd,'2nd value',1);"));
      //oappend(SET_F("addInfo('staircase:selectfield',1,'additional info');"));  // 0 is field type, 1 is actual field
    }


    /*
    * Writes the configuration to internal flash memory.
    */
    void addToConfig(JsonObject& root) {
      JsonObject staircase = root[FPSTR(_name)];
      if (staircase.isNull()) {
        staircase = root.createNestedObject(FPSTR(_name));
      }
      staircase[FPSTR(_enabled)]                   = enabled;
      staircase[FPSTR(_onTime)]                    = on_time_ms / 1000;
      staircase[FPSTR(_topPIRorTrigger_pin)]       = topPIRorTriggerPin;
      staircase[FPSTR(_bottomPIRorTrigger_pin)]    = bottomPIRorTriggerPin;
      DEBUG_PRINTLN(F("Staircase config saved."));
    }

    /*
    * Reads the configuration to internal flash memory before setup() is called.
    * 
    * The function should return true if configuration was successfully loaded or false if there was no configuration.
    */
    bool readFromConfig(JsonObject& root) {
      int8_t oldTopAPin = topPIRorTriggerPin;
      int8_t oldBottomAPin = bottomPIRorTriggerPin;

      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) {
        DEBUG_PRINT(FPSTR(_name));
        DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
        return false;
      }

      enabled   = top[FPSTR(_enabled)] | enabled;

      on_time_ms = top[FPSTR(_onTime)] | on_time_ms/1000;
      on_time_ms = min(900,max(10,(int)on_time_ms)) * 1000; // min 10s, max 15min

      topPIRorTriggerPin = top[FPSTR(_topPIRorTrigger_pin)] | topPIRorTriggerPin;
      bottomPIRorTriggerPin = top[FPSTR(_bottomPIRorTrigger_pin)] | bottomPIRorTriggerPin;

      DEBUG_PRINT(FPSTR(_name));
      if (!initDone) {
        // first run: reading from cfg.json
        DEBUG_PRINTLN(F(" config loaded."));
      } else {
        // changing parameters from settings page
        DEBUG_PRINTLN(F(" config (re)loaded."));
        bool changed = false;
        if ((oldTopAPin != topPIRorTriggerPin) ||
            (oldBottomAPin != bottomPIRorTriggerPin)) {
          changed = true;
          //pinManager.deallocatePin(oldTopAPin, PinOwner::UM_AnimatedStaircase);
          //pinManager.deallocatePin(oldBottomAPin, PinOwner::UM_AnimatedStaircase);
        }
        if (changed) setup();
      }
      // use "return !top["newestParameter"].isNull();" when updating Usermod with new features
      return true;
    }

    /*
    * Shows the delay between steps and power-off time in the "info"
    * tab of the web-UI.
    */
    void addToJsonInfo(JsonObject& root) {
      JsonObject user = root["u"];
      if (user.isNull()) {
        user = root.createNestedObject("u");
      }

      JsonArray infoArr = user.createNestedArray(FPSTR(_name));  // name

      String uiDomString = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
      uiDomString += FPSTR(_name);
      uiDomString += F(":{");
      uiDomString += FPSTR(_enabled);
      uiDomString += enabled ? F(":false}});\">") : F(":true}});\">");
      uiDomString += F("<i class=\"icons ");
      uiDomString += enabled ? "on" : "off";
      uiDomString += F("\">&#xe08f;</i></button>");
      infoArr.add(uiDomString);
    }

    void startWipe()
    {
    bri = briLast; //turn on
    transitionDelayTemp = 0; //no transition
    effectCurrent = FX_MODE_COLOR_WIPE;
    resetTimebase(); //make sure wipe starts from beginning

    //set wipe direction
    Segment& seg0 = strip.getSegment(0);
    Segment& seg1 = strip.getSegment(1);
    bool doReverse = (userVar0 == 2);
    seg0.setOption(1, doReverse);
    seg1.setOption(1, doReverse);

    colorUpdated(CALL_MODE_NOTIFICATION);
    }

    



   //More methods can be added in the future, this example will then be extended.
   //Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};

const char StairwayWipeUsermod::_name[]                      PROGMEM = "staircaseSVH";
const char StairwayWipeUsermod::_enabled[]                   PROGMEM = "enabled";
const char StairwayWipeUsermod::_onTime[]                    PROGMEM = "on-time-s";
const char StairwayWipeUsermod::_topPIRorTrigger_pin[]       PROGMEM = "topPIRorTrigger_pin (16)";
const char StairwayWipeUsermod::_bottomPIRorTrigger_pin[]    PROGMEM = "bottomPIRorTrigger_pin( 17)";
