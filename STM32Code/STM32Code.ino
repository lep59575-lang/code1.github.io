#include <PZEM004Tv30.h>        // Thư viện đọc module PZEM
#include <Wire.h>               // Thư viện I2C
#include <LiquidCrystal_I2C.h>  // Thư viện điều khiển LCD I2C

#define CMDONPUMP 0xB1          // Lệnh bật bơm từ ESP
#define CMDOFFPUMP 0xB2         // Lệnh tắt bơm từ ESP

#define CMDONCHARGE 0xB3        // Lệnh bật sạc
#define CMDOFFCHARGE 0xB4       // Lệnh tắt sạc

#define CMDWIFI 0xB5            // Lệnh báo WiFi từ ESP
#define CMDRFIDLOGIN1 0xB6      // RFID user 1
#define CMDRFIDLOGIN2 0xB7      // RFID user 2
#define CMDRFIDLOGIN3 0xB8      // RFID user 3

#define CMDRFIDEXIT 0xB9        // RFID logout

#define RELAYPUMP PC15          // Pin relay của bơm
#define RELAYCHARGE PC14        // Pin relay của sạc

#define BUTTONPUMP PB11         // Nút điều khiển bơm
#define BUTTONCHARGE PB10       // Nút điều khiển sạc

#define BUZZER PA11             // Pin buzzer
#define CHARGEFULL PB12         // Pin báo sạc đầy

#define PUMPRICE "2.000"        // Giá tiền bơm mỗi lần

LiquidCrystal_I2C LCDDisplay(0x27,20,4); // LCD 20x4 địa chỉ 0x27

unsigned long TimeCount;        // Biến thời gian gửi dữ liệu
PZEM004Tv30 *Pzem;              // Đối tượng PZEM
float Voltage, Current, Power, Energy; // Thông số điện

byte ChargeState=0;             // Trạng thái đang sạc
byte PumpState=0;               // Trạng thái đang bơm

byte PumpCount=0;               // Số lần bơm đã hoàn tất
int PumpMoney=0;                // Tiền bơm

byte ChargeCount=0;             // Số lần sạc đã hoàn tất

unsigned long ChargeTimeStart=0; // TG bắt đầu sạc
unsigned long ChargeTimeCount=0; // TG sạc tạm
unsigned long ChargeMinute=0;    // Phút sạc
unsigned long ChargeMoney=0;     // Tiền sạc
unsigned long TotalChargeMinute=0; // Tổng phút sạc

byte FullFlag=0;                // Cờ báo đầy
unsigned long TimeFull=0;       // TG kiểm tra đầy

int PumpCountLimit=20;          // Giới hạn lần bơm
int ChargeCountLimit=10;        // Giới hạn lần sạc

byte UARTData=0;                // Dữ liệu serial nhận từ ESP

byte SystemReady=0;             // Hệ thống đã đăng nhập RFID hay chưa
void UpdateDisplay()
{
  Pzem->readAddress();            // Cập nhật địa chỉ PZEM
  Voltage = Pzem->voltage();      // Đọc điện áp
  Current =  Pzem->current();     // Đọc dòng
  Power = Pzem->power();          // Đọc công suất
  Energy = Pzem->energy();        // Đọc điện năng

  if(Power>200) OnBuzzer();       // Nếu quá tải → kêu buzzer
  else OffBuzzer();               // Ngược lại tắt buzzer
}

void DisplayMain()
{
  LCDDisplay.clear();             // Xóa LCD
  LCDDisplay.print("HT BOM HOI SAC DIEN"); // Dòng 1
  LCDDisplay.setCursor(0, 1); 
  LCDDisplay.print("DA:    V   DD:    A"); // Dòng 2
  LCDDisplay.setCursor(0, 2);
  LCDDisplay.print("CS:    W   DN:   KWh"); // Dòng 3
}
void DisplayParameter()
{
  if(Voltage==0)                   // Nếu mất điện
  {
    LCDDisplay.setCursor(0, 3);
    LCDDisplay.print(" MAT NGUON DIEN "); // Báo mất nguồn
    LCDDisplay.setCursor(3, 1);
    LCDDisplay.print("    ");
    LCDDisplay.print((int)Voltage);
  }
  else
  {
      LCDDisplay.setCursor(3, 1);
      LCDDisplay.print("    ");
      LCDDisplay.print((int)Voltage); // Điện áp

      LCDDisplay.setCursor(14, 1);
      LCDDisplay.print("    ");
      LCDDisplay.print(Current,2); // Dòng

      LCDDisplay.setCursor(3, 2);
      LCDDisplay.print("    ");
      LCDDisplay.print(Power,1); // Công suất

      LCDDisplay.setCursor(14, 2);
      LCDDisplay.print("   ");
      LCDDisplay.print((int)Energy ); // Điện năng
  }
}
void OnPump()  { digitalWrite(RELAYPUMP,LOW); }   // Bật bơm
void OffPump() { digitalWrite(RELAYPUMP,HIGH); }  // Tắt bơm

void OnCharge()  { digitalWrite(RELAYCHARGE,LOW);}  // Bật sạc
void OffCharge() { digitalWrite(RELAYCHARGE,HIGH);} // Tắt sạc

void OnBuzzer()  { digitalWrite(BUZZER,HIGH);}    // Bật còi
void OffBuzzer() { digitalWrite(BUZZER,LOW);}     // Tắt còi
void DisplayRunPump()
{
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print("DANG BOM..");   // Đang bơm
}

void DisplayRunCharge()
{
  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print("DANG SAC..");   // Đang sạc
}

void DisplayPumpDone()
{
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print("BOM XONG! ");   // Bơm xong
}

void DisplayPumpLimit()
{
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print("QUA GH BOM");   // Quá giới hạn
}

void DisplayChargeDone()
{
  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print("SAC XONG! ");   // Sạc xong
}

void DisplayChargeLimit()
{
  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print("QUA GH SAC"); // Hết số lần sạc
}
void DisplayPumpMoney()
{
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print("          ");   // Xóa
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print(PUMPRICE);       // Giá bơm cố định
}

void ClearPump()
{
  LCDDisplay.setCursor(0, 3);
  LCDDisplay.print("           ");  // Xóa dòng
}

void DisplayChargeTime()
{
  ChargeMinute = millis()-ChargeTimeStart; // Lấy thời gian sạc
  ChargeMinute = ChargeMinute/120000;      // Quy đổi phút
  TotalChargeMinute=ChargeMinute;          // Lưu tổng phút

  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print("          ");
  LCDDisplay.setCursor(10, 3);

  LCDDisplay.print(ChargeMinute);          // In số phút
  LCDDisplay.print("h");                   // Ký hiệu
}

void DisplayChargeMoney()
{
  if(ChargeMinute<3) ChargeMoney=ChargeMinute*10;    // Tính tiền mức 1
  else if(ChargeMinute<5) ChargeMoney=ChargeMinute*7;// Mức 2
  else ChargeMoney=ChargeMinute*5;                   // Mức 3
  
  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print("          ");
  LCDDisplay.setCursor(10, 3);
  LCDDisplay.print(ChargeMoney);
  LCDDisplay.print(".000");    // Đơn vị tiền
}
void DisplayClearCharge()              // Xóa vùng hiển thị thông tin sạc
{
  LCDDisplay.setCursor(10, 3);         // Đặt con trỏ tại cột 10, dòng 3
  LCDDisplay.print("          ");      // In 10 dấu cách để xóa
}
void SendESPData()                     // Gửi dữ liệu trạng thái sang ESP (gói thường)
{
  int Value=0;                         // Biến tạm để chia tách số

  Value=(int)Voltage;                  // Điện áp (V)
  Serial.write(0xFE);                  // Byte bắt đầu gói dữ liệu
  Serial.write(Value/1000);            // Hàng nghìn
  Serial.write((Value%1000)/100);      // Hàng trăm
  Serial.write((Value%100)/10);        // Hàng chục
  Serial.write(Value%10);              // Hàng đơn vị

  Value=(int)(Current*10);             // Dòng điện nhân 10 (A)
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)(Power*10);               // Công suất nhân 10 (W)
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)Energy;                   // Điện năng (Wh)
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Serial.write(PumpCount);             // Số lần bơm đã dùng
  Serial.write(ChargeCount);           // Số lần sạc đã dùng

  Value=(int)TotalChargeMinute;        // Tổng phút đã sạc
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Serial.write(0xFD);                  // Byte kết thúc gói dữ liệu
}
void SendESPDataDone()                 // Gửi dữ liệu kết thúc 1 phiên bơm/sạc
{
  int Value=0;

  Value=(int)Voltage;                  // Gửi điện áp
  Serial.write(0xFE);
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)(Current*10);             // Gửi dòng điện
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)(Power*10);               // Gửi công suất
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)Energy;                   // Gửi điện năng
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Serial.write(PumpCount);             // Số lần bơm
  Serial.write(ChargeCount);           // Số lần sạc

  Value=(int)TotalChargeMinute;        // Tổng phút sạc
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Value=(int)ChargeMoney;              // Tổng tiền phải trả
  Serial.write(Value/1000);
  Serial.write((Value%1000)/100);
  Serial.write((Value%100)/10);
  Serial.write(Value%10);

  Serial.write(0xFC);                  // Byte kết thúc gói DONE
}
void DisplayWaitWifi()                 // Hiển thị đang chờ WiFi
{
  LCDDisplay.clear();                 
  LCDDisplay.print("CHO KET NOI WIFI...");
  LCDDisplay.setCursor(0, 1);
}
void DisplayWaitforRFID()              // Hiển thị yêu cầu quét RFID
{
  LCDDisplay.clear();
  LCDDisplay.print("HAY QUET THE RFID ");
}
void setup() {
  Serial.begin(9600);                  // Serial chính cho ESP
  Serial2.begin(9600);                 // Serial2 cho PZEM
  Pzem = new PZEM004Tv30(Serial2);     // Khởi tạo PZEM sau khi mở Serial2

  pinMode(RELAYPUMP,OUTPUT);           // Relay máy bơm
  pinMode(RELAYCHARGE,OUTPUT);         // Relay sạc
  pinMode(BUTTONPUMP,INPUT);           // Nút bơm
  pinMode(BUTTONCHARGE,INPUT);         // Nút sạc
  pinMode(BUZZER,OUTPUT);              // Còi
  pinMode(CHARGEFULL,INPUT);           // Cảm biến đầy pin

  OffPump();                           // Tắt bơm lúc đầu
  OffCharge();                         // Tắt sạc lúc đầu
  OffBuzzer();                         // Tắt buzzer

  Wire.begin();                        
  Wire.setClock(50000);                // Tốc độ I2C 50 kHz
  
  LCDDisplay.init();                   
  LCDDisplay.backlight();              // Bật nền LCD

  DisplayWaitWifi();                   // Hiển thị chờ WiFi
  
  Serial.println("START"); 
  
  OnBuzzer();                          // Bíp báo khởi động
  delay(1000);
  OffBuzzer();

  TimeCount=millis();                  // Lưu thời gian bắt đầu
  TimeFull=millis();
}
void loop() {

  if(SystemReady==1)                       // Nếu đã đăng nhập RFID → chạy chế độ chính
  {
      byte Check=digitalRead(CHARGEFULL);  // Đọc tín hiệu cảm biến đầy pin
      delay(1);                            // Delay 1ms để ổn định
    
      if(Check==1)                         // Khi CHARGEFULL = 1 → chưa đầy
      {
        TimeFull=millis();                 // Reset thời gian
        FullFlag=0;                        // Chưa đầy
      }
      if(millis()-TimeCount>5000)          // Cứ 5 giây cập nhật 1 lần
      {
         UpdateDisplay();                  // Cập nhật màn hình chính
         DisplayParameter();               // Hiển thị điện áp, dòng, công suất
         SendESPData();                    // Gửi dữ liệu sang ESP

         if(FullFlag==1)                   // Nếu phát hiện pin đầy
         {
           OnBuzzer();                     // Còi báo
           delay(500);
           OffBuzzer();
         }

         TimeCount=millis();               // Reset timer 5 giây
         FullFlag=1;                       // Mặc định là đầy sau mỗi 5 giây
      }
      if(digitalRead(BUTTONCHARGE)==LOW)   // Nếu nhấn nút sạc
      {
          delay(350);                      // Chống dội phím
          while(digitalRead(BUTTONCHARGE)==LOW);  // Chờ nhả nút

          if(ChargeCount<ChargeCountLimit) // Nếu số lần sạc chưa vượt mức
          {
            if(ChargeState==0)             // Nếu đang TẮT → bật sạc
            {
              OnCharge();
              ChargeState=1;
              ChargeTimeStart=millis();    // Bắt đầu tính thời gian sạc
              DisplayRunCharge();          // Hiển thị trạng thái đang sạc
            }
            else                           // Nếu đang BẬT → tắt sạc
            {
              OffCharge();
              ChargeCount++;               // Tăng số lần sạc
              
              ChargeState=0;               
              DisplayChargeDone();         // Hiển thị đã sạc xong
              delay(1000);
              DisplayChargeTime();         // Hiển thị thời gian sạc
              delay(1000);
              DisplayChargeMoney();        // Hiển thị tiền
              SendESPDataDone();           // Gửi dữ liệu hoàn tất
              delay(2000);
              DisplayClearCharge();        // Xóa vùng hiển thị
            }
          }
          else                              // Nếu vượt giới hạn số lần sạc
          {
            DisplayChargeLimit();           // Báo hết số lần
            delay(2000);
            DisplayClearCharge();
          }
          delay(350);
      }
      if(digitalRead(BUTTONPUMP)==LOW)     // Nếu nhấn nút bơm
      {
          delay(350);
          while(digitalRead(BUTTONPUMP)==LOW);

          if(PumpCount<PumpCountLimit)     // Nếu còn lượt bơm
          {
            if(PumpState==0)               // Đang tắt → bật bơm
            {
              OnPump();
              DisplayRunPump();
              PumpState=1;
            }
            else                           // Đang bật → tắt bơm
            {
              OffPump();
              PumpCount++;                 // Tăng số lần bơm
             
              DisplayPumpDone();
              delay(1000);
              DisplayPumpMoney();          // Hiển thị số tiền bơm
              SendESPDataDone();           // Gửi dữ liệu hoàn tất
              delay(2000);
              ClearPump();                 // Xóa hiển thị
              PumpState=0;
            }
          }
          else                              // Hết lượt bơm
          {
            DisplayPumpLimit();
            delay(2000);
            ClearPump();
          }
      }
      if(Serial.available())               // Nếu có dữ liệu từ ESP gửi xuống
      {
        UARTData=Serial.read();            // Đọc byte lệnh

        if(UARTData==CMDONPUMP)            // Lệnh BẬT bơm
        {
           if(PumpCount<PumpCountLimit)
          {
              OnPump();
              DisplayRunPump();
              PumpState=1;
          }
          else
          {
            DisplayPumpLimit();
            delay(2000);
            ClearPump();
          }
        }
        else  if(UARTData==CMDOFFPUMP)     // Lệnh TẮT bơm
        {
        if(PumpState==1)
        {
                OffPump();
                PumpCount++;
               
                DisplayPumpDone();
                delay(1000);
                DisplayPumpMoney();
                SendESPDataDone();
                delay(2000);
                ClearPump();
                PumpState=0;
        }
        }
        else  if(UARTData==CMDONCHARGE)    // Lệnh BẬT sạc
        {
          if(ChargeCount<ChargeCountLimit)
          {
              OnCharge();
              ChargeState=1;
              ChargeTimeStart=millis();
              DisplayRunCharge();
          }
          else
          {
            DisplayChargeLimit();
            delay(2000);
            DisplayClearCharge();
          }
        }
         else  if(UARTData==CMDOFFCHARGE)  // Lệnh TẮT sạc
        {
          if(ChargeState==1)
          {
                  OffCharge();
                  ChargeCount++;
                  
                  ChargeState=0;
                  DisplayChargeDone();
                  delay(1000);
                  DisplayChargeTime();
                  delay(1000);
                  DisplayChargeMoney();
                  SendESPDataDone();
                  delay(2000);
                  DisplayClearCharge();
          }
        }
        else if(UARTData==CMDRFIDEXIT)     // Người dùng thoát tài khoản RFID
        {
          OnBuzzer();
          delay(200);
          OffBuzzer();
          DisplayWaitforRFID();            // Quay lại màn hình quét thẻ
          SystemReady=0;                   // Tắt chế độ vận hành
        }
      }
  }
  else
  {
    if(Serial.available())                 // Nhận lệnh từ ESP
      {
        UARTData=Serial.read();

        if(UARTData==CMDWIFI)              // Yêu cầu chờ kết nối WiFi
        {
          DisplayWaitforRFID();
        }
        else if(UARTData==CMDRFIDLOGIN1)   // RFID loại 1 đăng nhập
        {
          LCDDisplay.print("1");
          OnBuzzer();
          delay(200);
          OffBuzzer();
          delay(1000);
          DisplayMain();                   // Vào giao diện chính
          SystemReady=1;
        }
        else if(UARTData==CMDRFIDLOGIN2)   // RFID loại 2 đăng nhập
        {
          LCDDisplay.print("2");
          OnBuzzer();
          delay(200);
          OffBuzzer();
          delay(1000);
          DisplayMain();
          SystemReady=1;
          
        }
        else if(UARTData==CMDRFIDLOGIN3)   // RFID loại 3 đăng nhập
        {
          LCDDisplay.print("3");
          OnBuzzer();
          delay(200);
          OffBuzzer();
          delay(1000);
          DisplayMain();
          SystemReady=1;
        }
      }
  }
}
