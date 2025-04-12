#define ARDUINO 10808
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <Arduino.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>   // Include the Wi-Fi-Multi library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <WiFiManager.h>                // https://github.com/tzapu/WiFiManager
#include <DHTesp.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include "Font_Data.h"

using namespace std;

// Turn on debug statements to the serial output
#define  DEBUG  1

#if  DEBUG
#define PRINT(s, x) { Serial.print(F(s)); Serial.print(x); }
#define PRINTS(x) Serial.print(F(x))
#define PRINTX(x) Serial.println(x, HEX)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTX(x)
#endif

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers are for ESO8266 hardware SPI and will probably not
// work with your hardware and may need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_ZONES 2
#define ZONE_SIZE 20
#define MAX_DEVICES (MAX_ZONES * ZONE_SIZE)

#define ZONE_UPPER  1
#define ZONE_LOWER  0

#define PAUSE_TIME 0
#define SCROLL_SPEED 50

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

// HARDWARE SPI
// MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// SOFTWARE SPI
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Scrolling parameters
uint8_t frameDelay = 50;  // default frame delay value
textEffect_t scrollEffect = PA_SCROLL_LEFT;
textEffect_t scrollUpper = PA_SCROLL_LEFT;
textEffect_t scrollLower = PA_SCROLL_LEFT;

// Global message buffers shared by Wifi and Scrolling functions
#define BUF_SIZE  512
char curMessage[BUF_SIZE];
char newMessage[BUF_SIZE];
char upperMessage[BUF_SIZE];
bool newMessageAvailable = false;

#define softap_SSID "LEDMatrix18"                      // insert your SSID
#define softap_PASS "knockknock"                // insert your password

#define OTApassword "knockknock" //the password you will need to enter to upload remotely via the ArduinoIDE
#define OTAport 8266

#define TZ              -7       // (utc+) TZ in hours
#define DST_MN          0       // use 60mn for summer time in some countries

////////////////////////////////////////////////////////

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

std::string handleMacros (std::string message);
void Display (char* text);

ADC_MODE(ADC_VCC); // 3.3v voltage sensor

// ESP8266WiFiMulti wifiMulti;     // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
DNSServer dnsServer;
DHTesp dht;
ESP8266WebServer server(80);
int displayOffset = 0;

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
  }
}

String form()
{
  std::string WebPage =
    "<!DOCTYPE html>" \
    "<html>" \
    "<head>" \
    "<title>Scrolling Text Setup</title>" \

    "<script>" \
    "strLine = \"\";" \

    "function SendData()" \
    "{" \
    "  nocache = \"/msg&nocache=\" + Math.random() * 1000000;" \
    "  var request = new XMLHttpRequest();" \
    "  strLine = \"&MSG=\" + document.getElementById(\"data_form\").message.value;" \
    "  strLine = strLine + \"/&SD=\" + document.getElementById(\"data_form\").ScrollType.value;" \
    "  strLine = strLine + \"/&I=\" + document.getElementById(\"data_form\").Invert.value;" \
    "  strLine = strLine + \"/&SP=\" + document.getElementById(\"data_form\").Speed.value;" \
    "  request.open(\"GET\", strLine + nocache, false);" \
    "  request.send(null);" \
    "}" \
    "</script>" \
    "</head>" \

    "<body>" \
    "<p><b>Set Message</b></p>" \

    "<form id=\"data_form\" name=\"frmText\" action=\"/msg\">" \
    "\nText macros supported: $$ followed by: VCC IP SSID DATE TIME TEMP_F TEMP_C HUMID <br/>" \
    "<label>Message:<br><input type=\"text\" name=\"message\" size=\"128\" maxlength=\"255\" value='__MESSAGE__'></label>" \
    "<br/><!-- <br><br>" \
    "<input type = \"radio\" name = \"Invert\" value = \"0\" checked> Normal" \
    "<input type = \"radio\" name = \"Invert\" value = \"1\"> Inverse" \
    "<br>" \
    "<input type = \"radio\" name = \"ScrollType\" value = \"L\" checked> Left Scroll" \
    "<input type = \"radio\" name = \"ScrollType\" value = \"R\"> Right Scroll" \
    "<br><br>" \
    "<label>Speed:<br>Fast<input type=\"range\" name=\"Speed\"min=\"10\" max=\"250\">Slow"\
    "<br>" \
    "</form>" \
    "<br> -->" \
    "<input type=\"submit\" value=\"Send Data\" onclick=\"SendData()\">";

  std::string myhtml3 =
    "\n<pre>__PRE__</pre><br/><br/> "
    "\n</center></body></html>";

  replaceAll(WebPage, "__MESSAGE__", newMessage );
  replaceAll(WebPage, "__SPEED__", "250" );
  replaceAll(myhtml3, "__PRE__", handleMacros(newMessage) );

  yield();
  return (String)WebPage.c_str() + (String)myhtml3.c_str();
}

void save (String filename, String content) {
  Serial.printf("save(\"%s\",\"%s\")\n", filename.c_str(), content.c_str());
  File file = SPIFFS.open(filename, "w");
  if (file) {
    file.printf("%s", content.c_str());
  } else {
    Serial.printf("Cannot write to %s\n", filename.c_str());
  }
}

String load (String filename) {
  Serial.printf("load(\"%s\")", filename.c_str());
  File  file = SPIFFS.open(filename, "r");
  if (file) {
    String S;
    S = file.readString().c_str();
    Serial.printf(" == %s\n", S.c_str());
    return (S);
  } else {
    Serial.printf(" == '' due to file error...\n");
  }
}

//get the message from http
void handle_msg() {
  if (!server.authenticate("admin", "knockknock")) {
    return server.requestAuthentication();
  }
  sprintf(newMessage, "%s", server.arg("message").c_str());

  save("/msg.txt", newMessage); // spiffs
  displayOffset = 0;
  newMessageAvailable = true;
  sprintf(curMessage, "%s", handleMacros(newMessage).c_str() );
  createHString(upperMessage, curMessage);

  server.send(200, "text/html", form());
}

void change_speed() {
  if (!server.authenticate("admin", "knockknock")) {
    return server.requestAuthentication();
  }

  // myspeed = server.arg("speeds");


  server.send(200, "text/html", form());
}

// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec * tp);

timeval tv;
timespec tp;
uint32_t now_ms, now_us;
time_t now;

timeval cbtime;      // time set in callback
bool cbtime_set = false;

float humidity;
float temperature;

void time_is_set(void) {
  gettimeofday(&cbtime, NULL);
  cbtime_set = true;
}


std::string handleMacros (std::string message)
{
  if (std::string::npos != message.find("$$TIME")) {
    gettimeofday(&tv, nullptr);
    clock_gettime(0, &tp);
    now = time(nullptr);

    struct tm * timeinfo;
    char buffer [80];

    time (&now);
    timeinfo = localtime (&now);

    strftime (buffer, 80, "%r", timeinfo);
    replaceAll(message, "$$TIME", buffer);
  }

  if (std::string::npos != message.find("$$DATE")) {
    gettimeofday(&tv, nullptr);
    clock_gettime(0, &tp);
    now = time(nullptr);

    struct tm * timeinfo;
    char buffer [80];

    time (&now);
    timeinfo = localtime (&now);

    strftime (buffer, 80, "%A %B %e %G", timeinfo);
    replaceAll(message, "$$DATE", buffer);
  }

  if (std::string::npos != message.find("$$VCC")) {
    char buffer [80];
    float vcc = ESP.getVcc() / 1024.0 * 0.91591111111; // was off by about 500mv
    sprintf(buffer, "%0.3f", vcc);
    replaceAll(message, "$$VCC", buffer);
  }

  if (std::string::npos != message.find("$$SSID")) {
    char buffer [80];
    if ( 0 != WiFi.localIP()[0] ) {
      sprintf(buffer, "%s", WiFi.SSID().c_str());
    } else {
      sprintf(buffer, "%s", softap_SSID);
    }
    replaceAll(message, "$$SSID", buffer);
  }

  if (std::string::npos != message.find("$$IP")) {
    char result[16];
    if ( 0 != WiFi.localIP()[0] ) {
      sprintf(result, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    } else {
      sprintf(result, "%d.%d.%d.%d", WiFi.softAPIP()[0], WiFi.softAPIP()[1], WiFi.softAPIP()[2], WiFi.softAPIP()[3]);
    }
    replaceAll(message, "$$IP", result);
  }

  if (displayOffset == 0) {

    temperature = NULL;
    if ((std::string::npos != message.find("$$TEMP_F")) || (std::string::npos != message.find("$$TEMP_C"))) {
      for (int g = dht.getMinimumSamplingPeriod() * 2 ; g > 0 ; g -= 25) {
        temperature = dht.getTemperature();
        if (!isnan(temperature)) {
          break;
        }
        //delay(dht.getMinimumSamplingPeriod());
        delay(25); // we lock on until the checksum clears (OK read) or the sampling period expires.
      }
    }

    humidity = NULL;
    for (int g = dht.getMinimumSamplingPeriod() * 2 ; g > 0 ; g -= 25) {
      humidity = dht.getHumidity();
      if (!isnan(humidity)) {
        break;
      }
      delay(25); // we lock on until the checksum clears (OK read) or the sampling period expires.
    }
  }

  if (std::string::npos != message.find("$$TEMP_F")) {
    char F [8];
    sprintf(F, "%0.1f", dht.toFahrenheit(temperature));
    replaceAll(message, "$$TEMP_F", F);
  }
  if (std::string::npos != message.find("$$TEMP_C")) {
    char C [8];
    sprintf(C, "%0.1f", temperature);
    replaceAll(message, "$$TEMP_C", C);
  }
  if (std::string::npos != message.find("$$HUMID")) {
    char H [8];
    sprintf(H, "%d", int(humidity));
    replaceAll(message, "$$HUMID", H);
  }

  return message;
}

void handleRoot()
{
  if (!server.authenticate("admin", "knockknock")) {
    return server.requestAuthentication();
  }
  server.send(200, "text/html", form());
}

void check_factory_reset () {
  if (LOW == digitalRead(D1)) {
    Serial.println("factory_reset() per active LOW input on D1.");
    Display("RESET!!!");
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    WiFi.disconnect(true);
    delay(1000);
    Display("RESET in 4s");
    delay(1000);
    Display("RESET in 3s");
    delay(1000);
    Display("RESET in 2s");
    delay(1000);
    Display("RESET in 1s");
    delay(1000);
    Display("RESET in 0s");
    SPIFFS.format();
    ESP.reset();
  } else {
    Serial.println("D1 is HIGH, skip factory reset. (using flash)");
  }
}

void httpd_starter() {
  // Set up the endpoints for HTTP server,  Endpoints can be written as inline functions:
  server.on("/", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.

  server.on("/msg", handle_msg);                  // And as regular external functions:
  server.on("/speed", change_speed);

  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/gen_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.

  server.onNotFound([]() {
    handleRoot();
  });
  server.begin();                                 // Start the server
}

void ota_starter() {
  //OTA SETUP
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname("LEDMatrix18");                // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setPassword((const char *)OTApassword); // No authentication by default

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnded OTA");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    char p [40];
    sprintf(p, "OTA: %u%%", (progress / (total / 100)));
    Display(p);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("OTA Error[%u]: " + error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("OTA Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("OTA Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("OTA Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("OTA Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("OTA End Failed");
  });
  ArduinoOTA.begin();
}

void WM_autoConnect() {
  if ( digitalRead(D2) == LOW ) {
    Serial.println("D2 is LOW, enable WM");
    Display("WM_autoConnect");
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //exit after config instead of connecting
    wifiManager.setBreakAfterConfig(true);

    //reset settings - for testing
    //wifiManager.resetSettings();

    //tries to connect to last known settings
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP" with password "password"
    //and goes into a blocking loop awaiting configuration
    //  wifiManager.setTimeout(180);
    Display("D2 LOW, wifiManager");
    if (!wifiManager.autoConnect("LEDMatrix18", "knockknock")) {
      Serial.println("failed to connect, we should reset as see if it connects");
      delay(3000);
      ESP.reset();
      delay(5000);
    }
  } else {
    Serial.println("D2 is HIGH, ignore/skip WM");
    Display("D2 HIGH, no WIFI");
    delay(5000);
  }
}

boolean D2_handler() {
  if ( digitalRead(D2) == LOW ) {
    ESP.wdtDisable();                               // used to debug, disable wachdog timer,
    Serial.println("D2 is LOW, startConfigPortal(...)");
    Display("LEDMatrix18 192.168.4.1");
    WiFiManager wifiManager;
    wifiManager.setTimeout(180);
    wifiManager.startConfigPortal("LEDMatrix18", "knockknock");
    ESP.wdtEnable(10000);
    yield();
    return true;
  }
  return false;
}

void setup ()
{
  uint8_t max = 0;

  Serial.begin(115200);                           // full speed to monitor
  SPIFFS.begin();

  P.begin(MAX_ZONES);
  // Set up zones for 2 halves of the display
  P.setZone(ZONE_LOWER, 0, ZONE_SIZE - 1);
  P.setZone(ZONE_UPPER, ZONE_SIZE, MAX_DEVICES - 1);
  P.setFont(BigFont);
  P.setCharSpacing(P.getCharSpacing() * 2); // double height --> double spacing

  P.displayReset(ZONE_LOWER);
  P.displayReset(ZONE_UPPER);
  Display("LED Matrix by ...");
  delay(2500);

  P.displayReset(ZONE_LOWER);
  P.displayReset(ZONE_UPPER);
  Display("Scott C Edwards");
  delay(2500);

  P.displayReset(ZONE_LOWER);
  P.displayReset(ZONE_UPPER);
  Display("(C) 2019");
  delay(2500);

//  sprintf(curMessage,"%s","LED Matrix by Scott C Edwards (C) 2019");
//  char *uppertext = (char *)malloc(strlen(curMessage) + 2);
//  createHString(uppertext, curMessage);
//  P.displayZoneText(ZONE_LOWER, curMessage, PA_LEFT, 25, 1000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
//  P.displayZoneText(ZONE_UPPER, uppertext, PA_LEFT, 25, 1000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
//  P.displayReset(ZONE_LOWER);
//  P.displayReset(ZONE_UPPER);
//  P.displayClear();
//  P.synchZoneStart();
//  P.displayAnimate();

  Display("OTA starter");
  ota_starter();

  Display("DHT setup");
  dht.setup(D6, DHTesp::DHT11); // Connect DHT sensor to D5
  pinMode(D1, INPUT); // factory reset SPIFFS switch.
  pinMode(D2, INPUT); // captive portal switch.
  delay(50); // D1????

  WM_autoConnect();

  Display("httpd starter");
  httpd_starter();

  Display("check factory rst");
  check_factory_reset();

  char result[16];
  sprintf(result, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  Serial.println();
  Serial.println(result);
  Serial.print("WebServer ready!   ");
  Serial.println(WiFi.localIP());  // Serial monitor prints localIP

  WiFi.hostname("LEDMatrix18");

  IPAddress apIP(WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);

  settimeofday_cb(time_is_set);
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  sprintf(newMessage, "%s", load("/msg.txt").c_str() );
  displayOffset = 0;
  newMessageAvailable = true;
  sprintf(curMessage, "%s", handleMacros (newMessage).c_str() );

  if (strlen(curMessage) == 0) {
    sprintf(curMessage, "%s", handleMacros ("Login to http://$$IP ($$SSID)").c_str() );
  }
}

void createHString(char *pH, char *pL)
{
  for (; *pL != '\0'; pL++)
    *pH++ = *pL | 0x80;   // offset character

  *pH = '\0'; // terminate the string
}

void Display(char *text) {
  Serial.printf("Display(\"%s\");\n", text);
  char *uppertext = (char *)malloc(strlen(text) + 2);
  createHString(uppertext, text);
  P.displayZoneText(ZONE_LOWER, text, PA_LEFT, 0, 0, PA_PRINT, PA_PRINT);
  P.displayZoneText(ZONE_UPPER, uppertext, PA_LEFT, 0, 0, PA_PRINT, PA_PRINT);
  P.synchZoneStart();
  P.displayAnimate();
}

void theMatrix() {

  P.displayAnimate();

  if (P.getZoneStatus(ZONE_LOWER) && P.getZoneStatus(ZONE_UPPER))
  {
    // set up the string
    displayOffset = 0;
    newMessageAvailable = true;

    sprintf(curMessage, "%s", handleMacros(newMessage).c_str() );
    createHString(upperMessage, curMessage);
    Serial.printf("curMessage = %s\n", curMessage);

    P.displayReset(ZONE_LOWER);
    P.displayReset(ZONE_UPPER);
    P.displayClear();
    P.displayZoneText(ZONE_LOWER, curMessage, PA_LEFT, SCROLL_SPEED, PAUSE_TIME, scrollLower, scrollLower);
    P.displayZoneText(ZONE_UPPER, upperMessage, PA_LEFT, SCROLL_SPEED, PAUSE_TIME, scrollUpper, scrollUpper);

    // synchronize the start and run the display
    P.synchZoneStart();
  }

}

void loop ()
{
  server.handleClient();
  dnsServer.processNextRequest();
  ArduinoOTA.handle(); // Check OTA Firmware Updates
  theMatrix();
}  // end of loop
