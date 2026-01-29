
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h> 
#include <TFT_eSPI.h> 
#include <time.h>     
#include <EEPROM.h>

// ================= 继电器电平逻辑 =================
#define RELAY_ACTIVE_LEVEL LOW  // 吸合
#define RELAY_SAFE_LEVEL   HIGH // 断开

// ================= Dark Colors =================
#define D_BG 0x0000 
#define D_TEXT 0xFFFF 
#define D_GRID 0x18E3 
#define D_BLUE 0x04F9 
#define D_GREEN 0x05E5 
#define D_YELLOW 0xFFE0 
#define D_RED 0xF800 
#define D_SUBTEXT 0x9CD3 

// ================= WiFi =================
struct WiFiCreds { const char* ssid; const char* password; };
WiFiCreds primaryWifi = { "Wi-Finame1", "Wi-FiPassword"};
WiFiCreds backupWifi  = { "Wi-Finame2", "Wi-FiPassword" }; //如不需要请留空,仅填写第一个
// ================= Objects =================
TFT_eSPI tft = TFT_eSPI();
ESP8266WebServer server(80);

// 继电器 1 接 D0 (GPIO 16) - 对应板载红灯，动的时候红灯亮是正常的
// 继电器 2 接 D6 (GPIO 12) - MISO引脚，初始化顺序修正后可正常使用
const int PIN_RELAY1 = 16; 
const int PIN_RELAY2 = 12; 

// ================= Global Vars =================
time_t now; struct tm *timeinfo; time_t last_time = 0; char timeStr[20];
bool systemPaused = false;
unsigned long lastWiFiCheck = 0;
int mac_cpu = 0; int mac_ram = 0;
String mac_up = "0K/s"; String mac_down = "0K/s";
unsigned long lastDataTime = 0; 
#define HISTORY_LEN 40
int cpuHistory[HISTORY_LEN]; 
char name1[32] = "Device 1"; char name2[32] = "Device 2"; 
const int MAGIC_CODE = 54338; // 更新 Magic Code 以重置配置

class AsyncRelay {
  private: int _pin; bool _state = RELAY_SAFE_LEVEL; bool _isCycling = false; unsigned long _lastSwitch = 0;
  public: int onTime = 1000; int offTime = 1000;
    // 初始化函数
    void begin(int p) {
      _pin = p;
      digitalWrite(_pin, RELAY_SAFE_LEVEL); 
      pinMode(_pin, OUTPUT); 
      digitalWrite(_pin, RELAY_SAFE_LEVEL); // 双重保险
      _state = RELAY_SAFE_LEVEL;
    }
    // 循环逻辑
    void loop(bool p) {
      if (!_isCycling || p) return; 
      unsigned long t = millis();
      bool isOn = (_state == RELAY_ACTIVE_LEVEL);
      if (isOn && (t - _lastSwitch >= onTime)) setState(RELAY_SAFE_LEVEL);
      else if (!isOn && (t - _lastSwitch >= offTime)) setState(RELAY_ACTIVE_LEVEL);
    }
    void manualToggle() { _isCycling = false; setState((_state == RELAY_ACTIVE_LEVEL) ? RELAY_SAFE_LEVEL : RELAY_ACTIVE_LEVEL); }
    void toggleCycle() { _isCycling = !_isCycling; if (_isCycling) { _lastSwitch = millis(); setState(RELAY_SAFE_LEVEL); } else { setState(RELAY_SAFE_LEVEL); } }
    bool isCycling() { return _isCycling; }
    int getStateForWeb() { return (_state == RELAY_ACTIVE_LEVEL) ? 0 : 1; }
    void restoreSettings(int on, int off, char* n) { onTime = on; offTime = off; _state = RELAY_SAFE_LEVEL; _isCycling = false; digitalWrite(_pin, _state); }
    void setCycleState(bool a) { _isCycling = a; if (a) _lastSwitch = millis(); else setState(RELAY_SAFE_LEVEL); }
  private: void setState(bool v) { _state = v; digitalWrite(_pin, _state); _lastSwitch = millis(); }
};
AsyncRelay relay1; AsyncRelay relay2;

// ================= EEPROM =================
struct Settings { int magic; bool paused; int r1_on,r1_off; bool r1_cyc,r1_st; char r1_n[32]; int r2_on,r2_off; bool r2_cyc,r2_st; char r2_n[32]; };
void saveConfig(){ Settings d={MAGIC_CODE,systemPaused,relay1.onTime,relay1.offTime,relay1.isCycling(),(relay1.getStateForWeb()==0),"",relay2.onTime,relay2.offTime,relay2.isCycling(),(relay2.getStateForWeb()==0),""}; strncpy(d.r1_n,name1,32);strncpy(d.r2_n,name2,32);EEPROM.put(0,d);EEPROM.commit(); }
void loadConfig(){ Settings d; EEPROM.get(0,d); if(d.magic==MAGIC_CODE){ systemPaused = d.paused; relay1.restoreSettings(d.r1_on, d.r1_off, d.r1_n); relay2.restoreSettings(d.r2_on, d.r2_off, d.r2_n); strncpy(name1, d.r1_n, 32); strncpy(name2, d.r2_n, 32); } else { saveConfig(); } }
void checkWiFi(){if(WiFi.status()==WL_CONNECTED)return;tft.fillScreen(D_BG);tft.setTextColor(D_TEXT);tft.drawCentreString("Connecting...",120,110,2);WiFi.begin(primaryWifi.ssid,primaryWifi.password);for(int i=0;i<15&&WiFi.status()!=WL_CONNECTED;i++)delay(500);if(WiFi.status()!=WL_CONNECTED){WiFi.begin(backupWifi.ssid,backupWifi.password);for(int i=0;i<20&&WiFi.status()!=WL_CONNECTED;i++)delay(500);}tft.fillScreen(D_BG);}

// ================= Setup (关键逻辑修正) =================
void setup() {
  Serial.begin(115200); EEPROM.begin(1024);
  
  // 1. 【第一步】先初始化屏幕
  //tft.init() 会操作 SPI 引脚。
  // 如果它在后面运行，可能会把 D6 重置为输入模式，导致继电器失效。
  tft.init(); 
  tft.setRotation(0); 
  tft.invertDisplay(true); // 黑底
  tft.fillScreen(D_BG);

  // 2. 【第二步】再初始化继电器
  // 强制把 D0 和 D6 设定为输出模式
  relay1.begin(PIN_RELAY1); 
  relay2.begin(PIN_RELAY2);

  for(int i=0; i<HISTORY_LEN; i++) cpuHistory[i] = 0;

  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(D_TEXT); tft.drawString("Mac Monitor Pro", 120, 100, 4);
  
  loadConfig(); checkWiFi();
  configTime(8*3600,0,"ntp.aliyun.com");

  ArduinoOTA.setHostname("ESP8266-Monitor");
  ArduinoOTA.begin();

  // API 接口
  server.on("/api/mac", [](){
    if(server.hasArg("cpu")) mac_cpu=server.arg("cpu").toInt();
    if(server.hasArg("ram")) mac_ram=server.arg("ram").toInt();
    if(server.hasArg("up"))  mac_up=server.arg("up");
    if(server.hasArg("down")) mac_down=server.arg("down");
    for(int i=0;i<HISTORY_LEN-1;i++) cpuHistory[i]=cpuHistory[i+1];
    cpuHistory[HISTORY_LEN-1]=mac_cpu;
    lastDataTime=millis(); server.send(200,"text/plain","OK"); updateDisplay();
  });

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/master", [](){ systemPaused=!systemPaused; if(systemPaused){relay1.setCycleState(0);relay2.setCycleState(0);}else{relay1.setCycleState(relay1.isCycling());relay2.setCycleState(relay2.isCycling());} saveConfig(); server.send(200,"text/json","{}"); });
  server.on("/api/r1/toggle",[](){relay1.manualToggle();saveConfig();server.send(200);});
  server.on("/api/r1/cycle",[](){relay1.toggleCycle();saveConfig();server.send(200);});
  server.on("/api/r1/set",[](){if(server.hasArg("on"))relay1.onTime=server.arg("on").toInt();if(server.hasArg("off"))relay1.offTime=server.arg("off").toInt();if(server.hasArg("name")){String n=server.arg("name");n.toCharArray(name1,32);}saveConfig();server.send(200);});
  server.on("/api/r2/toggle",[](){relay2.manualToggle();saveConfig();server.send(200);});
  server.on("/api/r2/cycle",[](){relay2.toggleCycle();saveConfig();server.send(200);});
  server.on("/api/r2/set",[](){if(server.hasArg("on"))relay2.onTime=server.arg("on").toInt();if(server.hasArg("off"))relay2.offTime=server.arg("off").toInt();if(server.hasArg("name")){String n=server.arg("name");n.toCharArray(name2,32);}saveConfig();server.send(200);});

  server.begin(); tft.fillScreen(D_BG);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  relay1.loop(systemPaused); relay2.loop(systemPaused);
  time(&now);
  if(now!=last_time){ last_time=now; timeinfo=localtime(&now); if(millis()-lastDataTime>2000) updateDisplay(); }
  if(millis()-lastWiFiCheck>30000){ if(WiFi.status()!=WL_CONNECTED) checkWiFi(); lastWiFiCheck=millis(); }
}

// ================= UI Drawing =================
void updateDisplay() {
  bool isConnected = (millis() - lastDataTime <= 5000);
  static bool wasConnected = false; 
  if (isConnected != wasConnected) { tft.fillScreen(D_BG); wasConnected = isConnected; }

  if(timeinfo->tm_year+1900>2000) strftime(timeStr,sizeof(timeStr),"%H:%M",timeinfo); else sprintf(timeStr,"--:--");
  tft.setTextColor(D_TEXT,D_BG); tft.setTextDatum(TL_DATUM); tft.drawString(timeStr,15,8,4);

  if (isConnected) {
    uint16_t dotColor = (millis()/800)%2==0 ? D_BLUE : D_BG;
    tft.fillCircle(220,20,4,dotColor);
    uint16_t waveColor = D_BLUE;
    if(mac_cpu > 50) waveColor = D_YELLOW;
    if(mac_cpu > 80) waveColor = D_RED;
    
    tft.fillRect(15,50,210,80,D_BG); tft.drawRect(15,50,210,80,D_GRID); tft.drawFastHLine(15,90,210,D_GRID);
    tft.setTextColor(waveColor, D_BG); tft.setTextDatum(TL_DATUM); tft.drawString("CPU", 18, 54, 2);
    tft.setTextDatum(TR_DATUM); tft.drawString(String(mac_cpu)+"%", 222, 54, 2);
    int step = 210/(HISTORY_LEN-1);
    for(int i=0;i<HISTORY_LEN-1;i++){
      int y1=map(cpuHistory[i],0,100,128,52); int y2=map(cpuHistory[i+1],0,100,128,52);
      tft.drawLine(15+i*step, y1, 15+(i+1)*step, y2, waveColor); tft.drawLine(15+i*step, y1+1, 15+(i+1)*step, y2+1, waveColor);
    }
    tft.setTextColor(D_TEXT, D_BG); tft.setTextDatum(BL_DATUM); tft.drawString("RAM", 15, 155, 2);
    tft.setTextColor(D_SUBTEXT, D_BG); tft.setTextDatum(BR_DATUM); tft.drawString(String(mac_ram)+"%", 225, 155, 2);
    tft.fillRoundRect(15,160,210,10,4,D_GRID); 
    int ramW = map(mac_ram,0,100,0,210); uint16_t ramC = (mac_ram>80)?D_RED:((mac_ram>60)?D_YELLOW:D_GREEN);
    tft.fillRoundRect(15,160,ramW,10,4,ramC);

    tft.drawFastHLine(15, 185, 210, D_GRID); 
    tft.setTextColor(D_SUBTEXT, D_BG); tft.setTextDatum(TL_DATUM); tft.drawString("UP", 15, 195, 2);
    tft.setTextColor(D_TEXT, D_BG); tft.setTextDatum(TR_DATUM); tft.drawString(mac_up, 110, 195, 2); 
    tft.setTextColor(D_SUBTEXT, D_BG); tft.setTextDatum(TL_DATUM); tft.drawString("DOWN", 125, 195, 2);
    tft.setTextColor(D_TEXT, D_BG); tft.setTextDatum(TR_DATUM); tft.drawString(mac_down, 225, 195, 2);
    tft.setTextColor(D_TEXT, D_BG); tft.setTextDatum(BC_DATUM); tft.drawString(WiFi.localIP().toString(), 120, 235, 2); 
  } else {
    tft.setTextColor(D_SUBTEXT,D_BG); tft.setTextDatum(MC_DATUM); tft.drawString("Waiting for Mac...",120,120,4);
    tft.setTextColor(D_TEXT, D_BG); tft.setTextDatum(BC_DATUM); tft.drawString(WiFi.localIP().toString(), 120, 235, 2);
  }
}

// ================= Web Interface =================
void handleRoot() {
  String html = R"HTML(
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
      <title>设备中控台</title>
      <style>
        body { font-family: -apple-system, sans-serif; background: #f2f2f7; text-align: center; padding: 10px; margin: 0; }
        .container { max-width: 500px; margin: 0 auto; }
        .card { background: white; padding: 15px; border-radius: 12px; margin-bottom: 15px; box-shadow: 0 2px 8px rgba(0,0,0,0.05); }
        .master-card { border-top: 5px solid #ccc; transition: border-color 0.3s; }
        .header-row { display: flex; align-items: center; justify-content: center; margin-bottom: 10px; }
        .led { width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }
        .led-on { background: #4CAF50; box-shadow: 0 0 5px #4CAF50; }
        .led-off { background: #ccc; }
        .name-input { font-size: 16px; font-weight: bold; color: #333; border: 1px dashed #ccc; border-radius: 4px; padding: 4px; width: 140px; text-align: left; }
        .name-input:focus { border-color: #2196F3; outline: none; background: #e3f2fd; }
        button { border: none; border-radius: 8px; padding: 12px; font-size: 15px; font-weight: 600; cursor: pointer; width: 100%; transition: all 0.2s; -webkit-tap-highlight-color: transparent; }
        button:active { transform: scale(0.97); opacity: 0.8; }
        #masterBtn { font-size: 18px; padding: 20px; color: white; box-shadow: 0 4px 10px rgba(0,0,0,0.15); }
        .btn-row { display: flex; gap: 10px; margin-top: 10px; }
        .btn-cycle { flex: 1; color: white; }
        .btn-toggle { flex: 1; color: white; } 
        .btn-save { background: #673AB7; color: white; margin-top: 5px; }
        .input-group { display: flex; align-items: center; justify-content: center; gap: 5px; margin-bottom: 5px; color: #666; font-size: 14px; }
        .time-input { width: 60px; padding: 8px; border: 1px solid #ddd; border-radius: 6px; text-align: center; font-size: 16px; }
      </style>
    </head>
    <body>
      <div class="container">
        <div class="card master-card" id="masterCard">
          <button id="masterBtn" onclick="sendCmd('/api/master')">连接中...</button>
        </div>
        
        <div class="card">
          <div class="header-row">
            <span id="led1" class="led led-off"></span>
            <input type="text" id="name1" class="name-input" value="Device 1">
          </div>
          <div class="input-group">
            开启ms <input type="number" id="on1" class="time-input"> 
            关闭ms <input type="number" id="off1" class="time-input">
          </div>
          <button class="btn-save" onclick="saveSet(1)">保存设置</button>
          <div class="btn-row">
            <button id="btnCyc1" class="btn-cycle" onclick="sendCmd('/api/r1/cycle')">循环</button>
            <button id="btnMan1" class="btn-toggle" onclick="sendCmd('/api/r1/toggle')">读取中</button>
          </div>
        </div>

        <div class="card">
           <div class="header-row">
            <span id="led2" class="led led-off"></span>
            <input type="text" id="name2" class="name-input" value="Device 2">
          </div>
          <div class="input-group">
            开启ms <input type="number" id="on2" class="time-input"> 
            关闭ms <input type="number" id="off2" class="time-input">
          </div>
          <button class="btn-save" onclick="saveSet(2)">保存设置</button>
          <div class="btn-row">
            <button id="btnCyc2" class="btn-cycle" onclick="sendCmd('/api/r2/cycle')">循环</button>
            <button id="btnMan2" class="btn-toggle" onclick="sendCmd('/api/r2/toggle')">读取中</button>
          </div>
        </div>
      </div>

      <script>
        window.onload = function() { fetchStatus(); setInterval(fetchStatus, 2000); };
        function sendCmd(url) { fetch(url).then(fetchStatus); }
        function saveSet(id) {
          let onT = document.getElementById('on' + id).value;
          let offT = document.getElementById('off' + id).value;
          let nameVal = document.getElementById('name' + id).value; 
          document.activeElement.blur();
          fetch('/api/r' + id + '/set?on=' + onT + '&off=' + offT + '&name=' + encodeURIComponent(nameVal)).then(fetchStatus);
        }
        function fetchStatus() {
          if (document.activeElement.tagName === 'INPUT') return;
          fetch('/api/status').then(r => r.json()).then(d => {
            updateMaster(d.paused); updateRelay(1, d.r1); updateRelay(2, d.r2);
          }).catch(e => console.log('Err'));
        }
        function updateMaster(isPaused) {
          let btn = document.getElementById('masterBtn'); let card = document.getElementById('masterCard');
          if (isPaused) { btn.innerText = "系统已暂停 ▶ 点击恢复"; btn.style.backgroundColor = "#4CAF50"; card.style.borderTopColor = "#4CAF50"; }
          else { btn.innerText = "系统运行中 ■ 点击停止"; btn.style.backgroundColor = "#e74c3c"; card.style.borderTopColor = "#e74c3c"; }
        }
        function updateRelay(id, rData) {
          document.getElementById('led' + id).className = (rData.state == 0) ? 'led led-on' : 'led led-off';
          document.getElementById('name' + id).value = rData.name;
          let btnCyc = document.getElementById('btnCyc' + id);
          if (rData.cycling) { btnCyc.style.backgroundColor = "#2196F3"; btnCyc.innerText = "循环运行中"; }
          else { btnCyc.style.backgroundColor = "#90CAF9"; btnCyc.innerText = "开启循环"; }
          let btnMan = document.getElementById('btnMan' + id);
          if (rData.state == 0) { btnMan.style.backgroundColor = "#4CAF50"; btnMan.innerText = "已开启"; }
          else { btnMan.style.backgroundColor = "#e74c3c"; btnMan.innerText = "已关闭"; }
          document.getElementById('on' + id).value = rData.on;
          document.getElementById('off' + id).value = rData.off;
        }
      </script>
    </body>
    </html>
  )HTML";
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"paused\":" + String(systemPaused ? "true" : "false") + ",";
  json += "\"r1\":{\"state\":" + String(relay1.getStateForWeb()) + ",\"cycling\":" + String(relay1.isCycling()) + ",\"on\":" + String(relay1.onTime) + ",\"off\":" + String(relay1.offTime) + ",\"name\":\"" + String(name1) + "\"},";
  json += "\"r2\":{\"state\":" + String(relay2.getStateForWeb()) + ",\"cycling\":" + String(relay2.isCycling()) + ",\"on\":" + String(relay2.onTime) + ",\"off\":" + String(relay2.offTime) + ",\"name\":\"" + String(name2) + "\"}";
  json += "}";
  server.send(200, "application/json", json);
}