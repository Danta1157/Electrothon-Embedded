// Importing Required Libraries
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>


// States
enum SystemState {
  WAIT,
  TYPING,
  VALIDATING,
  ACCESS_GRANTED,
  LOCKOUT
};

SystemState current_state;


// Timing Variables
unsigned long lastInputTime = 0;
unsigned long lockoutStart = 0;
unsigned long grantedStart = 0;
unsigned long deniedStart = 0;
unsigned long waitTime = 0;

const unsigned long timeoutDur = 7000;
const unsigned long accessDur = 5000;
const unsigned long deniedDur = 2000;
const unsigned long waitDur = 1000;
const unsigned long lockoutDur = 15000;

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


// LCD and Keypad Setup
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


// Hardware Ports
const int accessLED = 26;
const int lockoutLED = 32;
const int buzzer = 19;


// Function Prototypes
void updateDisplay(String text1, String text2 = "");
void process(char key);
void validate();
void accessGranted(int index);
void accessDenied();
void idDisplay();
void resetSystem();


// Setup
void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  pinMode(accessLED, OUTPUT);
  pinMode(lockoutLED, OUTPUT);
  pinMode(buzzer, OUTPUT);

  resetSystem();
}


// Loop
void loop() {
  // Retrieve Key Input and Process it
  char key = keypad.getKey();
  process(key);


  // Finite State Machine 
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
  }
}


// Key Processing Function 
void process(char key) {
  if (current_state != TYPING)
    return;

  if (!key)
    return;

  lastInputTime = millis();


  switch (key) {

    case '*':

      if (input.length() > 0) {
        input.remove(input.length() - 1);
        idDisplay();
      }

      break;


    case '#':

      current_state = VALIDATING;
      validate();

      break;


    default:

      input += key;
      idDisplay();

      break;
  }
}


// Validation Function
void validate() {
  for (int i = 0; i < 4; i++) {
    if (input.equals(database[i].id)) {
      accessGranted(i);
      return;
    }
  }

  accessDenied();
}


// Aftermath of ID match
void accessGranted(int index) {
  current_state = ACCESS_GRANTED;
  String role = database[index].role;

  grantedStart = millis();  

  tone(buzzer, 1000, 500);
  digitalWrite(accessLED, HIGH);
  updateDisplay("Access Granted", "Welcome " + role);
}


// Aftermath of Denied Entry
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


// Function to mask input
void idDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  for (int i = 0; i < input.length(); i++) {
    lcd.print('*');
  }
}


// Reset System
void resetSystem() {
  current_state = TYPING;

  input = "";

  updateDisplay("Enter ID");
  digitalWrite(accessLED, LOW);
  digitalWrite(lockoutLED, LOW);
  tone(buzzer, 2000, 80);

  lastInputTime = millis();
}


// One-Stop Solution to display something on the LCD
void updateDisplay(String text1, String text2) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(text1);

  lcd.setCursor(0, 1);
  lcd.print(text2);
}