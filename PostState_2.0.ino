#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);   // I2C address and LCD Size
// SDA - A4
// SCL - A5

/* Defines */
#define RELAY_LOW   HIGH
#define RELAY_HIGH  LOW
#define VALVE_HIGH  LOW

#define WATER_LOW_PIN               A0
#define WATER_HIGH_PIN              A1

#define IR_INPUT_PIN                2
#define PING_PIN                    3
#define ECHO_PIN                    4
#define PIRA_INPUT_PIN              5
#define RELAY_INVALVE_PIN           6
#define RELAYA_OUT_VALVE_MAIN_PIN   7
#define RELAYB_OUT_VALVE_PIN        8
#define ZERO_DRINKINGTIME_PIN       9
#define RELAYC_OUT_VALVE_PIN        10

#define FULLY_OPEN_PIN              11
#define FULLY_CLOSED_PIN            12

#define LED_PIN                     13
#define FILL_TIMEOUT                10000
#define DRAIN_TIMEOUT               10000
#define OPEN_TIME_LENGTH            15000  // 600,000 = 10 minutes
#define VALVE_TIMEOUT               3300 //3300 
#define TRIGGER_INCHES              40 

#define ERROR_DELAY_SHORT           300
#define ERROR_DELAY_LONG            1000
#define WATER_THRESHOLD             700

//                        12345678901234567890
char * errErrorStrs[] = {"*OUTVAL FAILED OPEN ",
                         "*OUTVAL FAILED CLOSE",
                         "*BASIN FAILED DRAIN ",
                         " *BASIN FAILED FILL "
                        };
/* Enumerations */
typedef enum e_errorStrs {erOutOpen, erOutClose, erDraining, erFilling} tErrorStrs;
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
sValve gInValve, gOutValve;

unsigned long gFillStartTime;
unsigned long gDrainStartTime;
unsigned long gFilledStartTime;
unsigned long gThisMs;
unsigned long gDiffMs;
unsigned long gLastMs = 0;
unsigned long gIdleStart = 0;
unsigned long gDisabledStart;
String timeStr;
bool gError = false;
unsigned long gErrorBlinkTime;
int gErrorBlinkCount;
int gCurBlink;
unsigned long gNextBlinkTime;
bool gLedState = LOW;
int totalFilled, totalDrained;
char data[20];

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

void setValvePower(sValve &valve, tValvePower power) {
  digitalWrite(valve.portWriteA, (power == prEnabled) ? RELAY_HIGH : RELAY_LOW );
  valve.portEnable = power;
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
      delay(30);
      digitalWrite(valve.portWriteB, RELAY_LOW);
      digitalWrite(valve.portWriteC, RELAY_LOW);
      delay(20);
      setValvePower(valve, prEnabled);
    } else {
      setValvePower(valve, prDisabled);
      delay(30);
      digitalWrite(valve.portWriteB, RELAY_HIGH);
      digitalWrite(valve.portWriteC, RELAY_HIGH);
      delay(20);
      setValvePower(valve, prEnabled);
    }
  }
}



tValvePositions getValvePosition(sValve &valve) {
  if (valve.type == vtFast)
    return valve.portEnable == prEnabled ? vaOpen : vaClosed;
  else
    return (digitalRead(FULLY_OPEN_PIN) == VALVE_HIGH ) ? vaOpen : (digitalRead(FULLY_CLOSED_PIN) == VALVE_HIGH) ? vaClosed : vaPartiallyOpen;
}

unsigned int lenStr;
int curPos ;
String ErrS;
tErrorStrs errIndex = 0;

void startError(tErrorStrs err) {
  changeMainState(stDisabled);
  setValvePower(gInValve, prDisabled);
  setValvePower(gOutValve, prDisabled);
  lcd.setCursor(0, 1); lcd.print(errErrorStrs[err]);
  gError = true;
}

void showState(tMainStates state) {
  switch (state) {
    case stIdle:
      lcd.setCursor(0, 0); lcd.print("Idle            ");
      break;
    case stClosingOut:
      //lcd.setCursor(0, 0); lcd.print("Closing   ");
      break;
    case stFilling:
      lcd.setCursor(0, 0); lcd.print("Filling  ");
      break;
    case stFilled:
      lcd.setCursor(0, 0); lcd.print("Drinking ");
      break;
    case stDraining:
      lcd.setCursor(0, 0); lcd.print("Draining ");
      break;
    case stDisabled:
      lcd.setCursor(0, 0); lcd.print("Disabled ");
      break;
  }
}

void changeMainState(tMainStates target) {
  Serial.print(millis()); Serial.println("\t State CHANGED target is now... ");
  showState(target);
  switch (target) {
    case stIdle:
      setValveTarget(gOutValve, vaClosed);
      gIdleStart = millis();
      Serial.print(millis()); Serial.println("\t stIdle");
      break;

    case stClosingOut:
      setValveTarget(gOutValve, vaClosed);
      Serial.print(millis()); Serial.println("\t stClosingOut");
      break;

    case stFilling:
      setValveTarget(gInValve, vaOpen);
      gFillStartTime = millis();
      Serial.print(millis()); Serial.print("\t stFilling gFillStartTime = ");
      Serial.println(gFillStartTime);
      break;

    case stFilled:
      totalFilled++;
      sprintf(data, "%03i", totalFilled);
      lcd.setCursor(2, 3); lcd.print(data);
      setValveTarget(gInValve, vaClosed);
      gFilledStartTime = millis();
      Serial.print(millis()); Serial.print("\t stFilled gFilledStartTime = ");
      Serial.println(gFilledStartTime);
      break;

    case stDraining:
      totalDrained++;
      sprintf(data, "%03i", totalDrained);
      lcd.setCursor(9, 3); lcd.print(data);
      setValveTarget(gOutValve, vaOpen);
      gDrainStartTime = millis();
      Serial.print(millis()); Serial.print("\t stDraining gDrainStartTime= ");
      Serial.println(gDrainStartTime);
      break;

    case stDisabled:
      gDisabledStart = millis();
      //Serial.print(millis()); Serial.println("\t stDisabled");
      break;
  }
  gMainState = target;
}

void handleValve(sValve &valve) {
  if (valve.motion == moMoving) { // for OUT VALVE ONLY!!!
    if (getValvePosition(valve) == valve.target) {
      valve.motion = moStopped;
      setValvePower(valve, prDisabled);
      if (millis() < valve.moveStartTime + VALVE_TIMEOUT) {
        timeStr = String( (valve.moveStartTime + VALVE_TIMEOUT - millis()) / 1000.0, 1 );
        lcd.setCursor(17, 2);
        lcd.print( timeStr);
      }

    }
    else if (millis() > valve.moveStartTime + VALVE_TIMEOUT && !gError) {

      valve.motion = moStopped;
      startError( (getValvePosition(valve) == vaOpen) ? erOutOpen : erOutClose );
    }
    else {
      if (millis() < valve.moveStartTime + VALVE_TIMEOUT) {
        timeStr = String( (valve.moveStartTime + VALVE_TIMEOUT - millis()) / 1000.0, 1 );
        lcd.setCursor(17, 2);
        lcd.print( timeStr);
      }
    }
  }
}

/* Main */
void setup() {
  // put your setup code here, to run once:
  totalFilled = 0;
  totalDrained = 0;

  pinMode(RELAYA_OUT_VALVE_MAIN_PIN, OUTPUT);
  digitalWrite(RELAYA_OUT_VALVE_MAIN_PIN, RELAY_LOW );
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT );
  pinMode(WATER_LOW_PIN, INPUT_PULLUP);
  pinMode(WATER_HIGH_PIN, INPUT_PULLUP);
  pinMode(IR_INPUT_PIN, INPUT_PULLUP);
  pinMode(PING_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIRA_INPUT_PIN, INPUT);
  pinMode(RELAY_INVALVE_PIN, OUTPUT);
  pinMode(RELAYB_OUT_VALVE_PIN, OUTPUT);
  pinMode(RELAYC_OUT_VALVE_PIN, OUTPUT);
  pinMode(FULLY_OPEN_PIN, INPUT_PULLUP);
  pinMode(FULLY_CLOSED_PIN, INPUT_PULLUP);
  pinMode(ZERO_DRINKINGTIME_PIN, INPUT_PULLUP);

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
  gLastMs = millis();
  showState(gMainState);
  lcd.setCursor(0, 2); lcd.print("TIR");
  lcd.setCursor(0, 3); lcd.print("F 000  D 000");

}

long checkUltraSonic()
{
  digitalWrite(PING_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(PING_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(PING_PIN, LOW);
  
   return  pulseIn(ECHO_PIN, HIGH) / 74 / 2;
}
unsigned long wakeUp = 0;
bool PIR, SNOUT, UPPERWATER, LOWERWATER, FLUSH, ULTRASONIC;
bool lastSNOUT = false;
bool lastFLUSH = false;
unsigned long lastMs;
unsigned long totalSnoutMs = 0;

tValvePositions OUTVALVE, INVALVE;
String  SNOUTs, OUTVALVEs, INVALVEs, UPPERWATERs, LOWERWATERs, USs;
long inches;
long lastinches = -1;
/**************************************************************************************/
void loop() {

  // Read All of the sensors


  SNOUT = checkSnout();
  INVALVE = getValvePosition(gInValve);
  OUTVALVE = getValvePosition(gOutValve);
  UPPERWATER = checkUpperWater();
  LOWERWATER = checkLowerWater();
  FLUSH = !digitalRead(ZERO_DRINKINGTIME_PIN);
 inches = checkUltraSonic();
  if (inches==0) inches=9999;
  ULTRASONIC = inches <= TRIGGER_INCHES;
  if( lastinches != inches ) lcd.setCursor(10, 2); lcd.print("      ");
  timeStr = String(inches);
  lcd.setCursor(10, 2); lcd.print(timeStr);
  lastinches = inches;


  // fill sensor display strings
  USs = (ULTRASONIC) ? "*" : " ";
  SNOUTs = (SNOUT) ? "S" : "s";
  OUTVALVEs = (OUTVALVE == vaClosed) ? "C" : (OUTVALVE == vaOpen) ? "O" : "?";
  UPPERWATERs = (UPPERWATER) ? "U" : "u";
  LOWERWATERs = (LOWERWATER) ? "L" : "l";
  INVALVEs = (INVALVE == vaClosed) ? "w" : "W";

  lcd.setCursor(14, 3); lcd.print(USs + SNOUTs + UPPERWATERs + LOWERWATERs + INVALVEs + OUTVALVEs);

  unsigned long nowMs = millis();
  if (!lastSNOUT && SNOUT )
    lastMs = nowMs;
  else if (lastSNOUT && SNOUT ) {
    totalSnoutMs = totalSnoutMs + (nowMs - lastMs);
    lastMs = nowMs;
  }
  lastSNOUT = SNOUT;
  timeStr = String( totalSnoutMs / 1000.0, 1 );
  lcd.setCursor(3, 2); lcd.print(timeStr);

  if (!lastFLUSH && FLUSH ) //button pressed
    gDrainStartTime = -DRAIN_TIMEOUT;
  lastFLUSH = FLUSH;

  switch (gMainState) {
    case stIdle:
      if (ULTRASONIC || SNOUT) {
        Serial.print(millis()); Serial.print("\t TRIGGER US= "); Serial.print(ULTRASONIC);
        Serial.print("  IR="); Serial.println(SNOUT);
        changeMainState(stClosingOut);
      }
      timeStr = String( (millis() - gIdleStart) / 1000.0, 1 ) + " ";
      lcd.setCursor(5, 0); lcd.print(timeStr);
      break;

    case stClosingOut:
      if (OUTVALVE == vaClosed) {
        Serial.print(millis()); Serial.println("\t OutValve is closed");
        changeMainState(stFilling);
      }
      break;

    case stFilling:
      if (millis() >= gFillStartTime + FILL_TIMEOUT) {
        Serial.print(millis()); Serial.print("\t ERROR 3 did not Fill in ");
        Serial.println(FILL_TIMEOUT);
        startError(erFilling);
      }
      else if (UPPERWATER) {
        Serial.print(millis()); Serial.println("\t Upper water reached");
        changeMainState(stFilled);
      }
      else {
        timeStr = String( (gFillStartTime + FILL_TIMEOUT - millis()) / 1000.0, 1) + " ";
        lcd.setCursor(9, 0); lcd.print(timeStr);
      }
      break;

    case stFilled:
      if (!LOWERWATER) {
        Serial.print(millis()); Serial.println("\t TRIGGER Lower water broken");
        changeMainState(stFilling);
      }
      else if (millis() >= gFilledStartTime + OPEN_TIME_LENGTH) {
        Serial.print(millis()); Serial.print("\t Drinking Timeout ");
        Serial.println(gFilledStartTime + OPEN_TIME_LENGTH);
        changeMainState(stDraining);
      }
      else if (SNOUT) {
        gFilledStartTime = millis();
        Serial.print(millis()); Serial.print("\t Snout Triggerd a reset gFilledStartTime to");
        Serial.println(gFilledStartTime + OPEN_TIME_LENGTH);
      } else {
        timeStr = String( (gFilledStartTime + OPEN_TIME_LENGTH - millis()) / 1000.0, 1) + " ";
        lcd.setCursor(9, 0); lcd.print(timeStr);
      }
      break;

    case stDraining:
      if (!LOWERWATER && !UPPERWATER && OUTVALVE == vaOpen)  {
        changeMainState(stIdle);
      }
      else if (millis() >= gDrainStartTime + DRAIN_TIMEOUT) {
        Serial.print(millis()); Serial.print("\t ERROR 4 Failed to Drain in " );
        Serial.println(DRAIN_TIMEOUT);
        startError(erDraining);
      }
      else {
        timeStr = String( (gDrainStartTime + DRAIN_TIMEOUT - millis()) / 1000.0, 1) + " ";
        lcd.setCursor(9, 0); lcd.print(timeStr);
      }
      break;

    case stDisabled:
      timeStr = String( (millis() - gDisabledStart) / 1000.0, 1 ) + " ";
      lcd.setCursor(9, 0); lcd.print(timeStr);
      break;
  }

  handleValve(gInValve);
  handleValve(gOutValve);
  gMainStatePrev = gMainState;
  gDiffMs = millis() - gLastMs;
  if (gDiffMs <= 100)  delay(100 - gDiffMs);  else  Serial.println(gDiffMs);
  gLastMs = millis();
}
