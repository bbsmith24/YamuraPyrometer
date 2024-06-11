/*
  YamuraLog Recording Tire Pyrometer
  By: Brian Smith
  Yamura Motors LLC
  Date: September 2023
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware License).

  Hardware
  3 buttons - menu select/record temp/next tire temp review
              menu down/exit tire temp review
              menu up
  ESP32 WROOM-32D Devkit
  I2C K type thermocouple amplifier I2C address MPC9600 0x60, MPC9601 0x67
  I2C RTC                           I2C address 0x69 (8563) 0x51 (3231)
  Hosyond 4" TFT/ST7796 SPI Module with SD card reader (8GB is fine, not huge amount of data being stored)
  On/Off switch
  3 NO momentary switches
  4xAA battery box
  misc headers
  2x ElectroCookie solderable breadboards
  3D printed box

  measurement records to microSD card
  device and car definitions files stored on microSD card
  HTML files for web interface in LittleFS
  wifi interface for display, up/down load (to add)
*/
#include "YamuraPyrometer.h"
//
// required initialization file
//
void setup()
{
  char outStr[128];
  Serial.begin(115200);
  // location of text
  textPosition[0] = 5;
  textPosition[1] = 0;

  // user input buttons
  buttons[0].buttonPin = 12;
  buttons[1].buttonPin = 14;
  buttons[2].buttonPin = 26;
  // set up tft display
  tftDisplay.init();
  tftDisplay.invertDisplay(false);
  RotateDisplay(true);  
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
  tftDisplay.drawString("Yamura Motors LLC Recording Pyrometer", textPosition[0], textPosition[1], GFXFF);
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

  // RTC setup
  #ifdef HAS_RTC
  while (!RTC_Setup()) 
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
  RTC_SetDateTime(DateTime(F(__DATE__), F(__TIME__)));
  #endif
  String tmpTimeStr = "RTC OK ";
  tmpTimeStr += RTC_GetStringTime();
  tmpTimeStr += " ";
  tmpTimeStr += RTC_GetStringDate();
  tftDisplay.drawString(tmpTimeStr.c_str(), textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #endif


  // thermocouple amp setup
  Thermo_Setup();

  // user buttons setup
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
  ListDirectory(LittleFS, "/", 3);
  #endif

  #ifdef DEBUG_VERBOSE
  Serial.println( "initializing microSD" );
  #endif
  tftDisplay.drawString("Initializing SD card", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;

  int failCount = 0;
  while(!SD.begin(SD_CS))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("microSD card mount failed");
    #endif
    failCount++;
    sprintf(outStr, "microSD card mount failed after %d attempts", failCount);
    tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
    if(failCount > 100)
    {
      while(true) { }
    }
    delay(1000);
  }
  tftDisplay.drawString("SD initialized", textPosition[0], textPosition[1], GFXFF);
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
  ListDirectory(SD, "/", 3);
  #endif

  tftDisplay.drawString("Read device setup", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  ReadDeviceSetupFile(SD,  "/py_set.txt");
  WriteDeviceSetupHTML(LittleFS, "/py_set.html");
  WriteDeviceSetupHTML(SD, "/py_set.html");

  tftDisplay.drawString("Read cars setup", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  ReadCarSetupFile(SD,  "/py_cars.txt");
  WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
  WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);

  #ifdef HAS_RTC
  // get time from RTC
  sprintf(outStr, "%s %s", RTC_GetStringTime().c_str(), RTC_GetStringDate().c_str());
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  #endif
  deviceState = DISPLAY_MENU;

  tftDisplay.drawString("Write results to HTML", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  WriteResultsHTML(LittleFS);
  WriteResultsHTML(SD);

  WiFi.softAP(deviceSettings.ssid, deviceSettings.pass);
  IP = WiFi.softAPIP();
  // Web Server Root URL
  Serial.println("starting webserver");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("HTTP_GET, send /py_main.html from LittleFS");
    #endif
    request->send(LittleFS, "/py_main.html", "text/html");
  });
  
  server.serveStatic("/", LittleFS, "/");
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
      // true for C, false for F
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
      // screenRotation  0 = R, 1 = L
      if (strcmp(p->name().c_str(), "orientation_id") == 0)
      {
        tempDevice.screenRotation = 1;
        if(strcmp(p->value().c_str(), "R") == 0)
        {
          tempDevice.screenRotation = 0;
        }
        continue;
      }
      // is12Hour true for 12 hour clock, false for 24 hour clock
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
          WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
          WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);
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
          cars[carCount - 1].carID = maxCarID;
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
          WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
          WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);
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
          WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
          WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);
        }
        // buttons
        if (strcmp(p->name().c_str(), "next") == 0)
        {
          carSetupIdx = carSetupIdx + 1 < carCount ? carSetupIdx + 1 : carCount - 1;
          WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
          WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);
          request->send(LittleFS, "/py_cars.html", "text/html");
        }
        if (strcmp(p->name().c_str(), "prior") == 0)
        {
          carSetupIdx = carSetupIdx - 1 >= 0 ? carSetupIdx - 1 : 0;
          WriteCarSetupHTML(LittleFS, "/py_cars.html", carSetupIdx);
          WriteCarSetupHTML(SD, "/py_cars.html", carSetupIdx);
          request->send(LittleFS, "/py_cars.html", "text/html");
        }
        request->send(LittleFS, "/py_cars.html", "text/html");
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
          WriteDeviceSetupHTML(LittleFS, "/py_set.html");
          WriteDeviceSetupHTML(SD, "/py_set.html");
          request->send(LittleFS, "/py_set.html", "text/html");
        }
      }
      else
      {
        request->send(LittleFS, "/py_main.html", "text/html");
      }
    }
  });
  server.begin();
  
  sprintf(outStr, "IP %d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  sprintf(outStr, "Password %s", deviceSettings.pass);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  SetupTireMeasureGrid(deviceSettings.fontPoints);

  delay(5000);
  RotateDisplay(deviceSettings.screenRotation != 0);
}
//
// display menu associated with current device state
//
void loop()
{
  switch (deviceState)
  {
    case DISPLAY_MENU:
      DisplayMainMenu();
      break;
    case SELECT_CAR:
      SelectCarMenu();
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
      DisplaySelectedResultsMenu(SD, outStr);
      deviceState = DISPLAY_MENU;
      break;
    case CHANGE_SETTINGS:
      ChangeSettingsMenu();
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

// USER INTERFACE FUNCTIONS

//
// main menu - car select, start measurement, etc
//
void DisplayMainMenu()
{
  int menuCount = 6;
  MenuChoice mainMenuChoices[6];  
  mainMenuChoices[0].description = "Measure Temps";                   mainMenuChoices[0].result = MEASURE_TIRES;
  mainMenuChoices[1].description = cars[selectedCar].carName; mainMenuChoices[1].result = SELECT_CAR;
  mainMenuChoices[2].description = "Display Temps";                   mainMenuChoices[2].result = DISPLAY_TIRES;
  mainMenuChoices[3].description = "Instant Temp";                    mainMenuChoices[3].result = INSTANT_TEMP;
  mainMenuChoices[4].description = "Display Selected Results";        mainMenuChoices[4].result = DISPLAY_SELECTED_RESULT;
  mainMenuChoices[5].description = "Settings";                        mainMenuChoices[5].result = CHANGE_SETTINGS;
  
  deviceState = MenuSelect(deviceSettings.fontPoints, mainMenuChoices, menuCount, MEASURE_TIRES); 
}
//
// select car/driver for measurement
//
void SelectCarMenu()
{
  MenuChoice* carsMenu = (MenuChoice*)calloc(carCount, sizeof(MenuChoice));
  for(int idx = 0; idx < carCount; idx++)
  {
    carsMenu[idx].description = cars[idx].carName;
    carsMenu[idx].description += " (ID: ";
    carsMenu[idx].description += cars[idx].carID;
    carsMenu[idx].description += ")";
    carsMenu[idx].result = idx; 
  }
  selectedCar = MenuSelect(deviceSettings.fontPoints, carsMenu, carCount, 0); 
  free(carsMenu);
}
//
// display all results for a selected car
//
void DisplaySelectedResultsMenu(fs::FS &fs, const char * path)
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
  ReadMeasurementFile(buf, currentResultCar);
  DisplayAllTireTemps(currentResultCar);
}
//
// change device settings menu
//
void ChangeSettingsMenu()
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
    else if(deviceSettings.screenRotation == 0)
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
    result = MenuSelect(deviceSettings.fontPoints, settingsChoices, menuCount, 0); 
    switch(result)
    {
      case SET_DATETIME:
        SetDateTime();
        break;
      case SET_TEMPUNITS:
        SetUnitsMenu();
        break;
      case SET_DELETEDATA:
        DeleteDataFilesMenu();
        break;
      case SET_FLIPDISPLAY:
        deviceSettings.screenRotation = deviceSettings.screenRotation == 0 ? 1 : 0;
        RotateDisplay(true);
        break;
      case SET_FONTSIZE:
        SelectFontSizeMenu();
        break;
      case SET_12H24H:
        Select12or24Menu();
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
// select 12 or 24 hour time display
//
void Select12or24Menu()
{
  MenuChoice hour12_24Choices[2];
  hour12_24Choices[HOURS_12].description  = "12 Hour clock";  hour12_24Choices[HOURS_12].result = HOURS_12;
  hour12_24Choices[HOURS_24].description = "24 Hour clock"; hour12_24Choices[HOURS_24].result = HOURS_24;
  int result = MenuSelect(deviceSettings.fontPoints, hour12_24Choices, 2, 0); 
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
// select display font size
//
void SelectFontSizeMenu()
{
  MenuChoice fontSizeChoices[4];
  fontSizeChoices[FONTSIZE_9].description  = "9 point";  fontSizeChoices[FONTSIZE_9].result = FONTSIZE_9;
  fontSizeChoices[FONTSIZE_12].description = "12 point"; fontSizeChoices[FONTSIZE_12].result = FONTSIZE_12;
  fontSizeChoices[FONTSIZE_18].description = "18 point"; fontSizeChoices[FONTSIZE_18].result = FONTSIZE_18;
  fontSizeChoices[FONTSIZE_24].description = "24 point"; fontSizeChoices[FONTSIZE_24].result = FONTSIZE_24;
  int result = MenuSelect(deviceSettings.fontPoints, fontSizeChoices, 4, 0); 
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
// select temperature units
//
void SetUnitsMenu()
{
  int menuCount = 2;
  MenuChoice unitsChoices[2];

  unitsChoices[0].description = "Temp in F";   unitsChoices[0].result = 0;
  unitsChoices[1].description = "Temp in C";   unitsChoices[1].result = 1;
  int menuResult = MenuSelect(deviceSettings.fontPoints, unitsChoices, menuCount, 0); 
  // true for C, false for F
  deviceSettings.tempUnits = menuResult == 1;
}
//
// delete data files menu (Yes or No)
//
void DeleteDataFilesMenu(bool verify)
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
    WriteResultsHTML(LittleFS);
  }
}
//
// return next state as selection from choices array
// initialSelect defines the item in choices selected at the start of user input
//
int MenuSelect(int fontSize, MenuChoice choices[], int menuCount, int initialSelect)
{
  char outStr[256];
  // location of text
  textPosition[0] = 5;
  textPosition[1] = 0;
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
// user set date/time
//
void SetDateTime()
{
  char outStr[256];
  int timeVals[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // date, month, year, day, hour, min, sec, 100ths
  // location of text
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
  DateTime now;
  now = RTC_GetDateTime();
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

  RTC_SetDateTime(timeVals[YEAR], timeVals[MONTH], timeVals[DATE], timeVals[HOUR],timeVals[MINUTE],timeVals[SECOND]);
  textPosition[1] += fontHeight * 2;
  sprintf(outStr,"Set to %s %s", RTC_GetStringTime(), RTC_GetStringDate());
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  SetFont(deviceSettings.fontPoints);
  delay(5000);
}

// READ WRITE CREATE HTML SETUP FILES

//
// read car settings file
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
    // read ID
    ReadLine(file, buf);
    cars[carIdx].carID = atoi(buf);
    maxCarID = maxCarID > cars[carIdx].carID ? maxCarID : cars[carIdx].carID;
    // read name
    ReadLine(file, buf);
    strcpy(cars[carIdx].carName, buf);
    // read tire count and create arrays
    ReadLine(file, buf);
    cars[carIdx].tireCount = atoi(buf);
    maxTires = maxTires > cars[carIdx].tireCount ? maxTires : cars[carIdx].tireCount;
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
// write car settings file
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
    maxCarID = maxCarID > cars[carIdx].carID ? maxCarID : cars[carIdx].carID;
    file.println(cars[carIdx].carID);    // carID
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
// write car settings to HTML for web interface
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
      sprintf(buf, "<div>");
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
  file.println("</table>");
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
// read device settings file
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
// write device settings file
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
// write device settings to HTML for web interface
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
// write all results to HTML file for web interface
//
void WriteResultsHTML(fs::FS &fs)
{
  char buf[512];
  char outStr[512];
  char tmpStr[512];
  char nameBuf[128];
  CarSettings currentResultCar;
  File fileIn;   // data source file
  File fileOut;  // html results file
  // create a new HTML file
  #ifdef DEBUG_VERBOSE
  Serial.println("py_res.html header");
  #endif
  DeleteFile(fs, "/py_res.html");
  fileOut = fs.open("/py_res.html", FILE_WRITE);
  if(!fileOut)
  {
    Serial.println("failed to open data HTML file /py_res.html");
    return;
  }

  fileOut.println("<!DOCTYPE html>");
  fileOut.println("<html>");
  fileOut.println("<head>");
  fileOut.println("<title>Recording Pyrometer</title>");
  fileOut.println("</head>");
  fileOut.println("<body>");
  fileOut.println("<h1>Recorded Results</h1>");
  fileOut.println("<p>");
  fileOut.println("<table border=\"1\">");
  fileOut.println("<tr>");
  fileOut.println("<th>Date/Time</th>");
  fileOut.println("<th>Car/Driver</th>");
  fileOut.println("</tr>");
  float tireMin =  999.9;
  float tireMax = -999.9;
  int rowCount = 0;
  bool outputSubHeader = true;
  for (int dataFileCount = 0; dataFileCount < 100; dataFileCount++)
  {
    sprintf(nameBuf, "/py_temps_%d.txt", dataFileCount);
    fileIn = SD.open(nameBuf, FILE_READ);
    if(!fileIn)
    {
      continue;
    }
    outputSubHeader = true;
    while(true)
    {
      ReadLine(fileIn, buf);
      // end of file
      if(strlen(buf) == 0)
      {
        fileIn.close();
        break;
      }
      ReadMeasurementFile(buf, currentResultCar);
      if(outputSubHeader)
      {
        fileOut.println("<tr>");
        fileOut.println("<td></td>");
        fileOut.println("<td></td>");
        for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
        {
          for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
          {
            sprintf(buf, "<td>%s-%s</td>", currentResultCar.tireShortName[t_idx], currentResultCar.positionShortName[p_idx]);
            fileOut.println(buf);
          }
        }
        fileOut.println("</tr>");
      }
      outputSubHeader = false;
      rowCount++;
      fileOut.println("<tr>");
      sprintf(buf, "<td>%s</td>", currentResultCar.dateTime);
      fileOut.println(buf);
      sprintf(buf, "<td>%s</td>", currentResultCar.carName);
      fileOut.println(buf);
      for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
      {
        tireMin =  999.9;
        tireMax = -999.9;
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
            sprintf(buf, "<td bgcolor=\"red\">%0.2f</td>", tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else if(tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] <= tireMin)
          {
            sprintf(buf, "<td bgcolor=\"cyan\">%0.2f</td>", tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else if (tireTemps[(t_idx * currentResultCar.positionCount) + p_idx] == tireMax)
          {
            sprintf(buf, "<td bgcolor=\"yellow\">%0.2f</td>", tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          else
          {
            sprintf(buf, "<td>%0.2f</td>", tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          }
          fileOut.println(buf);
        }
      }
      fileOut.println("</tr>");
    }
    fileIn.close();
  }
  if(rowCount == 0)
  {
    rowCount++;
    fileOut.println("<tr>");
    sprintf(buf, "<td>---</td>");
    fileOut.println(buf);
    sprintf(buf, "<td>---</td>");
    fileOut.println(buf);
    for(int t_idx = 0; t_idx < 4; t_idx++)
    {
      tireMin =  999.9;
      tireMax = -999.9;
      sprintf(buf, "<td>---</td>");
      fileOut.println(buf);
      // add cells to file
      for(int p_idx = 0; p_idx < 3; p_idx++)
      {
        sprintf(buf, "<td>---</td>");
        fileOut.println(buf);
      }
    }
    fileOut.println("</tr>");
  }
  fileOut.println("</table>");
  fileOut.println("</p>");
  fileOut.println("<p>");
  fileOut.println("<button name=\"home\" type=\"submit\" value=\"home\"><a href=\"/py_main.html\">Home</a></button>");
  fileOut.println("</p>");
  // add copy button, raw results text and script
  fileOut.println("<p>");
  fileOut.println("<button onclick=\"copyResults()\">Copy data</button>");
  fileOut.println("</p>");
  fileOut.println("<p>");
  fileOut.println("<textarea id=\"rawTextResultsID\" rows=\"20\" cols=\"100\">");
  rowCount = 0;
  for (int dataFileCount = 0; dataFileCount < 100; dataFileCount++)
  {
    sprintf(nameBuf, "/py_temps_%d.txt", dataFileCount);
    fileIn = SD.open(nameBuf, FILE_READ);
    if(!fileIn)
    {
      continue;
    }
    outputSubHeader = true;
    while(true)
    {
      ReadLine(fileIn, buf);
      // end of file
      if(strlen(buf) == 0)
      {
        fileIn.close();
        break;
      }
      ReadMeasurementFile(buf, currentResultCar);
      if(outputSubHeader)
      {
        sprintf(outStr, "\t%s", currentResultCar.carName);
        for(int tireIdx = 0; tireIdx < currentResultCar.tireCount; tireIdx++)
        {
          for(int posIdx = 0; posIdx < currentResultCar.positionCount; posIdx++)
          {
            sprintf(tmpStr, "\t%s-%s", currentResultCar.tireShortName[tireIdx], currentResultCar.positionShortName[posIdx]);
            strcat(outStr, tmpStr);
          }
        }
        fileOut.println(outStr);
        outputSubHeader = false;
      }
      sprintf(outStr, "%s\t%s", currentResultCar.dateTime, currentResultCar.carName);
      for(int t_idx = 0; t_idx < currentResultCar.tireCount; t_idx++)
      {
        // add cells to file
        for(int p_idx = 0; p_idx < currentResultCar.positionCount; p_idx++)
        {
          sprintf(tmpStr, "\t%lf", tireTemps[(t_idx * currentResultCar.positionCount) + p_idx]);
          strcat(outStr, tmpStr);
        }
      }
      fileOut.println(outStr);
      rowCount++;
    }
    fileIn.close();
  }
  fileOut.println("</textarea>");
  fileOut.println("</p>");
  fileOut.println("</body>");
  fileOut.println("<script>");
  fileOut.println("function copyResults() {");
  fileOut.println("var copyText = document.getElementById(\"rawTextResultsID\");");
  fileOut.println("copyText.select();");
  fileOut.println("copyText.setSelectionRange(0, 99999);");
  fileOut.println("navigator.clipboard.writeText(copyText.value);");
  fileOut.println("}");
  fileOut.println("</script>");
  fileOut.println("</html>");
  fileOut.close();

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
// write current measurement to by car results file
// (easier than trying to sore the results while writing the HTML....)
//
void WriteMeasurementFile()
{
  char outStr[255];
  #ifdef HAS_RTC
  String curTimeStr;
  curTimeStr = RTC_GetStringTime();
  strcpy(cars[selectedCar].dateTime, curTimeStr.c_str());
  curTimeStr += " ";
  curTimeStr += RTC_GetStringDate();
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
  // find file to write
  String fileLine = outStr;
  for(int idxTire = 0; idxTire < cars[selectedCar].tireCount; idxTire++)
  {
    for(int idxPosition = 0; idxPosition < cars[selectedCar].positionCount; idxPosition++)
    {
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
  sprintf(outStr, "/py_temps_%d.txt", cars[selectedCar].carID);
  AppendFile(SD, outStr, fileLine.c_str());
}
//
// parse measurement file (single read of all tires/positions)
//
void ReadMeasurementFile(char buf[], CarSettings &currentResultCar)
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
    }
    // 3 - position count
    else if(tokenIdx == 3)
    {
      currentResultCar.positionCount = atoi(token);
      tempCnt = currentResultCar.tireCount * currentResultCar.positionCount;
      measureRange[0]  = tokenIdx + 1;  // measurements start at next token
      measureRange[1]  = measureRange[0] +  (currentResultCar.tireCount *  currentResultCar.positionCount) - 1;
      tireNameRange[0] = measureRange[1];                                                                     
      tireNameRange[1] = tireNameRange[0] + currentResultCar.tireCount;                                       
      posNameRange[0]  = tireNameRange[1];                                                                    
      posNameRange[1]  = posNameRange[0] + currentResultCar.positionCount;                                    
      maxTempRange[0]  = posNameRange[1];                                                                    
      maxTempRange[1]  = maxTempRange[0] + currentResultCar.tireCount;                                   
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

// TIRE TEMPERATURE MEASUREMENT AND DISPLAY

//
// measure temperatures on a single tire
//
int MeasureTireTemps(int tireIdx)
{
  char outStr[512];
  // location of text
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
  armed = false;
  unsigned long priorTime = millis();
  unsigned long curTime = millis();
  // text position on screen
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
    if(!armed)
    {
      curTime = millis();
      continue;
    }
    // get stable temp after arming
    row = ((tireIdx / 2) * 2) + 1;
    col = measIdx + ((tireIdx % 2) * 3);
    tireTemps[(tireIdx * cars[selectedCar].positionCount) + measIdx] = GetStableTemp(row, col);
    // disarm after stable temp
    armed = false;
    // next position
    measIdx++;
    drawStars = true;
  }
  return 1;
}
//
// display current probe temp until user cancels
//
void InstantTemp()
{
  unsigned long priorTime = 0;
  unsigned long curTime = millis();
  char outStr[128];
  float instant_temp = 0.0;
  // location of text
  textPosition[0] = 5;
  textPosition[1] = 0;
  randomSeed(100);
  tftDisplay.fillScreen(TFT_BLACK);
  YamuraBanner();
  SetFont(deviceSettings.fontPoints);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  sprintf(outStr, "Temperature at %s %s", RTC_GetStringTime(), RTC_GetStringDate());
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
      sprintf(outStr, "Temperature at %s %s", RTC_GetStringTime(), RTC_GetStringDate());
      tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
      textPosition[1] +=  fontHeight;
      sprintf(outStr, " ");
      SetFont(24);
      textPosition[0] = tftDisplay.width()/2;
      textPosition[1] = tftDisplay.height()/2;
      tftDisplay.setTextDatum(TC_DATUM);

      priorTime = curTime;
      // read temp
      instant_temp = Thermo_GetTemp();
      if(deviceSettings.tempUnits == 1)
      {
        instant_temp = FtoCAbsolute(instant_temp);
      }
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
// draw grid for tire measurement (set up in SetupTireMeasureGrid)
//
void DrawTireMeasureGrid(int tireCount)
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
// set up grid lines for tire measurement based on tire and position counts and font size
//
void SetupTireMeasureGrid(int fontHeight)
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
//
// show all values for last measurement
//
void DisplayAllTireTemps(CarSettings currentResultCar)
{
  unsigned long curTime = millis();
  unsigned long priorTime = millis();
  // location of text
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
  DrawTireMeasureGrid(currentResultCar.tireCount);

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
// measure all tires/positions until Done selected
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
  DrawTireMeasureGrid(cars[selectedCar].tireCount);
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
  SetFont(deviceSettings.fontPoints);
  tftDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
  // location of text
  textPosition[0] = 5;
  textPosition[1] = 0;
  tftDisplay.drawString("Done", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  tftDisplay.drawString("Storing results...", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  WriteMeasurementFile();
  tftDisplay.drawString("Updating results HTML...", textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;
  WriteResultsHTML(LittleFS);  
}
//
// move to next tire after measurement of a tire is complete, 
// or if user selects next or prior tire in list
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
// rotate display for right or left hand use
//
void RotateDisplay(bool rotateButtons)
{
  tftDisplay.setRotation(deviceSettings.screenRotation == 0 ? 1 : 3);
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
// wait until temperature at probe stabilizes
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
      devRange[idx] = FtoCRelative(devRange[idx]);
    }
  }

  for(int idx = 0; idx < TEMP_BUFFER; idx++)
  {
    tempValues[idx] = -100.0;
  }
  while(true)
  {
    temperature = Thermo_GetTemp();
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
// draw the Yamura banner at bottom of screen
//
void YamuraBanner()
{
  tftDisplay.setTextColor(TFT_BLACK, TFT_YELLOW);
  SetFont(9);
  int xPos = tftDisplay.width()/2;
  int yPos = tftDisplay.height() - fontHeight/2;
  tftDisplay.setTextDatum(BC_DATUM);
  tftDisplay.drawString("  Yamura Motors LLC Recording Pyrometer  ",xPos, yPos, GFXFF);    // Print the font name onto the TFT screen
  tftDisplay.setTextDatum(TL_DATUM);
}
//
// set font for TFT display, update fontHeight used for vertical stepdown by line
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
// COMMON FUNCTION DEFINITIONS
// these functions could be used as is in other applications
// or at least are not specific to pyrometer functions
//

//
// convert temperature from C to F
//
float CtoFAbsolute(float tempC)
{
  return (tempC * 1.8) + 32; 
}
//
// convert temperature difference from C to F
//
float CtoFRelative(float tempC)
{
  return (tempC * 1.8); 
}
//
// convert temperature from C to F
//
float FtoCAbsolute(float tempF)
{
  return (tempF - 32) / 1.8; 
}
//
// convert temperature difference from C to F
//
float FtoCRelative(float tempF)
{
  return tempF / 1.8; 
}
//
// returns true if time is PM (for 12 hour display)
// requires RTC
//
bool RTC_IsPM()
{
  DateTime now;
  now = RTC_GetDateTime();
  return now.isPM();
}
//
// get HH:MM:SS from RTC time
// requires RTC
//
String RTC_GetStringTime()
{
  bool h12Flag;
  bool pmFlag;
  String rVal;
  char buf[512];
  int ampm;
  DateTime now;
  now = RTC_GetDateTime();
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
// get MM/DD/YYYY from RTC date
// requires RTC
//
String RTC_GetStringDate()
{
  bool century = false;
  String rVal;
  char buf[512];
	DateTime now;
  now = RTC_GetDateTime();
  int year = now.year();
  int month = now.month();
  int day = now.day();
  sprintf(buf, "%02d/%02d/%02d", month, day, year);
  rVal = buf;
  return rVal;
}
//
// Set date time
// pass in a year, month, date, hour, minute, second as integer values
//
void RTC_SetDateTime(int year, int month, int date, int hour, int minute, int second)
{
  rtc.adjust(DateTime(year, month, date, hour, minute, second));
}
//
// Set date time
// pass in a DateTime structure
//
void RTC_SetDateTime(DateTime timeVal)
{
  rtc.adjust(timeVal);
}
//
// get time from RTC
// make it easier to change RTC modules if libraries need
// different calls
// return as a DateTime structure
//
DateTime RTC_GetDateTime()
{
  return rtc.now();
}
//
// any initialization required for RTC module, return TRUE for sucess, FALSE for fail
//
bool RTC_Setup()
{
  bool rVal = rtc.begin();
  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  #ifdef RTC_8563
  if(rVal) 
  {
    rtc.start();
  };
  #endif
  return rVal;
}
//
//
//
float Thermo_GetTemp()
{
  //float temperature = tempSensor.getThermocoupleTemp();
  float temperature = tempSensor.readThermocouple();
  if (isnan(temperature)) 
  {
    return -100.0F;
  }
  // convert to F if required
  if(deviceSettings.tempUnits == 0)
  {
    temperature = CtoFAbsolute(temperature);
  }
  return temperature;
}
//
//
//
void Thermo_Setup()
{
  char outStr[256];
  if(tempSensor.begin(I2C_ADDRESS_THERMO))
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple acknowledged");
    #endif
    tftDisplay.drawString("Thermocouple acknowledged", textPosition[0], textPosition[1], GFXFF);
    textPosition[1] += fontHeight;
  }
  else 
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("Thermocouple did not acknowledge");
    #endif
    tftDisplay.drawString("Thermocouple did not acknowledge", textPosition[0], textPosition[1], GFXFF);
    textPosition[1] += fontHeight;
    while(1); //hang forever
  }
  tempSensor.setADCresolution(MCP9600_ADCRESOLUTION_18);
  Serial.print("ADC resolution set to ");
  switch (tempSensor.getADCresolution()) 
  {
    case MCP9600_ADCRESOLUTION_18:   
      Serial.print("18"); 
      break;
    case MCP9600_ADCRESOLUTION_16:   
      Serial.print("16"); 
      break;
    case MCP9600_ADCRESOLUTION_14:   
      Serial.print("14"); 
      break;
    case MCP9600_ADCRESOLUTION_12:   
      Serial.print("12"); 
      break;
  }
  Serial.println(" bits");
  //change the thermocouple type being used
  Serial.println("Setting Thermocouple Type!");
  tempSensor.setThermocoupleType(MCP9600_TYPE_K);
   //make sure the type was set correctly!
  switch(tempSensor.getThermocoupleType())
  {
    case MCP9600_TYPE_K:
      sprintf(outStr,"Type K ");
      break;
    case MCP9600_TYPE_J:
      sprintf(outStr,"Thermocouple set failed (Type J");
      break;
    case MCP9600_TYPE_T:
      sprintf(outStr,"Thermocouple set failed (Type T");
      break;
    case MCP9600_TYPE_N:
      sprintf(outStr,"Thermocouple set failed (Type N");
      break;
    case MCP9600_TYPE_S:
      sprintf(outStr,"Thermocouple set failed (Type S");
      break;
    case MCP9600_TYPE_E:
      sprintf(outStr,"Thermocouple set failed (Type E");
      break;
    case MCP9600_TYPE_B:
      sprintf(outStr,"Thermocouple set failed (Type B");
      break;
    case MCP9600_TYPE_R:
      sprintf(outStr,"Thermocouple set failed (Type R");
      break;
    default:
      sprintf(outStr,"Thermocouple set failed (unknown type");
      break;
  }
  Serial.println(outStr);
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;

  tempSensor.setFilterCoefficient(3);
  Serial.print("Thermocouple filter coefficient value set to: ");
  Serial.println(tempSensor.getFilterCoefficient());


  sprintf(outStr,"Temp: C: %0.2FC/%0.2FF H: %0.2FC/%0.2FF",tempSensor.readAmbient(), CtoFAbsolute(tempSensor.readAmbient()),
                                                           tempSensor.readThermocouple(), CtoFAbsolute(tempSensor.readThermocouple()));
  tftDisplay.drawString(outStr, textPosition[0], textPosition[1], GFXFF);
  textPosition[1] += fontHeight;


  for(int idx = 0; idx < TEMP_BUFFER; idx++)
  {
    tempValues[idx] = -100.0;
  }
}
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
//
// read a line from an open file
// requires a file system of some kind = LittleFS or SD
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
// append to a file - this function opens, writes line and closes the file
// requires a file system of some kind = LittleFS or SD
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
// delete a file
// requires a file system of some kind = LittleFS or SD
//
void DeleteFile(fs::FS &fs, const char * path)
{
  fs.remove(path);
}
//
// list files in folder
// requires a file system of some kind = LittleFS or SD
//

void ListDirectory(fs::FS &fs, const char * dirname, uint8_t levels)
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
                ListDirectory(fs, file.path(), levels -1);
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