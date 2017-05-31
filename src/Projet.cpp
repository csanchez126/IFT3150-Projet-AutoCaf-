#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

#include <MyLib.h>
#include <Status.h>

// LCD SETUP
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// ================Worry free global values=======================
//Pin declaration
#define DOUT  3
#define CLK  2

int buttonPin = 5;     // the number of the pushbutton pin
int greenLedPin = 6;
int redLedPin = 7;
int potPin = A0;    // select the input pin for the potentiometer

HX711 scale(DOUT, CLK);
float base_calibration_factor = 225;
float calibration_factor = 225;
float calibration_multiplier = 1;
float pin_inc = 10;   // calibration_factor increments
float inc_sens = 20;  // Values the pot has to increase before incrementing calibration_factor
int displayMode = 2;  // What info should the LCD show

int   potValue = 0;                  // variable to store the value coming from the sensor
int   buttonState;                   // For manual taring
int   lastButtonState = LOW;         // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

//=================SENSITIVE global stuff========================
int   loopDelta = 20;
bool  calibrationMode = false;
float lastReadWeight = 0;

float weightVarThresh = 0.5;
float weightDiffThresh = 5;
int   stableTime = 3000;
bool  waitingForStability = true;
long  fluctuationTime = 0;
bool  isTaring = false;
bool  endingTransaction = false;

float containerWeight;
float drinkWeight;
float weightVar;
float lastStableWeight = 0;
float sensorWeight;

STATUS sysStatus;
//=================================================================
void userLoop(){
  switch(sysStatus){
    case STANDBY:
      if(containerWeight>1){
        sysStatus = AUTHENTICATION;
        Serial.println("CONTAINER WEIGHT: "+String(containerWeight));
      }
    break;
    case AUTHENTICATION:
      // IF NOTHING IS READ WHITIN A CERTAIN DELAY,
      // INTERPRET FLUCTUATION AS A FALSE POSITIVE AND REVERT TO STANDBY
      Serial.println("SIMULATING READING NFC CHIP BEEP BOOP");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, POUR SOME JAVA ON ME");
      setServingSensitivity();
      sysStatus = SERVING;
    break;
    case SERVING:
      waitBlink(-10, 2, greenLedPin);
    break;
    case PAYMENT:
      Serial.println("SIMULATING NFC PAYMENT BEEP BOOP");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, ENJOY YOUR DRINK");
      setClearingSensitivity();
      sysStatus = CLEARING;
    break;
    case CLEARING:
      waitBlink(0, 10000, greenLedPin);
      if(lastStableWeight < 0){
        Serial.println("CUSTOMER LEFT");
        containerWeight = 0;
        drinkWeight = 0;
        setStandbySensitivity();
        digitalWrite(greenLedPin, HIGH);
        scale.tare();
        sysStatus = STANDBY;
      }
    break;
  }
}

void setup()   {
  Serial.begin(9600);
  Serial.println("Starting! Enter 'help' to see command list");
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
  // init done
  pinMode(buttonPin, INPUT);
  pinMode(greenLedPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  scale.set_scale();
  scale.tare();  //Reset the scale to 0
  digitalWrite(greenLedPin, HIGH);
  sysStatus = STANDBY;
}

void loop() {
  updateWeight();   //Read scale and update weight related values
  stabilityCheck(); //Checks weight related values and determines stability
  userLoop();       //After weight and stability updates, use info accordingly in use case loop

  //Display management
  setDisplayWeight(lastReadWeight);
  setDisplayInfo(displayMode);
  activateDisplay(loopDelta);
  
  //Hardware and serial stuff
  serialCommands();
  readButtonTare();
}
