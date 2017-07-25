// Dawn & Dusk controller with frost protection.
// 5th December 2014. Modified 17th April 2015
// (C) A.G.Doswell 2014 - 2016
//
// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
//
// Designed to control a relay connected to pin A3. Pin goes low during daylight hours and high during night. Relay uses active low, so is
// "On" during the day. This is connected to the fountain pump in my garden.
//
// Time is set using a rotary encoder with integral push button. The Encoder is connected to interrupt pins D2 & D3 (and GND), 
// and the push button to pin analogue 0 (and GND)
// The RTC is connections are: Analogue pin 4 to SDA. Connect analogue pin 5 to SCL.
// A 2 x 16 LCD display is connected as follows (NOTE. This is NOT conventional, as interrupt pins are required for the encoder)
//  Arduino LCD  
//  D4      DB7
//  D5      DB6
//  D6      DB5
//  D7      DB4
//  D12     RS
//  D13     E
// 
// In this revision, there is a Dallas 18B20 Temperature sensor connected to pin 8, enabling frost protection. This is pulled up to +5volts via a 4.7K resistor.
//
// Use: Pressing and holding the button will enter the clock set mode (on release of the button). Clock is set using the rotary encoder. 
// The clock must be set to UTC.
// Pressing and releasing the button quickly will display the current sun rise and sun set times. Pressing the button again will enter the mode select menu. 
// Modes are AUTO: On when the sun rises, off when it sets.
//           AUTO EXTEND : same as above but goes off 1 hour after sunet (nice for summer evenings!) 
//           ON: Permanently ON
//           OFF: Permanently OFF (Who'd have guessed it?)
//
// Change the LATTITUDE and LONGITUDE constant to your location.
// Use the address finder from http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html to find the address of your temperature sensor and enter it where required in DeviceAddress.
// 28th Dec 2014 - Slowed the rate at which the temperature is requested down to once per minute. It has improved the clock display, and solved an issue whereby the temp sensor wasn't always ready to send data, and hung up.
// 10th Jan 2015 - Altered the method of updating the temp to something simpler and more sensible!
// 17th April 2015 - Stored Mode settings in EEPROM// 
//
// Be sure to visit my website at http://andydoz.blogspot.com

#include <Wire.h>
#include "RTClib.h" // from https://github.com/adafruit/RTClib
#include <LiquidCrystal.h>
#include <Encoder.h> // from http://www.pjrc.com/teensy/td_libs_Encoder.html
#include <TimeLord.h> // from http://swfltek.com/arduino/timelord.html. When adding it to your IDE, rename the file, removing the "-depreciated" 
#include <OneWire.h> // from http://playground.arduino.cc/Learning/OneWire
#include <DallasTemperature.h> // from http://www.hacktronics.com/code/DallasTemperature.zip. When adding this to your IDE, ensure the .h and .cpp files are in the top directory of the library.
#include <EEPROM.h> 

#define ONE_WIRE_BUS 8 // Data wire from temp sensor is plugged into pin 8 on the Arduino
OneWire oneWire(ONE_WIRE_BUS); // Setup a oneWire instance to communicate with any OneWire devices
DeviceAddress outsideThermometer = { 0x28, 0x1A, 0x1A, 0x3E, 0x06, 0x00, 0x00,0xC7 }; // use the address finder from http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html to find the address of your device.
RTC_DS1307 RTC; // Tells the RTC library that we're using a DS1307 RTC
Encoder knob(2, 3); //encoder connected to pins 2 and 3 (and ground)
LiquidCrystal lcd(12, 13, 7, 6, 5, 4); // I used an odd pin combination because I need pin 2 and 3 for the interrupts.
//the variables provide the holding values for the set clock routine
int setyeartemp; 
int setmonthtemp;
int setdaytemp;
int sethourstemp;
int setminstemp;
int setsecs = 0;
int maxday; // maximum number of days in the given month
int TimeMins = 0; // number of seconds since midnight
int TimerMode = 2; //mode 0=Off 1=On 2=Auto
int TimeOut = 10;
int TimeOutCounter;

// These variables are for the push button routine
int buttonstate = 0; //flag to see if the button has been pressed, used internal on the subroutine only
int pushlengthset = 3000; // value for a long push in mS
int pushlength = pushlengthset; // set default pushlength
int pushstart = 0;// sets default push value for the button going low
int pushstop = 0;// sets the default value for when the button goes back high

int knobval; // value for the rotation of the knob
boolean buttonflag = false; // default value for the button flag
float tempC; // Temperature

// Over-Ride feature flag for Raminda Subhashana
boolean OverRideFlag = false;


const int TIMEZONE = 0; //UTC
const float LATITUDE = 20.00, LONGITUDE = -2.00;  // set YOUR position here 
int Sunrise, Sunset; //sunrise and sunset expressed as minute of day (0-1439)
TimeLord myLord; // TimeLord Object, Global variable
byte sunTime[]  = {0, 0, 0, 1, 1, 13}; // 17 Oct 2013
int SunriseHour, SunriseMin, SunsetHour, SunsetMin; //Variables used to make a decent display of our sunset and sunrise time.
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature.

void setup () {
    //Serial.begin(57600); //start debug serial interface
    Wire.begin(); //start I2C interface
    RTC.begin(); //start RTC interface
    lcd.begin(16,2); //Start LCD (defined as 16 x 2 characters)
    lcd.clear(); 
    pinMode(A0,INPUT);//push button on encoder connected to A0 (and GND)
    digitalWrite(A0,HIGH); //Pull A0 high
    pinMode(A1,INPUT);// Over ride push button connected to A1 (and GND)
    digitalWrite(A1,HIGH);
    pinMode(A3,OUTPUT); //Relay connected to A3
    digitalWrite (A3, HIGH); //sets relay off (default condition)
    sensors.begin();
    sensors.setResolution(outsideThermometer, 12); // set the resolution to 12 bits (why not?!)
    
    //Checks to see if the RTC is runnning, and if not, sets the time to the time this sketch was compiled, also sets default mode to 2 and writes to EEPROM
    DateTime now = RTC.now();
    lcd.clear();
    lcd.print("Waiting for RTC");
    delay (60000);// wait a minute for the RTC to sort itself out.
    if (! RTC.isrunning()) {
    RTC.adjust(DateTime(__DATE__, __TIME__));
    EEPROM.update (0,0); // set the auto mode as 0 (off) for default
    }
  
 
   lcd.print("Dawn Dusk Pond");
   lcd.setCursor(0, 1);
   lcd.print("Pump Controller");
   delay (2000);
   lcd.clear();
   lcd.print("Version 1.32");
   lcd.setCursor(0, 1);
   lcd.print("(C) A.G.Doswell ");
   delay (5000);
   lcd.clear();
    //Timelord initialisation
    myLord.TimeZone(TIMEZONE * 60);
    myLord.Position(LATITUDE, LONGITUDE);
    CalcSun ();
    TimerMode = EEPROM.read (0); // Read TimerMode from EEPROM
    delay (1000);
}
           
void printTemperature(DeviceAddress deviceAddress)
{
  lcd.setCursor (9,1);
  tempC = sensors.getTempC(deviceAddress);
  if (tempC == -127.00) {
    lcd.print("Err");
  } else {
    
    lcd.print(tempC);
    lcd.print((char)0xDF);
    lcd.print("C ");
   
  }
}


void loop () {
    

  
    DateTime now = RTC.now(); //get time from RTC
    //Display current time
    lcd.setCursor (0,0);
    lcd.print(now.day(), DEC);
    lcd.print('/');
    lcd.print(now.month());
    lcd.print('/');
    lcd.print(now.year(), DEC);
    lcd.print(" ");
    lcd.setCursor (0,1);
    lcd.print(now.hour(), DEC);
    lcd.print(':');
    if (now.minute() <10) 
      {
        lcd.print("0");
      }
    lcd.print(now.minute(), DEC);
    lcd.print(':');
    if (now.second() <10) 
      {
        lcd.print("0");
      }
    lcd.print(now.second());
    lcd.print(" ");
    
    //current time in minutes since midnight (used to check against sunrise/sunset easily)
    TimeMins = (now.hour() * 60) + now.minute();
   
    
    //Get and display temp every minute
    if (now.second() == 7) {
      sensors.requestTemperatures(); // Request temperature
      delay (249);
      printTemperature(outsideThermometer); // display on lcd.
     
    }
    
    // Calculate sun times once a day at a minute past midnight
    if (TimeMins == 1) {
      lcd.clear();
      CalcSun ();
    }
    
    // Reset Override flag if the sun is up!
    if (TimeMins == Sunrise) {
      OverRideFlag = false;
    }
         
    
    if (TimerMode ==2) {
      if (TimeMins >= Sunrise && TimeMins <=Sunset-1 && tempC>=2 && OverRideFlag==false) { //If it's after sunrise and before sunset, and it's not frozen, and our override isn't set, switch our relay on
          digitalWrite (A3, LOW);
          lcd.setCursor (13,0);
          lcd.print ("On ");
        }
        else {  //otherwise switch it off
          digitalWrite (A3, HIGH);
          lcd.setCursor (13,0);
          lcd.print ("Off");
        }
      }
       if (TimerMode ==3) {
      if (TimeMins >= Sunrise && TimeMins <=Sunset+60 && tempC>=2 && OverRideFlag==false) { //If it's after sunrise and before sunset + 1 hour, and it's not frozen, and our override isn't set, switch our relay on
          digitalWrite (A3, LOW);
          lcd.setCursor (13,0);
          lcd.print ("On ");
        }
        else {  //otherwise switch it off
          digitalWrite (A3, HIGH);
          lcd.setCursor (13,0);
          lcd.print ("Off");
        }
      }
       if (TimerMode ==0) { // Off
         digitalWrite (A3, HIGH);
         lcd.setCursor (13,0);
         lcd.print ("Off");
       }
     
       if (TimerMode ==1 && tempC>=2) { // TimerMode is On, but it's not frozen
         digitalWrite (A3, LOW);
         lcd.setCursor (13,0);
         lcd.print ("On ");
       }
       
       buttonstate = digitalRead(A1);// check to see if our override button is pressed, if it is, set the flag
       if (buttonstate == LOW ) {
         OverRideFlag=true;
       }        

    
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    delay (10);
    
    if (pushlength <pushlengthset) {
      lcd.clear ();
      ShortPush ();   
      lcd.clear ();
    }
    
       
       //This runs the setclock routine if the knob is pushed for a long time
       if (pushlength >pushlengthset) {
         lcd.clear();
         DateTime now = RTC.now();
         setyeartemp=now.year(),DEC;
         setmonthtemp=now.month(),DEC;
         setdaytemp=now.day(),DEC;
         sethourstemp=now.hour(),DEC;
         setminstemp=now.minute(),DEC;
         setclock();
         pushlength = pushlengthset;
       };
}

//sets the clock
void setclock (){
   setyear ();
   lcd.clear ();
   setmonth ();
   lcd.clear ();
   setday ();
   lcd.clear ();
   sethours ();
   lcd.clear ();
   setmins ();
   lcd.clear();
   RTC.adjust(DateTime(setyeartemp,setmonthtemp,setdaytemp,sethourstemp,setminstemp,setsecs)); //set the DS1307 RTC
   CalcSun (); //refresh the sunrise and sunset times
   delay (1000);
   
}

// subroutine to return the length of the button push.
int getpushlength () {
  buttonstate = digitalRead(A0);  
       if(buttonstate == LOW && buttonflag==false) {     
              pushstart = millis();
              buttonflag = true;
          };
          
       if (buttonstate == HIGH && buttonflag==true) {
         pushstop = millis ();
         pushlength = pushstop - pushstart;
         buttonflag = false;
       };
       return pushlength;
}
// The following subroutines set the individual clock parameters
int setyear () {
   lcd.setCursor (0,0);
    lcd.print ("Set Year");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      return setyeartemp;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) { //bit of software de-bounce
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    setyeartemp=setyeartemp + knobval;
    if (setyeartemp < 2014) { //Year can't be older than currently, it's not a time machine.
      setyeartemp = 2014;
    }
    lcd.print (setyeartemp);
    lcd.print("  "); 
    setyear();
}
  
int setmonth () {

   lcd.setCursor (0,0);
    lcd.print ("Set Month");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      return setmonthtemp;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) {
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    setmonthtemp=setmonthtemp + knobval;
    if (setmonthtemp < 1) {// month must be between 1 and 12
      setmonthtemp = 1;
    }
    if (setmonthtemp > 12) {
      setmonthtemp=12;
    }
    lcd.print (setmonthtemp);
    lcd.print("  "); 
    setmonth();
}

int setday () {
  if (setmonthtemp == 4 || setmonthtemp == 5 || setmonthtemp == 9 || setmonthtemp == 11) { //30 days hath September, April June and November
    maxday = 30;
  }
  else {
  maxday = 31; //... all the others have 31
  }
  if (setmonthtemp ==2 && setyeartemp % 4 ==0) { //... Except February alone, and that has 28 days clear, and 29 in a leap year.
    maxday = 29;
  }
  if (setmonthtemp ==2 && setyeartemp % 4 !=0) {
    maxday = 28;
  }
  
   lcd.setCursor (0,0);
    lcd.print ("Set Day");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      return setdaytemp;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) {
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    setdaytemp=setdaytemp+ knobval;
    if (setdaytemp < 1) {
      setdaytemp = 1;
    }
    if (setdaytemp > maxday) {
      setdaytemp = maxday;
    }
    lcd.print (setdaytemp);
    lcd.print("  "); 
    setday();
}

int sethours () {
    lcd.setCursor (0,0);
    lcd.print ("Set Hours");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      return sethourstemp;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) {
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    sethourstemp=sethourstemp + knobval;
    if (sethourstemp < 1) {
      sethourstemp = 1;
    }
    if (sethourstemp > 23) {
      sethourstemp=23;
    }
    lcd.print (sethourstemp);
    lcd.print("  "); 
    sethours();
}

int setmins () {

   lcd.setCursor (0,0);
    lcd.print ("Set Mins");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      return setminstemp;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) {
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    setminstemp=setminstemp + knobval;
    if (setminstemp < 0) {
      setminstemp = 0;
    }
    if (setminstemp > 59) {
      setminstemp=59;
    }
    lcd.print (setminstemp);
    lcd.print("  "); 
    setmins();
}

int setmode () { //Sets the mode of the timer. Auto, On or Off

    lcd.setCursor (0,0);
    lcd.print ("Set Mode");
    pushlength = pushlengthset;
    pushlength = getpushlength ();
    if (pushlength != pushlengthset) {
      EEPROM.update (0,TimerMode); //write the mode setting to EEPROM
      return TimerMode;
    }

    lcd.setCursor (0,1);
    knob.write(0);
    delay (50);
    knobval=knob.read();
    if (knobval < -1) {
      knobval = -1;
    }
    if (knobval > 1) {
      knobval = 1;
    }
    TimerMode=TimerMode + knobval;
    if (TimerMode < 0) {
      TimerMode = 0;
    }
    if (TimerMode > 3) {
      TimerMode=3;
    }
    if (TimerMode == 0) {
    lcd.print("Off        ");
    lcd.print("  "); 
    }
    if (TimerMode == 1) {
    lcd.print("On         ");
    lcd.print(" "); 
    }
    if (TimerMode == 2) {
    lcd.print("Auto       ");
    lcd.print("  "); 
    }
    if (TimerMode == 3) {
    lcd.print("Auto Extend");
    lcd.print("  "); 
    }
    setmode ();
}

int CalcSun () { //Calculates the Sunrise and Sunset times
    DateTime now = RTC.now();
    sunTime[3] = now.day(); // Give Timelord the current date
    sunTime[4] = now.month();
    sunTime[5] = now.year();
    myLord.SunRise(sunTime); // Computes Sun Rise.
    Sunrise = sunTime[2] * 60 + sunTime[1]; // Sunrise returned in minutes past midnight
    SunriseHour = sunTime[2];
    SunriseMin = sunTime [1];
    sunTime[3] = now.day(); // Uses the Time library to give Timelord the current date
    sunTime[4] = now.month();
    sunTime[5] = now.year();
    myLord.SunSet(sunTime); // Computes Sun Set.
    Sunset = sunTime[2] * 60 + sunTime[1]; // Sunset returned in minutes past midnight
    SunsetHour = sunTime[2];
    SunsetMin = sunTime [1];
}

void ShortPush () {
  //This displays the calculated sunrise and sunset times when the knob is pushed for a short time.
for (long Counter = 0; Counter < 604 ; Counter ++) { //returns to the main loop if it's been run 604 times 
                                                     //(don't ask me why I've set 604,it seemed like a good number)
  lcd.setCursor (0,0);
  lcd.print ("Sunrise ");
  lcd.print (SunriseHour);
  lcd.print (":");
  if (SunriseMin <10) 
     {
     lcd.print("0");
     }
  lcd.print (SunriseMin);
  lcd.setCursor (0,1);
  lcd.print ("Sunset ");
  lcd.print (SunsetHour);
  lcd.print (":"); 
    if (SunsetMin <10) 
     {
     lcd.print("0");
     }
  lcd.print (SunsetMin);        

    
  //If the knob is pushed again, enter the mode set menu
  pushlength = pushlengthset;
  pushlength = getpushlength ();
  if (pushlength != pushlengthset) {
    lcd.clear ();
    TimerMode = setmode ();

  }
  
}



}
  
 

