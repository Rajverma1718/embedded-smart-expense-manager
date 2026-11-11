#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>

constexpr uint8_t BTN_MEAL=13, BTN_TRAVEL=12, BTN_BILLS=14, BTN_RESET=27;
constexpr uint8_t BUZZER=26, LED_GREEN=25, LED_YELLOW=32, LED_RED=33;
constexpr uint16_t EEPROM_BYTES=512, SLOT_BYTES=64;
constexpr uint8_t SLOT_COUNT=EEPROM_BYTES/SLOT_BYTES;
constexpr uint32_t MAGIC=0x45585031UL;
constexpr uint16_t VERSION=2;
constexpr int32_t DEFAULT_BUDGET=5000;
constexpr uint32_t DEBOUNCE_MS=35, RESET_HOLD_MS=2000, PAGE_MS=4500, TICK_MS=250;

LiquidCrystal_I2C lcd(0x27,16,2);
RTC_DS1307 rtc;

struct State { uint32_t magic; uint16_t version; uint16_t reserved; uint32_t sequence; int32_t total; int32_t budget; uint32_t count; int32_t lastAmount; uint32_t lastTimestamp; uint16_t crc; uint8_t padding[30]; };
static_assert(sizeof(State)==SLOT_BYTES,"State must be 64 bytes");
struct Button { uint8_t pin; bool raw; bool stable; uint32_t changed; };
State st{};
Button buttons[]={{BTN_MEAL,HIGH,HIGH,0},{BTN_TRAVEL,HIGH,HIGH,0},{BTN_BILLS,HIGH,HIGH,0}};
uint8_t slot=0,page=0; uint32_t lastPage=0,lastTick=0,resetStart=0; bool resetDone=false,rtcOK=false; const char* level="NORMAL";

uint16_t crc16(const uint8_t* p,size_t n){uint16_t c=0xFFFF;for(size_t i=0;i<n;i++){c^=p[i];for(uint8_t b=0;b<8;b++)c=(c&1)?(c>>1)^0xA001:(c>>1);}return c;}
bool valid(const State& x){if(x.magic!=MAGIC||x.version!=VERSION||x.total<0||x.total>100000000||x.budget<100||x.budget>100000000)return false;State y=x;uint16_t c=y.crc;y.crc=0;return crc16((uint8_t*)&y,sizeof(y))==c;}
void seal(){st.magic=MAGIC;st.version=VERSION;st.reserved=0;st.crc=0;st.crc=crc16((uint8_t*)&st,sizeof(st));}
void defaults(){memset(&st,0,sizeof(st));st.magic=MAGIC;st.version=VERSION;st.budget=DEFAULT_BUDGET;st.sequence=1;slot=0;seal();EEPROM.put(0,st);EEPROM.commit();}
void load(){State x{};bool found=false;uint32_t newest=0;for(uint8_t s=0;s<SLOT_COUNT;s++){EEPROM.get(s*SLOT_BYTES,x);if(valid(x)&&(!found||x.sequence>newest)){st=x;newest=x.sequence;slot=s;found=true;}}if(!found)defaults();Serial.printf("AUDIT,BOOT,RECOVERED,seq=%lu,total=%ld,budget=%ld,count=%lu\n",(unsigned long)st.sequence,(long)st.total,(long)st.budget,(unsigned long)st.count);}
void save(){st.sequence++;slot=(slot+1)%SLOT_COUNT;seal();EEPROM.put(slot*SLOT_BYTES,st);if(!EEPROM.commit())Serial.println("AUDIT,ERROR,EEPROM_COMMIT_FAILED");}
DateTime now(){return rtcOK?rtc.now():DateTime(2000,1,1,0,0,0);}
void stamp(const DateTime& d){char b[24];snprintf(b,sizeof(b),"%04d-%02d-%02dT%02d:%02d:%02d",d.year(),d.month(),d.day(),d.hour(),d.minute(),d.second());Serial.print(b);}
int pct(){return st.budget?constrain((int)(((long)st.total*100L)/st.budget),0,999):100;}
void update(){int p=pct();level=p>=100?"CRITICAL":p>=80?"WARNING":"NORMAL";digitalWrite(LED_GREEN,!strcmp(level,"NORMAL"));digitalWrite(LED_YELLOW,!strcmp(level,"WARNING"));digitalWrite(LED_RED,!strcmp(level,"CRITICAL"));noTone(BUZZER);if(!strcmp(level,"CRITICAL")&&(millis()/500)%2==0)tone(BUZZER,2200);else if(!strcmp(level,"WARNING")&&millis()-lastTick<90)tone(BUZZER,1200);}
void message(const char* a,const char* b){lcd.clear();lcd.setCursor(0,0);lcd.print(a);lcd.setCursor(0,1);lcd.print(b);lastPage=millis();}
void pageShow(){update();DateTime d=now();lcd.clear();if(page==0){lcd.setCursor(0,0);lcd.print("Spent Rs");lcd.print(st.total);lcd.setCursor(0,1);lcd.print(level);lcd.print(" ");lcd.print(pct());lcd.print("% ");lcd.print(st.count);lcd.print("TX");}else if(page==1){lcd.setCursor(0,0);lcd.print("Budget Rs");lcd.print(st.budget);lcd.setCursor(0,1);lcd.print("Left Rs");lcd.print(max(0L,(long)st.budget-st.total));}else{lcd.setCursor(0,0);lcd.print("Last Rs");lcd.print(st.lastAmount);lcd.setCursor(0,1);char t[17];snprintf(t,sizeof(t),"%02d:%02d:%02d S%lu",d.hour(),d.minute(),d.second(),(unsigned long)st.sequence);lcd.print(t);}lastPage=millis();}
bool pressed(Button& b){bool r=digitalRead(b.pin);uint32_t t=millis();if(r!=b.raw){b.raw=r;b.changed=t;}if(t-b.changed>=DEBOUNCE_MS&&r!=b.stable){b.stable=r;return b.stable==LOW;}return false;}
void add(int32_t amount,const char* category){st.total+=amount;st.lastAmount=amount;st.count++;DateTime d=now();st.lastTimestamp=d.unixtime();save();Serial.print("AUDIT,EXPENSE,category=");Serial.print(category);Serial.print(",amount=");Serial.print(amount);Serial.print(",total=");Serial.print(st.total);Serial.print(",time=");stamp(d);Serial.println();char a[17],b[17];snprintf(a,sizeof(a),"+Rs%ld %s",(long)amount,category);snprintf(b,sizeof(b),"Total Rs%ld",(long)st.total);message(a,b);tone(BUZZER,1500,90);update();}
void resetLedger(){int32_t b=st.budget;uint32_t sequence=st.sequence;memset(&st,0,sizeof(st));st.budget=b;st.sequence=sequence;st.lastTimestamp=now().unixtime();save();Serial.println("AUDIT,ADMIN,LEDGER_RESET");message("Ledger reset","Budget kept");tone(BUZZER,700,250);update();}
void setup(){Serial.begin(115200);Wire.begin(21,22);pinMode(BTN_MEAL,INPUT_PULLUP);pinMode(BTN_TRAVEL,INPUT_PULLUP);pinMode(BTN_BILLS,INPUT_PULLUP);pinMode(BTN_RESET,INPUT_PULLUP);pinMode(BUZZER,OUTPUT);pinMode(LED_GREEN,OUTPUT);pinMode(LED_YELLOW,OUTPUT);pinMode(LED_RED,OUTPUT);EEPROM.begin(EEPROM_BYTES);load();lcd.init();lcd.backlight();rtcOK=rtc.begin();if(rtcOK&&!rtc.isrunning())rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));if(!rtcOK)Serial.println("AUDIT,ERROR,RTC_UNAVAILABLE");Serial.println("SMART EXPENSE MANAGER v2.0 READY");Serial.println("Controls: MEAL=100, TRAVEL=200, BILLS=500, RESET=hold 2s");message("Expense Manager","Enterprise v2.0");delay(1200);pageShow();}
void loop(){uint32_t t=millis();if(pressed(buttons[0])){add(100,"MEAL");page=0;}if(pressed(buttons[1])){add(200,"TRAVEL");page=0;}if(pressed(buttons[2])){add(500,"BILLS");page=0;}if(digitalRead(BTN_RESET)==LOW){if(!resetStart){resetStart=t;resetDone=false;message("Hold to reset","Release=cancel");}if(!resetDone&&t-resetStart>=RESET_HOLD_MS){resetDone=true;resetLedger();}}else{resetStart=0;resetDone=false;}if(t-lastTick>=TICK_MS){lastTick=t;update();}if(t-lastPage>=PAGE_MS){page=(page+1)%3;pageShow();}delay(2);}
