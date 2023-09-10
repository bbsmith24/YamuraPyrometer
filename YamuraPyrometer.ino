/*
  YamuraLog Recording Tire Pyrometer
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
  GeeekPi OLED Display Module I2C 128X64 Pixel 0.96 Inch Display Module  (I2C address 0x36, 0x3C) 
  1200mAh LiON battery
  microSD card (8GB is fine, not huge amount of data being stored)
  misc headers
  3D printed box

  records to microSD card
  setup file on microSD card
  wifi interface for display, up/down load (to add)
*/

#include <SparkFun_MCP9600.h>
#include <SparkFun_Qwiic_OLED.h> //http://librarymanager/All#SparkFun_Qwiic_Graphic_OLED
#include "InputDebounce.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <SparkFun_RV1805.h>

// OLED Fonts
#include <res/qw_fnt_5x7.h>
#include <res/qw_fnt_8x16.h>
#include <res/qw_fnt_31x48.h>
#include <res/qw_fnt_7segment.h>
#include <res/qw_fnt_largenum.h>

// uncomment for debug to serial monitor
//#define DEBUG_VERBOSE
// uncomment for RTC module attached
#define HAS_RTC

#define TEMPSTABLE_LED 4
#define BUTTON_1 12
#define BUTTON_2 27
#define BUTTON_3 33
#define BUTTON_4 15
#define BUTTON_DEBOUNCE_DELAY   20   // [ms]
#define BUTTON_COUNT 4

#define DISPLAY_MENU 0
#define SELECT_CAR 1
#define MEASURE_TIRES 2
#define DISPLAY_TIRES 3
#define DISPLAY_SELECTED_RESULT 4
#define SET_DATETIME 5
#define CHANGE_SETTINGS 6
#define DELETE_DATAFILE 7

static InputDebounce buttonArray[BUTTON_COUNT];
int buttonPin[BUTTON_COUNT] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4};
int buttonReleased[BUTTON_COUNT] = {0, 0, 0, 0};
unsigned long pressDuration[BUTTON_COUNT] = {0, 0, 0, 0};

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
};
CarSettings* cars;
// dynamic tire temp array
float tireTemps[60]; // largest array 12 tires, 5 measurements
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
RV1805 rtc;
// OLED display
QwiicTransparentOLED oledDisplay;
// An array of available fonts
QwiicFont *demoFonts[] = {
    &QW_FONT_5X7,
    &QW_FONT_8X16,
    &QW_FONT_31X48,
    &QW_FONT_LARGENUM,
    &QW_FONT_7SEGMENT};
int nFONTS = sizeof(demoFonts) / sizeof(demoFonts[0]);
#define FONT_5X7      1
#define FONT_8X16     1
#define FONT_31X48    2
#define FONT_5X7      3
#define FONT_7SEGMENT 4

int iFont = 0;

unsigned long prior = 0;

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

// function prototypes
void WriteResultsHTML();
void ParseResult(char buf[], CarSettings &currentResultCar);
void button_pressedCallback(uint8_t pinIn);
void button_releasedCallback(uint8_t pinIn);
void button_pressedDurationCallback(uint8_t pinIn, unsigned long duration);
void button_releasedDurationCallback(uint8_t pinIn, unsigned long duration);
void DeleteFile(fs::FS &fs, const char * path);
void AppendFile(fs::FS &fs, const char * path, const char * message);
void ReadLine(File file, char* buf);
void ParseResult(char buf[], CarSettings &currentResultCar);
void DisplaySelectedResults(fs::FS &fs, const char * path);
void WriteSetupFile(fs::FS &fs, const char * path);
void ReadSetupFile(fs::FS &fs, const char * path);
void ResetTempStable();
bool CheckTempStable(float curTemp);
void DeleteDataFile();
void SetDateTime();
void SetUnits();
void ChangeSettings();
void SelectCar();
int MenuSelect(MenuChoice choices[], int menuCount, int linesToDisplay, int initialSelect, int maxWidth);
void DisplayTireTemps(CarSettings currentResultCar);
void MeasureTireTemps();
void DisplayMenu();
//
//
//
void setup()
{
  Serial.begin(115200);
  #ifdef DEBUG_VERBOSE
  Serial.println();
  Serial.println();
  Serial.println("YamuraLog Recording Tire Pyrometer V1.0");
  #endif

  pinMode(TEMPSTABLE_LED, OUTPUT);

  // register callback functions (shared, used by all buttons)
  // setup input buttons (debounced)
  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    buttonArray[buttonIdx].registerCallbacks(button_pressedCallback, 
                                             button_releasedCallback, 
                                             button_pressedDurationCallback, 
                                             button_releasedDurationCallback);
    buttonArray[buttonIdx].setup(buttonPin[buttonIdx], 
                                 BUTTON_DEBOUNCE_DELAY, 
                                 InputDebounce::PIM_INT_PULL_UP_RES);
  }


  Wire.begin();             // start I2C
  // Initalize the OLED device and related graphics system
  while (!oledDisplay.begin(Wire, 0x3C))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("OLED begin failed. Freezing...");
    #endif
    digitalWrite(TEMPSTABLE_LED, HIGH);
    delay(200);
    digitalWrite(TEMPSTABLE_LED, LOW);
    delay(200);
  }

  iFont = 0;
  oledDisplay.setFont(demoFonts[iFont]);  
  oledDisplay.erase();
  oledDisplay.text(5, 0, "Yamura Electronics");
  oledDisplay.text(5, oledDisplay.getStringHeight("X"), "Recording Pyrometer");
  oledDisplay.display();
  delay(10000);
  #ifdef DEBUG_VERBOSE
  Serial.print("OLED screen size ");
  Serial.print(oledDisplay.getWidth());
  Serial.print(" x ");
  Serial.print(oledDisplay.getHeight());
  Serial.println("");
  #endif

  tempSensor.begin();       // Uses the default address (0x60) for SparkFun Thermocouple Amplifier
  //check if the sensor is connected
  if(tempSensor.isConnected())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple Device acknowledged!");
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple did not acknowledge! Freezing.");
    #endif
    while(1); //hang forever
  }

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
    if (rtc.begin() == false) 
    {
      #ifdef DEBUG_VERBOSE
      Serial.println("RTC not initialized, check wiring");
      #endif
    }
    //Use the time from the Arduino compiler (build time) to set the RTC
    //Keep in mind that Arduino does not get the new compiler time every time it compiles. to ensure the proper time is loaded, open up a fresh version of the IDE and load the sketch.
    if (rtc.setToCompilerTime() == false) 
    {
      #ifdef DEBUG_VERBOSE
      Serial.println("RTC failed to set time");
      #endif
    }
    #ifdef DEBUG_VERBOSE
    Serial.println("RTC online!");
    #endif
  #endif

  Serial.println();

  if(!SD.begin(5))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Card Mount Failed");
    #endif
    return;
  }
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
  //deleteFile(SD, "/py_setup.txt");
  //WriteSetupFile(SD, "/py_setup.txt");
  ReadSetupFile(SD,  "/py_setup.txt");

  ResetTempStable();

  #ifdef DEBUG_VERBOSE
  Serial.println();
  Serial.println("Ready!");
  #endif
  #ifdef HAS_RTC
  //Updates the time variables from RTC
  if (rtc.updateTime() == false) 
  {
    #ifdef DEBUG_VERBOSE
    Serial.print("RTC failed to update");
    #endif
  }
  #endif
  oledDisplay.erase();
  oledDisplay.text(5, 0, "Ready!");
  #ifdef HAS_RTC
  oledDisplay.text(5, 2 * oledDisplay.getStringHeight("X"), rtc.stringTime());
  oledDisplay.text(5, 4 * oledDisplay.getStringHeight("X"), rtc.stringDateUSA());
  #endif
  oledDisplay.display();
  delay(5000);
  prior = millis();
  deviceState = DISPLAY_MENU;
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
      MeasureTireTemps();
      deviceState = DISPLAY_TIRES;
      break;
    case DISPLAY_TIRES:
      DisplayTireTemps(cars[selectedCar]);
      deviceState = DISPLAY_MENU;
      break;
    case DISPLAY_SELECTED_RESULT:
      DisplaySelectedResults(SD, "/py_temps.txt");
      deviceState = DISPLAY_MENU;
      break;
    case CHANGE_SETTINGS:
      ChangeSettings();
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
  int menuCount = 5;
  choices[0].description = "Measure Temps";   choices[0].result = MEASURE_TIRES;
  choices[1].description = cars[selectedCar].carName.c_str();      choices[1].result = SELECT_CAR;
  choices[2].description = "Display Temps";   choices[2].result = DISPLAY_TIRES;
  choices[3].description = "Display Results"; choices[3].result = DISPLAY_SELECTED_RESULT;
  choices[4].description = "Settings";        choices[4].result = CHANGE_SETTINGS;
  
  deviceState =  MenuSelect(choices, menuCount, 6, MEASURE_TIRES, 19); 
}
//
// 
//
void MeasureTireTemps()
{
  char outStr[512];
  tireIdx = 0;  // tire - RF, LF, RR, LR  
  int textPosition[2] = {5, 0};

  // local measurement array, allow preserve measurements on cancel
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int idxMeas = 0; idxMeas < cars[selectedCar].positionCount; idxMeas++)
    {
      currentTemps[(idxTire * cars[selectedCar].tireCount) + idxMeas] = 0.0;
    }
  }
  // measure defined tires
  #ifdef DEBUG_VERBOSE
  sprintf(outStr, "Selected car %s (%d) tire count %d positions %d\n", cars[selectedCar].carName.c_str(), 
                                                                       selectedCar, 
                                                                       cars[selectedCar].tireCount, 
                                                                       cars[selectedCar].positionCount);
  Serial.print(outStr);
  #endif
  while(tireIdx < cars[selectedCar].tireCount)
  {
    // text font
    iFont = 0;
   
    measIdx = 0;  // measure location - O, M, I
    for(int idx = 0; idx < cars[selectedCar].positionCount; idx++)
    {
      currentTemps[(tireIdx * cars[selectedCar].tireCount) + idx] = 0.0;
    }
    ResetTempStable();
    digitalWrite(TEMPSTABLE_LED, LOW);
    unsigned long priorTime = 0;
    unsigned long curTime = 0;
    // text position on OLED screen
    textPosition[0] = 5;
    textPosition[1] = 0;
    // measure  defined positions on tire 
    while(true)
    {
      // get time and process buttons for press/release
      curTime = millis();
      for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
      {
        buttonReleased[btnIdx] = false;
        buttonArray[btnIdx].process(curTime);
      }
      // cancel button released, return
      if (buttonReleased[1])
      {
        buttonReleased[1] = false;
        return;
      }
      // check for stable temp and button release
      // if button released, go next position or next tire
      if(tempStable)
      {
        digitalWrite(TEMPSTABLE_LED, HIGH);
        if (buttonReleased[0])
        {
          measIdx++;
          if(measIdx == cars[selectedCar].positionCount)
          {
            measIdx = 0;
            break;
          }
          currentTemps[(tireIdx * cars[selectedCar].tireCount) + measIdx] = 0;
          ResetTempStable();
          digitalWrite(TEMPSTABLE_LED, LOW);
        }
      }
      else
      {
        buttonReleased[0] = false;
        digitalWrite(TEMPSTABLE_LED, LOW);
      }
      // if not stablized. sample temp every .5 second, check for stable temp
      if(!tempStable && (curTime - priorTime) > 500)
      {
        priorTime = curTime;
        curTime = millis();
        // read temp, check for stable temp, light LED if stable
        currentTemps[(tireIdx * cars[selectedCar].tireCount) + measIdx] = tempSensor.getThermocoupleTemp(tempUnits); // false for F, true or empty for C
        tempStable = CheckTempStable(currentTemps[(tireIdx * cars[selectedCar].tireCount) + measIdx]);
        // text string location
        textPosition[0] = 5;
        textPosition[1] = 0;
        oledDisplay.setFont(demoFonts[iFont]);  
        oledDisplay.erase();
        oledDisplay.text(textPosition[0], textPosition[1], cars[selectedCar].tireLongName[tireIdx]);
        textPosition[1] +=  oledDisplay.getStringHeight(outStr);

        for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
        {
          if(currentTemps[(tireIdx * cars[selectedCar].tireCount) + tirePosIdx] > 0.0)
          {
            sprintf(outStr, "%s %s: %3.1F", cars[selectedCar].tireShortName[tireIdx].c_str(), 
                                            cars[selectedCar].positionShortName[tirePosIdx].c_str(), 
                                            currentTemps[(cars[selectedCar].tireCount * tireIdx) + tirePosIdx]);
          } 
          else
          {
            sprintf(outStr, "%s %s: -----",  cars[selectedCar].tireShortName[tireIdx].c_str(),  
                                             cars[selectedCar].positionShortName[tirePosIdx].c_str());
          }
          oledDisplay.text(textPosition[0], textPosition[1], outStr);
          textPosition[1] +=  oledDisplay.getStringHeight(outStr);
        }
        oledDisplay.display();
      }
    }
    tireIdx++;
  }
  digitalWrite(TEMPSTABLE_LED, LOW);
  oledDisplay.erase();
  textPosition[1] = 0;
  oledDisplay.text(textPosition[0], textPosition[1], "Done");
  textPosition[1] += oledDisplay.getStringHeight("X");
  oledDisplay.text(textPosition[0], textPosition[1], "Updating results");
  oledDisplay.display();
 
  // done, copy local to global
  #ifdef HAS_RTC
    if (rtc.updateTime() == false) 
    {
      Serial.print("RTC failed to update");
    }
    cars[selectedCar].dateTime = rtc.stringTime();
    curTimeStr = rtc.stringTime();
    curTimeStr += " ";
    curTimeStr += rtc.stringDateUSA();
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
      tireTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition] = currentTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition];
      //outStr = strcat(outStr, "\t");
      //outStr = strcat(outStr, sprintf("%.2f", tireTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition]));
      fileLine += ';';//(9);
      fileLine += tireTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition];
    }
  }
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    //outStr = strcat(outStr, "\t");
    //outStr = strcat(outStr, cars[selectedCar].tireShortName[idxTire].c_str());
    fileLine += ';';//(9);
    fileLine += cars[selectedCar].tireShortName[idxTire];
  }
  for(int idxPosition = 0; idxPosition < cars[selectedCar].positionCount; idxPosition++)
  {
    //outStr = strcat(outStr, "\t");
    //outStr = strcat(outStr, cars[selectedCar].positionShortName[idxPosition].c_str());
    fileLine += ';';//char(9);
    fileLine += cars[selectedCar].positionShortName[idxPosition];
  }
  #ifdef DEBUG_VERBOSE
  Serial.println(fileLine);
  Serial.println("Add to results file...");
  #endif
  AppendFile(SD, "/py_temps.txt", fileLine.c_str());
  // update the results HTML file
  #ifdef DEBUG_VERBOSE
  Serial.println("Update HTML file...");
  #endif
  WriteResultsHTML();  
}
//
///
///
void DisplayTireTemps(CarSettings currentResultCar)
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  int textPosition[2] = {5, 0};
  char outStr[255];
  while(true)
  {
    for(int idxTire = 0; idxTire < currentResultCar.tireCount; idxTire++)
    {
      textPosition[1] = 0;
      oledDisplay.setFont(demoFonts[iFont]);  
      oledDisplay.erase();
	  // trim this to the max width of the display?
      sprintf(outStr, currentResultCar.dateTime.c_str());
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
      textPosition[1] +=  oledDisplay.getStringHeight(outStr);
      sprintf(outStr, "%s %s", currentResultCar.tireShortName[idxTire].c_str(), 
                               currentResultCar.carName.c_str());
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
      textPosition[1] +=  (2 * oledDisplay.getStringHeight(outStr));

      for(int tirePosIdx = 0; tirePosIdx < currentResultCar.positionCount; tirePosIdx++)
      {
        sprintf(outStr, "%s: %3.1F", currentResultCar.positionLongName[tirePosIdx].c_str(), 
                                     tireTemps[(currentResultCar.tireCount * idxTire) + tirePosIdx]);
        oledDisplay.text(textPosition[0], textPosition[1], outStr);
        textPosition[1] +=  oledDisplay.getStringHeight(outStr);
      }
      oledDisplay.display();
      curTime = millis();
      priorTime = curTime;
      while(curTime - priorTime < 5000)
      {
        curTime = millis();
        for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
        {
          buttonReleased[btnIdx] = false;
          buttonArray[btnIdx].process(curTime);
        }
        // select button released, go to next tire
        if (buttonReleased[0])
        {
          buttonReleased[0] = false;
          buttonReleased[1] = false;
          break;
        }
        // cancel button released, return
        if (buttonReleased[1])
        {
          buttonReleased[0] = false;
          buttonReleased[1] = false;
          return;
        }
      }
    }
  }
}
//
//
//
int MenuSelect(MenuChoice choices[], int menuCount, int linesToDisplay, int initialSelect, int maxWidth)
{
  char outStr[256];
  int textPosition[2] = {5, 0};
  int rectPosition[4] = {0, 0, 0, 0};
  int charWidth = 0;
  unsigned long curTime = millis();
  int selection = initialSelect;
  #ifdef VERBOSE_DEBUG
  Serial.println("MenuSelect  choices");
  #endif
  for(int selIdx = 0; selIdx < menuCount; selIdx++)
  {
    #ifdef VERBOSE_DEBUG
    Serial.print(choices[selIdx].description);
    Serial.print(" - ");
    Serial.println(choices[selIdx].result);
    #endif
    if(choices[selIdx].result == initialSelect)
    {
      selection = selIdx;
    }
  }
  String selIndicator = " ";
  iFont = 0;
  int displayRange[2] = {0, linesToDisplay - 1 };
  displayRange[1] = (menuCount < linesToDisplay ? menuCount : linesToDisplay) - 1;
  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttonReleased[btnIdx] = false;
  }
  while(true)
  {
    textPosition[0] = 5;
    textPosition[1] = 0;
    oledDisplay.setFont(demoFonts[iFont]);  
    oledDisplay.erase();
    //oledDisplay.text(textPosition[0], textPosition[1], "Select:");
    rectPosition[0] = 0;
    rectPosition[2] = oledDisplay.getWidth() - 2;
    rectPosition[3] = oledDisplay.getStringHeight("X") - 5;
    charWidth = oledDisplay.getStringWidth("X");
    #ifdef VERBOSE_DEBUG
    Serial.print("Character width ");
    Serial.print(charWidth);
    Serial.print(" passed in max characters per line ");
    Serial.print(maxWidth);
    #endif
    maxWidth = (oledDisplay.getWidth() / charWidth) - 3;
    #ifdef VERBOSE_DEBUG
    Serial.print(" calculated max characters per line ");
    Serial.println(maxWidth);
    Serial.print("Choice index 0");
    Serial.print(" ");
    Serial.println(menuCount);
    Serial.print("Choice range ");
    Serial.print(displayRange[0]);
    Serial.print(" ");
    Serial.println(displayRange[1]);
    Serial.print("Selection ");
    Serial.print(selection);
    Serial.print(" ");
    Serial.println(choices[selection].description);
    Serial.print("Selection box X ");
    Serial.print(rectPosition[0]);
    Serial.print(" Y ");
    Serial.print(rectPosition[1]);
    Serial.print(" W ");
    Serial.print(rectPosition[2]);
    Serial.print(" H ");
    Serial.println(rectPosition[3]);
    Serial.print("Screen W ");
    Serial.print(oledDisplay.getWidth());
    Serial.print(" H ");
    Serial.println(oledDisplay.getHeight());
    Serial.println("===========================================");
    Serial.println("Menu:");
    Serial.print("Display range ");
    Serial.print(displayRange[0]);
    Serial.print(" ");
    Serial.println(displayRange[1]);
    #endif
    for(int menuIdx = displayRange[0]; menuIdx <= displayRange[1]; menuIdx++)
    {
      #ifdef VERBOSE_DEBUG
      Serial.println(choices[menuIdx].description);
      #endif
      rectPosition[0] = 0;//textPosition[0];
      rectPosition[1] = textPosition[1];
      if(strlen(choices[menuIdx].description.c_str()) > maxWidth)
      {
        sprintf(outStr, "%s", choices[menuIdx].description.substring(0, maxWidth - 1).c_str());
      }
      else
      {
        sprintf(outStr, "%s", choices[menuIdx].description.c_str());
      }
      rectPosition[2] = oledDisplay.getStringWidth(outStr) + 10;
      rectPosition[3] = oledDisplay.getStringHeight(outStr);
      #ifdef VERBOSE_DEBUG
      Serial.print(outStr);
      #endif
      if(menuIdx == selection)
      {
        #ifdef VERBOSE_DEBUG
        Serial.println(" SELECTED");
        Serial.print("Selection box X ");
        Serial.print(rectPosition[0]);
        Serial.print(" Y ");
        Serial.print(rectPosition[1]);
        Serial.print(" W ");
        Serial.print(rectPosition[2]);
        Serial.print(" H ");
        Serial.println(rectPosition[3]);
        Serial.print("Screen W ");
        Serial.print(oledDisplay.getWidth());
        Serial.print(" H ");
        Serial.print(oledDisplay.getHeight());
        Serial.print(" Y bottom ");
        Serial.println(rectPosition[1] + oledDisplay.getStringHeight("X"));
        #endif
        oledDisplay.rectangleFill(rectPosition[0], rectPosition[1], rectPosition[2], rectPosition[3], COLOR_WHITE);
        oledDisplay.text(textPosition[0], textPosition[1], outStr, COLOR_BLACK);
      }
      else
      {
        #ifdef VERBOSE_DEBUG
        Serial.println(" NOT SELECTED");
        #endif
        oledDisplay.text(textPosition[0], textPosition[1], outStr, COLOR_WHITE);
      }
      textPosition[1] += oledDisplay.getStringHeight("X");
    }
    #ifdef VERBOSE_DEBUG
    Serial.println("displaying to OLED");
    #endif
    oledDisplay.display();
    #ifdef VERBOSE_DEBUG
    Serial.println("done");
    #endif
    while(true)
    {
      curTime = millis();
      for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
      {
        buttonArray[btnIdx].process(curTime);
      }
      // selection made, set state and break
      if(buttonReleased[0])
      {
        buttonReleased[0] = false;
        return choices[selection].result;
      }
      // change selection, break
      else if(buttonReleased[1])
      {
        buttonReleased[1] = false;
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
      else if(buttonReleased[2])
      {
        buttonReleased[2] = false;
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
  selectedCar =  MenuSelect(choices, carCount, 6, 0, 19); 
  #ifdef DEBUG_VERBOSE
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
  int menuCount = 4;
  int result =  0;
  while(result != 3)
  {
    choices[0].description = "Set Date/Time"; choices[0].result = 0;
    choices[1].description = "Set Units";     choices[1].result = 1;
    choices[2].description = "Delete Data";   choices[2].result = 2;
    choices[3].description = "Exit";          choices[3].result = 3;
    result =  MenuSelect(choices, menuCount, 6, 0, 19); 
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
  int menuResult =  MenuSelect(choices, menuCount, 6, 0, 19); 
  // true for C, false for F
  tempUnits = menuResult == 1;
}
//
//
//
void SetDateTime()
{
  char outStr[256];
  int timeVals[6] = {0, 0, 0, 0, 0, 0};  // date, month, year, day, hour, min
  bool isPM = false;
  int textPosition[2] = {0, 0};
  int setIdx = 0;
  unsigned long curTime = millis();
  int delta = 0;
  if (rtc.updateTime() == false) 
  {
    #ifdef VERBOSE_DEBUG
    Serial.print("RTC failed to update");
    #endif
  }
  timeVals[0] = rtc.getDate();
  timeVals[1] = rtc.getMonth();
  timeVals[2] = rtc.getYear();
  timeVals[3] = rtc.getWeekday();
  timeVals[4] = rtc.getHours();
  timeVals[5] = rtc.getMinutes();
  isPM = rtc.isPM();
  iFont = 0;

  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttonReleased[btnIdx] = false;
  }
  while(true)
  {
    textPosition[0] = 5;
    textPosition[1] = 0;
    oledDisplay.erase();
    oledDisplay.setFont(demoFonts[iFont]);  
    oledDisplay.text(textPosition[0], textPosition[1], "Set date/time");
    
    textPosition[1] += oledDisplay.getStringHeight("X");

    sprintf(outStr, "%02d/%02d/%02d ", timeVals[0], timeVals[1], timeVals[2]);
    switch(timeVals[3])
    {
      case 0:
        strcat(outStr, "Su");
        break;
      case 1:
        strcat(outStr, "M");
        break;
      case 2:
        strcat(outStr, "Tu");
        break;
      case 3:
        strcat(outStr, "W");
        break;
      case 4:
        strcat(outStr, "Th");
        break;
      case 5:
        strcat(outStr, "F");
        break;
      case 6:
        strcat(outStr, "Sa");
        break;
      default:
        strcat(outStr, "--");
        break;
    }
    oledDisplay.text(textPosition[0], textPosition[1], outStr);
    if(setIdx < 4)
    {
      textPosition[1] += oledDisplay.getStringHeight(outStr);;
      sprintf(outStr, "%s %s %s %s", (setIdx == 0 ? "--" : "  "), (setIdx == 1 ? "--" : "  "), (setIdx == 2 ? "--" : "  "), (setIdx == 3 ? "--" : "  "));
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
    }
    textPosition[1] += oledDisplay.getStringHeight(outStr);;
    sprintf(outStr, "%02d:%02d %s", timeVals[4], timeVals[5], (isPM ? "PM" : "AM"));
    oledDisplay.text(textPosition[0], textPosition[1], outStr);
    if(setIdx > 3)
    {
      textPosition[1] += oledDisplay.getStringHeight(outStr);;
      sprintf(outStr, "%s %s %s", (setIdx == 4 ? "--" : "  "), (setIdx == 5 ? "--" : "  "), (setIdx == 6 ? "--" : "  "));
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
    }
    
    oledDisplay.display();
    while(!buttonReleased[0])
    {
      curTime = millis();
      for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
      {
        buttonReleased[btnIdx] = false;
        buttonArray[btnIdx].process(curTime);
      }
      // save time element, advance
      if(buttonReleased[0])
      {
        buttonReleased[0] = false;
        setIdx++;
        if(setIdx > 6)
        {
          break;
        }
      }
      // increase/decrease
      else if(buttonReleased[1])
      {
        buttonReleased[1] = false;
        delta = 1;
      }
      else if(buttonReleased[2])
      {
        buttonReleased[2] = false;
        delta = -1;
      }
      else
      {
        delta = 0;
        continue;
      }
      switch (setIdx)
      {
        case 0:  // date
          timeVals[0] += delta;
          if(timeVals[0] <= 0)
          {
            timeVals[0] = 31;
          }
          if(timeVals[0] > 31)
          {
            timeVals[0] = 1;
          }
          break;
        case 1:  // month
          timeVals[1] += delta;
          if(timeVals[1] <= 0)
          {
            timeVals[1] = 12;
          }
          if(timeVals[1] > 12)
          {
            timeVals[1] = 1;
          }
          break;
        case 2:  // year
          timeVals[2] += delta;
          if(timeVals[2] < 0)
          {
            timeVals[2] = 99;
          }
          if(timeVals[2] > 99)
          {
            timeVals[2] = 0;
          }
          break;
        case 3:  // day
          timeVals[3] += delta;
          if(timeVals[3] < 0)
          {
            timeVals[3] = 6;
          }
          if(timeVals[3] > 6)
          {
            timeVals[3] = 0;
          }
          break;
        case 4:  // hour
          timeVals[4] += delta;
          if(timeVals[4] < 0)
          {
            timeVals[4] = 12;
          }
          if(timeVals[4] > 12)
          {
            timeVals[4] = 1;
          }
          break;
        case 5:  // minute
          timeVals[5] += delta;
          if(timeVals[5] < 0)
          {
            timeVals[5] = 59;
          }
          if(timeVals[5] > 59)
          {
            timeVals[5] = 0;
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
    rtc.set24Hour();
    if (timeVals[4] < 12)
    {
      timeVals[4] += 12;
    }
  }
  //              hund, sec, min,         hour,        date         month,       year,        day
  if(!rtc.setTime(0,    0,   timeVals[5], timeVals[4], timeVals[0], timeVals[1], timeVals[2], timeVals[3]))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Set time failed");
    #endif
  }
  rtc.set12Hour();
}
//
//
//
void DeleteDataFile()
{
  int menuCount = 2;
  choices[0].description = "Yes";      choices[0].result = 1;
  choices[1].description = "No";   choices[1].result = 0;
  int menuResult = MenuSelect (choices, menuCount, 6, 1, 19); 
  if(menuResult == 1)
  {
    DeleteFile(SD, "/py_temps.txt");
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
//
//
void ReadSetupFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_VERBOSE
  Serial.printf("Reading file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_READ);
  if(!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
    return;
  }
  char buf[512];

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
    cars[carIdx].tireShortName = new String[cars[carIdx].tireCount];
    cars[carIdx].tireLongName = new String[cars[carIdx].tireCount];
    // read tire short and long names
    for(int tireIdx = 0; tireIdx < cars[carIdx].tireCount; tireIdx++)
    {
      ReadLine(file, buf);
      cars[carIdx].tireShortName[tireIdx] = buf;
      ReadLine(file, buf);
      cars[carIdx].tireLongName[tireIdx] = buf;
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
  #ifdef DEBUG_VERBOSE
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
      Serial.println(cars[carIdx].tireLongName[idx]);
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
  // allocate final and working tire temps
  //tireTemps = (float*)calloc((maxTires * maxPositions), sizeof(float));
  //currentTemps = (float*)calloc((maxTires * maxPositions), sizeof(float));
  //
  file.close();
}
//
//
//
void WriteSetupFile(fs::FS &fs, const char * path)
{
  #ifdef DEBUG_VERBOSE
  Serial.printf("Writing setup file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open file for writing");
    #endif
    return;
  }
  file.println("5");            // number of cars
  file.println("Brian Z4");    // car
  file.println("4");           // wheels
  file.println("RF");
  file.println("Right Front");
  file.println("LF");
  file.println("Left Front");
  file.println("LR");
  file.println("Left Rear");
  file.println("RR");
  file.println("Right Rear");
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
  file.println("Right Front");
  file.println("LF");
  file.println("Left Front");
  file.println("LR");
  file.println("Left Rear");
  file.println("RR");
  file.println("Right Rear");
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
  file.println("RF");
  file.println("Right Front");
  file.println("RM");
  file.println("Right Mid");
  file.println("LF");
  file.println("Left Front");
  file.println("LM");
  file.println("Left Mid");
  file.println("LR");
  file.println("Left Rear");
  file.println("RR");
  file.println("Right Rear");
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
  file.println("R");
  file.println("Rear");
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
  file.println("RF");
  file.println("Right Front");
  file.println("LF");
  file.println("Left Front");
  file.println("R");
  file.println("Rear");
  file.println("3");           // Measurements
  file.println("O");
  file.println("Outer");
  file.println("M");
  file.println("Middle");
  file.println("I");
  file.println("Inner");
  file.println("=========="); 

  file.close();
  #ifdef DEBUG_VERBOSE
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
  char buf[512];
  char* token;
  File file = SD.open("/py_temps.txt", FILE_READ);
  if(!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
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
    choices[menuCnt].description = buf;      choices[menuCnt].result = menuCnt;
    menuCnt++;
  }
  int menuResult = MenuSelect(choices, menuCnt, 6, 0, 19);
  file.close();
  // at this point, we need to parse the selected line and add to a measurment structure for display
  // get to the correct line
  file = SD.open("/py_temps.txt", FILE_READ);
  if(!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open file for reading");
    #endif
    return;
  }
  for (int lineNumber = 0; lineNumber <= menuResult; lineNumber++)
  {
    ReadLine(file, buf);
  } 
  file.close();
  ParseResult(buf, currentResultCar);
  DisplayTireTemps(currentResultCar);
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
void WriteResultsHTML(/*fs::FS &fs, const char * path*/)
{
  CarSettings currentResultCar;
  char buf[512];
  File fileIn = SD.open("/py_temps.txt", FILE_READ);
  if(!fileIn)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open files for TXT file for reading");
    #endif
    return;
  }
  // create a new HTML file
  if(!SD.remove("/py_res.html"))
  {
    Serial.print("failed to delete /py_res.html");
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
  while(true)
  {
    ReadLine(fileIn, buf);
    // end of file
    if(strlen(buf) == 0)
    {
      break;
    }
    ParseResult(buf, currentResultCar);
    AppendFile(SD, "/py_res.html", "		    <tr>");
    sprintf(buf, "<td>%s</td>", currentResultCar.dateTime.c_str());
    AppendFile(SD, "/py_res.html", buf);
    #ifdef DEBUG_VERBOSE
    Serial.print("date time ");
    Serial.println(buf);
    #endif
    sprintf(buf, "<td>%s</td>", currentResultCar.carName.c_str());
    AppendFile(SD, "/py_res.html", buf);
    #ifdef DEBUG_VERBOSE
    Serial.print("car ");
    Serial.println(buf);
    #endif
    for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
    {
      tireMin =  999.9;
      tireMax = -999.9;
      sprintf(buf, "<td>%s</td>", currentResultCar.tireShortName[t_idx].c_str());
      #ifdef DEBUG_VERBOSE
      Serial.print("tire ");
      Serial.println(buf);
      #endif
      // get min/max temps
      for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
      {
        tireMin = tireMin < tireTemps[(currentResultCar.tireCount * t_idx) + p_idx] ? tireMin : tireTemps[(currentResultCar.tireCount * t_idx) + p_idx];
        tireMax = tireMax > tireTemps[(currentResultCar.tireCount * t_idx) + p_idx] ? tireMax : tireTemps[(currentResultCar.tireCount * t_idx) + p_idx];
      }
      #ifdef DEBUG_VERBOSE
      Serial.print("tire temp range ");
      Serial.print(tireMin);
      Serial.print(" - ");
      Serial.println(tireMax);
      #endif
      // add cells to file
      for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
      {
        if(tireTemps[(currentResultCar.tireCount * t_idx) + p_idx] == tireMin)
        {
          sprintf(buf, "<td bgcolor=\"green\">%s %0.2f</td>", currentResultCar.positionShortName[p_idx].c_str(), tireTemps[(currentResultCar.tireCount * t_idx) + p_idx]);
        }
        else if (tireTemps[(currentResultCar.tireCount * t_idx) + p_idx] == tireMax)
        {
          sprintf(buf, "<td bgcolor=\"red\">%s %0.2f</td>", currentResultCar.positionShortName[p_idx].c_str(), tireTemps[(currentResultCar.tireCount * t_idx) + p_idx]);
        }
        else
        {
          sprintf(buf, "<td bgcolor=\"orange\">%s %0.2f</td>", currentResultCar.positionShortName[p_idx].c_str(), tireTemps[(currentResultCar.tireCount * t_idx) + p_idx]);
        }
        #ifdef DEBUG_VERBOSE
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
    #ifdef DEBUG_VERBOSE
    Serial.println("end of row");
    #endif
    AppendFile(SD, "/py_res.html", "		    </tr>");
  }
  #ifdef DEBUG_VERBOSE
  Serial.println("end of file");
  #endif
  fileIn.close();
  AppendFile(SD, "/py_res.html", "    </table>");
  AppendFile(SD, "/py_res.html", "</body>");
  AppendFile(SD, "/py_res.html", "</html>");
  
  //fileIn = SD.open("/py_res.html", FILE_READ);
  while(true)
  {
    ReadLine(fileIn, buf);
    // end of file
    if(strlen(buf) == 0)
    {
      break;
    }
    Serial.println(buf);
  }
  //fileIn.close();
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
  int measureRange[2] = {99, 99};
  int tireNameRange[2] = {99, 99};
  int posNameRange[2] = {99, 99};
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
  // // dynamic tire temp array
  // float tireTemps[60];
  // float currentTemps[60];
  token = strtok(buf, ";");
  while(token != NULL)
  {
    #ifdef DEBUG_VERBOSE
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
      #ifdef DEBUG_VERBOSE
      Serial.println("Timestamp");
      #endif
    }
    // 1 - car info
    if(tokenIdx == 1)
    {
      currentResultCar.carName = token;
      #ifdef DEBUG_VERBOSE
      Serial.println(" car");
      #endif
    }
    // 2 - tire count
    else if(tokenIdx == 2)
    {
      currentResultCar.tireCount = atoi(token);
      currentResultCar.tireShortName = new String[currentResultCar.tireCount];
      currentResultCar.tireLongName = new String[currentResultCar.tireCount];
      #ifdef DEBUG_VERBOSE
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
      #ifdef DEBUG_VERBOSE
      Serial.println(" positions");
      #endif
    }
    // tire temps
    else if((tokenIdx >= measureRange[0]) && (tokenIdx <= measureRange[1]))
    {
      tireTemps[measureIdx] = atof(token);
      currentTemps[measureIdx] = tireTemps[measureIdx];
      #ifdef DEBUG_VERBOSE
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
      #ifdef DEBUG_VERBOSE
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
      #ifdef DEBUG_VERBOSE
      Serial.println(" position name");
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
  return;
}
//
//
//
void AppendFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef DEBUG_VERBOSE
  Serial.printf("Appending to file: %s\n", path);
  #endif
  File file = fs.open(path, FILE_APPEND);
  if(!file)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Failed to open file for appending");
    #endif
    return;
  }
  if(file.println(message))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Message appended");
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
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
  #ifdef DEBUG_VERBOSE
  Serial.printf("Deleting file: %s\n", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("File deleted");
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Delete failed");
    #endif
  }
}
//
// handle pressed state
//
void button_pressedCallback(uint8_t pinIn)
{
  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    if(buttonPin[buttonIdx] == pinIn)
    {
      buttonReleased[buttonIdx] = false;
      break;
    }
  }
}
//
// handle released state
//
void button_releasedCallback(uint8_t pinIn)
{
  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    if(buttonPin[buttonIdx] == pinIn)
    {
      buttonReleased[buttonIdx] = true;
      break;
    }
  }
}
//
// handle pressed duration
//
void button_pressedDurationCallback(uint8_t pinIn, unsigned long duration)
{ 
}
//
// handle released duration
//
void button_releasedDurationCallback(uint8_t pinIn, unsigned long duration)
{
}