IPAddress ip_pvoutput;
int DnsStatus;

#define RESPONSE_LEN 80

// To store "(%5d ms)"
#define RESPONSE_OK_TIME 10

char pvResponseFail[RESPONSE_LEN] = "''";
time_t pvResponseFailTime = 0;
char pvResponseOk[RESPONSE_LEN + RESPONSE_OK_TIME] = "''";
time_t pvResponseOkTime = 0;
float previous = -1;


// This function will contact the DNS server and ask for an IP address of PvOutput
// If successfull, this address will be used
// If not, keep using the previous found address
// In this way, we can still update to pvoutput if the dns timeouts.
void CheckIpPv()
{
  // Look up the host first
  DNSClient dns;
  IPAddress remote_addr;

  dns.begin(Ethernet.dnsServerIP());
  DnsStatus = dns.getHostByName((char*)"pvoutput.org", remote_addr);
  if (DnsStatus == 1)  ip_pvoutput = remote_addr; // if success, copy
}

// This function updates all registered sensors to pvoutput
// The sensors are listed in the 'S' array
void SendToPvOutput(BaseSensor** S)
{
  EthernetClient pvout;
  // create a total for each variable that can be used in pvoutput
  // !! The index in this array starts at 0 while the pvoutput vars start at 1
  float v[12]; // data sum
  bool b[12]; // data present flags
  // start with 0
  for(byte n = 0; n < 12; n++)
  {
    v[n] = 0;
    b[n] = false;
  }

  CheckIpPv(); // update the ipaddress via DNS
  busy(0); // Strobe watchdog

  unsigned int sid = S[0]->SID;

  for(byte i = 0; i<NUMSENSORS; i++) // scan through the sensor array
  {
    byte type = S[i]->Type;
    float actual = (float)S[i]->Actual / S[i]->Factor;
    float peak = (float)S[i]->Peak / S[i]->Factor;
    float today = (float)S[i]->Today / S[i]->Factor;

    switch(type)
    {
      // temperature
      case 5:   v[type-1] += actual;
                b[type-1] = true;
                break;
      //voltage
      case 6:   v[type-1] += today;
                b[type-1] = true;
                break;
      //ferraris or P1
      case 24:  // total consumption is production + net consumption
                v[2] = v[0] + today;

                if(v[1] == 0) // no production, use data from type 24 directly
                {
                    v[3] = actual;
                }
                else
                {
                    // actual power is energy since previous upload divided by number of uploads per hour
                    // using this method because actual values of production and consumption sensors have different sampling rates, causing actual to be unreliable.
                    if(previous >=0 && previous < v[2])
                    {
                      v[3] = (v[2] - previous) * 60 / UPDATEINTERVAL;
                    }
                }
                previous = v[2];
                b[2] = true;
                b[3] = true;
                break;
      // other sensors (including type 0). Log Peak and total
      default:  v[type-1] += peak;
                v[type-2] += today;
                b[type-1] = true;
                b[type-2] = true;
    }

    if(i == NUMSENSORS-1 || S[i+1]->SID != sid)
    {
      if(sid > 0) // only upload if the sid is valid
      {
        int res = 0;
        int retries = 5;
        while (retries-- > 0 && res != 1)
        {
          res = pvout.connect(ip_pvoutput,80);
          busy(0); // Strobe watchdog
          if (retries > 0 && res != 1) delay(561);  // Arbitrary delay
        }

        static int noConnections = 0;

        if(res == 1) // connection successfull
        {
          noConnections = 0;

          pvout << F("GET /service/r2/addstatus.jsp");
          pvout << F("?key=" PVOUTPUT_API_KEY);
          pvout << F("&sid=") << sid;
          sprintf_P(webData, PSTR("&d=%04d%02d%02d"), year(),month(),day());
          pvout << webData;
          sprintf_P(webData, PSTR("&t=%02d:%02d"), hour(),minute());
          pvout << webData;
          for(byte i = 0; i < 12; i++)
          {
            #ifdef GRAADDAGEN
              // replace voltage(v6) by factor
              if(i==5)
              {
                pvout << "&v6=" << T1.GetFactor(G1.Today,hour());
              }
              else
            #endif
            if(b[i])  // only send data if present
            {
              pvout << "&v" << i+1 << "=" << v[i];
            }
          }
          pvout << F(" HTTP/1.1") << endl;
          pvout << F("Host: pvoutput.org") << endl << endl;

          retries = 50; // Wait maximally 50 * 0.05 = 2.5 seconds, currently (6-2017) usually takes 0.15 seconds
          while(retries-- > 0 && pvout.connected() && !pvout.available()) delay(50); // Wait for data
          busy(0); // Strobe watchdog
          char pvResponse[RESPONSE_LEN];
          pvResponse[0] = 0;
          if (pvout.connected() || pvout.available())
          {
            pvResponse[0] = '\'';
            size_t n = pvout.readBytes(pvResponse + 1, RESPONSE_LEN-2-1); // -2: start and end quote; -1: terminating 0
            pvResponse[n+1] = '\'';
            pvResponse[n+2] = 0; // terminate the string
          } // if
          if (strstr(pvResponse, "200 OK") != 0)
          {
            // "200 OK" found in response
            strncpy(pvResponseOk, pvResponse, RESPONSE_LEN - 1);
            pvResponseOk[RESPONSE_LEN - 1] = 0;
            size_t len = strlen(pvResponseOk);
            sprintf(pvResponseOk + len, "(%5d ms)",50*(50 - retries - 1)); // Append delay between post and response
            pvResponseOkTime = now();
          }
          else
          {
            // "200 OK" not found in response
            strncpy(pvResponseFail, pvResponse, RESPONSE_LEN - 1);
            pvResponseFail[RESPONSE_LEN - 1] = 0;
            pvResponseFailTime = now();
          } // if
          pvout.stop();

        }
        else // cannnot connect
        {
          strcpy_P(pvResponseFail, PSTR("No connection"));
          pvResponseFailTime = now();

          // From experience, the connectivity is lost after about 50-60 days of uptime. No idea why. A workaround
          // is to reboot. After 6 subsequent times no connection (1 hour), try that.
          noConnections++;
          if (noConnections > 6)
          {
            busy(241);  // WD_FORCED_RESET; Indicate forced watchdog reset
            while(1);  // Stay here until the watchdog barks
          } // if
        }
      }
      // reset the counters for the next round
      for(byte n = 0; n < 12; n++)
      {
        v[n] = 0;
        b[n] = false;
      }
      if(i < NUMSENSORS) sid = S[i+1]->SID;
    }
  }
}
