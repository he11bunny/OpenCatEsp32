// 添加全局变量
let deviceIP = '';
let deviceModel = '';

// 设置IP地址的函数
function setDeviceIP(ip)
{
  deviceIP = ip;
}

// 获取IP地址的函数
function getDeviceIP()
{
  return deviceIP;
}

// 设置型号的函数
function setDeviceModel(model)
{
  deviceModel = model;
}

// 获取型号的函数
function getDeviceModel()
{
  return deviceModel;
}

// 连接机器人函数实现 - WebSocket版本
async function makeConnection(ip, timeout = 2000) {
  try {
    if (window.client) {
      // 如果已经有连接，先断开
      window.client.disconnect();
      window.client = null; // 清除全局客户端实例
    }
    // 创建WebSocket客户端
    const client = new PetoiAsyncClient(`ws://${ip}:81`);
    // window.client = client;
    // 尝试连接
    await client.connect();
    // 发送问号命令测试连接
    const model = await client.sendCommand('?');
    
    // 更严格地检查响应内容，特别识别模拟数据
    if (model && model.length > 0 && model.trim() !== '?' && model.trim() !== '' && model.trim() !== 'PetoiModel-v1.0') {
      setDeviceIP(ip);
      setDeviceModel(model);
      // 设置全局客户端实例
      window.client = client;
      client.startHeartbeat();
      return true;
    } else {
      if (model.trim() === 'PetoiModel-v1.0') {
        alert(getText("connectionFailedMock") + '\n\n' + getText("programExecutionStopped"));
      } else {
        alert(getText("connectionFailedCheck") + '\n\n' + getText("programExecutionStopped"));
      }
      return false;
    }
  } catch (err) {
    client.disconnect();
    // 显示友好的错误信息，并明确说明程序已中断
    if (err.message.includes('timeout') || err.message.includes('超时')) {
      alert(getText("connectionTimeout").replace("{ip}", ip) + '\n\n' + getText("programExecutionStopped"));
    } else if (err.message.includes('Failed to fetch') || err.message.includes('Connection reset') || err.message.includes('ERR_CONNECTION_RESET')) {
      alert(getText("deviceConnectionLost").replace("{ip}", ip) + '\n\n' + getText("checkDeviceAndNetwork") + '\n\n' + getText("programExecutionStopped"));
    } else if (err.message.includes('Network Error') || err.message.includes('网络')) {
      alert(getText("networkError").replace("{ip}", ip) + '\n\n' + getText("programExecutionStopped"));
    } else {
      alert(getText("connectionErrorDetails").replace("{error}", err.message) + '\n\n' + getText("programExecutionStopped"));
    }
    return false;
  }
}

// 关闭连接函数实现
async function closeConnection() {
  try {
    if (window.client) {
      await window.client.disconnect();
      // 清除全局客户端实例
      window.client = null;
      return true;
    }
    return false;
  } catch (err) {
    console.error(getText("closeConnectionError"), err);
    return false;
  }
}

// 全局异步客户端类定义
// PetoiAsyncClient 类已移动到 petoi_async_client.js 文件中

function webRequest(command, timeout = 30000, needResponse = true) {
  return new Promise(async (resolve, reject) =>
    {
      try
      {
        // 使用全局的 WebSocket 客户端实例
        if (!window.client) {
          reject(new Error(getText("noConnectionEstablished")));
          return;
        }
        let result = await window.client.sendCommand(command, timeout);
        if (Array.isArray(result) && result.length == 1) {
          result = result[0];
        }
        // 根据 needResponse 参数决定是否返回结果
        resolve(needResponse ? result : true);
      } catch (error) {
        console.error(getText("httpRequestError"), error);
        reject(error);
      }
    });
}

function webBatchRequest(commands, timeout = 30000, needResponse = true)
{
  return new Promise(async (resolve, reject) => {
    try
    {
      // 使用全局的 WebSocket 客户端实例
      if (!window.client) {
        reject(new Error(getText("noConnectionEstablished")));
        return;
      }
      const result = await window.client.sendCommand(commands, timeout);
      resolve(needResponse ? result : true);
    } catch (error)
    {
      console.error(getText("webBatchRequestError"), error);
      reject(error);
    }
  });
}

// 添加来自websocket的事件监听
function addWebSocketEventListeners(eventName, callback)
{
   if (!window.client) {
    return;
   }
   window.client.eventTarget.addEventListener(eventName, async (event) => {
     try {
       await callback(event);
     } catch (error) {
       console.error(getText("messageProcessingError"), error);
     }
   });
}

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}