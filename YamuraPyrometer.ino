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
#include "LittleFS.h"
#include "SD.h"
#include "SPI.h" 
#include <SparkFun_MCP9600.h>    // https://github.com/sparkfun/SparkFun_MCP9600_Arduino_Library
#include "RTClib.h"              // PCF8563 RTC library https://github.com/adafruit/RTClib

// car info structure
struct CarSettings
{
    char carName[64];
    char dateTime[32];
    int tireCount;
    char tireShortName[6][16];
    char tireLongName[6][32];
    int positionCount;
    char positionShortName[6][16];
    char positionLongName[3][32];
    float maxTemp[6];
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
void ReadCarSetupFile(fs::FS &fs, const char * path);
void WriteCarSetupFile(fs::FS &fs, const char * path);
void WriteCarSetupHTML(fs::FS &fs, const char * path, int carIdx);
void ReadDeviceSetupFile(fs::FS &fs, const char * path);
void WriteDeviceSetupFile(fs::FS &fs, const char * path);
void DisplaySelectedResults(fs::FS &fs, const char * path);
void WriteResultsHTML();
void ParseResult(char buf[], CarSettings &currentResultCar);
void ReadLine(File file, char* buf);
void AppendFile(fs::FS &fs, const char * path, const char * message);
void DeleteFile(fs::FS &fs, const char * path);
//void handleRoot(AsyncWebServerRequest *request);
//void handleNotFound(AsyncWebServerRequest *request);
//void onServoInputWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
String GetStringTime();
String GetStringDate();
bool IsPM();
void CheckButtons(unsigned long curTime);
void YamuraBanner();
void SetFont(int fontSize);
float CtoFAbsolute(float tempC);
float CtoFRelative(float tempC);
float GetStableTemp(int row, int col);
void PrintTemp(float temp);
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);

// uncomment for debug to serial monitor (use #ifdef...#endif around debug output prints)
//#define DEBUG_VERBOSE
//#define DEBUG_EXTRA_VERBOSE
//#define DEBUG_HTML
//#define SET_TO_SYSTEM_TIME
// microSD chip select, I2C pins
#define SD_CS 5
#define I2C_SDA 21
#define I2C_SCL 22
#define THERMO_CS   25
#define THERMO_DO   19
#define THERMO_CLK  23
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
#define FORMAT_LITTLEFS_IF_FAILED true

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

#define TEMP_BUFFER 15
float tempValues[100];

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

int carCount = 0;
int selectedCar = 0;
int carSetupIdx = 0;
int tireIdx = 0;
int measIdx = 0;
float tempRes = 1.0;
int deviceState = 0;

IPAddress IP;
AsyncWebServer server(80);
//AsyncWebSocket wsServoInput("/ServoInput");
//String htmlStr;

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
  tftDisplay.invertDisplay(true);
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

#ifdef HAS_THERMO
  tempSensor.begin();       // Uses the default address (0x60) for SparkFun Thermocouple Amplifier
  if(tempSensor.isConnected())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple will acknowledge!");
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple did not acknowledge! Freezing.");
    #endif
    tftDisplay.drawString("Thermocouple did not acknowledge", textPosition[0], textPosition[1], GFXFF);
    textPosition[1] += fontHeight;
    while(1); //hang forever
  }
  //change the thermocouple type being used
  #ifdef DEBUG_VERBOSE
  Serial.println("Setting Thermocouple Type!");
  #endif
  tempSensor.setThermocoupleType(TYPE_K);
   //make sure the type was set correctly!
  switch(tempSensor.getThermocoupleType())
  {
    case TYPE_K:
      sprintf(outStr,"Thermocouple OK (Type K)");
      break;
    case TYPE_J:
      sprintf(outStr,"Thermocouple set failed (Type J");
      break;
    case TYPE_T:
      sprintf(outStr,"Thermocouple set failed (Type T");
      break;
    case TYPE_N:
      sprintf(outStr,"Thermocouple set failed (Type N");
      break;
    case TYPE_S:
      sprintf(outStr,"Thermocouple set failed (Type S");
      break;
    case TYPE_E:
      sprintf(outStr,"Thermocouple set failed (Type E");
      break;
    case TYPE_B:
      sprintf(outStr,"Thermocouple set failed (Type B");
      break;
    case TYPE_R:
      sprintf(outStr,"Thermocouple set failed (Type R");
      break;
    default:
      sprintf(outStr,"Thermocouple set failed (unknown type");
      break;
  }
  #ifdef DEBUG_VERBOSE
  Serial.println(outStr);
  #endif
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
#else
    tftDisplay.drawString("No thermocouple - random test values", textPosition[0], textPosition[1], GFXFF);
    Serial.println("No thermocouple - random test values");
#endif
  textPosition[1] += fontHeight;
  for(int idx = 0; idx < TEMP_BUFFER; idx++)
  {
    tempValues[idx] = -100.0;
  }
  #ifdef HAS_RTC
  while (!rtc.begin()) 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Couldn't find RTC...retry");
    #endif
    tftDisplay.drawString("Couldn't find RTC", textPosition[0], textPosition[1], GFXFF);
    textPosition[1] += fontHeight;
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
    #ifdef DEBUG_VERBOSE
    Serial.println("RTC is NOT initialized, let's set the time!");
    #endif
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
  #ifdef DEBUG_VERBOSE
  else
  {
    Serial.println("RTC initialized, time already set");
  }
  #endif
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
  
  #ifdef DEBUG_VERBOSE
  Serial.println( " initializing LittleFS" );
  #endif
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
      #ifdef DEBUG_VERBOSE
      Serial.println("LittleFS Mount Failed");
      #endif
      tftDisplay.drawString("LittleFS Mount Failed", textPosition[0], textPosition[1], GFXFF);
      textPosition[1] += fontHeight;
      return;
  }
  #ifdef DEBUG_VERBOSE
  Serial.println( "LittleFS initialized" );
  Serial.println("Files on LittleFS");
  listDir(LittleFS, "/", 3);
  #endif

  #ifdef DEBUG_VERBOSE
  Serial.println( "initializing microSD" );
  #endif
  if(!SD.begin(SD_CS))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("microSD card mount failed");
    #endif
    tftDisplay.drawString("microSD card mount failed", textPosition[0], textPosition[1], GFXFF);
    while(true);
  }
  tftDisplay.drawString("microSD initialized", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE)
  {
    return;
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  #ifdef DEBUG_VERBOSE
  Serial.println( "SD initialized" );
  Serial.println("Files on SD");
  listDir(SD, "/", 3);
  #endif
  
  ReadCarSetupFile(SD,  "/py_cars.txt");
  WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
  ReadDeviceSetupFile(SD,  "/py_set.txt");
  WriteDeviceSetupHTML(/*SD*/LittleFS, "/py_set.html");

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
  
    // Web Server Root URL
  Serial.println("starting webserver");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("HTTP_GET, send /py_main.html from LittleFS");
    #endif
    request->send(/*SD*/LittleFS, "/py_main.html", "text/html");
  });
  
  server.serveStatic("/", /*SD*/LittleFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("HTTP_POST");
    #endif
    int params = request->params();
    int pageSource = 0;
    CarSettings tempCar;
    DeviceSettings tempDevice;
    char valueCheck[32];
    bool forceContinue = false;
    for(int i=0; i < params; i++)
    {
      forceContinue = false;
      AsyncWebParameter* p = request->getParam(i);
      
      #ifdef DEBUG_VERBOSE
      Serial.print(i);
      Serial.print(": >");
      Serial.print(p->name());
      Serial.print("< >");
      Serial.print(p->value().c_str());
      Serial.println("<");
      #endif
      // car settings
      if (strcmp(p->name().c_str(), "car_id") == 0)
      {
        strcpy(tempCar.carName, p->value().c_str());
        pageSource = 1;
        continue;
      }
      if (strcmp(p->name().c_str(), "tirecount_id") == 0)
      {
        tempCar.tireCount = atoi(p->value().c_str());
        continue;
      }
      if (strcmp(p->name().c_str(), "measurecount_id") == 0)
      {
        tempCar.positionCount = atoi(p->value().c_str());
        continue;
      }
      for(int tireIdx = 0; tireIdx < 6; tireIdx++)
      {
        sprintf(valueCheck, "tire%d_full_id", tireIdx);
        if (strcmp(p->name().c_str(), valueCheck) == 0)
        {
          strcpy(tempCar.tireLongName[tireIdx], p->value().c_str());
          forceContinue = true;
          break;
        }
        sprintf(valueCheck, "tire%d_short_id", tireIdx);
        if (strcmp(p->name().c_str(), valueCheck) == 0)
        {
          strcpy(tempCar.tireShortName[tireIdx], p->value().c_str());
          forceContinue = true;
          break;
        }
        sprintf(valueCheck, "tire%d_maxt_id", tireIdx);
        if (strcmp(p->name().c_str(), valueCheck) == 0)
        {
          tempCar.maxTemp[tireIdx] = atof(p->value().c_str());
          forceContinue = true;
          break;
        }
      }
      if(forceContinue)
      {
        continue;
      }
      for(int posIdx = 0; posIdx < 3; posIdx++)
      {
        sprintf(valueCheck, "position%d_full_id", posIdx);
        if (strcmp(p->name().c_str(), valueCheck) == 0)
        {
          strcpy(tempCar.positionLongName[posIdx], p->value().c_str());
          forceContinue = true;
          break;
        }
        sprintf(valueCheck, "position%d_short_id", posIdx);
        if (strcmp(p->name().c_str(), valueCheck) == 0)
        {
          strcpy(tempCar.positionShortName[posIdx], p->value().c_str());
          forceContinue = true;
          break;
        }
      }
      if(forceContinue)
      {
        continue;
      }
      // device settings
      if (strcmp(p->name().c_str(), "ssid_id") == 0)
      {
        strcpy(tempDevice.ssid, p->value().c_str());
        pageSource = 2;
        continue;
      }
      if (strcmp(p->name().c_str(), "pass_id") == 0)
      {
        strcpy(tempDevice.pass, p->value().c_str());
        continue;
      }
      // bool tempUnits = false; // true for C, false for F
      if (strcmp(p->name().c_str(), "units_id") == 0)
      {
        tempDevice.tempUnits = false;
        if(strcmp(p->value().c_str(), "C") == 0)
        {
          Serial.println("C");
          tempDevice.tempUnits = true;
        }
        else
        {
          Serial.println("F");
          tempDevice.tempUnits = false;
        }
        continue;
      }
      // int screenRotation = 1; // 1 = R, 0 = L
      if (strcmp(p->name().c_str(), "orientation_id") == 0)
      {
        tempDevice.screenRotation = 1;
        if(strcmp(p->value().c_str(), "L") == 0)
        {
          tempDevice.screenRotation = 0;
        }
        continue;
      }
      // bool is12Hour = true;   // true for 12 hour clock, false for 24 hour clock
      if (strcmp(p->name().c_str(), "clock_id") == 0)
      {
        tempDevice.is12Hour = true;
        int hourMode = atoi(p->value().c_str());
        Serial.println(hourMode);
        if(hourMode == 24)
        {
          Serial.println("24 hour clock");
          tempDevice.is12Hour = false;
        }
        else
        {
          Serial.println("12 hour clock");
          tempDevice.is12Hour = true;
        }
        continue;
      }
      // int fontPoints = 12;    // size of font to use for display
      if (strcmp(p->name().c_str(), "fontsize_id") == 0)
      {
        tempDevice.fontPoints = atoi(p->value().c_str());
        if((tempDevice.fontPoints !=  9) &&
           (tempDevice.fontPoints != 12) &&
           (tempDevice.fontPoints != 18) &&
           (tempDevice.fontPoints != 24))
        {
          tempDevice.fontPoints =  12;
        }
        continue;
      }
      if (pageSource == 1) 
      {
        if(strcmp(p->name().c_str(), "update") == 0)
        {
          // update current car settings
          strcpy(cars[carSetupIdx].carName, tempCar.carName);
          cars[carSetupIdx].tireCount = tempCar.tireCount;
          cars[carSetupIdx].positionCount = tempCar.positionCount;
          for(int tireIdx = 0; tireIdx < 6; tireIdx++)
          {
            strcpy(cars[carSetupIdx].tireLongName[tireIdx], tempCar.tireLongName[tireIdx]);
            strcpy(cars[carSetupIdx].tireShortName[tireIdx], tempCar.tireShortName[tireIdx]);
            cars[carSetupIdx].maxTemp[tireIdx] = tempCar.maxTemp[tireIdx];
          }
          for(int posIdx = 0; posIdx < 3; posIdx++)
          {
            strcpy(cars[carSetupIdx].positionLongName[posIdx], tempCar.positionLongName[posIdx]);
            strcpy(cars[carSetupIdx].positionShortName[posIdx], tempCar.positionShortName[posIdx]);
          }
          WriteCarSetupFile(SD, "/py_cars.txt");
          WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
        }
        else if ((strcmp(p->name().c_str(), "new") == 0))
        {
          // create a blank new car entry
          carCount++;
          carSetupIdx++;
          if (void* mem = realloc(cars, sizeof(CarSettings) * carCount))
          {
            cars = static_cast<CarSettings*>(mem);
          }
          strcpy(cars[carCount - 1].carName, "-");
          cars[carCount - 1].tireCount = 4;
          cars[carCount - 1].positionCount = 3;
          strcpy(cars[carCount - 1].tireShortName[0], "-");
          strcpy(cars[carCount - 1].tireLongName[0], "-");
          cars[carCount - 1].maxTemp[0] = 100.0;
          strcpy(cars[carCount - 1].tireShortName[1], "-");
          strcpy(cars[carCount - 1].tireLongName[1], "-");
          cars[carCount - 1].maxTemp[1] = 100.0;
          strcpy(cars[carCount - 1].tireShortName[2], "-");
          strcpy(cars[carCount - 1].tireLongName[2], "-");
          cars[carCount - 1].maxTemp[2] = 100.0;
          strcpy(cars[carCount - 1].tireShortName[3], "-");
          strcpy(cars[carCount - 1].tireLongName[3], "-");
          cars[carCount - 1].maxTemp[3] = 100.0;

          strcpy(cars[carCount - 1].positionShortName[0], "-");
          strcpy(cars[carCount - 1].positionLongName[0], "-");
          strcpy(cars[carCount - 1].positionShortName[1], "-");
          strcpy(cars[carCount - 1].positionLongName[1], "-");
          strcpy(cars[carCount - 1].positionShortName[2], "-");
          strcpy(cars[carCount - 1].positionLongName[2], "-");
          WriteCarSetupFile(SD, "/py_cars.txt");
          WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
        }
        else if ((strcmp(p->name().c_str(), "delete") == 0))
        {
          // delete current car entry
          for(int carIdx = carSetupIdx; carIdx < carCount - 1; carIdx++)
          {
            cars[carIdx] = cars[carIdx + 1];
          }
          carCount--;
          if (void* mem = realloc(cars, sizeof(CarSettings) * carCount))
          {
            cars = static_cast<CarSettings*>(mem);
          }
          WriteCarSetupFile(SD, "/py_cars.txt");
          WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
        }
        // buttons
        if (strcmp(p->name().c_str(), "next") == 0)
        {
          carSetupIdx = carSetupIdx + 1 < carCount ? carSetupIdx + 1 : carCount - 1;
          WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
          request->send(/*SD*/LittleFS, "/py_cars.html", "text/html");
        }
        if (strcmp(p->name().c_str(), "prior") == 0)
        {
          carSetupIdx = carSetupIdx - 1 >= 0 ? carSetupIdx - 1 : 0;
          WriteCarSetupHTML(/*SD*/LittleFS, "/py_cars.html", carSetupIdx);
          request->send(/*SD*/LittleFS, "/py_cars.html", "text/html");
        }
        request->send(/*SD*/LittleFS, "/py_cars.html", "text/html");
      }
      else if (pageSource == 2)
      {
        if (strcmp(p->name().c_str(), "update") == 0)
        {
          // update device settings
          strcpy(deviceSettings.ssid, tempDevice.ssid);
          strcpy(deviceSettings.pass , tempDevice.pass);
          deviceSettings.tempUnits = tempDevice.tempUnits;
          deviceSettings.screenRotation = tempDevice.screenRotation;
          deviceSettings.is12Hour = tempDevice.is12Hour;
          deviceSettings.fontPoints = tempDevice.fontPoints;
          WriteDeviceSetupFile(SD, "/py_set.txt");
          WriteDeviceSetupHTML(/*SD*/LittleFS, "/py_set.html");
          request->send(/*SD*/LittleFS, "/py_set.html", "text/html");
        }
      }
      else
      {
        request->send(/*SD*/LittleFS, "/py_main.html", "text/html");
      }
    }
  });
  server.begin();
  /* 
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
  */
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
  mainMenuChoices[1].description = cars[selectedCar].carName; mainMenuChoices[1].result = SELECT_CAR;
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
  //ResetTempStable();
  armed = false;
  unsigned long priorTime = millis();
  unsigned long curTime = millis();
  // text position on OLED screen
  // measuring until all positions are measured
  bool drawStars = true;
  while(measIdx < cars[selectedCar].positionCount)
  {
    if(drawStars)
    {
      row = ((tireIdx / 2) * 2) + 1;
      col = measIdx + ((tireIdx % 2) * 3);
      DrawCellText(row, 
                   col, 
                   "****", 
                   TFT_WHITE, 
                   TFT_BLACK);
      drawStars = false;
    }
    // get time and process buttons for press/release
    curTime = millis();
    CheckButtons(curTime);
    // button 0 release arms probe
    // prior to arm, display ****, after display temp as it stabilizes
    if (buttons[0].buttonReleased)
    {
      armed = true;
      buttons[0].buttonReleased = false;
    }
    // check for button 1 or 2 release (tire selection) before measure starts
    else if (((buttons[1].buttonReleased) || (buttons[2].buttonReleased))  && (measIdx == 0))
    {
      // erase stars in first measure position
      DrawCellText(row, 
                   col, 
                   "****", 
                   TFT_BLACK,
                   TFT_BLACK);

      int rVal = buttons[1].buttonReleased == true ? 1 : -1;
      for(int btnIdx = 1; btnIdx < BUTTON_COUNT; btnIdx++)
      {
        buttons[btnIdx].buttonReleased = false;
      }
      return rVal;
    }
    // if not armed continue
    if(!armed)// && (curTime - priorTime) < 250)
    {
      curTime = millis();
      continue;
    }
    // get stable temp after arming
    row = ((tireIdx / 2) * 2) + 1;
    col = measIdx + ((tireIdx % 2) * 3);
    #ifdef HAS_THERMO
    tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = GetStableTemp(row, col);
    #else
    tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = 100;
    #endif
    // disarm after stable temp
    armed = false;
    // next position
    measIdx++;
    drawStars = true;
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
      //instant_temp = tempSensor.readTempC();//tempSensor.getThermocoupleTemp(deviceSettings.tempUnits); // false for F, true or empty for C
      instant_temp = tempSensor.getThermocoupleTemp();
      if(deviceSettings.tempUnits == 0)
      {
        instant_temp = CtoFAbsolute(instant_temp);
      }
      #else
      instant_temp = 100.0F;
      #endif
      sprintf(outStr, "0000.00000");
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

  sprintf(outStr, "%s %s", currentResultCar.carName, currentResultCar.dateTime);
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
        sprintf(outStr, "%s %s", currentResultCar.tireShortName[idxTire],
                                 currentResultCar.positionShortName[tirePosIdx]);
      }
      else
      {
        sprintf(outStr, "%s", currentResultCar.positionShortName[tirePosIdx]);
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
    sprintf(outStr, "%s", cars[selectedCar].carName);
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
            sprintf(outStr, "%s %s", cars[selectedCar].tireShortName[idxTire],
                                   cars[selectedCar].positionShortName[tirePosIdx]);
          }
          else
          {
            sprintf(outStr, "%s", cars[selectedCar].positionShortName[tirePosIdx]);
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
  String curTimeStr;
  curTimeStr = GetStringTime();
  strcpy(cars[selectedCar].dateTime, curTimeStr.c_str());
  curTimeStr += " ";
  curTimeStr += GetStringDate();
  sprintf(outStr, "%s;%s;%d;%d", curTimeStr.c_str(), 
                                 cars[selectedCar].carName,
                                 cars[selectedCar].tireCount, 
                                 cars[selectedCar].positionCount);
  #else
  sprintf(outStr, "%d;%s;%d;%d", millis(), 
                                 cars[selectedCar].carName, 
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
        tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
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
        WriteDeviceSetupFile(SD, "/py_set.txt");
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
float CtoFAbsolute(float tempC)
{
  return (tempC * 1.8) + 32; 
}
//
//
//
float CtoFRelative(float tempC)
{
  return (tempC * 1.8); 
}
//
//
//
void PrintTemp(float temp)
{
  if(temp >= 0.0)
  {
    Serial.print(" ");
    if(temp > -100.0)
    {
      Serial.print(" ");
    }
    if(temp > -10.0)
    {
      Serial.print(" ");
    }

  }
  else
  {
    if(temp < 100.0)
    {
      Serial.print(" ");
    }
    if(temp < 10.0)
    {
      Serial.print(" ");
    }
  }
  Serial.print(temp);
}
//
//
//
float GetStableTemp(int row, int col)
{
  char outStr[512];
  float minMax[2] = {5000.0, -5000.0};
  float devRange[2] = {-0.25, 0.25};
  float averageTemp = 0;
  bool rVal = true;
  float temperature;
  int countTemperature = 0;
  // convert deviation band to F if needed
  if(deviceSettings.tempUnits == 0)
  {
    for(int idx = 0; idx < 2; idx++)
    {
      devRange[idx] = CtoFRelative(devRange[idx]);
    }
  }

  for(int idx = 0; idx < TEMP_BUFFER; idx++)
  {
    tempValues[idx] = -100.0;
  }
  while(true)
  {
    temperature = tempSensor.getThermocoupleTemp();
    if (isnan(temperature)) 
    {
      return temperature;
    }
    // convert to F if required
    if(deviceSettings.tempUnits == 0)
    {
      temperature = CtoFAbsolute(temperature);
    }
    // draw current temp in cell
    sprintf(outStr, "%0.1F", temperature);
    DrawCellText(row, col, outStr, TFT_WHITE, TFT_BLACK);
    // get average temp in circular buffer
    if(countTemperature >= TEMP_BUFFER)
    {
      countTemperature = 0;
    }
    tempValues[countTemperature] = temperature;
    countTemperature++;
    averageTemp = 0.0;
    for(int idx = 0; idx < TEMP_BUFFER; idx++)
    {
      averageTemp += tempValues[idx];
    }
    averageTemp = averageTemp / (float)TEMP_BUFFER;
    // check deviations, exit if within +/- 0.25
    minMax[0] = 5000.0;  
    minMax[1] = -5000.0;
    for(int idx = 0; idx < TEMP_BUFFER; idx++)
    {
      minMax[0] = (averageTemp - tempValues[idx]) < minMax[0] ? (averageTemp - tempValues[idx]) : minMax[0];
      minMax[1] = (averageTemp - tempValues[idx]) > minMax[1] ? (averageTemp - tempValues[idx]) : minMax[1];
    }
    if(((minMax[1] - minMax[0]) >= devRange[0]) &&
       ((minMax[1] - minMax[0]) <=  devRange[1]))
    {
      break;
    }
    delay(500);
  }
  return averageTemp;
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
  cars = (CarSettings*)calloc(carCount, sizeof(CarSettings));
  int maxTires = 0;
  int maxPositions = 0;
  for(int carIdx = 0; carIdx < carCount; carIdx++)
  {
    // read name
    ReadLine(file, buf);
    strcpy(cars[carIdx].carName, buf);
    // read tire count and create arrays
    ReadLine(file, buf);
    cars[carIdx].tireCount = atoi(buf);
    maxTires = maxTires > cars[carIdx].tireCount ? maxTires : cars[carIdx].tireCount;
    //cars[carIdx].maxTemp = (float*)calloc(maxTires, sizeof(float));
    //cars[carIdx].tireShortName = new String[cars[carIdx].tireCount];
    //cars[carIdx].tireLongName = new String[cars[carIdx].tireCount];
    // read tire short and long names
    for(int tireIdx = 0; tireIdx < cars[carIdx].tireCount; tireIdx++)
    {
      ReadLine(file, buf);
      strcpy(cars[carIdx].tireShortName[tireIdx], buf);
      ReadLine(file, buf);
      strcpy(cars[carIdx].tireLongName[tireIdx], buf);
      ReadLine(file, buf);
      cars[carIdx].maxTemp[tireIdx] = atof(buf);
    }
    // read measurement count and create arrays
    ReadLine(file, buf);
    cars[carIdx].positionCount = atoi(buf);
    //cars[carIdx].positionShortName = new String[cars[carIdx].positionCount];
    //cars[carIdx].positionLongName = new String[cars[carIdx].positionCount];
    maxPositions = maxPositions > cars[carIdx].positionCount ? maxPositions : cars[carIdx].positionCount;
    // read tire short and long names
    for(int positionIdx = 0; positionIdx < cars[carIdx].positionCount; positionIdx++)
    {
      ReadLine(file, buf);
      strcpy(cars[carIdx].positionShortName[positionIdx], buf);
      ReadLine(file, buf);
      strcpy(cars[carIdx].positionLongName[positionIdx], buf);
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
  char buf[512];
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    return;
  }
  sprintf(buf, "%d", carCount);
  file.println(buf);            // number of cars
  for(int carIdx = 0; carIdx < carCount; carIdx++)
  {
    file.println(cars[carIdx].carName);    // car
    sprintf(buf, "%d", cars[carIdx].tireCount);
    file.println(buf);
    for(int wheelIdx = 0; wheelIdx < cars[carIdx].tireCount; wheelIdx++)
    {
      sprintf(buf, "%s", cars[carIdx].tireShortName[wheelIdx]);
      file.println(buf);
      sprintf(buf, "%s", cars[carIdx].tireLongName[wheelIdx]);
      file.println(buf);
      sprintf(buf, "%0.2lf", cars[carIdx].maxTemp[wheelIdx]);
      file.println(buf);
    }
    sprintf(buf, "%d", cars[carIdx].positionCount);
    file.println(buf);
    for(int posIdx = 0; posIdx < cars[carIdx].positionCount; posIdx++)
    {
      sprintf(buf, "%s", cars[carIdx].positionShortName[posIdx]);
      file.println(buf);
      sprintf(buf, "%s", cars[carIdx].positionLongName[posIdx]);
      file.println(buf);
    }
    file.println("=========="); 
  }
  file.close();
  
  //#ifdef DEBUG_VERBOSE
  Serial.println("Done writing, readback");
  file = fs.open(path, FILE_READ);
  Serial.println(path);
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf)==0)
    {
      break;
    }
    Serial.println(buf);
  }
  Serial.println("Done");
  file.close();
  //#endif
}
//
//
//
void WriteCarSetupHTML(fs::FS &fs, const char * path, int carIdx)
{
  DeleteFile(fs, path);
  char buf[512];
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    return;
  }
  file.println("<html>");
  file.println("<head>");
  file.println("<meta charset=\"UTF-8\">");
  file.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
  file.println("<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" />");
  file.println("<meta http-equiv=\"Pragma\" content=\"no-cache\" /><meta http-equiv=\"Expires\" content=\"0\"/>");
  file.println("<title>Yamura Tire Pyrometer</title>");
  file.println("<link rel=\"icon\" href=\"data:,\">");
  file.println("<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">");
  file.println("</head>");
  file.println("<body>");
  file.println("<div class=\"content\">");
  file.println("<div class=\"card-grid\">");
  file.println("<div class=\"card\">");
  file.println("<form action=\"/\" method=\"POST\">");
  file.println("<p>");
  
  file.println("<div>");
  file.println("<h3>Car and Driver Info</h3>");
  file.println("</div>");

  file.println("<div class=\"dInput\" v-if=\"activeStage == 3\">");
  file.println("<div><label for=\"car_id\">Car/Driver</label>");
  sprintf(buf, "<input type=\"text\" id =\"car_id\" name=\"car_id\" value = \"%s\"></div>", cars[carIdx].carName);
  file.println(buf);
  file.println("</div>");

  sprintf(buf, "<div><label for=\"tirecount_id\">Tires (%d)</label>", cars[carIdx].tireCount);
  file.println(buf);
  file.println("<div><select id =\"tirecount_id\" name=\"tirecount_id\"><br>");
  sprintf(buf, "<option>%d</option>", cars[carIdx].tireCount);
  file.println(buf);
  file.println("<option>2</option>");  
  file.println("<option>3</option>");
  file.println("<option>4</option>");
  file.println("<option>6</option>");
  file.println("</select>");

  sprintf(buf, "<div><label for=\"measurecount_id\">Measurements (%d)</label>", cars[carIdx].positionCount);
  file.println(buf);
  file.println("<div><select id =\"measurecount_id\" name=\"measurecount_id\"><br>");
  sprintf(buf, "<option>%d</option>", cars[carIdx].positionCount);
  file.println(buf);
  file.println("<option>1</option>");  
  file.println("<option>2</option>");
  file.println("<option>3</option>");
  file.println("</select>");
  
  file.println("</div>");
  file.println("</p>");
  file.println("<p>");
  file.println("<div>");
  file.println("<h3>Tire Info</h3>");
  file.println("</div>");
  for(int tireIdx = 0; tireIdx < 6; tireIdx++)
  {
    sprintf(buf, "<div class=\"dInput\" v-if=\"activeStage == 3\">");
    file.println(buf);
    if(tireIdx == 0)
    {
      sprintf(buf, "<div><label for=\"tire%d_full_id\">Full</label>", tireIdx);
    }
    else
    {
      sprintf(buf, "<div>");//<label for=\"tire%d_full_id\"></label>", tireIdx);
    }
    file.println(buf);
    if(tireIdx < cars[carIdx].tireCount)
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_full_id\" name=\"tire%d_full_id\" value = \"%s\"></div>", tireIdx, tireIdx, cars[carIdx].tireLongName[tireIdx]);
    }
    else
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_full_id\" name=\"tire%d_full_id\" value = \"%s\"></div>", tireIdx, tireIdx, "-");
    }
    file.println(buf);
    if(tireIdx == 0)
    {
      sprintf(buf, "<div><label for=\"tire%d_short_id\">Short</label>", tireIdx);
    }
    else
    {
      sprintf(buf, "<div><label for=\"tire%d_short_id\"></label>", tireIdx);
    }

    file.println(buf);
    if(tireIdx < cars[carIdx].tireCount)
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_short_id\" name=\"tire%d_short_id\" value = \"%s\"></div>",  tireIdx, tireIdx, cars[carIdx].tireShortName[tireIdx]);
    }
    else
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_short_id\" name=\"tire%d_short_id\" value = \"%s\"></div>",  tireIdx, tireIdx, "-");
    }
    file.println(buf);
    if(tireIdx == 0)
    {
      sprintf(buf, "<div><label for=\"tire%d_maxt_id\">Max T</label>", tireIdx);
    }
    else
    {
      sprintf(buf, "<div><label for=\"tire%d_maxt_id\"></label>", tireIdx);
    }
    file.println(buf);
    if(tireIdx < cars[carIdx].tireCount)
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_maxt_id\" name=\"tire%d_maxt_id\" value = \"%0.1lf\"></div>", tireIdx, tireIdx, cars[carIdx].maxTemp[tireIdx]);
    }
    else
    {
      sprintf(buf, "<input type=\"text\" id =\"tire%d_maxt_id\" name=\"tire%d_maxt_id\" value = \"%s\"></div>", tireIdx, tireIdx, "-");
    }
    file.println(buf);
    sprintf(buf, "</div>");
    file.println(buf);
  }
  file.println("</p>");
  file.println("<p>");
  file.println("<div>");
  file.println("<h3>Measure Points</h3>");
  file.println("</div>");
  for(int measIdx = 0; measIdx < 3; measIdx++)
  {
    file.println("<div class=\"dInput\" v-if=\"activeStage == 3\">");
    if(measIdx == 0)
    {
      sprintf(buf, "<div><label for=\"position%d_full_id\">Full</label>", measIdx);
    }
    else
    {
      sprintf(buf, "<div>");
    }
    file.println(buf);
    if(measIdx < cars[carIdx].positionCount)
    {
      sprintf(buf, "<input type=\"text\" id =\"position%d_full_id\" name=\"position%d_full_id\" value = \"%s\"></div>", measIdx, measIdx, cars[carIdx].positionLongName[measIdx]);
    }
    else
    {
      sprintf(buf, "<input type=\"text\" id =\"position%d_full_id\" name=\"position%d_full_id\" value = \"%s\"></div>", measIdx, measIdx, "-");
    }
    file.println(buf);
    if(measIdx == 0)
    {
      sprintf(buf, "<div><label for=\"position%d_short_id\">Short</label>", measIdx);
    }
    else
    {
      sprintf(buf, "<div>");
    }

    file.println(buf);
    if(measIdx < cars[carIdx].positionCount)
    {
      sprintf(buf, "<input type=\"text\" id =\"position%d_short_id\" name=\"position%d_short_id\" value = \"%s\"></div>", measIdx, measIdx, cars[carIdx].positionShortName[measIdx]);
    }
    else
    {
      sprintf(buf, "<input type=\"text\" id =\"position%d_short_id\" name=\"position%d_short_id\" value = \"%s\"></div>", measIdx, measIdx, "-");
    }
    file.println(buf);
    file.println("</div>");
  }
  file.println("<\table>");
  file.println("<p>");
  file.println("<div>");
  file.println("<h3>Actions</h3>");
  file.println("</div>");
  file.println("<button name=\"update\" type =\"submit\" value =\"update\">Update</button>");
  file.println("<button name=\"prior\"  type =\"submit\" value =\"prior\">Prior</button>");
  file.println("<button name=\"next\"   type =\"submit\" value =\"next\">Next</button>");
  file.println("<button name=\"delete\" type =\"submit\" value =\"delete\">Delete</button>");
  file.println("<button name=\"new\"    type =\"submit\" value =\"new\">New</button>");
  file.println("<button name=\"home\" type=\"submit\" value=\"home\"><a href=\"/py_main.html\">Home</a></button>");
  file.println("</p>");
  file.println("</form>");
  file.println("</div>");
  file.println("</div>");
  file.println("</div>");
  file.println("</body>");
  file.println("</html>");
  file.close();
  delay(100);
  #ifdef DEBUG_VERBOSE
  file = fs.open(path, FILE_READ);
  Serial.println(path);
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf)==0)
    {
      break;
    }
    Serial.println(buf);
  }
  Serial.println("Done");
  file.close();
  #endif
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
  char buf[512];
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
  file.close();
  #ifdef DEBUG_VERBOSE
  Serial.println("Done writing, readback");
  file = fs.open(path, FILE_READ);
  Serial.println(path);
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf)==0)
    {
      break;
    }
    Serial.println(buf);
  }
  Serial.println("Done");
  file.close();
  #endif
}
//
//
//
void WriteDeviceSetupHTML(fs::FS &fs, const char * path)
{
  int selectedIndex = 0;
  char buf[512];
  DeleteFile(fs, path);
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    return;
  }
  file.println("<!DOCTYPE html>");
  file.println("<html>");
  file.println("<head>");
  file.println("<meta charset=\"UTF-8\">");
  file.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  file.println("<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" />");
  file.println("<meta http-equiv=\"Pragma\" content=\"no-cache\" /><meta http-equiv=\"Expires\" content=\"0\"/>");
  file.println("<title>Yamura Pyrometer Setup</title>");
  file.println("<link rel=\"icon\" href=\"data:,\">");
  file.println("<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">");
  file.println("</head>");

  file.println("<body>");
  file.println("<div class=\"content\">");
  file.println("<div class=\"card-grid\">");
  file.println("<div class=\"card\">");
  file.println("<form action=\"/\" method=\"POST\">");

  file.println("<p>");
  file.println("<div>");
  file.println("<h3>Pyrometer Settings</h3>");
  file.println("</div>");
  file.println("</p>");

  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  file.println("<label for=\"ssid_id\">SSID</label>");
  sprintf(buf, "<div><input type=\"text\" id =\"ssid_id\" name=\"ssid_id\" value = \"%s\"><br>", deviceSettings.ssid);
  file.println(buf);
  file.println("</div>");
  file.println("</p>");
  
  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  file.println("<label for=\"pass_id\">Password</label>");
  sprintf(buf, "<div><input type=\"text\" id =\"pass_id\" name=\"pass_id\" value = \"%s\"><br>", deviceSettings.pass);
  file.println(buf);
  file.println("</div>");
  file.println("</p>");
  
  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  selectedIndex = deviceSettings.tempUnits == true ? 0 : 1;
  sprintf(buf, "<label for=\"units_id\">Temperature Units (%s)</label>", (deviceSettings.tempUnits == true ? "C" : "F"));
  file.println(buf);
  file.println("<div><select id =\"units_id\" name=\"units_id\"><br>");
  sprintf(buf, "<option>%s</option>", (deviceSettings.tempUnits == true ? "C" : "F"));
  file.println(buf);
  file.println("<option>F</option>");  
  file.println("<option>C</option>");
  file.println("</select>");
  file.println("</div>");
  file.println("</p>");

  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  sprintf(buf, "<label for=\"orientation_id\">Screen Orientation (%s)</label>", (deviceSettings.screenRotation == 1 ? "R" : "L"));
  file.println(buf);
  file.println("<div><select id =\"orientation_id\" name=\"orientation_id\"><br>");
  sprintf(buf, "<option>%s</option>", (deviceSettings.screenRotation == 1 ? "R" : "L"));
  file.println(buf);
  file.println("<option>R</option>");  
  file.println("<option>L</option>");
  file.println("</select>");
  file.println("</div>");
  file.println("</p>");

  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  sprintf(buf, "<label for=\"clock_id\">Clock (%d)</label>", (deviceSettings.is12Hour == true ? 12 : 24));
  file.println(buf);
  file.println("<div><select id =\"clock_id\" name=\"clock_id\"><br>");
  sprintf(buf, "<option>%d</option>", (deviceSettings.is12Hour == true ? 12 : 24));
  file.println(buf);
  file.println("<option>12</option>");  
  file.println("<option>24</option>");
  file.println("</select>");
  file.println("</div>");
  file.println("</p>");

  file.println("<p>");
  file.println("<div class=\"dinput\" v-if=\"activeStage == 3\">");
  sprintf(buf, "<label for=\"fontsize_id\">Font Size (%d)</label>", deviceSettings.fontPoints);
  file.println(buf);
  file.println("<div><select id =\"fontsize_id\" name=\"fontsize_id\"><br>");
  sprintf(buf, "<option>%d</option>", deviceSettings.fontPoints);
  file.println(buf);
  file.println("<option>9</option>");
  file.println("<option>12</option>");  
  file.println("<option>18</option>");
  file.println("<option>24</option>");
  file.println("</select>");

  file.println("</div>");
  file.println("</p>");
  
  file.println("<p>");
  file.println("<div>");
  file.println("<h3>Actions</h3>");
  file.println("</div>");  
  file.println("<button name=\"update\" type =\"submit\" value =\"update\">Update</button>");
  file.println("<button><a href=\"py_main.html\">Home</a></button>");
  file.println("</p>");
  file.println("</form>");
  file.println("</div>");
  file.println("</div>");
  file.println("</div>");
  file.println("</div>");
  file.println("</body>");
  file.println("</html>");
  file.close();

  #ifdef DEBUG_VERBOSE
  Serial.println("Done writing, readback");
  file = fs.open(path, FILE_READ);
  Serial.println(path);
  while(true)
  {
    ReadLine(file, buf);
    if(strlen(buf)==0)
    {
      break;
    }
    Serial.println(buf);
  }
  Serial.println("Done");
  file.close();
  #endif
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
    tftDisplay.drawString(cars[selectedCar].carName, 5, fontHeight, GFXFF);
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
    tftDisplay.drawString(cars[selectedCar].carName, 5, fontHeight, GFXFF);
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
  //free(currentResultCar.maxTemp);
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
  //htmlStr = "";
  CarSettings currentResultCar;
  File fileIn;
  // create a new HTML file
  //Serial.println("py_res.html header");
  LittleFS.remove("/py_res.html");
  AppendFile(LittleFS, "/py_res.html", "<!DOCTYPE html>");
  AppendFile(LittleFS, "/py_res.html", "<html>");
  AppendFile(LittleFS, "/py_res.html", "<head>");
  AppendFile(LittleFS, "/py_res.html", "<title>Recording Pyrometer</title>");
  AppendFile(LittleFS, "/py_res.html", "</head>");
  AppendFile(LittleFS, "/py_res.html", "<body>");
  AppendFile(LittleFS, "/py_res.html", "<h1>Recorded Results</h1>");
  AppendFile(LittleFS, "/py_res.html", "<p>");
  AppendFile(LittleFS, "/py_res.html", "<table border=\"1\">");
  AppendFile(LittleFS, "/py_res.html", "<tr>");
  AppendFile(LittleFS, "/py_res.html", "<th>Date/Time</th>");
  AppendFile(LittleFS, "/py_res.html", "<th>Car/Driver</th>");
  AppendFile(LittleFS, "/py_res.html", "</tr>");
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
    //Serial.print("Reading data file ");
    //Serial.println(nameBuf);
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
      //Serial.println("output measurements to py_res.html");
      if(outputSubHeader)
      {
        AppendFile(LittleFS, "/py_res.html", "<tr>");
        AppendFile(LittleFS, "/py_res.html", "<td></td>");
        AppendFile(LittleFS, "/py_res.html", "<td></td>");
        for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
        {
          for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
          {
            sprintf(buf, "<td>%s-%s</td>", currentResultCar.tireShortName[t_idx], currentResultCar.positionShortName[p_idx]);
            AppendFile(LittleFS, "/py_res.html", buf);
          }
        }
        AppendFile(LittleFS, "/py_res.html", "</tr>");
      }
      outputSubHeader = false;
      rowCount++;
      AppendFile(LittleFS, "/py_res.html", "		    <tr>");
      sprintf(buf, "<td>%s</td>", currentResultCar.dateTime);
      AppendFile(LittleFS, "/py_res.html", buf);
      sprintf(buf, "<td>%s</td>", currentResultCar.carName);
      AppendFile(LittleFS, "/py_res.html", buf);
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
          AppendFile(LittleFS, "/py_res.html", buf);
        }
      }
      AppendFile(LittleFS, "/py_res.html", "</tr>");
	    //free(currentResultCar.maxTemp);
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
    AppendFile(SD, "/py_res.html", "</tr>");
  }
  fileIn.close();

  AppendFile(LittleFS, "/py_res.html", "</table>");
  AppendFile(LittleFS, "/py_res.html", "</p>");
  AppendFile(LittleFS, "/py_res.html", "<p>");
  AppendFile(LittleFS, "/py_res.html", "<button name=\"home\" type=\"submit\" value=\"home\"><a href=\"/py_main.html\">Home</a></button>");
  AppendFile(LittleFS, "/py_res.html", "</p>");
  AppendFile(LittleFS, "/py_res.html", "</body>");
  AppendFile(LittleFS, "/py_res.html", "</html>");


  #ifdef DEBUG_VERBOSE
  Serial.println("Done writing, readback");
  fileIn = LittleFS.open("/py_res.html", FILE_READ);
  Serial.println("/py_res.html");
  while(true)
  {
    ReadLine(fileIn, buf);
    if(strlen(buf)==0)
    {
      break;
    }
    Serial.println(buf);
  }
  Serial.println("Done");
  fileIn.close();
  #endif

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
      strcpy(currentResultCar.dateTime, token);
    }
    // 1 - car info
    if(tokenIdx == 1)
    {
      strcpy(currentResultCar.carName, token);
    }
    // 2 - tire count
    else if(tokenIdx == 2)
    {
      currentResultCar.tireCount = atoi(token);
      //currentResultCar.tireShortName = new String[currentResultCar.tireCount];
      //currentResultCar.tireLongName = new String[currentResultCar.tireCount];
      //currentResultCar.maxTemp = (float*)calloc(currentResultCar.tireCount, sizeof(float));
    }
    // 3 - position count
    else if(tokenIdx == 3)
    {
      currentResultCar.positionCount = atoi(token);
      //currentResultCar.positionShortName = new String[currentResultCar.positionCount];
      //currentResultCar.positionLongName = new String[currentResultCar.positionCount];
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
      strcpy(currentResultCar.tireShortName[tireIdx], token);
      strcpy(currentResultCar.tireLongName[tireIdx], token);
      tireIdx++;
    }
    // position names
    else if((tokenIdx >= posNameRange[0]) && (tokenIdx <= posNameRange[1]))
    {
      strcpy(currentResultCar.positionShortName[positionIdx], token);
      strcpy(currentResultCar.positionLongName[positionIdx], token);
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
bool IsPM()
{
  DateTime now;
  now = rtc.now();
  return now.isPM();
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
  }
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
  bool isPM = now.isPM();

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
    SetFont(24);
    tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  
    tftDisplay.drawString("Set date/time", textPosition[0], textPosition[1], GFXFF);
    
    textPosition[1] += fontHeight;

    sprintf(outStr, "%02d", timeVals[MONTH]);
    if(setIdx == 0)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, "/");
    tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, "%02d", timeVals[DATE]);
    if(setIdx == 1)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, "/");
    tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, "%02d", timeVals[YEAR]);
    if(setIdx == 2)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, " %s", days[timeVals[DAYOFWEEK]]);
    if(setIdx == 3)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);

    textPosition[0] = 5;
    textPosition[1] += fontHeight;

    sprintf(outStr, "%02d", timeVals[HOUR]);
    if(setIdx == 4)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, ":");
    tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    sprintf(outStr, "%02d", timeVals[MINUTE]);
    if(setIdx == 5)
    {
      tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
    }
    else
    {
      tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    textPosition[0] += tftDisplay.textWidth(outStr);

    if(deviceSettings.is12Hour)
    {
      sprintf(outStr, "%s", (isPM ? " PM" : " AM"));
      if(setIdx == 6)
      {
        tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
      }
      else
      {
        tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
      }
      tftDisplay.drawString(outStr,textPosition[0], textPosition[1], GFXFF);
    }

    textPosition[0] = 5;
    textPosition[1] += fontHeight;

    if((deviceSettings.is12Hour &&  (setIdx >= 7)) ||
       (!deviceSettings.is12Hour && (setIdx >= 6)))
    {
      break;
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
            if(timeVals[HOUR] <= 0) // going back from 1 to 12
            {
              timeVals[HOUR] = 12;
            }
            if(timeVals[HOUR] > 12) // going forward from 12 to 1
            {
              timeVals[HOUR] = 1;
            }
          }
          else
          {
            if(timeVals[HOUR] < 0)  // going back from 0 to 23 (12am to 11 pm)
            {
              timeVals[HOUR] = 23;
            }
            if(timeVals[HOUR] > 23) // going forward from 11pm to 12am
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
  }
  if((deviceSettings.is12Hour) && isPM && (timeVals[HOUR] < 12)) // convert to 24 hour clock for RTC module 
  {
    timeVals[HOUR] += 12;
  }

  rtc.adjust(DateTime(timeVals[YEAR], timeVals[MONTH], timeVals[DATE], timeVals[HOUR],timeVals[MINUTE],timeVals[SECOND]));
  textPosition[1] += fontHeight * 2;
  sprintf(outStr,"Set to %s %s", GetStringTime(), GetStringDate());
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  SetFont(deviceSettings.fontPoints);
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
//
//
//
void listDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
    root.close();
    file.close();
}