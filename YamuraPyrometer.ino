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
#include <SparkFun_MCP9600.h>    // thermocouple amplifier
#include <TFT_eSPI.h>            // Graphics and font library for ST7735 driver chip
#include "Free_Fonts.h"          // Include the header file attached to this sketch
#include "FS.h"
#include "SD.h"
#include "SPI.h"
// various RTC module libraries
//#define RTC_RV1805
//#define RTC_RV8803
#define HILETGO_DS3231
#ifdef RTC_RV1805
#include <SparkFun_RV1805.h>
#endif
#ifdef RTC_RV8803
#include <SparkFun_RV8803.h> //Get the library here:http://librarymanager/All#SparkFun_RV-8803
#endif
#ifdef HILETGO_DS3231
#include <DS3231-RTC.h> // https://github.com/hasenradball/DS3231-RTC
#endif

// uncomment for debug to serial monitor
#define DEBUG_VERBOSE
//#define DEBUG_EXTRA_VERBOSE
//#define DEBUG_HTML
// microSD chip select, I2C pins
#define sd_CS 5
#define I2C_SDA 21
#define I2C_SCL 22
// uncomment for RTC module attached
#define HAS_RTC
// uncomment for thermocouple module attached
#define HAS_THERMO
// uncomment to write INI file
#define WRITE_INI

#define DISPLAY_MENU            0
#define SELECT_CAR              1
#define MEASURE_TIRES           2
#define DISPLAY_TIRES           3
#define DISPLAY_SELECTED_RESULT 4
#define CHANGE_SETTINGS         5
#define INSTANT_TEMP            6

#define FONT_HEIGHT            25
#define DISPLAY_WIDTH         480
#define DISPLAY_HEIGHT        320

// index to date/time value array
#define DATE    0
#define MONTH   1
#define YEAR    2
#define DAY     3
#define HOUR    4
#define MINUTE  5
#define SECOND  6
#define HUNDSEC 7

#define BUTTON_COUNT 4
// user inputs
#define BUTTON_1 0
#define BUTTON_2 1
#define BUTTON_3 2
#define BUTTON_4 3
#define BUTTON_RELEASED 0
#define BUTTON_PRESSED  1
#define BUTTON_DEBOUNCE_DELAY   20   // ms
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

String GetStringTime();
String GetStringDate();
bool UpdateTime();

char buf[512];

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
CarSettings* cars;
// tire temp array
float tireTemps[60];
float currentTemps[60];

String curMenu[50];
int menuResult[50];
struct MenuChoice
{
  String description;
  int result;
};
MenuChoice choices[50];

// devices
// thermocouple amplifier
MCP9600 tempSensor;
// rtc
#ifdef RTC_RV1805
RV1805 rtc;
#endif
#ifdef RTC_RV8803
RV8803 rtc;
#endif
#ifdef HILETGO_DS3231
DS3231 rtc;
#endif

bool century = false;
bool h12Flag;
bool pmFlag;
String days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri","Sat"};
String ampmStr[3] = {"am", "pm", "\0"};

#define MAX_DISPLAY_LINES 12
// TFT display
TFT_eSPI tftDisplay = TFT_eSPI();
// location of text
int textPosition[2] = {5, 0};

int tempIdx = 0;
float tempStableRecent[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
float tempStableMinMax[2] = {150.0, -150.0};
bool tempStable = false;
bool tempUnits = false; // true for C, false for F

int carCount = 0;
int selectedCar = 0;
int tireIdx = 0;
int measIdx = 0;
float tempRes = 1.0;
int deviceState = 0;

String curTimeStr;
unsigned long prior = 0;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
const char* ssid     = "Yamura-Pyrometer";
const char* pass = "ZoeyDora48375";
IPAddress IP;
AsyncWebServer server(80);
AsyncWebSocket wsServoInput("/ServoInput");
String htmlStr;

// function prototypes
void WriteResultsHTML();
void ParseResult(char buf[], CarSettings &currentResultCar);
void DeleteFile(fs::FS &fs, const char * path);
void AppendFile(fs::FS &fs, const char * path, const char * message);
void ReadLine(File file, char* buf);
void DisplaySelectedResults(fs::FS &fs, const char * path);
void WriteSetupFile(fs::FS &fs, const char * path);
void ReadSetupFile(fs::FS &fs, const char * path);
void ResetTempStable();
bool CheckTempStable(float curTemp);
void DeleteDataFile(bool verify = true);
void SetDateTime();
void SetUnits();
void ChangeSettings();
void SelectCar();
int MenuSelect(MenuChoice choices[], int menuCount, int linesToDisplay, int initialSelect);
int MeasureTireTemps(int tire);
void DisplayAllTireTemps(CarSettings currentResultCar);
void MeasureAllTireTemps();
void DisplayMenu();
void InstantTemp();
void CheckButtons(unsigned long curTime);
void YamuraBanner();
void DrawGrid(int tireCount);
void DrawCellText(int row, int col, char* text, uint16_t textColor, uint16_t backColor);
void SetupGrid(const GFXfont* font);
int GetNextTire(int selTire, int nextDirection);
//
// 
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
  
  #ifdef DEBUG_VERBOSE
  delay(1000);
  Serial.println();
  Serial.println();
  Serial.println("YamuraLog Recording Tire Pyrometer V1.0");
  Serial.println("Pin assignments");
  Serial.println("Buttons");
  Serial.println("----------------");
  Serial.print("\tButton 1 ");
  Serial.println(BUTTON_1);
  Serial.print("\tButton 2 ");
  Serial.println(BUTTON_2);
  Serial.print("\tButton 3 ");
  Serial.println(BUTTON_3);
  Serial.print("\tButton 4 ");
  Serial.println(BUTTON_4);

  Serial.println("SPI bus");
  Serial.println("-------");
  Serial.print("\tSCK ");
  Serial.println(SCK);
  Serial.print("\tMISO ");
  Serial.println(MISO);
  Serial.print("\tMOSI ");
  Serial.println(MOSI);
  
  Serial.println("microSD (SPI)");
  Serial.println("-------------");
  Serial.print("\tCS ");
  Serial.println(sd_CS);  
  
  Serial.println("TFT display (SPI)");
  Serial.println("-----------------");
  Serial.print("\tTFT_CS ");
  Serial.println(TFT_CS);  
  Serial.print("\tTFT_DC ");
  Serial.println(TFT_DC);  
  Serial.print("\tTFT_RST ");
  Serial.println(TFT_RST);  
  
  Serial.println("I2C bus");
  Serial.println("-------");
  Serial.print("\tI2C_SDA ");
  Serial.println(I2C_SDA);
  Serial.print("\tI2C_SCL ");
  Serial.println(I2C_SCL);

  #endif
  // setup buttons on SX1509
  buttons[0].buttonPin = 12;
  buttons[1].buttonPin = 14;
  buttons[2].buttonPin = 26;
  buttons[3].buttonPin = 27;
  // set up tft display
  tftDisplay.init();
  int w = tftDisplay.width();
  int h = tftDisplay.height();
  textPosition[0] = 5;
  textPosition[1] = 0;
    // 0 portrait pins down
  // 1 landscape pins right
  // 2 portrait pins up
  // 3 landscape pins left
  tftDisplay.setRotation(1);
  SetupGrid(FSS12);
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();

  tftDisplay.setFreeFont(FSS12);
  tftDisplay.setTextDatum(TL_DATUM);
  int fontHeight = tftDisplay.fontHeight(GFXFF);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString("Yamura Electronics Recording Pyrometer", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  //
  #ifdef DEBUG_VERBOSE
  Serial.print("OLED screen size ");
  Serial.print(tftDisplay.width());
  Serial.print(" x ");
  Serial.print(tftDisplay.height());
  Serial.println("");
  #endif
  // start I2C
  #ifdef DEBUG_VERBOSE
  Serial.println("Start I2C");
  #endif
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
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple did not acknowledge! Freezing.");
    #endif
    tftDisplay.drawString("Thermocouple FAIL", textPosition[0], textPosition[1], GFXFF);
    delay(5000);
  }
  //check if the sensor is connected
  #ifdef DEBUG_VERBOSE
  Serial.println("Thermocouple Device acknowledged!");
  #endif
  tftDisplay.drawString("Thermocouple OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  delay(1000);
  //check if the Device ID is correct
  if(tempSensor.checkDeviceID())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple ID is correct!");        
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple ID is not correct! Freezing.");
    #endif
    while(1);
  }
  //change the thermocouple type being used
  #ifdef DEBUG_VERBOSE
  Serial.print("Current Thermocouple Type: ");
  #endif
  switch(tempSensor.getThermocoupleType())
  {
    case 0b000:
      #ifdef DEBUG_VERBOSE
      Serial.println("K Type");
      #endif
      break;
    case 0b001:
      #ifdef DEBUG_VERBOSE
      Serial.println("J Type");
      #endif
      break;
    case 0b010:
      #ifdef DEBUG_VERBOSE
      Serial.println("T Type");
      #endif
      break;
    case 0b011:
      #ifdef DEBUG_VERBOSE
      Serial.println("N Type");
      #endif
      break;
    case 0b100:
      #ifdef DEBUG_VERBOSE
      Serial.println("S Type");
      #endif
      break;
    case 0b101:
      #ifdef DEBUG_VERBOSE
      Serial.println("E Type");
      #endif
      break;
    case 0b110:
      #ifdef DEBUG_VERBOSE
      Serial.println("B Type");
      #endif
      break;
    case 0b111:
      #ifdef DEBUG_VERBOSE
      Serial.println("R Type");
      #endif
      break;
    default:
      #ifdef DEBUG_VERBOSE
      Serial.println("Unknown Thermocouple Type");
      #endif
      break;
  }
  if(tempSensor.getThermocoupleType() != TYPE_K)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Setting Thermocouple Type to K (0b000)");
    #endif
    tempSensor.setThermocoupleType(TYPE_K);
    //make sure the type was set correctly!
    if(tempSensor.getThermocoupleType() == TYPE_K)
    {
      #ifdef DEBUG_VERBOSE
      Serial.println("Thermocouple Type set sucessfully!");
      #endif
    }
    else
    {
      #ifdef DEBUG_VERBOSE
      Serial.println("Setting Thermocouple Type failed!");
      #endif
    }
  }
  #ifdef DEBUG_VERBOSE
  Serial.print("Ambient resolution ");
  #endif
  switch(tempSensor.getAmbientResolution())
  {
    case RES_ZERO_POINT_0625:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 0.0625C / 0.1125F");
      #endif
      tempRes = 0.1125;
      break;
    case RES_ZERO_POINT_25:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 0.25 C / 0.45F");
      #endif
      tempRes = 0.45;
      break;
    default:
      #ifdef DEBUG_VERBOSE
      Serial.print(" unknown ");
      #endif
      break;
  }
  #ifdef DEBUG_VERBOSE
  Serial.print(" Thermocouple resolution ");
  #endif
  switch(tempSensor.getThermocoupleResolution())
  {
    case RES_12_BIT:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 12 bit");
      #endif
      break;
    case RES_14_BIT:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 14 bit");
      #endif
      break;
    case RES_16_BIT:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 16 bit");
      #endif
      break;
    case RES_18_BIT:
      #ifdef DEBUG_VERBOSE
      Serial.print(" 18 bit");
      #endif
      break;
    default:
      #ifdef DEBUG_VERBOSE
      Serial.print(" unknown ");
      #endif
      break;
  }
  #ifdef HAS_RTC
  #ifndef HILETGO_DS3231
    if (rtc.begin() == false) 
    {
      #ifdef DEBUG_VERBOSE
      Serial.println("RTC not initialized, check wiring");
      #endif
      tftDisplay.drawString("RTC FAIL", textPosition[0], textPosition[1], GFXFF);
      while(true);
    }
    #ifdef DEBUG_VERBOSE
    Serial.println("RTC online!");
    #endif
    delay(1000);
  #endif
  #endif
  tftDisplay.drawString("RTC OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;

  Serial.println("Button setup");
  for(int idx = 0; idx < BUTTON_COUNT; idx++)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("Button ");
    Serial.print(idx);
    Serial.print(" Pin ");
    Serial.println(buttons[idx].buttonPin);
    #endif
    pinMode(buttons[idx].buttonPin, INPUT_PULLUP);
  }

  if(!SD.begin(sd_CS))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("microSD card mount Failed");
    #endif
    tftDisplay.drawString("microSD card mount failed", textPosition[0], textPosition[1], GFXFF);
    while(true);
  }
  tftDisplay.drawString("microSD OK", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("No SD card attached");
    #endif
    return;
  }
  #ifdef DEBUG_VERBOSE
  Serial.print("SD Card Type: ");
  #endif
  if(cardType == CARD_MMC)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("MMC");
    #endif
  }
  else if(cardType == CARD_SD)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("SDSC");
    #endif
  }
  else if(cardType == CARD_SDHC)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("SDHC");
    #endif
  } 
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("UNKNOWN");
    #endif
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  #ifdef DEBUG_VERBOSE
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  #endif
  // uncomment to write a default setup file
  // maybe check for setup and write one if needed?
  #ifdef WRITE_INI
  #ifdef DEBUG_VERBOSE
  Serial.println("Write setup file");
  #endif
  DeleteFile(SD, "/py_setup.txt");
  WriteSetupFile(SD, "/py_setup.txt");
  #endif
  ReadSetupFile(SD,  "/py_setup.txt");

  ResetTempStable();

  #ifdef DEBUG_VERBOSE
  Serial.println();
  Serial.println("Ready!");
  #endif
  #ifdef HAS_RTC
  //Updates the time variables from RTC
  if (UpdateTime() == false) 
  {
    #ifdef DEBUG_VERBOSE
    Serial.print("RTC failed to update");
    #endif
  }
  #endif
  #ifdef HAS_RTC
  sprintf(outStr, "%s %s", GetStringTime().c_str(), GetStringDate().c_str());
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #endif
  prior = millis();
  deviceState = DISPLAY_MENU;

  WriteResultsHTML();
  WiFi.softAP(ssid, pass);
  IP = WiFi.softAPIP();
  #ifdef DEBUG_VERBOSE
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.print(ssid);
  Serial.print(" ");
  Serial.println(pass);
  #endif
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  wsServoInput.onEvent(onServoInputWebSocketEvent);
  server.addHandler(&wsServoInput);
  server.begin();
  #ifdef DEBUG_VERBOSE
  Serial.print(IP);
  Serial.print(" ");
  Serial.println(pass);
  #endif
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  wsServoInput.onEvent(onServoInputWebSocketEvent);
  server.addHandler(&wsServoInput);
  server.begin();
  sprintf(outStr, "IP %d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  sprintf(outStr, "Password %s", pass);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #ifdef DEBUG_VERBOSE
  Serial.println("HTTP server started");
  Serial.println(outStr);
  #endif
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
    default:
      break;
  }
}
//
//
//
void DisplayMenu()
{
  int menuCount = 6;
  choices[0].description = "Measure Temps";                   choices[0].result = MEASURE_TIRES;
  choices[1].description = cars[selectedCar].carName.c_str(); choices[1].result = SELECT_CAR;
  choices[2].description = "Display Temps";                   choices[2].result = DISPLAY_TIRES;
  choices[3].description = "Instant Temp";                    choices[3].result = INSTANT_TEMP;
  choices[4].description = "Display Results";                 choices[4].result = DISPLAY_SELECTED_RESULT;
  choices[5].description = "Settings";                        choices[5].result = CHANGE_SETTINGS;
  
  deviceState =  MenuSelect(choices, menuCount, MAX_DISPLAY_LINES, MEASURE_TIRES); 
}
//
// measure temperatures on a single tire
// called by MeasureAllTireTemps
//
int MeasureTireTemps(int tireIdx)
{
  char outStr[512];
  textPosition[0] = 5;
  textPosition[1] = 0;
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
  #ifdef DEBUG_VERBOSE
  Serial.print("Start tire measurement for ");
  Serial.print(cars[selectedCar].carName.c_str());
  Serial.print(" tire ");
  Serial.println(cars[selectedCar].tireLongName[tireIdx].c_str());
  #endif
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
      #ifdef DEBUG_VERBOSE
      Serial.print("Armed tire ");
      Serial.print(tireIdx);
      Serial.print(" position ");
      Serial.println(measIdx);
      #endif
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
        #ifdef DEBUG_VERBOSE
        Serial.print("Save temp tire ");
        Serial.print(tireIdx);
        Serial.print(" position ");
        Serial.println(measIdx);
        #endif
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
        tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = tempSensor.getThermocoupleTemp(tempUnits); // false for F, true or empty for C
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
  #ifdef DEBUG_VERBOSE
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int idxMeas = 0; idxMeas < cars[selectedCar].positionCount; idxMeas++)
    {
      Serial.println(tireTemps[(idxTire * cars[selectedCar].positionCount) + idxMeas]);
    }
  }
  Serial.println("End tire measurement");
  #endif
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
  textPosition[0] = 5;
  textPosition[1] = 0;
  randomSeed(100);
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  tftDisplay.setFreeFont(FSS18);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString("Temperature", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] +=  tftDisplay.fontHeight();
  while(true)
  {
    curTime = millis();
    if(curTime - priorTime > 1000)
    {
      priorTime = curTime;
      // read temp
      #ifdef HAS_THERMO
      instant_temp = tempSensor.getThermocoupleTemp(tempUnits); // false for F, true or empty for C
      #else
      instant_temp = 100.0F;
      #endif
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("Instant temp ");
      Serial.println(instant_temp);
      #endif
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
void SetupGrid(const GFXfont* font)
{
    tftDisplay.setFreeFont(font);
	int fontHeight = tftDisplay.fontHeight(GFXFF);
  // 4 horizontal grid lines
  // x = 1 (start) 475 (end)
  // at y = 
  // FONT_HEIGHT +  10;
  // FONT_HEIGHT +  80;
  // FONT_HEIGHT + 150;
  // FONT_HEIGHT + 220;
  gridLineH[0][0][0] = 1;      gridLineH[0][0][1] = fontHeight +   10;
  gridLineH[0][1][0] = 475;    gridLineH[0][1][1] = fontHeight +   10;
  
  gridLineH[1][0][0] = 1;      gridLineH[1][0][1] = fontHeight +   80;
  gridLineH[1][1][0] = 475;    gridLineH[1][1][1] = fontHeight +   80;
  
  gridLineH[2][0][0] = 1;      gridLineH[2][0][1] = fontHeight +  150;
  gridLineH[2][1][0] = 475;    gridLineH[2][1][1] = fontHeight +  150;
  
  gridLineH[3][0][0] = 1;      gridLineH[3][0][1] = fontHeight +  220;
  gridLineH[3][1][0] = 475;    gridLineH[3][1][1] = fontHeight +  220;
  
  // 3 vertical grid lines
  // at x = 
  // 1
  // 237
  // 475
  gridLineV[0][0][0] = 1;       gridLineV[0][0][1] = fontHeight +  10;
  gridLineV[0][1][0] = 1;       gridLineV[0][1][1] = fontHeight + 220;
  gridLineV[1][0][0] = 237;     gridLineV[1][0][1] = fontHeight +  10;
  gridLineV[1][1][0] = 237;     gridLineV[1][1][1] = fontHeight + 220;
  gridLineV[2][0][0] = 475;     gridLineV[2][0][1] = fontHeight +  10;
  gridLineV[2][1][0] = 475;     gridLineV[2][1][1] = fontHeight + 220;
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
  cellPoint[1][0][0] = gridLineV[0][0][0] +   5;              cellPoint[1][0][1] = gridLineH[0][0][1] + fontHeight + 5;
  cellPoint[1][1][0] = gridLineV[0][0][0] +  75;              cellPoint[1][1][1] = gridLineH[0][0][1] + fontHeight + 5;
  cellPoint[1][2][0] = gridLineV[0][0][0] + 145;              cellPoint[1][2][1] = gridLineH[0][0][1] + fontHeight + 5;
  cellPoint[1][3][0] = gridLineV[1][0][0] + 145;              cellPoint[1][3][1] = gridLineH[0][0][1] + fontHeight + 5;
  cellPoint[1][4][0] = gridLineV[1][0][0] +  75;              cellPoint[1][4][1] = gridLineH[0][0][1] + fontHeight + 5;
  cellPoint[1][5][0] = gridLineV[1][0][0] +   5;              cellPoint[1][5][1] = gridLineH[0][0][1] + fontHeight + 5;
  // grid row 1
  // cell row 2
  cellPoint[2][0][0] = gridLineV[0][0][0] +   5;              cellPoint[2][0][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][1][0] = gridLineV[0][0][0] +  75;              cellPoint[2][1][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][2][0] = gridLineV[0][0][0] + 145;              cellPoint[2][2][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][3][0] = gridLineV[1][0][0] + 145;              cellPoint[2][3][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][4][0] = gridLineV[1][0][0] +  75;              cellPoint[2][4][1] = gridLineH[1][0][1] + 5;
  cellPoint[2][5][0] = gridLineV[1][0][0] +   5;              cellPoint[2][5][1] = gridLineH[1][0][1] + 5;
  // cell row 3
  cellPoint[3][0][0] = gridLineV[0][0][0] +   5;              cellPoint[3][0][1] = gridLineH[1][0][1] + fontHeight + 5;
  cellPoint[3][1][0] = gridLineV[0][0][0] +  75;              cellPoint[3][1][1] = gridLineH[1][0][1] + fontHeight + 5;
  cellPoint[3][2][0] = gridLineV[0][0][0] + 145;              cellPoint[3][2][1] = gridLineH[1][0][1] + fontHeight + 5;
  cellPoint[3][3][0] = gridLineV[1][0][0] + 145;              cellPoint[3][3][1] = gridLineH[1][0][1] + fontHeight + 5;
  cellPoint[3][4][0] = gridLineV[1][0][0] +  75;              cellPoint[3][4][1] = gridLineH[1][0][1] + fontHeight + 5;
  cellPoint[3][5][0] = gridLineV[1][0][0] +   5;              cellPoint[3][5][1] = gridLineH[1][0][1] + fontHeight + 5;
  // grid row 2
  // cell row 4
  cellPoint[4][0][0] = gridLineV[0][0][0] +   5;              cellPoint[4][0][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][1][0] = gridLineV[0][0][0] +  75;              cellPoint[4][1][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][2][0] = gridLineV[0][0][0] + 145;              cellPoint[4][2][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][3][0] = gridLineV[1][0][0] + 145;              cellPoint[4][3][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][4][0] = gridLineV[1][0][0] +  75;              cellPoint[4][4][1] = gridLineH[2][0][1] + 5;
  cellPoint[4][5][0] = gridLineV[1][0][0] +   5;              cellPoint[4][5][1] = gridLineH[2][0][1] + 5;
  // cell row 5
  cellPoint[5][0][0] = gridLineV[0][0][0] +   5;              cellPoint[5][0][1] = gridLineH[2][0][1] + fontHeight + 5;
  cellPoint[5][1][0] = gridLineV[0][0][0] +  75;              cellPoint[5][1][1] = gridLineH[2][0][1] + fontHeight + 5;
  cellPoint[5][2][0] = gridLineV[0][0][0] + 145;              cellPoint[5][2][1] = gridLineH[2][0][1] + fontHeight + 5;
  cellPoint[5][3][0] = gridLineV[1][0][0] + 145;              cellPoint[5][3][1] = gridLineH[2][0][1] + fontHeight + 5;
  cellPoint[5][4][0] = gridLineV[1][0][0] +  75;              cellPoint[5][4][1] = gridLineH[2][0][1] + fontHeight + 5;
  cellPoint[5][5][0] = gridLineV[1][0][0] +   5;              cellPoint[5][5][1] = gridLineH[2][0][1] + fontHeight + 5;
  // cell row 6
  cellPoint[6][0][0] = gridLineV[0][0][0] +   5;              cellPoint[6][0][1] = gridLineH[3][0][1] + fontHeight + 10;
  cellPoint[6][1][0] = gridLineV[0][0][0] +  75;              cellPoint[6][1][1] = gridLineH[3][0][1] + fontHeight + 10;
  cellPoint[6][2][0] = gridLineV[0][0][0] + 145;              cellPoint[6][2][1] = gridLineH[3][0][1] + fontHeight + 10;
  cellPoint[6][3][0] = gridLineV[1][0][0] + 145;              cellPoint[6][3][1] = gridLineH[3][0][1] + fontHeight + 10;
  cellPoint[6][4][0] = gridLineV[1][0][0] +  75;              cellPoint[6][4][1] = gridLineH[3][0][1] + fontHeight + 10;
  cellPoint[6][5][0] = gridLineV[1][0][0] +   5;              cellPoint[6][5][1] = gridLineH[3][0][1] + fontHeight + 10;
}
///
///
///
void DisplayAllTireTemps(CarSettings currentResultCar)
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  textPosition[0] = 5;
  textPosition[1] = 0;
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
  #ifdef DEBUG_VERBOSE
  Serial.print("Results for car: ");
  Serial.print(currentResultCar.carName.c_str());
  Serial.print(" ");
  Serial.print(" on ");
  Serial.print(currentResultCar.dateTime.c_str());
  Serial.print(" ");
  Serial.println(outStr);
  #endif
  tftDisplay.setFreeFont(FSS12);
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
      buttons[3].buttonReleased = false;
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
  
  while(true)
  {
    #ifdef DEBUG_VERBOSE
    Serial.print("Measure tire ");
    Serial.print(selTire);
    Serial.println(" or select another tire");
    #endif
    sprintf(outStr, "%s", cars[selectedCar].carName.c_str());
    #ifdef DEBUG_VERBOSE
    Serial.print("Measuring car: ");
    Serial.print(cars[selectedCar].carName.c_str());
    Serial.println(outStr);
    #endif
    tftDisplay.setFreeFont(FSS12);
    tftDisplay.setTextDatum(TL_DATUM);
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
          int fontHeight = tftDisplay.fontHeight(GFXFF);
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
        Serial.print("Done measuring ");
        Serial.print(selTire);
        Serial.println(" find next tire to measure");
        selTire = GetNextTire(selTire, nextDirection);
        Serial.print("next tire is ");
        Serial.println(selTire);
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
          #ifdef DEBUG_VERBOSE
          Serial.println("Start MEARURE");
          #endif
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
        #ifdef DEBUG_VERBOSE
        Serial.println("Select NEXT tire");
        #endif
        selTire = GetNextTire(selTire, 1);
        buttons[1].buttonReleased = false;
        break;
      }
      else if ((buttons[2].buttonReleased)) 
      {
        #ifdef DEBUG_VERBOSE
        Serial.println("Select PRIOR tire");
        #endif
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
  tftDisplay.setFreeFont(FSS18);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  textPosition[0] = 5;
  textPosition[1] = 5;

  tftDisplay.drawString("Done", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += tftDisplay.fontHeight();
  tftDisplay.drawString("Storing results...", textPosition[0], textPosition[1], GFXFF);
    // done, copy local to global
  #ifdef HAS_RTC
  UpdateTime();
  cars[selectedCar].dateTime = GetStringTime();
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
      //tireTemps[(idxTire * cars[selectedCar].positionCount) + idxPosition] = currentTemps[(idxTire * cars[selectedCar].positionCount) + idxPosition];
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
  #ifdef DEBUG_VERBOSE
  Serial.println(fileLine);
  Serial.println("Add to results file...");
  #endif
  
  sprintf(outStr, "/py_temps_%d.txt", selectedCar);
  
  #ifdef DEBUG_VERBOSE
  Serial.print("Write results to: ");
  Serial.println(outStr);
  #endif

  AppendFile(SD, outStr, fileLine.c_str());
  // update the HTML file
  #ifdef DEBUG_VERBOSE
  Serial.println("Update HTML file...");
  #endif
  textPosition[1] += tftDisplay.fontHeight();
  tftDisplay.drawString("Updating HTML...", textPosition[0], textPosition[1], GFXFF);
  WriteResultsHTML();  
}
//
//
//
int GetNextTire(int selTire, int nextDirection)        
{
  Serial.print("next tire direction ");
  Serial.print(nextDirection);
  Serial.print(" last tire  ");
  Serial.print(selTire);
  selTire += nextDirection;
  while(true)
  {
    if(selTire < 0)
    {
Serial.println("backward past tire 0, select 'done'");
      selTire = cars[selectedCar].tireCount;
    }
    if(selTire > cars[selectedCar].tireCount)
    {
Serial.println("forward past done, select tire 0");
      selTire = 0;
    }
    Serial.print("next tire ");
    Serial.println(selTire);
    if(selTire == cars[selectedCar].tireCount)
    {
      Serial.println("go to DONE");
      break;
    }
    Serial.println();
    Serial.print("first temp for car ");
    Serial.print(selectedCar);
    Serial.print(" tire ");
    Serial.print(selTire);
    Serial.print(" ");
    Serial.println(tireTemps[(selTire * cars[selectedCar].positionCount)]);
    if (tireTemps[(selTire * cars[selectedCar].positionCount)] == 0.0)
    {
      Serial.print("go to tire ");
      Serial.println(selTire);
      break;
    }
    Serial.println("next tire already measured! skip!");
    selTire += nextDirection;
  }
  Serial.print("start measuring tire ");
  Serial.println(selTire);
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
  tftDisplay.setFreeFont(FSS12);
  tftDisplay.setTextDatum(TL_DATUM);
  tftDisplay.setTextColor(textColor, backColor);
  tftDisplay.drawString(outStr, cellPoint[row][col][0], cellPoint[row][col][1], GFXFF);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
}
//
//
//
int MenuSelect(MenuChoice choices[], int menuCount, int linesToDisplay, int initialSelect)
{
  char outStr[256];
  textPosition[0] = 5;
  textPosition[1] = 0;
  currentMillis = millis();
  // find initial selection
  int selection = initialSelect;
  for(int selIdx = 0; selIdx < menuCount; selIdx++)
  {
    if(choices[selIdx].result == initialSelect)
    {
      selection = selIdx;
    }
  }
  // range of selections to display (allow scrolling)
  int displayRange[2] = {0, linesToDisplay - 1 };
  displayRange[1] = (menuCount < linesToDisplay ? menuCount : linesToDisplay) - 1;
  // reset buttons
  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttons[btnIdx].buttonReleased = false;
  }
  // erase screen, draw banner
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  // loop until selection is made
  tftDisplay.setFreeFont(FSS18);
  tftDisplay.setTextDatum(TL_DATUM);
  int fontHeight = tftDisplay.fontHeight(GFXFF);
  while(true)
  {
    textPosition[0] = 5;
    textPosition[1] = 0;
    for(int menuIdx = displayRange[0]; menuIdx <= displayRange[1]; menuIdx++)
    {
      sprintf(outStr, "%s", choices[menuIdx].description.c_str());
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

  for(int idx = 0; idx < carCount; idx++)
  {
    choices[idx].description = cars[idx].carName;
    choices[idx].result = idx; 
  }
  selectedCar =  MenuSelect(choices, carCount, MAX_DISPLAY_LINES, 0); 
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.print("Selected car from SelectCar() ") ;
  Serial.print(selectedCar) ;
  Serial.print(" ") ;
  Serial.println(cars[selectedCar].carName.c_str());
  #endif
}
//
//
//
void ChangeSettings()
{
  int menuCount = 6;
  int result =  0;
  while(result != 3)
  {
    choices[0].description = "Set Date/Time"; choices[0].result = 0;
    choices[1].description = "Set Units";     choices[1].result = 1;
    choices[2].description = "Delete Data";   choices[2].result = 2;
    sprintf(buf, "IP %d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
    choices[3].description = buf;             choices[3].result = 4;
    sprintf(buf, "Pass %s", pass);
    choices[4].description = buf;             choices[4].result = 4;
    choices[5].description = "Exit";          choices[5].result = 3;
    result =  MenuSelect(choices, menuCount, MAX_DISPLAY_LINES, 0); 
    switch(result)
    {
      case 0:
        SetDateTime();
        break;
      case 1:
        SetUnits();
        break;
      case 2:
        DeleteDataFile();
        break;
      default:
        break;
    }
  }
}
//
//
//
void SetUnits()
{
  int menuCount = 2;
  choices[0].description = "Temp in F";   choices[0].result = 0;
  choices[1].description = "Temp in C";   choices[1].result = 1;
  int menuResult =  MenuSelect(choices, menuCount, MAX_DISPLAY_LINES, 0); 
  // true for C, false for F
  tempUnits = menuResult == 1;
}
//
//
//
void SetDateTime()
{
  char outStr[256];
  int timeVals[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // date, month, year, day, hour, min, sec, 100ths
  bool isPM = false;
  textPosition[0] = 5;
  textPosition[1] = 0;
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

  if (UpdateTime() == false) 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("RTC failed to update");
    #endif
  }
  timeVals[DATE] = rtc.getDate();
  timeVals[MONTH] = rtc.getMonth(century);
  if (century) 
  {
    timeVals[YEAR] += 100;
	}
  timeVals[DAY] = rtc.getDoW();
  timeVals[HOUR] = rtc.getHour(h12Flag, isPM);
  timeVals[MINUTE] = rtc.getMinute();
  //isPM = rtc.isPM();
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.print("GET Date/Time DATE: ");
  Serial.print(timeVals[DATE]);
  Serial.print(" MONTH: ");
  Serial.print(timeVals[MONTH]);
  Serial.print(" DAY OF WEEK: ");
  Serial.print(timeVals[DAY]);
  Serial.print(" YEAR: ");
  Serial.print(timeVals[YEAR]);
  Serial.print(" HOUR: ");
  Serial.print(timeVals[HOUR]);
  Serial.print(" MINUTE: ");
  Serial.print(timeVals[MINUTE]);
  Serial.print(" AM/PM: ");
  Serial.print(isPM ? "PM" : "AM");
  Serial.println();
  #endif
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
    
    textPosition[1] += FONT_HEIGHT;

    sprintf(outStr, "%02d/%02d/%04d %s", timeVals[DATE], timeVals[MONTH], timeVals[YEAR], days[timeVals[DAY]]);
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    if(setIdx < 4)
    {
      textPosition[1] += FONT_HEIGHT;
      sprintf(outStr, "%s %s %s %s", (setIdx == 0 ? "dd" : "  "), (setIdx == 1 ? "mm" : "  "), (setIdx == 2 ? "yyyy" : "    "), (setIdx == 3 ? "ww" : "  "));
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    }
    textPosition[1] += FONT_HEIGHT;
    sprintf(outStr, "%02d:%02d %s", timeVals[HOUR], timeVals[MINUTE], (isPM ? "PM" : "AM"));
    tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    if(setIdx > 3)
    {
      textPosition[1] += FONT_HEIGHT;
      sprintf(outStr, "%s %s %s", (setIdx == 4 ? "hh" : "  "), (setIdx == 5 ? "mm" : "  "), (setIdx == 6 ? "ap" : "  "));
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
        case 0:  // date
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
        case 1:  // month
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
        case 2:  // year
          if(timeVals[YEAR] < 2020)
          {
            timeVals[YEAR] = 2020;
          }
          if(timeVals[YEAR] > 2100)
          {
            timeVals[YEAR] = 2020;
          }
          timeVals[YEAR] += delta;
          break;
        case 3:  // day
          timeVals[DAY] += delta;
          if(timeVals[DAY] < 0)
          {
            timeVals[DAY] = 6;
          }
          if(timeVals[DAY] > 6)
          {
            timeVals[DAY] = 0;
          }
          break;
        case 4:  // hour
          timeVals[HOUR] += delta;
          if(timeVals[HOUR] < 0)
          {
            timeVals[HOUR] = 12;
          }
          if(timeVals[HOUR] > 12)
          {
            timeVals[HOUR] = 1;
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
    if(setIdx > 6)
    {
      break;
    }
  }
  if(isPM)
  {
    if (timeVals[HOUR] < 12)
    {
      timeVals[HOUR] += 12;
    }
  }
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.print("SET Date/Time DATE: ");
  Serial.print(timeVals[DATE]);
  Serial.print(" MONTH: ");
  Serial.print(timeVals[MONTH]);
  Serial.print(" DAY OF WEEK: ");
  Serial.print(timeVals[DAY]);
  Serial.print(" YEAR: ");
  Serial.print(timeVals[YEAR]);
  Serial.print(" HOUR: ");
  Serial.print(timeVals[HOUR]);
  Serial.print(" MINUTE: ");
  Serial.print(timeVals[MINUTE]);
  Serial.print(" AM/PM: ");
  Serial.print(isPM ? "PM" : "AM");
  Serial.println();
  #endif
  if (century) 
  {
    rtc.setYear(timeVals[YEAR] - 2100);
	}
  else
  {
    rtc.setYear(timeVals[YEAR] - 2000);
  }
  rtc.setMonth(timeVals[MONTH]);
  rtc.setDate(timeVals[DATE]);
  rtc.setDoW(timeVals[DAY]);
  rtc.setHour(timeVals[HOUR]);
  rtc.setMinute(timeVals[MINUTE]);
  rtc.setSecond(timeVals[SECOND]);
  rtc.setClockMode(isPM);
  textPosition[1] += FONT_HEIGHT * 2;
  tftDisplay.drawString(GetStringTime(), textPosition[0], textPosition[1], GFXFF);
  delay(5000);
}
//
//
//
void DeleteDataFile(bool verify)
{
  int menuResult = 1;
  if(verify)
  {
    int menuCount = 2;
    choices[0].description = "Yes";      choices[0].result = 1;
    choices[1].description = "No";   choices[1].result = 0;
    menuResult = MenuSelect (choices, menuCount, MAX_DISPLAY_LINES, 1); 
  }
  if(menuResult == 1)
  {
    #ifdef DEBUG_HTML
    Serial.println("Delete data and html files");
    #endif
    int dataIdx = 0;
    char nameBuf[128];
    for(int dataIdx = 0; dataIdx < 100; dataIdx++)
    {
      sprintf(nameBuf, "/py_temps_%d.txt", dataIdx);
      if(!SD.exists(nameBuf))
      {
        #ifdef DEBUG_EXTRA_VERBOSE
        Serial.print(nameBuf);
        Serial.println(" does not exist");
        #endif
        continue;
      }
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print(nameBuf);
      Serial.println(" deleted");
      #endif
      DeleteFile(SD, nameBuf);
    }
    DeleteFile(SD, "/py_res.html");
    // create the HTML header
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Write empty html");
    #endif
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
void ReadSetupFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.print("Read setup file ");
  Serial.println(path);
  Serial.printf("Reading file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_READ);
  if(!file)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
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
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.println();
  Serial.println("Cars Structure:");
  Serial.println("===============");
  Serial.print("Defined cars ");
  Serial.println(carCount);
  Serial.println("===============");
  for(int carIdx = 0; carIdx < carCount; carIdx++)
  {
    Serial.print("Car: ");
    Serial.println(cars[carIdx].carName.c_str());
    Serial.print("Tires (");
    Serial.print(cars[carIdx].tireCount);
    Serial.println(")");
    for(int idx = 0; idx < cars[carIdx].tireCount; idx++)
    {
      Serial.print(cars[carIdx].tireShortName[idx]);
      Serial.print(" ");
      Serial.print(cars[carIdx].tireLongName[idx]);
      Serial.print(" ");
      Serial.println(cars[carIdx].maxTemp[idx]);
    }

    Serial.print("Measurements (");
    Serial.print(cars[carIdx].positionCount);
    Serial.println(")");
    for(int idx = 0; idx <  cars[carIdx].positionCount; idx++)
    {
      Serial.print(cars[carIdx].positionShortName[idx]);
      Serial.print(" ");
      Serial.println(cars[carIdx].positionLongName[idx]);
    }
    Serial.println("=====");
  }
  #endif
  file.close();
}
//
//
//
void WriteSetupFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.printf("Writing setup file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Failed to open file for writing");
    #endif
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
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.println("File written");
  #endif
}
//
//
//
void DisplaySelectedResults(fs::FS &fs, const char * path)
{
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
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
    tftDisplay.fillScreen(TFT_BLACK);
    YamuraBanner();
    tftDisplay.drawString("No results for", 5, 0,  GFXFF);
    tftDisplay.drawString(cars[selectedCar].carName.c_str(), 5, FONT_HEIGHT, GFXFF);
    tftDisplay.drawString("Select another car", 5, 2* FONT_HEIGHT, GFXFF);
    delay(5000);
    return;
  }
  int menuCnt = 0;
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf) == 0)
    {
      break;
    }
    token = strtok(buf, ";");
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("result (");
    Serial.print(menuCnt);
    Serial.print(") ");
    Serial.println(token);
    #endif
    choices[menuCnt].description = token;      choices[menuCnt].result = menuCnt;
    menuCnt++;
  }
  int menuResult = MenuSelect(choices, menuCnt, MAX_DISPLAY_LINES, 0);
  file.close();
  // at this point, we need to parse the selected line and add to a measurment structure for display
  // get to the correct line
  file = SD.open(path, FILE_READ);
  if(!file)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
    tftDisplay.fillScreen(TFT_BLACK);
    YamuraBanner();
    tftDisplay.drawString("No results for", 5, 0, GFXFF);
    tftDisplay.drawString(cars[selectedCar].carName.c_str(), 5, FONT_HEIGHT, GFXFF);
    tftDisplay.drawString("Select another car", 5, 2* FONT_HEIGHT, GFXFF);
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
  char nameBuf[128];
  htmlStr = "";
  CarSettings currentResultCar;
  File fileIn;
  // create a new HTML file
  if(!SD.remove("/py_res.html"))
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("failed to delete /py_res.html");
    #endif
  }
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
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print(nameBuf);
      Serial.println(" failed to open for reading");
      #endif
      continue;
    }
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print(nameBuf);
    Serial.println(" opened for reading");
    #endif
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
            #ifdef DEBUG_HTML
            Serial.println(buf);
            #endif
            AppendFile(SD, "/py_res.html", buf);
          }
        }
        AppendFile(SD, "/py_res.html", "        </tr>");
      }
      outputSubHeader = false;
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("begin new row");
      #endif
      rowCount++;
      AppendFile(SD, "/py_res.html", "		    <tr>");
      sprintf(buf, "<td>%s</td>", currentResultCar.dateTime.c_str());
      AppendFile(SD, "/py_res.html", buf);
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("date time ");
      Serial.println(buf);
      #endif
      sprintf(buf, "<td>%s</td>", currentResultCar.carName.c_str());
      AppendFile(SD, "/py_res.html", buf);
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("car ");
      Serial.println(buf);
      #endif
      for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
      {
        tireMin =  999.9;
        tireMax = -999.9;
        //sprintf(buf, "<td>%s</td>", currentResultCar.tireShortName[t_idx].c_str());
        #ifdef DEBUG_EXTRA_VERBOSE
        Serial.print("tire ");
        Serial.println(buf);
        #endif
        // get min/max temps
        for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
        {
          tireMin = tireMin < tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] ? tireMin : tireTemps[(t_idx * currentResultCar.positionCount) + p_idx];
          tireMax = tireMax > tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] ? tireMax : tireTemps[(t_idx * currentResultCar.positionCount) + p_idx];
        }
        #ifdef DEBUG_EXTRA_VERBOSE
        Serial.print("tire temp range ");
        Serial.print(tireMin);
        Serial.print(" - ");
        Serial.println(tireMax);
        Serial.print(" overheat ");
        Serial.println(currentResultCar.maxTemp[t_idx]);
        #endif
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
          #ifdef DEBUG_EXTRA_VERBOSE
          Serial.print("tire ");
          Serial.print(t_idx);
          Serial.print("position ");
          Serial.print(p_idx);
          Serial.print(" ");
          Serial.println(buf);
          #endif
          AppendFile(SD, "/py_res.html", buf);
        }
      }
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("end of row");
      #endif
      AppendFile(SD, "/py_res.html", "		    </tr>");
    }
  }
  if(rowCount == 0)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("empty data file - begin blank row");
    #endif
    rowCount++;
    AppendFile(SD, "/py_res.html", "		    <tr>");
    sprintf(buf, "<td>---</td>");
    AppendFile(SD, "/py_res.html", buf);
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("date time ");
    Serial.println(buf);
    #endif
    sprintf(buf, "<td>---</td>");
    AppendFile(SD, "/py_res.html", buf);
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("car ");
    Serial.println(buf);
    #endif
    for(int t_idx = 0; t_idx < 4; t_idx++)
    {
      tireMin =  999.9;
      tireMax = -999.9;
      sprintf(buf, "<td>---</td>");
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print("tire ");
      Serial.println(buf);
      #endif
      AppendFile(SD, "/py_res.html", buf);
      // add cells to file
      for(int p_idx = 0; p_idx < 3; p_idx++)
      {
        sprintf(buf, "<td>---</td>");
        AppendFile(SD, "/py_res.html", buf);
      }
    }
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("end of row");
    #endif
    AppendFile(SD, "/py_res.html", "		    </tr>");
  }
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.println("end of file");
  #endif
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
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.print("Current token ");
    Serial.print(tokenIdx);
    Serial.print(" >>>>");
    Serial.print(token);
    Serial.println("<<<< ");
    #endif
    // tokenIdx 0 is date/time
    // 0 - timestamp
    if(tokenIdx == 0)
    {
      currentResultCar.dateTime = token;      
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println("Timestamp");
      #endif
    }
    // 1 - car info
    if(tokenIdx == 1)
    {
      currentResultCar.carName = token;
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" car");
      #endif
    }
    // 2 - tire count
    else if(tokenIdx == 2)
    {
      currentResultCar.tireCount = atoi(token);
      currentResultCar.tireShortName = new String[currentResultCar.tireCount];
      currentResultCar.tireLongName = new String[currentResultCar.tireCount];
      currentResultCar.maxTemp = (float*)calloc(currentResultCar.tireCount, sizeof(float));
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" tires");
      #endif
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
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" positions");
      #endif
    }
    // tire temps
    else if((tokenIdx >= measureRange[0]) && (tokenIdx <= measureRange[1]))
    {
      tireTemps[measureIdx] = atof(token);
      currentTemps[measureIdx] = tireTemps[measureIdx];
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.print(" tireTemps[");
      Serial.print(measureIdx);
      Serial.print("] = ");
      Serial.println(currentTemps[measureIdx]);
      #endif
      measureIdx++;
    }
    // tire names
    else if((tokenIdx >= tireNameRange[0]) && (tokenIdx <= tireNameRange[1]))
    {
      currentResultCar.tireShortName[tireIdx] = token;
      currentResultCar.tireLongName[tireIdx] = token;
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" tire name");
      #endif
      tireIdx++;
    }
    // position names
    else if((tokenIdx >= posNameRange[0]) && (tokenIdx <= posNameRange[1]))
    {
      currentResultCar.positionShortName[positionIdx] = token;
      currentResultCar.positionLongName[positionIdx] = token;
      positionIdx++;
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" position name");
      #endif
    }
    // max temps
    else if((tokenIdx >= maxTempRange[0]) && (tokenIdx <= maxTempRange[1]))
    {
      currentResultCar.maxTemp[maxTempIdx] = atof(token);
      maxTempIdx++;
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.println(" max temp value");
      #endif
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
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.println(buf);
  #endif
  return;
}
//
//
//
void AppendFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.printf("Appending to file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_APPEND);
  if(!file)
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Failed to open file for appending");
    #endif
    return;
  }
  if(file.println(message))
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Message appended");
    #endif
    #ifdef DEBUG_HTML
    Serial.print("Message appended ");
    Serial.println(message);
    #endif 
  }
  else 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Append failed");
    #endif
  }
  file.close();
}
//
//
//
void DeleteFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.printf("Deleting file: %s\n", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("File deleted");
    #endif
  }
  else 
  {
    #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("Delete failed");
    #endif
  }
}
//
//
//
void handleRoot(AsyncWebServerRequest *request) 
{
  #ifdef DEBUG_EXTRA_VERBOSE
  Serial.println("send HOMEPAGE");
  #endif
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
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      #endif
      break;
    case WS_EVT_DISCONNECT:
      #ifdef DEBUG_EXTRA_VERBOSE
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      #endif
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
	int hour = 0;
	int minute = 0;
	int second = 0;
  hour = (int)rtc.getHour(h12Flag, pmFlag);
  minute = (int)rtc.getMinute();
  second = (int)rtc.getSecond();
  if (h12Flag) 
  {
		if (pmFlag) 
    {
      ampm = 1;
		} 
    else 
    {
      ampm = 0;
		}
	}
  else
  {
    ampm = 2;
  }
  sprintf(buf, "%02d:%02d%s", hour, minute, ampmStr[ampm]);
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
  int ampm;
	// send what's going on to the serial monitor.
	int year = 2000;
	int shortYear = 0;
	int month = 0;
	int date = 0;
  int dow = 0;
	if (century) 
  {
    year += 100;
	}
  shortYear = (int)rtc.getYear();
  year += shortYear;
	month = (int)rtc.getMonth(century);
 	date = (int)rtc.getDate();
  dow = (int)rtc.getDoW();
  sprintf(buf, "%02d/%02d/%02d", month, date, shortYear);
  rVal = buf;
  return rVal;
}
//
//
//
bool UpdateTime()
{
  return true;
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
  tftDisplay.setFreeFont(FSS9);     // Select the orginal small GLCD font by using NULL or GLCD
  int fontHt = tftDisplay.fontHeight(GFXFF);
  int xPos = tftDisplay.width()/2;
  int yPos = tftDisplay.height() - fontHt/2;
  tftDisplay.setTextDatum(BC_DATUM);
  tftDisplay.drawString("  Yamura Recording Tire Pyrometer v1.0  ",xPos, yPos, GFXFF);    // Print the font name onto the TFT screen
  tftDisplay.setTextDatum(TL_DATUM);
}