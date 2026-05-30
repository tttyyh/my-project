#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "HX711.h"
#include "time.h"

// ================= 引脚定义 =================

// TFT 引脚
#define TFT_CS    1
#define TFT_DC    2
#define TFT_RST   3
#define TFT_BL    4

// TFT SPI 引脚
// 注意：如果你使用的是普通 ESP32，GPIO6/GPIO7 通常连接内部 Flash，不能使用。
// 如果屏幕仍然全白，请优先检查这里的引脚是否和你的实际接线一致。
#define TFT_SCLK  6
#define TFT_MOSI  7

// HX711 引脚
#define HX711_DT  5
#define HX711_SCK 8

// 蜂鸣器引脚
#define BEEP_PIN  10

// ================= 电平配置 =================

// TFT 背光电平
// 大多数 TFT 背光为 HIGH 点亮，如果你的屏幕背光相反，可以改为 LOW
#define TFT_BL_ON   HIGH
#define TFT_BL_OFF  LOW

// 蜂鸣器触发电平
// 很多有源蜂鸣器模块是低电平触发：LOW 响，HIGH 不响。
// 如果你的蜂鸣器是高电平触发，请改成：
// #define BEEP_ACTIVE_LEVEL HIGH
// #define BEEP_OFF_LEVEL    LOW
#define BEEP_ACTIVE_LEVEL LOW
#define BEEP_OFF_LEVEL    HIGH

// ================= WiFi 配置 =================

const char* ssid = "YourPhoneHotspot";
const char* password = "YourPassword";

const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;

// ================= 对象实例化 =================

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
HX711 scale;
WebServer server(80);

// ================= 全局变量 =================

// 演示用：默认 10 秒提醒一次
unsigned long reminderIntervalSec = 10;
unsigned long totalMs = reminderIntervalSec * 1000UL;

unsigned long startTime = 0;
bool isReminding = false;
int drinkCount = 0;

// 重量检测变量
long currentWeight = 0;

// 这个阈值需要你根据实际 HX711 裸数据调整
long threshold = 50000;

bool isCupRemoved = false;

// 时间记录
char lastDrinkTimeStr[20] = "未记录";
char currentTimeStr[20] = "获取中";

// 屏幕刷新控制
unsigned long lastScreenUpdate = 0;
const unsigned long screenUpdateInterval = 300;

// ================= 蜂鸣器控制函数 =================

void beepOff() {
  digitalWrite(BEEP_PIN, BEEP_OFF_LEVEL);
}

void beepOn() {
  digitalWrite(BEEP_PIN, BEEP_ACTIVE_LEVEL);
}

// ================= 网页前端代码 =================

String HtmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>智能水杯</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:sans-serif;background:#f4f4f9;}";
  html += "button{padding:12px 20px;font-size:16px;border:none;border-radius:8px;background:#007BFF;color:white;margin:5px;cursor:pointer;}";
  html += ".danger{background:#dc3545;}";
  html += ".card{background:white;padding:20px;border-radius:12px;margin:20px auto;max-width:400px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
  html += "</style></head><body>";

  html += "<h2>智能定时喝水控制系统</h2>";
  html += "<div class='card'>";

  html += "<p>当前设定间隔：<b>" + String(reminderIntervalSec) + "</b> 秒</p>";

  unsigned long now = millis();
  long remain = 0;

  if (now - startTime < totalMs) {
    remain = (totalMs - (now - startTime)) / 1000;
  } else {
    remain = 0;
  }

  html += "<p>距离下次喝水：<b>" + String(remain) + "</b> 秒</p>";
  html += "<p>当前状态：<b>";

  if (isReminding) {
    html += "正在提醒喝水";
  } else {
    html += "正常计时中";
  }

  html += "</b></p>";
  html += "<p>今日喝水次数：<b>" + String(drinkCount) + "</b></p>";
  html += "<p>上次喝水：<b>" + String(lastDrinkTimeStr) + "</b></p>";
  html += "<p>当前重量裸数据：<b>" + String(currentWeight) + "</b></p>";
  html += "</div>";

  html += "<a href='/reset'><button>手动重置计时</button></a><br><br>";
  html += "<a href='/close'><button class='danger'>关闭蜂鸣提醒</button></a>";

  html += "<h3>修改提醒时长</h3>";
  html += "<a href='/time10s'><button>10秒演示</button></a>";
  html += "<a href='/time30s'><button>30秒</button></a>";
  html += "<a href='/time60s'><button>60秒</button></a>";

  html += "</body></html>";
  return html;
}

// ================= 设置提醒时间 =================

void setReminderSeconds(unsigned long sec) {
  reminderIntervalSec = sec;
  totalMs = reminderIntervalSec * 1000UL;
  startTime = millis();
  isReminding = false;
  beepOff();
}

// ================= Web服务器路由 =================

void WebService() {
  server.on("/", []() {
    server.send(200, "text/html", HtmlPage());
  });

  server.on("/reset", []() {
    startTime = millis();
    isReminding = false;
    beepOff();
    server.send(200, "text/html", "<meta charset='UTF-8'>已重置计时<br><a href='/'>返回控制面板</a>");
  });

  server.on("/close", []() {
    isReminding = false;
    beepOff();
    server.send(200, "text/html", "<meta charset='UTF-8'>提醒已关闭<br><a href='/'>返回控制面板</a>");
  });

  server.on("/time10s", []() {
    setReminderSeconds(10);
    server.send(200, "text/html", "<meta charset='UTF-8'>已设置为10秒<br><a href='/'>返回</a>");
  });

  server.on("/time30s", []() {
    setReminderSeconds(30);
    server.send(200, "text/html", "<meta charset='UTF-8'>已设置为30秒<br><a href='/'>返回</a>");
  });

  server.on("/time60s", []() {
    setReminderSeconds(60);
    server.send(200, "text/html", "<meta charset='UTF-8'>已设置为60秒<br><a href='/'>返回</a>");
  });
}

// ================= 获取本地时间 =================

void updateLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    strcpy(currentTimeStr, "未同步");
    return;
  }

  sprintf(currentTimeStr, "%02d:%02d:%02d",
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);
}

// ================= 屏幕显示函数 =================

void drawScreen() {
  unsigned long now = millis();

  long leaveTime = 0;
  if (now - startTime < totalMs) {
    leaveTime = (totalMs - (now - startTime)) / 1000;
  } else {
    leaveTime = 0;
  }

  tft.fillScreen(ST77XX_BLACK);

  // 顶部时间栏
  tft.fillRect(0, 0, 160, 20, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 6);
  tft.print("Time: ");
  tft.print(currentTimeStr);

  // 中间提醒状态
  if (isReminding) {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 42);
    tft.print("Drink");
    tft.setCursor(10, 65);
    tft.print("Water!");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.setCursor(5, 35);
    tft.print("Next drink in:");

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(35, 55);

    int m = leaveTime / 60;
    int s = leaveTime % 60;

    if (m < 10) tft.print("0");
    tft.print(m);
    tft.print(":");
    if (s < 10) tft.print("0");
    tft.print(s);
  }

  // 底部数据
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(5, 95);
  tft.print("Last: ");
  tft.print(lastDrinkTimeStr);

  tft.setCursor(5, 108);
  tft.print("Count: ");
  tft.print(drinkCount);

  tft.setCursor(75, 108);
  tft.print("W:");
  tft.print(currentWeight);
}

// ================= 初始化 =================

void setup() {
  Serial.begin(115200);
  delay(200);

  // 蜂鸣器初始化
  // 先写关闭电平，再设置为输出，避免上电瞬间误响
  digitalWrite(BEEP_PIN, BEEP_OFF_LEVEL);
  pinMode(BEEP_PIN, OUTPUT);
  beepOff();

  // 背光初始化
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BL_ON);

  // 初始化 SPI
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // 初始化 TFT
  tft.initR(INITR_BLACKTAB);

  // 如果屏幕颜色异常或仍然显示异常，可以尝试下面这些初始化方式之一：
  // tft.initR(INITR_GREENTAB);
  // tft.initR(INITR_REDTAB);

  tft.setRotation(1);
  tft.invertDisplay(false);
  tft.fillScreen(ST77XX_BLACK);

  // TFT 测试显示
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 35);
  tft.print("Smart Cup");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(15, 70);
  tft.print("TFT Init OK");

  delay(1000);

  // 显示 WiFi 连接状态
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 40);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.print("Connecting WiFi...");

  WiFi.begin(ssid, password);

  // WiFi 连接增加超时，避免一直卡死
  unsigned long wifiStart = millis();
  const unsigned long wifiTimeout = 10000;

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < wifiTimeout) {
    delay(500);
    Serial.print(".");
  }

  tft.fillScreen(ST77XX_BLACK);

  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(5, 20);
    tft.print("WiFi Connected!");

    tft.setCursor(5, 40);
    tft.print("IP:");
    tft.print(WiFi.localIP());

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else {
    tft.setCursor(5, 20);
    tft.print("WiFi Failed");

    tft.setCursor(5, 40);
    tft.print("Web unavailable");

    strcpy(currentTimeStr, "无WiFi");
  }

  delay(1500);

  // 启动 Web 服务
  WebService();
  server.begin();

  // 初始化 HX711
  scale.begin(HX711_DT, HX711_SCK);

  // 如果需要开机自动去皮，可以取消下面这行注释
  // scale.tare();

  totalMs = reminderIntervalSec * 1000UL;
  startTime = millis();

  beepOff();
}

// ================= 主循环 =================

void loop() {
  server.handleClient();

  updateLocalTime();

  unsigned long now = millis();

  long leaveTime = 0;
  if (now - startTime < totalMs) {
    leaveTime = (totalMs - (now - startTime)) / 1000;
  } else {
    leaveTime = 0;
  }

  // 1. 处理称重传感器逻辑
  if (scale.is_ready()) {
    currentWeight = scale.read();

    // 当前重量低于阈值，认为杯子被拿走
    if (currentWeight < threshold) {
      isCupRemoved = true;
    }

    // 之前杯子被拿走，现在又放回，认为完成一次喝水
    else if (currentWeight > threshold && isCupRemoved) {
      startTime = millis();
      isReminding = false;
      beepOff();

      drinkCount++;
      isCupRemoved = false;

      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        sprintf(lastDrinkTimeStr, "%02d:%02d",
                timeinfo.tm_hour,
                timeinfo.tm_min);
      } else {
        strcpy(lastDrinkTimeStr, "未同步");
      }
    }
  }

  // 2. 检查是否到达提醒时间
  if (leaveTime <= 0 && !isReminding) {
    isReminding = true;
  }

  // 3. 控制蜂鸣器
  // 提醒状态下，蜂鸣器间歇鸣响
  if (isReminding) {
    if ((now / 500) % 2 == 0) {
      beepOn();
    } else {
      beepOff();
    }
  } else {
    beepOff();
  }

  // 4. 刷新屏幕
  if (now - lastScreenUpdate >= screenUpdateInterval) {
    lastScreenUpdate = now;
    drawScreen();
  }
}