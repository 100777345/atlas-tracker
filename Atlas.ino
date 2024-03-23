// For ESP32. For use in acceptance test demo on March 5th.

// ======== Libraries ========
#include <string> // String
#include <TinyGPS++.h> // GPS
#include <SoftwareSerial.h> // GPS + SD card
#include <ArduinoJson.h> // JSON
#include <ezButton.h> // Button
#include "Adafruit_MAX1704X.h" // Battery State
#include <WiFi.h> // WiFi
#include <HTTPClient.h> // HTTP requests
#include <WiFiClientSecure.h> // HTTPS
#include <AutoConnect.h> // WiFi Autoconnect

// ======== Pins ========
// Line 512 should also be changed when changing button pin
#define BUTTON_PIN 10
#define GPS_RX_PIN 38
#define GPS_TX_PIN 39
#define POWER_LED_PIN 5
#define RGB_RED_PIN 11
#define RGB_GREEN_PIN 12
#define RGB_BLUE_PIN 13

// ======== Other Constants ========
#define SHORT_PRESS_TIME 1000 // 1000 milliseconds
#define LONG_PRESS_TIME  1000 // 1000 milliseconds
#define BLINK_INTERVAL 1000 
#define WIFI_TIMEOUT 20000 // Time until device gives up on connecting to WiFi network (ms)
#define GPS_TIMEOUT 10000 // Time until device gives up on getting location data from GPS (ms)
#define POWER_TIMEOUT 60000 // Time until device automatically goes to sleep when turned on via button (ms)
#define FREQUENCY_MULTIPLIER 60000000

// ======== GPS Setup ========
TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
int GPSBaud = 9600;
String currentLocation, locationPayload;

// ======== Button Setup ========
ezButton button(BUTTON_PIN);
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

// ======== JSON Setup ========
// For getLocations
StaticJsonDocument<200> innerDoc;
StaticJsonDocument<200> outerDoc; 

String alias = "atlas4";

// ======== AutoConnect Setup ========

const char LOGIN[] PROGMEM = R"(
{
  "uri": "/login",
  "title": "Register Device",
  "menu": true,
  "element": [
    {
      "name": "header",
      "type": "ACText",
      "value": "<h2>Atlas Login</h2>",
      "style": "text-align:center;color:#2f4f4f;"
    },
    {
      "name": "username",
      "type": "ACInput",
      "value": "",
      "label": "Username"
    },
    {
      "name": "password",
      "type": "ACInput",
      "value": "",
      "label": "Password",
      "apply": "password"
    },
    {
      "name": "login",
      "type": "ACSubmit",
      "value": "Log In",
      "uri": "/login_check"
    }
  ]
}
)";

const char LOGIN_CHECK[] PROGMEM = R"(
{
  "uri": "/login_check",
  "title": "Login Result",
  "menu": false,
  "element": [
    {
      "name": "result",
      "type": "ACText",
      "value": "",
      "style": "text-align:center;color:#2f4f4f;"
    },
    {
      "name": "login_check",
      "type": "ACSubmit",
      "value": "",
      "uri": ""
    }
  ]
}
)";

const char REGISTER[] PROGMEM = R"(
{
  "uri": "/register",
  "title": "Device Registration",
  "menu": false,
  "element": [
    {
      "name": "deviceName",
      "type": "ACInput",
      "value": "",
      "label": "Device Name"
    },
    {
      "name": "deviceType",
      "type": "ACRadio",
      "label": "Device Type",
      "value": [
        "Bag",
        "Car",
        "Person"
      ],
      "arrange": "vertical",
      "checked": 1
    },
    {
      "name": "register",
      "type": "ACSubmit",
      "value": "Register",
      "uri": "/register_check"
    }
  ]
}
)";

const char REGISTER_CHECK[] PROGMEM = R"(
{
  "uri": "/register_check",
  "title": "Registration Result",
  "menu": false,
  "element": [
    {
      "name": "result",
      "type": "ACText",
      "value": "",
      "style": "text-align:center;color:#2f4f4f;"
    },
    {
      "name": "register_check",
      "type": "ACSubmit",
      "value": "Return to Home",
      "uri": "/_ac"
    }
  ]
}
)";

WebServer Server;

AutoConnect* portal = nullptr;
AutoConnectConfig config;
AutoConnectAux page_login, page_logincheck, page_register, page_registercheck;

// ======== State Management Setup ========
bool checkedRequests, checkedLocations, connectionAvailable;
int wakeupReason = -1;
Adafruit_MAX17048 maxlipo;

unsigned long startupTime;
unsigned long elapsedTime;
AutoConnectCredential cred;

RTC_DATA_ATTR int ownerId = 0;
RTC_DATA_ATTR float frequency = 0.25;

bool connectWiFi(const char* ssid, const char* password, unsigned long timeout) {
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(ssid, password);
  unsigned long tm = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - tm > timeout){
      return false;
    }
  }
  return true;
}

bool postLogin(String username, String password){
  StaticJsonDocument<200> loginJson;  
  StaticJsonDocument<200> responseJson;
  String payload = "";
  loginJson["email"] = username;
  loginJson["password"] = password;
  serializeJson(loginJson, payload);
  String response = httpsPOSTRequest("https://atlas-backend-app-9cd00bb1dd87.herokuapp.com/users/login", payload, 200);
  Serial.println(response);
  if (response != "-1"){
    DeserializationError error = deserializeJson(responseJson, response);
    // Test if parsing succeeds.
    if (error) {
      // Error lamp on
    } else {
      //Serial.println(responseJson["user"]["userId"]);
      ownerId = responseJson["user"]["userId"];
      Serial.print("Owner ID changed to : ");
      Serial.println(ownerId);
      return 1;
    }
  } else {
    return 0;
  }
}

bool postDevice(int userID, String name, String type){
  StaticJsonDocument<200> registerJson;  
  
  String payload = "";
  registerJson["id"] = alias;
  registerJson["userId"] = userID;
  registerJson["name"] = name;
  registerJson["itemType"] = type;
  serializeJson(registerJson, payload);
  String response = httpsPOSTRequest("https://atlas-backend-app-9cd00bb1dd87.herokuapp.com/devices/add/", payload, 201);

  if (response != "-1"){
    return 1;
  } else {
    return 0;
  }
}

String onLoginCheck(AutoConnectAux& aux, PageArgument& args) {
      
      String username = args.arg("username");
      String password = args.arg("password");
      if (postLogin(username, password) == 1){
        aux["result"].as<AutoConnectText>().value = "Login Successful.";
        aux["login_check"].as<AutoConnectSubmit>().value = "Enter Device Details";
        aux["login_check"].as<AutoConnectSubmit>().uri = "/register";
      } else {
        aux["result"].as<AutoConnectText>().value = "Incorrect Credentials.";
        aux["login_check"].as<AutoConnectSubmit>().value = "Return to Login Page";
        aux["login_check"].as<AutoConnectSubmit>().uri = "/login";
      }
      return String();
}

String onRegisterCheck(AutoConnectAux& aux, PageArgument& args) {
      
      String deviceName = args.arg("deviceName");
      String deviceType = args.arg("deviceType");

      if (postDevice(ownerId, deviceName, deviceType) == 1){
        aux["result"].as<AutoConnectText>().value = "Registration Successful.";
      } else {
        aux["result"].as<AutoConnectText>().value = "An error has occured during registration. Please try again.";
      }
      return String();
}

void getLocations() {
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid())
  {
    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 15);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 15);
    //Serial.print("Altitude: ");
    //Serial.println(gps.altitude.meters());
  }
  else
  {
    Serial.println("Location: Not Available");
  }
  
  Serial.print("Date: ");
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print("/");
    Serial.print(gps.date.day());
    Serial.print("/");
    Serial.println(gps.date.year());
  }
  else
  {
    Serial.println("Not Available");
  }

  Serial.print("Time: ");
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(":");
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(":");
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(".");
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.println(gps.time.centisecond());
  }
  else
  {
    Serial.println("Not Available");
  }
      if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
        JsonArray locationArray = innerDoc.to<JsonArray>();

        char JsonDate[80] = "";
        String output = "";

        // Create location object
        JsonObject location1 = locationArray.createNestedObject();

        // Insert values into array
        location1["deviceId"] = "atlas1";
        location1["latitude"] = gps.location.lat();
        location1["longitude"] = gps.location.lng();
        // Prepare and insert date/time in ISO8601 format
        sprintf(JsonDate, "%02d-%02d-%02dT%02d:%02d:%02d", gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
        location1["date"] = JsonDate;

        outerDoc["locations"] = locationArray;
        serializeJson(outerDoc, output);

        innerDoc.clear();
        outerDoc.clear();
        currentLocation = output;
        
        // Temporary
        locationPayload = currentLocation;
        checkedLocations = true;
      }
    }
  }
}

void sendLocations(String payload) {
  if(WiFi.status() == WL_CONNECTED) {
    httpsPOSTRequest("https://atlas-backend-app-9cd00bb1dd87.herokuapp.com/locations/add/", payload, 201);
  }
}

void checkRequests() {
  if(WiFi.status() == WL_CONNECTED) {
    StaticJsonDocument<200> requestJson;
    StaticJsonDocument<200> responseJson;  
    String request = httpsGETRequest("https://atlas-backend-app-9cd00bb1dd87.herokuapp.com/actions/get-action/atlas1");
    
    DeserializationError error = deserializeJson(requestJson, request);
    checkedRequests = true;
    // Test if parsing succeeds.
    if (error) {
      // Error lamp on
    } else {
      int temp = requestJson["value"];
      if (temp != 0){
        frequency = temp;
        String response = "";
        responseJson["deviceId"] = "atlas1";
        responseJson["frequency"] = frequency;
        serializeJson(responseJson, response);
        httpsPOSTRequest("https://atlas-backend-app-9cd00bb1dd87.herokuapp.com/devices/update-frequency", response, 200);
      }
    }
  }
}

void buttonHandler() {
    //Serial.println("Button handler called.");
  if (button.isReleased()){
    pressedTime = millis();
    Serial.println("Button pressed.");
  }

  if (button.isPressed()) {
    releasedTime = millis();
    Serial.println("Button released.");

    long pressDuration = releasedTime - pressedTime;
    Serial.println(pressDuration);

    if ( pressDuration < SHORT_PRESS_TIME ){
    rgbLedControl(0);
    delay(3000);
    esp_deep_sleep_start();
    }
      
    if ( pressDuration > LONG_PRESS_TIME ) {
      rgbLedControl(2);
      AutoConnectConfig config;
      config.immediateStart= true;
      if (!portal) {
        portal = new AutoConnect;
      }
      config.menuItems = config.menuItems | AC_MENUITEM_DELETESSID;
      portal->config(config);
      page_login.load(LOGIN);
      page_logincheck.load(LOGIN_CHECK);
      page_register.load(REGISTER);
      page_registercheck.load(REGISTER_CHECK);
      portal->join({ page_login, page_logincheck, page_register, page_registercheck });
      portal->on("/login_check", onLoginCheck);
      portal->on("/register_check", onRegisterCheck);
      if (portal->begin()) {
        while(1){
          portal->handleClient();
        }
        Serial.println("ending.");
        portal->end();
        delete portal;
        portal = nullptr;
      }
    }
  }
}

void powerLedControl(int mode){ // 0: Off, 1: On
  if (mode == 1){
    digitalWrite(POWER_LED_PIN, HIGH);
  } else {
    digitalWrite(POWER_LED_PIN, LOW);
  }
}

// 0: Off, 1: Red, 2: Orange, 3: Yellow, 4: Blue
void rgbLedControl(int mode){
  switch(mode){
    case 0:
      analogWrite(RGB_RED_PIN,   0);
      analogWrite(RGB_GREEN_PIN, 0);
      analogWrite(RGB_BLUE_PIN,  0);
      break;
    case 1:
      analogWrite(RGB_RED_PIN,   127);
      analogWrite(RGB_GREEN_PIN, 0);
      analogWrite(RGB_BLUE_PIN,  0);
      break;
    case 2:
      analogWrite(RGB_RED_PIN,   127);
      analogWrite(RGB_GREEN_PIN, 80);
      analogWrite(RGB_BLUE_PIN,  0);
      break;
    case 3:
      analogWrite(RGB_RED_PIN,   127);
      analogWrite(RGB_GREEN_PIN, 127);
      analogWrite(RGB_BLUE_PIN,  0);
      break;
    case 4:
      analogWrite(RGB_RED_PIN,   0);
      analogWrite(RGB_GREEN_PIN, 0);
      analogWrite(RGB_BLUE_PIN,  127);
      break;
  }
}

String httpsGETRequest(const char* serverName) {
  HTTPClient https;
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
    
  // Your Domain name with URL path or IP address with path
  https.begin(*client, serverName);
 
  Serial.print("Sending GET request to: ");
  Serial.println(serverName);
  // Send HTTP POST request
  int httpResponseCode = https.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = https.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  https.end();

  return payload;
}

String httpsPOSTRequest(const char* serverName, String message, int expectedResponse){
  HTTPClient https;
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
    
  // Your Domain name with URL path or IP address with path
  https.begin(*client, serverName);
  https.addHeader("Content-Type", "application/json");

  Serial.print("Sending POST request to: ");
  Serial.println(serverName);
  Serial.print("Content: ");
  Serial.println(message);

  // Send HTTP POST request
  int httpResponseCode = https.POST(message);
  
  String payload = "{}"; 
  
  if (httpResponseCode == expectedResponse) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = https.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    payload = "-1";
  }
  // Free resources
  https.end();

  return payload;
}

int getWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : return 1;
    case ESP_SLEEP_WAKEUP_TIMER : return 0;
    default : return -1;
  }
}

void setup()
{
  Serial.begin(115200);
  gpsSerial.begin(GPSBaud);
  pinMode(POWER_LED_PIN, OUTPUT);
  pinMode(RGB_RED_PIN,   OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN,  OUTPUT);
  button.setDebounceTime(50);
  // ======
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_10, 1); // Change if needed (GPIO_NUM_x)
  // ======
  esp_sleep_enable_timer_wakeup(frequency * FREQUENCY_MULTIPLIER);
  checkedRequests = false;
  connectionAvailable = false;
  wakeupReason = getWakeupReason();

  if (!maxlipo.begin()) {
    Serial.println(F("Couldnt find Adafruit MAX17048?\nMake sure a battery is plugged in!"));
    while (1) delay(10);
  }
  delay(3000);
  Serial.print(F("Found MAX17048"));
  Serial.print(F(" with Chip ID: 0x")); 
  Serial.println(maxlipo.getChipID(), HEX);

  Serial.print(F("Batt Voltage: ")); Serial.print(maxlipo.cellVoltage(), 3); Serial.println(" V");
  Serial.print(F("Batt Percent: ")); Serial.print(maxlipo.cellPercent(), 1); Serial.println(" %");
  Serial.println();
  if (maxlipo.cellPercent() > 15){
    powerLedControl(1);
  } else {
    powerLedControl(0);
  }

  AutoConnectCredential credt;
  station_config_t  config;
  for (int8_t e = 0; e < credt.entries(); e++) {
    credt.load(e, &config);
    if (connectWiFi((char*)config.ssid, (char*)config.password, WIFI_TIMEOUT)){
      connectionAvailable = true;
      break;
    }
  }

  if(connectionAvailable == true){
    rgbLedControl(4);
  } else {
    rgbLedControl(1);
  }

  startupTime = millis();
  Serial.println("Setup complete.");
}

void loop()
{
  button.loop();
  //Serial.println("Loop started.");
  
  // No WiFi connection available
  if((connectionAvailable == false) && (wakeupReason == 0)){
    Serial.println("Connection unavailable, timer wakeup.");
    // Store to SD Card
    //getLocations();
    //storeLocations();
    rgbLedControl(0);
    delay(3000);
    esp_deep_sleep_start();
  }

  if((connectionAvailable == false) && (wakeupReason == 1)){
    Serial.println("Connection unavailable, button wakeup.");
  }
    
  if ((connectionAvailable == true) && (wakeupReason == 0)){
    Serial.println("Connection available, timer wakeup.");
    while (checkedLocations == false) {
      elapsedTime = millis() - startupTime;
      getLocations();
      buttonHandler();
      if (elapsedTime > GPS_TIMEOUT && checkedLocations == false){
        rgbLedControl(0);
        delay(3000);
        esp_deep_sleep_start();
      }
    }
    rgbLedControl(3);
    sendLocations(locationPayload);
    checkRequests();
    delay(1000);
    rgbLedControl(0);
    delay(3000);
    esp_deep_sleep_start();
  }
  
  if ((connectionAvailable == true) && (wakeupReason == 1) && (checkedRequests == false)){

    Serial.println("Connection available, button wakeup.");
    checkRequests();
  }

  buttonHandler();

  elapsedTime = millis() - startupTime;
  if (elapsedTime > POWER_TIMEOUT)
    esp_deep_sleep_start();

}

