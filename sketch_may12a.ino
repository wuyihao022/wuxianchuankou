#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

#define MOSFET_PIN     15
#define MOSFET_ON      1
#define MOSFET_OFF     0
#define LED_ON         0
#define LED_OFF        1
#define AP_PASSWORD    "12345678"
#define UDP_PORT       8266
#define BLINK_SLOW     1000  // 慢闪 1000ms
#define BLINK_FAST     200   // 快闪 200ms

// 动态生成的设备信息
String deviceID = "";      // 基于MAC地址的设备ID
String apSSID = "";        // 动态生成的AP SSID

// WiFi configuration
String wifiSSID = "";      // Will be loaded from config file
String wifiPassword = "";  // Will be loaded from config file

// Server for telnet
WiFiServer server(23);
ESP8266WebServer webServer(80);
WiFiUDP udp;

// Serial parameters
String baudRateStr = "9600";
String parityStr = "N";

// LED状态管理
enum LED_State {
  LED_WIFI_CONNECTED,    // 常亮 - WiFi已连接
  LED_WIFI_DISCONNECTED, // 慢闪 - WiFi未连接
  LED_DATA_ACTIVITY      // 快闪 - 数据传输中
};

LED_State currentLedState = LED_WIFI_DISCONNECTED;
unsigned long lastLedToggle = 0;
bool ledOn = false;
bool dataActivity = false;
unsigned long lastDataActivity = 0;
unsigned long lastUdpBroadcast = 0;

// 初始化设备ID和AP名称
void initDeviceInfo() {
  // 获取MAC地址
  byte mac[6];
  WiFi.macAddress(mac);
  
  // 创建MAC地址后缀 (最后4位十六进制)
  char macStr[7];
  sprintf(macStr, "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  
  // 设备ID
  deviceID = "ESP-" + String(macStr);
  
  // AP SSID
  apSSID = "ESP8266-AP-" + String(macStr);
  
  Serial.println("Device ID: " + deviceID);
  Serial.println("AP SSID: " + apSSID);
}

void updateLED() {
  unsigned long now = millis();
  
  // 检查数据活动指示
  if (dataActivity) {
    if (now - lastDataActivity > 500) { // 数据活动超时
      dataActivity = false;
      // 恢复到WiFi状态
      currentLedState = (WiFi.status() == WL_CONNECTED) ? LED_WIFI_CONNECTED : LED_WIFI_DISCONNECTED;
    } else {
      currentLedState = LED_DATA_ACTIVITY;
    }
  }
  
  // 根据当前状态控制LED
  switch (currentLedState) {
    case LED_WIFI_CONNECTED:
      digitalWrite(LED_BUILTIN, LED_ON); // 常亮
      break;
      
    case LED_WIFI_DISCONNECTED:
      // 慢闪 (1000ms)
      if (now - lastLedToggle >= BLINK_SLOW) {
        ledOn = !ledOn;
        digitalWrite(LED_BUILTIN, ledOn ? LED_ON : LED_OFF);
        lastLedToggle = now;
      }
      break;
      
    case LED_DATA_ACTIVITY:
      // 快闪 (200ms)
      if (now - lastLedToggle >= BLINK_FAST) {
        ledOn = !ledOn;
        digitalWrite(LED_BUILTIN, ledOn ? LED_ON : LED_OFF);
        lastLedToggle = now;
      }
      break;
  }
}

void indicateDataActivity() {
  dataActivity = true;
  lastDataActivity = millis();
}

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
      // 触发数据活动指示
      indicateDataActivity();
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

// Web API处理函数 - JSON格式返回设备状态
void handleApi() {
  // 触发数据活动指示
  indicateDataActivity();
  
  // 创建JSON响应
  DynamicJsonDocument doc(1024);
  
  // 设备信息
  JsonObject device = doc.createNestedObject("device");
  device["id"] = deviceID;
  device["ap_ssid"] = apSSID;
  
  // WiFi状态
  JsonObject status = doc.createNestedObject("status");
  status["wifi_mode"] = (WiFi.getMode() == WIFI_AP) ? "AP" : 
                      (WiFi.getMode() == WIFI_STA) ? "STA" : 
                      (WiFi.getMode() == WIFI_AP_STA) ? "AP+STA" : "OFF";
  status["connected"] = (WiFi.status() == WL_CONNECTED);
  
  if (WiFi.status() == WL_CONNECTED) {
    status["ssid"] = WiFi.SSID();
    status["rssi"] = WiFi.RSSI();
    status["ip"] = WiFi.localIP().toString();
  }
  
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    status["ap_ip"] = WiFi.softAPIP().toString();
  }
  
  // 串口配置
  JsonObject serial = doc.createNestedObject("serial");
  serial["baudrate"] = baudRateStr;
  serial["parity"] = parityStr;
  
  // 转换为字符串并发送
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

// API接口 - 设置WiFi配置
void handleApiSetWifi() {
  indicateDataActivity();
  
  DynamicJsonDocument doc(256);
  bool success = false;
  String message;
  
  if (webServer.hasArg("plain")) {
    String body = webServer.arg("plain");
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      if (doc.containsKey("ssid")) {
        wifiSSID = doc["ssid"].as<String>();
        if (doc.containsKey("password")) {
          wifiPassword = doc["password"].as<String>();
          saveWiFiConfig();
          success = true;
          message = "WiFi configuration saved";
        } else {
          message = "Missing password parameter";
        }
      } else {
        message = "Missing SSID parameter";
      }
    } else {
      message = "Invalid JSON format";
    }
  } else {
    message = "No data received";
  }
  
  // 创建JSON响应
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["message"] = message;
  
  String responseStr;
  serializeJson(response, responseStr);
  webServer.send(200, "application/json", responseStr);
}

// API接口 - 设置串口配置
void handleApiSetSerial() {
  indicateDataActivity();
  
  DynamicJsonDocument doc(256);
  bool success = false;
  String message;
  
  if (webServer.hasArg("plain")) {
    String body = webServer.arg("plain");
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      bool paramsChanged = false;
      
      if (doc.containsKey("baudrate")) {
        baudRateStr = doc["baudrate"].as<String>();
        paramsChanged = true;
      }
      
      if (doc.containsKey("parity")) {
        String newParity = doc["parity"].as<String>();
        if (newParity == "N" || newParity == "E" || newParity == "O") {
          parityStr = newParity;
          paramsChanged = true;
        } else {
          message = "Invalid parity value (use N, E, or O)";
          success = false;
        }
      }
      
      if (paramsChanged) {
        saveSerialConfig();
        setupSerial();
        success = true;
        message = "Serial configuration saved";
      } else {
        message = "No parameters changed";
      }
    } else {
      message = "Invalid JSON format";
    }
  } else {
    message = "No data received";
  }
  
  // 创建JSON响应
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["message"] = message;
  
  String responseStr;
  serializeJson(response, responseStr);
  webServer.send(200, "application/json", responseStr);
}

// API接口 - 重启设备
void handleApiRestart() {
  indicateDataActivity();
  
  DynamicJsonDocument response(128);
  response["success"] = true;
  response["message"] = "Device restarting...";
  
  String responseStr;
  serializeJson(response, responseStr);
  webServer.send(200, "application/json", responseStr);
  
  // 延迟一段时间后重启
  delay(1000);
  ESP.restart();
}

void handleRoot()
{
  // 触发数据活动指示
  indicateDataActivity();
  
  String html = "<html><head><title>ESP8266 Configuration</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;} input,select{width:100%;padding:6px;margin:6px 0;} button{background-color:#4CAF50;color:white;padding:8px 16px;border:none;cursor:pointer;} .info{background-color:#f0f0f0;padding:10px;border-radius:5px;margin-bottom:15px;} .status{font-weight:bold;}</style>";
  html += "<script>function refreshPage(){setTimeout(function(){location.reload();},30000);} window.onload=refreshPage;</script>";
  html += "</head><body>";
  
  // 显示设备ID
  html += "<div class='info'>";
  html += "<h2>Device Information</h2>";
  html += "<p>Device ID: <span class='status'>" + deviceID + "</span></p>";
  html += "</div>";
  
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
    html += "<p>Access Point SSID: <span class='status'>" + apSSID + "</span></p>";
    html += "<p>AP IP Address: <span class='status'>" + WiFi.softAPIP().toString() + "</span></p>";
  }
  
  // Show current port info
  html += "<p>Telnet Server: <span class='status'>Port 23</span></p>";
  html += "<p>Web Server: <span class='status'>Port 80</span></p>";
  html += "<p>UDP Broadcast: <span class='status'>Port " + String(UDP_PORT) + "</span></p>";
  
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
  
  // API信息
  html += "<div class='info' style='margin-top:20px;'>";
  html += "<h2>API Endpoints</h2>";
  html += "<p><code>/api</code> - Get device status (GET)</p>";
  html += "<p><code>/api/wifi</code> - Configure WiFi (POST)</p>";
  html += "<p><code>/api/serial</code> - Configure Serial (POST)</p>";
  html += "<p><code>/api/restart</code> - Restart device (POST)</p>";
  html += "</div>";
  
  html += "<p><small>Page auto-refreshes every 30 seconds</small></p>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}
void handleApiReset() {
  indicateDataActivity();
  
  DynamicJsonDocument response(128);
  response["success"] = true;
  response["message"] = "Device configuration reset...";
  
  String responseStr;
  serializeJson(response, responseStr);
  webServer.send(200, "application/json", responseStr);
  
  // 删除配置文件
  if (LittleFS.begin()) {
    if (LittleFS.exists("/wifi.txt")) {
      LittleFS.remove("/wifi.txt");
    }
    if (LittleFS.exists("/serial.txt")) {
      LittleFS.remove("/serial.txt");
    }
    LittleFS.end();
  }
  
  // 设置默认值
  wifiSSID = "";
  wifiPassword = "";
  baudRateStr = "9600";
  parityStr = "N";
  
  // 重启设备
  delay(1000);
  ESP.restart();
}

void handleSave()
{
  // 触发数据活动指示
  indicateDataActivity();
  
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
  // 触发数据活动指示
  indicateDataActivity();
  
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
  // 触发数据活动指示
  indicateDataActivity();
  
  webServer.send(200, "text/html", "<html><body><h2>Device restarting...</h2><script>setTimeout(function(){window.location.href='/';},5000);</script></body></html>");
  delay(1000);
  ESP.restart();
}

// 发送UDP广播的函数
void broadcastUDP() {
  unsigned long now = millis();
  if (now - lastUdpBroadcast > 5000) { // 每5秒广播一次
    lastUdpBroadcast = now;
    
    // 创建JSON数据
    DynamicJsonDocument doc(256);
    doc["device_id"] = deviceID;
    doc["ap_ssid"] = apSSID;
    doc["wifi_mode"] = (WiFi.getMode() == WIFI_AP) ? "AP" : 
                      (WiFi.getMode() == WIFI_AP_STA) ? "AP+STA" : "STA";
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    
    if (WiFi.status() == WL_CONNECTED) {
      doc["sta_ip"] = WiFi.localIP().toString();
      doc["ssid"] = WiFi.SSID();
    }
    
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
      doc["ap_ip"] = WiFi.softAPIP().toString();
    }
    
    // 转换为字符串
    String message;
    serializeJson(doc, message);
    
    // 在AP模式下，同时发送到AP网络和STA网络（如果连接）
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
      // AP网络广播（192.168.4.255）
      udp.beginPacket(IPAddress(192,168,4,255), UDP_PORT);
      udp.write(message.c_str(), message.length());
      udp.endPacket();
      // Serial.println("UDP Broadcast to AP network: " + message);
    }
    
    // 在STA模式下，或双模式下，发送全网广播
    if (WiFi.status() == WL_CONNECTED) {
      // 计算STA接口的广播地址
      IPAddress broadcastIP = calculateBroadcast(WiFi.localIP(), WiFi.subnetMask());
      udp.beginPacket(broadcastIP, UDP_PORT);
      udp.write(message.c_str(), message.length());
      udp.endPacket();
      // Serial.println("UDP Broadcast to STA network: " + message);
    }
    
    // 全网广播，作为备用
    udp.beginPacket("255.255.255.255", UDP_PORT);
    udp.write(message.c_str(), message.length());
    udp.endPacket();
    // Serial.println("UDP Broadcast global: " + message);
  }
}

// 计算广播地址的辅助函数
IPAddress calculateBroadcast(IPAddress ip, IPAddress subnet) {
  IPAddress broadcast;
  for (int i = 0; i < 4; i++) {
    broadcast[i] = ip[i] | (~subnet[i] & 255);
  }
  return broadcast;
}

void setupWiFi() {
  // 首先设置为双模式，但暂不连接
  WiFi.mode(WIFI_AP_STA);
  
  // 先启动AP模式
  Serial.println("Starting AP mode...");
  
  // 详细配置AP，提高稳定性
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(apSSID.c_str(), AP_PASSWORD, 6, false, 4);
  
  Serial.print("AP Started: ");
  Serial.println(apSSID);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // 等待AP稳定
  delay(500);
  
  // 然后尝试连接WiFi
  bool wifiConnected = false;
  
  // 检查是否有保存的WiFi凭据
  if (wifiSSID.length() > 0) {
    Serial.print("Connecting to ");
    Serial.print(wifiSSID);
    
    // 开始连接
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    
    // 等待连接，最多10秒
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
      
      // 设置LED状态为连接状态
      currentLedState = LED_WIFI_CONNECTED;
    } else {
      Serial.println("\nFailed to connect to WiFi.");
      // 设置LED状态为未连接状态
      currentLedState = LED_WIFI_DISCONNECTED;
    }
  } else {
    Serial.println("No saved WiFi credentials. Operating in AP mode only.");
    currentLedState = LED_WIFI_DISCONNECTED;
  }
  
  // 输出操作模式
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
  
  // 初始化设备信息 (在任何网络操作之前)
  initDeviceInfo();
  
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
  
  // 初始化UDP
  udp.begin(UDP_PORT);
  Serial.println("UDP Broadcast initialized on port " + String(UDP_PORT));
  
  // Configure web server
  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/savewifi", HTTP_POST, handleSaveWiFi);
  webServer.on("/restart", HTTP_POST, handleRestart);
  
  // 配置API接口
  webServer.on("/api", HTTP_GET, handleApi);
  webServer.on("/api/wifi", HTTP_POST, handleApiSetWifi);
  webServer.on("/api/serial", HTTP_POST, handleApiSetSerial);
  webServer.on("/api/restart", HTTP_POST, handleApiRestart);
  webServer.on("/api/reset", HTTP_POST, handleApiReset);

  webServer.begin();
  Serial.println("HTTP server started");
  
  // Start telnet server
  server.begin();
  Serial.println("Telnet server started");
}

void loop()
{
  // 更新LED状态
  updateLED();
  
  // 发送UDP广播
  broadcastUDP();
  
  // Handle HTTP requests
  webServer.handleClient();
  
  // Optional: Check WiFi status periodically and attempt reconnect if disconnected
  static unsigned long lastWifiCheck = 0;
  if (WiFi.getMode() == WIFI_STA && millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost, attempting to reconnect...");
      WiFi.reconnect();
      currentLedState = LED_WIFI_DISCONNECTED; // 更新LED状态
    } else {
      currentLedState = LED_WIFI_CONNECTED; // 确保LED状态正确
    }
  }
  
  // Handle telnet client
  WiFiClient client = server.available();
  if (client) {
    digitalWrite(MOSFET_PIN, MOSFET_ON);
    indicateDataActivity(); // 触发数据活动指示
    client.setNoDelay(true);
    Serial.println("New client connected");
    
    while (client.connected()) {
      // From client to Serial
      if (client.available()) {
        char ch = client.read();
        Serial.write(ch);
        STC_Auto_ISP(ch);
        indicateDataActivity(); // 触发数据活动指示
      }
      
      // From Serial to client
      if (Serial.available()) {
        String s = Serial.readString();
        client.print(s);
        indicateDataActivity(); // 触发数据活动指示
      }
      
      // Handle HTTP requests while in telnet session
      webServer.handleClient();
      
      // 更新LED状态
      updateLED();
        
      // 添加此行：即使在TCP会话中也广播UDP
      broadcastUDP();
      
      // If client disconnected, break loop
      if (!client.connected()) {
        break;
      }
    }
    
    client.stop();
    Serial.println("Client disconnected");
    
    // 恢复到WiFi状态
    currentLedState = (WiFi.status() == WL_CONNECTED) ? LED_WIFI_CONNECTED : LED_WIFI_DISCONNECTED;
  }
}
