#include <OneWire.h>
#include <DallasTemperature.h>

// basic
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <ESP8266httpUpdate.h>

#include <ArduinoJson.h>
#include <FS.h>


// OLED screen
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>



// firmware version
#define SOFT_NAME "bicoqueTemperature"
#define SOFT_VERSION "0.1.01"
#define SOFT_DATE "2020-04-19"

#define DEBUG 1

//-- OneWire Configuration
// GPIO where the DS18B20 is connected to
const int oneWireBus = D5;     
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);


//-- Screen Init
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// Init
float temperatureOld = -255 ;
float temperature;
float temperatureAdjustement = 0.2;
int tempTimer = 0;
IPAddress ip;
String dataJsonData;
DynamicJsonDocument jsonData(200);
// Time
long timeAtStarting = 0; // need to get from ntp server timestamp when ESP start


// Wifi AP info for configuration
const char* wifiApSsid = SOFT_NAME;
const char* wifiApPasswd = "randomPass";
String wifiSsid   = "";
String wifiPasswd = "";


// Update info
#define BASE_URL "http://mangue.net/ota/esp/bicoqueTemperature/"
#define UPDATE_URL "http://mangue.net/ota/esp/bicoqueTemperature/update.php"


// Web server info
ESP8266WebServer server(80);


// config default
typedef struct configWifi 
{
  String ssid;
  String password;
  boolean enable;
};
typedef struct configTemp
{
  float adjustment;
  int checkTimer;
};
typedef struct config
{
  configWifi wifi;
  configTemp temp;
  boolean alreadyStart;
  String softName;
};
config softConfig;







//----------------
//
//
// Functions
//
//
//----------------
// ********************************************
// SPIFFFS storage Functions
// ********************************************
String storageRead(char *fileName)
{
  String dataText;

  File file = SPIFFS.open(fileName, "r");
  if (!file)
  {
    logger("FS: opening file error");
    //-- debug
  }
  else
  {
    size_t sizeFile = file.size();
    if (sizeFile > 400)
    {
      Serial.println("Size of file is too clarge");
    }
    else
    {
      dataText = file.readString();
      file.close();
    }
  }

  return dataText;
}

bool storageWrite(char *fileName, String dataText)
{
  File file = SPIFFS.open(fileName, "w");
  file.println(dataText);

  file.close();

  return true;
}


void dataSave()
{
  dataJsonData = "";
  serializeJson(jsonData, dataJsonData);

  if (DEBUG) {
    Serial.print("write consumption : ");
    Serial.println(dataJsonData);
  }

  storageWrite("/data.json", dataJsonData);
}

String configSerialize()
{
  String dataJsonConfig;
  DynamicJsonDocument jsonConfig(800);
  JsonObject jsonConfigWifi = jsonConfig.createNestedObject("wifi");
  JsonObject jsonConfigTemp = jsonConfig.createNestedObject("temp");
  
  jsonConfig["alreadyStart"]   = softConfig.alreadyStart;
  jsonConfig["softName"]       = softConfig.softName;
  jsonConfigWifi["ssid"]       = softConfig.wifi.ssid;
  jsonConfigWifi["password"]   = softConfig.wifi.password;
  jsonConfigWifi["enable"]     = softConfig.wifi.enable;
  jsonConfigTemp["adjustment"] = softConfig.temp.adjustment;
  jsonConfigTemp["checkTimer"] = softConfig.temp.checkTimer;
  serializeJson(jsonConfig, dataJsonConfig);

  return dataJsonConfig;
}


bool configSave()
{
  String dataJsonConfig = configSerialize();
  bool fnret = storageWrite("/config.json", dataJsonConfig);
  
  if (DEBUG) 
  {
    Serial.print("write config : ");
    Serial.println(dataJsonConfig);
    Serial.print("Return of write : "); Serial.println(fnret);
  }

  return fnret;
}

bool configRead(config &ConfigTemp)
{
  String dataJsonConfig;
  DynamicJsonDocument jsonConfig(800);
  
  dataJsonConfig = storageRead("/config.json");
  
  DeserializationError jsonError = deserializeJson(jsonConfig, dataJsonConfig);
  if (jsonError)
  {
    Serial.println("Got Error when deserialization : "); //Serial.println(jsonError);
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(jsonError.c_str());
    return false;
    // Getting error when deserialise... I don know what to do here...
  }

  ConfigTemp.alreadyStart    = jsonConfig["alreadyStart"];
  ConfigTemp.softName        = jsonConfig["softName"].as<String>();
  ConfigTemp.wifi.ssid       = jsonConfig["wifi"]["ssid"].as<String>();
  ConfigTemp.wifi.password   = jsonConfig["wifi"]["password"].as<String>();
  ConfigTemp.wifi.enable     = jsonConfig["wifi"]["enable"];
  ConfigTemp.temp.adjustment = jsonConfig["temp"]["adjustment"];
  ConfigTemp.temp.checkTimer = jsonConfig["temp"]["checkTimer"];

  return true;
  
}

void configDump(config ConfigTemp)
{
  Serial.println("wifi data :");
  Serial.print("  - ssid : "); Serial.println(ConfigTemp.wifi.ssid);
  Serial.print("  - password : "); Serial.println(ConfigTemp.wifi.password);
  Serial.print("  - enable : "); Serial.println(ConfigTemp.wifi.enable);
  Serial.println("Temp data :");
  Serial.print("  - adjustment : "); Serial.println(ConfigTemp.temp.adjustment);
  Serial.print("  - checkTimer : "); Serial.println(ConfigTemp.temp.checkTimer);
  Serial.println("General data :");
  Serial.print("  - alreadyStart : "); Serial.println(ConfigTemp.alreadyStart);
  Serial.print("  - softName : "); Serial.println(ConfigTemp.softName);
}



// ********************************************
// Time and Stats Functions
// ********************************************
long getTime()
{
  // get mili from begining
  long timeInSec = timeAtStarting + millis() / 1000; // work in sec instead of milisec

  return timeInSec;
}

long getTimeOnStartup()
{
  // send NTP request. or get a webpage with a timestamp

  HTTPClient httpClient;
  httpClient.begin("http://mangue.net/ota/esp/bicoqueEvse/timestamp.php");
  int httpAnswerCode = httpClient.GET();
  String timestamp;
  if (httpAnswerCode > 0)
  {
    timestamp = httpClient.getString();
  }
  httpClient.end();

  // global variable
  timeAtStarting = timestamp.toInt() - (millis() / 1000);

}



// ********************************************
// WebServer
// ********************************************
void webRoot() {
  String message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += SOFT_NAME;
  message += " v";
  message += SOFT_VERSION;
  message += " <a href='/reload'>reload</a> ";
  message += "<br><br>";

  message += "Temperature : "; message += temperature; message += " °C<br>";
  message += "Adjustment : "; message += softConfig.temp.adjustment; message += " °C<br>";
  message += "Adjustment : "; message += softConfig.temp.checkTimer; message += " sec<br>";
  message += "<br><br>";

  message += "Wifi power : "; message += WiFi.RSSI(); message += "<br>";
  message += "Wifi power : "; message += wifiPower(); message += "<br>";
  message += "<br>";

  message += "</html>";
  server.send(200, "text/html", message);
}
void webDebug()
{
  String message = "<!DOCTYPE HTML>";
  message += "<html>";
  message += SOFT_NAME; message += " - debug -<a href='/reload'>reload</a> ";
  message += "<br><br>\n";
  message += "<form action='/write' method='GET'>Adjustment  : <input type=text name=adjustment><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>Check Timer : <input type=text name=checkTimer><input type=submit></form><br>\n";
  message += "<br><br>\n";
  message += "<a href='/reboot'>Rebbot device</a><br>\n";
  message += "</html>\n";
  server.send(200, "text/html", message);
}
void webReboot()
{
  String message = "<!DOCTYPE HTML>";
  message += "<html>";
  message += "Rebbot in progress...<b>";
  message += "</html>\n";
  server.send(200, "text/html", message);

  ESP.restart();
}
void webApiConfig()
{
  String dataJsonConfig = configSerialize();
  server.send(200, "application/json", dataJsonConfig);

}
void webNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " Name: " + server.argName(i) + " - Value: " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
void webReload()
{
  webRoot();
}

void webWrite()
{
  String message;

  String adjustment  = server.arg("adjustment");
  String checkTimer  = server.arg("checkTimer");
  String wifiEnable  = server.arg("wifienable");
  String alreadyBoot = server.arg("alreadyboot");

  
  if ( alreadyBoot != "")
  {
    softConfig.alreadyStart = false;
    if ( alreadyBoot == "1") { softConfig.alreadyStart = true; }
    configSave();
  }
  if ( wifiEnable != "")
  {
    softConfig.wifi.enable = false;
    if ( wifiEnable == "1") { softConfig.wifi.enable = true; }
    configSave();
  }
  if (adjustment != "")
  {
    softConfig.temp.adjustment = adjustment.toFloat();
    configSave();
  }
  if (checkTimer != "")
  {
    softConfig.temp.checkTimer = checkTimer.toInt();
    configSave();
  }

  Serial.println("Write done");
  message += "Write done...\n";
  server.send(200, "text/plain", message);
}
void webInitRoot()
{
  String wifiList = wifiScan();
  String message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += SOFT_NAME;
  message += "<br><br>\n";

  message += "Please configure your Wifi : <br>\n";
  message += "<p>\n";
  message += wifiList;
  message += "</p>\n<form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>\n";
  message += "</html>\n";
  server.send(200, "text/html", message);
}
void webInitSetting()
{
  String content;
  int statusCode;
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");
  if (qsid.length() > 0 && qpass.length() > 0)
  {
    Serial.print("Debug : qsid : ");Serial.println(qsid);
    Serial.print("Debug : qpass : ");Serial.println(qpass);
    softConfig.wifi.ssid     = qsid;
    softConfig.wifi.password = qpass;
    softConfig.wifi.enable   = 1;
    configSave();

    content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
    statusCode = 200;
  }
  else
  {
    content = "{\"Error\":\"404 not found\"}";
    statusCode = 404;
    Serial.println("Sending 404");
  }
  server.send(statusCode, "application/json", content);
}


// Wifi
bool wifiConnect(const char* ssid, const char* password)
{
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(500);
    Serial.print(WiFi.status());
    c++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}
String wifiScan(void)
{
  String st;
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    st = "<ol>";
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      st += WiFi.SSID(i);
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
      st += "</li>";
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
    st += "</ol>";
  }
  Serial.println("");
  delay(100);

  return st;
}
void wifiReset()
{

  softConfig.wifi.ssid     = "";
  softConfig.wifi.password = "";
  configSave();

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect();
}
int wifiPower()
{
  int dBm = WiFi.RSSI();
  int quality;
  // dBm to Quality:
  if (dBm <= -100)
  {
    quality = 0;
  }
  else if (dBm >= -50)
  {
    quality = 100;
  }
  else
  {
    quality = 2 * (dBm + 100);
  }

  return quality;
}



String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }

  return encodedString;
}


void logger(String message)
{
  if (softConfig.wifi.enable)
  {
    HTTPClient httpClient;
    String urlTemp = BASE_URL;
    urlTemp += "log.php?message=";
    urlTemp += urlencode(message);

    httpClient.begin(urlTemp);
    httpClient.GET();
    httpClient.end();
  }
}




















void setup() {
  // Start the Serial Monitor
  Serial.begin(115200);
  // Start the DS18B20 sensor
  sensors.begin();

  Serial.println("");
  Serial.print("Welcome to "); Serial.print(SOFT_NAME); Serial.print(" - "); Serial.println(SOFT_VERSION);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  delay(2000);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  // Display static text
  display.println("Booting...");
  display.setCursor(0,40);
  display.print("v"); display.print(SOFT_VERSION);
  display.display(); 


  // FileSystem
  if (SPIFFS.begin())
  {
    boolean configFileToCreate = 0;
    // check if we have et config file
    if (SPIFFS.exists("/config.json"))
    {
      Serial.println("Config.json found. read data");
      configRead(softConfig);

      if (softConfig.softName != SOFT_NAME)
      {
        Serial.println("Not the same softname");
        Serial.print("Name from configFile : "); Serial.println(softConfig.softName);
        Serial.print("Name from code       : "); Serial.println(SOFT_NAME);
        configFileToCreate = 1;
      }
    }
    else
    {
      configFileToCreate = 1;
    }

    if (configFileToCreate == 1)
    {
      // No config found.
      // Start in AP mode to configure
      // debug create object here
      Serial.println("Config.json not found. Create one");
      
      softConfig.wifi.enable     = 1;
      softConfig.wifi.ssid       = "";
      softConfig.wifi.password   = "";
      softConfig.temp.adjustment = 0;
      softConfig.temp.checkTimer = 60;
      softConfig.alreadyStart    = 0;
      softConfig.softName        = SOFT_NAME;

      Serial.println("Config.json : load save function");
      configSave();
    }


    if (SPIFFS.exists("/data.json"))
    {
      dataJsonData = storageRead("/data.json");
      DeserializationError jsonError = deserializeJson(jsonData, dataJsonData);
      if (jsonError)
      {
        // Getting error when deserialise... I don know what to do here...
      }

      //consumptionLastCharge = jsonConsumption["lastCharge"];
      //consumptionTotal      = jsonConsumption["total"];
    }
    else
    {
      //consumptionLastCharge  = 0;
      //consumptionTotal       = 0;

      //jsonConsumption["lastCharge"] = consumptionLastCharge;
      //jsonConsumption["total"]      = consumptionTotal;

      dataSave();
    }
  }

  if (DEBUG)
  {
    configDump(softConfig);
  }


  if (softConfig.alreadyStart == 0 && softConfig.wifi.enable == 0)
  {
    softConfig.wifi.enable = 1;
  }


  int wifiMode = 0;
  String wifiList;
  if (softConfig.wifi.enable)
  {
    Serial.println("Enter wifi config");
    display.clearDisplay();
    display.setCursor(0,10);
    display.println("wifi settings");

    if (softConfig.wifi.ssid.length() > 0)
    {
      display.println("   connecting...");
      Serial.print("Wifi: Connecting to -");
      Serial.print(softConfig.wifi.ssid); Serial.println("-");

      // Connecting to wifi
      WiFi.mode(WIFI_AP);
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.hostname(wifiApSsid);

      const char * login = softConfig.wifi.ssid.c_str();
      const char * pass  = softConfig.wifi.password.c_str();
      bool wifiConnected = wifiConnect(login, pass);

      if (wifiConnected)
      {
        Serial.println("WiFi connected");
        wifiMode = 1;

        display.println(" .. ok");
      }
      else
      {
        Serial.println("Can't connect to Wifi. disactive webserver");
        softConfig.wifi.enable = 0;
      }
    }
    else
    {
      display.println("   standalone mode");
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.hostname(wifiApSsid);
      Serial.println("Wifi config not present. set AP mode");
      WiFi.softAP(wifiApSsid, wifiApPasswd, 6);
      Serial.println("softap");
      wifiMode = 2;

      display.println("  ko");
    }
  }
  else
  {
    Serial.println("Wifi desactivated");
    WiFi.mode(WIFI_OFF);

    display.println(" ..off");
    delay(2000);
  }

  Serial.println("End of wifi config");

  if (softConfig.wifi.enable)
  {
    // Start the server
    display.clearDisplay();
    display.setCursor(0,10);
    display.print("load webserver");

    if (wifiMode == 1) // normal mode
    {
      server.on("/", webRoot);
      server.on("/reload", webReload);
      server.on("/write", webWrite);
      server.on("/debug", webDebug);
      server.on("/reboot", webReboot);
      server.on("/api/config", webApiConfig);
      server.on("/setting", webInitSetting);
      ip = WiFi.localIP();
    }
    else if (wifiMode == 2) // AP mode for config
    {
      server.on("/", webInitRoot);
      server.on("/setting", webInitSetting);
      ip = WiFi.softAPIP();
    }
    server.onNotFound(webNotFound);
    server.begin();

    Serial.print("Http: Server started at http://");
    Serial.print(ip);
    Serial.println("/");
    Serial.print("Status : ");
    Serial.println(WiFi.RSSI());

    delay(1000);

    display.clearDisplay();
    display.setCursor(0,10);
    display.println("check update");

    String updateUrl = UPDATE_URL;
    Serial.println("Check for new update at : "); Serial.println(updateUrl);
    ESPhttpUpdate.rebootOnUpdate(1);
    t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, SOFT_VERSION);
    //t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, ESP.getSketchMD5() );

    Serial.print("return : "); Serial.println(ret);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;

      default:
        Serial.print("Undefined HTTP_UPDATE Code: "); Serial.println(ret);
    }
    display.println("update done");

    // get time();
    getTimeOnStartup();
    logger("Starting bicoqueTemp");
    String messageToLog = SOFT_VERSION ; messageToLog += " "; messageToLog += SOFT_DATE;
    logger(messageToLog);
  }

  
  display.clearDisplay();
  display.setCursor(0,10);
  display.print("Init ended.. starting");
}







void loop() 
{
  // We check httpserver evry 1 sec
  // We check Temp every X sec



  // check web client connections
  if (softConfig.wifi.enable)
  {
    server.handleClient();
  }


  if (tempTimer > softConfig.temp.checkTimer)
  {
    // Get sensor from 1wire
    sensors.requestTemperatures(); 
    temperature = sensors.getTempCByIndex(0) + temperatureAdjustement;

    float delta = temperature - temperatureOld;
    Serial.print("Delta temp : "); Serial.println(delta);
  
    if (delta > 0.1 or delta < -0.1)
    {
      Serial.print(temperature);
      Serial.println("ºC");

      display.clearDisplay();
      display.setTextSize(3);
      display.setTextColor(WHITE);
      display.setCursor(10, 25);
      // Display static text
      display.print(temperature,1);
      display.println(" C");
      display.display();

      temperatureOld = temperature;
    }
    else
    {
      Serial.print("No change");
    }

    tempTimer = 0;
  }
  
  tempTimer++; 
  delay(1000);
}
