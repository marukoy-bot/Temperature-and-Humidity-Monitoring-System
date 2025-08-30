# Temperature and Humidity Monitoring System
Temperature and Humidity Monitoring System using:
- Arduino UNO as MCU
- I²C LCD for displaying temperature and humidity information
- AJ-SR04M utrasonic sensor for water level monitoring inside the water container container
- SIM800L EVB GMS Module for SMS functionality
- 5v relay to turn on 12v pump
- 2 DHT11 modules to gather temperature and humidity data for both indoor and outdoor.

# Functionality

 - Sends SMS alerts when tempreature reaches 30°C or water level is below 20%. Sending SMS cooldown is 2 mins.
 - Manual override of relay (sprinkler) functions using SMS keywords:
    - "VAPOR ON" turns on relay regardless of temperature
    - "VAPOR OFF" turn off relay
    - "VAPOR STATUS" sends current temperature and humidity for 2 DHT11 sensors, water level, and relay(sprinkler) status if ON or OFF
