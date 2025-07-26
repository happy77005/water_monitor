#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h>

// Software Serial
SoftwareSerial mySerial(3, 2); // TX = 3 (to ESP32 RX), RX = 2 (from ESP32 TX)

// pH Sensor
#define pH_PIN A0        
#define PH_VOLTAGE_REF 5.0

// Turbidity Sensor
const int turbidityPin = A2;

// TDS Sensor
const int tdsPin = A1;
#define TDS_VOLTAGE_REF 5.0
#define CALIBRATION_FACTOR 0.7

// RTC Module
RTC_DS3231 rtc;

// Timing
unsigned long lastReadingTime = 0;
const unsigned long readingInterval = 60000; // 1 minute

// Control
bool isRunning = true;
float temperature = 25.0; // for compensation

// ---------- Function Prototypes ----------
float mapVoltageToPH(float voltage, float pHLow, float pHHigh, float voltageLow, float voltageHigh);
String evaluatePHQuality(float voltage, float pH);
String evaluateTurbidityQuality(float voltage);
String evaluateTDSQuality(float tds);
float calculateTDS(int rawValue);
void evaluateOverallQuality(String pHQuality, String turbidityQuality, String tdsQuality);
String formatTime12Hour(int hour, int minute, int second);
void readAllSensors();
void logSensorValues(DateTime now);

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  mySerial.begin(115200);

  pinMode(pH_PIN, INPUT);
  pinMode(turbidityPin, INPUT);
  pinMode(tdsPin, INPUT);

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.println("System initialized.");
  Serial.println("Available commands: stop, start, clear, read.");
  Serial.println("Press:");
  Serial.println("s  -> start");
  Serial.println("st -> stop");
  Serial.println("c  -> clear");
  Serial.println("r  -> read");
  Serial.println("-----------");
}

// ---------- Main Loop ----------
void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "st") {
      isRunning = false;
      Serial.println("Execution halted.");
    } else if (command == "s") {
      isRunning = true;
      Serial.println("Execution resumed.");
    } else if (command == "c") {
      for (int i = 0; i < 50; i++) Serial.println(" ");
    } else if (command.equalsIgnoreCase("r")) {
      readAllSensors();
    }
  }

  if (!isRunning) {
    delay(100);
    return;
  }

  DateTime now = rtc.now();

  // Log once daily at 10:00:00 AM
  if (now.hour() == 10 && now.minute() == 0 && now.second() == 0) {
    logSensorValues(now);
    delay(1000);
  }

  if (millis() - lastReadingTime >= readingInterval) {
    lastReadingTime = millis();
    readAllSensors();
  }
}

// ---------- Sensor Reading ----------
void readAllSensors() {
  DateTime now = rtc.now();

  // pH
  int pHRaw = analogRead(pH_PIN);
  float pHVoltage = (pHRaw / 1023.0) * PH_VOLTAGE_REF;
  float pHValue;

  if (pHVoltage >= 1.0 && pHVoltage < 1.6)
    pHValue = mapVoltageToPH(pHVoltage, 7.0, 8.0, 1.0, 1.6);
  else if (pHVoltage >= 1.6 && pHVoltage < 2.0)
    pHValue = mapVoltageToPH(pHVoltage, 8.0, 9.0, 1.6, 2.0);
  else if (pHVoltage >= 2.0 && pHVoltage < 2.5)
    pHValue = mapVoltageToPH(pHVoltage, 6.0, 7.5, 2.0, 2.5);
  else if (pHVoltage >= 2.5 && pHVoltage < 3.0)
    pHValue = mapVoltageToPH(pHVoltage, 7.5, 9.0, 2.5, 3.0);
  else
    pHValue = 0.0;

  String pHQuality = evaluatePHQuality(pHVoltage, pHValue);

  Serial.print("pH Value: "); Serial.println(pHValue, 2);
  Serial.print("pH Quality: "); Serial.println(pHQuality);
  mySerial.println(pHValue, 2); // Send to ESP32

  // Turbidity
  int turbRaw = analogRead(turbidityPin);
  float turbVoltage = (turbRaw / 1023.0) * 5.0;
  String turbQuality = evaluateTurbidityQuality(turbVoltage);
  Serial.print("Turbidity Voltage: "); Serial.println(turbVoltage, 2);
  Serial.print("Turbidity Quality: "); Serial.println(turbQuality);

  // TDS
  int tdsRaw = analogRead(tdsPin);
  float tds = calculateTDS(tdsRaw);
  String tdsQuality = evaluateTDSQuality(tds);
  Serial.print("TDS Value: "); Serial.println(tds, 2);
  Serial.print("TDS Quality: "); Serial.println(tdsQuality);

  evaluateOverallQuality(pHQuality, turbQuality, tdsQuality);
}

// ---------- Helper Functions ----------
float mapVoltageToPH(float voltage, float pHLow, float pHHigh, float voltageLow, float voltageHigh) {
  return pHLow + ((voltage - voltageLow) * (pHHigh - pHLow)) / (voltageHigh - voltageLow);
}

String evaluatePHQuality(float voltage, float pH) {
  if (voltage <= 2.29)
    return "Acidic - Bad";
  else if (voltage >= 2.3 && voltage <= 2.5)
    return "Good";
  else
    return "Bad - Alkaline";
}

String evaluateTurbidityQuality(float voltage) {
  return (voltage >= 2.0 && voltage <= 5.0) ? "Good" : "Bad";
}

String evaluateTDSQuality(float tds) {
  return (tds <= 500) ? "Good" : "Bad";
}

float calculateTDS(int rawValue) {
  float voltage = (rawValue / 1024.0) * TDS_VOLTAGE_REF;
  float tds = (133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage) * 0.5;
  tds *= CALIBRATION_FACTOR;
  float compensation = 1.0 + 0.02 * (temperature - 25.0);
  return (tds / compensation);
}

void evaluateOverallQuality(String pHQuality, String turbidityQuality, String tdsQuality) {
  String overall = (pHQuality == "Good" && turbidityQuality == "Good" && tdsQuality == "Good") ? "Excellent" : "Poor, Needs Treatment";
  Serial.println("Overall Water Quality: " + overall);
  if (overall != "Excellent") {
    Serial.println("⚠️ Water may require treatment.");
  }
  Serial.println("------------------------------------------");
}

void logSensorValues(DateTime now) {
  Serial.print("Logging Sensor Values at: ");
  Serial.println(formatTime12Hour(now.hour(), now.minute(), now.second()));
}

String formatTime12Hour(int hour, int minute, int second) {
  String am_pm = "AM";
  if (hour >= 12) {
    am_pm = "PM";
    if (hour > 12) hour -= 12;
  }
  if (hour == 0) hour = 12;

  char buffer[20];
  sprintf(buffer, "%02d:%02d:%02d %s", hour, minute, second, am_pm.c_str());
  return String(buffer);
}
