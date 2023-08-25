/*
  YamuraLog Just a Tire Pyrometer
  By: Brian Smith
  Yamura Electronics Division
  Date: July 2023
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware License).

  Hardware Connections:
  Attach the Qwiic Shield to your Arduino/Photon/ESP32 or other
  Plug the sensor onto the shield
  Serial.print it out at 115200 baud to serial monitor.

  Operation
  Just display probe temps when on

*/

#include <SparkFun_MCP9600.h>
#include <SparkFun_Qwiic_OLED.h> //http://librarymanager/All#SparkFun_Qwiic_Graphic_OLED
#include "InputDebounce.h"
// OLED Fonts
#include <res/qw_fnt_5x7.h>
#include <res/qw_fnt_8x16.h>
#include <res/qw_fnt_31x48.h>
#include <res/qw_fnt_7segment.h>
#include <res/qw_fnt_largenum.h>

#define TEMPSTABLE_LED 4
#define BUTTON_1 12
#define BUTTON_2 27
#define BUTTON_3 33
#define BUTTON_4 15
#define BUTTON_DEBOUNCE_DELAY   20   // [ms]

#define BUTTON_COUNT 4
static InputDebounce buttonArray[BUTTON_COUNT];
int buttonPin[BUTTON_COUNT] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4};
int buttonReleased[BUTTON_COUNT] = {0, 0, 0, 0};
unsigned long pressDuration[BUTTON_COUNT] = {0, 0, 0, 0};

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

#define TIRE_COUNT 4
#define MEASURE_COUNT 3

unsigned long prior = 0;

int tempIdx = 0;
float tempStableRecent[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
float tempStableMinMax[2] = {150.0, -150.0};
bool tempStable = false;

int tireIdx = 0;
int measIdx = 0;
float tireTemps[TIRE_COUNT][MEASURE_COUNT] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
float tempRes = 1.0;
String tireShortName[TIRE_COUNT] = {"RF", "LF", "RR", "LR"};
String tireLongName[TIRE_COUNT] = {"Right Front", "Left Front", "Left Rear", "Right Rear"};
String tirePos[MEASURE_COUNT] = {"O", "M", "I"};
//
//
//
void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println("YamuraLog Recording Tire Pyrometer V1.0");

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
  if (oledDisplay.begin(Wire, 0x3C) == false)
  {
    Serial.println("Device begin failed. Freezing...");
    while (true);
  }

  iFont = 0;
  oledDisplay.setFont(demoFonts[iFont]);  
  oledDisplay.erase();
  oledDisplay.text(0, 0, "Yamura Electronics");
  oledDisplay.text(0, oledDisplay.getStringHeight("X"), "Recording Pyrometer");
  oledDisplay.display();
  delay(10000);
  Serial.print("OLED screen size ");
  Serial.print(oledDisplay.getWidth());
  Serial.print(" x ");
  Serial.print(oledDisplay.getHeight());
  Serial.println("");


  tempSensor.begin();       // Uses the default address (0x60) for SparkFun Thermocouple Amplifier
  //check if the sensor is connected
  if(tempSensor.isConnected())
  {
      Serial.println("Device will acknowledge!");
  }
  else 
  {
    Serial.println("Device did not acknowledge! Freezing.");
    while(1); //hang forever
  }

  //check if the Device ID is correct
  if(tempSensor.checkDeviceID())
  {
    Serial.println("Device ID is correct!");        
  }
  else 
  {
    Serial.println("Device ID is not correct! Freezing.");
    while(1);
  }
  //change the thermocouple type being used
  Serial.print("Current Thermocouple Type: ");
  switch(tempSensor.getThermocoupleType())
  {
    case 0b000:
      Serial.println("K Type");
      break;
    case 0b001:
      Serial.println("J Type");
      break;
    case 0b010:
      Serial.println("T Type");
      break;
    case 0b011:
      Serial.println("N Type");
      break;
    case 0b100:
      Serial.println("S Type");
      break;
    case 0b101:
      Serial.println("E Type");
      break;
    case 0b110:
      Serial.println("B Type");
      break;
    case 0b111:
      Serial.println("R Type");
      break;
    default:
      Serial.println("Unknown Thermocouple Type");
      break;
  }
  if(tempSensor.getThermocoupleType() != TYPE_K)
  {
    Serial.println("Setting Thermocouple Type to K (0b000)");
    tempSensor.setThermocoupleType(TYPE_K);
    //make sure the type was set correctly!
    if(tempSensor.getThermocoupleType() == TYPE_K)
    {
        Serial.println("Thermocouple Type set sucessfully!");
    }
    else
    {
      Serial.println("Setting Thermocouple Type failed!");
    }
  }
  Serial.print("Ambient resolution ");
  switch(tempSensor.getAmbientResolution())
  {
    case RES_ZERO_POINT_0625:
      Serial.print(" 0.0625C / 0.1125F");
      tempRes = 0.1125;
      break;
    case RES_ZERO_POINT_25:
      Serial.print(" 0.25 C / 0.45F");
      tempRes = 0.45;
      break;
    default:
      Serial.print(" unknown ");
      break;
  }
  Serial.print(" Thermocouple resolution ");
  switch(tempSensor.getThermocoupleResolution())
  {
    case RES_12_BIT:
      Serial.print(" 12 bit");
      break;
    case RES_14_BIT:
      Serial.print(" 14 bit");
      break;
    case RES_16_BIT:
      Serial.print(" 16 bit");
      break;
    case RES_18_BIT:
      Serial.print(" 18 bit");
      break;
    default:
      Serial.print(" unknown ");
      break;
  }
  Serial.println();

  ResetTempStable();

  Serial.println();
  Serial.println("Ready!");
  oledDisplay.erase();
  oledDisplay.text(0, 0, "Ready!");
  oledDisplay.display();
  delay(1000);
  prior = millis();
  MeasureTimeTemp();
  DisplayTireTemps();
}
//
//print the thermocouple, ambient and delta temperatures every 200ms if available
//
void loop()
{
}
void MeasureTimeTemp()
{
  char outStr[512];
  tireIdx = 0;  // tire - RF, LF, RR, LR  
  // local measurement array, allow preserve measurements on cancel
  float localTemps[TIRE_COUNT][MEASURE_COUNT];
  for(int idxTire = 0; idxTire < TIRE_COUNT; idxTire++)
  {
    for(int idxMeas = 0; idxMeas < MEASURE_COUNT; idxMeas++)
    {
      localTemps[idxTire][idxMeas] = 0.0;
    }
  }
  //
  while(tireIdx < TIRE_COUNT)
  {
    // text font
    iFont = 1;
   
    measIdx = 0;  // measure location - O, M, I
    for(int idx = 0; idx < 3; idx++)
    {
      localTemps[tireIdx][idx] = 0.0;
    }
    ResetTempStable();
    digitalWrite(TEMPSTABLE_LED, LOW);
    unsigned long priorTime = 0;
    unsigned long curTime = 0;
    //
    int xText = 0;
    int yText = 0;
    bool displayed = false;
    // 
    while(true)
    {
      if(!displayed)
      {
        Serial.print("Measuring tire ");
        Serial.print(tireLongName[tireIdx]);
        Serial.print(" (");
        Serial.print(tireIdx);
        Serial.print(") ");
        Serial.print("position ");
        Serial.print(tirePos[measIdx]);
        Serial.print(" (");
        Serial.print(measIdx);
        Serial.println(")");
        displayed = true;
      }
      // get time and process buttons for press/release
      curTime = millis();
      for(int btnIdx = 0; btnIdx < BUTTON_COUNT; btnIdx++)
      {
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
          buttonReleased[0] = false;
          displayed = false;
          if(measIdx == 2)
          {
            measIdx = 0;
            break;
          }
          measIdx++;
          localTemps[tireIdx][measIdx] = 0;
          ResetTempStable();
          digitalWrite(TEMPSTABLE_LED, LOW);
        }
      }
      else
      {
        buttonReleased[0] = false;
        digitalWrite(TEMPSTABLE_LED, LOW);
      }
      // sample temp every .25 seconds, check for stable temp
      if((curTime - priorTime) > 250)
      {
        priorTime = curTime;
        curTime = millis();
        // read temp, check for stable temp, light LED if stable
        localTemps[tireIdx][measIdx] = tempSensor.getThermocoupleTemp(false); // false for F, true or empty for C
        tempStable = CheckTempStable(localTemps[tireIdx][measIdx]);
        // text string location
        xText = 0;
        yText = 0;
        oledDisplay.setFont(demoFonts[iFont]);  
        oledDisplay.erase();
        oledDisplay.text(xText, yText, tireLongName[tireIdx]);
        yText +=  oledDisplay.getStringHeight(outStr);

        for(int tirePosIdx = 0; tirePosIdx < MEASURE_COUNT; tirePosIdx++)
        {
          if(localTemps[tireIdx][tirePosIdx] > 0.0)
          {
            sprintf(outStr, "%s %s: %3.1F", tireShortName[tireIdx], tirePos[tirePosIdx], localTemps[tireIdx][tirePosIdx]);
          } 
          else
          {
            sprintf(outStr, "%s %s: -----", tireShortName[tireIdx], tirePos[tirePosIdx]);
          }
          oledDisplay.text(xText, yText, outStr);
          yText +=  oledDisplay.getStringHeight(outStr);
        }
        oledDisplay.display();
      }
    }
    tireIdx++;
  }
  // done, copy local to global
  for(int idxTire = 0; idxTire < TIRE_COUNT; idxTire++)
  {
    for(int idxMeas = 0; idxMeas < MEASURE_COUNT; idxMeas++)
    {
      tireTemps[idxTire][idxMeas] = localTemps[idxTire][idxMeas];
    }
  }
}
void DisplayTireTemps()
{
  for(int idxTire = 0; idxTire < TIRE_COUNT; idxTire++)
  {
    Serial.print(tireShortName[idxTire]);
    Serial.print(":");
    for(int idxMeas = 0; idxMeas < MEASURE_COUNT; idxMeas++)
    {
      Serial.print("\t");
      Serial.print(tirePos[idxMeas]);
      Serial.print(" ");
      Serial.print(tireTemps[idxTire][idxMeas]);
    }
    Serial.println();
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
// handle pressed state
//
void button_pressedCallback(uint8_t pinIn)
{
  //Serial.print("button_pressedCallback ");
  //Serial.println(pinIn);

  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    if(buttonPin[buttonIdx] == pinIn)
    {
      //Serial.print("button pressed ");
      //Serial.println(buttonIdx);
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
  //Serial.print("button_releasedCallback ");
  //Serial.println(pinIn);

  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
    if(buttonPin[buttonIdx] == pinIn)
    {
      //Serial.print("button released ");
      //Serial.println(buttonIdx);
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
  //Serial.print("button_pressedDurationCallback pin ");
  //Serial.print(pinIn);
  //Serial.print(" for ");
  //Serial.println(duration);

  for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  {
//    if(buttonPin[buttonIdx] == pinIn)
//    {
//      Serial.print("button ");
//      Serial.print(buttonIdx);
//      Serial.print(" pressed for ");
//      Serial.println(duration);
//      break;
//    }
  }
}
//
// handle released duration
//
void button_releasedDurationCallback(uint8_t pinIn, unsigned long duration)
{
  //Serial.print("button_releasedDurationCallback pin ");
  //Serial.print(pinIn);
  //Serial.print(" for ");
  //Serial.println(duration);
  //for(int buttonIdx = 0; buttonIdx < BUTTON_COUNT; buttonIdx++)
  //{
  //  if(buttonPin[buttonIdx] == pinIn)
  //  {
  //    Serial.print("button ");
  //    Serial.print(buttonIdx);
  //    Serial.print(" released for ");
  //    Serial.println(duration);
  //    break;
  //  }
  //}
}