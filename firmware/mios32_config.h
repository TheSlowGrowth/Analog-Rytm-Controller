/*
 * Local MIOS32 configuration file
 *
 * this file allows to disable (or re-configure) default functions of MIOS32
 * available switches are listed in $MIOS32_PATH/modules/mios32/MIOS32_CONFIG.txt
 *
 */

#ifndef _MIOS32_CONFIG_H
#define _MIOS32_CONFIG_H

// The boot message which is print during startup and returned on a SysEx query
#define MIOS32_LCD_BOOT_MSG_LINE1 "Analog Rytm"
#define MIOS32_LCD_BOOT_MSG_LINE2 "MIDI Controller"

#define MIOS32_UART_NUM 2

// AIN configuration:

// bit mask to enable channels
//
// Pin mapping on MBHP_CORE_STM32 module:
//   15       14      13     12     11     10      9      8
// J16.SO  J16.SI  J16.SC J16.RC J5C.A11 J5C.A10 J5C.A9 J5C.A8
//   7        6       5      4      3      2      1       0
// J5B.A7  J5B.A6  J5B.A5 J5B.A4 J5A.A3 J5A.A2 J5A.A1  J5A.A0
//
// Examples:
//   mask 0x000f will enable all J5A channels
//   mask 0x00f0 will enable all J5B channels
//   mask 0x0f00 will enable all J5C channels
//   mask 0x0fff will enable all J5A/B/C channels
// (all channels are disabled by default)

// the STM32F4 core board has only 8 analog inputs, so we must multiplex
// them with two 74HC4051 (see: MBHP AINx4 module)
#define MIOS32_AIN_CHANNEL_MASK 0x0003 // 2 ch multiplexed by 8 => 16 ch
#define MIOS32_AIN_MUX_PINS 3
#define MIOS32_AIN_MUX0_PIN   GPIO_Pin_8  // J10A.D0
#define MIOS32_AIN_MUX0_PORT  GPIOE
#define MIOS32_AIN_MUX1_PIN   GPIO_Pin_9  // J10A.D1
#define MIOS32_AIN_MUX1_PORT  GPIOE
#define MIOS32_AIN_MUX2_PIN   GPIO_Pin_10 // J10A.D2
#define MIOS32_AIN_MUX2_PORT  GPIOE

// The STM32 Core board has all 12 analog inputs (use this line instead)
//#define MIOS32_AIN_CHANNEL_MASK 0x0fff

// define the deadband (min. difference to report a change to the application hook)
// typically set to (2^(12-desired_resolution)-1)
// e.g. for a resolution of 7 bit, it's set to (2^(12-7)-1) = (2^5 - 1) = 31
#define MIOS32_AIN_DEADBAND 31

#define MIOS32_SRIO_NUM_SR 2
#define MIOS32_ENC_NUM_MAX 0
#define MIOS32_MIDI_DEFAULT_PORT USB0
#define MIOS32_MIDI_DEBUG_PORT USB0
#define MIOS32_USB_MIDI_NUM_PORTS 1

// enable 1 BankStick
#define MIOS32_IIC_BS_NUM 1

#endif /* _MIOS32_CONFIG_H */
