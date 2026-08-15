// Sensor/comms task — V1 bench logic (BeetleDash_V1_bench.ino) ported verbatim,
// wrapped in a core-0 FreeRTOS task. Every I2C transaction goes through the bus
// mutex in I2C_Driver because the CST820 touch shares the same wires on core 1.
#include "Config.h"
#include "Sensors.h"
#include "I2C_Driver.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_ADS1X15.h>
#include <TinyGPSPlus.h>

// ---------- Module state ----------
static Adafruit_ADS1115 ads;
static TinyGPSPlus gps;
static WebServer server(80);

static bool  adsOK = false;
static int   magAddr = 0;          // 0 = none, 0x0D = QMC5883L, 0x1E = HMC5883L, 0x0E/0x0C = IST8310
static bool  magIsIST = false;
static float fuelPct = 0, battV = 0, headingDeg = 0, speedMph = 0;
static float gpsCourse = -1;       // course over ground, deg true; -1 = never valid
static int   sats = 0;
static bool  gpsFix = false;
static float fuelEMA = -1, voltEMA = -1;   // smoothing state

// Dual-circuit brake monitor (V4): hysteresis state of the two switch taps.
static bool brakeSw1 = false, brakeSw2 = false;
static uint8_t brakeFault = BRAKE_FAULT_NONE;      // latched by brakeFaultUpdate()
static volatile bool brakeClearReq = false;        // set by UI core, applied here

// Pure fault detector, unit-testable: XOR of the two switches sustained
// >= BRAKE_XOR_MS latches the LOW (failed) side. Never auto-clears — a failure
// is only observable while braking, so the verdict must outlive the pedal press.
static uint8_t brakeFaultUpdate(uint8_t current, bool sw1, bool sw2, uint32_t nowMs)
{
  static uint32_t xorSince = 0;
  if (sw1 == sw2) { xorSince = 0; return current; }
  if (xorSince == 0) { xorSince = nowMs; return current; }
  if (nowMs - xorSince >= BRAKE_XOR_MS)
    return sw1 ? BRAKE_FAULT_C2 : BRAKE_FAULT_C1;  // the low side failed
  return current;
}

// Shared snapshot for the UI core — single writer (this task), single reader (LVGL).
static GaugeData shared;
static portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
//  Magnetometer (auto-detect QMC5883L @0x0D, HMC5883L @0x1E, IST8310 @0x0E/0x0C)
// ============================================================
static bool i2cPresent(uint8_t addr) {
  I2C_Lock();
  Wire.beginTransmission(addr);
  bool ok = (Wire.endTransmission() == 0);
  I2C_Unlock();
  return ok;
}

static uint8_t regRead8(uint8_t addr, uint8_t reg) {
  I2C_Lock();
  Wire.beginTransmission(addr); Wire.write(reg); Wire.endTransmission();
  uint8_t v = 0;
  if (Wire.requestFrom((int)addr, 1) == 1) v = Wire.read();
  I2C_Unlock();
  return v;
}

static void regWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  I2C_Lock();
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val); Wire.endTransmission();
  I2C_Unlock();
}

static bool ist8310Detect(uint8_t addr) {
  return i2cPresent(addr) && regRead8(addr, 0x00) == 0x10;   // WHO_AM_I
}

static void magInit() {
  if (i2cPresent(0x0D)) {              // QMC5883L
    magAddr = 0x0D;
    I2C_Lock();
    Wire.beginTransmission(0x0D); Wire.write(0x0B); Wire.write(0x01); Wire.endTransmission(); // SET/RESET period
    Wire.beginTransmission(0x0D); Wire.write(0x09); Wire.write(0x1D); Wire.endTransmission(); // OSR512, 8G, 200Hz, continuous
    I2C_Unlock();
  } else if (i2cPresent(0x1E)) {       // HMC5883L
    magAddr = 0x1E;
    I2C_Lock();
    Wire.beginTransmission(0x1E); Wire.write(0x00); Wire.write(0x70); Wire.endTransmission(); // 8-avg, 15Hz
    Wire.beginTransmission(0x1E); Wire.write(0x01); Wire.write(0xA0); Wire.endTransmission(); // gain
    Wire.beginTransmission(0x1E); Wire.write(0x02); Wire.write(0x00); Wire.endTransmission(); // continuous
    I2C_Unlock();
  } else if (ist8310Detect(0x0E) || ist8310Detect(0x0C)) {   // IST8310 (these GPS pucks vary)
    magAddr = ist8310Detect(0x0E) ? 0x0E : 0x0C;
    magIsIST = true;
    regWrite8(magAddr, 0x41, 0x24);  // AVGCNTL: 16x average
    regWrite8(magAddr, 0x42, 0xC0);  // PDCNTL: recommended pulse duration
    regWrite8(magAddr, 0x0A, 0x01);  // CNTL1: trigger first single measurement
  } else {
    magAddr = 0;
  }
}

static bool magRead(int16_t &x, int16_t &y, int16_t &z) {
  if (magAddr == 0x0D) {                       // QMC5883L: data at 0x00, order X,Y,Z (LSB first)
    I2C_Lock();
    Wire.beginTransmission(0x0D); Wire.write(0x00); Wire.endTransmission();
    if (Wire.requestFrom(0x0D, 6) != 6) { I2C_Unlock(); return false; }
    x = (int16_t)(Wire.read() | (Wire.read() << 8));
    y = (int16_t)(Wire.read() | (Wire.read() << 8));
    z = (int16_t)(Wire.read() | (Wire.read() << 8));
    I2C_Unlock();
    return true;
  } else if (magAddr == 0x1E) {                // HMC5883L: data at 0x03, order X,Z,Y (MSB first)
    I2C_Lock();
    Wire.beginTransmission(0x1E); Wire.write(0x03); Wire.endTransmission();
    if (Wire.requestFrom(0x1E, 6) != 6) { I2C_Unlock(); return false; }
    x = (int16_t)((Wire.read() << 8) | Wire.read());
    z = (int16_t)((Wire.read() << 8) | Wire.read());
    y = (int16_t)((Wire.read() << 8) | Wire.read());
    I2C_Unlock();
    return true;
  } else if (magIsIST) {                       // IST8310: data at 0x03, X,Y,Z LSB first
    I2C_Lock();
    Wire.beginTransmission(magAddr); Wire.write(0x03); Wire.endTransmission();
    if (Wire.requestFrom((int)magAddr, 6) != 6) { I2C_Unlock(); return false; }
    x = (int16_t)(Wire.read() | (Wire.read() << 8));
    y = (int16_t)(Wire.read() | (Wire.read() << 8));
    z = (int16_t)(Wire.read() | (Wire.read() << 8));
    I2C_Unlock();
    regWrite8(magAddr, 0x0A, 0x01);            // single-shot chip: trigger the next sample
    return true;
  }
  return false;
}

static void updateHeading() {
  int16_t x, y, z;
  if (!magRead(x, y, z)) return;
  float h = atan2((float)y, (float)x) * 180.0f / PI + MAG_DECLINATION;
  if (h < 0) h += 360.0f;
  if (h >= 360.0f) h -= 360.0f;
  headingDeg = h;
}

// ============================================================
//  Sensor reads (V1 math, unchanged)
// ============================================================
static void updateAnalog() {
  if (!adsOK) return;

  // --- Fuel (A0) ---
  I2C_Lock();
  float vNode = ads.computeVolts(ads.readADC_SingleEnded(0));
  I2C_Unlock();
  if (vNode < 0.001f) vNode = 0.001f;
  float denom = VEXC - vNode;
  float rs = (denom > 0.02f) ? (FUEL_RTOP * vNode / denom) : 100000.0f; // open circuit -> huge R
  float pct = (rs - FUEL_R_EMPTY) / (FUEL_R_FULL - FUEL_R_EMPTY) * 100.0f;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  fuelEMA = (fuelEMA < 0) ? pct : (fuelEMA * 0.85f + pct * 0.15f);   // slosh smoothing
  fuelPct = fuelEMA;

  // --- Battery voltage (A1) ---
  I2C_Lock();
  float vDiv = ads.computeVolts(ads.readADC_SingleEnded(1));
  I2C_Unlock();
  float vbat = vDiv * (VOLT_R1 + VOLT_R2) / VOLT_R2 * VOLT_TRIM;
  voltEMA = (voltEMA < 0) ? vbat : (voltEMA * 0.8f + vbat * 0.2f);
  battV = voltEMA;

#if BRAKE_MONITOR_ENABLED
  // --- Brake circuits (A2 / A3), volts at the ADC node with hysteresis ---
  I2C_Lock();
  float vB1 = ads.computeVolts(ads.readADC_SingleEnded(2));
  float vB2 = ads.computeVolts(ads.readADC_SingleEnded(3));
  I2C_Unlock();
  brakeSw1 = brakeSw1 ? (vB1 > BRAKE_V_CLR) : (vB1 > BRAKE_V_SET);
  brakeSw2 = brakeSw2 ? (vB2 > BRAKE_V_CLR) : (vB2 > BRAKE_V_SET);
  brakeFault = brakeFaultUpdate(brakeFault, brakeSw1, brakeSw2, millis());
#endif
}

static void updateGps() {
  while (Serial1.available()) gps.encode(Serial1.read());
  if (gps.speed.isValid())     speedMph  = gps.speed.mph();
  if (gps.course.isValid())    gpsCourse = gps.course.deg();
  if (gps.satellites.isValid())sats      = gps.satellites.value();
  gpsFix = gps.location.isValid();
}

// Smart heading: GPS course over ground while moving (accurate, needs no cal),
// magnetometer at rest. GPS course is already true north — no declination there.
static float smartHeading() {
  if (gpsFix && gpsCourse >= 0 && speedMph > GPS_HEADING_MIN_MPH) return gpsCourse;
  return headingDeg;
}

// Ask the GPS for a faster update rate at boot, for a smooth speedo.
// NEO-M8N takes UBX CFG-RATE; the PMTK sentence is a fallback for MTK-based pucks.
static void gpsSetRate() {
  const uint16_t ms = GPS_RATE_MS;
  uint8_t ubx[] = { 0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
                    (uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8),   // measRate (ms)
                    0x01, 0x00,                                 // navRate = 1 cycle
                    0x01, 0x00,                                 // timeRef = GPS time
                    0x00, 0x00 };                               // checksum (filled below)
  for (int i = 2; i < 12; i++) { ubx[12] += ubx[i]; ubx[13] += ubx[12]; }
  Serial1.write(ubx, sizeof(ubx));
  delay(100);
  Serial1.print("$PMTK220,200*2C\r\n");   // fallback is fixed at 5 Hz (checksum is baked in)
}

static void updateClock(char *out) {
  if (!gps.time.isValid()) { strcpy(out, "--:--"); return; }
  int mins = gps.time.hour() * 60 + gps.time.minute() + TZ_OFFSET_MIN;
  mins %= 24 * 60;
  if (mins < 0) mins += 24 * 60;
  snprintf(out, 6, "%02d:%02d", mins / 60, mins % 60);
}

// ============================================================
//  Web dashboard (V1 page, offline, no CDN — works in the car)
// ============================================================
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>BeetleDash</title><style>
:root{color-scheme:dark}
*{box-sizing:border-box;margin:0;font-family:system-ui,-apple-system,sans-serif}
body{background:#0b0f14;color:#e8eef5;padding:16px;max-width:520px;margin:auto}
h1{font-size:15px;letter-spacing:.15em;color:#7d8ea0;text-transform:uppercase;margin-bottom:14px;text-align:center}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.card{background:#141b24;border:1px solid #1f2a36;border-radius:16px;padding:16px}
.lbl{font-size:12px;letter-spacing:.12em;color:#7d8ea0;text-transform:uppercase}
.val{font-size:40px;font-weight:650;margin-top:4px;line-height:1}
.unit{font-size:15px;color:#9fb0c2;font-weight:400}
.bar{height:14px;background:#1f2a36;border-radius:8px;overflow:hidden;margin-top:12px}
.fill{height:100%;background:linear-gradient(90deg,#e5484d,#f5a623,#3fb950);transition:width .4s}
.wide{grid-column:1/3}
.rose{width:150px;height:150px;margin:6px auto 0;position:relative;border-radius:50%;
  border:2px solid #1f2a36;display:flex;align-items:center;justify-content:center}
.needle{width:4px;height:66px;background:linear-gradient(#e5484d 50%,#3d4a58 50%);
  border-radius:2px;transform-origin:50% 50%;transition:transform .3s}
.deg{position:absolute;bottom:8px;font-size:13px;color:#9fb0c2}
.tick{position:absolute;font-size:11px;color:#5f7183}
.n{top:6px}.s{bottom:22px}.e{right:8px}.w{left:8px}
.sub{font-size:12px;color:#7d8ea0;margin-top:6px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;vertical-align:1px}
.brk{display:none;background:#B3352C;color:#fff;text-align:center;border-radius:12px;
  padding:12px;font-weight:700;letter-spacing:.12em;margin-bottom:12px}
</style></head><body>
<h1>BeetleDash &middot; V2</h1>
<div class=brk id=brk>BRAKE FAULT</div>
<div class=grid>
  <div class="card"><div class=lbl>Speed</div>
    <div class=val id=spd>--<span class=unit> mph</span></div>
    <div class=sub id=gps>acquiring fix&hellip;</div></div>
  <div class="card"><div class=lbl>Battery</div>
    <div class=val id=volt>--<span class=unit> V</span></div>
    <div class=sub id=vstat>&nbsp;</div></div>
  <div class="card wide"><div class=lbl>Fuel</div>
    <div class=val><span id=fuel>--</span><span class=unit> %</span></div>
    <div class=bar><div class=fill id=fbar style=width:0%></div></div></div>
  <div class="card wide"><div class=lbl>Compass</div>
    <div class=rose>
      <span class="tick n">N</span><span class="tick s">S</span>
      <span class="tick e">E</span><span class="tick w">W</span>
      <div class=needle id=ndl></div><div class=deg id=hdg>--&deg;</div>
    </div></div>
</div>
<script>
function fmt(v,d){return (v==null||isNaN(v))?'--':v.toFixed(d);}
async function tick(){
 try{
  const r=await fetch('/data'); const d=await r.json();
  spd.innerHTML=fmt(d.speed,0)+'<span class=unit> mph</span>';
  volt.innerHTML=fmt(d.volts,1)+'<span class=unit> V</span>';
  fuel.textContent=fmt(d.fuel,0); fbar.style.width=Math.max(0,Math.min(100,d.fuel))+'%';
  hdg.innerHTML=fmt(d.heading,0)+'&deg;';
  ndl.style.transform='rotate('+d.heading+'deg)';
  gps.textContent=d.fix?(d.sats+' sats · fix'):(d.sats+' sats · no fix');
  const dc=d.volts<11.8?'#e5484d':(d.volts>14.6?'#f5a623':'#3fb950');
  vstat.innerHTML='<span class=dot style=background:'+dc+'></span>'+(d.volts<11.8?'low':(d.volts>14.6?'high':'ok'));
  if(d.brakeFault){brk.textContent='BRAKE FAULT — CIRCUIT '+d.brakeFault;brk.style.display='block';}
  else{brk.style.display='none';}
 }catch(e){}
}
setInterval(tick,400); tick();
</script></body></html>
)HTML";

static void handleRoot() { server.send_P(200, "text/html", PAGE); }

static void handleData() {
  char buf[300];
  GaugeData d;
  Gauge_GetData(&d);
  snprintf(buf, sizeof(buf),
    "{\"fuel\":%.1f,\"volts\":%.2f,\"speed\":%.1f,\"heading\":%.1f,\"sats\":%d,\"fix\":%s,\"mag\":\"%s\",\"clock\":\"%s\","
    "\"brake1\":%s,\"brake2\":%s,\"brakeFault\":%u}",
    d.fuelPct, d.battV, d.speedMph, d.headingDeg, d.sats, d.fix ? "true" : "false",
    d.magName, d.clock,
    d.brake1 ? "true" : "false", d.brake2 ? "true" : "false", d.brakeFault);
  server.send(200, "application/json", buf);
}

// ============================================================
//  Shared snapshot
// ============================================================
static void publishData() {
  GaugeData d;
  d.fuelPct    = fuelPct;
  d.battV      = battV;
  d.speedMph   = speedMph;
  d.headingDeg = smartHeading();
  d.sats       = sats;
  d.fix        = gpsFix;
  d.brake1     = brakeSw1;
  d.brake2     = brakeSw2;
  d.brakeFault = brakeFault;
  updateClock(d.clock);
  strncpy(d.magName, magAddr == 0x0D ? "QMC5883L" : magAddr == 0x1E ? "HMC5883L" :
                     magIsIST ? "IST8310" : "none", sizeof(d.magName));
  taskENTER_CRITICAL(&sharedMux);
  shared = d;
  taskEXIT_CRITICAL(&sharedMux);
}

void Gauge_GetData(GaugeData *out) {
  taskENTER_CRITICAL(&sharedMux);
  *out = shared;
  taskEXIT_CRITICAL(&sharedMux);
}

// Called from the UI core (3 s long-press on the fault banner). Applied by the
// sensor task on its next cycle; a genuine failure re-latches on the next brake.
void Sensors_ClearBrakeFault(void) {
  brakeClearReq = true;
}

// ============================================================
//  Core-0 task
// ============================================================
static void sensorTask(void *param) {
  uint32_t tSensor = 0, tLog = 0;
  for (;;) {
    updateGps();                     // fast: drain GPS bytes every pass
    if (millis() - tSensor > 200) {  // sensors + compass at ~5 Hz
      tSensor = millis();
      updateAnalog();
      if (brakeClearReq) { brakeClearReq = false; brakeFault = BRAKE_FAULT_NONE; }
      updateHeading();
      publishData();
    }
    server.handleClient();
    if (millis() - tLog > 1000) {
      tLog = millis();
      Serial.printf("fuel %.0f%%  batt %.2fV  spd %.1f  hdg %.0f  sats %d  %s  brk %d/%d\n",
                    fuelPct, battV, speedMph, headingDeg, sats, gpsFix ? "fix" : "no fix",
                    brakeSw1, brakeSw2);
    }
    vTaskDelay(pdMS_TO_TICKS(2));    // yield; keeps WiFi/idle task fed on core 0
  }
}

void Sensors_Start(void) {
  strcpy(shared.clock, "--:--");
  strcpy(shared.magName, "none");

  Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  gpsSetRate();

  I2C_Lock();
  adsOK = ads.begin(0x48);
  if (adsOK) ads.setGain(GAIN_ONE);          // +/-4.096V full scale
  I2C_Unlock();
  Serial.printf("ADS1115: %s\n", adsOK ? "OK" : "NOT FOUND");

  magInit();
  Serial.printf("Magnetometer: %s\n",
    magAddr == 0x0D ? "QMC5883L (0x0D)" : magAddr == 0x1E ? "HMC5883L (0x1E)" :
    magIsIST ? "IST8310" : "NONE FOUND");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("WiFi AP '%s'  ->  http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  xTaskCreatePinnedToCore(sensorTask, "sensors", 8192, NULL, 3, NULL, 0);
}
