#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Seeed_Arduino_FS.h>

// Independent, read-only test for the Wio Terminal's built-in microSD slot.
// Copy sd_card/test.txt to the card root before running.
static const char *TEST_FILE = "/test.txt";
static constexpr uint32_t SD_CLOCK_HZ = 4000000UL;
static constexpr uint32_t PREVIEW_BYTES = 256;
static constexpr uint32_t MAX_ROOT_ENTRIES = 64;
static TFT_eSPI tft;

static void report(const char *message, uint16_t color = TFT_WHITE) {
    Serial.println(message);
    tft.setTextColor(color, TFT_BLACK);
    tft.println(message);
}

static bool listRoot() {
    File root = SD.open("/", FILE_READ);
    if (!root || !root.isDirectory()) {
        root.close();
        report("FAIL: cannot open root", TFT_RED);
        return false;
    }
    Serial.println("--- Root directory (up to 64 entries) ---");
    uint32_t count = 0;
    while (count < MAX_ROOT_ENTRIES) {
        File entry = root.openNextFile(FILE_READ);
        if (!entry) break;
        Serial.print(entry.isDirectory() ? "[DIR]  " : "[FILE] ");
        Serial.print(entry.name());
        if (!entry.isDirectory()) {
            Serial.print("  bytes=");
            Serial.print(entry.size());
        }
        Serial.println();
        entry.close();
        ++count;
    }
    if (count == MAX_ROOT_ENTRIES) Serial.println("Directory listing limit reached.");
    root.close();
    report("OK: root directory opened", TFT_GREEN);
    return true;
}

static void readTestFile() {
    File file = SD.open(TEST_FILE, FILE_READ);
    if (!file || file.isDirectory()) {
        file.close();
        report("FAIL: cannot open test.txt", TFT_RED);
        report("Copy test.txt to card root.", TFT_YELLOW);
        return;
    }
    const uint32_t expected = file.size();
    if (expected == 0) {
        file.close();
        report("FAIL: test.txt is empty", TFT_YELLOW);
        return;
    }
    uint8_t buffer[512];
    uint32_t bytesRead = 0;
    uint32_t hash = 2166136261UL; // FNV-1a; compare with the host to verify content.
    Serial.println("--- File preview (first 256 bytes) ---");
    const uint32_t started = millis();
    while (bytesRead < expected) {
        const uint32_t remaining = expected - bytesRead;
        const uint32_t request = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t got = file.read(buffer, request);
        if (got == 0 || got > request) {
            Serial.println("\nRead stopped before expected end of file.");
            break;
        }
        if (bytesRead < PREVIEW_BYTES) {
            const size_t available = PREVIEW_BYTES - bytesRead;
            Serial.write(buffer, got < available ? got : available);
        }
        for (size_t i = 0; i < got; ++i) {
            hash ^= buffer[i];
            hash *= 16777619UL;
        }
        bytesRead += got;
        yield();
    }
    const uint32_t elapsed = millis() - started;
    file.close();
    Serial.println("\n--- End preview ---");
    char line[64];
    snprintf(line, sizeof(line), "Bytes: %lu / %lu",
             static_cast<unsigned long>(bytesRead), static_cast<unsigned long>(expected));
    report(line);
    snprintf(line, sizeof(line), "FNV-1a: %08lX", static_cast<unsigned long>(hash));
    report(line);
    snprintf(line, sizeof(line), "Elapsed: %lu ms (includes USB)", static_cast<unsigned long>(elapsed));
    Serial.println(line);
    report(bytesRead == expected ? "PASS: whole file read" : "FAIL: incomplete read",
           bytesRead == expected ? TFT_GREEN : TFT_RED);
}

static void runTest() {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    report("Wio SD Read Test", TFT_CYAN);
    report("Read only / SPI 4 MHz");
    // Use the board's SD SPI bus and CS pin, not the generic SD.begin().
    if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI, SD_CLOCK_HZ)) {
        report("FAIL: SD mount failed", TFT_RED);
        report("Check card / FAT32 format.", TFT_YELLOW);
    } else {
        report("OK: SD mounted", TFT_GREEN);
        Serial.print("Card capacity (MiB): ");
        Serial.println(static_cast<unsigned long>(SD.cardSize() / (1024ULL * 1024ULL)));
        if (listRoot()) readTestFile();
    }
    // Release the volume after each test, including a failed mount.
    SD.end();
    report("Serial 'r' / reset: retry", TFT_CYAN);
}

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(3);
    tft.setTextSize(2);
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    report("Starting SD read test...", TFT_CYAN);
    const uint32_t started = millis();
    while (!Serial && millis() - started < 3000) delay(10);
    runTest();
}

void loop() {
    if (Serial.available()) {
        const char command = Serial.read();
        if (command == 'r' || command == 'R') runTest();
    }
    delay(10);
}
