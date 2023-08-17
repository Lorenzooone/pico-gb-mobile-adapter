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

#include "globals.h"

#include "gb_printer.h"
#include "linkcable.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

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

bool debug_enable = ENABLE_DEBUG;
bool speed_240_MHz = false;

//------------- prototypes -------------//

void handle_input_data(void);
void led_blinking_task(void);
void cdc_task(void);
void webserial_task(void);

// link cable
void link_cable_ISR(void) {
    uint32_t data = protocol_data_process(linkcable_receive());
    clean_linkcable_fifos();
    linkcable_send(data);
}

// main loop
int main(void) {
    board_init();

#ifdef OVERCLOCK
    speed_240_MHz = set_sys_clock_khz(240000, false);
#endif

    // Initialize tinyusb
    tusb_init();

    linkcable_init(link_cable_ISR);

    while (true) {
        tud_task(); // tinyusb device task
        cdc_task();
        webserial_task();
        led_blinking_task();
    }

    return 0;
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
                board_led_write(true);
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

void handle_input_data(void) {
    uint8_t buf_in[MAX_TRANSFER_BYTES*2];
    uint32_t count = tud_vendor_read(buf_in, sizeof(buf_in));
    for(int i = count; i < (MAX_TRANSFER_BYTES*2); i++)
        buf_in[i] = 0;
    // pprintf("Sending: %02x", buf[0]);
    uint8_t total_processed = 2;
    uint8_t buf_out[MAX_TRANSFER_BYTES*2];
    buf_out[0] = 'A';
    buf_out[1] = '\n';
    echo_all(buf_out, total_processed);
}

void webserial_task(void)
{
    if ( web_serial_connected )
        if ( tud_vendor_available() )
            handle_input_data();
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
    if ( tud_cdc_connected() )
    // connected and there are data available
        if ( tud_cdc_available() )
            handle_input_data();
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
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
    start_ms += blink_interval_ms;

    // LEDs lead to instability. Why? IDK. BUT DON'T USE LEDS!
    //board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}
