#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "usbkeyboard.h"
#include "bsp/board.h"
#include "tusb.h"
#include "../lcd-lib/LCDdriver.h"
#include "../lcd-lib/graphlib.h"


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
#if CFG_TUH_CDC
CFG_TUSB_MEM_SECTION static char serial_in_buffer[64] = { 0 };

void tuh_mount_cb(uint8_t dev_addr)
{
  // application set-up
  printf("A device with address %d is mounted\r\n", dev_addr);

  tuh_cdc_receive(dev_addr, serial_in_buffer, sizeof(serial_in_buffer), true); // schedule first transfer
}

void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);
}

// invoked ISR context
void tuh_cdc_xfer_isr(uint8_t dev_addr, xfer_result_t event, cdc_pipeid_t pipe_id, uint32_t xferred_bytes)
{
  (void) event;
  (void) pipe_id;
  (void) xferred_bytes;

  printf(serial_in_buffer);
  tu_memclr(serial_in_buffer, sizeof(serial_in_buffer));

  tuh_cdc_receive(dev_addr, serial_in_buffer, sizeof(serial_in_buffer), true); // waiting for next data
}

void cdc_task(void)
{

}

#endif

void led_blinking_task(void) {
    const uint32_t interval_ms = 250;
    static uint32_t start_ms = 0;

    static bool led_state = false;
    static unsigned char cursorchar=0;

    // Blink every interval ms
    if (board_millis() - start_ms < interval_ms) return; // not enough time
    start_ms += interval_ms;
    board_led_write(led_state);
    led_state = 1 - led_state; // toggle

    cursorchar^=0x87;
    printchar(cursorchar);
    cursor--;
}

int main(void) {
	stdio_init_all();

	// 液晶用ポート設定
	// Enable SPI at 32 MHz and connect to GPIOs
	spi_init(LCD_SPICH, 32000 * 1000);
	gpio_set_function(LCD_SPI_RX, GPIO_FUNC_SPI);
	gpio_set_function(LCD_SPI_TX, GPIO_FUNC_SPI);
	gpio_set_function(LCD_SPI_SCK, GPIO_FUNC_SPI);

	gpio_init(LCD_CS);
	gpio_put(LCD_CS, 1);
	gpio_set_dir(LCD_CS, GPIO_OUT);
	gpio_init(LCD_DC);
	gpio_put(LCD_DC, 1);
	gpio_set_dir(LCD_DC, GPIO_OUT);
	gpio_init(LCD_RESET);
	gpio_put(LCD_RESET, 1);
	gpio_set_dir(LCD_RESET, GPIO_OUT);

	init_textgraph(HORIZONTAL); //液晶初期化、テキスト利用開始
//	init_textgraph(VERTICAL); //液晶初期化、テキスト利用開始

	gpio_init(PICO_DEFAULT_LED_PIN);  // on board LED
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    lockkey=1; // 下位3ビットが<SCRLK><CAPSLK><NUMLK>
    keytype=0; // 0：日本語109キー、1：英語104キー
    if(hidkb_init()){
        return 1;
    }
    printstr("Init USB OK\n");
    sleep_ms(100);

    while (1) {
      tuh_task();
      usbkb_polling_task();
//      lockkeychangedevent();

#if CFG_TUH_CDC
    cdc_task();
#endif
        uint8_t ch=usbreadkey();
        uint8_t vk=(uint8_t)vkey;
        uint8_t sh=vkey>>8;
        if(ch) printchar(ch);
        if(vk==VK_RETURN) printstr(" \n");
        led_blinking_task();
//        sleep_ms(16);
    }

    return 0;
}
