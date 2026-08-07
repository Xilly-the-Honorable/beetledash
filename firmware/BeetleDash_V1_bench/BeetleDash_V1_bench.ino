/*
 * BeetleDash — V1 bench firmware
 * ------------------------------------------------------------
 * Target : Waveshare ESP32-S3-Touch-LCD-2.1 (round 480x480)
 *          (this sketch does NOT drive the display yet — it verifies
 *           the sensors and serves a live dashboard to your phone)
 *
 * V1 shows: Fuel level, Battery voltage, GPS speed, Compass heading
 *
 * Wiring (all on the board's 12-pin header):
 *   I2C : SDA = GPIO15, SCL = GPIO7   -> ADS1115 (0x48) + magnetometer
 *   UART: TX  = GPIO43, RX = GPIO44   -> NEO-M8N GPS (GPS TX -> GPIO44)
 *   3V3, GND, 5V as labeled. Power the board from USB-C for the bench.
 *
 *   ADS1115 A0 = fuel divider:  3V3 -[100R]-+- A0 -[VDO sender]- GND
 *   ADS1115 A1 = volt divider:  12V -[47k]-+- A1 -[10k]- GND
 *   (Put a 0.1uF cap from A0->GND and A1->GND to calm sender noise.)
 *
 * Libraries (install via Library Manager):
 *   - Adafruit ADS1X15
 *   - TinyGPSPlus (by Mikal Hart)
 * ------------------------------------------------------------
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_ADS1X15.h>
#include <TinyGPSPlus.h>

// ---------- Pins ----------
#define PIN_SDA   15
#define PIN_SCL    7
#define PIN_GPS_TX 43   // ESP32 TX -> GPS RX
#define PIN_GPS_RX 44   // ESP32 RX <- GPS TX

// ---------- Fuel + voltage calibration (edit to taste) ----------
#define VEXC        3.30f    // divider excitation = board 3V3 rail
#define FUEL_RTOP   100.0f   // top resistor of the fuel divider (ohms)
#define FUEL_R_EMPTY 70.0f   // MEASURED on your sender: 70.0 ohm = empty (stock VW VDO)
#define FUEL_R_FULL  10.9f   // MEASURED on your sender: 10.9 ohm = full
// Note: this VDO sender drops resistance as the tank fills. The linear map below
// handles that direction automatically (no logic change needed).
#define VOLT_R1     47000.0f // volt divider top (to +12V)
#define VOLT_R2     10000.0f // volt divider bottom (to GND)

// ---------- Compass ----------
#define MAG_DECLINATION 0.0f // set your local magnetic declination (deg) later

// ---------- WiFi Access Point ----------
const char* AP_SSID = "BeetleDash";
const char* AP_PASS = "beetle1234";   // >= 8 chars

// ---------- Globals ----------
Adafruit_ADS1115 ads;
TinyGPSPlus gps;
WebServer server(80);

bool  adsOK = false;
int   magAddr = 0;          // 0 = none, 0x0D = QMC5883L, 0x1E = HMC5883L
float fuelPct = 0, battV = 0, headingDeg = 0, speedMph = 0;
int   sats = 0;
bool  gpsFix = false;
float fuelEMA = -1, voltEMA = -1;   // smoothing state

// ============================================================
//  Magnetometer (auto-detect QMC5883L @0x0D or HMC5883L @0x1E)
// ============================================================
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

void magInit() {
  if (i2cPresent(0x0D)) {              // QMC5883L
    magAddr = 0x0D;
    Wire.beginTransmission(0x0D); Wire.write(0x0B); Wire.write(0x01); Wire.endTransmission(); // SET/RESET period
    Wire.beginTransmission(0x0D); Wire.write(0x09); Wire.write(0x1D); Wire.endTransmission(); // OSR512, 8G, 200Hz, continuous
  } else if (i2cPresent(0x1E)) {       // HMC5883L
    magAddr = 0x1E;
    Wire.beginTransmission(0x1E); Wire.write(0x00); Wire.write(0x70); Wire.endTransmission(); // 8-avg, 15Hz
    Wire.beginTransmission(0x1E); Wire.write(0x01); Wire.write(0xA0); Wire.endTransmission(); // gain
    Wire.beginTransmission(0x1E); Wire.write(0x02); Wire.write(0x00); Wire.endTransmission(); // continuous
  } else {
    magAddr = 0;
  }
}

bool magRead(int16_t &x, int16_t &y, int16_t &z) {
  if (magAddr == 0x0D) {                       // QMC5883L: data at 0x00, order X,Y,Z (LSB first)
    Wire.beginTransmission(0x0D); Wire.write(0x00); Wire.endTransmission();
    if (Wire.requestFrom(0x0D, 6) != 6) return false;
    x = (int16_t)(Wire.read() | (Wire.read() << 8));
    y = (int16_t)(Wire.read() | (Wire.read() << 8));
    z = (int16_t)(Wire.read() | (Wire.read() << 8));
    return true;
  } else if (magAddr == 0x1E) {                // HMC5883L: data at 0x03, order X,Z,Y (MSB first)
    Wire.beginTransmission(0x1E); Wire.write(0x03); Wire.endTransmission();
    if (Wire.requestFrom(0x1E, 6) != 6) return false;
    x = (int16_t)((Wire.read() << 8) | Wire.read());
    z = (int16_t)((Wire.read() << 8) | Wire.read());
    y = (int16_t)((Wire.read() << 8) | Wire.read());
    return true;
  }
  return false;
}

void updateHeading() {
  int16_t x, y, z;
  if (!magRead(x, y, z)) return;
  float h = atan2((float)y, (float)x) * 180.0f / PI + MAG_DECLINATION;
  if (h < 0) h += 360.0f;
  if (h >= 360.0f) h -= 360.0f;
  headingDeg = h;
}

// ============================================================
//  Sensor reads
// ============================================================
void updateAnalog() {
  if (!adsOK) return;

  // --- Fuel (A0) ---
  float vNode = ads.computeVolts(ads.readADC_SingleEnded(0));
  if (vNode < 0.001f) vNode = 0.001f;
  float denom = VEXC - vNode;
  float rs = (denom > 0.02f) ? (FUEL_RTOP * vNode / denom) : 100000.0f; // open circuit -> huge R
  float pct = (rs - FUEL_R_EMPTY) / (FUEL_R_FULL - FUEL_R_EMPTY) * 100.0f;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  fuelEMA = (fuelEMA < 0) ? pct : (fuelEMA * 0.85f + pct * 0.15f);   // slosh smoothing
  fuelPct = fuelEMA;

  // --- Battery voltage (A1) ---
  float vDiv = ads.computeVolts(ads.readADC_SingleEnded(1));
  float vbat = vDiv * (VOLT_R1 + VOLT_R2) / VOLT_R2;
  voltEMA = (voltEMA < 0) ? vbat : (voltEMA * 0.8f + vbat * 0.2f);
  battV = voltEMA;
}

void updateGps() {
  while (Serial1.available()) gps.encode(Serial1.read());
  if (gps.speed.isValid())     speedMph = gps.speed.mph();
  if (gps.satellites.isValid())sats     = gps.satellites.value();
  gpsFix = gps.location.isValid();
}

// ============================================================
//  Web dashboard (offline, no CDN — works in the car)
// ============================================================
const char PAGE[] PROGMEM = R"HTML(
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
</style></head><body>
<h1>BeetleDash &middot; V1</h1>
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
 }catch(e){}
}
setInterval(tick,400); tick();
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleData() {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"fuel\":%.1f,\"volts\":%.2f,\"speed\":%.1f,\"heading\":%.1f,\"sats\":%d,\"fix\":%s,\"mag\":\"%s\"}",
    fuelPct, battV, speedMph, headingDeg, sats, gpsFix ? "true" : "false",
    magAddr == 0x0D ? "QMC5883L" : magAddr == 0x1E ? "HMC5883L" : "none");
  server.send(200, "application/json", buf);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nBeetleDash V1 bench starting...");

  Wire.begin(PIN_SDA, PIN_SCL);
  Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  adsOK = ads.begin(0x48);
  if (adsOK) ads.setGain(GAIN_ONE);          // +/-4.096V full scale
  Serial.printf("ADS1115: %s\n", adsOK ? "OK" : "NOT FOUND");

  magInit();
  Serial.printf("Magnetometer: %s\n",
    magAddr == 0x0D ? "QMC5883L (0x0D)" : magAddr == 0x1E ? "HMC5883L (0x1E)" : "NONE FOUND");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("WiFi AP '%s'  ->  http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  updateGps();                 // fast: drain GPS bytes every loop
  static uint32_t t = 0;
  if (millis() - t > 200) {    // sensors + compass at ~5 Hz
    t = millis();
    updateAnalog();
    updateHeading();
    Serial.printf("fuel %.0f%%  batt %.2fV  spd %.1f  hdg %.0f  sats %d\n",
                  fuelPct, battV, speedMph, headingDeg, sats);
  }
  server.handleClient();
}
