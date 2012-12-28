//*****************************************************************
//  userdefines  

#ifndef userdefs
#define userdefs

//*****************************************************************
// If you want the logging data to be written to the SD card, remove // from the next line:
//#define USE_LOGGING

// Mail variables. Uncomment the next line and a mail will be sent once a day
#define USE_MAIL
#define MAIL_TIME 21 // The default time to mail is 21:00 h
#define MAIL_TO "harold65@gmail.com" // fill in the destination mail address
#define MAIL_FROM "arduino@meterkast.nl" // any valid mail address will do here
#define MAIL_SERVER "smtp.upcmail.nl" // use the server address of your own provider

//*****************************************************************
// Network variables
static byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // MAC address can be any number, as long as it is unique in your local network
static byte ip[] = { 192, 168, 1, 99 }; // IP of arduino
static byte dnsserver[] = {192,168,1,1};    // use the address of your gateway { 192, 168, 1, 1 } if your router supports this
                                            // or use the address of the dns server of your internet provider
                                            // or use { 8, 8, 8, 8 } as general DNS server from Google if you have no other option

//*****************************************************************
// You can find your api-key in the PvOutput settings page, under api-settings
#define PVOUTPUT_API_KEY "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

//*****************************************************************
// Sensor configuration
//*****************************************************************
// NUMSENSORS must match the number of sensors defined.
#define NUMSENSORS 4

// S0 sensors have 4 parameters: 
//   1: The digital pin to which they are connected.
//   2: The number of pulses for 1 kWh
//   3: The System ID of the corresponding pvOutput graph
//   4: The number of the variable to log to (see end of file for allowed numbers)
S0Sensor  S1(2,1000,2222,2);   // S0 sensor connected to pin 2, logging to variable 2 (production) of sid 2222
//S0Sensor  S2(3,2000,2222,2);   // S0 sensor connected to pin 3, logging to variable 2 (production) of sid 2222. This will be added to S1
//S0Sensor  S3(4,1000,3333,4);   // S0 sensor connected to pin 4, logging to variable 4 (consumption) of sid 3333

// Analog Sensors have 4 parameters: 
//   1: The analog pin to which they are connected
//   2: The number of pulses for unit
//   3: The SID
//   4: The number of the variable to log to (see end of file for allowed numbers)
AnalogSensor G1(A2,100,2222,6);    // gas sensor connected to analog 2, measuring 100 pulses per m3, showing on SID 2812 variable 6 (voltage)

// Graaddagen 'sensor' will calculate the gas/graaddag factor. 
// Parameters:
//   1: The number of the weatherstation to get the temperature from
//      Find the nearest weatherstation on:  http://gratisweerdata.buienradar.nl/#Station
//   2: The SID
Temperature T1("6275",2222);
//*****************************************************************
// if you want to log the gas per 'graaddag' in stead of the temperature, enable the next line
//#define GRAADDAGEN
//*****************************************************************

// Ferrarissensors have 4 parameters: 
//   1: The analog input of the right sensor
//   2: The analog input of the left sensor
//   3: The number of revolutions of the disc for 1kWh
//   4: The SID
//   This sensor always logs to variable 3 and 4
FerrarisSensor F1(A3,A4,250,2222);

// the next list must be in the correct order and have the same length as NUMSENSORS
BaseSensor* sensors[] = {&S1,&F1,&G1,&T1};

#endif
