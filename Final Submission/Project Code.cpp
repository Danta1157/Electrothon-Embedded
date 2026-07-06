// Import Required Libraries
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <time.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>


// States and Timings
enum SystemState {
  WAIT,
  TYPING,
  VALIDATING,
  ACCESS_GRANTED,
  LOCKOUT,
  EMERGENCY
};

SystemState current_state;

unsigned long lastInputTime = 0;
unsigned long lockoutStart = 0;
unsigned long grantedStart = 0;
unsigned long deniedStart = 0;
unsigned long waitTime = 0;
unsigned long wifiStart = 0;

const unsigned long timeoutDur = 7000;
const unsigned long accessDur = 5000;
const unsigned long deniedDur = 2000;
const unsigned long waitDur = 1000;
const unsigned long lockoutDur = 15000;
const unsigned long connectionDur = 1000;

unsigned int failedAttempt = 0;


// User Database
struct User {
  const char id[5];
  String role;
};

User database[4] = {
  {"A123", "Manager"},
  {"B456", "Associate"},
  {"C789", "Employee"},
  {"D000", "Admin"}
};


// Offline Queue Structure
struct Log {
  char id[5];
  String role;
  String timestamp;
};

Log offlineQueue[10];
int front = 0; 


// Hardware Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);
String input = "";

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte row_pins[4] = {18, 5, 17, 16};
byte col_pins[4] = {4, 0, 2, 15};
Keypad keypad = Keypad(makeKeymap(keys), row_pins, col_pins, 4, 4);

const int accessLED = 26;
const int lockoutLED = 32;
const int buzzer = 19;
const int button = 27;

volatile bool emergencyOverride = false;


// Function Prototypes
void updateDisplay(String text1, String text2 = "");
void process(char key);
void validate();
void accessGranted(int index);
void accessDenied();
void idDisplay();
void resetSystem();
String getTimestamp();
void saveLogOffline(const char* id, String role, String timestamp);
void sendToCloud(const char* id, String role, String timestamp);
void flushQueue();


// ISR
void IRAM_ATTR ISR() {
  emergencyOverride = true;
}


void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  pinMode(accessLED, OUTPUT);
  pinMode(lockoutLED, OUTPUT);
  pinMode(buzzer, OUTPUT);

  pinMode(button, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(button), ISR, FALLING);

  WiFi.begin("Wokwi-GUEST", "");
  while (WiFi.status() != WL_CONNECTED) {

  }
  configTime(0, 0, "pool.ntp.org");

  resetSystem();
}


void loop() {
  // 1. Emergency Override Check
  if (emergencyOverride) {
    current_state = EMERGENCY;
    emergencyOverride = false; 
    
    digitalWrite(lockoutLED, LOW); 
    digitalWrite(accessLED, HIGH);
    tone(buzzer, 3000);

    updateDisplay("EMERGENCY ACCESS", "Override Engaged");
  }

  // 2. Flush offline logs if Wi-Fi is back
  if (WiFi.status() == WL_CONNECTED && front > 0) {
    flushQueue();
  }
  
  // 3. Keypad Processing
  char key = keypad.getKey();
  if (current_state != EMERGENCY) {
    process(key);
  }

  // 4. FSM
  switch (current_state) {

    case TYPING:
      if (millis() - lastInputTime > timeoutDur) {
        updateDisplay("System Timed Out", "Resetting...");
        waitTime = millis();
        current_state = WAIT;
      }
      break;

    case WAIT:
      if (millis() - waitTime > waitDur) {
        resetSystem();
      }
      break;

    case VALIDATING:
      if (millis() - deniedStart > deniedDur) {
        resetSystem();
      }
      break;

    case ACCESS_GRANTED:
      if (millis() - grantedStart > accessDur) {
        failedAttempt = 0;
        resetSystem();
      }
      break;

    case LOCKOUT:
      if (millis() - lockoutStart > lockoutDur) {
        updateDisplay("Lockout Lifted", "Resetting...");
        digitalWrite(lockoutLED, LOW);
        failedAttempt = 0;
        waitTime = millis();
        current_state = WAIT;
      }
      break;

    case EMERGENCY:

      break;
  }
}


void process(char key) {
  if (current_state != TYPING || !key) return;

  lastInputTime = millis();

  switch (key) {
    case '*': // Backspace
      if (input.length() > 0) {
        input.remove(input.length() - 1);
        idDisplay();
      }
      break;

    case '#': // Submit
      current_state = VALIDATING;
      validate();
      break;

    default: // Append Key
      if (input.length() < 4) {
        input += key;
        idDisplay();
      }
      break;
  }
}


void validate() {
  for (int i = 0; i < 4; i++) {
    if (input.equals(database[i].id)) {
      accessGranted(i);
      return;
    }
  }
  accessDenied();
}


void accessGranted(int index) {
  current_state = ACCESS_GRANTED;
  String role = database[index].role;
  String timeStamp = getTimestamp();

  if (WiFi.status() == WL_CONNECTED) {
     sendToCloud(database[index].id, role, timeStamp);
  } 
  
  else {
     saveLogOffline(database[index].id, role, timeStamp);
  }

  grantedStart = millis();  
  tone(buzzer, 1000, 500);
  digitalWrite(accessLED, HIGH);
  updateDisplay("Access Granted", "Welcome " + role);
}


void accessDenied() {
  failedAttempt++;

  if (failedAttempt < 3) {
    current_state = VALIDATING;
    deniedStart = millis();
    tone(buzzer, 150, 600);
    digitalWrite(lockoutLED, HIGH);
    updateDisplay("Access Denied", "Invalid ID");
  } 
  
  else {
    current_state = LOCKOUT;
    lockoutStart = millis();
    tone(buzzer, 300, 1500);
    digitalWrite(lockoutLED, HIGH);
    updateDisplay("SYSTEM LOCKOUT", "Wait 15 seconds...");
  }
}


void idDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  for (int i = 0; i < input.length(); i++) {
    lcd.print('*');
  }
}


void resetSystem() {
  current_state = TYPING;
  input = "";
  updateDisplay("Enter ID");
  digitalWrite(accessLED, LOW);
  digitalWrite(lockoutLED, LOW);
  tone(buzzer, 2000, 80);
  lastInputTime = millis();
}


void updateDisplay(String text1, String text2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(text1);
  lcd.setCursor(0, 1);
  lcd.print(text2);
}


String getTimestamp() {
  struct tm timeinfo;
  
  if (!getLocalTime(&timeinfo)) {
    return "Time Sync Error";
  }

  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeString);
}


void saveLogOffline(const char* id, String role, String timestamp) {
  if (front < 10) {
    strcpy(offlineQueue[front].id, id);
    offlineQueue[front].role = role;
    offlineQueue[front++].timestamp = timestamp;
  
    Serial.println("Network offline. Log saved to queue.");
  } 
  
  else {
    Serial.println("Offline queue is full!");
  }
}


void sendToCloud(const char* id, String role, String timestamp) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://webhook.site/0fb8bf2c-c8a9-417c-b2a2-7f2398ea091a"); 
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc; 
    doc["id"] = id;
    doc["role"] = role;
    doc["timestamp"] = timestamp;

    String requestBody;
    serializeJson(doc, requestBody);

    int httpResponseCode = http.POST(requestBody);
    
    if (httpResponseCode > 0) {
      Serial.println("Success! Cloud log: " + String(httpResponseCode));
    } else {
      Serial.println("Cloud POST failed. Error: " + String(httpResponseCode));
    }
    
    http.end(); 
  }
}


void flushQueue() {
    Serial.println("Connection detected! Flushing queue...");
    
    while (front > 0) {
      const char* savedId = offlineQueue[--front].id;
      String savedRole = offlineQueue[front].role;
      String savedTime = offlineQueue[front].timestamp;
      
      sendToCloud(savedId, savedRole, savedTime);
    }
    
    Serial.println("Queue flushed.");
}
