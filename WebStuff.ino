String inString = String((char*)"");

void ServeWebClients()
{
    EthernetClient client = server.available();
    if (client)
    {
        inString = client.readStringUntil('\n');
        client << F("HTTP/1.1 200 OK") << endl;
        client << F("Content-Type: text/html") << endl;
        client << F("Cache-Control: no-store") << endl;
        client << F("Connection: close") << endl << endl;
        if (Command((char*)"save")) SaveValues();
        #ifdef USE_MINDERGAS
          if (Command((char*)"gas")) GasCountdown = 1;
        #endif
        if (Command((char*)"reset")) ResetValues();
        if (Command((char*)"restart"))
        {
            busy(242);  // WD_FORCED_RESET; Indicate forced watchdog reset
            while(1);  // Stay here until the watchdog barks
        }
        if (Command((char*)"ntp")) UpdateTime(1); // reload the ntp time, try only once
        if (Command((char*)"upload")) SendToPvOutput(sensors); // upload to pvoutput now
        int i=inString.indexOf("?");
        if(i!=-1) ReadValue(inString,i);
        ShowStatus(client);
        client.stop();
    }
}

bool Command(char* command)
{
  int i = inString.indexOf(command);
  if (i==-1) return false;
  return true;
}

void ReadValue(String input,int i)
{
    // string format is "?0=12345"
    //                  "?3=-12345"
    //                  "?G=12345"
    long val = 0;
    bool neg = false;
    int j = i + 3;
    int address = input[i+1];
    if((address >= '0' && address < '9') || (address >= 'A' && address < 'J'))
    {
        char c = input[j];
        if(c == '-')
        {
            neg = true;
            j ++;
            c = input[j];
        }
        while(c >= '0' && c <= '9')
        {
            val = 10 * val + (c - '0');
            j ++;
            c = input[j];
        }
        if(neg) val = -val;
        if(address >= 'A') // address = 'A...'J' to write the total counters
        {
            sensors[address-'A']->NewTotal(val);
        }
        else // address = '0'...'9' to write the day counters
        {
            // let the sensor write the value to eeprom
            sensors[address-'0']->Update(val);
        }
    }
}

void SaveValues()
{
    for(byte i = 0; i < NUMSENSORS; i++)
    {
        sensors[i]->Save();
    }
}

void ResetValues()
{
    for(byte i = 0; i < NUMSENSORS; i++)
    {
        sensors[i]->Reset();
    }
}

void ShowStatus(EthernetClient client)
{
    const char* br = "<br>";
    client << F("<html><style>td,th {padding:8;text-align:center;}</style>");
    client << F(VERSION) << br;
    client << DateTime(now()) << br;
    client << F("Uptime=") << upTime/24 << "d+" << upTime%24 << "h" << br;
    client << F("<table border=\"1\" cellspacing=\"0\">");
    client << F("<tr><th>ID<th>SID<th>Type<th>Actual<th>Peak<th>Today<th>Total<th>Factor<th>TodayCnt<th>EEprom<th>ppu<th>Pulse<th>Extra</tr>");

    for(int i=0;i<NUMSENSORS;i++)
    {
        sensors[i]->CalculateActuals();
        client << F("<tr><td>") << i;
        sensors[i]->Status(client);
        client << F("</tr>");
    }
    client << F("</table>last PvOutput fail=") << pvResponseFail << " @ " << DateTime(pvResponseFailTime) << br;
    client << F("last PvOutput ok=") << pvResponseOk << " @ " << DateTime(pvResponseOkTime) << br;
    client << F("DNS status=") << DnsStatus << br;
    client << F("Last NTP update=") << DateTime(lastNtpUpdate) << " (in " << ntpRetry << "x)" <<  br;
    client << F("timeUpdateInterval=") << timeUpdateInterval << " seconds" << br;
    client << F("timeDiff=");
    if (timeDiff == LONG_MAX ) client << F("[unknown]"); else client << timeDiff << " seconds";
    client << br;
    client << F("clockDrift=") << clockDrift << " seconds" << br;
    client << F("slewed=") << slewed << " seconds" << br;
    #ifdef USE_MINDERGAS
    client << F("mgUpload=") << DateTime(mgUploadTime) << br;
    client << F("MgResponse=") << mgResponse << br;
    #endif
    #ifdef EXOSITE_KEY
    client << F("ExResponse=") << exResponse << br;
    #endif
    client << F("WD ctr=") << eeprom_read_byte ((uint8_t*)EE_CTR) << br;
    client << F("WD val=") << eeprom_read_byte ((uint8_t*)EE_STATE) << br;
    client << F("Reset Day=") << eeprom_read_byte ((uint8_t*)EE_RESETDAY) << br;
    client << F("Free=") << freeRam() << br;
}

int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
