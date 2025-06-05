#include "esp32-hal.h"
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <map>
#include <ArduinoJson.h>

// WiFi配置
String ssid = "";
String password = "";
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket服务器在81端口
long connectWebTime;
bool webServerConnected = false;

// WebSocket客户端管理
std::map<uint8_t, bool> connectedClients;
std::map<uint8_t, unsigned long> lastHeartbeat; // 记录每个客户端的最后心跳时间
const unsigned long HEARTBEAT_INTERVAL = 10000; // 心跳间隔10秒
const unsigned long HEARTBEAT_TIMEOUT = 15000;  // 心跳超时15秒

// 异步任务管理
struct WebTask
{
  String taskId;
  String command;
  String status; // "pending", "running", "completed", "error"
  String result;
  unsigned long timestamp;
  unsigned long startTime;
  bool resultReady;
  uint8_t clientId; // 添加客户端ID
};

std::map<String, WebTask> webTasks;
String currentWebTaskId = "";
bool webTaskActive = false;

// 函数声明
String generateTaskId();
void startWebTask(String taskId);
void completeWebTask();
void errorWebTask(String errorMessage);
void processNextWebTask();
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void checkHeartbeats();

// 生成任务ID
String generateTaskId()
{
  return String(millis()) + "_" + String(esp_random() % 1000);
}

// WebSocket事件处理
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      connectedClients.erase(num);
      lastHeartbeat.erase(num);
      PTHL("WebSocket client disconnected: ", num);
      break;
      
    case WStype_CONNECTED:
      connectedClients[num] = true;
      lastHeartbeat[num] = millis();
      PTHL("WebSocket client connected: ", num);
      break;
      
    case WStype_TEXT: {
      String message = String((char*)payload);
      
      // 解析 JSON 消息
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, message);
      
      if (error) {
        // JSON 解析错误，发送错误响应
        StaticJsonDocument<256> errorDoc;
        errorDoc["type"] = "error";
        errorDoc["error"] = "Invalid JSON format";
        String errorResponse;
        serializeJson(errorDoc, errorResponse);
        webSocket.sendTXT(num, errorResponse);
        return;
      }

      // 处理心跳消息
      if (doc["type"] == "heartbeat") {
        lastHeartbeat[num] = millis();
        StaticJsonDocument<128> response;
        response["type"] = "heartbeat";
        response["timestamp"] = millis();
        String responseStr;
        serializeJson(response, responseStr);
        webSocket.sendTXT(num, responseStr);
        return;
      }

      // 处理命令消息
      if (doc["type"] == "command") {
        String command = doc["command"].as<String>();
        String taskId = doc["taskId"].as<String>();
        
        // 更新心跳时间
        lastHeartbeat[num] = millis();
        
        // 创建任务记录
        WebTask task;
        task.taskId = taskId;
        task.command = command;
        task.status = "pending";
        task.result = "";
        task.timestamp = millis();
        task.startTime = 0;
        task.resultReady = false;
        task.clientId = num;
        
        // 存储任务
        webTasks[taskId] = task;
        
        // 如果当前没有活跃的web任务，立即开始执行
        if (!webTaskActive) {
          startWebTask(taskId);
        }
        
        // 发送任务开始响应
        StaticJsonDocument<256> startDoc;
        startDoc["type"] = "response";
        startDoc["taskId"] = taskId;
        startDoc["status"] = "running";
        String startResponse;
        serializeJson(startDoc, startResponse);
        webSocket.sendTXT(num, startResponse);
        
        PTHL("web command async: ", command);
        PTHL("task ID: ", taskId);
      }
      break;
    }
  }
}

// 检查心跳超时
void checkHeartbeats() {
  unsigned long currentTime = millis();
  for (auto it = lastHeartbeat.begin(); it != lastHeartbeat.end();) {
    if (currentTime - it->second > HEARTBEAT_TIMEOUT) {
      uint8_t clientId = it->first;
      PTHL("Client heartbeat timeout: ", clientId);
      webSocket.disconnect(clientId);
      connectedClients.erase(clientId);
      it = lastHeartbeat.erase(it);
    } else {
      // 发送心跳消息
      StaticJsonDocument<128> heartbeatDoc;
      heartbeatDoc["type"] = "heartbeat";
      heartbeatDoc["timestamp"] = currentTime;
      String heartbeatMsg;
      serializeJson(heartbeatDoc, heartbeatMsg);
      webSocket.sendTXT(it->first, heartbeatMsg);
      ++it;
    }
  }
}

// 开始执行web任务
void startWebTask(String taskId)
{
  if (webTasks.find(taskId) == webTasks.end()) {
    return;
  }

  WebTask &task = webTasks[taskId];
  String webCmd = task.command;

  // 设置全局标志和命令
  cmdFromWeb = true;
  currentWebTaskId = taskId;
  webTaskActive = true;
  webResponse = ""; // 清空响应缓冲区

  // 解析命令
  token = webCmd[0];
  strcpy(newCmd, webCmd.c_str() + 1);
  cmdLen = strlen(newCmd);
  newCmd[cmdLen + 1] = '\0';
  newCmdIdx = 4;

  // 更新任务状态
  task.status = "running";
  task.startTime = millis();

  // 通知客户端任务开始
  StaticJsonDocument<256> statusDoc;
  statusDoc["type"] = "response";
  statusDoc["taskId"] = taskId;
  statusDoc["status"] = "running";
  String statusMsg;
  serializeJson(statusDoc, statusMsg);
  webSocket.sendTXT(task.clientId, statusMsg);

  PTHL("starting web task: ", taskId);
}

// 完成web任务
void completeWebTask()
{
  if (!webTaskActive || currentWebTaskId == "") {
    return;
  }

  if (webTasks.find(currentWebTaskId) != webTasks.end()) {
    WebTask &task = webTasks[currentWebTaskId];
    task.status = "completed";
    task.result = webResponse;
    task.resultReady = true;

    // 发送完成状态给客户端
    StaticJsonDocument<512> completeDoc;
    completeDoc["type"] = "response";
    completeDoc["taskId"] = currentWebTaskId;
    completeDoc["status"] = "completed";
    completeDoc["result"] = webResponse;
    String statusMsg;
    serializeJson(completeDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);

    PTHL("web task completed: ", currentWebTaskId);
    PTHL("result length: ", task.result.length());
  }

  // 重置全局状态
  cmdFromWeb = false;
  webTaskActive = false;
  currentWebTaskId = "";

  // 检查是否有等待的任务
  processNextWebTask();
}

// Web任务错误处理
void errorWebTask(String errorMessage)
{
  if (!webTaskActive || currentWebTaskId == "") {
    return;
  }

  if (webTasks.find(currentWebTaskId) != webTasks.end()) {
    WebTask &task = webTasks[currentWebTaskId];
    task.status = "error";
    task.result = errorMessage;
    task.resultReady = true;

    // 发送错误状态给客户端
    StaticJsonDocument<512> errorDoc;
    errorDoc["type"] = "response";
    errorDoc["taskId"] = currentWebTaskId;
    errorDoc["status"] = "error";
    errorDoc["error"] = errorMessage;
    String statusMsg;
    serializeJson(errorDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);
  }

  // 重置状态
  cmdFromWeb = false;
  webTaskActive = false;
  currentWebTaskId = "";

  // 处理下一个任务
  processNextWebTask();
}

// 处理下一个等待的任务
void processNextWebTask()
{
  for (auto &pair : webTasks) {
    WebTask &task = pair.second;
    if (task.status == "pending") {
      startWebTask(task.taskId);
      break;
    }
  }
}

// WiFi配置函数
bool connectWifi(String ssid, String password)
{
  WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 100) {
    delay(100);
    PT('.');
    timeout++;
  }
  PTL();
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  } else {
    Serial.println("connection failed");
    return false;
  }
}

void startWifiManager()
{
#ifdef I2C_EEPROM_ADDRESS
  i2c_eeprom_write_byte(EEPROM_WIFI_MANAGER, false);
#else
  config.putBool("WifiManager", false);
#endif

  WiFiManager wm;
  wm.setConfigPortalTimeout(60);
  if (!wm.autoConnect((uniqueName + " WifiConfig").c_str())) {
    PTLF("Fail to connect Wifi. Rebooting.");
    delay(3000);
    ESP.restart();
  } else {
    webServerConnected = true;
  }

  if (webServerConnected) {
    // 启动WebSocket服务器
    webSocket.begin();
    webSocket.onEvent(handleWebSocketEvent);
    PTLF("WebSocket server started");
  } else {
    PTLF("Timeout: Fail to connect web server!");
  }

#ifdef I2C_EEPROM_ADDRESS
  i2c_eeprom_write_byte(EEPROM_WIFI_MANAGER, webServerConnected);
#else
  config.putBool("WifiManager", webServerConnected);
#endif
}

void resetWifiManager()
{
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  delay(2000);
  if (esp_wifi_restore() != ESP_OK) {
    PTLF("\nWiFi is not initialized by esp_wifi_init ");
  } else {
    PTLF("\nWiFi Configurations Cleared!");
  }
  delay(2000);
  ESP.restart();
}

// 主循环调用函数
void WebServerLoop()
{
  if (webServerConnected) {
    webSocket.loop();
    checkHeartbeats(); // 检查心跳

    // 检查任务超时
    unsigned long currentTime = millis();
    for (auto &pair : webTasks) {
      WebTask &task = pair.second;
      if (task.status == "running" && task.startTime > 0) {
        if (currentTime - task.startTime > 30000) { // 30秒超时
          PTHL("web task timeout: ", task.taskId);
          task.status = "error";
          task.result = "Task timeout";
          task.resultReady = true;

          // 发送超时状态给客户端
          String statusMsg = "{\"taskId\":\"" + task.taskId + "\",\"status\":\"error\",\"error\":\"Task timeout\"}";
          webSocket.sendTXT(task.clientId, statusMsg);

          if (task.taskId == currentWebTaskId) {
            cmdFromWeb = false;
            webTaskActive = false;
            currentWebTaskId = "";
            processNextWebTask();
          }
        }
      }
    }
  }
}
