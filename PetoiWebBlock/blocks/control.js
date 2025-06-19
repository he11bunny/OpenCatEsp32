// 延迟积木
Blockly.defineBlocksWithJsonArray([
    {
      type: 'delay_ms',
      message0: "delay %1 ms",
      args0: [
        {
          type: "field_input",
          name: "DELAY_MS",
          text: "1000",
        }
      ],
      previousStatement: null,
      nextStatement: null,
      colour: 230,
      tooltip: "delay milliseconds",
      helpUrl: ""
    },
  ]);


// delay function block the process
function delay(time_ms) {
    const start = Date.now();
    while (Date.now() - start < time_ms) {
      // Wait
    }
}
