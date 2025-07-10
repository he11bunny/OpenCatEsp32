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
  String status; // "pending", "running", "completed", "error"
  unsigned long timestamp;
  unsigned long endTime;
  unsigned long startTime;
  bool resultReady;
  uint8_t clientId; // 添加客户端ID
  std::vector<String> commandGroup; // 命令组中的命令列表
  std::vector<String> results; // 命令组中的执行结果
  size_t currentCommandIndex; // 当前执行的命令索引
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
void sendCameraData(int xCoord, int yCoord, int width, int height);
void sendUltrasonicData(int distance);
void clearWebTask(String taskId);

// 简单的 Base64 解码函数
String base64Decode(String input) {
  const char PROGMEM b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String result = "";
  int val = 0, valb = -8;
  
  for (char c : input) {
    if (c == '=') break;
    
    int index = -1;
    for (int i = 0; i < 64; i++) {
      if (pgm_read_byte(&b64_alphabet[i]) == c) {
        index = i;
        break;
      }
    }
    
    if (index == -1) continue;
    
    val = (val << 6) | index;
    valb += 6;
    
    if (valb >= 0) {
      result += char((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  
  return result;
}

// 生成任务ID
String generateTaskId()
{
  return String(millis()) + "_" + String(esp_random() % 1000);
}

// 发送摄像头数据到所有连接的客户端
void sendCameraData(int xCoord, int yCoord, int width, int height) {
  if (!webServerConnected || connectedClients.empty()) {
    return;
  }

  JsonDocument cameraDoc;
  cameraDoc["type"] = "event_cam";
  cameraDoc["x"] = xCoord - imgRangeX / 2.0;  // 与showRecognitionResult保持一致
  cameraDoc["y"] = yCoord - imgRangeY / 2.0;  // 与showRecognitionResult保持一致
  cameraDoc["width"] = width;
  cameraDoc["height"] = height;
  cameraDoc["timestamp"] = millis();

  String cameraData;
  serializeJson(cameraDoc, cameraData);

  // 向所有连接的客户端发送数据
  for (auto &client : connectedClients) {
    if (client.second) { // 如果客户端仍然连接
      webSocket.sendTXT(client.first, cameraData);
    }
  }
}

// 发送超声波数据到所有连接的客户端
void sendUltrasonicData(int distance) {
  if (!webServerConnected || connectedClients.empty()) {
    return;
  }

  JsonDocument ultrasonicDoc;
  ultrasonicDoc["type"] = "event_us";
  ultrasonicDoc["distance"] = distance;
  ultrasonicDoc["timestamp"] = millis();

  String ultrasonicData;
  serializeJson(ultrasonicDoc, ultrasonicData);

  // 向所有连接的客户端发送数据
  for (auto &client : connectedClients) {
    if (client.second) { // 如果客户端仍然连接
      webSocket.sendTXT(client.first, ultrasonicData);
    }
  }
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
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, message);
      
      if (error) {
        // JSON 解析错误，发送错误响应
        JsonDocument errorDoc;
        errorDoc["type"] = "error";
        errorDoc["error"] = "Invalid JSON format";
        String errorResponse;
        serializeJson(errorDoc, errorResponse);
        webSocket.sendTXT(num, errorResponse);
        return;
      }

      String msgType = doc["type"].as<String>();
      PTHL("msg type: ", msgType);
      
      // 处理心跳消息
      if (doc["type"] == "heartbeat") {
        lastHeartbeat[num] = millis();
        JsonDocument response;
        response["type"] = "heartbeat";
        response["timestamp"] = millis();
        String responseStr;
        serializeJson(response, responseStr);
        webSocket.sendTXT(num, responseStr);
        return;
      }

      // 处理命令消息（统一使用命令组格式）
      if (doc["type"] == "command") {
        String taskId = doc["taskId"].as<String>();
        JsonArray commands;
        
        // 如果是单个命令，转换为命令组格式
        commands = doc["commands"].as<JsonArray>();
        
        // 更新心跳时间
        lastHeartbeat[num] = millis();
        
        // 创建任务记录
        WebTask task;
        task.taskId = taskId;
        task.status = "pending";
        task.timestamp = millis();
        task.startTime = 0;
        task.resultReady = false;
        task.clientId = num;
        task.currentCommandIndex = 0;
        
        // 存储命令组
        for (JsonVariant cmd : commands) {
          task.commandGroup.push_back(cmd.as<String>());
        }
        
        // 如果当前没有活跃的web任务，立即开始执行
        if (!webTaskActive) {
          // 存储任务
          webTasks[taskId] = task;
          startWebTask(taskId);
        } else {
          // 如果当前有活跃的web任务，丢弃并返回错误
          errorWebTask("Previous web task is still running");
          return;
        }
        
        // 发送任务开始响应
        JsonDocument startDoc;
        startDoc["type"] = "response";
        startDoc["taskId"] = taskId;
        startDoc["status"] = "running";
        String startResponse;
        serializeJson(startDoc, startResponse);
        webSocket.sendTXT(num, startResponse);
        
        PTHL("web command group async: ", taskId);
        PTHL("command count: ", task.commandGroup.size());
      }
      break;
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

  // 设置全局标志和命令
  cmdFromWeb = true;
  currentWebTaskId = taskId;
  webTaskActive = true;
  webResponse = ""; // 清空响应缓冲区

  // 执行命令组中的下一个命令
  if (task.currentCommandIndex < task.commandGroup.size()) {
    String webCmd = task.commandGroup[task.currentCommandIndex];
    
    // 检查是否是base64编码的命令
    if (webCmd.startsWith("b64:")) {
      String base64Cmd = webCmd.substring(4);
      String decodedString = base64Decode(base64Cmd);
      if (decodedString.length() > 0) {
        token = decodedString[0];
        for (int i = 1; i < decodedString.length(); i++) {
          int8_t param = (int8_t)decodedString[i];
          newCmd[i-1] = param;
        }
        // strcpy(newCmd, decodedString.c_str() + 1);
        cmdLen = decodedString.length() - 1;
        if (token >= 'A' && token <= 'Z') {
          newCmd[cmdLen] = '~';
        } else {
          newCmd[cmdLen] = '\0';
        }
        PTHL("base64 decode token: ", token);
        PTHL("base64 decode args count: ", cmdLen);
        int8_t argStart = newCmd[0];
        int8_t argEnd = newCmd[cmdLen - 1];
        printf("base64 decode arg start: %d\n",argStart);
        printf("base64 decode arg end: %d\n", argEnd);
      } else {
        PTHL("base64 decode failed: ", task.currentCommandIndex);
        // base64 解码失败，跳过这个命令
        task.currentCommandIndex++;
        startWebTask(taskId);
        return;
      }
    } else {
      // 解析命令
      token = webCmd[0];
      strcpy(newCmd, webCmd.c_str() + 1);
      cmdLen = strlen(newCmd);
      newCmd[cmdLen + 1] = '\0';
    }
    newCmdIdx = 4;

    // 更新任务状态
    task.status = "running";
    task.startTime = millis();

    // 通知客户端任务开始
    JsonDocument statusDoc;
    statusDoc["type"] = "response";
    statusDoc["taskId"] = taskId;
    statusDoc["status"] = "running";
    String statusMsg;
    serializeJson(statusDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);

    PTHL("executing command group task: ", taskId);
    PTHL("sub command Index: ", task.currentCommandIndex);
    PTHL("sub command: ", webCmd);
    PTHL("total commands: ", task.commandGroup.size());
  } else {
    // 所有命令执行完成
    completeWebTask();
  }
}

// 完成web任务
void completeWebTask()
{
  if (!webTaskActive || currentWebTaskId == "") {
    return;
  }

  if (webTasks.find(currentWebTaskId) != webTasks.end()) {
    WebTask &task = webTasks[currentWebTaskId];
    task.results.push_back(webResponse);

    // 检查是否还有下一个命令
    if (task.currentCommandIndex + 1 < task.commandGroup.size()) {
      // 还有下一个命令，继续执行
      task.currentCommandIndex++;
      startWebTask(currentWebTaskId);
      return;
    }
    
    // 所有命令执行完成
    task.status = "completed";
    task.endTime = millis();
    task.resultReady = true;

    PTHL("web task completed: ", currentWebTaskId);
    PTHL("results length: ", task.results.size());

    // 发送完成状态给客户端
    JsonDocument completeDoc;
    completeDoc["type"] = "response";
    completeDoc["taskId"] = currentWebTaskId;
    completeDoc["status"] = "completed";
    JsonArray results = completeDoc["results"].to<JsonArray>();
    for (String result : task.results) {
      results.add(result);
    }
    String statusMsg;
    serializeJson(completeDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);
    PTHL("web task response: ", statusMsg);
    clearWebTask(currentWebTaskId);
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
    task.resultReady = true;

    // 发送错误状态给客户端
    JsonDocument errorDoc;
    errorDoc["type"] = "response";
    errorDoc["taskId"] = currentWebTaskId;
    errorDoc["status"] = "error";
    errorDoc["error"] = errorMessage;
    String statusMsg;
    serializeJson(errorDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);
    clearWebTask(currentWebTaskId);
  }

  // 重置状态
  cmdFromWeb = false;
  webTaskActive = false;
  currentWebTaskId = "";

  // 处理下一个任务
  processNextWebTask();
}

void clearWebTask(String taskId)
{
  if (webTasks.find(taskId) != webTasks.end()) {
    WebTask &task = webTasks[taskId];
    PTHL("clear web task: ", taskId);
    task.commandGroup.clear();
    task.results.clear();
    webTasks.erase(taskId);
  }
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
    PTHL("Successfully connected Wifi to IP Address: ", WiFi.localIP());
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

    // 检查任务超时
    unsigned long currentTime = millis();
    for (auto &pair : webTasks) {
      WebTask &task = pair.second;
      if (task.status == "running" && task.startTime > 0) {
        if (currentTime - task.startTime > 30000) { // 30秒超时
          PTHL("web task timeout: ", task.taskId);
          task.status = "error";
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
