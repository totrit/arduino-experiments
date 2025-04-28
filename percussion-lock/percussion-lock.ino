#include "SafeString.h"
#include <Servo.h>

createSafeString(logging, 128);

const int NO_VALUE = 0;
const int MAX_BEATS = 100;
const int SWITCH_LONG_PRESS_DURATION_MILLIS = 1000;
unsigned long secretSequence[MAX_BEATS];
unsigned long receivedSequence[MAX_BEATS];
bool wasPercussionKeyDown = false;
unsigned long timeMillisOfSwitchOn = NO_VALUE;
unsigned long beatIndicationStartTimeMillis = NO_VALUE;
bool hasDetectedLongPressOfSwitch = false;
enum Mode {
  Locked, Unlocking, Unlocked, SettingPassword
};
Mode mode = SettingPassword;
const int switchPin = 2;
const int percussionPin = 4;
const int servoPin = 9;
Servo myServo;
const int redLedPin = 11;
const int yellowLedPin = 12;
const int greenLedPin = 13;

void setup() {
  // put your setup code here, to run once:
  pinMode(switchPin, INPUT);
  pinMode(percussionPin, INPUT);
  myServo.attach(9);
  pinMode(redLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  resetPassword();
  resetReceivedBeats();
  Serial.begin(9600);
}

void loop() {
  const bool percussionKeyDown = digitalRead(percussionPin) == HIGH;
  const bool switchOn = digitalRead(switchPin) == HIGH;
  const bool beatDetected = hasDetectedBeat(percussionKeyDown);
  const bool hasFinishedShortPress = hasPressedSwitch(switchOn);
  const bool hasFinishedLongPress = hasLongPressedSwitch(switchOn);
  if (beatDetected) {
    indicateBeatDetected();
  }
  switch(mode) {
    case Locked:
    case Unlocking:
      if (hasFinishedShortPress) {
        resetReceivedBeats();
        mode = Locked;
        wasPercussionKeyDown = NO_VALUE;
      } else {
        const int unlockResult = tryUnlock(beatDetected);
        if (unlockResult == 1) {
          mode = Unlocked;
        } else if (unlockResult == -1) {
          mode = Locked;
        } else if (mode == Locked && beatDetected) {
          mode = Unlocking;
        }
      }
      break;
    case Unlocked:
      if (hasFinishedLongPress) {
        mode = SettingPassword;
        resetPassword();
      } else if (hasFinishedShortPress) {
        mode = Locked;
      }
      break;
    case SettingPassword:
      if (hasFinishedLongPress) {
        indicateBeatDetected();
        resetPassword();
      } else if (getLongArraySize(secretSequence) > 2 && hasFinishedShortPress || beatDetected && !continueToReceiveNewPassword()) {
        mode = Locked;
      }
      break;
  }

  logging = "beatDetected=";
  logging += beatDetected;
  logging += ", secret-size=";
  logging += getLongArraySize(secretSequence);
  logging += ", received-size=";
  logging += getLongArraySize(receivedSequence);
  Serial.println(logging);
  setLedForMode(mode);
  setServoForMode(mode);
}

bool hasDetectedBeat(bool percussionKeyDown) {
  if (wasPercussionKeyDown && !percussionKeyDown) {
    wasPercussionKeyDown = false;
    return true;
  } else if (percussionKeyDown && !wasPercussionKeyDown) {
    wasPercussionKeyDown = true;
  }
  return false;
}

void indicateBeatDetected() {
  beatIndicationStartTimeMillis = millis();
}

bool hasPressedSwitch(bool switchOn) {
  if (!hasDetectedLongPressOfSwitch && timeMillisOfSwitchOn != NO_VALUE && !switchOn) {
    timeMillisOfSwitchOn = NO_VALUE;
    return true;
  } else if (timeMillisOfSwitchOn == NO_VALUE && switchOn) {
    timeMillisOfSwitchOn = millis();
  } else if (!switchOn && timeMillisOfSwitchOn != NO_VALUE) {
    timeMillisOfSwitchOn = NO_VALUE;
    hasDetectedLongPressOfSwitch = false;
  }
  return false;
}

bool hasLongPressedSwitch(bool switchOn) {
  const unsigned long currentTimeMillis = millis();
  if (!hasDetectedLongPressOfSwitch && timeMillisOfSwitchOn != NO_VALUE && currentTimeMillis - timeMillisOfSwitchOn > SWITCH_LONG_PRESS_DURATION_MILLIS && switchOn) {
    hasDetectedLongPressOfSwitch = true;
    timeMillisOfSwitchOn = NO_VALUE;
    return true;
  } else if (timeMillisOfSwitchOn == NO_VALUE && switchOn) {
    timeMillisOfSwitchOn = millis();
  } else if (!switchOn && timeMillisOfSwitchOn != NO_VALUE) {
    timeMillisOfSwitchOn = NO_VALUE;
    hasDetectedLongPressOfSwitch = false;
  }
  return false;
}

int getLongArraySize(unsigned long arr[]) {
  int beatsCount = 0;
  for (int i = 0; i < MAX_BEATS; i ++) {
    if (arr[i] != NO_VALUE) {
      beatsCount ++;
    } else {
      break;
    }
  }
  return beatsCount;
}

int getIntArraySize(int arr[]) {
  int beatsCount = 0;
  for (int i = 0; i < MAX_BEATS; i ++) {
    if (arr[i] != NO_VALUE) {
      beatsCount ++;
    } else {
      break;
    }
  }
  return beatsCount;
}

void printArray(int arr[], int size) {
  for (int i = 0; i < size; i++) {
    Serial.print(arr[i]);
    Serial.print(" ");
  }
}

int* getBeatsPatternDisregardingSpeed(unsigned long beats[]) {
  const int beatsCount = getLongArraySize(beats);
  if (beatsCount < 2) {
    return nullptr;
  }
  int* ret = (int*) malloc(beatsCount * sizeof(int));
  ret[beatsCount - 1] = NO_VALUE; // For easier checking size of array
  for (int i = 1; i < beatsCount; i ++) {
    ret[i - 1] = (unsigned int) (beats[i] - beats[i - 1]);
  }
  // Normalise numbers to 100-based
  float factor = (float)100 / (float) ret[0];
  ret[0] = 100;
  for (int i = 1; i < beatsCount - 1; i ++) {
    ret[i] = (int)(ret[i] * factor);
  }
  return ret;
}

// -1: unmatched; 0: matching; 1: matched
int matchPatterns(int target[], int test[]) {
  const int testArraySize = (test == nullptr? 0 : getIntArraySize(test));
  const int targetArraySize = (target == nullptr? 0 : getIntArraySize(target));
  for (int i = 0; i < testArraySize && i < targetArraySize; i ++) {
    if (!similarValue(test[i], target[i])) {
      return -1;
    }
  }
  if (testArraySize < targetArraySize) {
    return 0;
  } else if (testArraySize == targetArraySize) {
    return 1;
  } else {
    return -1;
  }
}

int tryUnlock(bool beatDetected) {
  if (beatDetected) {
    for (int i = 0; i < MAX_BEATS; i ++) {
      if (receivedSequence[i] == NO_VALUE) {
        receivedSequence[i] = millis();
        break;
      }
    }
    // Check if sequences match
    int* secretPattern = getBeatsPatternDisregardingSpeed(secretSequence);
    int* tryingPattern = getBeatsPatternDisregardingSpeed(receivedSequence);
    int matchResult = matchPatterns(secretPattern, tryingPattern);

    if (secretPattern != nullptr) {
      free(secretPattern);
    }
    if (tryingPattern != nullptr) {
      free(tryingPattern);
    }
    switch(matchResult) {
      case -1:
        resetReceivedBeats();
        return -1;
      case 1:
        resetReceivedBeats();
        return 1;
      default:
        return 0;
    }
  } else {
    return 0;
  }
}

bool similarValue(int val1, int val2) {
  return ((float) min(val1, val2) / (float) max(val1, val2)) > 0.50;
}

// Return false if can't anymore beats
bool continueToReceiveNewPassword() {
  for (int i = 0; i < MAX_BEATS; i ++) {
    if (secretSequence[i] == NO_VALUE) {
      secretSequence[i] = millis();
      return true;
    }
  }
  return false;
}

void setLedForMode(Mode mode) {
  bool redOn = false;
  bool yellowOn = false;
  bool greenOn = false;

  switch(mode) {
    case Locked:
      redOn = true;
      break;
    case Unlocking:
      yellowOn = true;
      break;
    case Unlocked:
      greenOn = true;
      break;
    case SettingPassword:
      redOn = yellowOn = greenOn = true;
      break;
  }
  if (millis() - beatIndicationStartTimeMillis < 100) {
    yellowOn = !yellowOn;
  }
  digitalWrite(redLedPin, redOn);
  digitalWrite(yellowLedPin, yellowOn);
  digitalWrite(greenLedPin, greenOn);
}

void setServoForMode(Mode mode) {
  if (mode == Unlocked) {
    myServo.write(90);
  } else {
    myServo.write(0);
  }
}

void resetPassword() {
  for (int i = 0; i < MAX_BEATS; i ++) {
    secretSequence[i] = NO_VALUE;
  }
}

void resetReceivedBeats() {
  for (int i = 0; i < MAX_BEATS; i ++) {
    receivedSequence[i] = NO_VALUE;
  }
}
