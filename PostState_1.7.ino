#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);   // I2C address and LCD Size
// SDA - A4
// SCL - A5

/* Defines */
#define RELAY_LOW HIGH
#define RELAY_HIGH LOW

#define WATER_LOW_PIN               A0
#define WATER_HIGH_PIN              A1

#define IR_INPUT_PIN                2
#define PIRA_INPUT_PIN              5
#define RELAY_INVALVE_PIN           6
#define RELAYA_OUT_VALVE_MAIN_PIN   7
#define RELAYB_OUT_VALVE_PIN        8
#define RELAYC_OUT_VALVE_PIN        10

#define FULLY_OPEN_PIN              11
#define FULLY_CLOSED_PIN            12
#define VALVE_TRUE                  0
#define LED_PIN                     13
#define FILL_TIMEOUT                15000
#define DRAIN_TIMEOUT               20000
#define OPEN_TIME_LENGTH            20000  // 600,000 = 10 minutes
#define VALVE_TIMEOUT               9000 //3300 

#define ERROR_DELAY_SHORT           300
#define ERROR_DELAY_LONG            1000
#define WATER_THRESHOLD             700

/* Enumerations */
typedef enum e_mainStates {stIdle, stClosingOut, stFilling, stFilled, stDraining, stDisabled } tMainStates;
typedef enum e_valvePositions {vaOpen, vaPartiallyOpen, vaClosed } tValvePositions;
typedef enum e_valveMotion {moMoving, moStopped } tValveMotion;
typedef enum e_valvePower {prEnabled, prDisabled } tValvePower;
typedef enum e_valveType {vtSlow, vtFast} tValveType;
typedef enum e_valveName {vnIn, vnOut } tValveName;

/* Structures */
typedef struct s_valve {
  tValveName valveName;
  int portWriteA;
  int portWriteB;
  int portWriteC;
  int portReadFullyOpen;
  int portReadFullyClosed;
  tValvePower portEnable;
  unsigned long moveStartTime;
  tValvePositions target;
  tValvePositions curPosition;
  tValveMotion motion;
  tValveType type;
} sValve;

/* Variables */
tMainStates gMainState = stIdle;
tMainStates gMainStatePrev = stDisabled;
sValve gInValve;
sValve gOutValve;
bool errorLog = true;
unsigned long gFillStartTime;
unsigned long gDrainStartTime;
unsigned long gFilledStartTime;
unsigned long gThisMs;
unsigned long gDiffMs;
unsigned long gLastMs = 0;
unsigned long gIdleStart = 0;
String s1;
bool gError = false;
unsigned long gErrorBlinkTime;
int gErrorBlinkCount;
int gCurBlink;
unsigned long gNextBlinkTime;
bool gLedState = LOW;

/* Declerations */
bool checkPIR();
bool checkSnout();
bool checkUpperWater();
bool checkLowerWater();
void initValve(sValve &valve, tValveName vName, int portWriteA);
void initValve(sValve &valve, tValveName vName, int portWriteA, int portWriteB, int portWriteC, int portReadFullyOpen, int portReadFullyClosed);
void setValveTarget(sValve &valve, tValvePositions pos);
void setValve(sValve &valve, tValvePositions target);
void enableValve(sValve &valve);
void setValvePower(sValve &valve, tValvePower power);
tValvePositions getValvePosition(sValve &valve);
void changeMainState(tMainStates target);
void startError(bool fatal, unsigned long blinkTime, int blinkCount);
void handleValve(sValve &valve);
void setErrorLED(bool state);

bool checkPIR() {
  return digitalRead(PIRA_INPUT_PIN);
}
bool checkSnout() {
  return !digitalRead(IR_INPUT_PIN);
}
bool checkUpperWater() {
  return (analogRead(WATER_HIGH_PIN) > WATER_THRESHOLD) ? false : true;
}
bool checkLowerWater() {
  return (analogRead(WATER_LOW_PIN) > WATER_THRESHOLD) ? false : true;
}

void initValve(sValve &valve, tValveName vName, int portWriteA, int portWriteB, int portWriteC, int portReadFullyOpen, int portReadFullyClosed) {
  valve.valveName = vName;
  valve.portWriteA = portWriteA;
  valve.portWriteB = portWriteB;
  valve.portWriteC = portWriteC;
  valve.portReadFullyClosed = portReadFullyClosed;
  valve.portReadFullyOpen = portReadFullyOpen;
  valve.motion = moStopped;
  valve.type = vtSlow;
  valve.portEnable = prDisabled;
  valve.curPosition = vaClosed;
}

void initValve(sValve &valve, tValveName vName, int portWriteA) {
  valve.valveName = vName;
  valve.portWriteA = portWriteA;
  valve.type = vtFast;
  valve.motion = moStopped;
  valve.curPosition = vaClosed;
  valve.portEnable = prDisabled;
}

void setValveTarget(sValve &valve, tValvePositions pos) {
  valve.target = pos;
  setValve(valve, pos);
  if (valve.type == vtSlow) {
    valve.motion = moMoving;
    valve.moveStartTime = millis();
  }
}

void setValve(sValve &valve, tValvePositions target) {
  if (valve.type == vtFast) {
    if (target == vaOpen) {
      setValvePower(valve, prEnabled);
    } else {
      setValvePower(valve, prDisabled);
    }
  } else {
    if (target == vaOpen) {
      setValvePower(valve, prDisabled);
      delay(50);
      digitalWrite(valve.portWriteB, RELAY_LOW);
      digitalWrite(valve.portWriteC, RELAY_LOW);
      delay(15);
      setValvePower(valve, prEnabled);
    } else {
      setValvePower(valve, prDisabled);
      delay(50);
      digitalWrite(valve.portWriteB, RELAY_HIGH);
      digitalWrite(valve.portWriteC, RELAY_HIGH);
      delay(15);
      setValvePower(valve, prEnabled);
    }
  }
}

void setValvePower(sValve &valve, tValvePower power) {
  digitalWrite(valve.portWriteA, (power == prEnabled) ? RELAY_HIGH : RELAY_LOW );
}

tValvePositions getValvePosition(sValve &valve) {
  return (digitalRead(FULLY_OPEN_PIN) == VALVE_TRUE )? vaOpen : (digitalRead(FULLY_CLOSED_PIN) == VALVE_TRUE) ? vaClosed : vaPartiallyOpen;
}

void startError(bool fatal, unsigned long blinkTime, int blinkCount) {
  if (fatal) {
    changeMainState(stDisabled);
    setValvePower(gInValve, prDisabled);
    setValvePower(gOutValve, prDisabled);
  }

  gErrorBlinkTime = blinkTime;
  gErrorBlinkCount = blinkCount;
  gCurBlink = 0;
  gNextBlinkTime = 0;
  gError = true;
}

void changeMainState(tMainStates target) {
  Serial.print(millis());
  Serial.print("\t");
  Serial.print("State CHANGED target is now ");

  switch (target) {
    case stIdle:
      Serial.println("stIdle");
      setValveTarget(gOutValve, vaClosed);
      gIdleStart = millis();
      break;

    case stClosingOut:
      Serial.println("stClosingOut");
      setValveTarget(gOutValve, vaClosed);
      break;

    case stFilling:
      Serial.println("stFilling");
      setValveTarget(gInValve, vaOpen);
      Serial.print(millis());
      Serial.print("\t");
      Serial.print("gFillStartTime = ");
      gFillStartTime = millis();
      Serial.println(gFillStartTime);
      break;

    case stFilled:
      gFilledStartTime = millis();
      Serial.println("stFilled");
      setValveTarget(gInValve, vaClosed);
      Serial.print(millis());
      Serial.print("\t");
      Serial.print("gFilledStartTime =");
      Serial.println(gFilledStartTime);
      break;

    case stDraining:
      Serial.println("stDraining");
      Serial.print(millis());
      Serial.print("\t");
      Serial.print("gDrainStartTime=");
      gDrainStartTime = millis();
      Serial.println(gDrainStartTime);
      setValveTarget(gOutValve, vaOpen);
      break;

    case stDisabled:
      Serial.println("stDisabled");
      break;
  }
  gMainState = target;
}

void handleValve(sValve &valve) {
  if (valve.motion == moMoving) {

    if (getValvePosition(valve) == valve.target) {
      Serial.print(millis());
      Serial.print("\t");
      Serial.print(valve.valveName == 1 ? "OutValve" : "InValve");
      Serial.print(" reached Target in ");
      Serial.println( millis() - valve.moveStartTime);
      valve.motion = moStopped;
      setValvePower(valve, prDisabled);
    }
    else if (millis() > valve.moveStartTime + VALVE_TIMEOUT && !gError) {
      Serial.print(millis());
      Serial.print("\t");
      Serial.print("*******  ERROR 2 ");
      Serial.print(valve.valveName == 1 ? "OutValve" : "InValve");
      Serial.print(" failed to reached Target in ");
      Serial.println(VALVE_TIMEOUT);
      valve.motion = moStopped;
      startError(true, 500, 2);
    }
    else {
      lcd.setCursor(9, 0);
      s1 = String( (valve.moveStartTime + VALVE_TIMEOUT - millis()) / 1000.0, 1 ) + " ";
      lcd.print( s1);
    }
  }
}

void setErrorLED(bool state) {
  digitalWrite(LED_PIN, state );
}

/* Main */
void setup() {
  // put your setup code here, to run once:

  pinMode(RELAYA_OUT_VALVE_MAIN_PIN, OUTPUT);
  digitalWrite(RELAYA_OUT_VALVE_MAIN_PIN, RELAY_LOW );
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT );
  pinMode(WATER_LOW_PIN, INPUT_PULLUP);
  pinMode(WATER_HIGH_PIN, INPUT_PULLUP);
  pinMode(IR_INPUT_PIN, INPUT_PULLUP);
  pinMode(PIRA_INPUT_PIN, INPUT);
  pinMode(RELAY_INVALVE_PIN, OUTPUT);
  pinMode(RELAYB_OUT_VALVE_PIN, OUTPUT);
  pinMode(RELAYC_OUT_VALVE_PIN, OUTPUT);
  pinMode(FULLY_OPEN_PIN, INPUT_PULLUP);
  pinMode(FULLY_CLOSED_PIN, INPUT_PULLUP);

  //initialing states turn everything off

  digitalWrite(RELAY_INVALVE_PIN, RELAY_LOW );
  digitalWrite(RELAYC_OUT_VALVE_PIN, RELAY_LOW );
  digitalWrite(RELAYB_OUT_VALVE_PIN, RELAY_LOW  );

  initValve(gInValve, vnIn, RELAY_INVALVE_PIN);
  initValve(gOutValve, vnOut, RELAYA_OUT_VALVE_MAIN_PIN, RELAYB_OUT_VALVE_PIN, RELAYC_OUT_VALVE_PIN, FULLY_OPEN_PIN, FULLY_CLOSED_PIN);

  // initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("HYDRATORS");
  lcd.setCursor(0, 1);
  lcd.print("Initializing");
  for ( int j = 1; j <= 1; j++) {
    for ( int i = 12; i <= 19; i++ ) {
      lcd.setCursor(i, 1);
      lcd.print(".");
      delay(250);
      lcd.setCursor(i, 1);
      lcd.print(" ");
      delay(250);
    }
  }
  lcd.clear();
}

bool PIR, SNOUT, UPPERWATER, LOWERWATER;
tValvePositions OUTVALVE;
String PIRs, SNOUTs, OUTVALVEs, UPPERWATERs, LOWERWATERs;

/**************************************************************************************/
void loop() {

  // Read All of the sensors
  PIR =  checkPIR();
  SNOUT = checkSnout();
  OUTVALVE = getValvePosition(gOutValve);
  UPPERWATER = checkUpperWater();
  LOWERWATER = checkLowerWater();

  // fill sensor display strings
  PIRs = (PIR) ? "P" : "p";
  SNOUTs = (SNOUT) ? "S" : "s";
  OUTVALVEs = (OUTVALVE == vaClosed) ? "C" : (OUTVALVE == vaOpen) ? "O" : "?";
  UPPERWATERs = (UPPERWATER) ? "U" : "u";
  LOWERWATERs = (LOWERWATER) ? "L" : "l";

  lcd.setCursor(15, 3);
  lcd.print(PIRs + SNOUTs + UPPERWATERs + LOWERWATERs + OUTVALVEs);

  switch (gMainState) {
    case stIdle:
      if (PIR || SNOUT) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.print("TRIGGER PIR=");
        Serial.print(PIR);
        Serial.print("  IR=");
        Serial.println(SNOUT);
        changeMainState(stClosingOut);
      }
      lcd.setCursor(9, 0);
      s1 = String( (millis() - gIdleStart) / 1000.0, 1 ) + " ";
      lcd.print( s1);
      break;

    case stClosingOut:
      if (OUTVALVE == vaClosed) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.println("TRIGGER OutValve is closed");
        changeMainState(stFilling);
      }
      break;

    case stFilling:
      if (millis() >= gFillStartTime + FILL_TIMEOUT) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.print("*******  ERROR 3 ");
        Serial.print("Fill timedOut  did NOT fill in ");
        Serial.println(FILL_TIMEOUT);
        startError(true, 500, 3);
      }
      else if (UPPERWATER) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.println("TRIGGER Upper water reached");
        changeMainState(stFilled);
      }

      s1 = String( (gFillStartTime + FILL_TIMEOUT - millis()) / 1000.0, 1) + " ";
      lcd.setCursor(9, 0);
      lcd.print( s1);

      break;

    case stFilled:
      if (!LOWERWATER) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.println("TRIGGER Lower water broken");
        changeMainState(stFilling);
      }
      else if (millis() >= gFilledStartTime + OPEN_TIME_LENGTH) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.print ("TRIGGER Water filled too long start= ");
        Serial.print(gFilledStartTime);
        Serial.print(" end= ");
        Serial.println(gFilledStartTime + OPEN_TIME_LENGTH);
        changeMainState(stDraining);
      }
      else if (SNOUT) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.print ("Snout Triggerd a reset gFilledStartTime  prev=");
        Serial.print(gFilledStartTime);
        Serial.print("  new=");
        gFilledStartTime = millis();
        Serial.print(gFilledStartTime);
        Serial.print("  will drain at=");
        Serial.println( gFilledStartTime + OPEN_TIME_LENGTH);
      }
      s1 = String( (gFilledStartTime + OPEN_TIME_LENGTH - millis()) / 1000.0, 1) + " ";
      lcd.setCursor(9, 0);
      lcd.print( s1);
      break;

    case stDraining:
      if (!LOWERWATER && !UPPERWATER )  {
        gMainState = stIdle;
      }
      else if (millis() >= gDrainStartTime + DRAIN_TIMEOUT) {
        Serial.print(millis());
        Serial.print("\t");
        Serial.print("*******  ERROR 4 ");
        Serial.print("Did NOT drain in DRAIN_TIMEOUT");
        Serial.println(DRAIN_TIMEOUT);
        startError(true, 500, 4);
      }
      else {
        s1 = String( (gDrainStartTime + DRAIN_TIMEOUT - millis()) / 1000.0, 1) + " ";
        lcd.setCursor(9, 0);
        lcd.print( s1);
      }

      break;

    case stDisabled:
      break;
  }

  handleValve(gInValve);
  handleValve(gOutValve);

  if (gError && millis() > gNextBlinkTime) {
    if (gLedState == LOW) {
      gNextBlinkTime = millis() +  ERROR_DELAY_SHORT;
    }
    else {
      if (gCurBlink < gErrorBlinkCount - 1) {
        gNextBlinkTime = millis() + ERROR_DELAY_SHORT;
        gCurBlink++;
      }
      else {
        gNextBlinkTime = millis() + ERROR_DELAY_LONG;
        gCurBlink = 0;
      }
    }
    gLedState = !gLedState;
    setErrorLED(gLedState);
  }

  if (errorLog && gMainState != gMainStatePrev) {
    Serial.print(millis());
    Serial.print("\t");
    Serial.print("State is ");

    switch (gMainState) {
      case stIdle:
        lcd.setCursor(0, 0);
        lcd.print("Idle    ");
        Serial.println("stIdle");
        break;

      case stClosingOut:
        lcd.setCursor(0, 0);
        lcd.print("Closing  ");
        Serial.println("stClosingOut");
        break;

      case stFilling:
        lcd.setCursor(0, 0);
        lcd.print("Filling ");
        Serial.println("stFilling");
        break;

      case stFilled:
        lcd.setCursor(0, 0);
        lcd.print("Filled  ");
        Serial.println("stFilled");
        break;

      case stDraining:
        lcd.setCursor(0, 0);
        lcd.print("Draining");
        Serial.println("stDraining");
        break;

      case stDisabled:
        lcd.setCursor(0, 0);
        lcd.print("Disabled            ");
        Serial.println("stDisabled");
        break;
    }
  }

  gMainStatePrev = gMainState;

  gDiffMs = millis() - gLastMs;
  if (gDiffMs <= 100) delay(100 - gDiffMs);
  gLastMs = millis();


}
