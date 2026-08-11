#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =================================================
// OLED
// =================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// =================================================
// SENSOR UART
// =================================================

// Sensor TX -> ESP32 GPIO16
// Sensor RX -> ESP32 GPIO17

#define SENSOR_RX 16
#define SENSOR_TX 17

HardwareSerial SensorSerial(2);

// =================================================
// SENSOR VALUES
// =================================================

float pH = 7.00;

int lightValue = 0;

int tempRaw = 0;

float temperatureC = 0.0;

String packet = "";

// =================================================
// SETUP
// =================================================

void setup()
{
  Serial.begin(115200);

  // =================================================
  // OLED
  // =================================================

  Wire.begin(21, 22);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR))
  {
    Serial.println("OLED ERROR");

    while (1);
  }

  // =================================================
  // SENSOR UART
  // =================================================

  SensorSerial.begin(
    9600,
    SERIAL_8N1,
    SENSOR_RX,
    SENSOR_TX
  );

  // =================================================
  // STARTUP SCREEN
  // =================================================

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // Main heading
  display.setTextSize(2);

  display.setCursor(10, 8);
  display.println("SOIL pH");

  display.setCursor(10, 28);
  display.println("MEASURING");

  // Smaller name
  display.setTextSize(1);

  display.setCursor(38, 50);
  display.println("~ Arghya");

  display.display();

  delay(3000);

  // =================================================
  // WAITING SCREEN
  // =================================================

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(20, 28);
  display.println("Waiting for data...");

  display.display();

  delay(500);
}

// =================================================
// LOOP
// =================================================

void loop()
{
  readSensor();

  delay(100);
}

// =================================================
// READ SENSOR UART
// =================================================

void readSensor()
{
  while (SensorSerial.available())
  {
    char c = SensorSerial.read();

    // Ignore carriage return
    if (c == '\r')
    {
      continue;
    }

    // Complete packet
    if (c == '\n')
    {
      if (packet.length() > 0)
      {
        parseSensor(packet);

        packet = "";
      }
    }
    else
    {
      packet += c;
    }

    // Safety
    if (packet.length() > 100)
    {
      packet = "";
    }
  }
}

// =================================================
// PARSE SENSOR DATA
// =================================================

void parseSensor(String data)
{
  data.trim();

  Serial.print("UART: ");
  Serial.println(data);

  // =================================================
  // pH
  // =================================================

  int phPos = data.indexOf("PH:");

  if (phPos >= 0)
  {
    int phEnd = data.indexOf(',', phPos);

    if (phEnd > phPos)
    {
      String value = data.substring(
        phPos + 3,
        phEnd
      );

      value.trim();

      pH = value.toFloat();
    }
  }

  // =================================================
  // LIGHT
  // =================================================

  int lightPos = data.indexOf("L:");

  if (lightPos >= 0)
  {
    int lightEnd = data.indexOf(',', lightPos);

    if (lightEnd > lightPos)
    {
      String value = data.substring(
        lightPos + 2,
        lightEnd
      );

      value.trim();

      lightValue = value.toInt();
    }
  }

  // =================================================
  // TEMPERATURE
  // =================================================

  int tempPos = data.indexOf("T:");

  if (tempPos >= 0)
  {
    int tempEnd = data.indexOf(',', tempPos);

    if (tempEnd > tempPos)
    {
      String value = data.substring(
        tempPos + 2,
        tempEnd
      );

      value.trim();

      tempRaw = value.toInt();

      // ---------------------------------------------
      // YOUR CURRENT TEMPERATURE CALIBRATION
      //
      // Sensor T:57 = actual 31 C
      // ---------------------------------------------

      temperatureC =
        tempRaw * (31.0 / 57.0);
    }
  }

  // =================================================
  // SERIAL MONITOR
  // =================================================

  Serial.print("PH = ");
  Serial.println(pH, 2);

  Serial.print("Temperature = ");
  Serial.print(temperatureC, 1);
  Serial.println(" C");

  Serial.print("Light = ");
  Serial.println(lightValue);

  Serial.println("-------------------------");

  // =================================================
  // UPDATE OLED
  // =================================================

  updateOLED();
}

// =================================================
// pH CLASSIFICATION
// =================================================

String getPHStatus(float value)
{
  if (value < 3.5)
  {
    return "VERY ACIDIC";
  }

  else if (value < 7.0)
  {
    return "ACIDIC";
  }

  else if (value < 10.5)
  {
    return "ALKALINE";
  }

  else
  {
    return "VERY ALKALINE";
  }
}

// =================================================
// OLED DISPLAY
// =================================================

void updateOLED()
{
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // =================================================
  // HEADER
  // =================================================

  display.setTextSize(1);

  display.setCursor(0, 0);

  display.println("PH MEASUREMENT");

  display.drawLine(
    0,
    9,
    127,
    9,
    SSD1306_WHITE
  );

  // =================================================
  // pH VALUE
  // =================================================

  display.setTextSize(2);

  display.setCursor(0, 14);

  display.print("pH:");

  display.print(pH, 2);

  // =================================================
  // pH STATUS
  // =================================================

  display.setTextSize(1);

  display.setCursor(0, 35);

  display.print(getPHStatus(pH));

  // =================================================
  // TEMPERATURE
  // =================================================

  display.setCursor(0, 48);

  display.print("TEMP:");

  display.print(temperatureC, 1);

  display.print(" C");

  // =================================================
  // LIGHT
  // =================================================

  display.setCursor(75, 48);

  display.print("L:");

  display.print(lightValue);

  // =================================================
  // SHOW OLED
  // =================================================

  display.display();
}
