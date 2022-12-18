#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "usbkeyboard.h"
#include "bsp/board.h"
#include "tusb.h"
#include "../lcd-lib/LCDdriver.h"
#include "../lcd-lib/graphlib.h"


void led_blinking_task(int cursor_on) {
    const uint32_t interval_ms = 250;
    static uint32_t start_ms = 0;

    static bool led_state = false;
    static unsigned char cursorchar=0;

    // Blink every interval ms
    if (board_millis() - start_ms < interval_ms) return; // not enough time
    start_ms += interval_ms;
    board_led_write(led_state);
    led_state = 1 - led_state; // toggle

    if(cursor_on){
      cursorchar^=0x87;
      printchar(cursorchar);
      cursor--;
    }
}

void core1_entry(void){
  while(1){
    usbkb_polling();
    sleep_us(100);
  }
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
  if(!usbkb_init()){
      return 1;
  }
  printstr("Init USB OK\n");
  multicore_launch_core1(core1_entry);
  while(1){
    while(1){
      if(usbkb_mounted()){
        printstr("USB Keyboard found\n");
        break;
      }
      led_blinking_task(0);
      sleep_ms(16);
    }
    while (1) {
      if(usbkb_mounted()){
        uint8_t ch=usbkb_readkey();
        uint8_t vk=(uint8_t)vkey;
        uint8_t sh=vkey>>8;
        if(ch) printchar(ch);
        if(vk==VK_RETURN) printstr(" \n");
      }
      else{
        printstr("\nUSB Keyboard unmounted\n");
        break;
      }
      led_blinking_task(1);
      sleep_ms(16);
    }

  }
}
