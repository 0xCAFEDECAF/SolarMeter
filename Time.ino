
# include <limits.h>

time_t lastNtpUpdate = 0;
byte ntpRetry;

// NTP time stamp is in the first 48 bytes of the message
#define NTP_PACKET_SIZE (48)
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// clockDrift is stored in at this address as word (2 bytes)
#define EE_CLOCK_DRIFT 2

// timeUpdateInterval is stored in at this address as long (4 bytes)
#define EE_UPDATE_INTERVAL 4

unsigned long timeUpdateInterval = 0; // in seconds
long clockDrift = 0; // number of seconds that the internal clock deviated within the last timeUpdateInterval
long timeDiff = LONG_MAX; // number of seconds that the slewed clock deviated within the last timeUpdateInterval
long slewed = 0; // in seconds
static time_t _lastSlew = 0;
static long _D = 0; // Accumulated difference

// Send an NTP request to the time server at the given address
void sendNTPpacket()
{
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    Udp.beginPacket(NTP_SERVER, 123); //NTP requests are to port 123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
} // sendNTPpacket

// Check if an epoch value is valid
inline boolean isTimeValid(time_t t)
{
    return (t >= 1451606400UL); // 2016-01-01 0:00:00
} // isTimeValid

boolean DaylightSavingTime = false;

// Return offset from GMT in seconds
inline int timeZoneOffset()
{
    static const int PROGMEM TIMEZONE = 1; // CET
    return (TIMEZONE + (DaylightSavingTime ? 1 : 0)) * SECS_PER_HOUR;
} // timeZoneOffset

inline const char* timeZoneStr()
{
    return DaylightSavingTime ? "CEST" : "CET";
} // timeZoneStr

// Return the time as Epoch (number of seconds since 1-1-1970 GMT)
// Adds either 3600 (winter) for CET or 7200 (summer) for CEST
// Adds also TIME_OFFSET (see userdefs.h)
unsigned long getNtpTime()
{
    // Make sure udp buffer is empty
    while (Udp.parsePacket()) Udp.flush();

    sendNTPpacket(); // Send an NTP packet to a time server

    int retries = 20; // Wait maximally 20 * 0.05 = 1 second
    while (retries-- > 0 && ! Udp.parsePacket()) delay(50); // Wait for data
    if (retries >= 0)
    {
        Udp.read(packetBuffer, NTP_PACKET_SIZE);  // Read the packet into the buffer

        // The time stamp starts at byte 40 of the received packet and is four bytes,
        // or two words, long. First, extract the two words:
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

        // Check for invalid response
        if(highWord == 0 || lowWord == 0) return 0;

        // Combine the four bytes (two words) into a long integer;
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        unsigned long now = secsSince1900 - 2208988800UL;  // GMT

        // DST == DaySavingTime == Zomertijd
        int m = month(now);
        int previousSunday = day(now) - weekday(now) + 1;  // Add one since weekday starts at 1
        DaylightSavingTime = (m >= 3 && m <= 10); // Between october and march
        if (DaylightSavingTime)
        {
            if (m == 3)
            {
                // Starts last sunday of march
                DaylightSavingTime = previousSunday >= 25;
            }
            else if (m == 10)
            {
                // Ends last sunday of october
                DaylightSavingTime = previousSunday < 25;
            } // if
        } // if
        now += timeZoneOffset();
        now += TIME_OFFSET;
        Udp.flush();
        return now;
    } // if
    return 0; // Return 0 if unable to get the time
} // getNtpTime

static char _dt[20];
const char* DateTime(time_t t)
{
    if (! isTimeValid(t)) return (const char*)"Never";
    sprintf(_dt, "%02d-%02d-%04d %02d:%02d:%02d", day(t), month(t), year(t), hour(t), minute(t), second(t));
    return _dt;
} // DateTime

void UpdateTime(int attempts)
{
    unsigned long retrieved = 0;

    // Try to retrieve the time 10 times
    int i;
    for(i = 1; i <= attempts; i++)
    {
        Udp.begin(8888);
        retrieved = getNtpTime();
        Udp.stop();
        if (retrieved > 0) break;
    } // for

    time_t local = now();

    if (retrieved == 0)
    {
        // Could not get a valid time

        // If the last update was more than 36 hours ago, then force-reset
        if (lastNtpUpdate > 0 && now() - lastNtpUpdate > 36 * SECS_PER_HOUR)
        {
            busy(240);  // WD_FORCED_RESET; Indicate forced watchdog reset
            while(1);  // Stay here until the watchdog barks
        } // if

        return;
    } // if

    // Got a valid time

    setTime(retrieved); // Do this ASAP

    // Don't cast retrieved and local to long, it will not work beyond epoch 2147483647 (19 Jan 2038)
    timeDiff = retrieved - local;

    // Keep the slew algorithm up-to-date
    _lastSlew += timeDiff;

    // We need at least an initial update
    static time_t lastClockDriftUpdate = 0;
    if (lastClockDriftUpdate <= 0) lastClockDriftUpdate = retrieved;

    // Some significant time (30 minutes) needs to pass before clockDrift can be estimated
    #define CLOCK_DRIFT_ESTIMATION_AFTER (30 * SECS_PER_MIN)
    if (retrieved - lastClockDriftUpdate >= CLOCK_DRIFT_ESTIMATION_AFTER)
    {
        unsigned long oldTimeUpdateInterval = timeUpdateInterval;
        long oldClockDrift = clockDrift;

        // Update timeUpdateInterval and corresponding clockDrift

        timeUpdateInterval = retrieved - lastClockDriftUpdate; // Will be at least CLOCK_DRIFT_ESTIMATION_AFTER

        // Start with the number of seconds that the time was actually corrected by during the last update interval
        clockDrift = slewed;

        // Adjust drift, only when useful
        if (abs(timeDiff) > 1 && abs(timeDiff) < 10 * SECS_PER_MIN) clockDrift += timeDiff;

        _D = -timeUpdateInterval; // Initial value for _D is -dx
        slewed = 0; // Reset correction seconds counter

        lastClockDriftUpdate = retrieved;

        // Save update interval
        if (timeUpdateInterval != oldTimeUpdateInterval)
        {
            eeprom_update_dword((uint32_t*)EE_UPDATE_INTERVAL, (uint32_t)timeUpdateInterval);
        } // if

        // Save clock drift
        if (clockDrift != oldClockDrift)
        {
            eeprom_update_word((uint16_t*)EE_CLOCK_DRIFT, (uint16_t)(clockDrift + SHRT_MAX));
        } // if
    } // if

    lastNtpUpdate = retrieved;
    ntpRetry = i;
} // UpdateTime

// Adjust time by at most 1 second forward or backward. Call as often as you like, but better not just before or after
// the whole minute; you might get the transition to the next minute twice.
void slewTime()
{
    time_t t = now();

    // Distribute the adjustment seconds evenly over the interval over which the clock drift was determined. Using a
    // slightly modified form of Bresenham's line algorithm; see:
    // https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm

    // _D = _D + 2*dy
    _D += 2 * (t - _lastSlew) * abs(clockDrift);

    if (_D > 0)
    {
        if (clockDrift)
        {
            // Adjust by 1 second forward or backward
            long adjustment = (clockDrift > 0 ? 1 : -1);
            adjustTime(adjustment);
            slewed += adjustment;
        } // if

        // _D = _D - 2*dx
        _D -= 2 * timeUpdateInterval;
    } // if

    _lastSlew = t;
} // slewTime

void setupNtp()
{
    unsigned long _timeUpdateInterval = eeprom_read_dword((uint32_t *)EE_UPDATE_INTERVAL);

    // Between 10 minutes and 72 hours?
    if (_timeUpdateInterval > 10 * SECS_PER_MIN && _timeUpdateInterval < 72 * SECS_PER_HOUR)
    {
        timeUpdateInterval = _timeUpdateInterval;
    } // if

    if (timeUpdateInterval > 10 * SECS_PER_MIN && _timeUpdateInterval < 72 * SECS_PER_HOUR)
    {
        // Don't do a normal cast; the default value in EEPROM when never written (0xFFFF) will become -1
        long _clockDrift = (long)eeprom_read_word((uint16_t *)EE_CLOCK_DRIFT) - SHRT_MAX;

        // Less than 20 minutes?
        if (abs(_clockDrift) < 20 * SECS_PER_MIN) clockDrift = _clockDrift;
    } // if
} // setupNtp
