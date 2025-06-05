/**
 * PetoiWebBlock WebSocket客户端
 * 实现实时双向通信
 */

class PetoiAsyncClient
{
    constructor(baseUrl = null)
    {
        this.baseUrl = baseUrl || `ws://${window.location.hostname}:81`;
        this.taskTimeout = 10000; // 10秒超时
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
                    console.log('[心跳] 超时，准备重连');
                    this.ws.close();
                }, this.heartbeatTimeoutMs);
            }
        }, this.heartbeatIntervalMs);
    }

    /**
     * 停止心跳
     */
    stopHeartbeat()
    {
        if (this.heartbeatInterval) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
            console.log('[心跳] 停止心跳检测');
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
                console.log('[WebSocket] 连接已建立');
                this.lastHeartbeatTime = Date.now();
                this.startHeartbeat(); // 启动心跳
                resolve();
            };

            this.ws.onclose = () => {
                this.connected = false;
                this.stopHeartbeat(); // 停止心跳
                console.log('[WebSocket] 连接已关闭');
            };

            this.ws.onerror = (error) => {
                console.error('[WebSocket] 错误:', error);
                reject(error);
            };

            this.ws.onmessage = (event) => {
                // 处理心跳响应
                if (event.data === 'pong') {
                    const now = Date.now();
                    const latency = now - this.lastHeartbeatTime;
                    console.log(`[心跳] 收到pong (延迟: ${latency}ms)`);
                    if (this.heartbeatTimeout) {
                        clearTimeout(this.heartbeatTimeout);
                        this.heartbeatTimeout = null;
                    }
                    return;
                }
                
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
            console.log(`[重连] 尝试重连 (${this.reconnectAttempts}/${this.maxReconnectAttempts})...`);
            setTimeout(() => this.connect(), this.reconnectDelay);
        } else {
            console.error('[重连] 达到最大重连次数，请检查网络连接');
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
            console.log(`[心跳] 发送心跳 (距离上次心跳: ${timeSinceLastHeartbeat}ms)`);
            
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
            // 清理数据中的特殊字符
            const cleanData = data.replace(/[\r\n\t\f\v]/g, ' ').trim();
            const message = JSON.parse(cleanData);
            
            // 处理心跳响应
            if (message.type === 'heartbeat') {
                const now = Date.now();
                const latency = now - this.lastHeartbeatTime;
                console.log(`[心跳] 收到心跳响应 (延迟: ${latency}ms)`);
                if (this.heartbeatTimeout) {
                    clearTimeout(this.heartbeatTimeout);
                    this.heartbeatTimeout = null;
                }
                return;
            }

            // 处理错误消息
            if (message.error) {
                console.error('服务器错误:', message.error);
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
                        task.resolve(message.result);
                        this.pendingTasks.delete(message.taskId);
                        break;
                    case 'error':
                        task.reject(new Error(message.error));
                        this.pendingTasks.delete(message.taskId);
                        break;
                }
            }
        } catch (error) {
            console.error('消息处理错误:', error);
            console.error('原始数据:', data);
        }
    }

    /**
     * 发送命令
     */
    async sendCommand(command, timeout = this.taskTimeout)
    {
        if (!this.connected) {
            throw new Error('未连接到服务器');
        }

        return new Promise((resolve, reject) => {
            const taskId = Date.now().toString();
            const message = {
                type: 'command',
                taskId: taskId,
                command: command,
                timestamp: Date.now()
            };

            const timeoutId = setTimeout(() => {
                this.pendingTasks.delete(taskId);
                reject(new Error('命令执行超时'));
            }, timeout);

            this.pendingTasks.set(taskId, {
                resolve: (result) => {
                    clearTimeout(timeoutId);
                    resolve(result);
                },
                reject: (error) => {
                    clearTimeout(timeoutId);
                    reject(error);
                }
            });

            this.ws.send(JSON.stringify(message));
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
     * 等待任务完成
     */
    async waitForCompletion(taskId)
    {
        const startTime = Date.now();

        while (Date.now() - startTime < this.taskTimeout)
        {
            try
            {
                const response = await fetch(`${this.baseUrl}/status?taskId=${taskId}`);
                const text = await response.text();
                const lines = text.trim().split('\n');
                const status = lines[0];
                const result = lines.slice(1).join('\n');

                if (status === 'completed')
                {
                    return result;
                } else if (status === 'error')
                {
                    throw new Error(result);
                }

                await this.delay(100);
            } catch (error)
            {
                throw new Error(`任务执行失败: ${error.message}`);
            }
        }
        throw new Error('任务执行超时');
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