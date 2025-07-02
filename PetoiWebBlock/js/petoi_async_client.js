/**
 * PetoiWebBlock WebSocket客户端
 * 实现实时双向通信
 */


class PetoiAsyncClient
{
    constructor(baseUrl = null)
    {
        this.baseUrl = baseUrl || `ws://${window.location.hostname}:81`;
        this.taskTimeout = 30000; // 30秒超时
        this.ws = null;
        this.connected = false;
        this.pendingTasks = new Map();
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;
        this.heartbeatInterval = null;
        this.heartbeatTimeout = null;
        this.heartbeatIntervalMs = 10000; // 10秒发送一次心跳
        this.heartbeatTimeoutMs = 15000;  // 15秒没有响应就重连
        this.lastHeartbeatTime = 0;       // 记录最后一次心跳时间
        this.eventTarget = new EventTarget();
        this.clientId = Date.now().toString(); // 唯一客户端ID
    }

    /**
     * 启动心跳
     */
    startHeartbeat()
    {
        // 清除可能存在的旧心跳
        this.stopHeartbeat();

        // 设置心跳定时器
        this.heartbeatInterval = setInterval(() => {
            if (this.connected) {
                this.sendHeartbeat();
                
                // 设置心跳超时
                this.heartbeatTimeout = setTimeout(() => {
                    console.log(getText('heartbeatTimeout'));
                    this.disconnect();
                }, this.heartbeatTimeoutMs);
            }
        }, this.heartbeatIntervalMs);
        // 延时1秒后发送心跳
        // setTimeout(() => {
        //     this.sendHeartbeat();
        // }, 1000);
    }

    /**
     * 停止心跳
     */
    stopHeartbeat()
    {
        if (this.heartbeatInterval) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
            console.log(getText('heartbeatStopped'));
        }
        if (this.heartbeatTimeout) {
            clearTimeout(this.heartbeatTimeout);
            this.heartbeatTimeout = null;
        }
    }

    /**
     * 连接到WebSocket服务器
     */
    async connect()
    {
        return new Promise((resolve, reject) => {
            this.ws = new WebSocket(this.baseUrl);

            this.ws.onopen = () => {
                this.connected = true;
                this.reconnectAttempts = 0;
                console.log(getText('websocketConnected'));
                this.lastHeartbeatTime = Date.now();
                resolve();
            };

            this.ws.onclose = () => {
                this.connected = false;
                this.stopHeartbeat(); // 停止心跳
                console.log(getText('websocketClosed'));
            };

            this.ws.onerror = (error) => {
                console.error(getText('websocketError'), error);
                reject(error);
            };

            this.ws.onmessage = (event) => {
                this.handleMessage(event.data);
            };
        });
    }

    /**
     * 处理重连
     */
    handleReconnect()
    {
        if (this.reconnectAttempts < this.maxReconnectAttempts) {
            this.reconnectAttempts++;
            console.log(getText('reconnectAttempt').replace('{current}', this.reconnectAttempts).replace('{max}', this.maxReconnectAttempts));
            setTimeout(() => this.connect(), this.reconnectDelay);
        } else {
            console.error(getText('maxReconnectAttempts'));
        }
    }

    /**
     * 发送心跳消息
     */
    sendHeartbeat()
    {
        if (this.ws && this.connected) {
            const now = Date.now();
            const timeSinceLastHeartbeat = now - this.lastHeartbeatTime;
            console.log(getText('heartbeatSent').replace('{time}', timeSinceLastHeartbeat));
            
            const heartbeatMessage = {
                type: 'heartbeat',
                timestamp: now
            };
            this.ws.send(JSON.stringify(heartbeatMessage));
            this.lastHeartbeatTime = now;
        }
    }

    /**
     * 处理接收到的消息
     */
    handleMessage(data)
    {
        try {
            console.log('handleMessage', data);
            // 清理数据中的特殊字符
            const cleanData = data.replace(/[\r\n\t\f\v]/g, ' ').trim();
            const message = JSON.parse(cleanData);
            //print message type
            console.log('message type', message.type);
            // 处理心跳响应
            if (message.type === 'heartbeat') {
                const now = Date.now();
                const latency = now - this.lastHeartbeatTime;
                console.log(getText('heartbeatResponse').replace('{latency}', latency));
                if (this.heartbeatTimeout) {
                    clearTimeout(this.heartbeatTimeout);
                    this.heartbeatTimeout = null;
                }
                return;
            }

            // 处理错误消息
            if (message.error) {
                console.error(getText('serverError'), message.error);
                return;
            }

            // 处理任务相关消息
            if (message.taskId && this.pendingTasks.has(message.taskId)) {
                const task = this.pendingTasks.get(message.taskId);
                
                switch (message.status) {
                    case 'running':
                        task.onProgress && task.onProgress(message);
                        break;
                    case 'completed':
                        if (message.results.length > 0) {
                            task.resolve(message.results);
                        } else {
                            task.reject(new Error("response no results"));
                        }
                        this.pendingTasks.delete(message.taskId);
                        break;
                    case 'error':
                        task.reject(new Error(message.error));
                        this.pendingTasks.delete(message.taskId);
                        break;
                }
                return;
            }

            if (message.event) {
                this.eventTarget.dispatchEvent(new CustomEvent(message.event, message));
            }
        } catch (error) {
            console.error(getText('messageProcessingError'), error);
            console.error(getText('rawData'), data);
        }
    }

    /**
     * 发送命令
     * @param {string|Array} command - 单个命令或命令数组
     * @param {number} timeout - 超时时间（毫秒）
     * @returns {Promise} - 返回命令执行完成后的结果
     */
    async sendCommand(command, timeout = this.taskTimeout)
    {
        if (!this.connected) {
            throw new Error(getText('notConnected'));
        }

        // 将单个命令转换为命令数组
        const commands = Array.isArray(command) ? command : [command];

        if (commands.length === 0) {
            throw new Error('Invalid command');
        }

        return new Promise((resolve, reject) => {
            const taskId = Date.now().toString();
            const message = {
                type: 'command',
                taskId: taskId,
                commands: commands,
                timestamp: Date.now()
            };

            const timeoutId = setTimeout(() => {
                this.pendingTasks.delete(taskId);
                reject(new Error(getText('commandTimeout') + ' ' + taskId + ' ' + commands.join(' ')));
            }, timeout);

            this.pendingTasks.set(taskId, {
                resolve: (result) => {
                    clearTimeout(timeoutId);
                    if (Array.isArray(command)) {
                        resolve(result.map(item => parseInt(item) || 0));
                    } else {
                        resolve(result[0]);
                    }
                },
                reject: (error) => {
                    clearTimeout(timeoutId);
                    reject(error);
                }
            });
            const messageStr = JSON.stringify(message);
            console.log('send message', messageStr);
            this.ws.send(messageStr);
        });
    }

    /**
     * 关闭连接
     */
    disconnect()
    {
        this.stopHeartbeat(); // 停止心跳
        if (this.ws)
        {
            this.ws.close();
            this.ws = null;
            this.connected = false;
        }
    }

    /**
     * 延迟函数
     */
    delay(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }
}

// 导出类
if (typeof module !== 'undefined' && module.exports) {
    module.exports = PetoiAsyncClient;
}