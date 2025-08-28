//Arduino UNO
//SCL   A5
//SDA   A4

#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT11.h>

#define trig 12
#define echo 11

#define RX 3
#define TX 4

#define relay 5

#define num "+639151635499"

#define o_trig_temp 30    //30°C
#define o_trig_hmdty 60   //60%
#define water_lvl_trig 100 

#define scroll_delay 500

LiquidCrystal_I2C lcd(0x27, 16, 2);

DHT11 o_dht11(2);
DHT11 i_dht11(6);

SoftwareSerial gsm(RX, TX);

void setup() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing GSM");

    InitializeGSM();

    Serial.begin(9600);

    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, HIGH);

    lcd.clear();
}

int display_mode = 0; //default

int i_temp = 0, i_hmdty = 0;
int o_temp = 0, o_hmdty = 0;

unsigned long sprinkler_timer = 0;
bool is_sprinkler_active = false;
const unsigned long sprinkler_timer_dur = 5000;

int i_res, o_res;
long duration_us;
int distance_us;

unsigned long current_time = 0, last_time = 0, mode_timer_duration = 5000;
float water_lvl;

bool isSMSCooldown = false;

const unsigned long smsCooldown = 120000; // 5 min in ms

bool SYSTEM_PAUSE = false;
bool manualOverride = false;

void loop() {
    current_time = millis();

    CheckSMS();    

    UpdateSensors();

    CheckSensorValues();

    if (current_time - last_time > mode_timer_duration) //cycle though display modes
    {
        lcd.clear();
        display_mode++;
        if (display_mode > 2) display_mode = 0;
        last_time = current_time;
    }

    if (!SYSTEM_PAUSE)
    {
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

unsigned long cooldownTimer = 0, cooldownTimerDuration = 120000;
void CheckSensorValues()
{
    if (o_temp >= 30)
    {
        if (!isSMSCooldown)
        {
            isSMSCooldown = true;
            cooldownTimer = millis();
            ToggleSprinkler(true);

            String msg =
                "[VAPOR SYSTEM ALERT]\n\nRoof: "   + String(o_temp)   + " C | " + String(o_hmdty) + "% RH\n" +
                "Indoor: " + String(i_temp)   + " C | " + String(i_hmdty) + "% RH\n" +
                "Water Level: "  + String((int)water_lvl) + "%\n" +
                "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

            SendSMS(num, msg);
            Serial.println("Message Sent");
        }
    }
    else if (o_temp < 29 && !manualOverride)
    {
        ToggleSprinkler(false);
    }

    if (water_lvl <= 18)
    {
        if (!isSMSCooldown)
        {
            isSMSCooldown = true;
            cooldownTimer = millis();

            String msg = "[WATER TANK STATUS]\n\nWater level is LOW (" + String(water_lvl) + "%). Please refill soon.";
            SendSMS(num, msg);
        }
        Serial.println("Water level LOW");
    }
    else if (water_lvl > 20)
    {
        Serial.println("Water level NORMAL");
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
            Serial.println("SMS Cooldown: " + String(timeLeft));
        }
    }    
}

unsigned long lastTime = 0, updateDuration = 1000;
void UpdateSensors()
{
    if (millis() - lastTime >= updateDuration)
    {
        lastTime = millis();

        i_res = i_dht11.readTemperatureHumidity(i_temp, i_hmdty);
        o_res = o_dht11.readTemperatureHumidity(o_temp, o_hmdty);

        water_lvl = map(GetDistance(), 20, 100, 100, 0); //assuming 100cm deep 
    }    
}

String toUCS2(String text) {
    String out = "";
    for (int i = 0; i < text.length(); i++) {
        char c = text[i];
        char buf[5];
        sprintf(buf, "%04X", (unsigned char)c); // UCS2 hex
        out += buf;
    }
    return out;
}

String decodeUCS2(String hex) {
    String result = "";
    for (int i = 0; i < hex.length(); i += 4) {
        String part = hex.substring(i, i + 4);
        char c = (char) strtol(part.c_str(), NULL, 16);
        result += c;
    }
    return result;
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
    if (i_res == 0 || o_res == 0) 
    {
        //print on serial monitor
        Serial.print(setting ? "Indoor: " : "Roof: ");
        Serial.print(setting ? i_temp : o_temp);
        Serial.print(" °C | ");
        Serial.print(setting ? i_hmdty : o_hmdty);
        Serial.println("% RH");

        //print on LCD
        lcd.setCursor(0, 0);
        lcd.print(setting ? "Indoor: " : "Roof: ");
        
        lcd.setCursor(0, 1);
        lcd.print(String(setting ? i_temp : o_temp) + (char)223 + "C | " + String(setting ? i_hmdty : o_hmdty) + "% RH");
    } 
    else 
    {
        //print error message based on the error code.
        lcd.print(DHT11::getErrorString(i_res));
    }    
}

void DisplayWaterLevel()
{
    lcd.setCursor(0, 0);
    lcd.print("Water Level:   "); // add spaces to clear old chars

    lcd.setCursor(0, 1);
    lcd.print((int)water_lvl);
    lcd.print("%   "); // spaces to overwrite old digits

    Serial.println("Water Level: " + (String)water_lvl + "%");
}

float GetDistance()
{
    digitalWrite(trig, LOW);
    delayMicroseconds(2);

    digitalWrite(trig, HIGH);
    delayMicroseconds(20);

    digitalWrite(trig, LOW);

    duration_us = pulseIn(echo, HIGH);

    return duration_us * 0.034 / 2;
}

void SendSMS(String number, String message)
{
    gsm.println("AT+CMGF=1");
    UpdateGSM();
    delay(1000);

    gsm.println("AT+CMGS=\"" + number + "\"");
    UpdateGSM();
    delay(1000);    

    gsm.print(message);
    gsm.print(char(26));
    UpdateGSM();
    delay(1000);
}

void UpdateGSM()
{
    while(Serial.available()) gsm.write(Serial.read());
    while(gsm.available()) Serial.write((char)gsm.read());
}

String GetDateTime()
{
    String dateTime = "";
    gsm.println("AT+CCLK?");
    delay(500);

    while(gsm.available()) dateTime += (char)gsm.read();
    dateTime.trim();

    return dateTime;
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
    static bool waitingForMsg = false;

    if (gsm.available())
    {
        String smsBuffer = gsm.readString();        
        //smsBuffer = decodeUCS2(smsBuffer);
        smsBuffer.toUpperCase();
        Serial.print(smsBuffer);
        delay(2000); 

        if (smsBuffer.indexOf("VAPOR ON") > -1)
        {
            manualOverride = true;  // force ON
            ToggleSprinkler(true);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: ON");
        }
        else if (smsBuffer.indexOf("VAPOR OFF") > -1 )
        {
            manualOverride = false; // release override
            ToggleSprinkler(false);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: OFF");
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
                "Water Level: "  + String((int)water_lvl) + "%\n" +
                "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

            SendSMS(num, msg);
            SYSTEM_PAUSE = false;
        }            
    }
}
