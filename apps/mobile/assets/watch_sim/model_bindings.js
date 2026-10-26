// GLB/CAD object names are not firmware switch IDs. Bind by physical position
// viewed from the OLED side: upper left, lower left, upper right.
export const bindings = {
  screen: 'ssd1306.001_export',
  board: 'Board_export',
  screenWidthFraction: 0.94,
  buttons: [
    { mesh: 'SW2_export', key: 0, label: '上', inward: [-1, 0, 0] },
    { mesh: 'SW1_export', key: 1, label: '下', inward: [-1, 0, 0] },
    { mesh: 'SW3_export', key: 2, label: '确认', inward: [1, 0, 0] },
  ],
};
