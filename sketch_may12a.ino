#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>

#define MOSFET_PIN  15
#define MOSFET_ON   1
#define MOSFET_OFF  0
#define LED_ON      0
#define LED_OFF     1
#define AP_SSID     "ESP8266-AP"
#define AP_PASSWORD "12345678"

// WiFi configuration
String wifiSSID = "";      // Will be loaded from config file
String wifiPassword = "";  // Will be loaded from config file

// Server for telnet
WiFiServer server(23);
ESP8266WebServer webServer(80);

// Serial parameters
String baudRateStr = "9600";
String parityStr = "N";

void STC_Auto_ISP(char ch)
{
  static uint8_t STC_7F_Count=0;
  if (0x7F==ch)
  {
    STC_7F_Count++;
    if (STC_7F_Count>=50)
    {
      STC_7F_Count=0;
      digitalWrite(MOSFET_PIN, !digitalRead(MOSFET_PIN));
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  }
  else
  {
    STC_7F_Count=0;
  }
}

bool loadSerialConfig()
{
  if (LittleFS.begin())
  {
    if (LittleFS.exists("/serial.txt"))
    {
      File file = LittleFS.open("/serial.txt", "r");
      if (file)
      {
        baudRateStr = file.readStringUntil('\n');
        baudRateStr.trim();
        parityStr = file.readStringUntil('\n');
        parityStr.trim();
        file.close();
        Serial.println("Loaded config: Baud=" + baudRateStr + ", Parity=" + parityStr);
        return true;
      }
    }
    LittleFS.end();
  }
  return false;
}

bool saveSerialConfig()
{
  if (LittleFS.begin())
  {
    File file = LittleFS.open("/serial.txt", "w");
    if (file)
    {
      file.println(baudRateStr);
      file.println(parityStr);
      file.close();
      LittleFS.end();
      Serial.println("Saved config: Baud=" + baudRateStr + ", Parity=" + parityStr);
      return true;
    }
    LittleFS.end();
  }
  return false;
}

bool loadWiFiConfig() {
  if (LittleFS.begin()) {
    if (LittleFS.exists("/wifi.txt")) {
      File file = LittleFS.open("/wifi.txt", "r");
      if (file) {
        wifiSSID = file.readStringUntil('\n');
        wifiSSID.trim();
        wifiPassword = file.readStringUntil('\n');
        wifiPassword.trim();
        file.close();
        Serial.println("Loaded WiFi config: SSID=" + wifiSSID);
        return true;
      }
    }
    LittleFS.end();
  }
  return false;
}

bool saveWiFiConfig() {
  if (LittleFS.begin()) {
    File file = LittleFS.open("/wifi.txt", "w");
    if (file) {
      file.println(wifiSSID);
      file.println(wifiPassword);
      file.close();
      LittleFS.end();
      Serial.println("Saved WiFi config: SSID=" + wifiSSID);
      return true;
    }
    LittleFS.end();
  }
  return false;
}

void setupSerial()
{
  unsigned long baud = baudRateStr.toInt();
  if (baud == 0) baud = 9600;  // Default if conversion fails
  
  SerialConfig config = SERIAL_8N1;
  if (parityStr == "E")
  {
    config = SERIAL_8E1;
  }
  else if (parityStr == "O")
  {
    config = SERIAL_8O1;
  }
  else
  {
    config = SERIAL_8N1;
  }
  
  Serial.begin(baud, config);
  Serial.setTimeout(1);
}

void handleRoot()
{
  String html = "<html><head><title>ESP8266 STC ISP Configuration</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;} input,select{width:100%;padding:6px;margin:6px 0;} button{background-color:#4CAF50;color:white;padding:8px 16px;border:none;cursor:pointer;} .info{background-color:#f0f0f0;padding:10px;border-radius:5px;margin-bottom:15px;} .status{font-weight:bold;}</style>";
  html += "<script>function refreshPage(){setTimeout(function(){location.reload();},30000);} window.onload=refreshPage;</script>";
  html += "</head><body>";
  
  // Display connection status and network information
  html += "<div class='info'>";
  html += "<h2>Connection Status</h2>";
  
  // Show WiFi mode
  html += "<p>WiFi Mode: <span class='status'>";
  switch(WiFi.getMode()) {
    case WIFI_OFF: html += "OFF"; break;
    case WIFI_STA: html += "Station (Client)"; break;
    case WIFI_AP: html += "Access Point"; break;
    case WIFI_AP_STA: html += "AP + Station"; break;
    default: html += "Unknown"; break;
  }
  html += "</span></p>";
  
  // Show station connection info if connected
  if (WiFi.status() == WL_CONNECTED) {
    html += "<p>Connected to: <span class='status'>" + WiFi.SSID() + "</span></p>";
    html += "<p>Signal Strength: <span class='status'>" + String(WiFi.RSSI()) + " dBm</span></p>";
    html += "<p>IP Address: <span class='status'>" + WiFi.localIP().toString() + "</span></p>";
    html += "<p>Subnet Mask: <span class='status'>" + WiFi.subnetMask().toString() + "</span></p>";
    html += "<p>Gateway IP: <span class='status'>" + WiFi.gatewayIP().toString() + "</span></p>";
  } else {
    html += "<p>WiFi Status: <span class='status'>Not Connected</span></p>";
  }
  
  // Show AP info if in AP mode
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    html += "<p>Access Point SSID: <span class='status'>" + String(AP_SSID) + "</span></p>";
    html += "<p>AP IP Address: <span class='status'>" + WiFi.softAPIP().toString() + "</span></p>";
  }
  
  // Show current port info
  html += "<p>Telnet Server: <span class='status'>Port 23</span></p>";
  html += "<p>Web Server: <span class='status'>Port 80</span></p>";
  
  html += "</div>";
  
  // WiFi Configuration
  html += "<h2>WiFi Configuration</h2>";
  html += "<form method='post' action='/savewifi'>";
  html += "SSID: <input type='text' name='ssid' value='" + wifiSSID + "'><br>";
  html += "Password: <input type='password' name='password' value='" + wifiPassword + "'><br>";
  html += "<button type='submit'>Save WiFi Settings</button>";
  html += "</form><br>";
  
  // Serial Configuration
  html += "<h2>Serial Configuration</h2>";
  html += "<form method='post' action='/save'>";
  html += "Baudrate: <input type='text' name='baudrate' value='" + baudRateStr + "'><br>";
  html += "Parity: <select name='parity'>";
  html += "<option value='N'";
  if (parityStr == "N") html += " selected";
  html += ">None</option>";
  html += "<option value='E'";
  if (parityStr == "E") html += " selected";
  html += ">Even</option>";
  html += "<option value='O'";
  if (parityStr == "O") html += " selected";
  html += ">Odd</option>";
  html += "</select><br><br>";
  html += "<button type='submit'>Save Serial Settings</button>";
  html += "</form>";
  
  html += "<br><form method='post' action='/restart'>";
  html += "<button type='submit' style='background-color:#FF5722;'>Restart Device</button>";
  html += "</form>";
  
  html += "<p><small>Page auto-refreshes every 30 seconds</small></p>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void handleSave()
{
  if (webServer.hasArg("baudrate"))
  {
    baudRateStr = webServer.arg("baudrate");
  }
  if (webServer.hasArg("parity"))
  {
    parityStr = webServer.arg("parity");
  }
  
  saveSerialConfig();
  setupSerial();
  
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

void handleSaveWiFi() {
  if (webServer.hasArg("ssid")) {
    wifiSSID = webServer.arg("ssid");
  }
  if (webServer.hasArg("password")) {
    wifiPassword = webServer.arg("password");
  }
  
  saveWiFiConfig();
  
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

void handleRestart() {
  webServer.send(200, "text/html", "<html><body><h2>Device restarting...</h2><script>setTimeout(function(){window.location.href='/';},5000);</script></body></html>");
  delay(1000);
  ESP.restart();
}

void setupWiFi() {
  // First try to connect to configured WiFi
  WiFi.mode(WIFI_STA);
  
  bool wifiConnected = false;
  
  // Use stored credentials if available
  if (wifiSSID.length() > 0) {
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    Serial.print("Connecting to ");
    Serial.print(wifiSSID);
  } else {
    // Fallback to hardcoded credentials if no stored ones
    WiFi.begin("", "");
    Serial.print("No saved WiFi credentials. Trying to connect anyway");
  }
  
  // Wait for connection for 10 seconds
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(wifiSSID);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
  
  // No matter if WiFi connection succeeded or failed, always start AP mode
  Serial.println("Starting AP mode...");
  WiFi.mode(WIFI_AP_STA); // Use both AP and STA modes
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP Started: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  if (!wifiConnected) {
    Serial.println("Device is operating in AP mode only");
  } else {
    Serial.println("Device is operating in both WiFi client and AP modes");
  }
}

void setup()
{
  // Setup GPIO pins
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, MOSFET_ON);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF);
  
  // Initialize serial at 115200 first for debug output
  Serial.begin(115200);
  Serial.println("\nStarting...");
  
  // Initialize filesystem and load config
  if (!LittleFS.begin())
  {
    Serial.println("Failed to mount filesystem");
  }
  else
  {
    LittleFS.end();
  }
  
  loadSerialConfig();
  loadWiFiConfig();
  setupSerial();
  
  // Setup WiFi
  setupWiFi();
  
  // Configure web server
  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/savewifi", HTTP_POST, handleSaveWiFi);
  webServer.on("/restart", HTTP_POST, handleRestart);
  webServer.begin();
  Serial.println("HTTP server started");
  
  // Start telnet server
  server.begin();
  Serial.println("Telnet server started");
}

void loop()
{
  // Handle HTTP requests
  webServer.handleClient();
  
  // Optional: Check WiFi status periodically and attempt reconnect if disconnected
  static unsigned long lastWifiCheck = 0;
  if (WiFi.getMode() == WIFI_STA && millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost, attempting to reconnect...");
      WiFi.reconnect();
    }
  }
  
  // Handle telnet client
  WiFiClient client = server.available();
  if (client) {
    digitalWrite(MOSFET_PIN, MOSFET_ON);
    digitalWrite(LED_BUILTIN, LED_ON);
    client.setNoDelay(true);
    Serial.println("New client connected");
    
    while (client.connected()) {
      // From client to Serial
      if (client.available()) {
        char ch = client.read();
        Serial.write(ch);
        STC_Auto_ISP(ch);
      }
      
      // From Serial to client
      if (Serial.available()) {
        String s = Serial.readString();
        client.print(s);
      }
      
      // Handle HTTP requests while in telnet session
      webServer.handleClient();
      
      // If client disconnected, break loop
      if (!client.connected()) {
        break;
      }
    }
    
    client.stop();
    Serial.println("Client disconnected");
    digitalWrite(LED_BUILTIN, LED_OFF);
  }
}
