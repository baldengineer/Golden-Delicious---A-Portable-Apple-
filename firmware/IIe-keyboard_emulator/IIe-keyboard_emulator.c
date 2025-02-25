/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2022 James Lewis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


#include "IIe-keyboard_emulator.h"

queue_t keycode_queue;

// old habits die hard
static inline uint32_t millis() {
    return ((time_us_32() / 1000));
}

// trying to reduce bus contention
bool repeating_timer_callback(struct repeating_timer *t) {
    dousb = true;
    return true;
}

//TODO Need to remove all references now that VGA works
void set_color_mode(bool state) {
    #if Mega_IIe_Rev2
        gpio_put(COLOR_MODE_PIN, state);
    #elif Mega_IIe_Rev3
        return;
    #endif
}

void queue_key(uint8_t key) {
    // if the PIO is ready, let's send in the character now
  //  if (pio_interrupt_get(pio,1) && queue_is_empty(&keycode_queue)) {
    if (key == '\0') // happens when function keys get pressed...
        return;
    D(printf("Im[%c]", key);)
    if (queue_is_empty(&keycode_queue)) {
        write_key(key);  
    }  else if (!queue_try_add(&keycode_queue, &key))
        printf("Failed to add [%c]\n", key);
}

static inline void empty_keyboard_queue() { 
    while (!queue_is_empty(&keycode_queue)) {
        uint8_t throwaway = '\0';
        queue_try_remove(&keycode_queue, &throwaway);
        printf("%c",throwaway);
    }
}
// TODO  pass references to pio stuff so I can move to another file
void reset_mega(uint8_t reset_type) {
    //reset_type = cold or warm

    printf("\nDisabling Mega-II...");
    gpio_put(RESET_CTL, 0x1);
    printf("\nReseting PIO...");
    pio_sm_set_enabled(pio, pio_sm, false);
    pio_sm_set_enabled(pio, pio_sm_1, false);
    pio_sm_restart(pio, pio_sm);
    pio_sm_restart(pio, pio_sm_1);
   // hid_app_task();
    tuh_task();
    printf("\nPausing");
    busy_wait_ms(250);
   // hid_app_task();
    tuh_task();
    printf("...[DONE]");
    printf("\nClearing Keyboard Queue..."); // queue_free() keeps causing hardfault
    empty_keyboard_queue();
    printf(" [Done].");
    printf("\nRenabling PIO...");
    pio_sm_set_enabled(pio, pio_sm, true);
    pio_sm_set_enabled(pio, pio_sm_1, true);
    printf("\nEnabling Mega-II...\n");
    gpio_put(RESET_CTL, 0x0);
}

void write_key(uint8_t key) {
    pio_sm_put(pio, pio_sm_1, key & 0x7F); 
    pio_sm_put(pio, pio_sm, 0x3);
    pio_interrupt_clear(pio, 1);
}

void raise_key() { // I don't remember why this is an if-statement, oh well, lolz.
    if (!pio_interrupt_get(pio,1)) {  //If irq 1 is clear we have a new key still
        pio_sm_put(pio, pio_sm,0x1);
    } else {
        pio_sm_put(pio, pio_sm,0x0);
    }
}

inline static uint8_t handle_serial_keyboard() {
    uint8_t incoming_char = getchar_timeout_us(0);
    // MEGA-II only seems to like these values
    if ((incoming_char > 0) && (incoming_char < 128)) {
        return incoming_char;
    }
    return 0;
}

// shortcut for setting up output pins (there isn't a SDK call for this?)
void out_init(uint8_t pin, bool state) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, state);
}

// For the blinky led
bool case_led_blink(struct repeating_timer *t) {
    if (kbd_led_state[1])
        kbd_led_state[1] = 0x0;
    else
        kbd_led_state[1] = 0x1;
    out_init(KBD_LED_PIN, kbd_led_state[1]);
    return true;
}

#define BLINK_OFF  0
#define BLINK_SLOW 1
#define BLINK_MED  2
#define BLINK_FAST 3
#define BLINK_ON   9

void blink_case_led(int speed) {
    switch (speed) {
        case BLINK_OFF:
        break;

        case BLINK_SLOW:
        break;

        case BLINK_MED:
        break;

        case BLINK_FAST:
        break;

        case BLINK_ON:
        break;

        default:
            printf("Got speed (%d) and no understand...\n", speed);
    }

}

bool audio_variable_debug;

static inline void handle_volume_control() {
    static uint16_t previous_audio_volume = 0;
    static uint32_t previous_audio_write = 0;
    static uint32_t previous_wiper_check = 0;
    static bool previous_audio_mute = true;
    static bool eeprom_write_flag = false;

    if (previous_audio_mute != audio_mute) {
        if (audio_mute) {
            // enter mute state
            write_mcp4541_wiper(MCP4541_MAX_STEPS, false);
        } else {
            // exit mute state
            previous_audio_volume = MCP4541_MAX_STEPS;
            audio_volume = read_mcp4541_eeprom();
        }
        previous_audio_mute = audio_mute;
    }
    if (audio_mute)
        return;
    
    if (audio_volume != previous_audio_volume) {
        write_mcp4541_wiper(audio_volume, false);
        previous_audio_write = time_us_32();
        eeprom_write_flag = true;
    }

    if (time_us_32() - previous_wiper_check >= WIPER_CHECK_INTERVAL) {
        int current_wiper_value = read_mcp4541_wiper();
        if (current_wiper_value < 0) {
            printf("Wiper Check Error: [%d]\n", current_wiper_value);
        }
        
        // for some reason, we're out of sync, force them different for next iteration
        if (audio_volume != current_wiper_value) {
            printf("ERROR: Audio level wrong. wiper:[%d], eeprom:[%d], variable[%d]\n", read_mcp4541_wiper(), read_mcp4541_eeprom(), audio_volume);
            previous_audio_volume = audio_mute-1;
        }

        previous_wiper_check = time_us_32();
    }

    // only update the EEPROM after user stops mashing the button.
    if (eeprom_write_flag && time_us_32() - previous_audio_write >= MCP4541_EEPROM_WAIT){
        write_mcp4541_eeprom(audio_volume);
        eeprom_write_flag = false;
    }

    if (audio_variable_debug) {
        audio_variable_debug = false;
        printf("[DEBUG] Audio levels. wiper:[%d], eeprom:[%d], variable[%d]\n", read_mcp4541_wiper(), read_mcp4541_eeprom(), audio_volume);

    }
    previous_audio_volume = audio_volume;
}

// put your setup code here, to run once:
void setup() {

    #if Mega_IIe_Rev2
        printf("\nConfiguring DEBUG Pin (%d)", DEBUG_PIN);     // debug pin to trigger the external logic analyzer
        out_init(DEBUG_PIN, 0x0);
    #elif Mega_IIe_Rev3
        kbd_led_state[1] = 0x1;
        printf("\nConfiguring Case LED Pin(%d)", KBD_LED_PIN);
        out_init(KBD_LED_PIN, kbd_led_state[1]);
    #endif

    struct repeating_timer case_led_timer;
    add_repeating_timer_ms(150, case_led_blink, NULL, &case_led_timer);
   
    stdio_init_all();  // UART accepts input and displays debug infos
   // busy_wait_ms(10);

    // yay usb!
    printf("\nEnabling tinyUSB Host");
    tusb_init();

    printf("\nInit Suppy Pins");
    setup_power_sequence();

    printf("\nConfiguring RESET_CTRL Pin (%d)", RESET_CTL);
    out_init(RESET_CTL, 0x0);

    mega_power_state = PWR_OFF;
    handle_power_sequence(mega_power_state);

    //audio_volume = (MCP4541_MAX_STEPS/2);
    printf("\nSetting up Audio I2C Interface...");
    setup_i2c_audio();

    // Get VGA2040 Ready To Go
    #if Mega_IIe_Rev3
        printf("\nConfiguring VID_ENABLE Pin (%d)", VID_ENABLE);
        out_init(VID_ENABLE, 0x1);
    #endif
    
    // BEEP BEEP BEEP
    #if Mega_IIe_Rev3
        headphone_status = gpio_get(HP_SENSE);
        // i2c setup stuff for AUD_SDA and AUD_SCL here
    #endif

    // Preload Reset state (is this needed?)
    #if Mega_IIe_Rev3
        reset_state = gpio_get(RESET_STATUS);
    #endif

    // helps with throtting usb (may get fixed in future TinyUSB, I hope)
 /*   printf("\nEnabling tuh_task");
    add_repeating_timer_ms(-2, repeating_timer_callback, NULL, &timer2);
    printf("\n(---------");*/

    // configure I/O control lines
    printf("\nConfiguring OAPL and CAPL");
    out_init(OAPL_pin, 0x0);
    out_init(CAPL_pin, 0x0);

    printf("\nConfiguring KBD PIO State Machine");
    KBD_pio_setup();

    printf("\nConfiguring key code queue");
    queue_init(&keycode_queue, sizeof(uint8_t), 10); // really only need 6, but wutwevers
 
// line 164 assertion error:
// https://github.com/raspberrypi/pico-sdk/issues/649
    while(!kbd_connected) {
        tuh_task();
        static uint32_t prev_micros;
        if (time_us_32() - prev_micros >= 2500000) {
            prev_micros = time_us_32();
            //printf("Keyboard not detected...(resetting)\n");
            printf("Keyboard not detected...\n");
            //hcd_port_reset(0);
        }
    }
    printf("Keyboard Detected, yay");

    tuh_task();
    int boot_wait_time = 2;
    printf("\nPausing for %d seconds ...", boot_wait_time);
    for (int x=boot_wait_time; x>=0; x--) {
        printf("%d...",x);
        for (int y=0; y<100; y++) {
            busy_wait_ms(10);
            tuh_task();
        }
    }

    mega_power_state = PWR_ON;
    handle_power_sequence(mega_power_state);
    bool cancelled = cancel_repeating_timer(&case_led_timer);
    printf("\nCancelled LED timer: %d\n", cancelled);

    printf("\n\n---------\nMega IIe Keyboard Emulatron 2000\n\nREADY.\n] ");
}

inline static void handle_apple_keys() {
    gpio_put(OAPL_pin, OAPL_state);
    gpio_put(CAPL_pin, CAPL_state);
}

inline static void handle_serial_buffer() {
    static uint32_t prev_serial_clear = 0;
    static bool serial_clear = false;

    uint8_t key_value = handle_serial_keyboard(); 
    if (key_value > 0) {
        if (key_value == 0x12) { // should be CTRL-R
            reset_mega(1); // 0 = cold, 1 = warm
        } else {
            //prepare_key_value(key_value);
            printf("%c",key_value);
            queue_key(key_value);
            serial_clear = true;
            prev_serial_clear = millis();
        }
    }

    // deassert ANYKEY when receiving characters over serial
    if (serial_clear && (millis() - prev_serial_clear >= serial_anykey_clear_interval)) {
        serial_clear = false; 
        //pio_sm_put(pio, pio_sm, (0x0));
        raise_key();
    }
}

inline static void handle_three_finger_reset() {
    if (do_a_reset) {
        struct repeating_timer case_led_timer;
        add_repeating_timer_ms(150, case_led_blink, NULL, &case_led_timer);
        do_a_reset = false;
        busy_wait_ms(THREE_FINGER_WAIT);
        reset_mega(0);
        bool cancelled = cancel_repeating_timer(&case_led_timer);
        printf("\nCancelled LED timer: %d\n", cancelled);
        kbd_led_state[1] = 0x1; // turn LED back on!
        out_init(KBD_LED_PIN, kbd_led_state[1]);

    }
}   

inline static void handle_runstop_button() {
    if (power_cycle_key_counter >= 3) {
        // do a power cycle
        mega_power_state = !mega_power_state;
        handle_power_sequence(mega_power_state);
        power_cycle_key_counter = 0;
        D(printf("Power State Now: (%d)", mega_power_state);)
    }

    if (time_us_32() - power_cycle_timer >= POWER_CYCLE_INTERVAL)
        power_cycle_key_counter = 0;
}

inline static void handle_pio_keycode_queue() {
     // keep the PIO queue full
    if (pio_interrupt_get(pio,1) && !queue_is_empty(&keycode_queue)) {
        uint8_t next_key;
        if (queue_try_remove(&keycode_queue, &next_key))
            write_key(next_key);
    }
}

#define MACRO_WAIT 10

void queue_macro_string(char *msg, bool before_cr, bool after_cr, bool after_sp) {
    int x = 0;
    struct repeating_timer case_led_timer;
    add_repeating_timer_ms(25, case_led_blink, NULL, &case_led_timer);

    if (before_cr) {
        queue_key(13);
        busy_wait_ms(MACRO_WAIT*5);
    }

    while (msg[x] != '\0') {
        queue_key(msg[x]);
        busy_wait_ms(MACRO_WAIT);
        x++;
        if (x >= 10)
            tuh_task();
    }
    busy_wait_ms(MACRO_WAIT*5);
    if (after_cr)
        queue_key(13);
    if (after_sp)
        queue_key(' ');
    printf("Queued %d chars\n", x);

    bool cancelled = cancel_repeating_timer(&case_led_timer);
    printf("\nCancelled LED timer: %d\n", cancelled);
    kbd_led_state[1] = 0x1; // turn LED back on!
    out_init(KBD_LED_PIN, kbd_led_state[1]);
}

inline static void handle_macros() {
    char macro_string[32];
    if (function_key_macros.Print) {
        function_key_macros.Print = false;
        //printf("Print Macro\n");
        sprintf(macro_string, "PRINT");
        queue_macro_string(macro_string, false, false, true);
    }

    if (function_key_macros.Input) {
        function_key_macros.Input = false;
        //printf("Input Macro\n");
        sprintf(macro_string, "INPUT");
        queue_macro_string(macro_string, false, false, true);
    }

    if (function_key_macros.Poke) {
        function_key_macros.Poke = false;
        //printf("Poke Macro\n");
        sprintf(macro_string, "POKE");
        queue_macro_string(macro_string, false, false, true);
    }

    if (function_key_macros.Peek) {
        function_key_macros.Peek = false;
        //printf("Peek Macro\n");
        sprintf(macro_string, "PEEK");
        queue_macro_string(macro_string, false, false, true);
    }

    if (function_key_macros.Call) {
        function_key_macros.Call = false;
        //printf("Call Macro\n");
        sprintf(macro_string, "CALL");
        queue_macro_string(macro_string, false, false, true);
    }

    if (function_key_macros.PR) {
        function_key_macros.PR = false;
        //printf("PR Macro\n");
        if (function_key_macros.shift)
            sprintf(macro_string, "PR #3");
        else
            sprintf(macro_string, "PR #6");
        queue_macro_string(macro_string, false, true, false);
    }

    if (function_key_macros.Text) {
        function_key_macros.Text = false;
        //printf("Text Macro\n");
        if (function_key_macros.shift)
            sprintf(macro_string, "GR");
        else
            sprintf(macro_string, "TEXT");
        queue_macro_string(macro_string, false, true, false);
    }

    if (function_key_macros.Home) {
        function_key_macros.Home = false;
        //printf("Home Macro\n");
        sprintf(macro_string, "HOME");
        queue_macro_string(macro_string, true, true, false);
    }

    if (function_key_macros.n151) {
        function_key_macros.n151 = false;
        //printf("-151 Macro\n");
        sprintf(macro_string,"CALL");
        queue_macro_string(macro_string, true, false, true);
        tuh_task();
        sprintf(macro_string, "-151");
        queue_macro_string(macro_string, false, true, false);
    }

    if (function_key_macros.x3F4) {
        function_key_macros.x3F4 = false;
        //printf("3F4 Macro\n");
        sprintf(macro_string, "3F4");
        queue_macro_string(macro_string, true, true, false);
        tuh_task();
        busy_wait_ms(100);
        sprintf(macro_string, ":00");
        tuh_task();
        queue_macro_string(macro_string, false, true, false);
        if (function_key_macros.shift)
            do_a_reset = true;
    }

    if (function_key_macros.p1012) {
        function_key_macros.p1012 = false;
        //printf("1012 Macro\n");
        sprintf(macro_string, "POKE");
        queue_macro_string(macro_string, true, false, true);
        tuh_task();
        sprintf(macro_string, "1012,1");
        queue_macro_string(macro_string, false, true, false);
        if (function_key_macros.shift)
            do_a_reset = true;
    }


    
}

inline static void handle_nkey_repeats() {
    switch (nkey) {               
        case NKEY_NEW:
            nkey_last_press = time_us_32();
            nkey = NKEY_ARMED;
        break;

        case NKEY_ARMED:
            if ((time_us_32() - nkey_last_press) >= nkey_wait_us) {
                nkey = NKEY_REPEATING;
            }
        break;

        case NKEY_REPEATING:
            if ((time_us_32() - nkey_last_press) >= nkey_repeat_us) {
                queue_key(last_key_pressed);
                nkey_last_press = time_us_32();
            }
        break;

        case NKEY_IDLE:
        default:

        break;
    }
}

int main() {
    setup();

    while (BALD_ENGINEER_IS_BALD) {
        // hid_app_task();
        tuh_task();

        handle_runstop_button();
        handle_apple_keys();
        handle_three_finger_reset();
        //handle_mega_power_button();
        handle_serial_buffer();
        handle_pio_keycode_queue();
        handle_nkey_repeats();
        handle_volume_control();
        handle_macros();
    } // while (true)
    return 0;  // but you never will hah!
} // it's da main thing


