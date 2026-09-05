/**
 * LoRaWAN Cattle Health Monitoring Firmware
 *
 * Hardware : ESP32-C6 Super Mini (Cezerio)
 * Sensors  : DS18B20 (temperature) + NEO-6M (GPS)
 * Radio    : RFM95W 868MHz (LoRaWAN ABP EU868)
 *
 * Power strategy:
 *   The node wakes 4 times per day, acquires a GPS fix, reads body
 *   temperature, transmits one LoRaWAN uplink, then returns to deep sleep.
 *   The GPS receiver is placed in UBX backup mode before sleeping.
 *   Frame counter is held in RTC memory so it survives deep sleep.
 *
 * Pin Configuration:
 * RFM95 SCK  -> GP18
 * RFM95 MISO -> GP20
 * RFM95 MOSI -> GP19
 * RFM95 NSS  -> GP7
 * RFM95 RST  -> GP1
 * RFM95 DIO0 -> GP0
 * DS18B20    -> GP3 (+ 4.7k pull-up to 3V3)
 * GPS TX     -> GP4
 * GPS RX     -> GP5
 *
 * Author: Yam Project
 * License: MIT
 */

#include <SPI.h>
#include <RadioLib.h>
#include <DS18B20.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <esp_sleep.h>

// --- PIN DEFINITIONS -----------------------------------------
#define DS18B20_PIN  3
#define RFM95_NSS    7
#define RFM95_RST    1
#define RFM95_DIO0   0
#define RFM95_SCK    18
#define RFM95_MISO   20
#define RFM95_MOSI   19
#define GPS_RX_PIN   4   // GPS TX connects here
#define GPS_TX_PIN   5   // GPS RX connects here

// --- POWER / TIMING CONFIGURATION ----------------------------
// 4 uplinks per day -> one cycle every 6 hours
#define SLEEP_SECONDS       (6UL * 60UL * 60UL)
#define uS_PER_S            1000000ULL

// How long we are willing to wait for a GPS fix before giving up.
// Longer means better fix rate, shorter means less energy per cycle.
#define GPS_FIX_TIMEOUT_MS  90000UL

// Set to 1 if a MOSFET cuts the GPS supply from a GPIO.
// Set to 0 to rely on UBX backup mode only (no hardware change needed).
#define GPS_HARD_POWER_GATE 0
#if GPS_HARD_POWER_GATE
  #define GPS_EN_PIN 2
#endif

// --- DS18B20 -------------------------------------------------
DS18B20 sensor(DS18B20_PIN);

// --- GPS -----------------------------------------------------
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

// --- LORA ----------------------------------------------------
RFM95 radio = new Module(RFM95_NSS, RFM95_DIO0, RFM95_RST);

// --- ABP KEYS ------------------------------------------------
// Replace with your own ChirpStack ABP session keys.
// Never commit real keys to a public repository.
uint8_t NwkSKey[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
uint8_t AppSKey[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
uint32_t DevAddr = 0x00000000;

// Frame counter must persist across deep sleep, otherwise the network
// server rejects uplinks as replays. RTC_DATA_ATTR keeps it in RTC RAM,
// which stays powered while the main core is off.
RTC_DATA_ATTR uint16_t frameCounter = 0;
RTC_DATA_ATTR uint32_t bootCount    = 0;

// --- AES-128 S-BOX -------------------------------------------
const uint8_t sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// --- AES HELPER FUNCTIONS ------------------------------------
uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t p = 0;
  for (int i = 0; i < 8; i++) {
    if (b & 1) p ^= a;
    bool hbs = a & 0x80;
    a <<= 1;
    if (hbs) a ^= 0x1b;
    b >>= 1;
  }
  return p;
}

void aes128_encrypt(uint8_t* key, uint8_t* in, uint8_t* out) {
  uint8_t state[16], w[176];
  memcpy(state, in, 16);
  memcpy(w, key, 16);
  const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
  for (int i=4;i<44;i++) {
    uint8_t tmp[4];
    memcpy(tmp,w+(i-1)*4,4);
    if (i%4==0) {
      uint8_t t=tmp[0];
      tmp[0]=sbox[tmp[1]]^rcon[i/4-1];
      tmp[1]=sbox[tmp[2]];
      tmp[2]=sbox[tmp[3]];
      tmp[3]=sbox[t];
    }
    for (int j=0;j<4;j++) w[i*4+j]=w[(i-4)*4+j]^tmp[j];
  }
  for (int i=0;i<16;i++) state[i]^=w[i];
  for (int round=1;round<=10;round++) {
    for (int i=0;i<16;i++) state[i]=sbox[state[i]];
    uint8_t tmp;
    tmp=state[1]; state[1]=state[5]; state[5]=state[9]; state[9]=state[13]; state[13]=tmp;
    tmp=state[2]; state[2]=state[10]; state[10]=tmp;
    tmp=state[6]; state[6]=state[14]; state[14]=tmp;
    tmp=state[3]; state[3]=state[15]; state[15]=state[11]; state[11]=state[7]; state[7]=tmp;
    if (round<10) {
      for (int c=0;c<4;c++) {
        uint8_t s0=state[4*c],s1=state[4*c+1],s2=state[4*c+2],s3=state[4*c+3];
        state[4*c]   = gmul(s0,2)^gmul(s1,3)^s2^s3;
        state[4*c+1] = s0^gmul(s1,2)^gmul(s2,3)^s3;
        state[4*c+2] = s0^s1^gmul(s2,2)^gmul(s3,3);
        state[4*c+3] = gmul(s0,3)^s1^s2^gmul(s3,2);
      }
    }
    for (int i=0;i<16;i++) state[i]^=w[round*16+i];
  }
  memcpy(out,state,16);
}

void xor16(uint8_t* a, uint8_t* b, uint8_t* out) {
  for (int i=0;i<16;i++) out[i]=a[i]^b[i];
}

void computeMIC(uint8_t* msg, uint8_t msgLen, uint8_t* mic) {
  uint8_t b0[16] = {
    0x49,0x00,0x00,0x00,0x00,0x00,
    (uint8_t)(DevAddr),(uint8_t)(DevAddr>>8),
    (uint8_t)(DevAddr>>16),(uint8_t)(DevAddr>>24),
    (uint8_t)(frameCounter),(uint8_t)(frameCounter>>8),
    0x00,0x00,0x00,(uint8_t)msgLen
  };
  uint8_t K1[16]={0},K2[16]={0},L[16]={0},tmp[16]={0};
  aes128_encrypt(NwkSKey,tmp,L);
  bool msb=L[0]&0x80;
  for (int i=0;i<15;i++) K1[i]=(L[i]<<1)|(L[i+1]>>7);
  K1[15]=(L[15]<<1)^(msb?0x87:0x00);
  msb=K1[0]&0x80;
  for (int i=0;i<15;i++) K2[i]=(K1[i]<<1)|(K1[i+1]>>7);
  K2[15]=(K1[15]<<1)^(msb?0x87:0x00);
  uint8_t buf[64]={0};
  memcpy(buf,b0,16);
  memcpy(buf+16,msg,msgLen);
  int totalLen=16+msgLen;
  int nBlocks=(totalLen+15)/16;
  uint8_t X[16]={0},Y[16],last[16]={0};
  for (int i=0;i<nBlocks-1;i++) {
    xor16(X,buf+i*16,Y);
    aes128_encrypt(NwkSKey,Y,X);
  }
  int lastLen=totalLen%16;
  if (lastLen==0) lastLen=16;
  memcpy(last,buf+(nBlocks-1)*16,lastLen);
  if (lastLen==16) xor16(last,K1,last);
  else { last[lastLen]=0x80; xor16(last,K2,last); }
  xor16(X,last,Y);
  aes128_encrypt(NwkSKey,Y,X);
  memcpy(mic,X,4);
}

void encryptPayload(uint8_t* payload, uint8_t len, uint8_t* encrypted) {
  uint8_t A[16] = {
    0x01,0x00,0x00,0x00,0x00,0x00,
    (uint8_t)(DevAddr),(uint8_t)(DevAddr>>8),
    (uint8_t)(DevAddr>>16),(uint8_t)(DevAddr>>24),
    (uint8_t)(frameCounter),(uint8_t)(frameCounter>>8),
    0x00,0x00,0x00,0x01
  };
  uint8_t S[16];
  aes128_encrypt(AppSKey,A,S);
  for (int i=0;i<len;i++) encrypted[i]=payload[i]^S[i];
}

// --- SEND LORAWAN PACKET -------------------------------------
void sendLoRa(float tempC, int32_t lat, int32_t lon, bool gpsValid) {
  int16_t tempInt = (int16_t)(tempC * 100);

  // Byte 10 is a status flag so the decoder can tell a real fix
  // from a cycle that timed out without one.
  uint8_t plainPayload[11];
  plainPayload[0] = (tempInt >> 8) & 0xFF;
  plainPayload[1] =  tempInt & 0xFF;
  plainPayload[2] = (lat >> 24) & 0xFF;
  plainPayload[3] = (lat >> 16) & 0xFF;
  plainPayload[4] = (lat >> 8) & 0xFF;
  plainPayload[5] =  lat & 0xFF;
  plainPayload[6] = (lon >> 24) & 0xFF;
  plainPayload[7] = (lon >> 16) & 0xFF;
  plainPayload[8] = (lon >> 8) & 0xFF;
  plainPayload[9] =  lon & 0xFF;
  plainPayload[10] = gpsValid ? 0x01 : 0x00;

  uint8_t encPayload[11];
  encryptPayload(plainPayload, 11, encPayload);

  uint8_t msg[20];
  msg[0] = 0x40;
  msg[1] = DevAddr & 0xFF;
  msg[2] = (DevAddr >> 8) & 0xFF;
  msg[3] = (DevAddr >> 16) & 0xFF;
  msg[4] = (DevAddr >> 24) & 0xFF;
  msg[5] = 0x00;
  msg[6] = frameCounter & 0xFF;
  msg[7] = (frameCounter >> 8) & 0xFF;
  msg[8] = 0x01;
  memcpy(msg+9, encPayload, 11);

  uint8_t mic[4];
  computeMIC(msg, 20, mic);

  uint8_t packet[24];
  memcpy(packet, msg, 20);
  memcpy(packet+19+1, mic, 4);

  radio.transmit(packet, 24);
  frameCounter++;
}

// --- GPS POWER MANAGEMENT ------------------------------------

// UBX-RXM-PMREQ: put the NEO-6M into backup mode indefinitely.
// Draws roughly 11 uA instead of tens of mA.
void gpsEnterBackup() {
  const uint8_t pmreq[] = {
    0xB5, 0x62, 0x02, 0x41, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00,   // duration 0 = infinite
    0x02, 0x00, 0x00, 0x00,   // flags: backup
    0x4D, 0x3B                // checksum
  };
  gpsSerial.write(pmreq, sizeof(pmreq));
  gpsSerial.flush();
  delay(50);

#if GPS_HARD_POWER_GATE
  digitalWrite(GPS_EN_PIN, LOW);
#endif
}

void gpsWake() {
#if GPS_HARD_POWER_GATE
  pinMode(GPS_EN_PIN, OUTPUT);
  digitalWrite(GPS_EN_PIN, HIGH);
  delay(200);
#endif
  // Any traffic on the UART pulls the receiver out of backup mode.
  gpsSerial.write(0xFF);
  delay(100);
}

// Wait for a valid fix, or give up after GPS_FIX_TIMEOUT_MS.
// Returns true if a fix was obtained.
bool acquireGpsFix(int32_t &lat, int32_t &lon) {
  uint32_t start = millis();
  while (millis() - start < GPS_FIX_TIMEOUT_MS) {
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());
    }
    if (gps.location.isValid() && gps.location.age() < 2000) {
      lat = (int32_t)(gps.location.lat() * 10000);
      lon = (int32_t)(gps.location.lng() * 10000);
      return true;
    }
    delay(10);
  }
  lat = 0;
  lon = 0;
  return false;
}

// --- ENTER DEEP SLEEP ----------------------------------------
void goToSleep() {
  gpsEnterBackup();

  // Put the radio in its lowest power state before the MCU sleeps.
  radio.sleep();

  // Release SPI so the pins do not hold current paths open.
  SPI.end();

  Serial.print("Sleeping for ");
  Serial.print(SLEEP_SECONDS);
  Serial.println(" s");
  Serial.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_PER_S);
  esp_deep_sleep_start();
  // Execution never returns here. The next wake restarts from setup().
}

// --- SETUP ---------------------------------------------------
// With deep sleep, every wake-up re-runs setup() from the top.
// The whole measurement cycle therefore lives here, and loop() is unused.
void setup() {
  bootCount++;

  Serial.begin(115200);
  delay(50);
  Serial.print("=== ESP32-C6 Cow Monitoring | cycle ");
  Serial.print(bootCount);
  Serial.print(" | fcnt ");
  Serial.println(frameCounter);

  // --- GPS ---
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsWake();

  // --- SPI + RFM95 ---
  SPI.begin(RFM95_SCK, RFM95_MISO, RFM95_MOSI, RFM95_NSS);

  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  int state = radio.begin(868.1);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("LoRa init failed, error ");
    Serial.println(state);
    // Do not spin forever on a battery node. Sleep and retry next cycle.
    goToSleep();
  }

  radio.setSyncWord(0x34);
  radio.setCRC(true);
  radio.setSpreadingFactor(7);
  radio.setBandwidth(125.0);
  radio.setCodingRate(5);

  // --- Temperature ---
  float tempC = sensor.getTempC();
  if (isnan(tempC)) {
    Serial.println("DS18B20 not detected");
    tempC = -99.0;   // sentinel value, flagged by the decoder
  } else {
    Serial.print("Temperature: ");
    Serial.print(tempC);
    Serial.println(" C");
  }

  // --- GPS fix ---
  int32_t lat = 0, lon = 0;
  bool gpsValid = acquireGpsFix(lat, lon);
  if (gpsValid) {
    Serial.print("GPS fix: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  } else {
    Serial.println("GPS: no fix within timeout");
  }

  // --- Transmit ---
  sendLoRa(tempC, lat, lon, gpsValid);
  Serial.print("Uplink sent, next fcnt ");
  Serial.println(frameCounter);

  // --- Sleep ---
  goToSleep();
}

// Never reached: the node always sleeps at the end of setup().
void loop() {
}
