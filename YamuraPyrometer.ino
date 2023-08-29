/*
  YamuraLog Recording Tire Pyrometer
  By: Brian Smith
  Yamura Electronics Division
  Date: July 2023
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware License).

  Hardware
  3 buttons - menu select/record temp/next tire temp review
              menu down/exit tire temp review
              menu up
  On/Off switch (battery, always on when charging on USB)
  LED/resistor for stable temp indicator
  Sparkfun ESP32 Thing Plus
  Sparkfun K type thermocouple amplifier
  Sparkfun RTC (to be added)
  GeeekPi OLED Display Module I2C 128X64 Pixel 0.96 Inch Display Module
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

// OLED Fonts
#include <res/qw_fnt_5x7.h>
#include <res/qw_fnt_8x16.h>
#include <res/qw_fnt_31x48.h>
#include <res/qw_fnt_7segment.h>
#include <res/qw_fnt_largenum.h>

// uncomment for debug to serial monitor
//#define DEBUG_VERBOSE

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
#define DISPLAY_RESULTS 4

static InputDebounce buttonArray[BUTTON_COUNT];
int buttonPin[BUTTON_COUNT] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4};
int buttonReleased[BUTTON_COUNT] = {0, 0, 0, 0};
unsigned long pressDuration[BUTTON_COUNT] = {0, 0, 0, 0};

// car info structure
struct CarSettings
{
    String carName;
    int tireCount;
    String* tireShortName;
    String* tireLongName;
    int positionCount;
    String* positionShortName;
    String* positionLongName;
};
CarSettings* cars;
// dynamic tire temp array
float* tireTemps;
float* currentTemps;

String curMenu[50];

// devices
MCP9600 tempSensor;
//QwiicMicroOLED oledDisplay;
QwiicTransparentOLED oledDisplay;

// An array of available fonts
QwiicFont *demoFonts[] = {
    &QW_FONT_5X7,
    &QW_FONT_8X16,
    &QW_FONT_31X48,
    &QW_FONT_LARGENUM,
    &QW_FONT_7SEGMENT};
int nFONTS = sizeof(demoFonts) / sizeof(demoFonts[0]);
int iFont = 1;

unsigned long prior = 0;

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
    Serial.println("Device begin failed. Freezing...");
    #endif
    digitalWrite(TEMPSTABLE_LED, HIGH);
    delay(200);
    digitalWrite(TEMPSTABLE_LED, LOW);
    delay(200);
  }

  iFont = 0;
  oledDisplay.setFont(demoFonts[iFont]);  
  oledDisplay.erase();
  oledDisplay.text(0, 0, "Yamura Electronics");
  oledDisplay.text(0, oledDisplay.getStringHeight("X"), "Recording Pyrometer");
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
    Serial.println("Device acknowledged!");
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Device did not acknowledge! Freezing.");
    #endif
    while(1); //hang forever
  }

  //check if the Device ID is correct
  if(tempSensor.checkDeviceID())
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Device ID is correct!");        
    #endif
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Device ID is not correct! Freezing.");
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
  oledDisplay.erase();
  oledDisplay.text(0, 0, "Ready!");
  oledDisplay.display();
  delay(1000);
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
      DisplayTireTemps();
      deviceState = DISPLAY_MENU;
      break;
    case DISPLAY_RESULTS:
      ReadResults(SD, "/py_temps.txt");
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
  curMenu[0] = "Car/Driver";
  curMenu[1] = "Measure Temps";
  curMenu[2] = "Display Temps";
  curMenu[3] = "Display Results";
  deviceState =  MenuSelect(curMenu, 4, 3, 0, 15) + 1; 
}
//
// 
//
void MeasureTireTemps()
{
  char outStr[512];
  tireIdx = 0;  // tire - RF, LF, RR, LR  
  int textPosition[2] = {0, 0};

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
  sprintf(outStr, "Selected car %s (%d) tire count %d positions %d\n", cars[selectedCar].carName.c_str(), selectedCar, cars[selectedCar].tireCount, cars[selectedCar].positionCount);
  Serial.print(outStr);
  #endif
  while(tireIdx < cars[selectedCar].tireCount)
  {
    // text font
    iFont = 1;
   
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
    textPosition[0] = 0;
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
        currentTemps[(tireIdx * cars[selectedCar].tireCount) + measIdx] = tempSensor.getThermocoupleTemp(false); // false for F, true or empty for C
        tempStable = CheckTempStable(currentTemps[(tireIdx * cars[selectedCar].tireCount) + measIdx]);
        // text string location
        textPosition[0] = 0;
        textPosition[1] = 0;
        oledDisplay.setFont(demoFonts[iFont]);  
        oledDisplay.erase();
        oledDisplay.text(textPosition[0], textPosition[1], cars[selectedCar].tireLongName[tireIdx]);
        textPosition[1] +=  oledDisplay.getStringHeight(outStr);

        for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
        {
          if(currentTemps[(tireIdx * cars[selectedCar].tireCount) + tirePosIdx] > 0.0)
          {
            sprintf(outStr, "%s %s: %3.1F", cars[selectedCar].tireShortName[tireIdx], cars[selectedCar].positionShortName[tirePosIdx], currentTemps[(cars[selectedCar].tireCount * tireIdx) + tirePosIdx]);
          } 
          else
          {
            sprintf(outStr, "%s %s: -----",  cars[selectedCar].tireShortName[tireIdx],  cars[selectedCar].tireShortName[tirePosIdx]);
          }
          oledDisplay.text(textPosition[0], textPosition[1], outStr);
          textPosition[1] +=  oledDisplay.getStringHeight(outStr);
        }
        oledDisplay.display();
      }
    }
    tireIdx++;
  }
  // done, copy local to global
  sprintf(outStr, "%d\t%s", millis(), cars[selectedCar].carName.c_str());
  String fileLine = outStr;
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int idxPosition = 0; idxPosition < cars[selectedCar].positionCount; idxPosition++)
    {
      tireTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition] = currentTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition];
      fileLine += "\t";
      fileLine += tireTemps[(cars[selectedCar].tireCount * idxTire) + idxPosition];
    }
  }
  #ifdef DEBUG_VERBOSE
  Serial.println(fileLine);
  #endif
  AppendFile(SD, "/py_temps.txt", fileLine.c_str());
}
//
///
///
void DisplayTireTemps()
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  int textPosition[2] = {0, 0};
  char outStr[255];
  while(true)
  {
    for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
    {
      textPosition[1] = 0;
      oledDisplay.setFont(demoFonts[iFont]);  
      oledDisplay.erase();
	  // trim this to the max width of the display?
      sprintf(outStr, "%s %s", cars[selectedCar].tireShortName[idxTire], cars[selectedCar].carName.c_str());
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
      textPosition[1] +=  oledDisplay.getStringHeight(outStr);

      for(int tirePosIdx = 0; tirePosIdx < cars[selectedCar].positionCount; tirePosIdx++)
      {
        sprintf(outStr, "%s: %3.1F", cars[selectedCar].positionLongName[tirePosIdx], tireTemps[(cars[selectedCar].tireCount * idxTire) + tirePosIdx]);
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
int MenuSelect(String menuList[], int menuCount, int linesToDisplay, int initialSelect, int maxWidth)
{
  char outStr[256];
  int textPosition[2] = {0, 0};
  unsigned long curTime = millis();
  int selection = initialSelect;
  String selIndicator = " ";
  iFont = 1;
  int displayWindow[2] = {0, linesToDisplay - 1 };
  for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
  {
    buttonReleased[btnIdx] = false;
  }
  while(true)
  {
    textPosition[0] = 0;
    textPosition[1] = 0;
    oledDisplay.setFont(demoFonts[iFont]);  
    oledDisplay.erase();
    oledDisplay.text(textPosition[0], textPosition[1], "Select:");
    for(int menuIdx = displayWindow[0]; menuIdx <= displayWindow[1]; menuIdx++)
    {
      if(menuIdx == selection)
      {
        selIndicator = ">";
      }
      else
      {
        selIndicator = "-";
      }
      sprintf(outStr, "%s%s", selIndicator, menuList[menuIdx].substring(0, maxWidth - 1));
      textPosition[1] += oledDisplay.getStringHeight("X");
      oledDisplay.text(textPosition[0], textPosition[1], outStr);
    }
    oledDisplay.display();
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
        return selection;
      }
      // change selection, break
      else if(buttonReleased[1])
      {
        buttonReleased[1] = false;
        selection = (selection + 1) < menuCount ? (selection + 1) : 0;
        // handle loop back to start
        if (selection < displayWindow[0])
        {
          displayWindow[0] = selection;
          displayWindow[1] = displayWindow[0] + linesToDisplay - 1; 
        }
        // show next line at bottom
        else 
        if (selection > displayWindow[1])
        {
          displayWindow[1] = selection; 
          displayWindow[0] = displayWindow[1] - linesToDisplay + 1;
        }
        break;
      }
      else if(buttonReleased[2])
      {
        buttonReleased[2] = false;
        selection = (selection - 1) >= 0 ? (selection - 1) : menuCount - 1;
        // handle loop back to start
        if (selection < displayWindow[0])
        {
          displayWindow[0] = selection;
          displayWindow[1] = displayWindow[0] + linesToDisplay - 1; 
        }
        // show next line at bottom
        else 
        if (selection > displayWindow[1])
        {
          displayWindow[1] = selection; 
          displayWindow[0] = displayWindow[1] - linesToDisplay + 1;
        }
        break;
      }
      delay(100);
    }
  }
  return selection;
}
//
//
//
void SelectCar()
{
  for(int idx = 0; idx < carCount; idx++)
  {
    curMenu[idx] = cars[idx].carName;
  }
  selectedCar =  MenuSelect(curMenu, carCount, 3, 0, 15); 
  Serial.print("Selected car from SelectCar() ") ;
  Serial.print(selectedCar) ;
  Serial.print(" ") ;
  Serial.println(cars[selectedCar].carName.c_str());
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
  File file = fs.open(path);
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
  tireTemps = (float*)calloc((maxTires * maxPositions), sizeof(float));
  currentTemps = (float*)calloc((maxTires * maxPositions), sizeof(float));
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
void ReadResults(fs::FS &fs, const char * path)
{
  char readText[256];
  File file = fs.open(path);
  while(true)
  {
     ReadLine(file, readText);
     if(strlen(readText) == 0)
     {
       break;
     }
     Serial.println(readText);
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
  #ifdef DEBUG_VERBOSE
  Serial.println("button_releasedCallback");
  #endif

  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    if(buttonPin[buttonIdx] == pinIn)
    {
      #ifdef DEBUG_VERBOSE
      Serial.print("Button ");
      Serial.print(buttonIdx);
      Serial.println(" released");
      #endif
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