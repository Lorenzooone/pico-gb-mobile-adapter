#include "hardware/flash.h"
#include "save_load_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico_mobile_adapter.h"
#include "gbridge.h"

#define KEY_STR_SIZE 16
#define KEY_CONFIG "CONFIG"
#define OFFSET_CONFIG 0
#define OFFSET_MAGB KEY_STR_SIZE

#define FINAL_FLASH_MIN_SIZE (KEY_STR_SIZE + EEPROM_SIZE)

#define FLASH_PAGE_NEEDED ((FINAL_FLASH_MIN_SIZE + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)
#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * FLASH_PAGE_NEEDED)
#define FLASH_TARGET_OFFSET (FLASH_DATA_SIZE * 1024)

const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

static void *memmem(const void *l, size_t l_len, const void *s, size_t s_len){
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;

	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);

	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;

	return NULL;
}

//512 bytes for the Mobile Adapter GB + Adapter Configs + 16 for "CONFIG", and the rest 
static void FormatFlashConfig(void){
    DEBUG_PRINT_FUNCTION("Erasing target region... ");
    uint32_t irqs = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_DATA_SIZE);
    restore_interrupts(irqs);
    DEBUG_PRINT_FUNCTION("Done.\n");
}

//Read flash memory and set the configs
bool ReadConfig(uint8_t * buff, uint32_t size) {
    if(size > (FLASH_DATA_SIZE - OFFSET_MAGB))
        size = (FLASH_DATA_SIZE - OFFSET_MAGB);
    DEBUG_PRINT_FUNCTION("Reading the target region... ");
    char tmp_config[KEY_STR_SIZE];
    memset(tmp_config,0x00,KEY_STR_SIZE);
    memcpy(tmp_config,flash_target_contents+OFFSET_CONFIG,KEY_STR_SIZE);
    
    //Check if the Flash is already formated 
    if(memmem(tmp_config,strlen(KEY_CONFIG),KEY_CONFIG,strlen(KEY_CONFIG)) == NULL) {
        DEBUG_PRINT_FUNCTION("The flash memory is not formatted. Formatting now...");
        FormatFlashConfig();
        memset(buff,0x00,size);
    }
    else
        memcpy(buff,flash_target_contents+OFFSET_MAGB,size);

    DEBUG_PRINT_FUNCTION("Done.\n");
    return true;
}

void SaveConfig(uint8_t * buff, uint32_t size) {
    if(size > (FLASH_DATA_SIZE - OFFSET_MAGB))
        size = (FLASH_DATA_SIZE - OFFSET_MAGB);
    FormatFlashConfig();
    DEBUG_PRINT_FUNCTION("Programming target region... ");
    uint8_t tmp_buff[FLASH_DATA_SIZE];
    sprintf(tmp_buff,"%s",KEY_CONFIG);
    memcpy(tmp_buff+OFFSET_MAGB,buff,size);
    uint32_t irqs = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET, tmp_buff, FLASH_DATA_SIZE);
    restore_interrupts(irqs);
    DEBUG_PRINT_FUNCTION("Done.\n");
}
