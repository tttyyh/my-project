#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <time.h>
#include <WiFi.h>
#include "pitches.h"

// ========== 屏幕配置 ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

#define SDA_PIN 33
#define SCL_PIN 35

// ========== 蜂鸣器配置 ==========
#define BUZZER_PIN 17      // 信号脚接 GPIO17

// ========== WiFi配置 ==========
const char* ssid     = "REDMI K80";
const char* password = "ffGfffHZ5f";

// ========== 时间配置 ==========
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;
const int   daylightOffset_sec = 0;

// ========== 全局变量 ==========
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
struct tm timeinfo;
char dateStr[20];
char timeStr[10];
char weekStr[10];

const char* weekdays[] = {"SUN", "MON", "TUES", "WED", "THUR", "FRI", "SAT"};

int lastHour = -1;       // 记录上一次整点报时的小时数
bool timeSynced = false; // 时间同步标志

// ========== 播放音符函数 ==========
void playTone(int frequency, int duration) {
  if (frequency == 0) {
    delay(duration);
    return;
  }
  
  ledcAttach(BUZZER_PIN, frequency, 8);
  ledcWrite(BUZZER_PIN, 128);
  delay(duration);
  ledcWrite(BUZZER_PIN, 0);
  ledcDetach(BUZZER_PIN);
  delay(duration * 0.1);  // 音符间停顿
}

// ========== 播放成功提示音（开机音乐） ==========
void playStartupMelody() {
  // 播放一段上升音阶 C4 -> C5
  int melody[] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5};
  for (int i = 0; i < 8; i++) {
    playTone(melody[i], 150);
  }
}

// ========== 播放整点报时音乐（《小星星》片段） ==========
void playHourlyChime() {
  int melody[] = {NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4};
  int durations[] = {4, 4, 4, 4, 4, 4, 2};
  
  for (int i = 0; i < 7; i++) {
    int noteDuration = 1000 / durations[i];
    playTone(melody[i], noteDuration);
  }
}

// ========== 播放整点半点报时（短提示） ==========
void playHalfHourChime() {
  // 两短一长
  playTone(NOTE_G4, 100);
  delay(100);
  playTone(NOTE_G4, 100);
  delay(100);
  playTone(NOTE_G4, 200);
}

// ========== 屏幕初始化 ==========
void initDisplay() {
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 初始化失败"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

// ========== 显示消息到屏幕 ==========
void showMessage(const char* line1, const char* line2 = nullptr) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  if (line2) {
    display.setCursor(0, 16);
    display.println(line2);
  }
  display.display();
}

// ========== 更新屏幕显示时间和日期 ==========
void updateDisplay() {
  display.clearDisplay();
  
  // 日期
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Date: ");
  display.println(dateStr);
  
  // 星期
  display.setCursor(0, 16);
  display.print("Week: ");
  display.println(weekStr);
  
  // 时间标签
  display.setTextSize(2);
  display.setCursor(0, 32);
  display.print("Time:");
  
  // 时间数值
  display.setCursor(0, 48);
  display.println(timeStr);
  
  display.display();
}

// ========== 初始化WiFi和时间 ==========
void initWiFiAndTime() {
  showMessage("Connecting WiFi...", ssid);
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    showMessage("WiFi Failed!", "Using default time");
    return;
  }
  
  showMessage("WiFi Connected!", "Syncing time...");
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (getLocalTime(&timeinfo)) {
    timeSynced = true;
    showMessage("Time Synced!", "Welcome!");
    playStartupMelody();  // 播放开机音乐
    delay(1500);
  } else {
    showMessage("Time Failed!", "Check network");
  }
}

// ========== 检查是否需要整点/半点报时 ==========
void checkHourlyChime() {
  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  
  // 整点报时（分钟为0，秒数小于2，避免重复触发）
  if (currentMinute == 0 && timeinfo.tm_sec < 2) {
    if (currentHour != lastHour) {
      lastHour = currentHour;
      playHourlyChime();  // 播放整点音乐
      Serial.print("整点报时: ");
      Serial.println(currentHour);
    }
  }
  // 半点报时（分钟为30，秒数小于2）
  else if (currentMinute == 30 && timeinfo.tm_sec < 2) {
    if (currentHour != lastHour) {  // 用 lastHour 来防止半小时内重复触发
      playHalfHourChime();
      Serial.println("半点报时");
    }
  }
  // 重置 lastHour，允许下一个半点触发
  else if (currentMinute != 0 && currentMinute != 30) {
    lastHour = -1;
  }
}

// ========== 主程序 ==========
void setup() {
  Serial.begin(115200);
  
  initDisplay();
  initWiFiAndTime();
  
  if (timeSynced) {
    lastHour = timeinfo.tm_hour;
  }
}

void loop() {
  if(!getLocalTime(&timeinfo)){
    Serial.println("获取时间失败");
    delay(1000);
    return;
  }
  
  // 格式化日期
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", 
           timeinfo.tm_year + 1900, 
           timeinfo.tm_mon + 1, 
           timeinfo.tm_mday);
  
  // 格式化时间
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  
  // 获取星期
  snprintf(weekStr, sizeof(weekStr), "%s", weekdays[timeinfo.tm_wday]);
  
  // 更新屏幕显示
  updateDisplay();
  
  // 检查是否需要整点/半点报时（仅在时间同步成功后）
  if (timeSynced) {
    checkHourlyChime();
  }
  
  delay(1000);
}