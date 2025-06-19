// 定义陀螺仪控制和传感器输入积木块
Blockly.defineBlocksWithJsonArray([
  {
    type: 'gyro_control',
    message0: "陀螺仪 %1",
    args0: [
      {
        type: "field_dropdown",
        name: "ACTION",
        options: [
          ["启用", "enable"],
          ["禁用", "disable"]
        ]
      }
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: "控制陀螺仪模块的启用和禁用",
    helpUrl: ""
  },
  {
    type: 'get_sensor_input',
    message0: "获取传感器 %1",
    args0: [
      {
        type: "field_dropdown",
        name: "SENSOR",
        options: [
          ["超声波", "ultrasonic"],
          ["触摸", "touch"],
          ["距离", "distance"]
        ]
      }
    ],
    output: true,
    outputType: "Number",
    colour: 230,
    tooltip: "读取各种传感器的值",
    helpUrl: ""
  }
]);
