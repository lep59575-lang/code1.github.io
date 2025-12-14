#include <ESP8266WiFi.h>          // Thư viện WiFi cho ESP8266
#include <DNSServer.h>            // Thư viện DNS server
#include <ESP8266WebServer.h>     // Thư viện WebServer
#include <WiFiManager.h>          // Quản lý WiFi tự động

#include <Arduino.h>              // Thư viện Arduino cơ bản
#include <SPI.h>                  // SPI cho RFID
#include <MFRC522.h>              // Thư viện đọc RFID
#include <EEPROM.h>               // Thư viện EEPROM

#define MAXRECORD 10              // Số lượng thẻ RFID lưu trữ tối đa
byte CurrentRFID[4];              // Lưu ID thẻ đang quét
byte RFIDData[MAXRECORD][4] = {0}; // Lưu danh sách thẻ

#define pinRST D1                // Chân RST RFID
#define pinSDA D2                // Chân SDA RFID

#define SW_PIN 0                 // Chân nút nhấn xóa WiFi
#define LED 2                    // Onboard LED

#define CMDWIFI 0xB5             // Lệnh báo WiFi
#define CMDRFIDLOGIN1 0xB6       // Lệnh đăng nhập RFID ID1
#define CMDRFIDLOGIN2 0xB7       // Lệnh đăng nhập RFID ID2
#define CMDRFIDLOGIN3 0xB8       // Lệnh đăng nhập RFID ID3
#define CMDRFIDEXIT 0xB9         // Lệnh RFID thoát

byte LastPumpCount=0;             // Lưu trạng thái bơm trước
byte LastChargeCount=0;           // Lưu trạng thái sạc trước
MFRC522 MyRFID(pinSDA, pinRST);   // Khởi tạo RFID

unsigned long TimeCheckRFID=0;    // Thời gian kiểm tra RFID
unsigned long TimeResetRFID=0;    // Thời gian reset trạng thái RFID

const char* host = "script.google.com"; // Host Google Apps Script
const int httpsPort = 443;               // Port HTTPS
WiFiClientSecure client;                 // Khởi tạo client HTTPS
String SCRIPT_ID = "AKfycbyCha5bqvvbb5uu0MlQSI37e4OUlGUQyC8UJ0V6nsQS086cYNc2_rfm4Ist_G6KAWdHCA"; // ID Script

#define DataSerial Serial       // Serial dùng nhận dữ liệu ESP

byte UARTByteCount;               // Đếm byte UART
byte UARTBuffer[50];              // Buffer UART

int Voltage=0;                    // Điện áp
float Current=0;                  // Dòng điện
float Power=0;                    // Công suất
int Energy=0;                     // Năng lượng

byte PumpCount=0;                 // Số lần bơm
byte ChargeCount=0;               // Số lần sạc

int MinuteCharge=0;               // Thời gian sạc
int ChargeMonney=0;               // Tiền sạc

byte CurrentLogInID = 0;          // ID đăng nhập hiện tại
byte CheckLogInID = 0;            // ID kiểm tra
byte ExitLogInID = 0;             // ID thoát
byte RFIDActive = 0;              // Trạng thái RFID đang đọc

// ---------------- EEPROM ----------------
void WriteDataEEPROM() {          // Ghi dữ liệu RFID vào EEPROM
  for (int i = 0; i < MAXRECORD; i++) {
    EEPROM.write(i*4 + 0, RFIDData[i][0]); // byte 0
    EEPROM.write(i*4 + 1, RFIDData[i][1]); // byte 1
    EEPROM.write(i*4 + 2, RFIDData[i][2]); // byte 2
    EEPROM.write(i*4 + 3, RFIDData[i][3]); // byte 3
  }
  EEPROM.commit();                // Lưu vào EEPROM
}

void ReadDataEEPROM() {           // Đọc dữ liệu từ EEPROM
  byte EEPROMData = EEPROM.read(0); 
  if (EEPROMData == 0xFF) {       // Nếu chưa lưu
    WriteDataEEPROM();             // Ghi mặc định
  } else {
    for (int i = 0; i < MAXRECORD; i++) {
      RFIDData[i][0] = EEPROM.read(i*4 + 0); 
      RFIDData[i][1] = EEPROM.read(i*4 + 1); 
      RFIDData[i][2] = EEPROM.read(i*4 + 2); 
      RFIDData[i][3] = EEPROM.read(i*4 + 3); 
    }
  }
}

void SaveCardCode() {             // Ghi thẻ mặc định
  RFIDData[0][0]=0x61; RFIDData[0][1]=0x12; RFIDData[0][2]=0xA7; RFIDData[0][3]=0x17; 
  RFIDData[1][0]=0x51; RFIDData[1][1]=0xEB; RFIDData[1][2]=0x61; RFIDData[1][3]=0x17; 
  RFIDData[2][0]=0x60; RFIDData[2][1]=0xCB; RFIDData[2][2]=0x60; RFIDData[2][3]=0x61; 
  WriteDataEEPROM();               // Lưu vào EEPROM
}

byte VerifyRFID() {               // Kiểm tra thẻ
  for (int i=0; i<MAXRECORD; i++) {
    if (RFIDData[i][0]==CurrentRFID[0] &&
        RFIDData[i][1]==CurrentRFID[1] &&
        RFIDData[i][2]==CurrentRFID[2] &&
        RFIDData[i][3]==CurrentRFID[3]) {
      return i+1;                 // Trả ID +1
    }
  }
  return 0;                        // Không tìm thấy
}

void sendData(String Type, int Among, int Voltage, float Current, float Power, int ChargeTime) { // Gửi Google Sheet
  Serial.println("\n=== Sending to Google Sheets ==="); // Log
  String url = "/macros/s/" + SCRIPT_ID + "/exec?" + // URL
               "Type=" + Type +
               "&Among=" + String(Among) +
               "&Voltage=" + String(Voltage) +
               "&Current=" + String(Current) +
               "&Power=" + String(Power) +
               "&ChargeTime=" + String(ChargeTime) +
               "&ID=" + String(CurrentLogInID);

  bool saved = false;
  client.setInsecure();             // Bỏ kiểm tra chứng chỉ

  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("🔄 Attempt %d...\n", attempt); 

    if (!client.connect(host, httpsPort)) {
      Serial.println("❌ Connect failed"); 
    } else {
      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: script.google.com\r\n" +
                   "User-Agent: ESP8266\r\n" +
                   "Connection: close\r\n\r\n");

      unsigned long timeout = millis();
      while (millis() - timeout < 5000) {
        if (client.available()) {
          String line = client.readStringUntil('\n'); 
          Serial.println(line);
          if (line.startsWith("HTTP/1.1 302") || line.startsWith("HTTP/1.1 200")) saved = true;
        }
        if (!client.connected()) break;
      }
      client.stop();
    }

    if (saved) { Serial.println("✅ Send success!"); break; }
    Serial.println("⏳ Retry in 1s..."); delay(1000);
  }

  if (!saved) Serial.println("⚠️ FAILED after 5 attempts!");
}

void UARTRead() {                  // Đọc dữ liệu từ ESP
  byte UARTDataCome;
  if (DataSerial.available()>0) {
    UARTDataCome=DataSerial.read(); 
    Serial.write(UARTDataCome); 
    if(UARTDataCome==0xFE) { UARTByteCount=0; } // Start frame
    else if(UARTDataCome==0xFD) { UARTByteCount=0; } // End frame short
    else if(UARTDataCome==0xFC) { // End frame full
      Voltage=UARTBuffer[0]*1000+UARTBuffer[1]*100+UARTBuffer[2]*10+UARTBuffer[3]; // Voltage
      Current=(UARTBuffer[4]*1000+UARTBuffer[5]*100+UARTBuffer[6]*10+UARTBuffer[7])/10.0; // Current
      Power=(UARTBuffer[8]*1000+UARTBuffer[9]*100+UARTBuffer[10]*10+UARTBuffer[11])/10.0; // Power
      Energy=UARTBuffer[12]*1000+UARTBuffer[13]*100+UARTBuffer[14]*10+UARTBuffer[15]; // Energy
      PumpCount=UARTBuffer[16]; ChargeCount=UARTBuffer[17]; // Count
      MinuteCharge=UARTBuffer[18]*1000+UARTBuffer[19]*100+UARTBuffer[20]*10+UARTBuffer[21]; // Charge time
      ChargeMonney=UARTBuffer[22]*1000+UARTBuffer[23]*100+UARTBuffer[24]*10+UARTBuffer[25]; // Charge money
      if(LastPumpCount!=PumpCount) sendData("BOM", 2000, Voltage, Current, Power, 0); // Pump changed
      if(LastChargeCount!=ChargeCount) sendData("SAC", ChargeMonney, Voltage, Current, Power, MinuteCharge); // Charge changed
      LastPumpCount=PumpCount; LastChargeCount=ChargeCount;
    } else {
      UARTBuffer[UARTByteCount]=UARTDataCome; UARTByteCount++;
      if(UARTByteCount==50) UARTByteCount=0; // Prevent overflow
    }
  }     
}

void CheckRFID() {                // Kiểm tra thẻ RFID mới
  if (MyRFID.PICC_IsNewCardPresent()) {
    Serial.print("RFID detected!");
    if (MyRFID.PICC_ReadCardSerial() && RFIDActive==0) {
      Serial.print("RFID TAG ID:");
      for (byte i=0; i<MyRFID.uid.size; ++i) {
        Serial.print(MyRFID.uid.uidByte[i], HEX); Serial.print(" "); 
        CurrentRFID[i]=MyRFID.uid.uidByte[i]; 
      }
      Serial.println();
      RFIDActive=1;
      CheckLogInID=VerifyRFID(); 

      if(CurrentLogInID==0 && CheckLogInID!=0) { // Login
        CurrentLogInID=CheckLogInID; ExitLogInID=0;
        if(CurrentLogInID==1) Serial.write(CMDRFIDLOGIN1);
        else if(CurrentLogInID==2) Serial.write(CMDRFIDLOGIN2);
        else if(CurrentLogInID==3) Serial.write(CMDRFIDLOGIN3);
      } else if(CurrentLogInID!=0 && CurrentLogInID==CheckLogInID) { // Logout
        ExitLogInID=CurrentLogInID; Serial.write(CMDRFIDEXIT);
        sendData("EXIT", 0, 0, 0, 0, 0); CurrentLogInID=0;
      }
    }
    MyRFID.PICC_HaltA();          // Halt card
    MyRFID.PCD_StopCrypto1();     // Stop encryption
  }
}

void setup() {                     // Setup ban đầu
  Serial.begin(9600);              // Serial máy tính
  pinMode(SW_PIN, INPUT_PULLUP);   // Nút xóa WiFi
  pinMode(LED, OUTPUT);            // LED onboard
  UARTByteCount=0;                 // Reset UART count

  WiFiManager wifiManager;         // Khởi tạo WiFiManager
  Serial.println("Delete old wifi? Press Flash button within 3 second");
  for(int i=3;i>0;i--){ Serial.print(String(i)+" "); delay(1000); } // Đếm ngược
  if(digitalRead(SW_PIN)==LOW){    // Nếu nhấn nút
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(200); // Nhấp nháy LED
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(200);
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(200);
    digitalWrite(LED,LOW);
    wifiManager.resetSettings();    // Xóa WiFi cũ
  }

  wifiManager.autoConnect("SMART SHEET","12345678"); // Tự connect WiFi
  Serial.println("YOU ARE CONNECTED TO WIFI");

  if(WiFi.status() == WL_CONNECTED){
    Serial.println("WIFI CONNECTED SUCCESSFULLY!");
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(200); // Nháy LED
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(200);
    digitalWrite(LED,LOW); delay(200); digitalWrite(LED,HIGH); delay(1000);
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
    client.setInsecure();           // Bỏ SSL check
    delay(10000); Serial.write(CMDWIFI); // Gửi lệnh WiFi ok
  } else { digitalWrite(LED,LOW); } // Nếu WiFi fail

  EEPROM.begin(512);                 // Init EEPROM
  SPI.begin();                        // Init SPI
  MyRFID.PCD_Init();                  // Init RFID
  MyRFID.PCD_SetAntennaGain(MyRFID.RxGain_max); 
  MyRFID.PCD_SetRegisterBitMask(MyRFID.RFCfgReg, (0x07<<4)); // Max gain
  Serial.print("MFRC522 Version: ");
  byte version = MyRFID.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.println(version, HEX);
  if(version==0x00||version==0xFF) Serial.println("ERROR: MFRC522 not detected. Check wiring and power!");
  else Serial.println("MFRC522 detected OK!");
  SaveCardCode();                    // Ghi thẻ mặc định
  ReadDataEEPROM();                  // Đọc EEPROM
  TimeCheckRFID=millis(); TimeResetRFID=millis(); // Init timer
}

void loop() {                        // Vòng lặp chính
  UARTRead();                        // Đọc dữ liệu từ ESP

  if(millis()-TimeResetRFID>2000){ RFIDActive=0; TimeResetRFID=millis(); } // Reset flag
  if(millis()-TimeCheckRFID>500){ CheckRFID(); TimeCheckRFID=millis(); }   // Kiểm tra RFID
}
