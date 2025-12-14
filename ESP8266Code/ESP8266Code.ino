// Khai báo thông tin template của Blynk để định danh project
#define BLYNK_TEMPLATE_ID "TMPL6zx1qEG0W"      // ID Template Blynk
#define BLYNK_TEMPLATE_NAME "SMARTSTATION"      // Tên Template
#define BLYNK_AUTH_TOKEN "2bWLLl0gkWKDdTgyfyFuQNznKPkVxZcT"  // Token xác thực thiết bị

// Khai báo các thư viện cần dùng
#include <SoftwareSerial.h>        // Tạo cổng Serial mềm SRX/STX để giao tiếp với Arduino
#include <ESP8266WiFi.h>           // WiFi của ESP8266
#include <DNSServer.h>             // Dùng bởi WiFiManager (DNS local)
#include <ESP8266WebServer.h>      // Dùng để tạo WebServer tạm cấu hình WiFi
#include <WiFiManager.h>           // Tự động tạo AP cấu hình WiFi
#include <BlynkSimpleEsp8266.h>    // Thư viện Blynk cho ESP8266

// Mã lệnh điều khiển PUMP và CHARGE gửi sang Arduino
#define CMDONPUMP 0xB1             // Lệnh bật bơm
#define CMDOFFPUMP 0xB2            // Lệnh tắt bơm
#define CMDONCHARGE 0xB3           // Lệnh bật sạc
#define CMDOFFCHARGE 0xB4          // Lệnh tắt sạc

// Khai báo chân UART mềm SoftwareSerial
#define SRX D5   // ESP8266 nhận dữ liệu từ Arduino (SoftwareSerial RX)
#define STX D6   // ESP8266 gửi dữ liệu sang Arduino (SoftwareSerial TX)

#define BLYNK_DEBUG                 // Bật chế độ debug của Blynk
#define BLYNK_PRINT Serial          // In toàn bộ log Blynk lên Serial chính

#define SW_PIN 0                    // Chân nút Flash (GPIO0) dùng để reset WiFi
#define LED 2                       // LED on-board (GPIO2)

// Lưu token Blynk vào biến
char blynk_token[] = BLYNK_AUTH_TOKEN;   // Token Blynk mặc định

// Tạo đối tượng Serial mềm SRX/STX
SoftwareSerial DataSerial(SRX, STX);     // DataSerial sẽ giao tiếp với Arduino

// Biến lưu số byte đã nhận qua UART
byte UARTByteCount;                      // Đếm byte trong buffer
byte UARTBuffer[50];                     // Mảng chứa dữ liệu UART nhận từ Arduino

// Tạo 2 timer của Blynk
BlynkTimer UARTTimer;                    // Timer đọc UART liên tục
BlynkTimer BlynkReconnectTimer;          // Timer thử kết nối lại Blynk mỗi giây

// Các biến lưu dữ liệu đo được từ Arduino
int Voltage=0;                           // Điện áp
float Current=0;                         // Dòng điện
float Power=0;                           // Công suất
int Energy=0;                            // Điện năng
byte PumpCount=0;                        // Số lần bật bơm
byte ChargeCount=0;                      // Số lần bật sạc
int MinuteCharge=0;                      // Tổng phút đã sạc

//------------------------------------------------------------
// HÀM ĐỌC UART TỪ ARDUINO — ĐƯỢC GỌI LIÊN TỤC 1ms/lần
//------------------------------------------------------------
void UARTRead()
{
   byte UARTDataCome;                   // Biến lưu 1 byte nhận được

   if (DataSerial.available()>0)        // Nếu Serial mềm có dữ liệu
   {
      UARTDataCome = DataSerial.read(); // Đọc 1 byte từ Arduino
      Serial.write(UARTDataCome);       // Gửi ra Serial để debug

      if(UARTDataCome == 0xFE)          // Nếu nhận được byte bắt đầu frame
      {
        UARTByteCount = 0;              // Reset bộ đếm buffer
      }
      
      else if(UARTDataCome == 0xFD || UARTDataCome == 0xFC) // Byte kết thúc frame
      {
        // Giải mã dữ liệu trong buffer
        Voltage = UARTBuffer[0]*1000 + UARTBuffer[1]*100 + UARTBuffer[2]*10 + UARTBuffer[3];
        Current = UARTBuffer[4]*1000 + UARTBuffer[5]*100 + UARTBuffer[6]*10 + UARTBuffer[7];
        Current /= 10;                  // Dữ liệu dòng chia 10
        Power = UARTBuffer[8]*1000 + UARTBuffer[9]*100 + UARTBuffer[10]*10 + UARTBuffer[11];
        Power /= 10;                    // Dữ liệu công suất chia 10
        Energy = UARTBuffer[12]*1000 + UARTBuffer[13]*100 + UARTBuffer[14]*10 + UARTBuffer[15];
        PumpCount = UARTBuffer[16];     // Số lần bật bơm
        ChargeCount = UARTBuffer[17];   // Số lần bật sạc
        MinuteCharge = UARTBuffer[18]*1000 + UARTBuffer[19]*100 + UARTBuffer[20]*10 + UARTBuffer[21];

        // Gửi dữ liệu lên Blynk
        Blynk.virtualWrite(V0,Voltage);
        Blynk.virtualWrite(V1,Current);
        Blynk.virtualWrite(V2,Power);
        Blynk.virtualWrite(V3,Energy);
        Blynk.virtualWrite(V4,PumpCount);
        Blynk.virtualWrite(V5,ChargeCount);
        Blynk.virtualWrite(V8,MinuteCharge);
      }

      // Nếu không phải start/frame byte thì lưu vào buffer
      else
      {
          UARTBuffer[UARTByteCount]=UARTDataCome;   // Lưu byte vào mảng
          UARTByteCount++;                          // Tăng số byte đã nhận

          if(UARTByteCount == 50)                   // Tránh tràn mảng
              UARTByteCount = 0;
      }
   }

   // Nếu có dữ liệu từ Serial Monitor → gửi sang Arduino
   if(Serial.available())
   {
    UARTDataCome = Serial.read();      // Đọc byte từ PC
    DataSerial.write(UARTDataCome);    // Gửi sang Arduino
   }
}

//------------------------------------------------------------
// XỬ LÝ NÚT BLYNK V6 (Điều khiển bơm)
//------------------------------------------------------------
BLYNK_WRITE(V6) 
{
  int Value = param.asInt();           // Lấy giá trị từ nút (0/1)
  if(Value==1) 
    DataSerial.write(CMDONPUMP);       // Gửi lệnh bật bơm
  else 
    DataSerial.write(CMDOFFPUMP);      // Gửi lệnh tắt bơm
}

//------------------------------------------------------------
// XỬ LÝ NÚT BLYNK V7 (Điều khiển sạc)
//------------------------------------------------------------
BLYNK_WRITE(V7) 
{
  int Value = param.asInt();           // Lấy giá trị từ nút (0/1)
  if(Value==1) 
    DataSerial.write(CMDONCHARGE);     // Gửi lệnh bật sạc
  else 
    DataSerial.write(CMDOFFCHARGE);    // Gửi lệnh tắt sạc
}

//------------------------------------------------------------
// HÀM TỰ ĐỘNG KẾT NỐI LẠI BLYNK
//------------------------------------------------------------
void BlynkReconnect()
{
  if(Blynk.connected()==0)             // Nếu chưa kết nối
  {
    Blynk.connect(3333);               // Thử kết nối lại với timeout 3s
  }
}

//------------------------------------------------------------
// SETUP() — CHẠY 1 LẦN KHI KHỞI ĐỘNG
//------------------------------------------------------------
void setup() {
  Serial.begin(9600);                  // Khởi động Serial chính để debug
  
  pinMode(SW_PIN,INPUT_PULLUP);        // Nút reset WiFi (active LOW)
  pinMode(LED,OUTPUT);                 // LED báo trạng thái
  UARTByteCount=0;                     // Reset bộ đếm UART buffer

  WiFiManager wifiManager;             // Tạo đối tượng WiFiManager

  Serial.println("Delete old wifi? Press Flash button within 3 second");

  // Đếm ngược 3 giây cho người dùng bấm nút reset WiFi
  for(int i=3;i>0;i--)
  {
    Serial.print(String(i)+" ");       // In số đếm
    delay(1000);
  }

  // Kiểm tra nếu người dùng giữ nút Flash → reset WiFi
  if(digitalRead(SW_PIN)==LOW)
  {
   Serial.println();
   Serial.println("Reset wifi config!");
   
   // Nháy LED báo hiệu reset
   digitalWrite(LED,LOW); delay(200);
   digitalWrite(LED,HIGH); delay(200);
   digitalWrite(LED,LOW); delay(200);
   digitalWrite(LED,HIGH); delay(200);
   digitalWrite(LED,LOW); delay(200);
   digitalWrite(LED,HIGH); delay(200);
   digitalWrite(LED,LOW);

   wifiManager.resetSettings();        // XÓA WiFi lưu trong flash
  }

  // Cho phép nhập token Blynk qua portal WiFi
  WiFiManagerParameter custom_blynk_token("Blynk", "blynk token", blynk_token, 34);
  wifiManager.addParameter(&custom_blynk_token);

  // Tạo AP tên "SMART STATION" để cấu hình WiFi
  wifiManager.autoConnect("SMART STATION","12345678");

  Serial.println("YOU ARE CONNECTED TO WIFI");       // WiFi ok
  Serial.println(custom_blynk_token.getValue());     // In token người dùng nhập

  Blynk.config(custom_blynk_token.getValue());       // Cấu hình Blynk

  if (WiFi.status() == WL_CONNECTED)                 // Nếu WiFi OK
  {
    Serial.println("WIFI CONNECTED SUCCESSFULLY! NOW TRY TO CONNECT BLYNK...");

    // Nháy LED báo trạng thái kết nối
    digitalWrite(LED,LOW); delay(200);
    digitalWrite(LED,HIGH); delay(200);
    digitalWrite(LED,LOW); delay(200);
    digitalWrite(LED,HIGH); delay(200);
    digitalWrite(LED,LOW); delay(200);
    digitalWrite(LED,HIGH); delay(1000);

    Blynk.connect(3333);                              // Thử kết nối Blynk

    if(Blynk.connected())                             // Nếu thành công
    {
      Serial.println("BLYNK CONNECTED! System ready");
      // Nháy LED nhiều lần báo thành công
      digitalWrite(LED,LOW); delay(200);
      digitalWrite(LED,HIGH); delay(200);
      digitalWrite(LED,LOW); delay(200);
      digitalWrite(LED,HIGH); delay(200);
      digitalWrite(LED,LOW); delay(200);
      digitalWrite(LED,HIGH); delay(1000);
    }
    else
    {
      Serial.println(" BLYNK Not connect. warning!");
      digitalWrite(LED,LOW);
    }
  }
  else
  {
    Serial.println("WIFI Not connect. warning!");
    digitalWrite(LED,LOW);
  }

  // Khởi động UART mềm để giao tiếp với Arduino
  DataSerial.begin(9600);

  // Đọc UART mỗi 1ms
  UARTTimer.setInterval(1L,UARTRead);

  // Tự retry kết nối Blynk mỗi 1 giây
  BlynkReconnectTimer.setInterval(1000L,BlynkReconnect);
}

//------------------------------------------------------------
// LOOP() — CHẠY MÃI MÃI
//------------------------------------------------------------
void loop() {
  Blynk.run();                   // Giữ Blynk hoạt động
  UARTTimer.run();               // Xử lý đọc UART
  BlynkReconnectTimer.run();     // Xử lý reconnect Blynk
}
