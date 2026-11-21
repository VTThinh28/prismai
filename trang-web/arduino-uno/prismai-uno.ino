/*
  PrismAI: đèn lăng trụ thông minh - chương trình cho mạch Arduino UNO R3 (bộ kit học tập Arduino)

  Vòng LED Neo Pixel (WS2812) 12 bóng, điều khiển từ trang web qua dây USB:
  mở trang điều khiển bằng Chrome hoặc Edge trên máy tính, bấm "Nối dây USB".
  Mạch UNO không có Bluetooth nên không điều khiển không dây được.

  Nối dây:
    Vòng LED    chân DI (có vòng ghi IN, DIN) -> chân số 6 của UNO
                chân 5V (có vòng ghi VCC)     -> chân 5V
                chân GND                      -> chân GND
    Nút nhấn    (không bắt buộc) một chân -> chân số 2, chân chéo góc -> GND
    Chiết áp    (không bắt buộc) chân giữa -> A0, hai chân ngoài -> 5V và GND
    Còi         (không bắt buộc) chân dương (+) -> chân số 8, chân kia -> GND

  Nạp bằng Arduino IDE: cài thư viện "Adafruit NeoPixel", chọn Tools > Board > Arduino Uno,
  chọn cổng ở Tools > Port rồi bấm Upload.

  Lệnh nhận qua dây USB (tốc độ 38400, mỗi lệnh một dòng, nối nhiều lệnh bằng dấu ;):
    P1 / P0         bật / tắt đèn
    B0 ... B100     độ sáng theo phần trăm
    C255,170,60     màu (đỏ, xanh lá, xanh dương), mỗi số từ 0 đến 255
    E0 ... E3       hiệu ứng: 0 đứng yên, 1 cầu vồng, 2 nhịp thở, 3 ánh nến
    F3              nhấp nháy 3 lần (báo hết giờ học)
    N12             số bóng của vòng LED (8, 12, 16, 24...), mạch nhớ luôn
    ?               gửi lại trạng thái
  Trạng thái gửi về trang web:  S<bật>,<độ sáng>,<đỏ>,<xanh lá>,<xanh dương>,<hiệu ứng>
  Ví dụ: S1,60,255,170,60,0
  Khi vừa cắm, khi nhận lệnh ? hoặc khi đổi số bóng, gửi thêm số bóng: N12
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// ================= THẦY CÓ THỂ SỬA CÁC DÒNG NÀY =================
#define LED_PIN       6     // chân nối dây DI của vòng LED
#define LED_MAC_DINH  12    // số bóng mặc định; đổi được trên trang web, không cần nạp lại
#define TONG_SANG     1728  // tổng độ sáng các bóng (12 mũ 3) để cổng USB không quá tải;
                            // mỗi bóng sáng tối đa TONG_SANG : số bóng (12 bóng: 144)
#define BUTTON_PIN    2     // nút nhấn bật/tắt đèn
#define CO_NUM_VAN    0     // đổi 0 thành 1 nếu có gắn chiết áp vào chân A0 để vặn độ sáng
#define KNOB_PIN      A0
#define CO_COI        0     // đổi 0 thành 1 nếu có gắn còi vào chân số 8, còi kêu khi hết giờ học
#define COI_PIN       8
// ================================================================

#define MAX_LEDS  60
#define TOC_DO    38400   // tốc độ dây USB, phải giống trên trang web
#define EEPROM_OK 0xD5    // dấu cho biết bộ nhớ đã có trạng thái cũ

Adafruit_NeoPixel strip(LED_MAC_DINH, LED_PIN, NEO_GRB + NEO_KHZ800);

bool powerOn = true;
int level = 60;                 // 0..100 (%)
int colR = 255, colG = 170, colB = 60;
int effect = 0;                 // 0..3
int flashLeft = 0;              // số lần đổi bật/tắt còn lại khi nhấp nháy
bool stateChanged = true;
bool countAsked = true;         // cần báo số bóng cho trang web
bool needSave = false;
uint32_t lastChangeMs = 0;
int pendingCount = 0;           // số bóng mới chờ áp dụng
int ledCount = LED_MAC_DINH;
uint8_t maxLevel = 144;         // độ sáng lớn nhất mỗi bóng, tính theo số bóng
bool forceShow = true;

float shown = 0;                // độ sáng đang hiện, để đổi mượt
uint8_t flicker[MAX_LEDS];

static int clampInt(long v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : (int)v); }

static void markChanged() {
  stateChanged = true;
  needSave = true;
  lastChangeMs = millis();
}

// Đọc số nguyên không âm ở đầu chuỗi; trả về -1 nếu không phải số
static long readNumber(const char *s) {
  while (*s == ' ') s++;
  if (*s < '0' || *s > '9') return -1;
  long v = 0;
  while (*s >= '0' && *s <= '9' && v < 100000L) v = v * 10 + (*s++ - '0');
  return v;
}

static void handleOne(char *c) {
  while (*c == ' ') c++;
  char k = *c;
  if (k == 0) return;
  const char *v = c + 1;
  if (k == '?') {
    stateChanged = true;
    countAsked = true;
    return;
  }
  long n = readNumber(v);
  if (n < 0) return;  // lệnh hỏng thì bỏ qua
  switch (k) {
    case 'P': case 'p':
      powerOn = n != 0;
      markChanged();
      break;
    case 'B': case 'b':
      level = clampInt(n, 0, 100);
      markChanged();
      break;
    case 'C': case 'c': {
      const char *a = strchr(v, ',');
      const char *b = a ? strchr(a + 1, ',') : NULL;
      long g = a ? readNumber(a + 1) : -1;
      long bl = b ? readNumber(b + 1) : -1;
      if (g >= 0 && bl >= 0) {
        colR = clampInt(n, 0, 255);
        colG = clampInt(g, 0, 255);
        colB = clampInt(bl, 0, 255);
        markChanged();
      }
      break;
    }
    case 'E': case 'e':
      effect = clampInt(n, 0, 3);
      markChanged();
      break;
    case 'F': case 'f':
      flashLeft = clampInt(n, 1, 10) * 2;
      break;
    case 'N': case 'n':
      pendingCount = clampInt(n, 1, MAX_LEDS);
      break;
  }
}

static void sendState() {
  Serial.print('S');
  Serial.print(powerOn ? 1 : 0);
  Serial.print(',');
  Serial.print(level);
  Serial.print(',');
  Serial.print(colR);
  Serial.print(',');
  Serial.print(colG);
  Serial.print(',');
  Serial.print(colB);
  Serial.print(',');
  Serial.println(effect);
  if (!countAsked) return;  // số bóng chỉ báo khi cần
  countAsked = false;
  Serial.print('N');
  Serial.println(ledCount);
}

static void applyCount(int n) {
  strip.clear();
  strip.show();
  strip.updateLength(n);
  strip.clear();
  ledCount = n;
  maxLevel = (uint8_t)min(255, TONG_SANG / n);
  forceShow = true;
}

static void coi(bool on) {
#if CO_COI
  digitalWrite(COI_PIN, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

// Chỉ gửi dữ liệu ra vòng LED khi hình mới khác hình cũ. Lúc gửi, mạch UNO tạm
// ngừng nghe dây USB, nên gửi càng ít thì nhận lệnh càng chắc.
static void showIfChanged() {
  static uint16_t lastSum = 0;
  const uint8_t *p = strip.getPixels();
  uint16_t count = (uint16_t)ledCount * 3;
  uint16_t a = 1, b = 0;
  for (uint16_t i = 0; i < count; i++) {
    a += p[i];
    b += a;
  }
  uint16_t sum = (uint16_t)(b << 5) ^ a;
  if (sum == lastSum && !forceShow) return;
  lastSum = sum;
  forceShow = false;
  strip.show();
}

static void render() {
  static uint32_t lastFrame = 0, lastFlash = 0;
  static bool flashOn = false;
  uint32_t now = millis();
  if (now - lastFrame < 20) return;  // khoảng 50 khung hình mỗi giây
  lastFrame = now;

  if (flashLeft > 0) {  // nhấp nháy báo hết giờ
    if (now - lastFlash > 250) {
      lastFlash = now;
      flashOn = !flashOn;
      flashLeft = flashLeft - 1;
    }
    uint16_t v = flashOn ? maxLevel : 0;
    for (int i = 0; i < ledCount; i++) {
      strip.setPixelColor(i, (uint8_t)(v * (uint16_t)colR / 255), (uint8_t)(v * (uint16_t)colG / 255),
                          (uint8_t)(v * (uint16_t)colB / 255));
    }
    coi(flashOn && flashLeft > 0);
    showIfChanged();
    return;
  }
  if (flashOn) {
    flashOn = false;
    coi(false);
  }

  float target = powerOn ? (float)level : 0.0f;
  shown += (target - shown) * 0.18f;
  if (fabs(target - shown) < 0.2f) shown = target;
  float k = shown / 100.0f * maxLevel / 255.0f;  // độ sáng tỉ lệ thuận với phần trăm

  for (int i = 0; i < ledCount; i++) {
    float r = colR, g = colG, b = colB, f = 1.0f;
    if (effect == 1) {  // cầu vồng
      uint16_t hue = (uint16_t)(now * 12UL + (uint32_t)i * 65536UL / (uint32_t)ledCount);
      uint32_t c = strip.ColorHSV(hue, 255, 255);
      r = (c >> 16) & 0xFF;
      g = (c >> 8) & 0xFF;
      b = c & 0xFF;
    } else if (effect == 2) {  // nhịp thở, 4 giây một nhịp
      float ph = (now % 4000UL) / 4000.0f;
      f = 0.25f + 0.75f * (0.5f - 0.5f * cos(ph * 2.0f * PI));
    } else if (effect == 3) {  // ánh nến chập chờn
      if (random(100) < 30) flicker[i] = (uint8_t)clampInt((long)flicker[i] + random(-40, 41), 140, 255);
      f = flicker[i] / 255.0f;
    }
    strip.setPixelColor(i, (uint8_t)(r * k * f + 0.5f), (uint8_t)(g * k * f + 0.5f), (uint8_t)(b * k * f + 0.5f));
  }
  showIfChanged();
}

static void checkButton() {
  static bool last = HIGH;
  static uint32_t lastMs = 0;
  bool b = digitalRead(BUTTON_PIN);
  if (b != last && millis() - lastMs > 40) {
    lastMs = millis();
    last = b;
    if (b == LOW) {
      powerOn = !powerOn;
      markChanged();
    }
  }
}

#if CO_NUM_VAN
// Vặn chiết áp thì độ sáng đổi theo: góc vặn và độ sáng tỉ lệ thuận.
// Lúc mới cắm điện đèn giữ độ sáng đã nhớ, vặn núm mới đổi.
static void checkKnob() {
  static uint32_t lastMs = 0, activeUntil = 0;
  static float smooth = -1;
  static int lastPct = -1;
  uint32_t now = millis();
  if (now - lastMs < 40) return;
  lastMs = now;
  int raw = analogRead(KNOB_PIN);
  if (smooth < 0) smooth = raw;
  smooth += (raw - smooth) * 0.3f;
  int pct = clampInt((long)((smooth - 10.0f) * 100.0f / 1003.0f + 0.5f), 0, 100);  // chừa 1% ở hai đầu núm
  if (lastPct < 0) {
    lastPct = pct;
    return;
  }
  int diff = abs(pct - lastPct);
  bool turning = (long)(activeUntil - now) > 0;  // đang vặn dở thì theo sát từng 1%
  if (diff >= 3 || (turning && diff >= 1)) {     // đứng yên thì bỏ qua rung nhỏ dưới 3%
    activeUntil = now + 800;
    lastPct = pct;
    level = pct;
    powerOn = pct > 0;
    markChanged();
  }
}
#endif

static void checkSerial() {
  static char buf[40];
  static uint8_t len = 0;
  static bool tooLong = false;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r' || ch == ';') {
      if (len > 0 && !tooLong) {
        buf[len] = 0;
        handleOne(buf);
      }
      len = 0;
      tooLong = false;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = ch;
    } else {
      tooLong = true;  // dòng quá dài là dòng hỏng, bỏ cả dòng
    }
  }
}

static void maybeSave() {
  if (needSave && millis() - lastChangeMs > 3000) {
    needSave = false;
    EEPROM.update(0, EEPROM_OK);
    EEPROM.update(1, powerOn ? 1 : 0);
    EEPROM.update(2, (uint8_t)level);
    EEPROM.update(3, (uint8_t)colR);
    EEPROM.update(4, (uint8_t)colG);
    EEPROM.update(5, (uint8_t)colB);
    EEPROM.update(6, (uint8_t)effect);
  }
}

void setup() {
  Serial.begin(TOC_DO);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#if CO_COI
  pinMode(COI_PIN, OUTPUT);
  digitalWrite(COI_PIN, LOW);
#endif
  strip.begin();
  strip.clear();
  strip.show();
  for (int i = 0; i < MAX_LEDS; i++) flicker[i] = 255;

  int n = LED_MAC_DINH;
  if (EEPROM.read(0) == EEPROM_OK) {  // nhớ trạng thái cũ khi cắm điện lại
    powerOn = EEPROM.read(1) != 0;
    level = clampInt(EEPROM.read(2), 0, 100);
    colR = EEPROM.read(3);
    colG = EEPROM.read(4);
    colB = EEPROM.read(5);
    effect = clampInt(EEPROM.read(6), 0, 3);
    uint8_t saved = EEPROM.read(7);
    if (saved >= 1 && saved <= MAX_LEDS) n = saved;
  }
  applyCount(n);

  Serial.println(F("PrismAI (Arduino UNO) san sang"));
  sendState();
  stateChanged = false;
}

void loop() {
  checkButton();
  checkSerial();
#if CO_NUM_VAN
  checkKnob();
#endif
  if (pendingCount > 0) {  // đổi số bóng
    int n = pendingCount;
    pendingCount = 0;
    applyCount(n);
    EEPROM.update(0, EEPROM_OK);
    EEPROM.update(7, (uint8_t)n);
    needSave = true;
    lastChangeMs = millis();
    stateChanged = true;
    countAsked = true;
  }
  static uint32_t lastSendMs = 0;
  if (stateChanged && millis() - lastSendMs >= 100) {  // báo về trang web nhiều nhất 10 lần mỗi giây
    stateChanged = false;
    lastSendMs = millis();
    sendState();
  }
  render();
  maybeSave();
}
