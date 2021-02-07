#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "bsp/board.h"
#include "tusb.h"

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum {
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

/*------------- MAIN -------------*/
int main(void) {
    board_init();
    tusb_init();

    int pressed = false, press;
    uint8_t keycode[6] = {0};
    typedef struct {
      uint8_t modifier;
      uint8_t keycode;
    } key_t;

    // Yes, I know HID_KEYCODE_TO_ASCII table but it's works only US keyboard.
    // This is a hack to make this work on JIS 106 keyboard.
    key_t keys[] = {
      { 0, HID_KEY_I },
      { 0, HID_KEY_F },
      { 0, HID_KEY_SPACE },
      { 0, HID_KEY_E },
      { 0, HID_KEY_R },
      { 0, HID_KEY_R },
      { 0, HID_KEY_SPACE },
      { KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_1 },
      { KEYBOARD_MODIFIER_LEFTSHIFT, 0x2d },
      { 0, HID_KEY_SPACE },
      { 0, HID_KEY_N },
      { 0, HID_KEY_I },
      { 0, HID_KEY_L },
      { 0, HID_KEY_SPACE },
      { KEYBOARD_MODIFIER_LEFTSHIFT, 0x30 },
      { 0, HID_KEY_RETURN },
      //{ 0, HID_KEY_TAB },
      { 0, HID_KEY_R },
      { 0, HID_KEY_E },
      { 0, HID_KEY_T },
      { 0, HID_KEY_U },
      { 0, HID_KEY_R },
      { 0, HID_KEY_N },
      { 0, HID_KEY_SPACE },
      { 0, HID_KEY_E },
      { 0, HID_KEY_R },
      { 0, HID_KEY_R },
      { 0, HID_KEY_RETURN },
      { KEYBOARD_MODIFIER_LEFTSHIFT, 0x31 },
      { 0, HID_KEY_RETURN },
      { 0, 0 },
    }, *key;

    while (1) {
        tud_task(); // tinyusb device task

        //led_blinking_task();
        press = get_bootsel_button();
        if (pressed == false && press == true) {
          key = keys;
        }
        pressed = press;
        gpio_put(PICO_DEFAULT_LED_PIN, press);

        if (tud_suspended()) {
          tud_remote_wakeup();
        }
        if (tud_hid_ready()) {
          if (key->keycode != 0) {
            keycode[0] = key->keycode;
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, key->modifier, keycode);
            board_delay(10);
            key++;
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
            board_delay(10);
          }
        }
    }

    return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    // TODO not Implemented
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    // TODO set LED based on CAPLOCK, NUMLOCK etc...
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms) return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}
