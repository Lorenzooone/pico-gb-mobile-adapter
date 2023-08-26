#include "pico/stdlib.h"
#include "time.h"
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/pio.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "bsp/board.h"
#include "pico_mobile_adapter.h"
#include "io_buffer.h"
#include "bridge_debug_commands.h"
#include "pico/multicore.h"
#include "useful_qualifiers.h"
#include "sync.h"

#include "linkcable.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

//#define USE_LEDS
// LEDs lead to instability. Why? IDK. BUT DON'T USE LEDs!

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED     = 1000,
  BLINK_SUSPENDED   = 2500,

  BLINK_ALWAYS_ON   = UINT32_MAX,
  BLINK_ALWAYS_OFF  = 0
};

//#define OVERCLOCK
//#define USE_CORE_1_AS_WELL
// Can be used with PICO_COPY_TO_RAM to have a fixed high speed for irqs,
// not interrupted by core 0 (USB) irqs/cache evictions.
// Though, for this project, it's not needed...

#define DEBUG_TRANSFER_FLAG 0x80

#define MAX_TRANSFER_BYTES 0x40

#define URL "Mobile Adapter"

//------ static/const definitions ------//

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
static bool web_serial_connected = false;

const tusb_desc_webusb_url_t desc_url =
{
  .bLength         = 3 + sizeof(URL) - 1,
  .bDescriptorType = 3, // WEBUSB URL type
  .bScheme         = 1, // 0: http, 1: https
  .url             = URL
};

bool speed_240_MHz = false;

//------------- prototypes -------------//

void handle_input_data(bool is_in_mobile_loop);
void led_blinking_task(void);
void cdc_task(bool is_in_mobile_loop);
void webserial_task(bool is_in_mobile_loop);
void loop_upkeep_functions(bool is_in_mobile_loop);

void TIME_SENSITIVE(core_1_main)(void) {
    linkcable_init(link_cable_ISR);
    while(1) {
        handle_disable_request();
        handle_time_request();
    }
}

// main loop
int main(void) {
    board_init();
    init_disable_handler();
    init_time_request_handler();
    bool is_same_core = true;
#ifdef USE_CORE_1_AS_WELL
    is_same_core = false;
#endif
    set_core_shared(is_same_core);
#ifdef USE_CORE_1_AS_WELL
    multicore_launch_core1(&core_1_main);
#endif

#ifdef OVERCLOCK
    speed_240_MHz = set_sys_clock_khz(240000, false);
#endif

    // Initialize tinyusb
    tusb_init();

#ifndef USE_CORE_1_AS_WELL
    linkcable_init(link_cable_ISR);
#endif
    pico_mobile_init(loop_upkeep_functions);

    while (true) {
        pico_mobile_loop(is_same_core);
        loop_upkeep_functions(false);
    }

    return 0;
}

void loop_upkeep_functions(bool is_in_mobile_loop) {
    tud_task(); // tinyusb device task
    cdc_task(is_in_mobile_loop);
    webserial_task(is_in_mobile_loop);
    led_blinking_task();
}

// send characters to both CDC and WebUSB
void echo_all(uint8_t buf[], uint32_t count)
{
    // echo to web serial
    if ( web_serial_connected )
    {
        tud_vendor_write(buf, count);
    }

    // echo to cdc
    if ( tud_cdc_connected() )
    {
        for(uint32_t i=0; i<count; i++)
        {
            tud_cdc_write_char(buf[i]);
            if ( buf[i] == '\r' ) tud_cdc_write_char('\n');
        }
        tud_cdc_write_flush();
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// WebUSB use vendor class
//--------------------------------------------------------------------+

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
    // nothing to do for DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP) return true;

    switch (request->bRequest)
    {
        case VENDOR_REQUEST_WEBUSB:
            // match vendor request in BOS descriptor
            // Get landing page url
            return tud_control_xfer(rhport, request, (void*) &desc_url, desc_url.bLength);

        case VENDOR_REQUEST_MICROSOFT:
            if ( request->wIndex == 7 )
            {
                // Get Microsoft OS 2.0 compatible descriptor
                uint16_t total_len;
                memcpy(&total_len, desc_ms_os_20+8, 2);

                return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
            }else
            {
                return false;
            }
        case 0x22:
            // Webserial simulate the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect.
            web_serial_connected = (request->wValue != 0);

            // Always lit LED if connected
            if ( web_serial_connected )
            {
            #ifdef USE_LEDS
                board_led_write(true);
            #endif
                blink_interval_ms = BLINK_ALWAYS_ON;

                // tud_vendor_write_str("\r\nTinyUSB WebUSB device example\r\n");
            }else
            {
                blink_interval_ms = BLINK_MOUNTED;
            }

            // response with status OK
            return tud_control_status(rhport, request);
            break;

        default: break;
    }

    // stall unknown request
    return false;
}

// Invoked when DATA Stage of VENDOR's request is complete
bool tud_vendor_control_complete_cb(uint8_t rhport, tusb_control_request_t const * request)
{
    (void) rhport;
    (void) request;

    // nothing to do
    return true;
}

void handle_input_data(bool is_in_mobile_loop) {
    uint8_t buf_in[MAX_TRANSFER_BYTES*2];
    uint32_t count = tud_vendor_read((uint8_t*)buf_in, sizeof(buf_in));
    for(int i = count; i < (MAX_TRANSFER_BYTES*2); i++)
        buf_in[i] = 0;
    if(count > 1) {
        uint32_t reported_num = buf_in[0];
        if((reported_num & 0xC0) == 0xC0)
            interpret_debug_command(buf_in + 1, reported_num & 0x3F, count - 1, is_in_mobile_loop);
        else {
            if(reported_num > (count - 1))
                reported_num = count - 1;
            set_data_in(buf_in + 1, reported_num);
        }
    }
    uint8_t buf_out[MAX_TRANSFER_BYTES * 2];
    for(int i = 0; i < (MAX_TRANSFER_BYTES*2); i++)
        buf_out[i] = 0;
    bool success;
    for(int i = 1; i < MAX_TRANSFER_BYTES; i++) {
        buf_out[i] = get_data_out(&success);
        if(!success)
            break;
        buf_out[0]++;
    }
    if(!buf_out[0]) {
        for(int i = 1; i < MAX_TRANSFER_BYTES; i++) {
            buf_out[i] = get_data_out_debug(&success);
            if(!success)
                break;
            buf_out[0]++;
        }
        if(buf_out[0])
            buf_out[0] |= DEBUG_TRANSFER_FLAG;
    }
    echo_all((uint8_t*)buf_out, MAX_TRANSFER_BYTES);
}

void webserial_task(bool is_in_mobile_loop)
{
    if ( web_serial_connected )
        if ( tud_vendor_available() )
            handle_input_data(is_in_mobile_loop);
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(bool is_in_mobile_loop)
{
    if ( tud_cdc_connected() )
    // connected and there are data available
        if ( tud_cdc_available() )
            handle_input_data(is_in_mobile_loop);
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void) itf;

    // connected
    if ( dtr && rts )
    {
    // print initial message when connected
    // tud_cdc_write_str("\r\nTinyUSB WebUSB device example\r\n");
    }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    (void) itf;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
#ifdef USE_LEDS
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
#endif
}
