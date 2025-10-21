//Arduino UNO
//SCL   A5
//SDA   A4
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <config.h>
#include <SD.h>

#define trig 14
#define echo 27
#define GSM_RX 32
#define GSM_TX 33
#define relay 25
#define off false
#define on true
#define o_trig_temp 30    //30°C
#define o_trig_hmdty 60   //60%
#define water_lvl_trig 100 
#define scroll_delay 500

#define o_dht11 17
#define i_dht11 16

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT o_dht11_sensor(o_dht11, DHT11);
DHT i_dht11_sensor(i_dht11, DHT11);
SoftwareSerial gsm(GSM_RX, GSM_TX);

void InitializeGSM();
void CheckSMS();    
void UpdateSensors();
void CheckSensorValues();
void DisplayTemperatureAndHumidity(int setting);
void DisplayWaterLevel();
void ToggleSprinkler(bool state);
void UpdateGSM();
void DeleteSMS();
void CheckSMS();
void SendSMS(String number, String message);
float GetDistance();
int getDaysInMonth(int month, int year);
bool parseDateTime(String cclk);
String getCurrentDateTime();
bool getDateTimeOnce();
void incrementDateTime();

//SD
void initSD();
void SDLog();
File logFile;

// time
String currentDateTime = "";
unsigned long lastDateTimeMillis = 0;
int dt_year = 2025;
int dt_month = 1;
int dt_day = 1;
int dt_hour = 0;
int dt_minute = 0;
int dt_second = 0;
bool dateTimeInitialized = false;

void setup() {
    Serial.begin(115200);
    Serial.println("Initializing...");
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing GSM");

    o_dht11_sensor.begin();
    i_dht11_sensor.begin();
    InitializeGSM();
    initSD();

    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT_PULLUP);
    digitalWrite(echo, HIGH);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, HIGH);

    if (getDateTimeOnce()) {
        lastDateTimeMillis = millis();
        dateTimeInitialized = true;
        currentDateTime = getCurrentDateTime();
        Serial.println("DateTime initialized [" + currentDateTime + "]");
    }
    else {
        Serial.println("Failed to get DateTime from GSM.");
    }

    lcd.clear();
}

int display_mode = 0; //default
float i_temp = 0, i_hmdty = 0;
float o_temp = 0, o_hmdty = 0;
unsigned long sprinkler_timer = 0;
bool is_sprinkler_active = false;
const unsigned long sprinkler_timer_dur = 5000;
int i_res, o_res;
long duration_us;
int distance_us;
unsigned long current_time = 0, last_time = 0, mode_timer_duration = 5000;
float water_lvl_percent = 0;
bool isSMSCooldown = false;
const unsigned long smsCooldown = 300000; // 5 min in ms
bool SYSTEM_PAUSE = false;
bool manualOverride = false;
unsigned long sensor_last_time = 0;

void loop() {
    current_time = millis();

    if (dateTimeInitialized) {
        incrementDateTime();
    }

    if (current_time - sensor_last_time > 1000) {
        sensor_last_time = current_time;
        UpdateSensors();
        CheckSensorValues();
    }

    CheckSMS();    

    if (!SYSTEM_PAUSE)
    {
        if (current_time - last_time > mode_timer_duration) //cycle though display modes
        {
            last_time = current_time;
            SDLog();
            lcd.clear();
            display_mode++;
            if (display_mode > 2) display_mode = 0;
        }

        switch(display_mode)
        {
        case 0:
            DisplayTemperatureAndHumidity(0);
        break;
        case 1:
            DisplayTemperatureAndHumidity(1);
        break;
        case 2:        
            DisplayWaterLevel();
        break;
        }   
    }    
}

unsigned long cooldownTimer = 0, cooldownTimerDuration = 300000;
void CheckSensorValues()
{
    if (!manualOverride) {
        if (o_temp >= 32 && o_hmdty <= 70) {
            ToggleSprinkler(on);            
            if (!isSMSCooldown)
            {
                SYSTEM_PAUSE = true;
                isSMSCooldown = true;
                cooldownTimer = millis();
                ToggleSprinkler(on);

                String msg =
                    "[VAPOR SYSTEM ALERT]\n\nRoof: "   + String(o_temp)   + " C | " + String(o_hmdty) + "% RH\n" +
                    "Indoor: " + String(i_temp)   + " C | " + String(i_hmdty) + "% RH\n" +
                    "Water Level: "  + String((int)water_lvl_percent) + "%\n" +
                    "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

                SendSMS(num, msg);
                Serial.println("Message Sent");
                SYSTEM_PAUSE = false;
            }
        }
        else if (i_temp <= 30 || i_hmdty >= 70) {
            ToggleSprinkler(off);
        }
    }

    if (water_lvl_percent <= 20)
    {
        if (!isSMSCooldown)
        {
            SYSTEM_PAUSE = true;
            isSMSCooldown = true;
            cooldownTimer = millis();

            String msg = "[WATER TANK STATUS]\n\nWater level is LOW (" + String((int)water_lvl_percent) + "%). Please refill soon.";
            SendSMS(num, msg);
            SYSTEM_PAUSE = false;
        }
    }

    if (isSMSCooldown)
    {
        unsigned long elapsed = millis() - cooldownTimer;
        if (elapsed >= cooldownTimerDuration)
        {
            isSMSCooldown = false;
        }
        else
        {
            unsigned long timeLeft = (cooldownTimerDuration - elapsed) / 1000;
            Serial.println("SMS Cooldown: " + String(timeLeft) + "s");
        }
    }    
}

void UpdateSensors()
{
    // i_res = i_dht11.readTemperatureHumidity(i_temp, i_hmdty);
    // o_res = o_dht11.readTemperatureHumidity(o_temp, o_hmdty);
    i_temp = i_dht11_sensor.readTemperature();
    i_hmdty = i_dht11_sensor.readHumidity();
    o_temp = o_dht11_sensor.readTemperature();
    o_hmdty = o_dht11_sensor.readHumidity();

    float dist = GetDistance();

    // Convert distance → percent
    const float empty_dist = 45.0; // cm
    const float full_dist  = 20.0; // cm
    water_lvl_percent = ((empty_dist - dist) / (empty_dist - full_dist)) * 100.0;

    if (water_lvl_percent > 100) water_lvl_percent = 100;
    if (water_lvl_percent < 0)   water_lvl_percent = 0;

    // Debug print
    Serial.print("Indoor: " + String(i_temp) + " °C, " + String(i_hmdty) + "% RH | Roof: " + 
                 String(o_temp) + " °C, " + String(o_hmdty) + "% RH | "); 
    Serial.println("Water level: " + String(water_lvl_percent) + "% (raw: " + dist + " cm)");
}

void InitializeGSM()
{    
    gsm.begin(9600);

    gsm.println("AT");
    delay(500);
    UpdateGSM();

    gsm.println("AT+CSCS=\"GSM\"");
    delay(500);
    UpdateGSM(); 

    gsm.println("AT+CNMI=1,2,0,0,0");
    delay(500);
    UpdateGSM(); 

    gsm.println("AT+CMGF=1");
    delay(500);
    UpdateGSM(); 

    DeleteSMS();
    Serial.println("Initialized");
}


void DisplayTemperatureAndHumidity(int setting) // 1 = indoor; 0 = roof
{    
    if (isnan(o_temp) || isnan(o_hmdty) || isnan(i_temp) || isnan(i_hmdty)) 
    {                
        Serial.println("Failed to read from DHT sensor!");
        lcd.setCursor(0, 0);
        lcd.clear();
        lcd.print("DHT Error");
    }
    else {       
        //print on LCD
        lcd.setCursor(0, 0);
        lcd.print(setting ? "Indoor: " : "Roof: ");
        lcd.setCursor(0, 1);
        lcd.print(String(setting ? i_temp : o_temp) + (char)223 + "C | " + String(setting ? i_hmdty : o_hmdty) + "% RH");
    }  
}

void DisplayWaterLevel()
{
    lcd.setCursor(0, 0);
    lcd.print("Water Level:   "); // add spaces to clear old chars

    lcd.setCursor(0, 1);
    lcd.print((int)water_lvl_percent);
    lcd.print("%    "); // spaces to overwrite old digits

    //Serial.println("Water Level: " + (String)water_lvl + "%"); 
}

float GetDistance()
{
    digitalWrite(trig, LOW);
    delayMicroseconds(2);

    digitalWrite(trig, HIGH);
    delayMicroseconds(15);

    digitalWrite(trig, LOW);

    float dur = pulseIn(echo, HIGH, 26000);                                                       

    return dur / 58;
}

void SendSMS(String number, String message)
{
    SYSTEM_PAUSE = true;

    gsm.println("AT+CMGF=1");
    UpdateGSM();
    delay(250);

    gsm.println("AT+CMGS=\"" + number + "\"");
    UpdateGSM();
    delay(1000);    

    gsm.print(message);
    gsm.print(char(26));
    UpdateGSM();
    delay(1000);

    SYSTEM_PAUSE = false;
}

void UpdateGSM()
{
    //while (Serial.available()) gsm.write(Serial.read());
    while(gsm.available()) Serial.write((char)gsm.read());
}

String FormatTime(String cclk)
{
    if (cclk.length() < 17) return "";

    int yy   = cclk.substring(0, 2).toInt();
    int MM   = cclk.substring(3, 5).toInt();
    int dd   = cclk.substring(6, 8).toInt();
    int hh   = cclk.substring(9, 11).toInt();
    int min  = cclk.substring(12, 14).toInt();

    int yyyy = 2000 + yy;

    // 12-hour conversion
    String ampm = (hh >= 12) ? "PM" : "AM";
    int hour12 = (hh % 12 == 0) ? 12 : hh % 12;

    char buf[30];
    sprintf(buf, "%02d/%02d/%04d, %d:%02d %s", 
            MM, dd, yyyy, hour12, min, ampm.c_str());

    return String(buf);
}

void ToggleSprinkler(bool state)
{
    is_sprinkler_active = state;
    digitalWrite(relay, !is_sprinkler_active);
}

void DeleteSMS(){
    String Comm;
    Comm = "AT+CMGDA=";
    Comm += "\x22";
    Comm += "DEL ALL";
    Comm += "\x22";
    gsm.println(Comm);

    String CommRep = gsm.readString();
    Serial.print("Reply: "), Serial.println(CommRep);
}


void CheckSMS()
{
    static String smsBuffer = "";
    //static bool waitingForMsg = false;

    if (gsm.available())
    {
        String smsBuffer = gsm.readString();        
        //smsBuffer = decodeUCS2(smsBuffer);
        smsBuffer.toUpperCase();
        Serial.print(smsBuffer);
        //delay(2000); 

        if (smsBuffer.indexOf("VAPOR ON") > -1)
        {
            manualOverride = true;  // force ON
            ToggleSprinkler(true);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: ON");
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR OFF") > -1 )
        {
            manualOverride = false; // release override
            ToggleSprinkler(false);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: OFF");
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR STATUS") > -1)
        {
            SYSTEM_PAUSE = true;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Sending Status ");            

            String msg =
                "[VAPOR SYSTEM ALERT]\n\nRoof: "   + String(o_temp)   + " C | " + String(o_hmdty) + "% RH\n" +
                "Indoor: " + String(i_temp)   + " C | " + String(i_hmdty) + "% RH\n" +
                "Water Level: "  + String((int)water_lvl_percent) + "%\n" +
                "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

            SendSMS(num, msg);
            lcd.clear();
            SYSTEM_PAUSE = false;
        }            
    }
}

void initSD() {
    // For ESP32, specify the CS pin (usually GPIO 5)
    // Adjust the pin number based on your wiring
    if (!SD.begin(5)) {  // Change 5 to your actual CS pin
        Serial.println("ERROR: SD Card initialization failed!");
        Serial.println("Check: 1) SD card inserted, 2) Wiring, 3) CS pin number");
        return;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("ERROR: No SD card attached");
        return;
    }
    
    Serial.println("SUCCESS: SD Card initialized");
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

void SDLog() {
    String timeStamp = getCurrentDateTime();

    // ESP32 requires "/" prefix for file paths
    if (!SD.exists("/logs.txt")) {
        logFile = SD.open("/logs.txt", FILE_WRITE);
        if (logFile) {
            logFile.println("===== VAPOR SYSTEM LOG FILE =====");
            logFile.println("Created: " + timeStamp);
            logFile.println("====================================");
            logFile.close();
        }
        else {
            Serial.println("ERROR: Failed to create /logs.txt");
            return;
        }
    }

    logFile = SD.open("/logs.txt", FILE_APPEND);
    if (logFile) {
        logFile.print("[" + timeStamp + "]");
        logFile.print(" Indoor: " + String(i_temp) + "°C, " + String(i_hmdty) + "% RH");
        logFile.print(" | Roof: " + String(o_temp) + "°C, " + String(o_hmdty) + "% RH");
        logFile.print(" | Water level: " + String((int)water_lvl_percent) + "%");
        logFile.println(" | Sprinkler: " + String(is_sprinkler_active ? "ON" : "OFF"));
        logFile.close();
        Serial.println("Logged [" + timeStamp + "]");
    } else {
        Serial.println("ERROR: Failed to open /logs.txt for writing");
    }
}

bool getDateTimeOnce() {
    while (gsm.available()) gsm.read(); // Clear buffer

    gsm.println("AT+CCLK?");
    delay(1000); // Increased delay for ESP32

    String response = "";
    unsigned long timeout = millis();
    
    // Read response with timeout
    while (millis() - timeout < 2000) {
        if (gsm.available()) {
            response += (char)gsm.read();
        }
    }

    Serial.println("GSM Response: [" + response + "]");
    response.trim();

    // Look for +CCLK: "25/10/21,18:52:24+32"
    int startIndex = response.indexOf("+CCLK: \"");
    if (startIndex == -1) {
        Serial.println("ERROR: Could not find '+CCLK: \"'");
        return false;
    }

    startIndex += 8; // Move past '+CCLK: "' to point at the '2' in '25'
    
    // Find the closing quote starting from startIndex
    int endIndex = response.indexOf("\"", startIndex);
    if (endIndex == -1) {
        Serial.println("ERROR: Could not find closing quote");
        return false;
    }

    // Extract the time string between the quotes
    String timeString = response.substring(startIndex, endIndex);
    Serial.println("Extracted time: [" + timeString + "]");
    
    bool success = parseDateTime(timeString);
    if (success) {
        Serial.println("Parsed successfully!");
        Serial.println("Date: " + String(dt_year) + "/" + String(dt_month) + "/" + String(dt_day));
        Serial.println("Time: " + String(dt_hour) + ":" + String(dt_minute) + ":" + String(dt_second));
    } else {
        Serial.println("ERROR: Failed to parse datetime");
    }
    
    return success;
}

bool parseDateTime(String cclk) {
    Serial.println("Parsing: [" + cclk + "] Length: " + String(cclk.length()));
    
    if (cclk.length() < 17) {
        Serial.println("ERROR: String too short (need at least 17 chars)");
        return false;
    }

    // Format: "25/10/21,18:52:24+32"
    //          01234567890123456789
    
    dt_year   = 2000 + cclk.substring(0, 2).toInt();
    dt_month  = cclk.substring(3, 5).toInt();
    dt_day    = cclk.substring(6, 8).toInt();
    dt_hour   = cclk.substring(9, 11).toInt();
    dt_minute = cclk.substring(12, 14).toInt();
    dt_second = cclk.substring(15, 17).toInt();

    // Validate parsed values
    if (dt_month < 1 || dt_month > 12) {
        Serial.println("ERROR: Invalid month: " + String(dt_month));
        return false;
    }
    if (dt_day < 1 || dt_day > 31) {
        Serial.println("ERROR: Invalid day: " + String(dt_day));
        return false;
    }
    if (dt_hour < 0 || dt_hour > 23) {
        Serial.println("ERROR: Invalid hour: " + String(dt_hour));
        return false;
    }
    if (dt_minute < 0 || dt_minute > 59) {
        Serial.println("ERROR: Invalid minute: " + String(dt_minute));
        return false;
    }
    if (dt_second < 0 || dt_second > 59) {
        Serial.println("ERROR: Invalid second: " + String(dt_second));
        return false;
    }

    return true;
}

// Increment DateTime based on millis()
void incrementDateTime() {
    unsigned long currentMillis = millis();
    unsigned long elapsed = currentMillis - lastDateTimeMillis;

    if (elapsed >= 1000) {
        // Calculate how many seconds passed (handle overflow)
        int secondsPassed = elapsed / 1000;
        lastDateTimeMillis += (secondsPassed * 1000);

        dt_second += secondsPassed;

        // Handle seconds overflow
        if (dt_second >= 60) {
            dt_minute += dt_second / 60;
            dt_second = dt_second % 60;
        }

        // Handle minutes overflow
        if (dt_minute >= 60) {
            dt_hour += dt_minute / 60;
            dt_minute = dt_minute % 60;
        }

        // Handle hours overflow
        if (dt_hour >= 24) {
            dt_day += dt_hour / 24;
            dt_hour = dt_hour % 24;
        }

        // Handle day overflow (simplified - doesn't account for different month lengths)
        int daysInMonth = getDaysInMonth(dt_month, dt_year);
        if (dt_day > daysInMonth) {
            dt_day = 1;
            dt_month++;
        }

        // Handle month overflow
        if (dt_month > 12) {
            dt_month = 1;
            dt_year++;
        }
    }
}

// Helper function to get days in month
int getDaysInMonth(int month, int year) {
    switch(month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            // Leap year check
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
                return 29;
            else
                return 28;
        default:
            return 30;
    }
}

// Get current DateTime as formatted string
String getCurrentDateTime() {
    char buf[25];
    sprintf(buf, "%02d/%02d/%04d, %02d:%02d:%02d", 
            dt_month, dt_day, dt_year, dt_hour, dt_minute, dt_second);
    return String(buf);
}


