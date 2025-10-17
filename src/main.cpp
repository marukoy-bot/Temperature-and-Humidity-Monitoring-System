//Arduino UNO - RAM Optimized Version
//SCL   A5
//SDA   A4
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT11.h>
#include <config.h>
#include <SD.h> 

#define trig 9
#define echo 8
#define RX 3
#define TX 4
#define relay 5
#define o_trig_temp 30
#define o_trig_hmdty 60
#define scroll_delay 500

LiquidCrystal_I2C lcd(0x27, 16, 2);
File logFile;

DHT11 o_dht11(2);
DHT11 i_dht11(6);

SoftwareSerial gsm(RX, TX);

// Function declarations
void initSD();
void InitializeGSM();
void CheckSMS();    
void UpdateSensors();
void CheckSensorValues();
void DisplayTemperatureAndHumidity(int8_t setting);
void DisplayWaterLevel();
void ToggleSprinkler(bool state);
void UpdateGSM();
void DeleteSMS();
void SendSMS(const char* number, const char* message);
float GetDistance();
void SDLog();
void GetDateTime(char* buf, uint8_t bufSize);
void formatDateTime(const char* cclk, char* buf, uint8_t bufSize);
void GetDateTimeOnce();
void FormatCurrentTime(char* buf, uint8_t bufSize);
void IncrementDateTime(unsigned long intervalMs);

// Global variables - keep minimal
int8_t display_mode = 0;
int i_temp = 0, i_hmdty = 0;  // Must be int for DHT11 library
int o_temp = 0, o_hmdty = 0;  // Must be int for DHT11 library
bool is_sprinkler_active = false;
int i_res, o_res;
uint32_t current_time = 0, last_time = 0, sensor_last_time = 0;
float water_lvl_percent = 0;
bool isSMSCooldown = false;
bool SYSTEM_PAUSE = false;
bool manualOverride = false;
uint32_t cooldownTimer = 0;

// ---------- Time handling ----------
struct DateTime {
    uint8_t sec, min, hour, day, month;
    uint16_t year;
};

DateTime currentDateTime;
unsigned long lastTimeSync = 0;
const unsigned long LOG_INTERVAL = 5000;       // 5 seconds
const unsigned long RESYNC_INTERVAL = 3600000; // 1 hour


// Constants in PROGMEM to save RAM
const char MSG_ALERT[] PROGMEM = "[VAPOR SYSTEM ALERT]\n\nRoof: ";
const char MSG_C[] PROGMEM = " C | ";
const char MSG_RH[] PROGMEM = "% RH\n";
const char MSG_INDOOR[] PROGMEM = "Indoor: ";
const char MSG_WATER[] PROGMEM = "Water Level: ";
const char MSG_STATUS[] PROGMEM = "%\nStatus: SPRINKLER ";
const char MSG_WATER_LOW[] PROGMEM = "[WATER TANK STATUS]\n\nWater level is LOW (";
const char MSG_REFILL[] PROGMEM = "%). Please refill soon.";
const char MSG_ON[] PROGMEM = "ON";
const char MSG_OFF[] PROGMEM = "OFF";

void setup() {
    Serial.begin(115200);
    Serial.println(F("Initializing..."));
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Initializing GSM"));

    InitializeGSM();
    initSD();

    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT_PULLUP);
    digitalWrite(echo, HIGH);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, HIGH);

    GetDateTimeOnce();  // get GSM time once at startup
    lastTimeSync = millis();
    lcd.clear();
}

void loop() {
    current_time = millis();
    CheckSMS();    

    if (current_time - sensor_last_time > 1000) {
        sensor_last_time = current_time;
        UpdateSensors();
        CheckSensorValues();
    }

    if (current_time - last_time > 5000) {
        last_time = current_time;
        SDLog();
        lcd.clear();
        display_mode++;
        if (display_mode > 2) display_mode = 0;
    }

    switch(display_mode) {
        case 0: DisplayTemperatureAndHumidity(0); break;
        case 1: DisplayTemperatureAndHumidity(1); break;
        case 2: DisplayWaterLevel(); break;
    }   
    if (!SYSTEM_PAUSE) {
    }    
}

void CheckSensorValues() {
    if (!manualOverride) {
        if (o_temp >= 30 && o_hmdty <= 70) {
            ToggleSprinkler(true);            
            if (!isSMSCooldown) {
                SYSTEM_PAUSE = true;
                isSMSCooldown = true;
                cooldownTimer = millis();

                // Build message in parts to save RAM
                char msg[160];
                strcpy_P(msg, MSG_ALERT);
                char temp[10];
                itoa(o_temp, temp, 10);
                strcat(msg, temp);
                strcat_P(msg, MSG_C);
                itoa(o_hmdty, temp, 10);
                strcat(msg, temp);
                strcat_P(msg, MSG_RH);
                strcat_P(msg, MSG_INDOOR);
                itoa(i_temp, temp, 10);
                strcat(msg, temp);
                strcat_P(msg, MSG_C);
                itoa(i_hmdty, temp, 10);
                strcat(msg, temp);
                strcat_P(msg, MSG_RH);
                strcat_P(msg, MSG_WATER);
                itoa((int)water_lvl_percent, temp, 10);
                strcat(msg, temp);
                strcat_P(msg, MSG_STATUS);
                strcat_P(msg, is_sprinkler_active ? MSG_ON : MSG_OFF);

                SendSMS(num, msg);
                Serial.println(F("Message Sent"));
                SYSTEM_PAUSE = false;
            }
        }
        else if (o_temp <= 27 || o_hmdty >= 75) {
            ToggleSprinkler(false);
        }
    }

    if (water_lvl_percent <= 20 && !isSMSCooldown) {
        SYSTEM_PAUSE = true;
        isSMSCooldown = true;
        cooldownTimer = millis();

        char msg[100];
        strcpy_P(msg, MSG_WATER_LOW);
        char temp[5];
        itoa((int)water_lvl_percent, temp, 10);
        strcat(msg, temp);
        strcat_P(msg, MSG_REFILL);
        
        SendSMS(num, msg);
        SYSTEM_PAUSE = false;
    }

    if (isSMSCooldown) {
        uint32_t elapsed = millis() - cooldownTimer;
        if (elapsed >= 300000UL) {
            isSMSCooldown = false;
        }
    }    
}

void UpdateSensors() {
    i_res = i_dht11.readTemperatureHumidity(i_temp, i_hmdty);
    o_res = o_dht11.readTemperatureHumidity(o_temp, o_hmdty);

    float dist = GetDistance();
    const float empty_dist = 45.0;
    const float full_dist  = 20.0;
    water_lvl_percent = ((empty_dist - dist) / (empty_dist - full_dist)) * 100.0;

    if (water_lvl_percent > 100) water_lvl_percent = 100;
    if (water_lvl_percent < 0)   water_lvl_percent = 0;

    Serial.print(F("Indoor: "));
    Serial.print(i_temp);
    Serial.print(F(" C, "));
    Serial.print(i_hmdty);
    Serial.print(F("% | Roof: "));
    Serial.print(o_temp);
    Serial.print(F(" C, "));
    Serial.print(o_hmdty);
    Serial.print(F("% | Water: "));
    Serial.print(water_lvl_percent);
    Serial.println('%');
}

void InitializeGSM() {    
    gsm.begin(9600);
    gsm.println(F("AT"));
    delay(500);
    UpdateGSM();

    gsm.println(F("AT+CSCS=\"GSM\""));
    delay(500);
    UpdateGSM(); 

    gsm.println(F("AT+CNMI=1,2,0,0,0"));
    delay(500);
    UpdateGSM(); 

    gsm.println(F("AT+CMGF=1"));
    delay(500);
    UpdateGSM(); 

    gsm.println(F("AT+CLTS=1"));
    delay(500);
    UpdateGSM();

    gsm.println(F("AT&W"));
    delay(500);
    UpdateGSM();

    gsm.println(F("AT+CCLK?"));
    delay(500);
    UpdateGSM();

    DeleteSMS();
    Serial.println(F("Initialized"));
}

void DisplayTemperatureAndHumidity(int8_t setting) {    
    if (i_res == 0 || o_res == 0) {       
        lcd.setCursor(0, 0);
        lcd.print(setting ? F("Indoor: ") : F("Roof: "));
        
        lcd.setCursor(0, 1);
        lcd.print(setting ? i_temp : o_temp);
        lcd.print((char)223);
        lcd.print(F("C | "));
        lcd.print(setting ? i_hmdty : o_hmdty);
        lcd.print(F("% RH"));
    } 
    else {
        lcd.print(DHT11::getErrorString(i_res));
    }    
}

void DisplayWaterLevel() {
    lcd.setCursor(0, 0);
    lcd.print(F("Water Level:   "));

    lcd.setCursor(0, 1);
    lcd.print((int)water_lvl_percent);
    lcd.print(F("%    "));
}

float GetDistance() {
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(15);
    digitalWrite(trig, LOW);

    return pulseIn(echo, HIGH, 26000) / 58.0;
}

void SendSMS(const char* number, const char* message) {
    SYSTEM_PAUSE = true;

    gsm.println(F("AT+CMGF=1"));
    UpdateGSM();
    delay(250);

    gsm.print(F("AT+CMGS=\""));
    gsm.print(number);
    gsm.println('"');
    UpdateGSM();
    delay(1000);    

    gsm.print(message);
    gsm.write(26);
    UpdateGSM();
    delay(1000);

    SYSTEM_PAUSE = false;
}

void UpdateGSM() {
    while(gsm.available()) Serial.write((char)gsm.read());
}

void ToggleSprinkler(bool state) {
    is_sprinkler_active = state;
    digitalWrite(relay, !state);
}

void DeleteSMS() {
    gsm.println(F("AT+CMGDA=\"DEL ALL\""));
    delay(500);
    UpdateGSM();
}

void CheckSMS() {   
    if (gsm.available()) {
        delay(300);
        String smsBuffer = "";

        while (gsm.available()) {
            smsBuffer += (char)gsm.read();
        }
        smsBuffer.toUpperCase();
        //Serial.println(smsBuffer);

        if (smsBuffer.indexOf("VAPOR ON") > -1) {
            manualOverride = true;
            ToggleSprinkler(true);
            lcd.clear();
            lcd.print(F("SPRINKLER: ON"));
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR OFF") > -1) {
            manualOverride = false;
            ToggleSprinkler(false);
            lcd.clear();
            lcd.print(F("SPRINKLER: OFF"));
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR STATUS") > -1) {
            SYSTEM_PAUSE = true;
            lcd.clear();
            lcd.print(F("Sending Status"));
    
            char msg[160];
            strcpy_P(msg, MSG_ALERT);
            char temp[10];
            itoa(o_temp, temp, 10);
            strcat(msg, temp);
            strcat_P(msg, MSG_C);
            itoa(o_hmdty, temp, 10);
            strcat(msg, temp);
            strcat_P(msg, MSG_RH);
            strcat_P(msg, MSG_INDOOR);
            itoa(i_temp, temp, 10);
            strcat(msg, temp);
            strcat_P(msg, MSG_C);
            itoa(i_hmdty, temp, 10);
            strcat(msg, temp);
            strcat_P(msg, MSG_RH);
            strcat_P(msg, MSG_WATER);
            itoa((int)water_lvl_percent, temp, 10);
            strcat(msg, temp);
            strcat_P(msg, MSG_STATUS);
            strcat_P(msg, is_sprinkler_active ? MSG_ON : MSG_OFF);
    
            SendSMS(num, msg);
            lcd.clear();
            SYSTEM_PAUSE = false;
        }
    }
}

void initSD() {
    if (SD.begin(10)) {
        Serial.println(F("SD Card ready."));
    } else {
        Serial.println(F("SD init failed."));
    }
}

void SDLog() {
    // Optional resync every 1 hour
    if (millis() - lastTimeSync >= RESYNC_INTERVAL) {
        GetDateTimeOnce();
        lastTimeSync = millis();
    }

    char timeStamp[25];
    FormatCurrentTime(timeStamp, sizeof(timeStamp));

    if (!SD.exists("logs.txt")) {
        logFile = SD.open("logs.txt", FILE_WRITE);
        if (logFile) {
            logFile.print(F("["));
            logFile.print(timeStamp);
            logFile.println(F("] LOG FILE CREATED"));
            logFile.println(F("===================================="));
            logFile.close();
        }
    }

    logFile = SD.open("logs.txt", FILE_WRITE);
    if (logFile) {
        logFile.print('[');
        logFile.print(timeStamp);
        logFile.print(F("] | Indoor: "));
        logFile.print(i_temp);
        logFile.print(F("C, "));
        logFile.print(i_hmdty);
        logFile.print(F("% | Roof: "));
        logFile.print(o_temp);
        logFile.print(F("C, "));
        logFile.print(o_hmdty);
        logFile.print(F("% | Water: "));
        logFile.print((int)water_lvl_percent);
        logFile.print(F("% | Sprinkler: "));
        logFile.println(is_sprinkler_active ? F("ON") : F("OFF"));
        logFile.close();
    }

    IncrementDateTime(LOG_INTERVAL);  // Advance local clock
}

void GetDateTime(char* buf, uint8_t bufSize) {
    while (gsm.available()) gsm.read();

    gsm.println(F("AT+CCLK?"));
    delay(1000);  // Increased delay

    char response[50];
    uint8_t i = 0;
    while(gsm.available() && i < 49) {
        response[i++] = (char)gsm.read();
    }
    response[i] = '\0';

    // Find first quote
    char* start = strchr(response, '"');
    if (start) {
        start++;  // Move past first quote
        // Find second quote (search from start position)
        char* end = strchr(start, '"');
        if (end) {
            *end = '\0';
            formatDateTime(start, buf, bufSize);
            return;
        }
    }
    
    strcpy(buf, "N/A");
}

void formatDateTime(const char* cclk, char* buf, uint8_t bufSize) {
    if (strlen(cclk) < 17) {
        strcpy(buf, "invalid time");
        return;
    }

    char temp[3];
    
    temp[0] = cclk[0]; temp[1] = cclk[1]; temp[2] = '\0';
    int yy = atoi(temp);
    
    temp[0] = cclk[3]; temp[1] = cclk[4];
    int MM = atoi(temp);
    
    temp[0] = cclk[6]; temp[1] = cclk[7];
    int dd = atoi(temp);
    
    temp[0] = cclk[9]; temp[1] = cclk[10];
    int hh = atoi(temp);
    
    temp[0] = cclk[12]; temp[1] = cclk[13];
    int mn = atoi(temp);
    
    temp[0] = cclk[15]; temp[1] = cclk[16];
    int ss = atoi(temp);

    snprintf(buf, bufSize, "%02d/%02d/%04d, %02d:%02d:%02d", 
             MM, dd, 2000 + yy, hh, mn, ss);
}

void GetDateTimeOnce() {
    while (gsm.available()) gsm.read();

    gsm.println(F("AT+CCLK?"));
    delay(1000);

    char response[50];
    uint8_t i = 0;
    while (gsm.available() && i < 49) {
        response[i++] = gsm.read();
    }
    response[i] = '\0';

    char* start = strchr(response, '"');
    if (!start) return;
    start++;

    char temp[3];
    temp[2] = '\0';

    temp[0]=start[0]; temp[1]=start[1]; currentDateTime.year = 2000 + atoi(temp);
    temp[0]=start[3]; temp[1]=start[4]; currentDateTime.month = atoi(temp);
    temp[0]=start[6]; temp[1]=start[7]; currentDateTime.day = atoi(temp);
    temp[0]=start[9]; temp[1]=start[10]; currentDateTime.hour = atoi(temp);
    temp[0]=start[12]; temp[1]=start[13]; currentDateTime.min = atoi(temp);
    temp[0]=start[15]; temp[1]=start[16]; currentDateTime.sec = atoi(temp);
}

void IncrementDateTime(unsigned long intervalMs) {
    unsigned long seconds = intervalMs / 1000;
    currentDateTime.sec += seconds;

    while (currentDateTime.sec >= 60) {
        currentDateTime.sec -= 60;
        currentDateTime.min++;
    }
    while (currentDateTime.min >= 60) {
        currentDateTime.min -= 60;
        currentDateTime.hour++;
    }
    while (currentDateTime.hour >= 24) {
        currentDateTime.hour -= 24;
        currentDateTime.day++;
        // Simplified; skipping month/day rollover for now
    }
}

void FormatCurrentTime(char* buf, uint8_t bufSize) {
    snprintf(buf, bufSize, "%02d/%02d/%04d, %02d:%02d:%02d",
             currentDateTime.month, currentDateTime.day, currentDateTime.year,
             currentDateTime.hour, currentDateTime.min, currentDateTime.sec);
}
