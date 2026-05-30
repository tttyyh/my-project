#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "time.h"

// ================= 引脚定义 (专为 S2 Mini 外排针脚纯手工定制!) =================
#define TFT_CS     16   // 左侧外排
#define TFT_DC     18   // 左侧外排
#define TFT_RST    33   // 左侧外排
#define TFT_SCLK   35   // 左侧外排
#define TFT_MOSI   37   // 左侧外排

#define SOIL_PIN   4    // 右侧外排 (ADC)
#define BEEP_PIN   6    // 右侧外排
#define BEEP_ON    HIGH
#define BEEP_OFF   LOW

// ================= WiFi 配置 =================
const char* ssid = "Titanic"; 
const char* password = "20060426"; 

const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600; 
const int   daylightOffset_sec = 0;

// ================= 对象实例化 (使用软件 SPI 随意定脚) =================
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

// ================= ?? 传感器校准参数 =================
int dryValue = 4095;  // 探头拿在空气中、擦干时的 AD 数值
int wetValue = 1500;  // 探头完全泡在水里的 AD 数值

// ================= 全局变量 =================
int currentMoisture = 0;   
int rawAnalogValue = 0;    
int alarmThreshold = 30;   
bool isMuted = false;      

char currentTimeStr[20] = "Syncing...";
unsigned long lastScreenUpdate = 0; 

// ================= 网页前端代码 =================
String HtmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='3'>"; 
  html += "<title>植物卫士 IoT 控制台</title>";
  html += "<style>body{text-align:center;font-family:'Segoe UI',sans-serif;background:#e8f5e9;} button{padding:12px 20px;font-size:16px;border:none;border-radius:8px;background:#4caf50;color:white;margin:5px;cursor:pointer;box-shadow: 0 2px 4px rgba(0,0,0,0.2); transition: 0.2s;} button:hover{opacity: 0.9;} .danger{background:#f44336;} .card{background:white;padding:30px;border-radius:15px;margin:20px auto;max-width:500px;box-shadow:0 10px 20px rgba(0,0,0,0.1);}</style></head><body>";
  html += "<h2 style='color:#2e7d32;'>? 智能植物卫士 IoT 控制台</h2><div class='card'>";
  html += "<p style='font-size:18px; color:#555;'>北京时间: <b>" + String(currentTimeStr) + "</b></p><hr style='border:0; border-top:1px solid #eee; margin:20px 0;'>";
  html += "<p>实时土壤湿度</p>";
  String color = currentMoisture < alarmThreshold ? "#f44336" : "#2e7d32";
  html += "<h1 style='color:" + color + ";font-size:72px;margin:10px 0;'>" + String(currentMoisture) + "%</h1>";
  if (currentMoisture < alarmThreshold) {
    html += "<p style='font-size:20px;'>状态：<b>?? 极度缺水，请立即浇水！</b></p>";
    if (isMuted) html += "<p style='color:#f44336;'>(警报已被手动静音)</p>";
    else html += "<p style='color:#f44336; animation: blink 1s infinite;'>(? 正在鸣笛报警 ?)</p>";
  } else {
    html += "<p style='font-size:20px;'>状态：<b>? 湿度完美，植物很健康</b></p>";
  }
  html += "<p style='color:#999;font-size:12px;margin-top:20px;'>当前报警线: 低于 " + String(alarmThreshold) + "% | 探头AD原始数值: " + String(rawAnalogValue) + "</p></div>";
  if (currentMoisture < alarmThreshold && !isMuted) {
    html += "<a href='/mute'><button class='danger'>? 强制关闭蜂鸣器报警</button></a><br><br>";
  }
  html += "<a href='/set20'><button>设为缺水线: 20%</button></a><a href='/set30'><button>设为缺水线: 30%</button></a><a href='/set50'><button>设为缺水线: 50%</button></a></body></html>";
  return html;
}

// ================= Web服务器路由 =================
void WebService() {
  server.on("/", []() { server.send(200, "text/html", HtmlPage()); });
  server.on("/mute", []() { isMuted = true; digitalWrite(BEEP_PIN, BEEP_OFF); server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>已静音，正在返回...</p>"); });
  server.on("/set20", []() { alarmThreshold = 20; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
  server.on("/set30", []() { alarmThreshold = 30; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
  server.on("/set50", []() { alarmThreshold = 50; server.send(200, "text/html", "<meta http-equiv='refresh' content='0; url=/'><p>设置成功</p>"); });
}

void updateLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  sprintf(currentTimeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void setup() {
  Serial.begin(115200);
  delay(100); 

  // ?? S2 特有保护：强制将模拟量分辨率设为 12 位 (0-4095)，以匹配你之前的干湿校准值！
  analogReadResolution(12);
  
  pinMode(BEEP_PIN, OUTPUT);
  digitalWrite(BEEP_PIN, BEEP_OFF);

  // 屏幕初始化 (如果花屏依然可以换成 INITR_GREENTAB)
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 30);
  tft.print("Plant Guard");
  
  WiFi.begin(ssid, password);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(15, 60);
  tft.print("Connecting WiFi...");
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    retry++;
  }

  tft.fillScreen(ST77XX_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(5, 20); tft.print("WiFi Connected!");
    tft.setCursor(5, 40); tft.print("IP: "); tft.print(WiFi.localIP());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println(WiFi.localIP());
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(5, 20); tft.print("WiFi Failed!");
    tft.setCursor(5, 40); tft.print("Offline Mode");
  }
  delay(2000);
  tft.fillScreen(ST77XX_BLACK); 

  WebService();
  server.begin();
}

void loop() {
  server.handleClient();
  updateLocalTime();

  rawAnalogValue = analogRead(SOIL_PIN);
  currentMoisture = map(rawAnalogValue, dryValue, wetValue, 0, 100);
  currentMoisture = constrain(currentMoisture, 0, 100); 

  if (currentMoisture >= alarmThreshold) {
    isMuted = false;
  }

  bool shouldAlarm = (currentMoisture < alarmThreshold) && !isMuted;
  if (shouldAlarm) {
    if ((millis() / 300) % 2 == 0) digitalWrite(BEEP_PIN, BEEP_ON);
    else digitalWrite(BEEP_PIN, BEEP_OFF);
  } else {
    digitalWrite(BEEP_PIN, BEEP_OFF);
  }

  if (millis() - lastScreenUpdate > 500) {
    lastScreenUpdate = millis();

    // 屏幕防闪烁优化绘制
    tft.fillRect(0, 0, 160, 20, ST77XX_BLUE);
    tft.setTextColor(ST77XX_WHITE); 
    tft.setTextSize(1);
    tft.setCursor(5, 6);
    tft.print("Time: ");
    tft.print(currentTimeStr);

    tft.fillRect(0, 30, 160, 20, ST77XX_BLACK); 
    if (currentMoisture < alarmThreshold) {
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.setCursor(15, 35);
      tft.print("NEED WATER!");
    } else {
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(1);
      tft.setCursor(20, 35);
      tft.print("Moisture Level:");
    }
    
    char moistureStr[10];
    sprintf(moistureStr, "%-3d%%", currentMoisture); 
    
    if (currentMoisture < alarmThreshold) {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    } else {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    }
    tft.setTextSize(4);
    tft.setCursor(40, 60);
    tft.print(moistureStr);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(5, 100);
    tft.print("IP: ");
    tft.print(WiFi.localIP().toString() + "    "); 
    
    tft.setCursor(5, 115);
    tft.print("WiFi Web Control Ready");
  }
}