#define VERSION "V11.43"

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Dns.h>
#include <TimeLib.h>
#include <MsTimer2.h>
#include <avr/wdt.h>
#include <utility/w5100.h>

#include "FlashMini.h"
#include "S0Sensor.h"
#include "P1GasSensor.h"
#include "P1Power.h"
#include "AnalogSensor.h"
#include "FerrarisSensor.h"
#include "userdefs.h"

//#include <SD.h>

// global variables
byte   lastDayReset;
byte   lastHour;
byte   lastMinute;
unsigned long upTime;  // the amount of hours the Arduino is running
EthernetServer server(555);  // port changed from 80 to 555
EthernetUDP Udp;
char   webData[14];
#ifdef USE_LOGGING
  File   logFile;
#endif
#define EE_RESETDAY 1

void UpdateTime(int attempts = 10);

void setup()
{
    // wait for the ethernet shield to wakeup
    delay(300);
    // initialize network
    Ethernet.begin(mac, ip, dnsserver, gateway, subnet);

    // Set connect timeout parameters

    // Units are 0.1 ms, i.e. 1000 will give 100ms for the initial attempt
    W5100.setRetransmissionTime(1000);

    // Timeout seems to double per retry up to a maximum of 64000 units, so by specifying 8 it will take:
    // 2000 + 4000 + 8000 + 16000 + 32000 + 64000 + 64000 + 64000 + 64000 = 318000 = 31,8 seconds(!)
    // See also: https://forum.arduino.cc/index.php?topic=430605.0
    W5100.setRetransmissionCount(4);

    setupNtp();

    // Do a quick attempt in retrieving the time via NTP. Try only once otherwise setup can take really long.
    UpdateTime(1);

    #ifdef USE_LOGGING
        // initialize SD card
        SetupSD();
        OpenLogFile();
    #endif
    // start listening
    server.begin();

    // initialize the sensors
    for(byte i = 0; i < NUMSENSORS; i++)
    {
        sensors[i]->Begin(i);
    }
    // set a random seed
    randomSeed(analogRead(0));

    // restore the last day on which the counters were reset
    lastDayReset = eeprom_read_byte((uint8_t*) EE_RESETDAY);
    // if the eeprom contains illegal data, set it to a useful value
    if(lastDayReset == 0 || lastDayReset > 31) lastDayReset = day();
    lastMinute = minute();
    lastHour = hour();
    upTime = 0;

    #ifdef USE_WD
      SetupWatchdog();
    #endif
    // start the timer interrupt
    MsTimer2::set(5, Every5ms); // 5ms period
    MsTimer2::start();
}

// check and update all counters every 5ms.
void Every5ms()
{
    for(byte i = 0; i < NUMSENSORS; i++)
    {
        sensors[i]->CheckSensor();
    }
    #ifdef USE_WD
      CheckWatchdog();
    #endif
}

// Time
extern time_t lastNtpUpdate;

void loop()
{
    // get the actual time
    time_t t = now();

    // if we know the time, reset counters when todays day is different from the last day the counters were reset
    if (isTimeValid(t))
    {
        byte iDay = day(t); // 1..31
        if (iDay != lastDayReset)
        {
            busy(1);
            #ifdef USE_MINDERGAS
                // Calculate the new gas metervalue and start the countdown
                UpdateGas();
            #endif
            for(byte i = 0; i < NUMSENSORS; i++)
            {
                sensors[i]->Reset();
            }
            #ifdef USE_LOGGING
                // create new logfile
                CloseLogFile();
                OpenLogFile();
            #endif
            lastDayReset = iDay;
            // store today as the date of the last counter reset
            eeprom_write_byte((uint8_t*) EE_RESETDAY, lastDayReset);
        }
    } // if

    // Second has changed
    static int lastSecond = -1;
    byte iSecond = second(t); // 0..59
    if (iSecond != lastSecond)
    {
        lastSecond = iSecond;

        // Half way every minute, slew time. Note that it is safe to call slewTime() multiple times within the same minute
        // (i.e. when slewTime() sets the time backwards by 1 second).
        if (iSecond == 30) slewTime();
    } // if

    // Minute has changed
    byte iMinute = minute(t); // 0..59
    if(iMinute != lastMinute)
    {
        busy(3);

        lastMinute = iMinute;

        // Keep on trying to retrieve NTP time as long as we don't know the time.
        // In a situation of power line restore, the uplink (home router) might still be rebooting when the
        // call to UpdateTime() is made within setup() above.
        if (! isTimeValid (now())) UpdateTime(1);

        for(byte i = 0; i < NUMSENSORS; i++)
        {
            sensors[i]->CalculateActuals();
        }
        busy(31);

        #ifdef USE_MINDERGAS
            // this function will not do anything until the countdown timer is finished
            SendToMinderGas();
        #endif

        #ifdef USE_LOGGING
            WriteDateToLog();
            for(byte i = 0; i < NUMSENSORS; i++)
            {
                //sensors[i]->Status(&logFile);
                logFile << sensors[i]->Today << ";" << sensors[i]->Actual << ";" << endl;
            }
            logFile << endl;
            logFile.flush();
        #endif
        busy(32);
        // update every 5 minutes or whatever is set in userdefs
        if((lastMinute%UPDATEINTERVAL) == 0)
        {
            SendToPvOutput(sensors);
            busy(33);
            // reset the maximum for pvoutput
            for(byte i = 0; i < NUMSENSORS; i++)
            {
                sensors[i]->ResetPeak();
            }
        }
        busy(34);
        #ifdef EXOSITE_KEY
          if((lastMinute%EXOSITEUPDATEINTERVAL) == 0)
          {
            SendToExosite();
          }
        #endif
    }

    // Hour has changed
    byte iHour = hour(t); // 0..23
    if (iHour != lastHour)
    {
        busy(2);

        lastHour = iHour;

        time_t dT = t - lastNtpUpdate;

        // Skip if last NTP update was recent.
        // Notes:
        // - If UpdateTime() adjusts the time, the hour may jump, but t will be equal to lastNtpUpdate.
        // - Assuming UpdateTime() does not set back the time by more than 5 minutes.
        if (dT > 5 * SECS_PER_MIN)
        {
            upTime++;

            // Save the daily values every hour. Do this first, before calling UpdateTime() which may take several
            // minutes (worst case)
            for(byte i = 0; i < NUMSENSORS; i++)
            {
                sensors[i]->Save();
            }
            #ifdef USE_MAIL
                if(lastHour == MAIL_TIME)
                {
                    SendMail();
                }
            #endif

            // Sync the time. Preferrably do this at a time that there is no sun shining, since a
            // time shift can distort the "average" output of pvoutput: if the current period becomes (let's say)
            // 12 minutes instead of 10 due to a 2 minute clock backwards setting, the average may be above the peak
            // value, that is very ugly.

            // Daily @ 2:00 am, or if the last update was 25 hours ago or more
            if (lastHour == 2 || dT >= 25 * SECS_PER_HOUR)
            {
                UpdateTime();
            }
        } // if
    }

    busy(4);
    // let all sensors do other stuff
    for(byte i = 0; i < NUMSENSORS; i++)
    {
      sensors[i]->Loop(lastMinute);
    }

    busy(5);
    // see if there are clients to serve
    ServeWebClients();

    busy(0);

    // give the ethernet shield some time to rest
    delay(50);
}
