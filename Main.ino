#include "Arduino_LED_Matrix.h"
#include "WiFiS3.h"
#include <PZEM004Tv40_R4.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>

// Sinric-Pro

#include <SinricPro.h>
#include <SinricProSwitch.h>

#define APP_KEY           "APP_KEY"
#define APP_SECRET        "APP_SECRET_KEY"
#define NODE1_ID          "DEVICE_ID" 
#define NODE2_ID          "DEVICE_ID" 

// EEPROM

const int ADDR_TARIFF = 0;
const int ADDR_LIMIT1 = 10;
const int ADDR_LIMIT2 = 20;
const int ADDR_AUTO1 = 30;
const int ADDR_AUTO2 = 35;

// OLED Buffering Refresh

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
unsigned long lastOledUpdate = 0;

// Wifi Connectivity

const char* ssid = "WIFI_SSID";
const char* pass = "WIFI_PASSKEY";
const char* ap_ssid = "AP_FALLBACK_SSID"; // AP Fallback Hotspot Name
const char* ap_pass = "AP_FALLBACK_PASSKEY"; // AP Fallback Hotspot Passkey

WiFiServer server(80);
ArduinoLEDMatrix matrix;
PZEM004Tv40_R4 pzem(&Serial1);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

// Pin Classification

#define R1 2
#define R2 7
#define BUZZER 4
#define VC02_NODE1 9  // VC-02 Pin B2
#define VC02_NODE2 11 // VC-02 Pin A27

// Wifi Stats

bool isOfflineMode = false;
int cH = 0, cM = 0, cS = 0;

// CT Coil Readings

float v = 0.0, c = 0.0, p = 0.0, e = 0.0, freq = 0.0, pf = 0.0;
unsigned long lastPzemRead = 0;
float tariff = 8.0;
float sessionCost = 0.0;

// Sinric Pro Login

String webhookHost = "maker.ifttt.com";
String webhookPath = "/trigger/cyber_log/with/key/YOUR_KEY";
unsigned long lastLogTime = 0;
const unsigned long logInterval = 3600000;

// VR Sensor

float cached_p1 = 0.0, cached_p2 = 0.0, display_p1 = 0.0, display_p2 = 0.0;

// Relay 1 {D2}

bool r1State = false, auto1 = false, tAct1 = false;
float limit1 = 1000.0;
unsigned long tStart1 = 0, tDur1 = 0;
long rem1 = 0;
int onH1 = -1, onM1 = -1, offH1 = -1, offM1 = -1;
bool lastVc1 = HIGH; // Edge Detection State

// Relay 2 {D7}

bool r2State = false, auto2 = false, tAct2 = false;
float limit2 = 1000.0;
unsigned long tStart2 = 0, tDur2 = 0;
long rem2 = 0;
int onH2 = -1, onM2 = -1, offH2 = -1, offM2 = -1;
bool lastVc2 = HIGH;          // Edge Detection State

const uint32_t cg_logo[] = { 0x31846cfe, 0xfe46c318, 0x00000000 };

// Alexa 

bool onPowerState1(const String &deviceId, bool &state) {
  if (state) {
    digitalWrite(R1, LOW); 
    r1State = true;
    tAct1 = false; 
  } else {
    digitalWrite(R1, HIGH); 
    r1State = false;
    tAct1 = false;
  }
  return true; 
}

bool onPowerState2(const String &deviceId, bool &state) {
  if (state) {
    digitalWrite(R2, LOW);
    r2State = true;
    tAct2 = false;
  } else {
    digitalWrite(R2, HIGH);
    r2State = false;
    tAct2 = false;
  }
  return true;
}

// Helper

void sendHTTPResponse(WiFiClient& client, String payload = "OK") {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain; charset=utf-8");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println(); 
  client.print(payload);
}

void updateCloud(const char* deviceId, bool state) {
  if(!isOfflineMode) {
    SinricProSwitch &mySwitch = SinricPro[deviceId];
    mySwitch.sendPowerStateEvent(state);
  }
}

// Setup

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  delay(2000);

  digitalWrite(R1, HIGH); pinMode(R1, OUTPUT);
  digitalWrite(R2, HIGH); pinMode(R2, OUTPUT);
  digitalWrite(BUZZER, LOW); pinMode(BUZZER, OUTPUT);
  
  // VC-02 Input

  pinMode(VC02_NODE1, INPUT_PULLUP); 
  pinMode(VC02_NODE2, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000);
  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "CYBER-GRID v8.0");
  u8g2.drawStr(0, 35, "Enterprise UI Boot...");
  u8g2.sendBuffer();

  EEPROM.get(ADDR_TARIFF, tariff);
  if (isnan(tariff) || tariff <= 0 || tariff > 100) tariff = 8.0;
  EEPROM.get(ADDR_LIMIT1, limit1);
  if (isnan(limit1) || limit1 < 0 || limit1 > 3000) limit1 = 1000.0;
  EEPROM.get(ADDR_LIMIT2, limit2);
  if (isnan(limit2) || limit2 < 0 || limit2 > 3000) limit2 = 1000.0;
  byte b1 = 0, b2 = 0;
  EEPROM.get(ADDR_AUTO1, b1); auto1 = (b1 == 1);
  EEPROM.get(ADDR_AUTO2, b2); auto2 = (b2 == 1);

  matrix.begin();
  matrix.loadFrame(cg_logo);
  pzem.begin();

  Serial.print("Connecting to Router: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  int attempts = 0;
  while ((WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) && attempts < 15) {
    delay(1000); attempts++; Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    isOfflineMode = false;
    Serial.println("\n✓ ONLINE MODE ACTIVE");
    timeClient.begin();

    SinricProSwitch& myNode1 = SinricPro[NODE1_ID];
    myNode1.onPowerState(onPowerState1);
    SinricProSwitch& myNode2 = SinricPro[NODE2_ID];
    myNode2.onPowerState(onPowerState2);
    SinricPro.begin(APP_KEY, APP_SECRET);

  } else {
    isOfflineMode = true;
    Serial.println("\n⚠ Router Failed! Launching Offline AP...");
    WiFi.disconnect(); delay(500);
    if (WiFi.beginAP(ap_ssid, ap_pass) != WL_AP_LISTENING) {
      Serial.println("AP Failed to start.");
    }
  }

  server.begin();
  tone(BUZZER, 2500, 200);
  
  digitalWrite(R1, HIGH); r1State = false;
  digitalWrite(R2, HIGH); r2State = false;
  delay(100); 
}

void logToCloud() {
  WiFiClient client;
  if (client.connect(webhookHost.c_str(), 80)) {
    String url = webhookPath + "?value1=" + String(p) + "&value2=" + String(e) + "&value3=" + String(sessionCost);
    client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + webhookHost + "\r\n" + "Connection: close\r\n\r\n");
    client.stop();
  }
}

// Looping

void loop() {
  if (!isOfflineMode) {
    SinricPro.handle(); 
    timeClient.update();
    cH = timeClient.getHours(); cM = timeClient.getMinutes(); cS = timeClient.getSeconds();
  }

  // VC-02 Implementation 

  bool currentVc1 = digitalRead(VC02_NODE1);
  
  if (lastVc1 == HIGH && currentVc1 == LOW) {               // Turn ON Command
    if (!r1State) {
      digitalWrite(R1, LOW); r1State = true; tAct1 = false; 
      updateCloud(NODE1_ID, true); tone(BUZZER, 2000, 100);     
    }
  } 
  else if (lastVc1 == LOW && currentVc1 == HIGH) {         // Turn OFF Command
    if (r1State) {
      digitalWrite(R1, HIGH); r1State = false; tAct1 = false; 
      updateCloud(NODE1_ID, false); tone(BUZZER, 2000, 100);
    }
  }
  lastVc1 = currentVc1;                                 // Save state for next loop

  bool currentVc2 = digitalRead(VC02_NODE2);
  
  if (lastVc2 == HIGH && currentVc2 == LOW) {               // Turn ON Command
    if (!r2State) {
      digitalWrite(R2, LOW); r2State = true; tAct2 = false; 
      updateCloud(NODE2_ID, true); tone(BUZZER, 2000, 100);     
    }
  } 
  else if (lastVc2 == LOW && currentVc2 == HIGH) {          // Turn OFF Command
    if (r2State) {
      digitalWrite(R2, HIGH); r2State = false; tAct2 = false; 
      updateCloud(NODE2_ID, false); tone(BUZZER, 2000, 100);
    }
  }
  lastVc2 = currentVc2;                              // Save state for next loop

  // Sensor Data Gathering 

  if (millis() - lastPzemRead > 1500) {
    if (pzem.readAll()) {
      v = pzem.getVoltage(); c = pzem.getCurrent();
      p = pzem.getPower(); e = pzem.getEnergy();
      freq = pzem.getFrequency(); pf = pzem.getPowerFactor();
      sessionCost = e * tariff;
    }
    if (r1State && !r2State) { display_p1 = p; display_p2 = 0; cached_p1 = p; } 
    else if (!r1State && r2State) { display_p1 = 0; display_p2 = p; cached_p2 = p; } 
    else if (!r1State && !r2State) { display_p1 = 0; display_p2 = 0; } 
    else if (r1State && r2State) {
      float total_cache = cached_p1 + cached_p2;
      if (total_cache > 0) { display_p1 = p * (cached_p1 / total_cache); display_p2 = p * (cached_p2 / total_cache); } 
      else { display_p1 = p / 2.0; display_p2 = p / 2.0; }
    }
    lastPzemRead = millis();
  }

  // SSD1306 Code

  if (millis() - lastOledUpdate > 2000) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    if (isOfflineMode) {
      u8g2.drawStr(0, 25, "Status: AP FALLBACK");
      u8g2.setCursor(0, 50); u8g2.print("IP: 192.168.4.1");
    } else {
      u8g2.drawStr(0, 25, "Status: ONLINE");
      u8g2.setCursor(0, 50); u8g2.print("IP: "); u8g2.print(WiFi.localIP());
    }
    u8g2.sendBuffer();
    lastOledUpdate = millis();
  }

  // Scheduling

  if (!isOfflineMode && cS == 0) {
    if (cH == onH1 && cM == onM1 && !r1State) { digitalWrite(R1, LOW); r1State = true; tAct1 = false; updateCloud(NODE1_ID, true); }
    if (cH == offH1 && cM == offM1 && r1State) { digitalWrite(R1, HIGH); r1State = false; tAct1 = false; updateCloud(NODE1_ID, false); }
    if (cH == onH2 && cM == onM2 && !r2State) { digitalWrite(R2, LOW); r2State = true; tAct2 = false; updateCloud(NODE2_ID, true); }
    if (cH == offH2 && cM == offM2 && r2State) { digitalWrite(R2, HIGH); r2State = false; tAct2 = false; updateCloud(NODE2_ID, false); }
  }

  if (!isOfflineMode && (millis() - lastLogTime > logInterval)) {
    logToCloud(); lastLogTime = millis();
  }

  // Node Auto Mode Code

  if (r1State && (p > 2500 || (auto1 && display_p1 > limit1))) {
    digitalWrite(R1, HIGH); r1State = false; tAct1 = false; updateCloud(NODE1_ID, false);
    noTone(BUZZER); tone(BUZZER, 2000, 200); 
  }
  if (r2State && (p > 2500 || (auto2 && display_p2 > limit2))) {
    digitalWrite(R2, HIGH); r2State = false; tAct2 = false; updateCloud(NODE2_ID, false);
    noTone(BUZZER); tone(BUZZER, 2000, 200); 
  }

  // Nodes Timing Mode Programming

  unsigned long now = millis();
  if (tAct1) {
    if (now - tStart1 >= tDur1) { digitalWrite(R1, HIGH); r1State = false; tAct1 = false; rem1 = 0; noTone(BUZZER); tone(BUZZER, 2500, 400); updateCloud(NODE1_ID, false); } 
    else { rem1 = (tDur1 - (now - tStart1)) / 1000; }
  } else { rem1 = 0; }

  if (tAct2) {
    if (now - tStart2 >= tDur2) { digitalWrite(R2, HIGH); r2State = false; tAct2 = false; rem2 = 0; noTone(BUZZER); tone(BUZZER, 2500, 400); updateCloud(NODE2_ID, false); } 
    else { rem2 = (tDur2 - (now - tStart2)) / 1000; }
  } else { rem2 = 0; }

  // Web Server

  WiFiClient client = server.available();
  if (client) {
    client.setTimeout(100); 
    String req = client.readStringUntil('\r'); 
    req.trim(); 
    
    if (req.length() < 5) { client.stop(); return; }

    if (req.indexOf("/data") != -1) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json; charset=utf-8");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Connection: close");
      client.println(); 

      char buf[700]; 
      snprintf(buf, sizeof(buf), "{\"v\":%.1f,\"c\":%.2f,\"p\":%.1f,\"dp1\":%.1f,\"dp2\":%.1f,\"e\":%.3f,\"f\":%.1f,\"pf\":%.2f,\"r1\":%d,\"r2\":%d,\"a1\":%d,\"a2\":%d,\"l1\":%.0f,\"l2\":%.0f,\"t1\":%ld,\"t2\":%ld,\"trf\":%.2f,\"cst\":%.2f,\"clk\":\"%02d:%02d\",\"net\":%d}",
              v, c, p, display_p1, display_p2, e, freq, pf, r1State, r2State, auto1, auto2, limit1, limit2, rem1, rem2, tariff, sessionCost, cH, cM, isOfflineMode ? 1 : 0);
      client.print(buf);
    } 
    
    else if (req.indexOf("/FACTORY_RESET") != -1) { 
      tariff = 8.0; limit1 = 1000.0; limit2 = 1000.0; auto1 = false; auto2 = false;
      EEPROM.put(ADDR_TARIFF, tariff); EEPROM.put(ADDR_LIMIT1, limit1); EEPROM.put(ADDR_LIMIT2, limit2); 
      EEPROM.put(ADDR_AUTO1, (byte)0); EEPROM.put(ADDR_AUTO2, (byte)0); 
      sendHTTPResponse(client); 
    }
    
    else if (req.indexOf("/SET_TARIFF?v=") != -1) { 
      int idx = req.indexOf("v=");
      if(idx != -1) { tariff = req.substring(idx + 2).toFloat(); EEPROM.put(ADDR_TARIFF, tariff); }
      sendHTTPResponse(client); 
    }
    
    else if (req.indexOf("/A1") != -1) { auto1 = !auto1; EEPROM.put(ADDR_AUTO1, (byte)(auto1 ? 1 : 0)); sendHTTPResponse(client); }
    else if (req.indexOf("/L1?v=") != -1) { 
      int idx = req.indexOf("v=");
      if(idx != -1) { limit1 = req.substring(idx + 2).toFloat(); EEPROM.put(ADDR_LIMIT1, limit1); }
      sendHTTPResponse(client); 
    }
    else if (req.indexOf("/R1ON") != -1) { digitalWrite(R1, LOW); r1State = true; tAct1 = false; sendHTTPResponse(client); updateCloud(NODE1_ID, true); }
    else if (req.indexOf("/R1OFF") != -1) { digitalWrite(R1, HIGH); r1State = false; tAct1 = false; rem1 = 0; sendHTTPResponse(client); updateCloud(NODE1_ID, false); }
    else if (req.indexOf("/T1?s=") != -1) { 
      int idx = req.indexOf("s=");
      if(idx != -1) {
        long sec = req.substring(idx + 2).toInt(); 
        if (sec > 0) { 
          digitalWrite(R1, LOW); r1State = true; tStart1 = millis(); 
          tDur1 = (unsigned long)sec * 1000UL; 
          tAct1 = true; noTone(BUZZER); tone(BUZZER, 2500, 150); 
          updateCloud(NODE1_ID, true);
        } 
      }
      sendHTTPResponse(client); 
    }
    else if (req.indexOf("/SCH1?") != -1) { 
      int idx1 = req.indexOf("onH="); int idx2 = req.indexOf("&onM="); int idx3 = req.indexOf("&offH="); int idx4 = req.indexOf("&offM="); int idx5 = req.indexOf(" HTTP/");
      if (idx1 != -1 && idx2 != -1 && idx3 != -1 && idx4 != -1 && idx5 != -1) {
        onH1 = req.substring(idx1+4, idx2).toInt(); onM1 = req.substring(idx2+5, idx3).toInt();
        offH1 = req.substring(idx3+6, idx4).toInt(); offM1 = req.substring(idx4+6, idx5).toInt();
      }
      sendHTTPResponse(client); 
    }
    
    else if (req.indexOf("/A2") != -1) { auto2 = !auto2; EEPROM.put(ADDR_AUTO2, (byte)(auto2 ? 1 : 0)); sendHTTPResponse(client); }
    else if (req.indexOf("/L2?v=") != -1) { 
      int idx = req.indexOf("v=");
      if(idx != -1) { limit2 = req.substring(idx + 2).toFloat(); EEPROM.put(ADDR_LIMIT2, limit2); }
      sendHTTPResponse(client); 
    }
    else if (req.indexOf("/R2ON") != -1) { digitalWrite(R2, LOW); r2State = true; tAct2 = false; sendHTTPResponse(client); updateCloud(NODE2_ID, true); }
    else if (req.indexOf("/R2OFF") != -1) { digitalWrite(R2, HIGH); r2State = false; tAct2 = false; rem2 = 0; sendHTTPResponse(client); updateCloud(NODE2_ID, false); }
    else if (req.indexOf("/T2?s=") != -1) { 
      int idx = req.indexOf("s=");
      if(idx != -1) {
        long sec = req.substring(idx + 2).toInt(); 
        if (sec > 0) { 
          digitalWrite(R2, LOW); r2State = true; tStart2 = millis(); 
          tDur2 = (unsigned long)sec * 1000UL; 
          tAct2 = true; noTone(BUZZER); tone(BUZZER, 2500, 150); 
          updateCloud(NODE2_ID, true);
        } 
      }
      sendHTTPResponse(client); 
    }
    else if (req.indexOf("/SCH2?") != -1) { 
      int idx1 = req.indexOf("onH="); int idx2 = req.indexOf("&onM="); int idx3 = req.indexOf("&offH="); int idx4 = req.indexOf("&offM="); int idx5 = req.indexOf(" HTTP/");
      if (idx1 != -1 && idx2 != -1 && idx3 != -1 && idx4 != -1 && idx5 != -1) {
        onH2 = req.substring(idx1+4, idx2).toInt(); onM2 = req.substring(idx2+5, idx3).toInt();
        offH2 = req.substring(idx3+6, idx4).toInt(); offM2 = req.substring(idx4+6, idx5).toInt();
      }
      sendHTTPResponse(client); 
    }
    else { handleRoot(client); }
    client.stop();
  }
}

// Front-End

void handleRoot(WiFiClient client) {
  client.println("HTTP/1.1 200 OK\nContent-Type: text/html; charset=utf-8\nConnection: close\n");
  client.print(R"rawliteral(
    <!DOCTYPE html><html><head><meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no, maximum-scale=1'>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
    <link href="https://fonts.googleapis.com/css2?family=Rajdhani:wght@500;600;700&family=Inter:wght@400;500;600&display=swap" rel="stylesheet">
    <style>
      :root { --bg: #070b14; --surface: rgba(16, 22, 34, 0.7); --border: rgba(0, 242, 255, 0.15); --cyan: #00f2ff; --green: #00ff88; --orange: #ffb84d; --text: #f0f4f8; --text-muted: #8b9bb4; --red: #ff3366; }
      * { box-sizing: border-box; }
      
      body { font-family: 'Inter', sans-serif; background: radial-gradient(circle at top, #142036 0%, var(--bg) 100%); color: var(--text); margin: 0; padding: 10px; min-height: 100vh; display: flex; flex-direction: column; overflow-y: auto; }
      
      .header-row { display: flex; justify-content: space-between; width: 100%; align-items: center; margin-bottom: 15px; padding: 0 5px; }
      h2 { font-family: 'Rajdhani', sans-serif; margin: 0; color: #fff; letter-spacing: 2px; font-size: 1.5em; text-shadow: 0 0 15px rgba(0, 242, 255, 0.5); }
      
      .pill-container { display: flex; gap: 8px; align-items: center; }
      .net-pill { padding: 4px 8px; border-radius: 15px; font-size: 0.7em; font-weight: bold; letter-spacing: 0.5px; border: 1px solid transparent; }
      .clock-pill { background: rgba(0,0,0,0.5); border: 1px solid var(--border); padding: 4px 8px; border-radius: 15px; font-family: 'Rajdhani'; color: var(--cyan); font-weight: bold; }

      .app-container { display: flex; flex-direction: column; gap: 15px; flex: 1; max-width: 1200px; margin: 0 auto; width: 100%; }
      .left-pane, .right-pane { display: flex; flex-direction: column; gap: 10px; width: 100%; }
      
      @media(min-width: 768px) {
        body { height: 100vh; overflow: hidden; padding: 20px; }
        .app-container { flex-direction: row; min-height: 0; }
        .left-pane, .right-pane { overflow-y: auto; padding-right: 10px; }
        .left-pane { flex: 1.5; }
        .right-pane { flex: 1; max-width: 450px; }
        h2 { font-size: 2em; }
      }

      ::-webkit-scrollbar { width: 4px; }
      ::-webkit-scrollbar-track { background: transparent; }
      ::-webkit-scrollbar-thumb { background: var(--cyan); border-radius: 10px; }

      .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; width: 100%; }
      .card { background: var(--surface); backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px); border: 1px solid var(--border); border-radius: 12px; padding: 10px; text-align: center; box-shadow: 0 4px 15px rgba(0,0,0,0.3); }
      .card i { color: var(--cyan); margin-bottom: 2px; font-size: 18px; }
      .val { font-family: 'Rajdhani', sans-serif; font-size: 1.5em; color: #fff; font-weight: 700; line-height: 1; margin-bottom: 2px; }
      .lbl { font-size: 0.65em; color: var(--text-muted); font-weight: 600; letter-spacing: 1px; text-transform: uppercase;}
      
      .money-card { background: linear-gradient(145deg, rgba(0, 255, 136, 0.1) 0%, rgba(0,0,0,0.6) 100%); border-color: rgba(0, 255, 136, 0.3); }
      .money-val { color: var(--green); text-shadow: 0 0 15px rgba(0, 255, 136, 0.4); }

      .ring-container { position: relative; width: 80px; height: 80px; margin: 0 auto; }
      .ring-svg { transform: rotate(-90deg); width:100%; height:100%; }
      .ring-bg { fill: none; stroke: rgba(255,255,255,0.05); stroke-width: 8; }
      .ring-prog { fill: none; stroke: var(--green); stroke-width: 8; stroke-dasharray: 283; stroke-dashoffset: 0; transition: stroke-dashoffset 1s linear, stroke 0.5s; stroke-linecap: round;}
      .ring-text { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); text-align: center; width: 100%;}
      .ring-time { font-family: 'Rajdhani'; font-size: 1.4em; font-weight: 700; color: #fff; }

      .tabs { display: flex; width: 100%; background: rgba(0,0,0,0.5); border-radius: 10px; padding: 4px; margin-bottom: 5px; border: 1px solid var(--border); flex-shrink: 0;}
      .tab { flex: 1; text-align: center; padding: 10px; border-radius: 6px; cursor: pointer; font-size: 0.8em; font-weight: 600; color: var(--text-muted); transition: 0.3s; }
      .t-active { background: var(--cyan); color: #000; box-shadow: 0 0 10px rgba(0,242,255,0.3); }

      .panel { display: none; width: 100%; animation: fadeIn 0.3s ease; flex-direction: column; gap: 8px;}
      .p-active { display: flex; }
      @keyframes fadeIn { from{opacity:0;} to{opacity:1;} }
      
      .node-watts { font-family: 'Rajdhani', sans-serif; font-size: 2.2em; font-weight:bold; color:var(--cyan); margin: 2px 0; text-shadow: 0 0 15px rgba(0,242,255,0.3); line-height: 1;}
      
      .ctrl-row { display: flex; gap: 8px; }
      .m-btn { flex: 1; background: rgba(0,0,0,0.4); border: 1px solid var(--border); color: var(--text-muted); padding: 10px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size:0.75em; transition: 0.2s;}
      .m-active { background: rgba(0,242,255,0.1); border-color: var(--cyan); color: var(--cyan); box-shadow: inset 0 0 10px rgba(0,242,255,0.1); }
      
      .input-box { display: flex; align-items: center; justify-content: space-between; background: rgba(0,0,0,0.4); border: 1px solid var(--border); padding: 10px 12px; border-radius: 8px; }
      input[type="number"] { background: transparent; border: none; color: var(--cyan); font-family: 'Rajdhani', sans-serif; font-size: 1.2em; text-align: right; width: 60px; outline: none; font-weight: 600;}
      input[type="time"] { background: rgba(0,0,0,0.5); border: 1px solid var(--border); color: var(--cyan); font-family: 'Rajdhani'; padding: 4px; border-radius: 4px; outline: none;}
      
      .btn-main { background: linear-gradient(135deg, #00f2ff 0%, #0066ff 100%); border: none; padding: 14px; color: #fff; font-size: 0.9em; font-weight: 700; border-radius: 10px; cursor: pointer; transition: 0.3s; width: 100%; letter-spacing: 1px; box-shadow: 0 5px 15px rgba(0, 242, 255, 0.3); margin-bottom: 20px;}
      .btn-danger { background: linear-gradient(135deg, #ff3366 0%, #cc0033 100%); box-shadow: 0 5px 15px rgba(255, 51, 102, 0.4); }
    </style></head><body>
    
    <div class='header-row'>
      <h2>CYBER-GRID</h2>
      <div class='pill-container'>
        <div class='net-pill' id='netBadge'>...</div>
        <div class='clock-pill' id='sysTime'>--:--</div>
      </div>
    </div>
    
    <div class="app-container">
      <div class="left-pane">
        <div class='grid'>
          <div class='card' style='grid-column: span 2; padding-bottom:5px;'>
            <div class='lbl'>TOTAL GRID LOAD</div>
            <div class='val' id='p' style='font-size:2.5em; color:var(--cyan);'>0W</div>
            <div style="height:50px; width:100%; margin-top:5px;"><canvas id="powerChart"></canvas></div>
          </div>
          <div class='card'><i class='material-icons'>electrical_services</i><div class='val' id='v'>0</div><div class='lbl'>VOLTS</div></div>
          <div class='card'><i class='material-icons'>sync_alt</i><div class='val' id='c'>0.00</div><div class='lbl'>AMPS</div></div>
          <div class='card'><i class='material-icons'>battery_charging_full</i><div class='val' id='e'>0.00</div><div class='lbl'>kWh ENERGY</div></div>
          <div class='card money-card'><i class='material-icons' style='color:var(--green);'>payments</i><div class='val money-val' id='cst'>₹0.00</div><div class='lbl'>SESSION COST</div></div>
          
          <div class='card' style='grid-column: span 2; text-align:left; padding: 15px;'>
            <div style="display:flex; justify-content:space-between; margin-bottom:10px;">
               <div><div class='lbl'>SESSION ENERGY</div><div class='val' style='font-size:1.3em;'><span id='projE'>0.000</span> kWh</div></div>
               <div style="text-align:right;"><div class='lbl'>DAILY AVERAGE</div><div class='val' style='font-size:1.3em;'><span id='projDaily'>0.00</span> kWh</div></div>
            </div>
            <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
               <div><div class='lbl'>PROJECTED MONTHLY</div><div class='val' style='font-size:1.3em; color:var(--orange);'><span id='projMonthly'>0.0</span> kWh</div></div>
               <div style="text-align:right;"><div class='lbl'>EST. MONTHLY BILL</div><div class='val' style='font-size:1.3em; color:var(--orange);'>₹<span id='projBill'>0</span></div></div>
            </div>
            <div style="width:100%; height:6px; background:rgba(255,255,255,0.1); border-radius:3px; margin: 10px 0; position:relative;">
              <div id="projBar" style="height:100%; background:var(--red); border-radius:3px; width:0%; transition: width 0.5s;"></div>
              <div style="position:absolute; top:-4px; left:60%; height:14px; width:2px; background:#fff;"></div>
            </div>
            <div style="display:flex; justify-content:space-between; font-size:0.55em; color:var(--text-muted); font-weight:bold;">
              <span>0 kWh</span><span id="projCompare">You: 0 / Avg: 90 kWh</span><span>150 kWh</span>
            </div>
            <div style="display:flex; gap:8px; margin-top:12px;">
              <div class='input-box' style="margin:0; flex:1; padding:4px 8px;"><span class='lbl'>Tariff(₹)</span><input type='number' id='projTrf' value='8' style="width:40px; font-size:1em;" onchange="calcProj()"></div>
              <div class='input-box' style="margin:0; flex:1; padding:4px 8px;"><span class='lbl'>Hrs Run</span><input type='number' id='projHrs' value='1' style="width:40px; font-size:1em;" onchange="calcProj()"></div>
            </div>
          </div>
        </div>
      </div>

      <div class="right-pane">
        <div class='tabs'>
          <div id='tab1' class='tab t-active' onclick='switchTab(1)'>NODE 1 (D2)</div>
          <div id='tab2' class='tab' onclick='switchTab(2)'>NODE 2 (D7)</div>
          <div id='tab3' class='tab' onclick='switchTab(3)'><i class='material-icons' style='font-size:16px; vertical-align:middle;'>settings</i></div>
        </div>

        <div id='panel1' class='panel p-active'>
          <div id="tContainer1" style="display:none; text-align:center; margin-bottom:5px;">
             <div class="lbl">NODE 1 TIMER</div>
             <div class="ring-container" style="margin-top:5px;">
               <svg class="ring-svg" viewBox="0 0 100 100"><circle class="ring-bg" cx="50" cy="50" r="45"></circle><circle id="tRing1" class="ring-prog" cx="50" cy="50" r="45"></circle></svg>
               <div class="ring-text"><div id="tVal1" class="ring-time">0:00</div></div>
             </div>
          </div>
          <div style='text-align:center;'><div class='lbl'>INDIVIDUAL DRAW</div><div id='dp1' class='node-watts'>0W</div></div>
          <div class='ctrl-row'><button id='m1_0' class='m-btn' onclick='setA(1, 0)'>MANUAL</button><button id='m1_1' class='m-btn' onclick='setA(1, 1)'>AUTO-LIMIT</button></div>
          <div id='lim1' class='input-box' style='display:none;'><span class='lbl'>SAFETY LIMIT (W)</span><input type='number' id='l1In' onchange='upL(1, this.value)' placeholder='1000'></div>
          
          <div class='input-box'>
            <span class='lbl'>SCHEDULE</span>
            <div style='display:flex; gap:5px; flex-direction:column; align-items:flex-end;'>
              <div><small style='color:#8b9bb4; font-size:0.7em;'>ON:</small>  <input type='time' id='schOn1'></div>
              <div><small style='color:#8b9bb4; font-size:0.7em;'>OFF:</small> <input type='time' id='schOff1'></div>
            </div>
          </div>
          <button class='m-btn' onclick='setSch(1)'>SAVE SCHEDULE</button>
          <button id='b1Btn' class='btn-main' onclick='tog(1)'>INITIATE LINK</button>
        </div>

        <div id='panel2' class='panel'>
          <div id="tContainer2" style="display:none; text-align:center; margin-bottom:5px;">
             <div class="lbl">NODE 2 TIMER</div>
             <div class="ring-container" style="margin-top:5px;">
               <svg class="ring-svg" viewBox="0 0 100 100"><circle class="ring-bg" cx="50" cy="50" r="45"></circle><circle id="tRing2" class="ring-prog" cx="50" cy="50" r="45"></circle></svg>
               <div class="ring-text"><div id="tVal2" class="ring-time">0:00</div></div>
             </div>
          </div>
          <div style='text-align:center;'><div class='lbl'>INDIVIDUAL DRAW</div><div id='dp2' class='node-watts'>0W</div></div>
          <div class='ctrl-row'><button id='m2_0' class='m-btn' onclick='setA(2, 0)'>MANUAL</button><button id='m2_1' class='m-btn' onclick='setA(2, 1)'>AUTO-LIMIT</button></div>
          <div id='lim2' class='input-box' style='display:none;'><span class='lbl'>SAFETY LIMIT (W)</span><input type='number' id='l2In' onchange='upL(2, this.value)' placeholder='1000'></div>
          
          <div class='input-box'>
            <span class='lbl'>SCHEDULE</span>
            <div style='display:flex; gap:5px; flex-direction:column; align-items:flex-end;'>
              <div><small style='color:#8b9bb4; font-size:0.7em;'>ON:</small>  <input type='time' id='schOn2'></div>
              <div><small style='color:#8b9bb4; font-size:0.7em;'>OFF:</small> <input type='time' id='schOff2'></div>
            </div>
          </div>
          <button class='m-btn' onclick='setSch(2)'>SAVE SCHEDULE</button>
          <button id='b2Btn' class='btn-main' onclick='tog(2)'>INITIATE LINK</button>
        </div>

        <div id='panel3' class='panel'>
          <div class='input-box'><span class='lbl'>TARIFF (₹/kWh)</span><input type='number' id='trfIn' step='0.1' onchange='setTariff(this.value)'></div>
          <div style='text-align:center; padding:15px; color:var(--text-muted); font-size:0.75em; line-height:1.5;'>
            <i class='material-icons' style='font-size:20px; color:var(--cyan); margin-bottom:5px;'>cloud_upload</i><br>
            Cloud logging is <span style='color:var(--cyan); font-weight:bold;'>ACTIVE</span>.<br>Data pushed automatically every 1 hour.
          </div>
          <button class='btn-main btn-danger' style='margin-top:auto; background: linear-gradient(135deg, #ff0000 0%, #990000 100%); margin-bottom:20px;' onclick='factoryReset()'>
            <i class='material-icons' style='vertical-align:middle; font-size:16px; margin-right:5px;'>warning</i> WIPE SYSTEM
          </button>
        </div>
      </div>
    </div>

    <script>
      let cTab=1, d={}, maxT1=0, maxT2=0;
      let pData = new Array(30).fill(0);
      let pLabels = new Array(30).fill('');
      let chart;

      window.onload = function() {
        const ctx = document.getElementById('powerChart').getContext('2d');
        chart = new Chart(ctx, {
          type: 'line',
          data: { labels: pLabels, datasets: [{ data: pData, borderColor: '#00f2ff', borderWidth: 2, tension: 0.4, pointRadius: 0 }] },
          options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, scales: { x: { display: false }, y: { display: false, min: 0 } }, layout: { padding: 0 } }
        });
        fetchTelemetry();
      };
      
      function switchTab(t) {
        cTab = t;
        document.getElementById('tab1').className = (t==1) ? 'tab t-active' : 'tab';
        document.getElementById('tab2').className = (t==2) ? 'tab t-active' : 'tab';
        document.getElementById('tab3').className = (t==3) ? 'tab t-active' : 'tab';
        document.getElementById('panel1').className = (t==1) ? 'panel p-active' : 'panel';
        document.getElementById('panel2').className = (t==2) ? 'panel p-active' : 'panel';
        document.getElementById('panel3').className = (t==3) ? 'panel p-active' : 'panel';
      }
      function setA(n, state) { fetch(`/A${n}`); }
      function upL(n, v) { fetch(`/L${n}?v=${v}`); }
      function setTariff(v) { fetch(`/SET_TARIFF?v=${v}`); }
      
      function setSch(n) {
        let onVal = document.getElementById(`schOn${n}`).value; let offVal = document.getElementById(`schOff${n}`).value;
        if(onVal && offVal) {
          let onArr = onVal.split(':'); let offArr = offVal.split(':');
          fetch(`/SCH${n}?onH=${onArr[0]}&onM=${onArr[1]}&offH=${offArr[0]}&offM=${offArr[1]}`); alert(`Node ${n} Schedule Saved!`);
        } else { alert("Please set both ON and OFF times."); }
      }

      function tog(n) { if(n==1) fetch(d.r1 ? "/R1OFF" : "/R1ON"); if(n==2) fetch(d.r2 ? "/R2OFF" : "/R2ON"); }
      function factoryReset() { if(confirm("Wipe Memory?")) { fetch('/FACTORY_RESET').then(() => location.reload()); } }

      function updateTimer(n, rem) {
        let cont = document.getElementById('tContainer'+n);
        if(rem <= 0) { cont.style.display = 'none'; return; }
        cont.style.display = 'block';
        
        if(n==1 && rem > maxT1) maxT1 = rem;
        if(n==2 && rem > maxT2) maxT2 = rem;
        let maxT = n==1 ? maxT1 : maxT2;
        
        let pct = rem / maxT;
        let ring = document.getElementById('tRing'+n);
        ring.style.strokeDashoffset = 283 - (283 * pct);
        
        if(pct > 0.5) ring.style.stroke = 'var(--green)';
        else if(pct > 0.2) ring.style.stroke = 'var(--orange)';
        else ring.style.stroke = 'var(--red)';
        
        let m = Math.floor(rem/60); let s = rem%60;
        document.getElementById('tVal'+n).innerText = m + ':' + (s<10?'0':'') + s;
      }

      function calcProj() {
        let e = window.currentEnergy || 0;
        let hrs = parseFloat(document.getElementById('projHrs').value) || 1;
        let trf = parseFloat(document.getElementById('projTrf').value) || 8;
        
        let hrRate = e / hrs;
        let daily = hrRate * 24;
        let monthly = daily * 30;
        let bill = monthly * trf;
        
        document.getElementById('projDaily').innerText = daily.toFixed(2);
        document.getElementById('projMonthly').innerText = monthly.toFixed(1);
        document.getElementById('projBill').innerText = Math.round(bill);
        
        let pctAvg = Math.round(((monthly / 90) * 100) - 100);
        document.getElementById('projCompare').innerText = monthly > 90 ? `Above avg — ${pctAvg}% higher` : `Below avg — ${Math.abs(pctAvg)}% lower`;
        
        let barW = Math.min((monthly / 150) * 100, 100);
        let bar = document.getElementById('projBar');
        bar.style.width = barW + '%';
        bar.style.background = monthly > 90 ? 'var(--red)' : 'var(--green)';
      }
      
      function fetchTelemetry() {
        fetch('/data')
        .then(r => r.json())
        .then(data => {
          d = data;
          
          const nBadge = document.getElementById('netBadge');
          if(d.net === 0) {
            nBadge.innerText = "🌐 ONLINE";
            nBadge.style.color = "var(--green)"; nBadge.style.borderColor = "rgba(0, 255, 136, 0.4)"; nBadge.style.background = "rgba(0, 255, 136, 0.1)";
            document.getElementById('sysTime').innerText = d.clk;
          } else {
            nBadge.innerText = "📶 OFFLINE AP";
            nBadge.style.color = "var(--orange)"; nBadge.style.borderColor = "rgba(255, 184, 77, 0.4)"; nBadge.style.background = "rgba(255, 184, 77, 0.1)";
            document.getElementById('sysTime').innerText = "LOCAL";
          }

          document.getElementById('p').innerText = Math.round(d.p) + "W";
          document.getElementById('v').innerText = Math.round(d.v); 
          document.getElementById('c').innerText = d.c.toFixed(2);
          
          document.getElementById('e').innerText = d.e.toFixed(3); 
          document.getElementById('cst').innerText = "₹" + d.cst.toFixed(2); 
          if(document.activeElement !== document.getElementById('trfIn')) document.getElementById('trfIn').value = d.trf;
          
          pData.push(d.p); pData.shift(); chart.update();

          document.getElementById('projE').innerText = d.e.toFixed(3);
          window.currentEnergy = d.e; 
          calcProj();

          updateTimer(1, d.t1);
          updateTimer(2, d.t2);
          
          document.getElementById('dp1').innerText = Math.round(d.dp1)+"W"; 
          document.getElementById('m1_0').className = d.a1 ? 'm-btn' : 'm-btn m-active';
          document.getElementById('m1_1').className = d.a1 ? 'm-btn m-active' : 'm-btn';
          document.getElementById('lim1').style.display = d.a1 ? 'flex' : 'none';
          if(document.activeElement !== document.getElementById('l1In')) document.getElementById('l1In').value = d.l1;
          const b1 = document.getElementById('b1Btn');
          b1.innerText = d.r1 ? "TERMINATE LINK" : "INITIATE LINK"; 
          b1.className = d.r1 ? "btn-main btn-danger" : "btn-main";
          
          document.getElementById('dp2').innerText = Math.round(d.dp2)+"W"; 
          document.getElementById('m2_0').className = d.a2 ? 'm-btn' : 'm-btn m-active';
          document.getElementById('m2_1').className = d.a2 ? 'm-btn m-active' : 'm-btn';
          document.getElementById('lim2').style.display = d.a2 ? 'flex' : 'none';
          if(document.activeElement !== document.getElementById('l2In')) document.getElementById('l2In').value = d.l2;
          const b2 = document.getElementById('b2Btn');
          b2.innerText = d.r2 ? "TERMINATE LINK" : "INITIATE LINK"; 
          b2.className = d.r2 ? "btn-main btn-danger" : "btn-main";
          
          setTimeout(fetchTelemetry, 2000); 
        })
        .catch(err => {
          setTimeout(fetchTelemetry, 3000);
        });
      }
    </script></body></html>
  )rawliteral");
}
