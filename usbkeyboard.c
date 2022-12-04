#include "bsp/board.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "usbkeyboard.h"
#include "../lcd-lib/LCDdriver.h"
#include "../lcd-lib/graphlib.h"

//#define USBKBDEBUG

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

// If your host terminal support ansi escape code, it can be use to simulate mouse cursor
#define USE_ANSI_ESCAPE   0
#define MAX_REPORT  4

uint16_t volatile ps2shiftkey_a; //シフト、コントロールキー等の状態（左右キー区別）
uint8_t volatile ps2shiftkey; //シフト、コントロールキー等の状態（左右キー区別なし）
uint16_t keycodebuf[KEYCODEBUFSIZE]; //キーコードバッファ
uint16_t * volatile keycodebufp1; //キーコード書き込み先頭ポインタ
uint16_t * volatile keycodebufp2; //キーコード読み出し先頭ポインタ

//公開変数
volatile uint8_t ps2keystatus[256]; // 仮想コードに相当するキーの状態（Onの時1）
volatile uint16_t vkey; // ps2readkey()関数でセットされるキーコード、上位8ビットはシフト関連キー
uint8_t lockkey; // 初期化時にLockキーの状態指定。下位3ビットが<SCRLK><CAPSLK><NUMLK>
uint8_t keytype; // キーボードの種類。0：日本語109キー、1：英語104キー
static bool lockkeychanged;

static uint8_t USBKB_dev_addr=0xFF;
static uint8_t USBKB_instance;
static hid_keyboard_report_t usbkb_report;

void lockkeycheck(uint8_t const vk){
  switch (vk)
  {
    case VK_NUMLOCK:
      ps2shiftkey_a^=CHK_NUMLK_A;
      ps2shiftkey  ^=CHK_NUMLK;
      lockkey ^= KEYBOARD_LED_NUMLOCK;
      lockkeychanged=true;
      break;
    case VK_CAPITAL:
      if((ps2shiftkey & CHK_SHIFT)==0) break;
      ps2shiftkey_a^=CHK_CAPSLK_A;
      ps2shiftkey  ^=CHK_CAPSLK;
      lockkey ^= KEYBOARD_LED_CAPSLOCK;
      lockkeychanged=true;
      break;
    case VK_SCROLL:
      ps2shiftkey_a^=CHK_SCRLK_A;
      ps2shiftkey  ^=CHK_SCRLK;
      lockkey ^= KEYBOARD_LED_SCROLLLOCK;
      lockkeychanged=true;
      break;
    default:
      break;
  }
}

void shiftkeycheck(uint8_t const modifier){
// SHIFT,ALT,CTRL,Winキーの押下状態を更新
  ps2shiftkey_a = (ps2shiftkey_a & 0xff00) | modifier;
	ps2shiftkey &= CHK_SCRLK | CHK_NUMLK | CHK_CAPSLK;
	if(ps2shiftkey_a & (CHK_SHIFT_L | CHK_SHIFT_R)) ps2shiftkey|=CHK_SHIFT;
	if(ps2shiftkey_a & (CHK_CTRL_L | CHK_CTRL_R)) ps2shiftkey|=CHK_CTRL;
	if(ps2shiftkey_a & (CHK_ALT_L | CHK_ALT_R)) ps2shiftkey|=CHK_ALT;
	if(ps2shiftkey_a & (CHK_WIN_L | CHK_WIN_R)) ps2shiftkey|=CHK_WIN;
}

#ifdef USBKBDEBUG
void dispkeys(hid_keyboard_report_t const *p_report);
#endif
void process_kbd_report(hid_keyboard_report_t const *p_new_report) {
#ifdef USBKBDEBUG
  dispkeys(p_new_report);
#endif
  // hid keyboard reportをusbkb_reportに取り込む
  // 実際の処理は別途定期的に行う
  // 複数キー同時押下エラー（キーコード=1が含まれる）場合無視する
  if(p_new_report->keycode[0]!=1) usbkb_report=*p_new_report;
}

void usbkb_task(void){
// 押下中のHIDキーコードを読み出し、仮想キーコードに変換
// keycodebufにためる
    static hid_keyboard_report_t prev_report = {0, 0, {0}}; // previous report to check key released
    static uint16_t oldvkey=0;
    static uint32_t keyrepeattime=0;
    uint16_t vkey;
    hid_keyboard_report_t *p_usbkb_report=&usbkb_report;

    if(USBKB_dev_addr==0xFF) return;
    vkey=0;
    shiftkeycheck(p_usbkb_report->modifier);
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t vk;
        if(keytype==1) vk=hidkey2virtualkey_en[p_usbkb_report->keycode[i]];
        else vk=hidkey2virtualkey_jp[p_usbkb_report->keycode[i]];
        if(vk==0) continue;
        vkey=vk;
        if(ps2keystatus[vk]) continue; // 前回も押されていた場合は無視
        if((ps2shiftkey & CHK_CTRL)==0) lockkeycheck(vk); //NumLock、CapsLock、ScrollLock反転処理
        if((keycodebufp1+1==keycodebufp2) ||
            (keycodebufp1==keycodebuf+KEYCODEBUFSIZE-1)&&(keycodebufp2==keycodebuf)){
            break; //バッファがいっぱいの場合無視
        }
        *keycodebufp1++=((uint16_t)ps2shiftkey<<8)+vk;
        if(keycodebufp1==keycodebuf+KEYCODEBUFSIZE) keycodebufp1=keycodebuf;
    }
    vkey|=(uint16_t)ps2shiftkey<<8;
    if(vkey & 0xff && vkey==oldvkey){
        if(time_us_32() >= keyrepeattime){
            keyrepeattime+=KEYREPEAT2*1000;
            if((keycodebufp1+1!=keycodebufp2) &&
                    (keycodebufp1!=keycodebuf+KEYCODEBUFSIZE-1)||(keycodebufp2!=keycodebuf)){
                *keycodebufp1++=vkey;
                if(keycodebufp1==keycodebuf+KEYCODEBUFSIZE) keycodebufp1=keycodebuf;
            }
        }
    }
    else{
        oldvkey=vkey;
        keyrepeattime=time_us_32()+KEYREPEAT1*1000;
    }
    // 前回押されていたキーステータスをいったん全てクリア
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t vk;
        if(keytype==1) vk=hidkey2virtualkey_en[prev_report.keycode[i]];
        else vk=hidkey2virtualkey_jp[prev_report.keycode[i]];
        if(vk) ps2keystatus[vk]=0;
    }
    // 今回押されているキーステータスをセット
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t vk;
        if(keytype==1) vk=hidkey2virtualkey_en[p_usbkb_report->keycode[i]];
        else vk=hidkey2virtualkey_jp[p_usbkb_report->keycode[i]];
        if(vk) ps2keystatus[vk]=1;
    }
    // シフト関連キーのステータスを書き換え
    if(p_usbkb_report->modifier & CHK_SHIFT_L) ps2keystatus[VK_LSHIFT]=1; else ps2keystatus[VK_LSHIFT]=0;
    if(p_usbkb_report->modifier & CHK_SHIFT_R) ps2keystatus[VK_RSHIFT]=1; else ps2keystatus[VK_RSHIFT]=0;
    if(p_usbkb_report->modifier & CHK_CTRL_L) ps2keystatus[VK_LCONTROL]=1; else ps2keystatus[VK_LCONTROL]=0;
    if(p_usbkb_report->modifier & CHK_CTRL_R) ps2keystatus[VK_RCONTROL]=1; else ps2keystatus[VK_RCONTROL]=0;
    if(p_usbkb_report->modifier & CHK_ALT_L) ps2keystatus[VK_LMENU]=1; else ps2keystatus[VK_LMENU]=0;
    if(p_usbkb_report->modifier & CHK_ALT_R) ps2keystatus[VK_RMENU]=1; else ps2keystatus[VK_RMENU]=0;
    if(p_usbkb_report->modifier & CHK_WIN_L) ps2keystatus[VK_LWIN]=1; else ps2keystatus[VK_LWIN]=0;
    if(p_usbkb_report->modifier & CHK_WIN_R) ps2keystatus[VK_RWIN]=1; else ps2keystatus[VK_RWIN]=0;

  if(lockkeychanged){
    // Set Lock keys LED
    tuh_hid_set_report(USBKB_dev_addr,USBKB_instance,0,HID_REPORT_TYPE_OUTPUT,&lockkey,sizeof(lockkey));
#ifdef USBKBDEBUG
    printnum(lockkey);
    printstr("Set LED completed\n");
#endif
    lockkeychanged=false;
  }

    prev_report = *p_usbkb_report;
}

#ifdef USBKBDEBUG
static void printhex2(int i){
    int h=(i>>4)&0xf;
    if(h<10) printchar('0'+h);
    else printchar('A'+h-10);
    h=i&0xf;
    if(h<10) printchar('0'+h);
    else printchar('A'+h-10);
}
void dispkeys(hid_keyboard_report_t const *p_report){
    unsigned char *cursor2;
    cursor2=cursor;
    setcursor(0,0,7);
    for(int i=0;i<6;i++){
        printchar('[');
        printhex2(p_report->keycode[i]);
        printchar(']');
    }

    setcursor(0,1,7);
    uint8_t sh=p_report->modifier;
    if(sh & KEYBOARD_MODIFIER_LEFTCTRL) printstr("LCTR ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_LEFTSHIFT) printstr("LSFT ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_LEFTALT) printstr("LALT ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_LEFTGUI) printstr("LWIN ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_RIGHTCTRL) printstr("RCTR ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_RIGHTSHIFT) printstr("RSFT ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_RIGHTALT) printstr("RALT ");
    else printstr("     ");
    if(sh & KEYBOARD_MODIFIER_RIGHTGUI) printstr("RWIN ");
    else printstr("     ");

    setcursor(0,2,4);
    if(lockkey & KEYBOARD_LED_NUMLOCK) printstr("NUM ");
    else printstr("    ");
    if(lockkey & KEYBOARD_LED_CAPSLOCK) printstr("CAP ");
    else printstr("    ");
    if(lockkey & KEYBOARD_LED_SCROLLLOCK) printstr("SCR ");
    else printstr("    ");

    cursor=cursor2;
    setcursorcolor(7);
}
#endif

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

void cursor_movement(int8_t x, int8_t y, int8_t wheel)
{
#if USE_ANSI_ESCAPE
  // Move X using ansi escape
  if ( x < 0)
  {
    printf(ANSI_CURSOR_BACKWARD(%d), (-x)); // move left
  }else if ( x > 0)
  {
    printf(ANSI_CURSOR_FORWARD(%d), x); // move right
  }

  // Move Y using ansi escape
  if ( y < 0)
  {
    printf(ANSI_CURSOR_UP(%d), (-y)); // move up
  }else if ( y > 0)
  {
    printf(ANSI_CURSOR_DOWN(%d), y); // move down
  }

  // Scroll using ansi escape
  if (wheel < 0)
  {
    printf(ANSI_SCROLL_UP(%d), (-wheel)); // scroll up
  }else if (wheel > 0)
  {
    printf(ANSI_SCROLL_DOWN(%d), wheel); // scroll down
  }

  printf("\r\n");
#else
//  printstr("(%d %d %d)\r\n", x, y, wheel);
#endif
}

static void process_mouse_report(hid_mouse_report_t const * report)
{

}

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) dev_addr;

  uint8_t const rpt_count = hid_info[instance].report_count;
  tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  }else
  {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the arrray
    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
//    printf("Couldn't find the report info for this report !\r\n");
    return;
  }

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        TU_LOG1("HID receive keyboard report\r\n");
        // Assume keyboard follow boot report layout
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;

      case HID_USAGE_DESKTOP_MOUSE:
        TU_LOG1("HID receive mouse report\r\n");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;

      default: break;
    }
  }
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

if(!usbkb_mounted()) return;

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      TU_LOG2("HID receive boot keyboard report\r\n");
      process_kbd_report( (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      TU_LOG2("HID receive boot mouse report\r\n");
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printstr("Error: cannot request to receive report\n");
  }
}

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
#ifdef USBKBDEBUG
  printstr("HID device address = ");
  printnum(dev_addr);
  printstr(", instance = ");
  printnum(instance);
  printstr(" is mounted\n");
#endif

  // Interface protocol (hid_interface_protocol_enum_t)
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
#ifdef USBKBDEBUG
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  printstr("HID has ");
  printnum(hid_info[instance].report_count);
  printstr("reports and interface protocol = ");
  printstr((unsigned char *)protocol_str[itf_protocol]);
  printchar('\n');
#endif

  if(itf_protocol==1){ //HIDキーボードの場合
    USBKB_dev_addr=dev_addr;
    USBKB_instance=instance;
    keycodebufp1=keycodebuf;
    keycodebufp2=keycodebuf;
    ps2shiftkey_a=(uint16_t)lockkey<<8; //Lock関連キーを変数lockkeyで初期化
    ps2shiftkey=lockkey<<4; //Lock関連キーを変数lockkeyで初期化
    for(int i=0;i<256;i++) ps2keystatus[i]=0; //全キー離した状態
    lockkeychanged=true;
  }

  // request to receive report
  // tuh_hid_report_received_cb() will be invoked when report is available
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
#ifdef USBKBDEBUG
    printstr("Error: cannot request to receive report\n");
#endif
  }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len)
{
#ifdef USBKBDEBUG
  printstr("HID set report completed\n");
  printstr("dev_addr ");printnum(dev_addr);printchar('\n');
  printstr("instance ");printnum(instance);printchar('\n');
  printstr("report_id ");printnum(report_id);printchar('\n');
  printstr("report_type");printnum(report_type);printchar('\n');
  printstr("len ");printnum(len);printchar('\n');
#endif
}
void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
#ifdef USBKBDEBUG
  printstr("HID report sent\n");
  printstr("dev_addr ");printnum(dev_addr);printchar('\n');
  printstr("instance ");printnum(instance);printchar('\n');
  printstr("len ");printnum(len);printchar('\n');
  printstr("report ");
  for(int i=0;i<len;i++) {printhex2(*report++);printchar(' ');}
  printchar('\n');
#endif
}


// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  if(dev_addr==USBKB_dev_addr){
    USBKB_dev_addr=0xFF;
  }
#ifdef USBKBDEBUG
  printstr("HID device address = ");
  printnum(dev_addr);
  printstr(", instance = ");
  printnum(instance);
  printstr(" is unmounted\n");
#endif
}

bool usbkb_init(void){
//    printstr("Initializing USB...");
    tusb_init();
//    printstr("OK\n");
    return 0;
}

void usbkb_polling(void){
  tuh_task();
  usbkb_task();
}

bool usbkb_mounted(void){
  return USBKB_dev_addr!=0xFF?true:false;
}
uint8_t usbreadkey(void){
// 入力された1つのキーのキーコードをグローバル変数vkeyに格納（押されていなければ0を返す）
// 下位8ビット：キーコード
// 上位8ビット：シフト状態
// 英数・記号文字の場合、戻り値としてASCIIコード（それ以外は0を返す）

	uint16_t k;
	uint8_t sh;

	vkey=0;
	if(keycodebufp1==keycodebufp2) return 0;
	vkey=*keycodebufp2++;
	if(keycodebufp2==keycodebuf+KEYCODEBUFSIZE) keycodebufp2=keycodebuf;
	sh=vkey>>8;
	if(sh & (CHK_CTRL | CHK_ALT | CHK_WIN)) return 0;
	k=vkey & 0xff;
	if(keytype==1){
	//英語キーボード
		if(k>='A' && k<='Z'){
			//SHIFTまたはCapsLock（両方ではない）
			if((sh & (CHK_SHIFT | CHK_CAPSLK))==CHK_SHIFT || (sh & (CHK_SHIFT | CHK_CAPSLK))==CHK_CAPSLK)
				return vk2asc2_en[k];
			else return vk2asc1_en[k];
		}
		if(k>=VK_NUMPAD0 && k<=VK_DIVIDE){ //テンキー
			if((sh & (CHK_SHIFT | CHK_NUMLK))==CHK_NUMLK) //NumLock（SHIFT＋NumLockは無効）
				return vk2asc2_en[k];
			else return vk2asc1_en[k];
		}
		if(sh & CHK_SHIFT) return vk2asc2_en[k];
		else return vk2asc1_en[k];
	}

	if(sh & CHK_SCRLK){
	//日本語キーボード（カナモード）
		if(k>=VK_NUMPAD0 && k<=VK_DIVIDE){ //テンキー
			if((sh & (CHK_SHIFT | CHK_NUMLK))==CHK_NUMLK) //NumLock（SHIFT＋NumLockは無効）
				return vk2kana2[k];
			else return vk2kana1[k];
		}
		if(sh & CHK_SHIFT) return vk2kana2[k];
		else return vk2kana1[k];
	}

	//日本語キーボード（英数モード）
	if(k>='A' && k<='Z'){
		//SHIFTまたはCapsLock（両方ではない）
		if((sh & (CHK_SHIFT | CHK_CAPSLK))==CHK_SHIFT || (sh & (CHK_SHIFT | CHK_CAPSLK))==CHK_CAPSLK)
			return vk2asc2_jp[k];
		else return vk2asc1_jp[k];
	}
	if(k>=VK_NUMPAD0 && k<=VK_DIVIDE){ //テンキー
		if((sh & (CHK_SHIFT | CHK_NUMLK))==CHK_NUMLK) //NumLock（SHIFT＋NumLockは無効）
			return vk2asc2_jp[k];
		else return vk2asc1_jp[k];
	}
	if(sh & CHK_SHIFT) return vk2asc2_jp[k];
	else return vk2asc1_jp[k];
}

uint8_t shiftkeys(void){
// SHIFT関連キーの押下状態を返す
// 上位から<0><SCRLK><CAPSLK><NUMLK><Wiin><ALT><SHIFT><CTRL>
	return ps2shiftkey;
}
