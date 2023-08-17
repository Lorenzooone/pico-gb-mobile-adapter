#include "pico/stdlib.h"
#include "time.h"
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/pio.h"

#include "globals.h"

#include "lwip/apps/fs.h"
#include "tusb_lwip_glue.h"
#include "gb_printer.h"
#include "linkcable.h"

//#define OVERCLOCK

bool debug_enable = ENABLE_DEBUG;
bool speed_240_MHz = false;

// storage implementation
volatile uint32_t receive_data_pointer = 0;
uint8_t receive_data[BUFFER_SIZE_KB * 1024];    // buffer length is 96K

uint8_t json_buffer[1024] = {0};                // buffer for rendering of status json

void receive_data_reset(void) {
    receive_data_pointer = 0;
}

void receive_data_write(uint8_t b) {
    if (receive_data_pointer < sizeof(receive_data))
         receive_data[receive_data_pointer++] = b;
}

void receive_data_commit(void) {
}

// link cable
bool link_cable_data_received = false;
void link_cable_ISR(void) {
    uint32_t data = protocol_data_process(linkcable_receive());
    clean_linkcable_fifos();
    linkcable_send(data);
    link_cable_data_received = true;
}

int64_t link_cable_watchdog(alarm_id_t id, void *user_data) {
    if (!link_cable_data_received) {
        linkcable_reset();
        protocol_reset();
    } else link_cable_data_received = false;
    return MS(300);
}

// key button
#ifdef PIN_KEY
static void key_callback(uint gpio, uint32_t events) {
    linkcable_reset();
    protocol_reset();
    receive_data_reset();
    LED_OFF;
}
#endif

// Webserver dynamic handling
#define ROOT_PAGE   "/index.shtml"
#define IMAGE_FILE  "/image.bin"
#define STATUS_FILE "/status.json"
#define LIST_FILE   "/list.json"

static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen) {
    size_t printed = 0;
    switch (iIndex) {
        case 0:
            if (debug_enable) printed = snprintf(pcInsert, iInsertLen, "<a href=\"/reset_usb_boot\">Reset</a>");
            break;
        case 1:
            if (debug_enable) printed = snprintf(pcInsert, iInsertLen, "<a href=\"/image.bin\">Raw data</a>");
            break;
        default:
            break;
    }
    return (u16_t)printed;
}

static const char * ssi_tags[] = {"RESET", "RAWDATA" };

static const char *cgi_options(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (!strcmp(pcParam[i], "debug")) debug_enable = (!strcmp(pcValue[i], "on"));
    }
    return STATUS_FILE;
}

static const char *cgi_download(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    return IMAGE_FILE;
}

static const char *cgi_list(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    return LIST_FILE;
}

static const char *cgi_reset(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    receive_data_reset();
    protocol_reset();
    return STATUS_FILE;
}

static const char *cgi_reset_usb_boot(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    if (debug_enable) reset_usb_boot(0, 0);
    return ROOT_PAGE;
}

static const tCGI cgi_handlers[] = {
    { "/options",           cgi_options },
    { "/download",          cgi_download },
    { "/dumps/list",        cgi_list },
    { "/reset",             cgi_reset },
    { "/reset_usb_boot",    cgi_reset_usb_boot }
};

int fs_open_custom(struct fs_file *file, const char *name) {
    static const char *on_off[]     = {"off", "on"};
    static const char *true_false[] = {"false", "true"};
    if (!strcmp(name, IMAGE_FILE)) {
        // initialize fs_file correctly
        memset(file, 0, sizeof(struct fs_file));
        file->data  = (const char *)receive_data;
        file->len   = receive_data_pointer;
        file->index = file->len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    } else if (!strcmp(name, STATUS_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data  = json_buffer;
        file->len   = snprintf(json_buffer, sizeof(json_buffer),
                               "{\"result\":\"ok\"," \
                               "\"options\":{\"debug\":\"%s\"}," \
                               "\"status\":{\"received:\":%d},"\
                               "\"system\":{\"fast\":%s,\"buffer_size\":%d}}",
                               on_off[debug_enable],
                               receive_data_pointer,
                               true_false[speed_240_MHz], sizeof(receive_data));
        file->index = file->len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    } else if (!strcmp(name, LIST_FILE)) {
        memset(file, 0, sizeof(struct fs_file));
        file->data  = json_buffer;
        file->len   = snprintf(json_buffer, sizeof(json_buffer),
                               "{\"dumps\": [%s]}",
                               ((receive_data_pointer != 0) ? "\"/image.bin\"" : ""));
        file->index = file->len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    }
    return 0;
}

void fs_close_custom(struct fs_file *file) {
    LWIP_UNUSED_ARG(file);
}

// main loop
int main(void) {
#ifdef OVERCLOCK
    speed_240_MHz = set_sys_clock_khz(240000, false);
#endif

    // For toggle_led
#ifdef LED_PIN
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif
    LED_ON;

#ifdef PIN_KEY
    // set up key
    gpio_init(PIN_KEY);
    gpio_set_dir(PIN_KEY, GPIO_IN);
    gpio_set_irq_enabled_with_callback(PIN_KEY, GPIO_IRQ_EDGE_RISE, true, &key_callback);
#endif

    // Initialize tinyusb, lwip, dhcpd, dnsd and httpd
    init_lwip();

    wait_for_netif_is_up();
    dhcpd_init();
    dns_init();
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));

    linkcable_init(link_cable_ISR);

    //add_alarm_in_us(MS(300), link_cable_watchdog, NULL, true);

    LED_OFF;

    while (true) {
        // process USB
        tud_task();
        // process WEB
        service_traffic();
    }

    return 0;
}
