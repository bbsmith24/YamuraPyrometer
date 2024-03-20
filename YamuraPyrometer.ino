/*
  YamuraLog Recording Tire Pyrometer
  4" TFT display with ST7796 SPI driver
  By: Brian Smith
  Yamura Electronics Division
  Date: September 2023
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware License).

  Hardware
  3 buttons - menu select/record temp/next tire temp review
              menu down/exit tire temp review
              menu up
  On/Off switch (battery, always on when charging on USB)
  LED/resistor for stable temp indicator
  Sparkfun ESP32 Thing Plus
  Sparkfun K type thermocouple amplifier MPC9600                         (I2C address 0x60)
  Sparkfun QWIIC RTC                                                     (I2C address 0x69)
  Hosyond 4" TFT/ST7796 SPI Module
  1200mAh LiON battery
  microSD card (8GB is fine, not huge amount of data being stored)
  misc headers
  3D printed box

  records to microSD card
  setup file on microSD card
  wifi interface for display, up/down load (to add)
*/
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>            // https://github.com/Bodmer/TFT_eSPI Graphics and font library for ST7735 driver chip
#include "Free_Fonts.h"          // Include the header file attached to this sketch
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <SparkFun_MCP9600.h>    // thermocouple amplifier
#include "RTClib.h"   // PCF8563 RTC library https://github.com/adafruit/RTClib

// car info structure
struct CarSettings
{
    String carName;
    String dateTime;
    int tireCount;
    String* tireShortName;
    String* tireLongName;
    int positionCount;
    String* positionShortName;
    String* positionLongName;
    float* maxTemp;
};
struct MenuChoice
{
  String description;
  int result;
};
// device settings structure
struct DeviceSettings
{
  char ssid[32] = "Yamura-Pyrometer";
  char pass[32] = "ZoeyDora48375";
  int screenRotation = 1;
  bool tempUnits = false; // true for C, false for F
  bool is12Hour = true;   // true for 12 hour clock, false for 24 hour clock
  int fontPoints = 12;    // size of font to use for display
};
// function prototypes
void TestMenu();
void DisplayMenu();
int MeasureTireTemps(int tire);
void InstantTemp();
void DrawGrid(int tireCount);
void SetupGrid(int fontHeight);
void DisplayAllTireTemps(CarSettings currentResultCar);
void MeasureAllTireTemps();
int GetNextTire(int selTire, int nextDirection);
void DrawCellText(int row, int col, char* text, uint16_t textColor, uint16_t backColor);
int MenuSelect(int fontSize, MenuChoice choices[], int menuCount, int initialSelect);
void SelectCar();
void ChangeSettings();
void Select12or24();
void SelectFontSize();
void RotateDisplay(bool rotateButtons);
void SetUnits();
void SetDateTime();
void DeleteDataFile(bool verify = true);
bool CheckTempStable(float curTemp);
void ResetTempStable();
void ReadCarSetupFile(fs::FS &fs, const char * path);
void WriteCarSetupFile(fs::FS &fs, const char * path);
void ReadDeviceSetupFile(fs::FS &fs, const char * path);
void WriteDeviceSetupFile(fs::FS &fs, const char * path);
void DisplaySelectedResults(fs::FS &fs, const char * path);
void WriteResultsHTML();
void ParseResult(char buf[], CarSettings &currentResultCar);
void ReadLine(File file, char* buf);
void AppendFile(fs::FS &fs, const char * path, const char * message);
void DeleteFile(fs::FS &fs, const char * path);
void handleRoot(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);
void onServoInputWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
String GetStringTime();
String GetStringDate();
void CheckButtons(unsigned long curTime);
void YamuraBanner();
void SetFont(int fontSize);

// uncomment for debug to serial monitor (use #ifdef...#endif around debug output prints)
//#define DEBUG_VERBOSE
//#define DEBUG_EXTRA_VERBOSE
//#define DEBUG_HTML
//#define SET_TO_SYSTEM_TIME
// microSD chip select, I2C pins
#define sd_CS 5
#define I2C_SDA 21
#define I2C_SCL 22
// uncomment for RTC module attached
#define HAS_RTC
// uncomment for thermocouple module attached (use random values for test if not attached)
#define HAS_THERMO
// uncomment to write setup files
//#define WRITE_INI
// max menu item count
#define MAX_MENU_ITEMS 100
// main menu
#define DISPLAY_MENU            0
#define SELECT_CAR              1
#define MEASURE_TIRES           2
#define DISPLAY_TIRES           3
#define DISPLAY_SELECTED_RESULT 4
#define CHANGE_SETTINGS         5
#define INSTANT_TEMP            6
#define TEST_MENU               7
// settings menu
#define SET_DATETIME 0
#define SET_TEMPUNITS 1
#define SET_FLIPDISPLAY 2
#define SET_FONTSIZE 3
#define SET_12H24H 4
#define SET_DELETEDATA 5
#define SET_IPADDRESS 6
#define SET_PASS 7
#define SET_SAVESETTINGS 8
#define SET_EXIT 9
// font size menu
#define FONTSIZE_9 0
#define FONTSIZE_12 1
#define FONTSIZE_18 2
#define FONTSIZE_24 3
// 12/24 hour menu
#define HOURS_12 0
#define HOURS_24 1
// index to date/time value array
#define DATE          0
#define MONTH         1
#define YEAR          2
#define DAYOFWEEK     3
#define HOUR          4
#define MINUTE        5
#define SECOND        6
#define HUNDSEC       7
// user inputs
#define BUTTON_COUNT 3
#define BUTTON_1     0
#define BUTTON_2     1
#define BUTTON_3     2
#define BUTTON_RELEASED 0
#define BUTTON_PRESSED  1
#define BUTTON_DEBOUNCE_DELAY   20   // ms
// button debounce structure
struct UserButton
{
  int buttonPin = 0;
  bool buttonReleased = false;
  bool buttonPressed =  false;
  byte buttonLast =     BUTTON_RELEASED;
  unsigned long pressDuration = 0;
  unsigned long releaseDuration = 0;
  unsigned long lastChange = 0;
};
UserButton buttons[BUTTON_COUNT];

// car list from setup file
CarSettings* cars;
// device settings from file
DeviceSettings deviceSettings;

// tire temp array
float tireTemps[18];
float currentTemps[18];

// devices
// thermocouple amplifier
MCP9600 tempSensor;
// rtc
RTC_PCF8563 rtc;

bool century = false;
bool h12Flag;
bool pmFlag;
String days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri","Sat"};
String ampmStr[3] = {"am", "pm", "\0"};

// TFT display
TFT_eSPI tftDisplay = TFT_eSPI();
int fontHeight;

int tempIdx = 0;
float tempStableRecent[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
float tempStableMinMax[2] = {150.0, -150.0};
bool tempStable = false;

int carCount = 0;
int selectedCar = 0;
int tireIdx = 0;
int measIdx = 0;
float tempRes = 1.0;
int deviceState = 0;

IPAddress IP;
AsyncWebServer server(80);
AsyncWebSocket wsServoInput("/ServoInput");
String htmlStr;

//
// grid lines for temp measure/display
//
int gridLineH[4][2][2];   //  4 vertical lines, 2 points per line, 2 values per point (X and Y)
int gridLineV[3][2][2];   //  3 vertical lines, 2 points per line, 2 values per point (X and Y)
int cellPoint[7][6][2];   //  6 max rows, 6 points per cell, 2 values per point (X and Y)
//
//
//
void setup()
{
  char outStr[128];
  Serial.begin(115200);
  // location of text
  int textPosition[2] = {5, 0};

  // setup buttons on SX1509
  buttons[0].buttonPin = 12;
  buttons[1].buttonPin = 14;
  buttons[2].buttonPin = 26;
  //buttons[3].buttonPin = 27;
  // set up tft display
  tftDisplay.init();
  RotateDisplay(false);  
  int w = tftDisplay.width();
  int h = tftDisplay.height();
  textPosition[0] = 5;
  textPosition[1] = 0;
    // 0 portrait pins down
  // 1 landscape pins right
  // 2 portrait pins up
  // 3 landscape pins left
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  SetFont(deviceSettings.fontPoints);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString("Yamura Electronics Recording Pyrometer", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  //
  // start I2C
  int sda = I2C_SDA;
  int scl = I2C_SCL;
  pinMode(sda, OUTPUT);
  pinMode(scl, OUTPUT);
  Wire.setPins(sda, scl);
  Wire.begin();
  Wire.setClock(100000);
  delay(5000);

  tempSensor.begin();       // Uses the default address (0x60) for SparkFun Thermocouple Amplifier
  //else 
  while(!tempSensor.isConnected())
  {
    tftDisplay.drawString("Thermocouple FAIL", textPosition[0], textPosition[1], GFXFF);
    delay(5000);
  }
  //check if the sensor is connected
  tftDisplay.drawString("Thermocouple OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  delay(1000);
  //check if the Device ID is correct
  if(!tempSensor.checkDeviceID())
  {
    while(1);
  }
  //change the thermocouple type being used
  switch(tempSensor.getThermocoupleType())
  {
    case 0b000:
      tftDisplay.drawString("K Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b001:
      tftDisplay.drawString("J Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b010:
      tftDisplay.drawString("T Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b011:
      tftDisplay.drawString("N Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b100:
      tftDisplay.drawString("S Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b101:
      tftDisplay.drawString("E Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b110:
      tftDisplay.drawString("B Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    case 0b111:
      tftDisplay.drawString("R Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
    default:
      tftDisplay.drawString("Unknown Thermocouple type", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      break;
  }
  if(tempSensor.getThermocoupleType() != TYPE_K)
  {
    tftDisplay.drawString("Setting to K Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
    textPosition[1] += fontHeight;
    tempSensor.setThermocoupleType(TYPE_K);
    //make sure the type was set correctly!
    if(!tempSensor.getThermocoupleType() == TYPE_K)
    {
      tftDisplay.drawString("Failed to set to K Type Thermocouple", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
    }
  }
  switch(tempSensor.getAmbientResolution())
  {
    case RES_ZERO_POINT_0625:
      tempRes = 0.1125;
      break;
    case RES_ZERO_POINT_25:
      tempRes = 0.45;
      break;
    default:
      tempRes = 0.45;
      break;
  }
  #ifdef HAS_RTC
  while (!rtc.begin()) 
  {
    Serial.println("Couldn't find RTC...retry");
    Serial.flush();
    delay(1000);
  }
  #ifdef SET_TO_SYSTEM_TIME
  Serial.println("Set date and time to system");
  delay(5000);
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  #endif

  if (rtc.lostPower()) 
  {
    delay(5000);
    Serial.println("RTC is NOT initialized, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    //
    // Note: allow 2 seconds after inserting battery or applying external power
    // without battery before calling adjust(). This gives the PCF8523's
    // crystal oscillator time to stabilize. If you call adjust() very quickly
    // after the RTC is powered, lostPower() may still return true.
  }
  else
  {
    Serial.println("RTC initialized, time already set");
  }
  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  rtc.start();
  tftDisplay.drawString("RTC OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #endif

  for(int idx = 0; idx < BUTTON_COUNT; idx++)
  {
    pinMode(buttons[idx].buttonPin, INPUT_PULLUP);
  }

  if(!SD.begin(sd_CS))
  {
    tftDisplay.drawString("microSD card mount failed", textPosition[0], textPosition[1], GFXFF);
    while(true);
  }
  tftDisplay.drawString("microSD OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE)
  {
    return;
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  // uncomment to write a default setup file
  // maybe check for setup and write one if needed?
  #ifdef WRITE_INI
  DeleteFile(SD, "/py_cars.txt");
  WriteCarSetupFile(SD, "/py_cars.txt");
  WriteDeviceSetupFile(SD, "/py_setup.txt");
  #endif
  ReadCarSetupFile(SD,  "/py_cars.txt");
  ReadDeviceSetupFile(SD,  "/py_setup.txt");

  ResetTempStable();

  #ifdef HAS_RTC
  // get time from RTC
  sprintf(outStr, "%s %s", GetStringTime().c_str(), GetStringDate().c_str());
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #endif
  deviceState = DISPLAY_MENU;

  WriteResultsHTML();
  WiFi.softAP(deviceSettings.ssid, deviceSettings.pass);
  IP = WiFi.softAPIP();
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  wsServoInput.onEvent(onServoInputWebSocketEvent);
  server.addHandler(&wsServoInput);
  server.begin();
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  wsServoInput.onEvent(onServoInputWebSocketEvent);
  server.addHandler(&wsServoInput);
  server.begin();
  sprintf(outStr, "IP %d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  sprintf(outStr, "Password %s", deviceSettings.pass);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;

  RotateDisplay(deviceSettings.screenRotation != 1);
  SetupGrid(deviceSettings.fontPoints);

  delay(5000);
}
//
//print the thermocouple, ambient and delta temperatures every 200ms if available
//
void loop()
{
  switch (deviceState)
  {
    case DISPLAY_MENU:
      DisplayMenu();
      break;
    case SELECT_CAR:
      SelectCar();
      deviceState = DISPLAY_MENU;
      break;
    case MEASURE_TIRES:
      MeasureAllTireTemps();
      deviceState = DISPLAY_MENU;
      break;
    case DISPLAY_TIRES:
      DisplayAllTireTemps(cars[selectedCar]);
      deviceState = DISPLAY_MENU;
      break;
    case DISPLAY_SELECTED_RESULT:
      char outStr[128];
      sprintf(outStr, "/py_temps_%d.txt", selectedCar);
      DisplaySelectedResults(SD, outStr);
      deviceState = DISPLAY_MENU;
      break;
    case CHANGE_SETTINGS:
      ChangeSettings();
      deviceState = DISPLAY_MENU;
      break;
    case INSTANT_TEMP:
      InstantTemp();
      deviceState = DISPLAY_MENU;
      break;
    case TEST_MENU:
      TestMenu();
      deviceState = DISPLAY_MENU;
      break;
    default:
      break;
  }
}
//
//
//
void TestMenu()
{
  int menuCount = 12;
  MenuChoice testChoices[12];
  testChoices[ 0].description = "Test  1";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 1].description = "Test  2";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 2].description = "Test  3";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 3].description = "Test  4";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 4].description = "Test  5";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 5].description = "Test  6";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 6].description = "Test  7";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 7].description = "Test  8";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 8].description = "Test  9";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[ 9].description = "Test 10";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[10].description = "Test 11";                   testChoices[0].result = MEASURE_TIRES;
  testChoices[11].description = "Test 12";                   testChoices[0].result = MEASURE_TIRES;
  deviceState =  MenuSelect(deviceSettings.fontPoints, testChoices, menuCount, MEASURE_TIRES); 
}
//
//
//
void DisplayMenu()
{
  int menuCount = 6;
  MenuChoice mainMenuChoices[6];  
  mainMenuChoices[0].description = "Measure Temps";                   mainMenuChoices[0].result = MEASURE_TIRES;
  mainMenuChoices[1].description = cars[selectedCar].carName.c_str(); mainMenuChoices[1].result = SELECT_CAR;
  mainMenuChoices[2].description = "Display Temps";                   mainMenuChoices[2].result = DISPLAY_TIRES;
  mainMenuChoices[3].description = "Instant Temp";                    mainMenuChoices[3].result = INSTANT_TEMP;
  mainMenuChoices[4].description = "Display Selected Results";        mainMenuChoices[4].result = DISPLAY_SELECTED_RESULT;
  mainMenuChoices[5].description = "Settings";                        mainMenuChoices[5].result = CHANGE_SETTINGS;
  //mainMenuChoices[6].description = "Test menu";                       mainMenuChoices[6].result = TEST_MENU;
  
  deviceState =  MenuSelect(deviceSettings.fontPoints, mainMenuChoices, menuCount, MEASURE_TIRES); 
}
//
// measure temperatures on a single tire
// called by MeasureAllTireTemps
//
int MeasureTireTemps(int tireIdx)
{
  char outStr[512];
  // location of text
  int textPosition[2] = {5, 0};
  int row = 0;
  int col = 0;
  bool armed = false;
  // measure location - O, M, I
  measIdx = 0;  
  // reset tire temps to 0.0
  for(int idx = 0; idx < cars[selectedCar].positionCount; idx++)
  {
    tireTemps[(tireIdx * cars[selectedCar].positionCount) + idx] = 0.0;
  }
  ResetTempStable();
  armed = false;
  unsigned long priorTime = millis();
  unsigned long curTime = millis();
  // text position on OLED screen
  // measuring until all positions are measured
  while(true)
  {
    // get time and process buttons for press/release
    curTime = millis();
    CheckButtons(curTime);
    // button 1 release arms probe
    // prior to arm, display ****, after display temp as it stabilizes
    if (buttons[0].buttonReleased)
    {
      armed = true;
      buttons[0].buttonReleased = false;
    }
    // only before measure starts, back/forward button returns without starting measurement
    for(int btnIdx = 1; btnIdx < BUTTON_COUNT; btnIdx++)
    {
      if ((buttons[btnIdx].buttonReleased) && (measIdx == 0) && (!armed))
      {
        row = ((tireIdx / 2) * 2) + 1;
        col = measIdx + ((tireIdx % 2) * 3);
        DrawCellText(row, 
                     col, 
                     "****", 
                     TFT_BLACK, 
                     TFT_BLACK);
        buttons[btnIdx].buttonReleased = false;
        if(btnIdx == 1)
        {
          return 1;
        }
        else
        {
          return -1;
        }
      }
    }
    // check for stable temp and armed
    if(tempStable)
    {
      // armed, save the temp and go to next position
      if(armed)
      {
        measIdx++;
        // done measuring
        if(measIdx == cars[selectedCar].positionCount)
        {
          measIdx = 0;
          break;
        }
        tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = 0;
        ResetTempStable();
        armed = false;
      }
    }
    // if not stablized. sample temp every .250 second, check for stable temp
    if(!tempStable && (curTime - priorTime) > 250)
    {
      priorTime = curTime;
      curTime = millis();
      // read temp, check for stable temp, light LED if stable
      if(armed)
      {
        #ifdef HAS_THERMO
        tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = tempSensor.getThermocoupleTemp(deviceSettings.tempUnits); // false for F, true or empty for C
        #else
        tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = 100;
        #endif
        tempStable = CheckTempStable(tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx]);
      }
      for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
      {
        sprintf(outStr, " ");
        if((tirePosIdx == measIdx) && (!armed))
        {
          sprintf(outStr, "****");
        }
        if(tireTemps[(tireIdx * cars[selectedCar].positionCount) + tirePosIdx] > 0.0)
        {
          sprintf(outStr, "%3.1F", tireTemps[(cars[selectedCar].positionCount * tireIdx) + tirePosIdx]);
        }
        row = ((tireIdx / 2) * 2) + 1;
        col = tirePosIdx + ((tireIdx % 2) * 3);
        DrawCellText(row, 
                     col, 
                     outStr, 
                     TFT_WHITE, 
                     TFT_BLACK);
      }
    }
  }
  return 1;
}
///
///
///
void InstantTemp()
{
  unsigned long priorTime = 0;
  unsigned long curTime = millis();
  char outStr[128];
  float instant_temp = 0.0;
  // location of text
  int textPosition[2] = {5, 0};
  randomSeed(100);
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  SetFont(deviceSettings.fontPoints);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  sprintf(outStr, "Temperature at %s %s", GetStringTime(), GetStringDate());
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] +=  fontHeight;
  sprintf(outStr, " ");
  SetFont(24);
  textPosition[0] = tftDisplay.width()/2;
  textPosition[1] = tftDisplay.height()/2;
  tftDisplay.setTextDatum(TC_DATUM);
 
  while(true)
  {
    curTime = millis();
    if(curTime - priorTime > 1000)
    {
      textPosition[0] = 5;
      textPosition[1] = 0;
      SetFont(deviceSettings.fontPoints);
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
      sprintf(outStr, "Temperature at %s %s", GetStringTime(), GetStringDate());
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
      textPosition[1] +=  fontHeight;
      sprintf(outStr, " ");
      SetFont(24);
      textPosition[0] = tftDisplay.width()/2;
      textPosition[1] = tftDisplay.height()/2;
      tftDisplay.setTextDatum(TC_DATUM);

      priorTime = curTime;
      // read temp
      #ifdef HAS_THERMO
      instant_temp = tempSensor.getThermocoupleTemp(deviceSettings.tempUnits); // false for F, true or empty for C
      #else
      instant_temp = 100.0F;
      #endif
      tftDisplay.fillRect(textPosition[0], textPosition[1], tftDisplay.textWidth(outStr), fontHeight, TFT_BLACK);
      sprintf(outStr, "%0.2f", instant_temp);
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    }
    CheckButtons(curTime);
    // any button released, exit
    if ((buttons[0].buttonReleased) || (buttons[1].buttonReleased) || (buttons[2].buttonReleased))
    {
      buttons[0].buttonReleased = false;
      buttons[1].buttonReleased = false;
      buttons[2].buttonReleased = false;
      break;
    }
  }
  SetFont(deviceSettings.fontPoints);
}
//
//
//
void DrawGrid(int tireCount)
{
  // horizontal lines
  int maxY = 0;
  for(int idx = 0; idx <= (tireCount/2 + tireCount%2); idx++)
  {
    tftDisplay.drawWideLine(  gridLineH[idx][0][0], gridLineH[idx][0][1], gridLineH[idx][1][0], gridLineH[idx][1][1], 1, TFT_WHITE, TFT_BLACK);
    maxY = gridLineH[idx][0][1];
  }
  // vertical Lines
  for(int idx = 0; idx < 3; idx++)
  {
    gridLineV[idx][1][1] = maxY;
    tftDisplay.drawWideLine(  gridLineV[idx][0][0], gridLineV[idx][0][1], gridLineV[idx][1][0], gridLineV[idx][1][1], 1, TFT_WHITE, TFT_BLACK);
  }
}
//
//
//
void SetupGrid(int fontHeight)
{
  SetFont(fontHeight <= 12 ? fontHeight : 12);
	int gridHeight = tftDisplay.fontHeight(GFXFF);
  // 4 horizontal grid lines
  // x = 1 (start) 475 (end)
  // at y = 
  gridLineH[0][0][0] = 1;      gridLineH[0][0][1] = gridHeight +   10;
  gridLineH[0][1][0] = 475;    gridLineH[0][1][1] = gridHeight +   10;
  
  gridLineH[1][0][0] = 1;      gridLineH[1][0][1] = gridHeight +   80;
  gridLineH[1][1][0] = 475;    gridLineH[1][1][1] = gridHeight +   80;
  
  gridLineH[2][0][0] = 1;      gridLineH[2][0][1] = gridHeight +  150;
  gridLineH[2][1][0] = 475;    gridLineH[2][1][1] = gridHeight +  150;
  
  gridLineH[3][0][0] = 1;      gridLineH[3][0][1] = gridHeight +  220;
  gridLineH[3][1][0] = 475;    gridLineH[3][1][1] = gridHeight +  220;
  
  // 3 vertical grid lines
  // at x = 
  // 1
  // 237
  // 475
  gridLineV[0][0][0] = 1;       gridLineV[0][0][1] = gridHeight +  10;
  gridLineV[0][1][0] = 1;       gridLineV[0][1][1] = gridHeight + 220;
  gridLineV[1][0][0] = 237;     gridLineV[1][0][1] = gridHeight +  10;
  gridLineV[1][1][0] = 237;     gridLineV[1][1][1] = gridHeight + 220;
  gridLineV[2][0][0] = 475;     gridLineV[2][0][1] = gridHeight +  10;
  gridLineV[2][1][0] = 475;     gridLineV[2][1][1] = gridHeight + 220;
  // V               V               V
  // 0               1               2
  // ==G0===============G1============  H0
  // | 0,0  0,1  0,2 | 0,5  0,4  0,3 |
  // | 1,0  1,1  1,2 | 1,5  1,4  1,3 |
  // ==G2===============G3============  H1
  // | 2,0  2,1  2,2 | 2,5  2,4  2,3 |
  // | 3,0  3,1  3,2 | 3,5  3,4  3,3 |
  // ==G4===============G5============  H2
  // | 4,0  4,1  4,2 | 4,5  4,4  4,3 |
  // | 5,0  5,1  5,2 | 5,5  5,4  5,3 |
  // =================================  H3
  //               DONE
  //  
  //
  // 36 text locations in grid boxes
  // not that cell order in col 1 is reversed so measure order matches orientation on car
  //
  // grid row 0
  // cell row 0
  cellPoint[0][0][0] = gridLineV[0][0][0] +   5;              cellPoint[0][0][1] = gridLineH[0][0][1] + 5;
  cellPoint[0][1][0] = gridLineV[0][0][0] +  75;              cellPoint[0][1][1] = gridLineH[0][0][1] + 5;
  cellPoint[0][2][0] = gridLineV[0][0][0] + 145;              cellPoint[0][2][1] = gridLineH[0][0][1] + 5;
  cellPoint[0][3][0] = gridLineV[1][0][0] + 145;              cellPoint[0][3][1] = gridLineH[0][0][1] + 5;
  cellPoint[0][4][0] = gridLineV[1][0][0] +  75;              cellPoint[0][4][1] = gridLineH[0][0][1] + 5;
  cellPoint[0][5][0] = gridLineV[1][0][0] +   5;              cellPoint[0][5][1] = gridLineH[0][0][1] + 5;
  // cell row 1
  cellPoint[1][0][0] = gridLineV[0][0][0] +   5;              cellPoint[1][0][1] = gridLineH[0][0][1] + gridHeight + 5;
  cellPoint[1][1][0] = gridLineV[0][0][0] +  75;              cellPoint[1][1][1] = gridLineH[0][0][1] + gridHeight + 5;
  cellPoint[1][2][0] = gridLineV[0][0][0] + 145;              cellPoint[1][2][1] = gridLineH[0][0][1] + gridHeight + 5;
  cellPoint[1][3][0] = gridLineV[1][0][0] + 145;              cellPoint[1][3][1] = gridLineH[0][0][1] + gridHeight + 5;
  cellPoint[1][4][0] = gridLineV[1][0][0] +  75;              cellPoint[1][4][1] = gridLineH[0][0][1] + gridHeight + 5;
  cellPoint[1][5][0] = gridLineV[1][0][0] +   5;              cellPoint[1][5][1] = gridLineH[0][0][1] + gridHeight + 5;
  // grid row 1
  // cell row 2
  cellPoint[2][0][0] = gridLineV[0][0][0] +   5;              cellPoint[2][0][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][1][0] = gridLineV[0][0][0] +  75;              cellPoint[2][1][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][2][0] = gridLineV[0][0][0] + 145;              cellPoint[2][2][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][3][0] = gridLineV[1][0][0] + 145;              cellPoint[2][3][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][4][0] = gridLineV[1][0][0] +  75;              cellPoint[2][4][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][5][0] = gridLineV[1][0][0] +   5;              cellPoint[2][5][1] = gridLineH[1][0][1] + 5;
  // cell row 3
  cellPoint[3][0][0] = gridLineV[0][0][0] +   5;              cellPoint[3][0][1] = gridLineH[1][0][1] + gridHeight + 5;
  cellPoint[3][1][0] = gridLineV[0][0][0] +  75;              cellPoint[3][1][1] = gridLineH[1][0][1] + gridHeight + 5;
  cellPoint[3][2][0] = gridLineV[0][0][0] + 145;              cellPoint[3][2][1] = gridLineH[1][0][1] + gridHeight + 5;
  cellPoint[3][3][0] = gridLineV[1][0][0] + 145;              cellPoint[3][3][1] = gridLineH[1][0][1] + gridHeight + 5;
  cellPoint[3][4][0] = gridLineV[1][0][0] +  75;              cellPoint[3][4][1] = gridLineH[1][0][1] + gridHeight + 5;
  cellPoint[3][5][0] = gridLineV[1][0][0] +   5;              cellPoint[3][5][1] = gridLineH[1][0][1] + gridHeight + 5;
  // grid row 2
  // cell row 4
  cellPoint[4][0][0] = gridLineV[0][0][0] +   5;              cellPoint[4][0][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][1][0] = gridLineV[0][0][0] +  75;              cellPoint[4][1][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][2][0] = gridLineV[0][0][0] + 145;              cellPoint[4][2][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][3][0] = gridLineV[1][0][0] + 145;              cellPoint[4][3][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][4][0] = gridLineV[1][0][0] +  75;              cellPoint[4][4][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][5][0] = gridLineV[1][0][0] +   5;              cellPoint[4][5][1] = gridLineH[2][0][1] + 5;
  // cell row 5
  cellPoint[5][0][0] = gridLineV[0][0][0] +   5;              cellPoint[5][0][1] = gridLineH[2][0][1] + gridHeight + 5;
  cellPoint[5][1][0] = gridLineV[0][0][0] +  75;              cellPoint[5][1][1] = gridLineH[2][0][1] + gridHeight + 5;
  cellPoint[5][2][0] = gridLineV[0][0][0] + 145;              cellPoint[5][2][1] = gridLineH[2][0][1] + gridHeight + 5;
  cellPoint[5][3][0] = gridLineV[1][0][0] + 145;              cellPoint[5][3][1] = gridLineH[2][0][1] + gridHeight + 5;
  cellPoint[5][4][0] = gridLineV[1][0][0] +  75;              cellPoint[5][4][1] = gridLineH[2][0][1] + gridHeight + 5;
  cellPoint[5][5][0] = gridLineV[1][0][0] +   5;              cellPoint[5][5][1] = gridLineH[2][0][1] + gridHeight + 5;
  // cell row 6
  cellPoint[6][0][0] = gridLineV[0][0][0] +   5;              cellPoint[6][0][1] = gridLineH[3][0][1] + gridHeight + 10;
  cellPoint[6][1][0] = gridLineV[0][0][0] +  75;              cellPoint[6][1][1] = gridLineH[3][0][1] + gridHeight + 10;
  cellPoint[6][2][0] = gridLineV[0][0][0] + 145;              cellPoint[6][2][1] = gridLineH[3][0][1] + gridHeight + 10;
  cellPoint[6][3][0] = gridLineV[1][0][0] + 145;              cellPoint[6][3][1] = gridLineH[3][0][1] + gridHeight + 10;
  cellPoint[6][4][0] = gridLineV[1][0][0] +  75;              cellPoint[6][4][1] = gridLineH[3][0][1] + gridHeight + 10;
  cellPoint[6][5][0] = gridLineV[1][0][0] +   5;              cellPoint[6][5][1] = gridLineH[3][0][1] + gridHeight + 10;

  SetFont(fontHeight);
}
///
///
///
void DisplayAllTireTemps(CarSettings currentResultCar)
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  // location of text
  int textPosition[2] = {5, 0};
  int row = 0;
  int col = 0;
  char outStr[255];
  char padStr[3];
  float maxTemp = 0.0F;
  float minTemp = 999.0F;
  // initial clear of screen
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  DrawGrid(currentResultCar.tireCount);

  sprintf(outStr, "%s %s", currentResultCar.carName.c_str(), currentResultCar.dateTime.c_str());
  SetFont(deviceSettings.fontPoints <= 12 ? deviceSettings.fontPoints : 12);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString(outStr, 5, 0, GFXFF);

  for(int idxTire = 0; idxTire < currentResultCar.tireCount; idxTire++)
  {
    // get min/max temps
    maxTemp = 0.0F;
    for(int tirePosIdx = 0; tirePosIdx < currentResultCar.positionCount; tirePosIdx++)
    {
      maxTemp = tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] > maxTemp ? tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] : maxTemp;
      minTemp = tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] < minTemp ? tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] : minTemp;
    }    
	for(int tirePosIdx = 0; tirePosIdx < currentResultCar.positionCount; tirePosIdx++)
    {
      // draw tire position name
      row = ((idxTire / 2) * 2);
      col = tirePosIdx + ((idxTire % 2) * 3);
      if(tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] >= 100.0F)
      {
        padStr[0] = '\0';
      }
      else if(tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] >= 10.0F)
      {
        sprintf(padStr, " ");
      }
      else
      {
        sprintf(padStr, "  ");
      }
      if(tirePosIdx == 0)
      {
        sprintf(outStr, "%s %s", currentResultCar.tireShortName[idxTire].c_str(),
                                 currentResultCar.positionShortName[tirePosIdx].c_str());
      }
      else
      {
        sprintf(outStr, "%s", currentResultCar.positionShortName[tirePosIdx].c_str());
      }
	  // tire name, position
      DrawCellText(row, col, outStr, TFT_WHITE, TFT_BLACK);
      row++;
      sprintf(outStr, "%3.1F", tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx]);
      if(tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] == maxTemp)
      {
        DrawCellText(row, col, outStr, TFT_BLACK, TFT_RED);
      }
      else if(tireTemps[(idxTire * currentResultCar.positionCount) + tirePosIdx] == minTemp)
      {
        DrawCellText(row, col, outStr, TFT_BLACK, TFT_BLUE);
      }
      else
      {
        DrawCellText(row, col, outStr, TFT_WHITE,  TFT_BLACK);
      }
    }
  }
  priorTime = curTime;
  while(true)
  {
    curTime = millis();
    CheckButtons(curTime);
    // select button released, go to next tire
    if (buttons[0].buttonReleased)
    {
      buttons[0].buttonReleased = false;
      break;
    }
    // cancel button released, return
    else if (buttons[1].buttonReleased)
    {
      buttons[1].buttonReleased = false;
      break;
    }
    else if ((buttons[2].buttonReleased) || (buttons[3].buttonReleased)) 
    {
      buttons[2].buttonReleased = false;
    }
    delay(50);
  }
}
//
//
//
void MeasureAllTireTemps()
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  char outStr[255];
  char padStr[3];
  int selTire = 0;
  bool startMeasure = false;
  bool measureDone = false;
  int row = 0;
  int col = 0;

  float maxTemp = 0.0F;
  float minTemp = 999.0F;
  // initial clear of screen
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
    {
      tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] = 0.0F;
    }
  }

  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  DrawGrid(cars[selectedCar].tireCount);
  SetFont(deviceSettings.fontPoints <= 12 ? deviceSettings.fontPoints : 12);
  while(true)
  {
    sprintf(outStr, "%s", cars[selectedCar].carName.c_str());
    tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    tftDisplay.drawString(outStr, 5, 0, GFXFF);

    if(!startMeasure)
    {
      row = cars[selectedCar].tireCount;
      col = 0;

      // done button
      if(selTire < cars[selectedCar].tireCount)
      {
        DrawCellText(row, col, "DONE", TFT_WHITE, TFT_BLACK);
      }
      else
      {
        DrawCellText(row, col, "DONE", TFT_BLACK, TFT_YELLOW);
      }
      // tires
      for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
      {
        // tires
        col = (idxTire % 2);
        maxTemp = 0.0F;
        minTemp = 999.0F;
        for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
        {
          maxTemp = tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] > maxTemp ? tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] : maxTemp;
          minTemp = tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] < minTemp ? tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] : minTemp;
        }
        for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
        {
          sprintf(padStr, "  ");
          if(tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] >= 100.0F)
          {
            padStr[0] = '\0';
          }
          else if(tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] >= 10.0F)
          {
            sprintf(padStr, " ");
          }
		      // draw tire position name
		      row = ((idxTire / 2) * 2);
		      col = tirePosIdx + ((idxTire % 2) * 3);
          if(tirePosIdx == 0)
          {
            sprintf(outStr, "%s %s", cars[selectedCar].tireShortName[idxTire].c_str(),
                                   cars[selectedCar].positionShortName[tirePosIdx].c_str());
          }
          else
          {
            sprintf(outStr, "%s", cars[selectedCar].positionShortName[tirePosIdx].c_str());
          }
          if(idxTire == selTire)
          {
            // full background highlight rectangle
            if(tirePosIdx == 0)
            {
              int rectCol = col <= 2 ? 0 : 5;
              tftDisplay.fillRect(cellPoint[row][rectCol][0], cellPoint[row][rectCol][1], 220, fontHeight - 5, TFT_YELLOW);
            }
            DrawCellText(row, col, outStr, TFT_BLACK, TFT_YELLOW);
          }
          else
          {
            if(tirePosIdx == 0)
            {
              int rectCol = col <= 2 ? 0 : 5;
              tftDisplay.fillRect(cellPoint[row][rectCol][0], cellPoint[row][rectCol][1], 220, fontHeight - 5, TFT_BLACK);
            }
            DrawCellText(row, col, outStr, TFT_WHITE, TFT_BLACK);
          }
          // draw tire temp
		      row++;
          sprintf(outStr, "%3.1F", tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx]);
          //
          if(tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] != 0.0F)
          {
            if(tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] >= maxTemp)
            {
              DrawCellText(row, col, outStr, TFT_BLACK, TFT_RED);
            }
            else if(tireTemps[(idxTire * cars[selectedCar].positionCount) + tirePosIdx] <= minTemp)
            {
              DrawCellText(row, col, outStr, TFT_BLACK, TFT_BLUE);
            }
            else
            {
              DrawCellText(row, col, outStr, TFT_WHITE, TFT_BLACK);
            }
          }
        }
      }
      // do measurements for tire
      if(selTire < cars[selectedCar].tireCount)
      {
        int nextDirection = MeasureTireTemps(selTire);
        selTire = GetNextTire(selTire, nextDirection);
        continue;
      }
    }
    priorTime = curTime;
    startMeasure = false;
    measureDone = false;
    while(true)
    {
      curTime = millis();
      CheckButtons(curTime);
      // select button released, go to next tire
      if (buttons[0].buttonReleased)
      {
        if(selTire < cars[selectedCar].tireCount)
        {
          startMeasure = true;
        }
        else
        {
          measureDone = true;
        }
        buttons[0].buttonReleased = false;
        break;
      }
      // cancel button released, return
      else if (buttons[1].buttonReleased)
      {
        selTire = GetNextTire(selTire, 1);
        buttons[1].buttonReleased = false;
        break;
      }
      else if ((buttons[2].buttonReleased)) 
      {
        selTire = GetNextTire(selTire, -1);
        buttons[2].buttonReleased = false;
        break;
      }
      delay(50);
    }
    if(measureDone)
    {
      break;
    }
  }
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  SetFont(deviceSettings.fontPoints);// <= 12 ? deviceSettings.fontPoints : 12);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  // location of text
  int textPosition[2] = {5, 0};

  tftDisplay.drawString("Done", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  tftDisplay.drawString("Storing results...", textPosition[0], textPosition[1], GFXFF);
    // done, copy local to global
  #ifdef HAS_RTC
  cars[selectedCar].dateTime = GetStringTime();
  String curTimeStr;
  curTimeStr = GetStringTime();
  curTimeStr += " ";
  curTimeStr += GetStringDate();
  sprintf(outStr, "%s;%s;%d;%d", curTimeStr.c_str(), 
                                 cars[selectedCar].carName.c_str(),
                                 cars[selectedCar].tireCount, 
                                 cars[selectedCar].positionCount);
  #else
  sprintf(outStr, "%d;%s;%d;%d", millis(), 
                                 cars[selectedCar].carName.c_str(), 
                                 cars[selectedCar].tireCount, 
                                 cars[selectedCar].positionCount);
  #endif
  String fileLine = outStr;
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int idxPosition = 0; idxPosition < cars[selectedCar].positionCount; idxPosition++)
    {
      //tireTemps[(idxTire * cars[selectedCar].positionCount) + idxPosinitialSelectition] = currentTemps[(idxTire * cars[selectedCar].positionCount) + idxPosition];
      fileLine += ';';
      fileLine += tireTemps[(idxTire * cars[selectedCar].positionCount) + idxPosition];
    }
  }
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    fileLine += ';';
    fileLine += cars[selectedCar].tireShortName[idxTire];
  }
  for(int idxPosition = 0; idxPosition < cars[selectedCar].positionCount; idxPosition++)
  {
    fileLine += ';';
    fileLine += cars[selectedCar].positionShortName[idxPosition];
  }
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    fileLine += ';';
    fileLine += cars[selectedCar].maxTemp[idxTire];
  }
  
  sprintf(outStr, "/py_temps_%d.txt", selectedCar);
  

  AppendFile(SD, outStr, fileLine.c_str());
  // update the HTML file
  textPosition[1] += fontHeight;
  tftDisplay.drawString("Updating HTML...", textPosition[0], textPosition[1], GFXFF);
  WriteResultsHTML();  
}
//
//
//
int GetNextTire(int selTire, int nextDirection)        
{
  selTire += nextDirection;
  while(true)
  {
    if(selTire < 0)
    {
      selTire = cars[selectedCar].tireCount;
    }
    if(selTire > cars[selectedCar].tireCount)
    {
      selTire = 0;
    }
	// stop on 'done'
    if(selTire == cars[selectedCar].tireCount)
    {
      break;
    }
	// stop on first measure of selTire == 0.0 (never measured)
    if (tireTemps[(selTire * cars[selectedCar].positionCount)] == 0.0)
    {
      break;
    }
    selTire += nextDirection;
  }
  return selTire;
}
//
// grid lines enclose 2 rows of 3 cells each
// top row is T (tire name) and measure position (O, M, I). This row is highlighted when selected during measure
// bottom row is the 3 temperatures
// up to 2 grid cells per axle (for cars) or 2 grid cells for front/rear for motorcycles
//   ------------------------------------
//  | T O    M    I    | I    M    T O  |
//  | temp   temp temp | temp temp temp |
//   ------------------------------------
//
void DrawCellText(int row, int col, char* outStr, uint16_t textColor, uint16_t backColor)
{
  //SetFont(deviceSettings.fontPoints);
  //tftDisplay.setTextDatum(TL_DATUM);
  //strcat(outStr, " ");
  String blankStr = "00000";
  tftDisplay.setTextColor(backColor, backColor);
  if(strlen(outStr) > 1)
  {
    tftDisplay.fillRect(cellPoint[row][col][0], cellPoint[row][col][1], tftDisplay.textWidth(blankStr.c_str()), fontHeight - 4, backColor);
  }
  tftDisplay.setTextColor(textColor, backColor);
  tftDisplay.drawString(outStr, cellPoint[row][col][0], cellPoint[row][col][1], GFXFF);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
}
//
//
//
int MenuSelect(int fontSize, MenuChoice choices[], int menuCount, int initialSelect)
{
  char outStr[256];
  // location of text
  int textPosition[2] = {5, 0};
  unsigned long currentMillis = millis();
  // find initial selection
  int selection = initialSelect;
  for(int selIdx = 0; selIdx < menuCount; selIdx++)
  {
    if(choices[selIdx].result == initialSelect)
    {
      selection = selIdx;
    }
  }
  // reset buttons
  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttons[btnIdx].buttonReleased = false;
  }
  // erase screen, draw banner
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  // loop until selection is made
  SetFont(fontSize);
  int linesToDisplay = (tftDisplay.height() - 10)/fontHeight;
  // range of selections to display (allow scrolling)
  int displayRange[2] = {0, linesToDisplay - 1 };
  displayRange[1] = (menuCount < linesToDisplay ? menuCount : linesToDisplay) - 1;
  // display menu
  while(true)
  {
    textPosition[0] = 5;
    textPosition[1] = 0;
    for(int menuIdx = displayRange[0]; menuIdx <= displayRange[1]; menuIdx++)
    {
      sprintf(outStr, "%s", choices[menuIdx].description.c_str());
      tftDisplay.fillRect(textPosition[0], textPosition[1], tftDisplay.width(), fontHeight, TFT_BLACK);
      if(menuIdx == selection)
      {
        tftDisplay.setTextColor(TFT_BLACK, TFT_WHITE);
        tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
      }
      else
      {
        tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
        tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
      }
      textPosition[1] += fontHeight;
    }
    while(true)
    {
      currentMillis = millis();
      CheckButtons(currentMillis);
      // selection made, set state and break
      if(buttons[0].buttonReleased)
      {
        buttons[0].buttonReleased = false;
        return choices[selection].result;
      }
      // change selection down, break
      else if(buttons[1].buttonReleased)
      {
        buttons[1].buttonReleased = false;
        selection = (selection + 1) < menuCount ? (selection + 1) : 0;
        // handle loop back to start
        if (selection < displayRange[0])
        {
          displayRange[0] = selection;
          displayRange[1] = displayRange[0] + linesToDisplay - 1; 
        }
        // show next line at bottom
        else 
        if (selection > displayRange[1])
        {
          displayRange[1] = selection; 
          displayRange[0] = displayRange[1] - linesToDisplay + 1;
        }
        break;
      }
      // change selection up, break
      else if(buttons[2].buttonReleased)
      {
        buttons[2].buttonReleased = false;
        selection = (selection - 1) >= 0 ? (selection - 1) : menuCount - 1;
        // handle loop back to start
        if (selection < displayRange[0])
        {
          displayRange[0] = selection;
          displayRange[1] = displayRange[0] + linesToDisplay - 1; 
        }
        // show next line at bottom
        else 
        if (selection > displayRange[1])
        {
          displayRange[1] = selection; 
          displayRange[0] = displayRange[1] - linesToDisplay + 1;
        }
        break;
      }
      else if(buttons[3].buttonReleased)
      {
      }
      delay(100);
    }
  }
  return choices[selection].result;
}
//
//
//
void SelectCar()
{
  MenuChoice* carsMenu = (MenuChoice*)calloc(carCount, sizeof(MenuChoice));
  for(int idx = 0; idx < carCount; idx++)
  {
    carsMenu[idx].description = cars[idx].carName;
    carsMenu[idx].result = idx; 
  }
  selectedCar =  MenuSelect(deviceSettings.fontPoints, carsMenu, carCount, 0); 
  free(carsMenu);
}
//
//
//
void ChangeSettings()
{
  int menuCount = 10;
  int result =  0;
  MenuChoice settingsChoices[10];
  char buf[512];

  while(true)
  {
    settingsChoices[SET_DATETIME].description = "Set Date/Time"; settingsChoices[SET_DATETIME].result = SET_DATETIME;
    settingsChoices[SET_TEMPUNITS].description = "Set Units";     settingsChoices[SET_TEMPUNITS].result = SET_TEMPUNITS;
    // flip display and buttons
    if(deviceSettings.screenRotation == 1)
    {
      settingsChoices[SET_FLIPDISPLAY].description = "Switch to Left Hand (invert screen)"; settingsChoices[SET_FLIPDISPLAY].result = SET_FLIPDISPLAY;      
    }
    else
    {
      settingsChoices[SET_FLIPDISPLAY].description = "Switch to Right Hand (invert screen)"; settingsChoices[SET_FLIPDISPLAY].result = SET_FLIPDISPLAY;      
    }
    settingsChoices[SET_FONTSIZE].description = "Font size";             settingsChoices[SET_FONTSIZE].result = SET_FONTSIZE;
    settingsChoices[SET_12H24H].description = "12 or 24 hour clock";     settingsChoices[SET_12H24H].result = SET_12H24H;


    settingsChoices[SET_DELETEDATA].description = "Delete Data";   settingsChoices[SET_DELETEDATA].result = SET_DELETEDATA;
    // IP address
    sprintf(buf, "IP %d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
    settingsChoices[SET_IPADDRESS].description = buf;             settingsChoices[SET_IPADDRESS].result = SET_IPADDRESS;
    // password
    sprintf(buf, "Pass %s", deviceSettings.pass);
    settingsChoices[SET_PASS].description = buf;             settingsChoices[SET_PASS].result = SET_PASS;
    // save
    settingsChoices[SET_SAVESETTINGS].description = "Save Settings"; settingsChoices[SET_SAVESETTINGS].result = SET_SAVESETTINGS;
    // exit settings
    settingsChoices[SET_EXIT].description = "Exit";          settingsChoices[SET_EXIT].result = SET_EXIT;
    result =  MenuSelect(deviceSettings.fontPoints, settingsChoices, menuCount, 0); 
    switch(result)
    {
      case SET_DATETIME:
        SetDateTime();
        break;
      case SET_TEMPUNITS:
        SetUnits();
        break;
      case SET_DELETEDATA:
        DeleteDataFile();
        break;
      case SET_FLIPDISPLAY:
        deviceSettings.screenRotation = deviceSettings.screenRotation == 1 ? 3 : 1;
        RotateDisplay(true);
        break;
      case SET_FONTSIZE:
        SelectFontSize();
        break;
      case SET_12H24H:
        Select12or24();
        break;
      case SET_SAVESETTINGS:
        WriteDeviceSetupFile(SD, "/py_setup.txt");
        break;
      case SET_IPADDRESS:
        break;
      case SET_PASS:
        break;
      case SET_EXIT:
        return;
      default:
        break;
    }
  }
}
//
//
//
void Select12or24()
{
  MenuChoice hour12_24Choices[2];
  hour12_24Choices[HOURS_12].description  = "12 Hour clock";  hour12_24Choices[HOURS_12].result = HOURS_12;
  hour12_24Choices[HOURS_24].description = "24 Hour clock"; hour12_24Choices[HOURS_24].result = HOURS_24;
  int result =  MenuSelect(deviceSettings.fontPoints, hour12_24Choices, 2, 0); 
  switch(result)
  {
    case HOURS_12:
      deviceSettings.is12Hour  = true;
      break;
    case HOURS_24:
      deviceSettings.is12Hour = false;
      break;
    default:
      break;
  }
  return;
}
//
//
//
void SelectFontSize()
{
  MenuChoice fontSizeChoices[4];
  fontSizeChoices[FONTSIZE_9].description  = "9 point";  fontSizeChoices[FONTSIZE_9].result = FONTSIZE_9;
  fontSizeChoices[FONTSIZE_12].description = "12 point"; fontSizeChoices[FONTSIZE_12].result = FONTSIZE_12;
  fontSizeChoices[FONTSIZE_18].description = "18 point"; fontSizeChoices[FONTSIZE_18].result = FONTSIZE_18;
  fontSizeChoices[FONTSIZE_24].description = "24 point"; fontSizeChoices[FONTSIZE_24].result = FONTSIZE_24;
  int result =  MenuSelect(deviceSettings.fontPoints, fontSizeChoices, 4, 0); 
  switch(result)
  {
    case FONTSIZE_9:
      deviceSettings.fontPoints = 9;
      break;
    case FONTSIZE_12:
      deviceSettings.fontPoints = 12;
      break;
    case FONTSIZE_18:
      deviceSettings.fontPoints = 18;
      break;
    case FONTSIZE_24:
      deviceSettings.fontPoints = 24;
      break;
    default:
    break;
  }
  return;
}
//
//
//
void RotateDisplay(bool rotateButtons)
{
  tftDisplay.setRotation(deviceSettings.screenRotation);
  tftDisplay.fillScreen(TFT_BLACK);
  if(rotateButtons)
  {
    int reverseButtons[BUTTON_COUNT];
    for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
    {
      reverseButtons[BUTTON_COUNT - (btnIdx + 1)] = buttons[btnIdx].buttonPin;
    }
    for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
    {
      buttons[btnIdx].buttonPin = reverseButtons[btnIdx];
    }
  }
}
//
//
//
void SetUnits()
{
  int menuCount = 2;
  MenuChoice unitsChoices[2];

  unitsChoices[0].description = "Temp in F";   unitsChoices[0].result = 0;
  unitsChoices[1].description = "Temp in C";   unitsChoices[1].result = 1;
  int menuResult =  MenuSelect(deviceSettings.fontPoints, unitsChoices, menuCount, 0); 
  // true for C, false for F
  deviceSettings.tempUnits = menuResult == 1;
}
//
//
//
void DeleteDataFile(bool verify)
{
  int menuResult = 1;
  MenuChoice deleteFileYN[2];
  if(verify)
  {
    int menuCount = 2;
    deleteFileYN[0].description = "Yes";      deleteFileYN[0].result = 1;
    deleteFileYN[1].description = "No";   deleteFileYN[1].result = 0;
    menuResult = MenuSelect(deviceSettings.fontPoints, deleteFileYN, menuCount, 1); 
  }
  if(menuResult == 1)
  {
    int dataIdx = 0;
    char nameBuf[128];
    for(int dataIdx = 0; dataIdx < 100; dataIdx++)
    {
      sprintf(nameBuf, "/py_temps_%d.txt", dataIdx);
      if(!SD.exists(nameBuf))
      {
        continue;
      }
      DeleteFile(SD, nameBuf);
    }
    DeleteFile(SD, "/py_res.html");
    // create the HTML header
    WriteResultsHTML();
  }
}
//
//
//
bool CheckTempStable(float curTemp)
{
  tempStableRecent[tempIdx] = curTemp;

  tempStableMinMax[0] = 150;
  tempStableMinMax[1] = -150;
  for(int cnt = 0; cnt < 10; cnt++)
  {
    tempStableMinMax[0] = tempStableRecent[cnt] < tempStableMinMax[0] ? tempStableRecent[cnt] : tempStableMinMax[0];
    tempStableMinMax[1] = tempStableRecent[cnt] > tempStableMinMax[1] ? tempStableRecent[cnt] : tempStableMinMax[1];
  }
  tempIdx = tempIdx + 1 >= 10 ? 0 : tempIdx + 1;
 if(((tempStableMinMax[1] - tempStableMinMax[0]) <= 0.5))
  {
    return true;
  }
  return false;
}
//
//
//
void ResetTempStable()
{
  // set min/max values, fill temp array
  tempStableMinMax[0] = 200.0F;
  tempStableMinMax[1] = -200.0F;
  for(int cnt = 0; cnt < 10; cnt++)
  {
    tempStableRecent[cnt] = 0.0;
  }
  tempStable = false;
}
//
//CarSettings
//
void ReadCarSetupFile(fs::FS &fs, const char * path)
{
  char buf[512];
  File file = fs.open(path, FILE_READ);
  if(!file)
  {
    return;
  }
  ReadLine(file, buf);
  carCount = atoi(buf);
  cars = new CarSettings[carCount];
  int maxTires = 0;
  int maxPositions = 0;
  for(int carIdx = 0; carIdx < carCount; carIdx++)
  {
    // read name
    ReadLine(file, buf);
    cars[carIdx].carName = buf;
    // read tire count and create arrays
    ReadLine(file, buf);
    cars[carIdx].tireCount = atoi(buf);
    maxTires = maxTires > cars[carIdx].tireCount ? maxTires : cars[carIdx].tireCount;
    cars[carIdx].maxTemp = (float*)calloc(maxTires, sizeof(float));
    cars[carIdx].tireShortName = new String[cars[carIdx].tireCount];
    cars[carIdx].tireLongName = new String[cars[carIdx].tireCount];
    // read tire short and long names
    for(int tireIdx = 0; tireIdx < cars[carIdx].tireCount; tireIdx++)
    {
      ReadLine(file, buf);
      cars[carIdx].tireShortName[tireIdx] = buf;
      ReadLine(file, buf);
      cars[carIdx].tireLongName[tireIdx] = buf;
      ReadLine(file, buf);
      cars[carIdx].maxTemp[tireIdx] = atof(buf);
    }
    // read measurement count and create arrays
    ReadLine(file, buf);
    cars[carIdx].positionCount = atoi(buf);
    cars[carIdx].positionShortName = new String[cars[carIdx].positionCount];
    cars[carIdx].positionLongName = new String[cars[carIdx].positionCount];
    maxPositions = maxPositions > cars[carIdx].positionCount ? maxPositions : cars[carIdx].positionCount;
    // read tire short and long names
    for(int positionIdx = 0; positionIdx < cars[carIdx].positionCount; positionIdx++)
    {
      ReadLine(file, buf);
      cars[carIdx].positionShortName[positionIdx] = buf;
      ReadLine(file, buf);
      cars[carIdx].positionLongName[positionIdx] = buf;
    }
    // seperator
    ReadLine(file, buf);
  }
  selectedCar = 0;
  file.close();
}
//
//
//
void WriteCarSetupFile(fs::FS &fs, const char * path)
{
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    return;
  }
  file.println("5");            // number of cars
  file.println("Brian Z4");    // car
  file.println("4");           // wheels
  file.println("LF");
  file.println("Left Front");
  file.println("110.0");
  file.println("RF");
  file.println("Right Front");
  file.println("110.0");
  file.println("LR");
  file.println("Left Rear");
  file.println("110.0");
  file.println("RR");
  file.println("Right Rear");
  file.println("110.0");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 
  file.println("Mark MR2");    // car
  file.println("4");           // wheels
  file.println("RF");
  file.println("Left Front");
  file.println("110.0");
  file.println("LR");
  file.println("Right Front");
  file.println("110.0");
  file.println("LF");
  file.println("Left Rear");
  file.println("110.0");
  file.println("RR");
  file.println("Right Rear");
  file.println("110.0");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 
  file.println("Jody P34");    // car
  file.println("6");           // wheels
  file.println("LF");
  file.println("Left Front");
  file.println("110.0");
  file.println("RF");
  file.println("Right Front");
  file.println("110.0");
  file.println("RM");
  file.println("LM");
  file.println("Left Mid");
  file.println("110.0");
  file.println("Right Mid");
  file.println("110.0");
  file.println("LR");
  file.println("Left Rear");
  file.println("110.0");
  file.println("RR");
  file.println("Right Rear");
  file.println("110.0");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 
  file.println("Doc YRZ-M1");    // car
  file.println("2");           // wheels
  file.println("F");
  file.println("Front");
  file.println("110.0");
  file.println("R");
  file.println("Rear");
  file.println("110.0");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 
  file.println("Nigel Super3");    // car
  file.println("3");           // wheels
  file.println("LF");
  file.println("Left Front");
  file.println("110.0");
  file.println("RF");
  file.println("Right Front");
  file.println("110.0");
  file.println("R");
  file.println("Rear");
  file.println("110.0");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 

  file.close();
}
//
//
//
void ReadDeviceSetupFile(fs::FS &fs, const char * path)
{
  char buf[512];
  File file = fs.open(path, FILE_READ);
  if(!file)
  {
    return;
  }
  ReadLine(file, buf);
  sprintf(deviceSettings.ssid, buf);
  ReadLine(file, buf);
  sprintf(deviceSettings.pass, buf);
  ReadLine(file, buf);
  deviceSettings.screenRotation = atoi(buf);
  int temp = 0;
  ReadLine(file, buf);
  temp = atoi(buf);
  deviceSettings.tempUnits = temp == 0 ? false : true;
  ReadLine(file, buf);
  temp = atoi(buf);
  deviceSettings.is12Hour = temp == 0 ? false : true;
  ReadLine(file, buf);
  deviceSettings.fontPoints = atoi(buf);
}
//
//
//
void WriteDeviceSetupFile(fs::FS &fs, const char * path)
{
  DeleteFile(fs, path);
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    return;
  }
  file.println(deviceSettings.ssid);
  file.println(deviceSettings.pass);
  file.println(deviceSettings.screenRotation);
  file.println(deviceSettings.tempUnits ? 1 : 0);
  file.println(deviceSettings.is12Hour ? 1 : 0);
  file.println(deviceSettings.fontPoints);
}
//
//
//
void DisplaySelectedResults(fs::FS &fs, const char * path)
{
  char buf[512];
  CarSettings currentResultCar;
  int tokenIdx = 0;
  int measureIdx = 0;
  int tireIdx = 0;
  int positionIdx = 0;
  int tempCnt = 0;
  int measureRange[2] = {99, 99};
  int tireNameRange[2] = {99, 99};
  int posNameRange[2] = {99, 99};
  char* token;
  File file = SD.open(path, FILE_READ);
  if(!file)
  {
    tftDisplay.fillScreen(TFT_BLACK);
    YamuraBanner();
    tftDisplay.drawString("No results for", 5, 0,  GFXFF);
    tftDisplay.drawString(cars[selectedCar].carName.c_str(), 5, fontHeight, GFXFF);
    tftDisplay.drawString("Select another car", 5, 2* fontHeight, GFXFF);
    delay(5000);
    return;
  }
  // get count of results
  int menuCnt = 0;
  while(menuCnt < MAX_MENU_ITEMS)
  {
    ReadLine(file, buf);
    if(strlen(buf) == 0)
    {
      break;
    }
	menuCnt++;
  }
  file.close();
  MenuChoice* carsMenu = (MenuChoice*)calloc(menuCnt, sizeof(MenuChoice));
  file = SD.open(path, FILE_READ);
  menuCnt = 0;
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf) == 0)
    {
      break;
    }
    token = strtok(buf, ";");
    char outStr[128];
    sprintf(outStr, "%s %s", cars[selectedCar].carName, token);
    carsMenu[menuCnt].description = outStr;
    carsMenu[menuCnt].result = menuCnt;
    menuCnt++;
  }
  file.close();
  
  int menuResult = MenuSelect(deviceSettings.fontPoints, carsMenu, menuCnt, 0);
  free(carsMenu);
  // at this point, we need to parse the selected line and add to a measurment structure for display
  // get to the correct line
  file = SD.open(path, FILE_READ);
  if(!file)
  {
    tftDisplay.fillScreen(TFT_BLACK);
    YamuraBanner();
    tftDisplay.drawString("No results for", 5, 0, GFXFF);
    tftDisplay.drawString(cars[selectedCar].carName.c_str(), 5, fontHeight, GFXFF);
    tftDisplay.drawString("Select another car", 5, 2* fontHeight, GFXFF);
    delay(5000);
    return;
  }
  for (int lineNumber = 0; lineNumber <= menuResult; lineNumber++)
  {
    ReadLine(file, buf);
  } 
  file.close();
  ParseResult(buf, currentResultCar);
  DisplayAllTireTemps(currentResultCar);
  free(currentResultCar.maxTemp);
}
//
// write HTML display file
//<!DOCTYPE html>
//<html>
//<head>
//    <title>Recording Pyrometer</title>
//</head>
//<body>
//    <h1>Recorded Results</h1>
//    <table border="1">
//        <tr>
//            <th>Date/Time</th>
//            <th>Car/Driver</th>
//        </tr>
//		    <tr>
//            <td>07:03:19PM 09/05/2023</td>
//            <td>Brian Smith Z4</td>
//			      <td>RF</td>
//            <td bgcolor="red">O 77.56</td>
//            <td bgcolor="red">M 77.56</td>
//            <td bgcolor="green">I 77.45</td>
//             ... more temp cells
//        </tr>
//        ... more car rows
//    </table>
//</body>
//</html>
//
void WriteResultsHTML()
{
  char buf[512];
  char nameBuf[128];
  htmlStr = "";
  CarSettings currentResultCar;
  File fileIn;
  // create a new HTML file
  SD.remove("/py_res.html");
  AppendFile(SD, "/py_res.html", "<!DOCTYPE html>");
  AppendFile(SD, "/py_res.html", "<html>");
  AppendFile(SD, "/py_res.html", "<head>");
  AppendFile(SD, "/py_res.html", "    <title>Recording Pyrometer</title>");
  AppendFile(SD, "/py_res.html", "</head>");
  AppendFile(SD, "/py_res.html", "<body>");
  AppendFile(SD, "/py_res.html", "    <h1>Recorded Results</h1>");
  AppendFile(SD, "/py_res.html", "    <table border=\"1\">");
  AppendFile(SD, "/py_res.html", "        <tr>");
  AppendFile(SD, "/py_res.html", "            <th>Date/Time</th>");
  AppendFile(SD, "/py_res.html", "            <th>Car/Driver</th>");
  AppendFile(SD, "/py_res.html", "        </tr>");
  float tireMin =  999.9;
  float tireMax = -999.9;
  int rowCount = 0;
  for (int dataFileCount = 0; dataFileCount < 100; dataFileCount++)
  {
    sprintf(nameBuf, "/py_temps_%d.txt", dataFileCount);
    fileIn = SD.open(nameBuf, FILE_READ);
    if(!fileIn)
    {
      continue;
    }
    bool outputSubHeader = true;
    while(true)
    {
      ReadLine(fileIn, buf);
      // end of file
      if(strlen(buf) == 0)
      {
        break;
      }
      ParseResult(buf, currentResultCar);
      if(outputSubHeader)
      {
        AppendFile(SD, "/py_res.html", "        <tr>");
        AppendFile(SD, "/py_res.html", "            <td></td>");
        AppendFile(SD, "/py_res.html", "            <td></td>");
        for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
        {
          for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
          {
            sprintf(buf, "<td>%s-%s</td>", currentResultCar.tireShortName[t_idx].c_str(), currentResultCar.positionShortName[p_idx].c_str());
            AppendFile(SD, "/py_res.html", buf);
          }
        }
        AppendFile(SD, "/py_res.html", "        </tr>");
      }
      outputSubHeader = false;
      rowCount++;
      AppendFile(SD, "/py_res.html", "		    <tr>");
      sprintf(buf, "<td>%s</td>", currentResultCar.dateTime.c_str());
      AppendFile(SD, "/py_res.html", buf);
      sprintf(buf, "<td>%s</td>", currentResultCar.carName.c_str());
      AppendFile(SD, "/py_res.html", buf);
      for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
      {
        tireMin =  999.9;
        tireMax = -999.9;
        //sprintf(buf, "<td>%s</td>", currentResultCar.tireShortName[t_idx].c_str());
        // get min/max temps
        for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
        {
          tireMin = tireMin < tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] ? tireMin : tireTemps[(t_idx * currentResultCar.positionCount) + p_idx];
          tireMax = tireMax > tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] ? tireMax : tireTemps[(t_idx * currentResultCar.positionCount) + p_idx];
        }
        // add cells to file
        for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
        {
          if(tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] >= currentResultCar.maxTemp[t_idx])
          {
            sprintf(buf, "<td bgcolor=\"red\">%0.2f</td>", /*currentResultCar.positionShortName[p_idx].c_str(),*/ tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else if(tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] <= tireMin)
          {
            sprintf(buf, "<td bgcolor=\"cyan\">%0.2f</td>", /*currentResultCar.positionShortName[p_idx].c_str(),*/ tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else if (tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] == tireMax)
          {
            sprintf(buf, "<td bgcolor=\"yellow\">%0.2f</td>", /*currentResultCar.positionShortName[p_idx].c_str(),*/ tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else
          {
            sprintf(buf, "<td>%0.2f</td>", /*currentResultCar.positionShortName[p_idx].c_str(),*/ tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          AppendFile(SD, "/py_res.html", buf);
        }
      }
      AppendFile(SD, "/py_res.html", "		    </tr>");
	  free(currentResultCar.maxTemp);
    }
  }
  if(rowCount == 0)
  {
    rowCount++;
    AppendFile(SD, "/py_res.html", "		    <tr>");
    sprintf(buf, "<td>---</td>");
    AppendFile(SD, "/py_res.html", buf);
    sprintf(buf, "<td>---</td>");
    AppendFile(SD, "/py_res.html", buf);
    for(int t_idx = 0; t_idx < 4; t_idx++)
    {
      tireMin =  999.9;
      tireMax = -999.9;
      sprintf(buf, "<td>---</td>");
      AppendFile(SD, "/py_res.html", buf);
      // add cells to file
      for(int p_idx = 0; p_idx < 3; p_idx++)
      {
        sprintf(buf, "<td>---</td>");
        AppendFile(SD, "/py_res.html", buf);
      }
    }
    AppendFile(SD, "/py_res.html", "		    </tr>");
  }
  fileIn.close();
  AppendFile(SD, "/py_res.html", "    </table>");
  AppendFile(SD, "/py_res.html", "</body>");
  AppendFile(SD, "/py_res.html", "</html>");
  
  fileIn = SD.open("/py_res.html", FILE_READ);
  if(!fileIn)
  {
    return;
  }
  while(true)
  {
    ReadLine(fileIn, buf);
    // end of file
    if(strlen(buf) == 0)
    {
      break;
    }
    htmlStr += buf;
  }
  fileIn.close();
}
//
//
//
void ParseResult(char buf[], CarSettings &currentResultCar)
{
  int tokenIdx = 0;
  int measureIdx = 0;
  int tireIdx = 0;
  int positionIdx = 0;
  int tempCnt = 0;
  int maxTempIdx = 0;
  int measureRange[2] = {99, 99};
  int tireNameRange[2] = {99, 99};
  int posNameRange[2] = {99, 99};
  int maxTempRange[2] = {99, 99};
  char* token;
  // parse the current line and add to a measurment structure for display
  // buf contains the line, now tokenize it
  // format is:
  // date/time car tireCnt positionCnt measurements tireShortNames positionShortNames (tsv)
  // structures to hold data
  // car info structure
  // struct CarSettings
  // {
  //     String carName;
  //     int tireCount;
  //     String* tireShortName;
  //     String* tireLongName;
  //     int positionCount;
  //     String* positionShortName;
  //     String* positionLongName;
  // };
  // CarSettings* cars;
  token = strtok(buf, ";");
  while(token != NULL)
  {
    // tokenIdx 0 is date/time
    // 0 - timestamp
    if(tokenIdx == 0)
    {
      currentResultCar.dateTime = token;      
    }
    // 1 - car info
    if(tokenIdx == 1)
    {
      currentResultCar.carName = token;
    }
    // 2 - tire count
    else if(tokenIdx == 2)
    {
      currentResultCar.tireCount = atoi(token);
      currentResultCar.tireShortName = new String[currentResultCar.tireCount];
      currentResultCar.tireLongName = new String[currentResultCar.tireCount];
      currentResultCar.maxTemp = (float*)calloc(currentResultCar.tireCount, sizeof(float));
    }
    // 3 - position count
    else if(tokenIdx == 3)
    {
      currentResultCar.positionCount = atoi(token);
      currentResultCar.positionShortName = new String[currentResultCar.positionCount];
      currentResultCar.positionLongName = new String[currentResultCar.positionCount];
      tempCnt = currentResultCar.tireCount * currentResultCar.positionCount;
      measureRange[0]  = tokenIdx + 1;  // measurements start at next token                                   // >= 5
      measureRange[1]  = measureRange[0] +  (currentResultCar.tireCount *  currentResultCar.positionCount) - 1;   // < 5 + 4*13 (17)
      tireNameRange[0] = measureRange[1];                                                                     // >= 17
      tireNameRange[1] = tireNameRange[0] + currentResultCar.tireCount;                                       // < 17 + 4 (21)
      posNameRange[0]  = tireNameRange[1];                                                                    // >= 21
      posNameRange[1]  = posNameRange[0] + currentResultCar.positionCount;                                    // < 21 + 3 (24)
      maxTempRange[0]  = posNameRange[1];                                                                    // >= 21
      maxTempRange[1]  = maxTempRange[0] + currentResultCar.tireCount;                                    // < 21 + 3 (24)
    }
    // tire temps
    else if((tokenIdx >= measureRange[0]) && (tokenIdx <= measureRange[1]))
    {
      tireTemps[measureIdx] = atof(token);
      currentTemps[measureIdx] = tireTemps[measureIdx];
      measureIdx++;
    }
    // tire names
    else if((tokenIdx >= tireNameRange[0]) && (tokenIdx <= tireNameRange[1]))
    {
      currentResultCar.tireShortName[tireIdx] = token;
      currentResultCar.tireLongName[tireIdx] = token;
      tireIdx++;
    }
    // position names
    else if((tokenIdx >= posNameRange[0]) && (tokenIdx <= posNameRange[1]))
    {
      currentResultCar.positionShortName[positionIdx] = token;
      currentResultCar.positionLongName[positionIdx] = token;
      positionIdx++;
    }
    // max temps
    else if((tokenIdx >= maxTempRange[0]) && (tokenIdx <= maxTempRange[1]))
    {
      currentResultCar.maxTemp[maxTempIdx] = atof(token);
      maxTempIdx++;
    }
    token = strtok(NULL, ";");
    tokenIdx++;
  }
}
//
//
//
void ReadLine(File file, char* buf)
{  
  char c;
  int bufIdx = 0;
  buf[0] = '\0';
  do
  {
    if(!file.available())
    {
      return;
    }
    c = file.read();
    if(c < 0x20)
    {
      if(c != 0x0A)
      {
        continue;
      }
      break;
    }
    buf[bufIdx] = c;
    bufIdx++;
    buf[bufIdx] = '\0';
  } while (c != 0x10);
  return;
}
//
//
//
void AppendFile(fs::FS &fs, const char * path, const char * message)
{
  File file = fs.open(path, FILE_APPEND);
  if(!file)
  {
    return;
  }
  file.println(message);
  file.close();
}
//
//
//
void DeleteFile(fs::FS &fs, const char * path)
{
  fs.remove(path);
}
//
//
//
void handleRoot(AsyncWebServerRequest *request) 
{
  request->send_P(200, "text/html", htmlStr.c_str());
}
//
//
//
void handleNotFound(AsyncWebServerRequest *request) 
{
  request->send(404, "text/plain", "File Not Found");
}
//
//
//
void onServoInputWebSocketEvent(AsyncWebSocket *server, 
                      AsyncWebSocketClient *client, 
                      AwsEventType type,
                      void *arg, 
                      uint8_t *data, 
                      size_t len) 
{                      
  switch (type) 
  {
    case WS_EVT_CONNECT:
      break;
    case WS_EVT_DISCONNECT:
      break;
    case WS_EVT_DATA:
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;  
  }
}
//
//
//
String GetStringTime()
{
  bool h12Flag;
  bool pmFlag;
  String rVal;
  char buf[512];
  int ampm;
  DateTime now;
  now = rtc.now();
  int year = now.year();
  int month = now.month();
  int day = now.day();
  int dayOfWeek = now.dayOfTheWeek();
	int hour = now.hour();
	int minute = now.minute();
	int second = now.second();
  bool isPM = now.isPM();
  if(deviceSettings.is12Hour)
  {
    if(isPM)
    {
      if(hour > 12)
      {
        hour -= 12;
      }
    } 
    else
    {
      Serial.print("am");
    }
  }
  else
  {
      Serial.print("(24h)");
  }

  Serial.print("Time: ");
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.print(second);
  if(deviceSettings.is12Hour)
  {
    if(isPM)
    {
      Serial.print("pm");
    } 
    else
    {
      Serial.print("am");
    }
  }
  else
  {
      Serial.print("(24h)");
  }
  Serial.print("\tDate ");
  Serial.print(month);
  Serial.print("/");
  Serial.print(day);
  Serial.print("/");
  Serial.print(year);
  Serial.print(" (");
  Serial.print(dayOfWeek);
  Serial.println(")");

  if(deviceSettings.is12Hour)
  {
    sprintf(buf, "%02d:%02d%s", hour, minute, ampmStr[isPM ? 1 : 0]);
  }
  else
  {
    if((isPM) && (hour < 12))
    {
      hour += 12;
    }
    sprintf(buf, "%02d:%02d", hour, minute);
  }
  rVal = buf;
  return rVal;
}
//
//
//
String GetStringDate()
{
  bool century = false;
  String rVal;
  char buf[512];
	DateTime now;
  now = rtc.now();
  int year = now.year();
  int month = now.month();
  int day = now.day();
  sprintf(buf, "%02d/%02d/%02d", month, day, year);
  rVal = buf;
  return rVal;
}
//
// set data/time handler
//
void SetDateTime()
{
  char outStr[256];
  int timeVals[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // date, month, year, day, hour, min, sec, 100ths
  bool isPM = false;
  // location of text
  int textPosition[2] = {5, 0};
  int setIdx = 0;
  unsigned long curTime = millis();
  int delta = 0;
  #ifndef HAS_RTC
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  tftDisplay.drawString("RTC not present", textPosition[0], textPosition[1], GFXFF);
  delay(5000);
  return;
  #endif

  DateTime now;
  now = rtc.now();
  int year = now.year();
  int month = now.month();
  int day = now.day();
  int dayOfWeek = now.dayOfTheWeek();

  timeVals[DATE] = now.day();
  timeVals[MONTH] = now.month();
  timeVals[YEAR] = now.year();
  timeVals[DAYOFWEEK] = now.dayOfTheWeek();
  timeVals[HOUR] = now.hour();
  if((now.isPM()) && (!deviceSettings.is12Hour))
  {
    timeVals[HOUR] += 12;
  }
  timeVals[MINUTE] = now.minute();
  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttons[btnIdx].buttonReleased = false;
  }
  while(true)
  {
    textPosition[0] = 5;
    textPosition[1] = 0;
    tftDisplay.fillScreen(TFT_BLACK);
    YamuraBanner();
    tftDisplay.drawString("Set date/time", textPosition[0], textPosition[1], GFXFF);
    
    textPosition[1] += fontHeight;

    sprintf(outStr, "%02d/%02d/%04d %s", timeVals[MONTH], timeVals[DATE], timeVals[YEAR], days[timeVals[DAYOFWEEK]]);
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    if(setIdx < 4)
    {
      textPosition[1] += fontHeight;
      sprintf(outStr, "%s %s %s %s", (setIdx == 0 ? "mm" : "  "), (setIdx == 1 ? "dd" : "  "), (setIdx == 2 ? "yyyy" : "    "), (setIdx == 3 ? "ww" : "  "));
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    }
    textPosition[1] += fontHeight;
    if(deviceSettings.is12Hour)
    {
      sprintf(outStr, "%02d:%02d %s", timeVals[HOUR], timeVals[MINUTE], (isPM ? "PM" : "AM"));
    }
    else
    {
      sprintf(outStr, "%02d:%02d %s", timeVals[HOUR], timeVals[MINUTE], " (24H)");
    }
    tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    if(setIdx > 3)
    {
      textPosition[1] += fontHeight;
      if(deviceSettings.is12Hour)
      {
        sprintf(outStr, "%s %s %s", (setIdx == 4 ? "hh" : "  "), (setIdx == 5 ? "mm" : "  "), (setIdx == 6 ? "ap" : "  "));
      }
      else
      {
        sprintf(outStr, "%s %s %s", (setIdx == 4 ? "hh" : "  "), (setIdx == 5 ? "mm" : "  "));
      }
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    }
    
    while(!buttons[0].buttonReleased)
    {
      curTime = millis();
      CheckButtons(curTime);
      // save time element, advance
      if(buttons[0].buttonReleased)
      {
        buttons[0].buttonReleased = false;
        setIdx++;
        if(setIdx > 6)
        {
          break;
        }
      }
      // increase/decrease
      else if(buttons[1].buttonReleased)
      {
        buttons[1].buttonReleased = false;
        delta = -1;
      }
      else if(buttons[2].buttonReleased)
      {
        buttons[2].buttonReleased = false;
        delta = 1;
      }
      else
      {
        delta = 0;
        continue;
      }
      switch (setIdx)
      {
        case 0:  // month
          timeVals[MONTH] += delta;
          if(timeVals[MONTH] <= 0)
          {
            timeVals[MONTH] = 12;
          }
          if(timeVals[MONTH] > 12)
          {
            timeVals[MONTH] = 1;
          }
          break;
        case 1:  // date
          timeVals[DATE] += delta;
          if(timeVals[DATE] <= 0)
          {
            timeVals[DATE] = 31;
          }
          if(timeVals[DATE] > 31)
          {
            timeVals[DATE] = 1;
          }
          break;
        case 2:  // year
          timeVals[YEAR] += delta;
          break;
        case 3:  // day of week
          timeVals[DAYOFWEEK] += delta;
          if(timeVals[DAYOFWEEK] < 0)
          {
            timeVals[DAYOFWEEK] = 6;
          }
          if(timeVals[DAYOFWEEK] > 6)
          {
            timeVals[DAYOFWEEK] = 0;
          }
          break;
        case 4:  // hour
          timeVals[HOUR] += delta;
          if(deviceSettings.is12Hour)
          {
            if(timeVals[HOUR] < 0)
            {
              timeVals[HOUR] = 12;
            }
            if(timeVals[HOUR] > 12)
            {
              timeVals[HOUR] = 1;
            }
          }
          else
          {
            if(timeVals[HOUR] < 0)
            {
              timeVals[HOUR] = 23;
            }
            if(timeVals[HOUR] > 23)
            {
              timeVals[HOUR] = 0;
            }
          }
          break;
        case 5:  // minute
          timeVals[MINUTE] += delta;
          if(timeVals[MINUTE] < 0)
          {
            timeVals[MINUTE] = 59;
          }
          if(timeVals[MINUTE] > 59)
          {
            timeVals[MINUTE] = 0;
          }
          break;
        case 6:  // am/pm
          if(delta != 0)
          {
            isPM = !isPM;
          }
          break;
      }
      break;
    }
    if((deviceSettings.is12Hour && (setIdx > 6)) ||
       (setIdx > 5))
    {
      break;
    }
  }
  //if(isPM)
  //{
  //  if (timeVals[HOUR] < 12)
  //  {
  //    timeVals[HOUR] += 12;
  //  }
  //}

  Serial.print("Set date/time to ");
  Serial.print(timeVals[YEAR]);
  Serial.print("/");
  Serial.print(timeVals[MONTH]);
  Serial.print("/");
  Serial.print(timeVals[DATE]);
  Serial.print("\t");
  Serial.print(timeVals[HOUR]);
  Serial.print(":");
  Serial.print(timeVals[MINUTE]);
  Serial.print(":");
  Serial.println(timeVals[SECOND]);
  if((deviceSettings.is12Hour) && isPM && (timeVals[HOUR] < 12))
  {
    timeVals[HOUR] += 12;
  }

  rtc.adjust(DateTime(timeVals[YEAR], timeVals[MONTH], timeVals[DATE], timeVals[HOUR],timeVals[MINUTE],timeVals[SECOND]));
  textPosition[1] += fontHeight * 2;
  tftDisplay.drawString(GetStringTime(), textPosition[0], textPosition[1], GFXFF);
  delay(5000);
}
//
// check all buttons for new press or release
//
//
// check all buttons for new press or release
//
void CheckButtons(unsigned long curTime)
{
  byte curState;
  for(byte btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    curState = digitalRead(buttons[btnIdx].buttonPin);
    if(((curTime - buttons[btnIdx].lastChange) > BUTTON_DEBOUNCE_DELAY) && 
       (curState != buttons[btnIdx].buttonLast))
    {
      buttons[btnIdx].lastChange = curTime;
      if(curState == BUTTON_PRESSED)
      {
        buttons[btnIdx].buttonPressed = 1;
        buttons[btnIdx].buttonReleased = 0;
        buttons[btnIdx].buttonLast = BUTTON_PRESSED;
        buttons[btnIdx].pressDuration = 0;
      }
      else if (curState == BUTTON_RELEASED)
      {
        buttons[btnIdx].buttonPressed =  0;
        buttons[btnIdx].buttonReleased = 1;
        buttons[btnIdx].buttonLast = BUTTON_RELEASED;
        buttons[btnIdx].releaseDuration = 0;
      }
    }
    else
    {
      if(curState == BUTTON_PRESSED)
      {
        buttons[btnIdx].pressDuration = curTime - buttons[btnIdx].lastChange;
      }
      else if (curState == BUTTON_RELEASED)
      {
        buttons[btnIdx].releaseDuration = curTime - buttons[btnIdx].lastChange;
      }
    }
  }
}
void YamuraBanner()
{
  tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
  SetFont(9);
  int xPos = tftDisplay.width()/2;
  int yPos = tftDisplay.height() - fontHeight/2;
  tftDisplay.setTextDatum(BC_DATUM);
  tftDisplay.drawString("  Yamura Recording Tire Pyrometer v1.0  ",xPos, yPos, GFXFF);    // Print the font name onto the TFT screen
  tftDisplay.setTextDatum(TL_DATUM);
}
//
//
//
void SetFont(int fontSize)
{
  switch(fontSize)
  {
    case 9:
      tftDisplay.setFreeFont(FSS9);
      break;
    case 12:
      tftDisplay.setFreeFont(FSS12);
      break;
    case 18:
      tftDisplay.setFreeFont(FSS18);
      break;
    case 24:
      tftDisplay.setFreeFont(FSS18);
      break;
    default:
      tftDisplay.setFreeFont(FSS12);
      break;
  }
  tftDisplay.setTextDatum(TL_DATUM);
  fontHeight = tftDisplay.fontHeight(GFXFF);
}