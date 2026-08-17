#include <WiFi.h>
#include <WebServer.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

// --- COLOR DEFINITIONS ---
#define BLACK     0x0000
#define WHITE     0xFFFF
#define GREEN     0x07E0
#define LIGHTGREY 0xC618
#define DARKGREY  0x31A6

// --- SEEED XIAO ESP32-S3 PINOUT ---
#define TFT_CS   D1
#define TFT_DC   D3
#define TFT_RST  D2
#define TFT_BL   D6

#define SPI_SCK  D8
#define SPI_MISO D9
#define SPI_MOSI D10

#define TOUCH_SDA D4
#define TOUCH_SCL D5

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, SPI_SCK, SPI_MOSI, SPI_MISO);
// Rotation set to 1 (Rotated 180 degrees from previous rotation 3)
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 1 /* rotation */, true /* IPS */);

WebServer server(80);

// --- NETWORK CONFIGURATION ---
const char* ssid     = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// --- STATE VARIABLES ---
String currentTrack   = "Ready";
String currentArtist  = "Spotify";
bool isPlaying        = false;
uint32_t currentPos   = 0;
uint32_t totalDur     = 1;
String currentAction  = "NONE";

// Scrolling & Timers
int scrollOffset = 0;
unsigned long lastScrollTime = 0;

// Touch & Gesture Tracking
bool wasTouching = false;
uint16_t touchStartX = 0, touchStartY = 0;
uint16_t lastTouchX = 0, lastTouchY = 0;
unsigned long touchStartTime = 0;

// --- TOUCH READING & 180-DEGREE ROTATED MAPPING ---
bool readTouch(uint16_t *x, uint16_t *y) {
  uint8_t devAddr = 0x2E;
  Wire.beginTransmission(devAddr);
  if (Wire.endTransmission() != 0) {
    devAddr = 0x15;
    Wire.beginTransmission(devAddr);
    if (Wire.endTransmission() != 0) return false;
  }

  Wire.requestFrom(devAddr, (uint8_t)5);
  if (Wire.available() < 5) return false;

  uint8_t touchNum = Wire.read();
  uint8_t x_high   = Wire.read();
  uint8_t x_low    = Wire.read();
  uint8_t y_high   = Wire.read();
  uint8_t y_low    = Wire.read();

  if (touchNum == 0) return false;

  uint16_t raw_x = ((x_high & 0x0F) << 8) | x_low;
  uint16_t raw_y = ((y_high & 0x0F) << 8) | y_low;

  // Transform mapping updated for Rotation 1 (180 degrees opposite of Rotation 3)
  *x = raw_y;
  *y = 239 - raw_x;
  
  return true;
}

String formatTime(uint32_t totalSeconds) {
  uint32_t mins = totalSeconds / 60;
  uint32_t secs = totalSeconds % 60;
  char buf[10];
  snprintf(buf, sizeof(buf), "%lu:%02lu", mins, secs);
  return String(buf);
}

// --- LARGE CENTERED CONTROLS ---
void drawControls() {
  gfx->fillRect(20, 165, 200, 50, BLACK);

  // 1. Rewind 10s
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(30, 180);
  gfx->print("-10s");

  // 2. Play / Pause (Large Center Button)
  gfx->drawCircle(120, 188, 20, GREEN);
  if (isPlaying) {
    gfx->fillRect(112, 180, 5, 16, GREEN);
    gfx->fillRect(123, 180, 5, 16, GREEN);
  } else {
    gfx->fillTriangle(115, 179, 115, 197, 130, 188, GREEN);
  }

  // 3. Fast Forward 10s
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(162, 180);
  gfx->print("+10s");
}

// --- MAIN UI RENDER ---
void renderUI() {
  // Clear Active Screen Text Region
  gfx->fillRect(10, 35, 220, 125, BLACK);

  // 1. EXTRA LARGE TRACK TITLE (Size 3)
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  
  if (currentTrack.length() > 8) {
    String scrollStr = currentTrack + "   " + currentTrack;
    String visibleStr = scrollStr.substring(scrollOffset, scrollOffset + 8);
    gfx->setCursor(38, 45);
    gfx->println(visibleStr);
  } else {
    // Center short tracks
    int startX = 120 - (currentTrack.length() * 9);
    gfx->setCursor(startX > 20 ? startX : 20, 45);
    gfx->println(currentTrack);
  }

  // 2. LARGE ARTIST SUBTITLE (Size 2)
  gfx->setTextColor(LIGHTGREY);
  gfx->setTextSize(2);
  if (currentArtist.length() > 14) {
    gfx->setCursor(25, 85);
    gfx->println(currentArtist.substring(0, 12) + "..");
  } else {
    int startX = 120 - (currentArtist.length() * 6);
    gfx->setCursor(startX > 20 ? startX : 20, 85);
    gfx->println(currentArtist);
  }

  // 3. TIMESTAMP DISPLAY (Size 2)
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  String timeStr = formatTime(currentPos) + " / " + formatTime(totalDur);
  int timeX = 120 - (timeStr.length() * 6);
  gfx->setCursor(timeX, 125);
  gfx->println(timeStr);

  // 4. CONTROLS
  drawControls();
}

// --- TOUCH & SWIPE LOGIC ---
void checkTouchEvents() {
  uint16_t touchX, touchY;
  bool isTouching = readTouch(&touchX, &touchY);

  if (isTouching) {
    if (!wasTouching) {
      wasTouching = true;
      touchStartX = touchX;
      touchStartY = touchY;
      touchStartTime = millis();
    }
    lastTouchX = touchX;
    lastTouchY = touchY;
  } else {
    if (wasTouching) {
      wasTouching = false;
      int deltaX = (int)lastTouchX - (int)touchStartX;
      int deltaY = (int)lastTouchY - (int)touchStartY;
      unsigned long duration = millis() - touchStartTime;

      // Swipe Gestures (Upper Area for Track/Artist Navigation)
      if (abs(deltaX) > 40 && abs(deltaX) > abs(deltaY) && duration < 800 && touchStartY < 165) {
        if (touchStartX >= 140 && deltaX < -40) {
          currentAction = "NEXT";
          return;
        } else if (touchStartX <= 100 && deltaX > 40) {
          currentAction = "PREV";
          return;
        }
      }

      // Button Taps for Rotated Layout (Y: 165 to 215)
      if (touchStartY >= 165 && touchStartY <= 215 && abs(deltaX) < 30 && abs(deltaY) < 30) {
        if (touchStartX >= 20 && touchStartX <= 85) {
          currentAction = "RW10";
        } else if (touchStartX >= 90 && touchStartX <= 150) {
          currentAction = "PLAY_PAUSE";
        } else if (touchStartX >= 155 && touchStartX <= 220) {
          currentAction = "FF10";
        }
      }
    }
  }
}

// --- HTTP SERVER HANDLERS ---
void handleUpdate() {
  String newTrack = server.hasArg("track") ? server.arg("track") : currentTrack;
  if (newTrack != currentTrack) {
    currentTrack = newTrack;
    scrollOffset = 0;
  }

  if (server.hasArg("artist"))  currentArtist = server.arg("artist");
  if (server.hasArg("playing")) isPlaying     = (server.arg("playing") == "true");
  if (server.hasArg("pos"))     currentPos    = server.arg("pos").toInt();
  if (server.hasArg("dur"))     totalDur      = server.arg("dur").toInt();

  renderUI();
  server.send(200, "text/plain", "OK");
}

void handleAction() {
  server.send(200, "text/plain", currentAction);
  currentAction = "NONE";
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);

  gfx->begin();
  gfx->fillScreen(BLACK);

  // Non-blocking Wi-Fi connection attempt with a 10-second timeout
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
  }

  server.on("/update", HTTP_GET, handleUpdate);
  server.on("/action", HTTP_GET, handleAction);
  server.begin();

  // Draw the initial UI layout immediately upon boot
  renderUI();
}

void loop() {
  server.handleClient();
  checkTouchEvents();

  // Scroll ticker for long titles
  if (currentTrack.length() > 8 && millis() - lastScrollTime > 300) {
    lastScrollTime = millis();
    scrollOffset++;
    if (scrollOffset >= currentTrack.length() + 3) {
      scrollOffset = 0;
    }
    renderUI();
  }
}