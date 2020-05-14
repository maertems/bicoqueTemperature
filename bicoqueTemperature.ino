/*

Help from : 
 https://github.com/ThingPulse/esp8266-weather-station
 https://mcuoneclipse.com/2017/09/09/wifi-oled-mini-weather-station-with-esp8266/
 https://how2electronics.com/weather-station-with-nodemcu-oled/
 https://randomnerdtutorials.com/esp32-esp8266-plot-chart-web-server/


Not yet :)
 https://github.com/landru29/ovh_metrics_wemos/blob/master/metrics.cpp
*/

#include <OneWire.h>
#include <DallasTemperature.h>

// basic
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <ESP8266httpUpdate.h>

#include <ArduinoJson.h>
#include <FS.h>

// NTP
#include <NTPClient.h>
#include <WiFiUdp.h>

// OLED screen
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Weather
#include "lib/WeatherIcon.h"

// firmware version
#define SOFT_NAME "bicoqueTemperature"
#define SOFT_VERSION "0.1.45"
#define SOFT_DATE "2020-05-14"

#define DEBUG 1

// NTP Constant
#define NTP_SERVER "ntp.ovh.net"
#define NTP_TIME_ZONE 2         // GMT +2:00

WiFiUDP ntpUDP;
// params WifiUDP object / ntp server / timeZone in sec / request ntp every xx milisec
NTPClient timeClient(ntpUDP, NTP_SERVER , (NTP_TIME_ZONE * 3600) , 86400000);


//-- OneWire Configuration
// GPIO where the DS18B20 is connected to
const int oneWireBus = D5;     
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);


//-- Screen Init
#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64   // OLED display height, in pixels
#define SCREEN_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ROTATION 2  // 0 -> 0 / 1 -> 90 / 2 -> 180 / 3 -> 270 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET);
// FreeSans12pt7b.h FreeSans18pt7b.h FreeSans24pt7b.h FreeSans9pt7b.h



// Init
float temperatureOld = -255 ;
float temperature;
bool networkEnable      = 1;
bool internetConnection = 0; 
int wifiActivationTempo = 600; // Time to enable wifi if it s define disable
int tempTimer = 0;
int tempTimerOw = 0;
int tempTimerHour = 3600;
IPAddress ip;


// Wifi AP info for configuration
const char* wifiApSsid = SOFT_NAME;


// Update info
#define BASE_URL "http://mangue.net/ota/esp/" SOFT_NAME "/"
#define UPDATE_URL BASE_URL "update.php"


// OpenWeather Info
String owUrlBase     = "http://api.openweathermap.org/data/2.5/weather";


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
  String owLocationId;
  int owCheckTimer;
  String owApiKey;
};
typedef struct configCloud
{
  String url;
  String apiKey;
};
typedef struct config
{
  configWifi wifi;
  configTemp temp;
  configCloud cloud;
  boolean alreadyStart;
  String softName;
};
config softConfig;


typedef struct meteoStruct
{
  float temp;
  int humidity;
  float wind_speed;
  int wind_degree;
  String icon;
};
meteoStruct Meteo;



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
    //logger("FS: opening file error");
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

bool storageAppend(char *fileName, String dataText)
{
  File file = SPIFFS.open(fileName, "a");
  file.print(dataText);
  file.close();
  return true;
}

bool storageClear(char *fileName)
{
  SPIFFS.remove(fileName);
  return true;
}

void dataClear()
{
  storageClear("/data.csv");
}

void dataSave()
{
  int timestamp = timeClient.getEpochTime();
  String lineToLog = String(timestamp);
  lineToLog += ",";
  lineToLog += temperature;
  lineToLog += "\n";

  if (DEBUG) 
  {
    Serial.print("write temp : ");
    Serial.print(lineToLog);
  }

  storageAppend("/data.csv", lineToLog);
}

void dataArchive()
{
  bool conitnueLoop        = 0;
  unsigned int lineToStart = 1;

  do {
    conitnueLoop            = 0;
    unsigned int lineNumber = 0;
    float temperatureTemp   = 0;
    unsigned int lastTimestamp;
    unsigned int firstTimestamp;

    File file = SPIFFS.open("/data.csv", "r");
    while(file.available())  // we could open the file, so loop through it to find the record we require
    {
      lineNumber++;

      if (lineToStart <= lineNumber)
      {
        String time = file.readStringUntil(',');   // Read line by line from the file
        String temp = file.readStringUntil('\n');  // Read line by line from the file

        if (time.length() > 0)
        {
          if (lineToStart = lineNumber)
          {
            firstTimestamp = time.toInt();
          }

          lastTimestamp    = time.toInt();
          if (lastTimestamp - firstTimestamp > 3600)
	  {
            lineToStart  = lineNumber;
            lineNumber--;
            conitnueLoop = 1;
	    break;
          }
          
          temperatureTemp += temp.toFloat();
        }
      }
    }
    file.close();

    temperatureTemp = temperatureTemp / lineNumber; // get moyen

    String lineToLog = String(lastTimestamp);
    lineToLog += ",";
    lineToLog += temperatureTemp;
    lineToLog += "\n";

    if (DEBUG)
    {
      Serial.print("Archive temp : ");
      Serial.print(lineToLog);
    }

    storageAppend("/history.csv", lineToLog);
  } while (conitnueLoop);


  // remove data
  dataClear();
}


void dataStats(int timeNow)
{
  int firstTimestamp;
  String lastTimestamp;
  bool firstLine = 1;
  unsigned int lineNumber = 0;

  File file = SPIFFS.open("/data.csv", "r");
  while(file.available())  // we could open the file, so loop through it to find the record we require
  {
    lineNumber++;
    String time = file.readStringUntil(',');   // Read line by line from the file

    if (firstLine == 1)
    {
      firstTimestamp = time.toInt();
      firstLine      = 0;
    }

    lastTimestamp = time;
  }

  if (DEBUG)
  {
    Serial.println("Data Stats :");
    Serial.print("First Timestamp : "); Serial.println(firstTimestamp); 
    Serial.print("Last Timestamp : "); Serial.println(lastTimestamp); 
    Serial.print("lineNumber : "); Serial.println(lineNumber); 
  }

  // Check if we need to archive datas
  if ((timeNow - firstTimestamp) > 3600)
  {
    dataArchive();
  }

}





void dataRead(String (&datas)[2])
{
  unsigned int lineNumber = 0;
  
  datas[0] = "[";
  //datas[1] = "[";

  File file = SPIFFS.open("/history.csv", "r");
  while(file.available())  // we could open the file, so loop through it to find the record we require
  {
    lineNumber++;       
    String time = file.readStringUntil(',');   // Read line by line from the file
    String temp = file.readStringUntil('\n');  // Read line by line from the file

    if (time.length() > 0)
    {
      if (lineNumber > 1)
      {
        datas[0] += ',';
        // datas[1] += ',';
      }

      datas[0] += String( '[' + time + "000," + temp + ']');
      // datas[0] += time;
      // datas[1] += temp;
    }

    Serial.print("time : "); Serial.print(time); Serial.print(" / temp : "); Serial.println(temp);
  }

  datas[0] += "]";
  // datas[1] += "]";

  Serial.println(lineNumber);         // show line number of SPIFFS file
  file.close(); 

  //return &datas;
}




String configSerialize()
{
  String dataJsonConfig;
  DynamicJsonDocument jsonConfig(800);
  JsonObject jsonConfigWifi  = jsonConfig.createNestedObject("wifi");
  JsonObject jsonConfigTemp  = jsonConfig.createNestedObject("temp");
  JsonObject jsonConfigCloud = jsonConfig.createNestedObject("cloud");

  jsonConfig["alreadyStart"]     = softConfig.alreadyStart;
  jsonConfig["softName"]         = softConfig.softName;
  jsonConfigWifi["ssid"]         = softConfig.wifi.ssid;
  jsonConfigWifi["password"]     = softConfig.wifi.password;
  jsonConfigWifi["enable"]       = softConfig.wifi.enable;
  jsonConfigTemp["adjustment"]   = softConfig.temp.adjustment;
  jsonConfigTemp["checkTimer"]   = softConfig.temp.checkTimer;
  jsonConfigTemp["owLocationId"] = softConfig.temp.owLocationId;
  jsonConfigTemp["owCheckTimer"] = softConfig.temp.owCheckTimer;
  jsonConfigTemp["owApiKey"]     = softConfig.temp.owApiKey;
  jsonConfigCloud["apiKey"]      = softConfig.cloud.apiKey;
  jsonConfigCloud["url"]         = softConfig.cloud.url;
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

  ConfigTemp.alreadyStart      = jsonConfig["alreadyStart"];
  ConfigTemp.softName          = jsonConfig["softName"].as<String>();
  ConfigTemp.wifi.ssid         = jsonConfig["wifi"]["ssid"].as<String>();
  ConfigTemp.wifi.password     = jsonConfig["wifi"]["password"].as<String>();
  ConfigTemp.wifi.enable       = jsonConfig["wifi"]["enable"];
  ConfigTemp.temp.adjustment   = jsonConfig["temp"]["adjustment"];
  ConfigTemp.temp.checkTimer   = jsonConfig["temp"]["checkTimer"];
  ConfigTemp.temp.owLocationId = jsonConfig["temp"]["owLocationId"].as<String>();
  ConfigTemp.temp.owCheckTimer = jsonConfig["temp"]["owCheckTimer"];
  ConfigTemp.temp.owApiKey     = jsonConfig["temp"]["owApiKey"].as<String>();
  ConfigTemp.cloud.apiKey      = jsonConfig["cloud"]["apiKey"].as<String>();
  ConfigTemp.cloud.url         = jsonConfig["cloud"]["url"].as<String>();

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
  Serial.print("  - owLocationId : "); Serial.println(ConfigTemp.temp.owLocationId);
  Serial.print("  - owCheckTimer : "); Serial.println(ConfigTemp.temp.owCheckTimer);
  Serial.print("  - owApiKey : "); Serial.println(ConfigTemp.temp.owApiKey);
  Serial.println("Cloud data :");
  Serial.print("  - apiKey : "); Serial.println(ConfigTemp.cloud.apiKey);
  Serial.print("  - url : "); Serial.println(ConfigTemp.cloud.url);
  Serial.println("General data :");
  Serial.print("  - alreadyStart : "); Serial.println(ConfigTemp.alreadyStart);
  Serial.print("  - softName : "); Serial.println(ConfigTemp.softName);
}



// ------------------------------
// Wifi
// ------------------------------
bool wifiConnectSsid(const char* ssid, const char* password)
{
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 80 ) {
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
  if (dBm <= -100)     { quality = 0; }
  else if (dBm >= -50) { quality = 100; }
  else                 { quality = 2 * (dBm + 100); }

  return quality;
}

void wifiDisconnect()
{
  WiFi.mode( WIFI_OFF );
}

bool wifiConnect(String ssid, String password)
{
    if ( DEBUG ) { Serial.println("Enter wifi config"); }
    display.println("- wifi settings");
    display.display();

    // Try connecting to SSID
    display.println(" - connecting... ");
    display.print(" ");
    display.print(ssid);
    display.display();

    if (DEBUG) { Serial.print("Wifi: Connecting to '"); Serial.print(ssid); Serial.println("'"); }

    // Unconnect from AP
    WiFi.mode(WIFI_AP);
    WiFi.disconnect();

    // Connecting to SSID
    WiFi.mode(WIFI_STA);
    WiFi.hostname(SOFT_NAME);

    bool wifiConnected = wifiConnectSsid(ssid.c_str(), password.c_str());

    if (wifiConnected)
    {
      if (DEBUG) { Serial.println("WiFi connected"); }
      internetConnection = 1;

      display.println(" .. ok");
      display.display();
  
      ip = WiFi.localIP();

      return true;
    }
    else
    {
      display.println(" .. KO");
      display.display();
    }

    // If SSID connection failed. Go into AP mode
    display.println(" - standalone mode");
    display.display();

    // Disconnecting from Standard Mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Connect to AP mode
    WiFi.mode(WIFI_AP);
    WiFi.hostname(SOFT_NAME);

    if (DEBUG) { Serial.println("Wifi config not present. set AP mode"); }
    WiFi.softAP(wifiApSsid);
    if (DEBUG) {Serial.println("softap"); }

    ip = WiFi.softAPIP();

    return false;
}


// --------------------------------
// Update
// --------------------------------
void updateCheck(bool displayScreen)
{
    if (displayScreen)
    {
      display.println("- check update");
      display.display();
    }

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

    if (displayScreen)
    {
      display.println("update done");
      display.display();
    }
}






// -------------------------------
// Logger function
// -------------------------------
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
  if (internetConnection)
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

bool dataPut(String type, float temp, unsigned int timestamp)
{
  if (internetConnection)
  {
    if (softConfig.cloud.url != "" and softConfig.cloud.apiKey != "")
    {
      HTTPClient httpClient;
      String urlTemp = softConfig.cloud.url;
      urlTemp += "/update.php?secret=";
      urlTemp += softConfig.cloud.apiKey;
      urlTemp += "&type=";
      urlTemp += type;
      urlTemp += "&temp=";
      urlTemp += temp;
      urlTemp += "&timestamp=";
      urlTemp += timestamp;

      //String url = urlencode(urlTemp);
      String url = urlTemp;

      if (DEBUG)
      {
        Serial.print("dataPush : Url for external use : "); Serial.println(url);
      }

      httpClient.begin(url);
      httpClient.GET();
      httpClient.end();

      return 1;
    }
    else
    {
      if (DEBUG)
      {
        Serial.println("dataPush : Missing either cloud.url or cloud.apiKey");
      }
    }
  }
  else
  {
    if (DEBUG)
    {
      Serial.println("dataPush : No internet connection");
    }
    return 0;
  }
}




void testscrolltext(void) 
{
  String message = "un texte qui est tres long et qui depasse";
  int x, minX;
  x = display.width();
  minX = -12 * message.length();

  // loop
  for( int i = 0; i < 400; i++)
  {
    // Clear line
    display.fillRect(0, 54, 128, 64, BLACK);

    display.setCursor(x, 56);
    display.print(message);
    display.display();
    x=x-1;
  }

}



// -----------------
// OpenWeather
// -----------------
String openWeatherCall()
{

  if (softConfig.temp.owLocationId == "")
  {
    return "{}";
  }

  if (softConfig.temp.owApiKey == "")
  {
    return "{}";
  }

  String owUrl = String( owUrlBase + "?id=" + softConfig.temp.owLocationId + "&APPID=" + softConfig.temp.owApiKey );

  HTTPClient http; //Declare an object of class HTTPClient
  http.begin(owUrl);
  int httpCode = http.GET(); // send the request

  String payload;

  if (httpCode > 0) // check the returning code
  {
    payload = http.getString(); //Get the request response payload
  }
  http.end(); //Close connection

  return payload;
}

void openWeatherGetWeather(meteoStruct &meteo)
{
    String jsonData = openWeatherCall();

    DynamicJsonDocument root(4096);

    // Parse JSON object
    DeserializationError jsonError = deserializeJson(root, jsonData);
    if (jsonError)
    {
      Serial.println("Got Error when deserialization : "); //Serial.println(jsonError);
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(jsonError.c_str());

      return;
    }

   // json data :
   // {"coord":{"lon":3.05,"lat":50.67},
   // "weather":[{"id":804,"main":"Clouds","description":"overcast clouds","icon":"04d"}],
   // "base":"stations",
   // "main":{"temp":285.04,"feels_like":279.19,"temp_min":283.71,"temp_max":286.48,"pressure":1019,"humidity":62},
   // "visibility":10000,
   // "wind":{"speed":6.7,"deg":50},
   // "clouds":{"all":97},
   // "dt":1588672109,
   // "sys":{"type":1,"id":6559,"country":"FR","sunrise":1588652067,"sunset":1588706065},
   // "timezone":7200,
   // "id":2981779,
   // "name":"Lille"
   // ,"cod":200}


    meteo.temp        = (float)(root["main"]["temp"]) - 273.15; // get temperature in °C
    meteo.humidity    = root["main"]["humidity"];               // get humidity in %
    meteo.wind_speed  = root["wind"]["speed"];                  // get wind speed in m/s
    meteo.wind_degree = root["wind"]["deg"];                    // get wind degree in °
    meteo.icon        = root["weather"][0]["icon"].as<String>();

    Serial.print("Debug openWeatherGetWeather : new temp : "); Serial.println(meteo.temp);
}


void displayMeteo()
{

/*
    // print data
    Serial.printf("Temperature = %.2f°C\r\n", temp);
    Serial.printf("Humidity = %d %%\r\n", humidity);
    Serial.printf("Wind speed = %.1f m/s\r\n", wind_speed);
    Serial.printf("Wind degree = %d°\r\n\r\n", wind_degree);
*/
}












// ********************************************
// WebServer
// ********************************************
void webRoot() 
{
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
  message += "<form action='/write' method='GET'>OpenWeather Check Timer : <input type=text name=owCheckTimer><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>OpenWeather Location ID : <input type=text name=owLocationId><input type=submit></form><br>\n";
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

void webApiHistoryClear()
{
  dataClear();
  server.send(200, "text/html", "done");
}

void webTemperature()
{
  server.send(200, "text/html", String(temperature));
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

  String adjustment    = server.arg("adjustment");
  String checkTimer    = server.arg("checkTimer");
  String owLocationId  = server.arg("owLocationId");
  String owCheckTimer  = server.arg("owCheckTimer");
  String owApiKey      = server.arg("owApiKey");
  String cloudUrl      = server.arg("cloudUrl");
  String cloudApiKey   = server.arg("cloudApiKey");

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
  if (owCheckTimer != "")
  {
    softConfig.temp.owCheckTimer = owCheckTimer.toInt();
    configSave();
  }
  if (owLocationId != "")
  {
    softConfig.temp.owLocationId = owLocationId;
    configSave();
  }
  if (owApiKey != "")
  {
    softConfig.temp.owApiKey = owApiKey;
    configSave();
  }
  if (cloudUrl != "")
  {
    softConfig.cloud.url = cloudUrl;
    configSave();
  }
  if (cloudApiKey != "")
  {
    softConfig.cloud.apiKey = cloudApiKey;
    configSave();
  }

  Serial.println("Write done");
  message += "Write done...\n";
  server.send(200, "text/plain", message);
}

void webWifiSetup()
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

void webWifiWrite()
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

void webApiHistory()
{
  String datas[2];
  dataRead(datas);

  String message;

  message = datas[0];
  message += "\n";
  message += datas[1];
  message += "\n";

  server.send(200, "text/html", message);
}

void webDisplay()
{
  String message;
  String qchar = server.arg("char");

  if (qchar != "")
  {
    uint8_t i = qchar.toInt();
    // Display Char on screen
    display.clearDisplay();
    display.setTextSize(1);            // Set font size to defaut
    display.setTextColor(WHITE);
    display.setFont(&Weathericon);        // Change font

    display.setCursor(80,20);    // Just cause Icon are pretty "big"
    display.write(i);
    display.display();
    display.setFont();
  }

  message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += SOFT_NAME;
  message += "<br><br>\n";

  message += "Debug : <br>\n";
  message += "<p>\n";
  message += "</p>\n<form method='get' action='display'><label>Char to display (32-96) : </label><input name='char' length=2><input type='submit'></form>\n";
  message += "</html>\n";
  server.send(200, "text/html", message);
}

void webApiForecast()
{
  String message = openWeatherCall();
  server.send(200, "text/html", message);
}

void web_index()
{

  // Get historic from file
  String datas[2];
  dataRead(datas);


  String index_html = R"rawliteral( 
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <script src="https://code.highcharts.com/highcharts.js"></script>
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .dht-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body>
  <h2>My Home</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Salon</span> 
    <span id="temperature">)rawliteral";
index_html += temperature;
index_html += R"rawliteral(</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
  <div id="chart-temperature" class="container"></div>
  </p>
</body>


<script>

var chartT = new Highcharts.Chart({
  chart:{ renderTo : 'chart-temperature', zoomType: 'x' },
  title: { text: 'History' },
  series: [{
    showInLegend: false,
    data: )rawliteral";
index_html += datas[0];
index_html += R"rawliteral(
  }],
  plotOptions: {
    fillColor: {
      linearGradient: {
            x1: 0,
            y1: 0,
            x2: 0,
            y2: 1
        },
        stops: [
            [0, Highcharts.getOptions().colors[0]],
            [1, Highcharts.color(Highcharts.getOptions().colors[0]).setOpacity(0).get('rgba')]
        ]
    },
    marker: {
        radius: 2
      },
    lineWidth: 1,
    states: {
        hover: {
            lineWidth: 1
        }
    },
    threshold: null,
    series: { color: '#059e8a' }
  },
  xAxis: { type: 'datetime',
    dateTimeLabelFormats: { second: '%H:%M:%S' },
  },
  yAxis: {
    title: { text: 'Temperature (Celsius)' }
  },
  credits: { enabled: false }
});



setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("temperature").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/temperature", true);
  xhttp.send();
}, 10000 ) ;

</script>
</html>)rawliteral";

  server.send(200, "text/html", index_html);
}




// DISPLAY Screen
void screenDisplayMain()
{
  if (DEBUG and 1 == 0)  // too much log on serial
  {
    Serial.println("List of thing to print on display :");
    Serial.print("temperature : "); Serial.println(temperature);
    Serial.print("Meteo.icon : "); Serial.println(Meteo.icon);
    Serial.print("hours : "); Serial.println(timeClient.getHours());
    Serial.print("minutes : "); Serial.println(timeClient.getMinutes());
    Serial.print("Meteo.temp : "); Serial.println(Meteo.temp);
    Serial.print("ip : "); Serial.println(ip);
    Serial.print(""); Serial.println();
  } 

  display.clearDisplay();
  display.setTextColor(WHITE);

  // draw lines
  display.drawLine(80,5, 80,40, WHITE);  // Verticaly
  display.drawLine(5,50, 123,50, WHITE); // Horizontal


  // Temp int
  display.setTextSize(2);
  display.setCursor(1, 15);
  display.print(temperature,1);
  display.println(" C");

  // Weather
  // icon
  uint8_t iconChar = getMeteoconIcon(Meteo.icon);

  display.setTextSize(1);          // Set font size to defaut
  display.setFont(&Weathericon);   // Change font
  display.setCursor(95,22);        // Set on the upper right
  display.write(iconChar);         // print char from 32 to 96
  display.setFont();               // Set default font

  // Temp
  display.setCursor(90, 32); //was 85
  display.print(Meteo.temp, 1);
  display.print(" C");


  // Lower line

  // Time
  display.setCursor(0,55);
  String hours;
  if ( timeClient.getHours() > 9 ) { hours = timeClient.getHours(); }
  else { hours = "0"; hours += timeClient.getHours(); }

  String minutes;
  if ( timeClient.getMinutes() > 9 ) { minutes = timeClient.getMinutes(); }
  else { minutes = "0"; minutes += timeClient.getMinutes(); }

  display.print(hours); display.print(":"); display.print(minutes);

  // IP
  display.setTextSize(1);
  display.setCursor(40,55);
  display.print(ip);


  display.display();

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

  // Init Display
  display.setRotation(SCREEN_ROTATION);
  display.clearDisplay();

  // First display
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Booting... v");
  display.println(SOFT_VERSION);
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
      
      softConfig.wifi.enable       = 1;
      softConfig.wifi.ssid         = "";
      softConfig.wifi.password     = "";
      softConfig.temp.adjustment   = 0;
      softConfig.temp.checkTimer   = 60;
      softConfig.temp.owLocationId = 3037520;
      softConfig.temp.owCheckTimer = 600;
      softConfig.alreadyStart      = 0;
      softConfig.softName          = SOFT_NAME;

      Serial.println("Config.json : load save function");
      configSave();
    }


    // check Data
    if (SPIFFS.exists("/data.csv"))
    {
      dataStats(timeClient.getEpochTime());
    }
  }

  if (DEBUG)
  {
    configDump(softConfig);
  }


  internetConnection = wifiConnect(softConfig.wifi.ssid, softConfig.wifi.password);

  if (softConfig.wifi.enable)
  {
    wifiActivationTempo = 0;
  }
  else
  {
    if (DEBUG) { Serial.println("Deactive wifi in 5 mins."); }
    wifiActivationTempo = 600;
  }

  Serial.println("End of wifi config");

  // Start the server
  display.println("- load webserver");

  //server.on("/", webRoot);
  server.on("/", web_index);
  server.on("/reload", webReload);
  server.on("/write", webWrite);
  server.on("/debug", webDebug);
  server.on("/reboot", webReboot);
  server.on("/display", webDisplay);
  server.on("/temperature", webTemperature);
  server.on("/api/config", webApiConfig);
  server.on("/api/history", webApiHistory);
  server.on("/api/historyClear", webApiHistoryClear);
  server.on("/api/forecast", webApiForecast);
  server.on("/setting", webWifiWrite);
  server.on("/wifi", webWifiSetup);

  server.onNotFound(webNotFound);
  server.begin();

  if (DEBUG)
  {
    Serial.print("Http: Server started at http://");
    Serial.print(ip);
    Serial.println("/");
    Serial.print("Status : ");
    Serial.println(WiFi.RSSI());
  }

  delay(1000);


  if (internetConnection)
  {
    // check update
    updateCheck(1);

    // get time();
    timeClient.begin();

    logger("Starting bicoqueTemp");
    String messageToLog = SOFT_VERSION ; messageToLog += " "; messageToLog += SOFT_DATE;
    logger(messageToLog);
  }

  
  display.println("Init ended.. starting");
  display.display();

  // Need to display when booting
  tempTimer     = softConfig.temp.checkTimer + 10000;
  tempTimerOw   = softConfig.temp.owCheckTimer + 10000;
}







void loop() 
{
  // We check httpserver evry 1 sec
  // We check Temp every X sec


  float sleepDelay = 0.1; // in secs  - old value : 0.1 
  

  if (networkEnable)
  {
    // check web client connections
    server.handleClient();
    timeClient.update();

   // Check if we have a delay on wifi to disable it
   if (wifiActivationTempo > 0 )
   {
      if (wifiActivationTempo > (millis() / 1000) )
      {
        if (softConfig.wifi.enable)
        {
          // try to reconnect evry 5 mins
	  internetConnection = wifiConnect(softConfig.wifi.ssid, softConfig.wifi.password);
 
          if (internetConnection)
          {
            wifiActivationTempo = 0;
          }
          else
          {
            wifiActivationTempo = (millis() / 1000) + 600;
          }
        }
        else
        {
          // Need to de-active wifi
          WiFi.mode(WIFI_OFF);
          WiFi.disconnect();

          wifiActivationTempo = 0;
          networkEnable       = 0;
        }
      }
   }
  }


  if (tempTimer > (softConfig.temp.checkTimer / sleepDelay) )
  {
    // Get sensor from 1wire
    sensors.requestTemperatures(); 
    temperature = sensors.getTempCByIndex(0) + softConfig.temp.adjustment;

    float delta = temperature - temperatureOld;
    Serial.print("Delta temp : "); Serial.println(delta);

    if (delta > 0.1 or delta < -0.1)
    {
      Serial.print(temperature);
      Serial.println("ºC");

      temperatureOld = temperature;
    }
    else
    {
      Serial.print("No change");
    }

    // log all temeratures
    dataSave();
    dataPut("indoor", temperature, timeClient.getEpochTime());
    tempTimer = 0;
  }


  if (tempTimerOw > (softConfig.temp.owCheckTimer / sleepDelay) )
  {
    // Get Info from openweathermap
    openWeatherGetWeather(Meteo);

    Serial.print("Object Meteo.temp : "); Serial.println(Meteo.temp);

    dataPut("outdoor", Meteo.temp, timeClient.getEpochTime());
    tempTimerOw = 0;
  }

  if (tempTimerHour > (3600 / sleepDelay) )
  {
    updateCheck(0);
    dataStats(timeClient.getEpochTime());
    tempTimerHour = 0;

    String messageToLog = "Datalog: temp int: "; messageToLog += temperature; messageToLog += " / Temp ext : "; messageToLog += Meteo.temp;
    logger(messageToLog);
  }

  // need to display the time  
  screenDisplayMain();
  // testscrolltext();

  tempTimer++;
  tempTimerOw++;
  tempTimerHour++;

  delay(sleepDelay * 1000);
}
