/*
  YamuraLog Recording Tire Pyrometer
  By: Brian Smith
  Yamura Electronics Division
  Date: May 2024
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware License).

*/
// select 1 RTC library. Modify RTC_ functions as needed
#define HAS_RTC
//#define RTC_8563   // using 8563 RTC
#define RTC_3231 // using 3231 RTC
// select 1 Thermocouple amp library. Modify Thermo_ functions as needed
//#define THERMO_MCP9600 // using MPC9600 thermocouple amp
#define THERMO_MCP9601 // using MPC9601 thermocouple amp
//
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
// install correct thermocouple library
#ifdef THERMO_MCP9600
//#include <SparkFun_MCP9600.h>    // MPC9600 Thermocouple library https://github.com/sparkfun/SparkFun_MCP9600_Arduino_Library
#include <Adafruit_MCP9600.h> 
#endif
#ifdef THERMO_MCP9601
#include <Adafruit_MCP9601.h> 
#endif
// install correct RTC library
#include "RTClib.h"              // PCF8563 RTC library https://github.com/adafruit/RTClib

// uncomment for debug to serial monitor (use #ifdef...#endif around debug output prints)
#define DEBUG_VERBOSE
//#define DEBUG_EXTRA_VERBOSE
//#define DEBUG_HTML
//#define SET_TO_SYSTEM_TIME
// microSD chip reader select
#define SD_CS 5
// I2C pins
#define I2C_SDA 21
#define I2C_SCL 22

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
#define TEMP_BUFFER 15

// car info structure
struct CarSettings
{
  int carID;
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
// button structure list
UserButton buttons[BUTTON_COUNT];
// car list from setup file
CarSettings* cars;
// device settings from file
DeviceSettings deviceSettings;
// tire temp array - max of 6 tires, 3 readings per tire
float tireTemps[18];
float currentTemps[18];

// devices
// thermocouple amplifier
#ifdef THERMO_MCP9600
Adafruit_MCP9600 tempSensor;
#define I2C_ADDRESS_THERMO 0x67
#endif
#ifdef THERMO_MCP9601
Adafruit_MCP9601 tempSensor;
#define I2C_ADDRESS_THERMO 0x67
#endif
// temp values for stabilization calculation
float tempValues[100];

#ifdef RTC_8563
RTC_PCF8563 rtc;
#endif
#ifdef RTC_3231
RTC_DS3231 rtc;
#endif

bool century = false;
bool h12Flag;
bool pmFlag;
String days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri","Sat"};
String ampmStr[3] = {"am", "pm", "\0"};

// TFT display
TFT_eSPI tftDisplay = TFT_eSPI();
int fontHeight;
int textPosition[2] = {5, 0};

int carCount = 0;
int maxCarID = 0;
int selectedCar = 0;
int carSetupIdx = 0;
int tireIdx = 0;
int measIdx = 0;
float tempRes = 1.0;
// initial state of pyrometer (show main menu)
int deviceState = 0;

IPAddress IP;
AsyncWebServer server(80);
//
// grid lines for temp measure/display
//
int gridLineH[4][2][2];   //  4 vertical lines, 2 points per line, 2 values per point (X and Y)
int gridLineV[3][2][2];   //  3 vertical lines, 2 points per line, 2 values per point (X and Y)
int cellPoint[7][6][2];   //  6 max rows, 6 points per cell, 2 values per point (X and Y)

// FUNCTION PROTOTYPES
// required
void setup();
void loop();
// user interface
// menu generators call MenuSelect with the list of selections and returned state
void MainMenu();
void SelectCarMenu();
void SelectedResultsMenu(fs::FS &fs, const char * path);
void ChangeSettingsMenu();
void Select12or24Menu();
void SelectFontSizeMenu();
void SetUnitsMenu();
void DeleteDataFilesMenu(bool verify = true);
int MenuSelect(int fontSize, MenuChoice choices[], int menuCount, int initialSelect);
// set date/time values
void SetDateTime();
// read, write, generate HTML for setup files and results
void ReadCarSetupFile(fs::FS &fs, const char * path);
void WriteCarSetupFile(fs::FS &fs, const char * path);
void WriteCarSetupHTML(fs::FS &fs, const char * path, int carIdx);
void ReadDeviceSetupFile(fs::FS &fs, const char * path);
void WriteDeviceSetupFile(fs::FS &fs, const char * path);
void WriteDeviceSetupHTML(fs::FS &fs, const char * path);
void WriteResultsHTML(fs::FS &fs);
void ReadMeasurementFile(char buf[], CarSettings &currentResultCar);
void WriteMeasurementFile();

// measure and display tire temps, TFT specific functions
// single tire version of tire temp measure
void MeasureAllTireTemps();
int MeasureTireTemps(int tire); // measure single tire temps full screen
float GetStableTemp(int row, int col);
int GetNextTire(int selTire, int nextDirection);
// current probe temp
void InstantTemp();
// display results
void DrawTireMeasureGrid(int tireCount);
void SetupTireMeasureGrid(int fontHeight);
void DisplayAllTireTemps(CarSettings currentResultCar);
void DrawCellText(int row, int col, char* text, uint16_t textColor, uint16_t backColor);
// rotate display (right/left hand orientation)
void RotateDisplay(bool rotateButtons);
// set font size
void SetFont(int fontSize);
// banner
void YamuraBanner();

// 'common' functions defined in this file
// temperature functions
float CtoFAbsolute(float tempC);
float CtoFRelative(float tempC);
float FtoCAbsolute(float tempF);
float FtoCRelative(float tempF);
// date, time, RTC
bool RTC_IsPM();
String RTC_GetStringTime();
String RTC_GetStringDate();
DateTime RTC_DateTime();
void RTC_SetDateTime(DateTime timeVal);
void RTC_SetDateTime(int year, int month, int date, int hour, int minute, int second);
void Thermo_Setup();
float Thermo_GetTemp();
// user input (button presses)
void CheckButtons(unsigned long curTime);
// SD and LittleFS file handling
void ReadLine(File file, char* buf);
void AppendFile(fs::FS &fs, const char * path, const char * message);
void DeleteFile(fs::FS &fs, const char * path);
void ListDirectory(fs::FS &fs, const char * dirname, uint8_t levels);
