#define DECODE_NEC
#define EXCLUDE_UNIVERSAL_PROTOCOLS
#define EXCLUDE_EXOTIC_PROTOCOLS
#include <IRremote.hpp>
    #include <Wire.h>
    #include <LiquidCrystal_I2C.h>
    #include <math.h>

    // ==================================================
// PIN ASSIGNMENTS
// ==================================================
const byte IR_PIN = 3;
const byte PWM_PIN = 5;
const byte RELAY_PIN = 12;
const byte BANK_SENSE_PIN = A0;
const byte CAP_SENSE_PIN = A1;
const byte OUTPUT_SENSE_PIN = A3;

// ==================================================
// LCD
// ==================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==================================================
// RELAY LOGIC
// ==================================================
const byte PARALLEL_MODE = HIGH;
const byte SERIES_MODE = LOW;
bool seriesModeActive = false;

// ==================================================
// VOLTAGE MEASUREMENT SETTINGS
// ==================================================
// Arduino Uno ADC reference = approximately 5 V.
//
// Voltage divider:
// 3.3k upper resistor
// 1k lower resistor
//
// Divider factor:
// (3.3k + 1k) / 1k = 4.30
// ==================================================
const float ADC_REFERENCE = 5.00;
const float BANK_FACTOR = 4.30;
const float CAP_FACTOR = 4.30;
const float OUTPUT_FACTOR = 4.30;
const float MAX_SELECTED_VOLTAGE = 4.30;
const float VOLTAGE_TOLERANCE = 0.02;

// ==================================================
// ONE TARGET VOLTAGE
// ==================================================
// This is the ONLY target-voltage variable.
//
// Arduino starts with 3.60 V.
// Remote input changes this same variable.
// ==================================================
float targetVoltage = 3.50;

// ==================================================
// AUTOMATIC BANK SWITCHING SETTINGS
// ==================================================
const float SERIES_HEADROOM = 0.30;
const float PARALLEL_HEADROOM = 0.50;
const float MAX_CAP_DIFFERENCE = 0.20;
const unsigned long MIN_MODE_TIME_MS = 1000;
const unsigned long SWITCH_CHECK_INTERVAL_MS = 100;
const byte REQUIRED_SWITCH_SAMPLES = 5;
byte seriesConfirmCount = 0;
byte parallelConfirmCount = 0;

// ==================================================
// CONTROL VARIABLES
// ==================================================
float measuredOutput = 0.00;
int pwmValue = 0;
int firstDigit = -1;

// ==================================================
// TIMING VARIABLES
// ==================================================
unsigned long lastModeChangeTime = 0;
unsigned long lastSwitchCheckTime = 0;
unsigned long lastRegulationTime = 0;
unsigned long lastLcdUpdateTime = 0;
unsigned long temporaryScreenUntil = 0;
unsigned long firstDigitTime = 0;
const unsigned long DIGIT_TIMEOUT_MS = 5000;

// ==================================================
// LCD SCREEN STATES
// ==================================================
enum ScreenState
{
    NORMAL_SCREEN,
        ENTERING_SCREEN,
        CONFIRM_SCREEN,
        ERROR_SCREEN
};
ScreenState screenState = NORMAL_SCREEN;
ScreenState previousScreenState = NORMAL_SCREEN;

// ==================================================
// CUSTOM LCD CHARACTERS
// ==================================================
byte parallelCharacter[8] =
    {
    B00001,
        B00111,
        B00101,
        B11100,
        B00100,
        B00101,
        B00111,
        B00001
};
byte parallelCharacters[8] =
    {
    B10000,
        B11100,
        B10100,
        B00111,
        B00100,
        B10100,
        B11100,
        B10000
};
byte seriesCharacter[8] =
    {
    B01010,
        B01010,
        B11011,
        B01010,
        B01010
};
byte seriesCharacters[8] =
    {
    B01010,
        B01010,
        B11011,
        B01010,
        B01010
};

// ==================================================
// FUNCTION PROTOTYPES
// ==================================================
int commandToDigit(uint16_t address, uint16_t command);
float readArduinoVoltage(byte pin);
float readBankVoltage();
float readOneCapacitorVoltage();
float readOutputVoltage();
void setBankMode(bool useSeriesMode);
void switchBankSafely(bool switchToSeries);
void automaticBankControl();
int estimatePwm(float requestedVoltage);
void setTargetVoltage(float requestedVoltage);
void regulateOutput();
void processDigit(int digit);
void readRemote();
void checkDigitTimeout();
void showTopRow();
void showNormalScreen();
void showEnteringScreen();
void showConfirmationScreen();
void showErrorScreen();
void updateLcd();

// ==================================================
// SETUP
// ==================================================
void setup()
{
    // ------------------------------------------------
    // Serial Monitor
    // ------------------------------------------------
    Serial.begin(9600);
    // ------------------------------------------------
    // LCD
    // ------------------------------------------------
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, parallelCharacter);
    lcd.createChar(1, parallelCharacters);
    lcd.createChar(2, seriesCharacter);
    lcd.createChar(3, seriesCharacters);
    lcd.clear();
    // ------------------------------------------------
    // OUTPUT PINS
    // ------------------------------------------------
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(PWM_PIN, OUTPUT);
    // ------------------------------------------------
    // START IN PARALLEL
    // ------------------------------------------------
    setBankMode(false);
    lastModeChangeTime = millis();
    lastSwitchCheckTime = millis();
    // ------------------------------------------------
    // INITIAL PWM
    //
    // Notice:
    // We DO NOT set targetVoltage again here.
    //
    // targetVoltage was already declared as 3.60 V.
    // ------------------------------------------------
    pwmValue = estimatePwm(targetVoltage);
    analogWrite(PWM_PIN, pwmValue);
    // ------------------------------------------------
    // INITIAL LCD
    // ------------------------------------------------
    showNormalScreen();
    // ------------------------------------------------
    // START IR RECEIVER
    // ------------------------------------------------
    IrReceiver.begin(IR_PIN, false);
    // ------------------------------------------------
    // STARTUP INFORMATION
    // ------------------------------------------------
    Serial.println();
    Serial.println("================================");
    Serial.println("AUTOMATIC CAPACITOR CONTROL");
    Serial.println("================================");
    Serial.print("Target = ");
    Serial.print(targetVoltage, 2);
    Serial.println(" V");
    Serial.println("IR REMOTE READY");
    Serial.println("Press two digits for target.");
    Serial.println("Example: 3 then 5 = 3.50 V");
    Serial.println("================================");
}

// ==================================================
// MAIN LOOP
// ==================================================
void loop()
{
    readRemote();
    checkDigitTimeout();
    automaticBankControl();
    regulateOutput();
    updateLcd();
}

// ==================================================
// REMOTE BUTTON MAPPING
// ==================================================
//
// These are the codes measured from YOUR
// physical remote.
//
// ==================================================
int commandToDigit(uint16_t address, uint16_t command)
{
    if (address == 0xBF00)
    {
        switch (command)
        {
            case 0xC: return 0;
            case 0x10: return 1;
            case 0x11: return 2;
            case 0x12: return 3;
            case 0x14: return 4;
            case 0x15: return 5;
            case 0x16: return 6;
            case 0x18: return 7;
            case 0x19: return 8;
            case 0x1A: return 9;
            default:
                return -1;
        }
    }
    return -1;
}

// ==================================================
// READ ARDUINO ANALOG VOLTAGE
// ==================================================
float readArduinoVoltage(byte pin)
{
    unsigned long total = 0;
    const byte NUMBER_OF_SAMPLES = 20;
    for (byte sample = 0;
    sample < NUMBER_OF_SAMPLES;
    sample++)
    {
        total += analogRead(pin);
    }
    float averageAdc =
        total / float(NUMBER_OF_SAMPLES);
    return averageAdc *
        ADC_REFERENCE /
        1023.0;
}

// ==================================================
// A0 - TOTAL BANK VOLTAGE
// ==================================================
float readBankVoltage()
{
    float a0Voltage =
        readArduinoVoltage(BANK_SENSE_PIN);
    return a0Voltage *
        BANK_FACTOR;
}

// ==================================================
// A1 - ONE CAPACITOR VOLTAGE
// ==================================================
float readOneCapacitorVoltage()
{
    float a1Voltage =
        readArduinoVoltage(CAP_SENSE_PIN);
    return a1Voltage *
        CAP_FACTOR;
}

// ==================================================
// A3 - OUTPUT VOLTAGE
// ==================================================
float readOutputVoltage()
{
    float a3Voltage =
        readArduinoVoltage(OUTPUT_SENSE_PIN);

    return a3Voltage *
        OUTPUT_FACTOR;
}

// ==================================================
// SET CAPACITOR BANK MODE
// ==================================================
void setBankMode(bool useSeriesMode)
{
    seriesModeActive = useSeriesMode;
    digitalWrite(
        RELAY_PIN,
        seriesModeActive
    ? SERIES_MODE
    : PARALLEL_MODE
    );
}

// ==================================================
// SAFE RELAY SWITCH
// ==================================================
void switchBankSafely(bool switchToSeries)
{
    // Temporarily disable PWM.
    analogWrite(PWM_PIN, 0);
    delay(30);
    // Change capacitor configuration.
    setBankMode(switchToSeries);
    delay(30);
    // Restore PWM using THE SAME targetVoltage.
    pwmValue =
        estimatePwm(targetVoltage);
    analogWrite(PWM_PIN, pwmValue);
    // Reset timers.
    lastRegulationTime = 0;
    lastModeChangeTime = millis();
    lastSwitchCheckTime = millis();
    lastLcdUpdateTime = 0;
    // Reset confirmation counters.
    seriesConfirmCount = 0;
    parallelConfirmCount = 0;
    if (switchToSeries)
    {
        Serial.println();
        Serial.println("=============================");
        Serial.println(">>> SWITCHED TO SERIES <<<");
        Serial.println("=============================");
    }
    else
    {
        Serial.println();
        Serial.println("=============================");
        Serial.println(">>> SWITCHED TO PARALLEL <<<");
        Serial.println("=============================");
    }
}

// ==================================================
// AUTOMATIC SERIES / PARALLEL CONTROL
// ==================================================
void automaticBankControl()
{
    float bankVoltage =
        readBankVoltage();
    float measuredCapVoltage =
        readOneCapacitorVoltage();
    measuredOutput =
        readOutputVoltage();

    // =================================================
    // TARGET = 0 V
    // =================================================
    if (targetVoltage <= 0.05)
    {
        pwmValue = 0;
        analogWrite(PWM_PIN, 0);
        seriesConfirmCount = 0;
        parallelConfirmCount = 0;
        if (!seriesModeActive)
        {
            return;
        }
        if (millis() - lastModeChangeTime <
            MIN_MODE_TIME_MS)
        {
            return;
        }
        float otherCapVoltage =
            bankVoltage - measuredCapVoltage;
        if (otherCapVoltage < 0.0)
        {
            otherCapVoltage = 0.0;
        }
        float difference =
            fabs(
            measuredCapVoltage -
            otherCapVoltage
        );
        if (difference <= MAX_CAP_DIFFERENCE)
        {
            switchBankSafely(false);
            pwmValue = 0;
            analogWrite(PWM_PIN, 0);
        }
        return;
    }

    // =================================================
    // WAIT AFTER RELAY SWITCH
    // =================================================
    if (millis() - lastModeChangeTime <
        MIN_MODE_TIME_MS)
    {
        seriesConfirmCount = 0;
        parallelConfirmCount = 0;
        return;
    }

    // =================================================
    // CHECK SWITCHING CONDITIONS EVERY 100 ms
    // =================================================
    if (millis() - lastSwitchCheckTime <
        SWITCH_CHECK_INTERVAL_MS)
    {
        return;
    }
    lastSwitchCheckTime = millis();

    // =================================================
    // CURRENTLY IN PARALLEL
    // =================================================
    if (!seriesModeActive)
    {
        float seriesThreshold =
            targetVoltage + SERIES_HEADROOM;
        if (measuredCapVoltage <= seriesThreshold &&
            bankVoltage > 0.80)
        {
            seriesConfirmCount++;
            Serial.print("Parallel -> Series check: ");
            Serial.print(seriesConfirmCount);
            Serial.print("/");
            Serial.print(REQUIRED_SWITCH_SAMPLES);
            Serial.print(" | Cap=");
            Serial.print(measuredCapVoltage, 2);
            Serial.print("V | Threshold=");
            Serial.print(seriesThreshold, 2);
            Serial.println("V");
            if (seriesConfirmCount >=
                REQUIRED_SWITCH_SAMPLES)
            {
                switchBankSafely(true);
                return;
            }
        }
        else
        {
            seriesConfirmCount = 0;
        }
        parallelConfirmCount = 0;
        return;
    }

    // =================================================
    // CURRENTLY IN SERIES
    // =================================================
    float otherCapVoltage =
        bankVoltage - measuredCapVoltage;
    if (otherCapVoltage < 0.0)
    {
        otherCapVoltage = 0.0;
    }
    float voltageDifference =
        fabs(
        measuredCapVoltage -
        otherCapVoltage
    );
    float parallelThreshold =
        targetVoltage + PARALLEL_HEADROOM;
    bool cap1HasEnoughVoltage =
        measuredCapVoltage >=
        parallelThreshold;
    bool cap2HasEnoughVoltage =
        otherCapVoltage >=
        parallelThreshold;
    bool capacitorsBalanced =
        voltageDifference <=
        MAX_CAP_DIFFERENCE;

    // =================================================
    // SERIES -> PARALLEL
    // =================================================
    if (cap1HasEnoughVoltage &&
        cap2HasEnoughVoltage &&
        capacitorsBalanced)
    {
        parallelConfirmCount++;
        Serial.print("Series -> Parallel check: ");
        Serial.print(parallelConfirmCount);
        Serial.print("/");
        Serial.print(REQUIRED_SWITCH_SAMPLES);
        Serial.print(" | C1=");
        Serial.print(
            measuredCapVoltage,
            2
        );
        Serial.print("V | C2=");
        Serial.print(
            otherCapVoltage,
            2
        );
        Serial.print("V | Diff=");
        Serial.print(
            voltageDifference,
            2
        );
        Serial.println("V");
        if (parallelConfirmCount >=
            REQUIRED_SWITCH_SAMPLES)
        {
            switchBankSafely(false);
            return;
        }
    }
    else
    {
        parallelConfirmCount = 0;
    }
    seriesConfirmCount = 0;
}

// ==================================================
// ESTIMATE PWM FOR TARGET VOLTAGE
// ==================================================
int estimatePwm(float requestedVoltage)
{
    if (requestedVoltage <= 0.02)
    {
        return 0;
    }
    float estimatedPwm =
        (requestedVoltage + 0.664) /
        0.01914;

    return constrain(
        int(round(estimatedPwm)),
        0,
        255
    );
}

// ==================================================
// CHANGE THE ONE TARGET VOLTAGE
// ==================================================
void setTargetVoltage(float requestedVoltage)
{
    // This changes THE SAME targetVoltage variable.
    targetVoltage =
        constrain(
        requestedVoltage,
        0.00,
        MAX_SELECTED_VOLTAGE
    );
    pwmValue =
        estimatePwm(targetVoltage);
    analogWrite(
        PWM_PIN,
        pwmValue
    );
    // Reset relay decision counters because
    // the target has changed.
    seriesConfirmCount = 0;
    parallelConfirmCount = 0;
    Serial.println();
    Serial.println("=============================");
    Serial.print("NEW TARGET = ");
    Serial.print(targetVoltage, 2);
    Serial.println(" V");
    Serial.println("=============================");
}

// ==================================================
// CLOSED-LOOP OUTPUT REGULATION
// ==================================================
void regulateOutput()
{
    if (millis() - lastRegulationTime < 100)
    {
        return;
    }
    lastRegulationTime = millis();
    measuredOutput =
        readOutputVoltage();
    // ------------------------------------------------
    // 0 V target
    // ------------------------------------------------
    if (targetVoltage <= 0.02)
    {
        pwmValue = 0;
        analogWrite(PWM_PIN, 0);
        return;
    }
    // ------------------------------------------------
    // Output too low
    // ------------------------------------------------
    if (measuredOutput <
        targetVoltage -
        VOLTAGE_TOLERANCE)
    {
        if (pwmValue < 255)
        {
            pwmValue++;
        }
    }
    // ------------------------------------------------
    // Output too high
    // ------------------------------------------------
    else if (measuredOutput >
        targetVoltage +
        VOLTAGE_TOLERANCE)
    {
        if (pwmValue > 0)
        {
            pwmValue--;
        }
    }
    analogWrite(
        PWM_PIN,
        pwmValue
    );
}

// ==================================================
// PROCESS REMOTE DIGITS
// ==================================================
void processDigit(int digit)
{

    // =================================================
    // FIRST DIGIT
    // =================================================
    if (firstDigit < 0)
    {
        if (digit > 4)
        {
            screenState =
                ERROR_SCREEN;
            temporaryScreenUntil =
                millis() + 500;
            return;
        }
        firstDigit = digit;
        firstDigitTime = millis();
        screenState =
            ENTERING_SCREEN;
        return;
    }

    // =================================================
    // SECOND DIGIT
    // =================================================
    float selectedVoltage =
        firstDigit +
        digit / 10.0;
    if (selectedVoltage <=
        MAX_SELECTED_VOLTAGE)
    {
        // IMPORTANT:
        // There is no separate confirmedVoltage anymore.
        //
        // The selected value goes directly into the ONE
        // targetVoltage variable.
        setTargetVoltage(selectedVoltage);
        screenState =
            CONFIRM_SCREEN;
        temporaryScreenUntil =
            millis() + 500;
    }
    else
    {
        screenState =
            ERROR_SCREEN;

        temporaryScreenUntil =
            millis() + 500;
    }
    firstDigit = -1;
}

// ==================================================
// READ IR REMOTE
// ==================================================
void readRemote()
{
    if (!IrReceiver.decode())
    {
        return;
    }
    uint16_t address =
        IrReceiver.decodedIRData.address;
    uint16_t command =
        IrReceiver.decodedIRData.command;
    // ------------------------------------------------
    // Show every detected button press.
    // ------------------------------------------------
    Serial.println();
    Serial.println("========================");
    Serial.println("BUTTON PRESSED");
    Serial.println("========================");
    Serial.print("Address: 0x");
    Serial.println(address, HEX);
    Serial.print("Command: 0x");
    Serial.println(command, HEX);
    int digit =
        commandToDigit(
        address,
        command
    );
    // ------------------------------------------------
    // Recognised number
    // ------------------------------------------------
    if (digit >= 0)
    {
        Serial.print("Number: ");
        Serial.println(digit);
        processDigit(digit);
    }
    else
    {
        Serial.println("Number: NOT MAPPED");
    }
    Serial.println("========================");
    IrReceiver.resume();
}

// ==================================================
// REMOTE ENTRY TIMEOUT
// ==================================================
void checkDigitTimeout()
{
    if (firstDigit >= 0 &&
        millis() - firstDigitTime >
        DIGIT_TIMEOUT_MS)
    {
        firstDigit = -1;
        screenState =
            NORMAL_SCREEN;
    }
}

// ==================================================
// LCD TOP ROW
// ==================================================
void showTopRow()
{
    measuredOutput =
        readOutputVoltage();
    lcd.setCursor(0, 0);
    // ------------------------------------------------
    // Series / Parallel symbol
    // ------------------------------------------------
    if (seriesModeActive)
    {
        lcd.write(byte(2));
        lcd.write(byte(3));
    }
    else
    {
        lcd.write(byte(0));
        lcd.write(byte(1));
    }
    // ------------------------------------------------
    // Measured output
    // ------------------------------------------------
    lcd.setCursor(2, 0);
    lcd.print(
        measuredOutput,
        2
    );
    lcd.print("V  ");
    // ------------------------------------------------
    // Surname
    // ------------------------------------------------
    lcd.setCursor(8, 0);
    lcd.print("NGOEPE");
}

// ==================================================
// NORMAL LCD SCREEN
// ==================================================
void showNormalScreen()
{
    float bankVoltage =
        readBankVoltage();
    float capacitorVoltage =
        readOneCapacitorVoltage();
    showTopRow();
    // ------------------------------------------------
    // Total bank voltage
    // ------------------------------------------------
    lcd.setCursor(0, 1);
    lcd.print(
        bankVoltage,
        2
    );
    lcd.print("V   ");
    // ------------------------------------------------
    // One capacitor voltage
    // ------------------------------------------------
    lcd.setCursor(10, 1);
    lcd.print(
        capacitorVoltage,
        2
    );
    lcd.print("V   ");
}

// ==================================================
// ENTERING TARGET SCREEN
// ==================================================
void showEnteringScreen()
{
    showTopRow();
    lcd.setCursor(0, 1);
    lcd.print("Input = ");
    lcd.print(firstDigit);
    lcd.print(".00V ");
}

// ==================================================
// CONFIRM TARGET SCREEN
// ==================================================
void showConfirmationScreen()
{
    showTopRow();
    lcd.setCursor(0, 1);
    lcd.print("Input = ");
    // Display the SAME targetVoltage variable.
    lcd.print(
        targetVoltage,
        2
    );
    lcd.print("V ");
}

// ==================================================
// ERROR SCREEN
// ==================================================
void showErrorScreen()
{
    showTopRow();

    lcd.setCursor(0, 1);

    lcd.print(
        "Invalid 0-4.30V "
    );
}

// ==================================================
// UPDATE LCD
// ==================================================
void updateLcd()
{
    // Return confirmation/error screen to normal.
    if ((screenState == CONFIRM_SCREEN ||
        screenState == ERROR_SCREEN) &&
        millis() >= temporaryScreenUntil)
    {
        screenState =
            NORMAL_SCREEN;
    }

    // Clear LCD only when screen changes.
    if (screenState !=
        previousScreenState)
    {
        lcd.clear();
        previousScreenState =
            screenState;
    }
    // Update LCD every 200 ms.
    if (millis() -
        lastLcdUpdateTime <
        200)
    {
        return;
    }
    lastLcdUpdateTime =
        millis();
    switch (screenState)
    {
        case NORMAL_SCREEN:
            showNormalScreen();
        break;
        case ENTERING_SCREEN:
            showEnteringScreen();
        break;
        case CONFIRM_SCREEN:
            showConfirmationScreen();
        break;
        case ERROR_SCREEN:
            showErrorScreen();
        break;
    }
}
