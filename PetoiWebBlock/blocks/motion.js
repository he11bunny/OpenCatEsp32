// 定义所有动作相关积木块
Blockly.defineBlocksWithJsonArray([
  {
    type: 'local_action',
    message0: "原地动作 %1",
    args0: [
      {
        type: "field_dropdown",
        name: "COMMAND",
        options: [
          ['站立', 'kup'],
          ['坐下', 'ksit'],
          ['休息', 'd'],
          ['尿尿', 'kpee'],
        ],
      }
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: "Send HTTP request with command parameter",
    helpUrl: ""
  },
  {
    type: 'high_difficulty_action',
    message0: "高难度特技动作(小心使用) %1",
    args0: [
      {
        type: "field_dropdown",
        name: "COMMAND",
        options: [
          ['后空翻', 'kbkf'],
          ['跳跃', 'kjmp'],
        ],
      }
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: "Send HTTP request with command parameter",
    helpUrl: ""
  },
  {
    type: 'set_motor_angle',
    message0: "设置马达 %1 角度为 %2",
    args0: [
      {
        type: "field_number",
        name: "MOTOR",
        value: 0,
        min: 0,
        max: 11,
        precision: 1
      },
      {
        type: "field_number",
        name: "ANGLE",
        value: 90,
        min: 0,
        max: 180
      }
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: "设置指定马达的角度（0-11号，0-180度）",
    helpUrl: ""
  },
  {
    type: 'set_motor_relative_angle',
    message0: "设置马达 %1 角度%2 %3",
    args0: [
      {
        type: "field_number",
        name: "MOTOR",
        value: 0,
        min: 0,
        max: 11,
        precision: 1
      },
      {
        type: "field_number",
        name: "ANGLE_SIGN",
        options: [
          ["+", "+"],
          ["-", "-"]
        ]
      },
      {
        type: "field_number",
        name: "ANGLE",
        value: 90,
        min: -125,
        max: 125
      }
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: "设置指定马达的角度（0-11号，0-180度）",
    helpUrl: ""
  },
  {
    type: 'get_joint_angle',
    message0: "获取关节 %1 的角度",
    args0: [
      {
        type: "field_dropdown",
        name: "JOINT",
        options: [
        ]
      }
    ],
    output: "Number",
    colour: 230,
    tooltip: "获取指定关节的当前角度值",
    helpUrl: ""
  },
  {
    type: 'get_all_joint_angles',
    message0: "获取所有关节角度",
    output: "String",  // 改为返回字符串类型（JSON字符串）
    colour: 230,
    tooltip: "获取所有关节(1-16号)的当前角度值，返回JSON格式",
    helpUrl: ""
  }
]);
