#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ESP32 RS485 PINS
#define RX2_PIN 16       // MAX485 RO -> ESP32 RX2
#define TX2_PIN 17       // ESP32 TX2 -> MAX485 DI

#define RS485_RE 4       // MAX485 RE
#define RS485_DE 2       // MAX485 DE

// NPK SENSOR SETTINGS

#define NPK_ID 0x01

#define NPK_BAUD 9600

// Current register
#define NPK_START_REGISTER 0x0000

// NPK SCALING

// Raw sensor value 2000 = 240 mg/kg

#define RAW_MAX_VALUE 2000
#define NPK_MAX_VALUE 240

// OLED SETTINGS

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_ADDR 0x3C

// OLED OBJECT

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ESP32 UART2


HardwareSerial RS485Serial(2);

// MODBUS CRC16

uint16_t modbusCRC(uint8_t *buffer, uint8_t length)
{
  uint16_t crc = 0xFFFF;

  for (uint8_t pos = 0; pos < length; pos++)
  {
    crc ^= buffer[pos];

    for (uint8_t i = 0; i < 8; i++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  return crc;
}

// RS485 TRANSMIT MODE

void rs485Transmit()
{
  digitalWrite(RS485_RE, HIGH);
  digitalWrite(RS485_DE, HIGH);

  delayMicroseconds(100);
}

// RS485 RECEIVE MODE

void rs485Receive()
{
  RS485Serial.flush();

  delayMicroseconds(100);

  digitalWrite(RS485_DE, LOW);
  digitalWrite(RS485_RE, LOW);

  delayMicroseconds(100);
}

// READ NPK SENSOR

bool readNPK(
  uint16_t &nitrogen,
  uint16_t &phosphorus,
  uint16_t &potassium
)
{
  uint8_t request[8];

  // MODBUS REQUEST

  request[0] = NPK_ID;
  request[1] = 0x03;

  request[2] = 0x00;
  request[3] = NPK_START_REGISTER;

  request[4] = 0x00;
  request[5] = 0x03;

  // CALCULATE CRC

  uint16_t crc = modbusCRC(request, 6);

  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  // CLEAR OLD DATA

  while (RS485Serial.available())
  {
    RS485Serial.read();
  }

  // SEND REQUEST

  rs485Transmit();

  RS485Serial.write(request, 8);

  RS485Serial.flush();

  // RECEIVE MODE

  rs485Receive();

  // EXPECTED RESPONSE
  //
  // 01 03 06 NN NN PP PP KK KK CRC CRC
  //
  // TOTAL = 11 BYTES

  uint8_t response[11];
  uint8_t index = 0;
  unsigned long startTime = millis();

  while (millis() - startTime < 1000)
  {
    if (RS485Serial.available())
    {
      response[index] = RS485Serial.read();

      index++;

      if (index >= 11)
      {
        break;
      }
    }
  }

  // CHECK RESPONSE LENGTH

  if (index != 11)
  {
    Serial.println("ERROR: No complete NPK response");

    Serial.print("Received bytes: ");
    Serial.println(index);

    return false;
  }

  // PRINT RAW RESPONSE
  Serial.print("RAW: ");

  for (uint8_t i = 0; i < 11; i++)
  {
    if (response[i] < 0x10)
    {
      Serial.print("0");
    }

    Serial.print(response[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

  // CHECK SENSOR ID

  if (response[0] != NPK_ID)
  {
    Serial.println("ERROR: Wrong sensor ID");

    return false;
  }

  // CHECK MODBUS FUNCTION

  if (response[1] != 0x03)
  {
    Serial.println("ERROR: Modbus function error");

    return false;
  }

  // CHECK BYTE COUNT
 
  if (response[2] != 0x06)
  {
    Serial.println("ERROR: Invalid byte count");

    return false;
  }

  // CHECK CRC

  uint16_t receivedCRC =
    response[9] |
    ((uint16_t)response[10] << 8);


  uint16_t calculatedCRC =
    modbusCRC(response, 9);


  if (receivedCRC != calculatedCRC)
  {
    Serial.println("ERROR: CRC error");

    Serial.print("Received CRC: ");
    Serial.println(receivedCRC, HEX);

    Serial.print("Calculated CRC: ");
    Serial.println(calculatedCRC, HEX);

    return false;
  }

  // EXTRACT RAW NPK VALUES

  uint16_t rawNitrogen =
    ((uint16_t)response[3] << 8) |
    response[4];


  uint16_t rawPhosphorus =
    ((uint16_t)response[5] << 8) |
    response[6];


  uint16_t rawPotassium =
    ((uint16_t)response[7] << 8) |
    response[8];

  // PRINT RAW NPK
  Serial.println();

  Serial.println("RAW NPK VALUES:");

  Serial.print("Raw Nitrogen: ");
  Serial.println(rawNitrogen);

  Serial.print("Raw Phosphorus: ");
  Serial.println(rawPhosphorus);

  Serial.print("Raw Potassium: ");
  Serial.println(rawPotassium);

  // SCALE NPK
  //
  // RAW 2000 = 240 mg/kg
  //
  // Formula:
  //
  // Value = RAW x 240 / 2000

  nitrogen =
    ((uint32_t)rawNitrogen * NPK_MAX_VALUE)
    / RAW_MAX_VALUE;


  phosphorus =
    ((uint32_t)rawPhosphorus * NPK_MAX_VALUE)
    / RAW_MAX_VALUE;


  potassium =
    ((uint32_t)rawPotassium * NPK_MAX_VALUE)
    / RAW_MAX_VALUE;

  // LIMIT VALUE TO 240

  if (nitrogen > NPK_MAX_VALUE)
  {
    nitrogen = NPK_MAX_VALUE;
  }

  if (phosphorus > NPK_MAX_VALUE)
  {
    phosphorus = NPK_MAX_VALUE;
  }

  if (potassium > NPK_MAX_VALUE)
  {
    potassium = NPK_MAX_VALUE;
  }


  return true;
}

// OLED PAGE 1
// INITIAL SCREEN


void displayStartup()
{
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  // SOIL MONITOR

  display.setCursor(25, 15);

  display.println("SOIL MONITOR");

  // SYSTEM

  display.setCursor(40, 30);

  display.println("SYSTEM");

  // LINE


  display.drawLine(
    20,
    45,
    108,
    45,
    SSD1306_WHITE
  );

  // STARTING


  display.setCursor(38, 52);

  display.println("STARTING...");


  display.display();


  // Show startup page for 2 seconds

  delay(2000);
}

// OLED PAGE 2


void displayNPK(
  uint16_t N,
  uint16_t P,
  uint16_t K
)
{
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);


  // ---------------------------------------------------
  // TITLE
  // ---------------------------------------------------

  display.setCursor(25, 3);

  display.println("SOIL PARAMETERS");


  // ---------------------------------------------------
  // UNIT
  // ---------------------------------------------------

  display.setCursor(48, 13);

  display.println("(mg/kg)");


  // ---------------------------------------------------
  // NITROGEN
  // ---------------------------------------------------

  display.setCursor(5, 27);

  display.print("NITROGEN   : ");

  display.print(N);


  // ---------------------------------------------------
  // PHOSPHORUS
  // ---------------------------------------------------

  display.setCursor(5, 39);

  display.print("PHOSPHORUS : ");

  display.print(P);


  // ---------------------------------------------------
  // POTASSIUM
  // ---------------------------------------------------

  display.setCursor(5, 51);

  display.print("POTASSIUM  : ");

  display.print(K);


  display.display();
}


// OLED COMMUNICATION ERROR

void displayError()
{
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);


  display.setCursor(25, 5);

  display.println("SOIL MONITOR");


  display.drawLine(
    0,
    15,
    127,
    15,
    SSD1306_WHITE
  );

  display.setCursor(20, 25);
  display.println("NPK SENSOR");
  display.setCursor(35, 38);
  display.println("COMM ERROR");
  display.setCursor(25, 52);
  display.println("CHECK RS485");
  display.display();
}



// SETUP
void setup()
{
  
  // SERIAL MONITOR
  
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("==============================");
  Serial.println(" ESP32 RS485 NPK SENSOR");
  Serial.println("==============================");

  // RS485 DIRECTION PINS

  pinMode(
    RS485_RE,
    OUTPUT
  );

  pinMode(
    RS485_DE,
    OUTPUT
  );

  // Start in receive mode

  digitalWrite(
    RS485_RE,
    LOW
  );

  digitalWrite(
    RS485_DE,
    LOW
  );

  // UART2
  RS485Serial.begin(
    NPK_BAUD,
    SERIAL_8N1,
    RX2_PIN,
    TX2_PIN
  );
  Serial.print("NPK Baud: ");
  Serial.println(NPK_BAUD);
  Serial.println("UART2 initialized");
 
  // OLED
 

  Wire.begin(
    21,
    22
  );
  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR
      ))
  {
    Serial.println("OLED ERROR!");
  }
  else
  {
    Serial.println("OLED initialized");
    // PAGE 1
    displayStartup();
  }


  Serial.println("System ready");
  Serial.println();
}

// LOOP


void loop()
{
  uint16_t nitrogen = 0;
  uint16_t phosphorus = 0;
  uint16_t potassium = 0;
  // SERIAL MESSAGE
 

  Serial.println("------------------------------");

  Serial.println("Reading NPK...");


  // READ SENSOR
  

  if (
    readNPK(
      nitrogen,
      phosphorus,
      potassium
    )
  )
  {
   
    // SERIAL OUTPUT
    

    Serial.println("NPK READ SUCCESS!");
    Serial.print("Nitrogen (N): ");
    Serial.print(nitrogen);
    Serial.println(" mg/kg");
    Serial.print("Phosphorus (P): ");
    Serial.print(phosphorus);
    Serial.println(" mg/kg");
    Serial.print("Potassium (K): ");
    Serial.print(potassium);
    Serial.println(" mg/kg");
    Serial.println();
    // OLED PAGE 2
   

    displayNPK(
      nitrogen,
      phosphorus,
      potassium
    );
  }
  else
  {
    
    Serial.println("NPK READ FAILED!");

    displayError();
  }
  delay(2000);
}
