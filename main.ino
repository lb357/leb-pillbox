#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266FtpServer.h>
#include <ESP8266WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DS3231.h>
#include <FastBot.h>
#include <GyverStepper2.h>


#define AP_SSID "leb-pillbox"
#define AP_PASS "lpboxauth"
bool server_mode = false;
String ip = "";
ESP8266WebServer server(80);
String tg_token = "";
FastBot bot;
String tg_id = "";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define NUMFLAKES 10
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int gmt_offset = 3;
int lastMinute = -1;
String lastCMD = "";
RTClib myRTC_get;
DS3231 myRTC_set;

int morning_h = -1;
int morning_m = -1;
int evening_h = -1;
int evening_m = -1;

#define SLOTS 15
#define STEPS 2039
GStepper2<STEPPER4WIRE> stepper(STEPS, D8, D6, D7, D5);
int current_slot = 0;
bool slot_change = false;
int slot_minute = -1;
int cup_full = 0;



void dprintln(String data, float text_size = 1.0, bool text_centered = false) {
  if (text_centered) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setTextSize(text_size);
    display.getTextBounds(data, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(WHITE);
    //display.setCursor(SCREEN_WIDTH/4 - data.length()*(text_size/2), display.getCursorY());
    display.setCursor((SCREEN_WIDTH - w) / 2, display.getCursorY());
    display.cp437(true);
    display.println(data);
    display.display();
  } else {
    display.setTextSize(text_size);
    display.setTextColor(WHITE);
    display.cp437(true);
    display.println(data);
    display.display();
  }
}

void check_reset_data(bool noButton = false) {
  delay(200);
  if ((digitalRead(0) == LOW) | noButton) {
    delay(1000);
    Serial.println("Reset");
    display.clearDisplay();
    display.display();
    display.setCursor(0, 0);
    dprintln(utf8rus("Сброс данных..."));
    cfg_save("wifi_ssid", "");
    cfg_save("wifi_password", "");
    cfg_save("tg_token", "");
    cfg_save("tg_id", "");
    cfg_save("gmt_offset", "3");
    cfg_save("morning_h", "-1");
    cfg_save("morning_m", "-1");
    cfg_save("evening_h", "-1");
    cfg_save("evening_m", "-1");
    cfg_save("current_slot", "0");
    cfg_save("cup_full", "0");
    delay(1000);
    ESP.restart();
  }
  delay(100);
}

void setup() {
  pinMode(0, INPUT_PULLUP);
  SPIFFS.begin();
  Serial.begin(115200);

  check_reset_data();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();

  delay(1000);
  dprintln("LeB-PILLBOX");
  dprintln("");
  dprintln(utf8rus("Запуск..."));
  delay(1000);

  check_reset_data();

  String wifi_ssid = cfg_load("wifi_ssid");
  String wifi_password = cfg_load("wifi_password");
  tg_token = cfg_load("tg_token");
  tg_id = cfg_load("tg_id");
  gmt_offset = cfg_load("gmt_offset").toInt();
  morning_h = cfg_load("morning_h").toInt();
  morning_m = cfg_load("morning_m").toInt();
  evening_h = cfg_load("evening_h").toInt();
  evening_m = cfg_load("evening_m").toInt();
  current_slot = cfg_load("current_slot").toInt();
  cup_full = cfg_load("cup_full").toInt();

  delay(100);

  Serial.println("Connecting to ");
  Serial.println(wifi_ssid);
  if (wifi_ssid != "") {
    dprintln(utf8rus("Подключение к WI-FI:"));
    dprintln(wifi_ssid);
    WiFi.begin(wifi_ssid, wifi_password);
    int wifi_status_count = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
      dprint(".");
      wifi_status_count++;
      if (wifi_status_count > 30) {
        WiFi.disconnect(true);
        server_mode = true;
        Serial.println("Connecting error!");
        dprintln(utf8rus("Ошибка подключения!"));
        break;
      }
    }
  } else {
    dprintln(utf8rus("Запуск точки доступа WI-FI..."));
    Serial.println("No ssid error!");
    server_mode = true;
  }

  Serial.println("server mode");
  Serial.println(server_mode);

  check_reset_data();

  String ip = "";
  if (server_mode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(500);
    Serial.println("Access point mode!");
    server.on("/", handleConnection);
    server.begin();
    ip = WiFi.softAPIP().toString();
    Serial.println(AP_SSID);
    Serial.println(AP_PASS);
    dprintln(utf8rus("Необходимо настроить Pillbox!"));
    delay(1000);
    display.clearDisplay();
    display.display();
    delay(1000);
    display.setCursor(0, 0);
    dprintln(utf8rus("НАСТРОЙКА"));
    dprintln(utf8rus("Подключитесь к WI-FI"));
    dprintln(utf8rus("SSID: ") + AP_SSID);
    dprintln(utf8rus("Пароль: ") + AP_PASS);
    dprintln(utf8rus("Зайдите на сайт:"));
    dprintln("http://" + ip + "/");
    dprintln(utf8rus("Выполните инструкции на сайте"));
  } else {
    ip = WiFi.localIP().toString();
    dprintln("");
    dprintln(utf8rus("Подключено!"));
    update_time();
    bot.setToken(tg_token);
    bot.attach(handleTgMessage);
    delay(100);
    stepper.setMaxSpeed(150);
    stepper.setAcceleration(150);
    bot.sendMessage("LeB-Pillbox запущен!\n/help - для справки", tg_id);
    delay(400);
  }

  Serial.println("IP:");
  Serial.println(ip);
  pinMode(D0, OUTPUT);
  digitalWrite(D0, HIGH);
  check_reset_data();
  Serial.println("BL-Sensor init...");
  delay(500);
  pinMode(D4, INPUT);
}

void loop() {
  DateTime now = myRTC_get.now();
  if (server_mode) {
    server.handleClient();
  } else if (digitalRead(D4) == LOW) {
    display.clearDisplay();
    display.display();
    display.setCursor(0, 0);
    dprintln(utf8rus("ВСТАВЬТЕ"), 2, true);
    dprintln(utf8rus("СТАКАН"), 2, true);
    dprintln(utf8rus("!!!"), 2, true);
    if (tg_id != "" && cup_full == 1) {
      bot.sendMessage(String(now.hour()) + ":" + String(now.minute()) + " стакан взят\nВставьте стакан", tg_id);
    }
    if (tg_id != "" && slot_change) {
      bot.sendMessage("Слот не может быть изменён, так как стакан отсутствует в таблетнице!!!", tg_id);
    }
    cup_full = 0;
    cfg_save("cup_full", String(cup_full));
    lastMinute = -1;
  } else if (slot_minute != now.minute() && ((now.hour() == morning_h && now.minute() == morning_m) || (now.hour() == evening_h && now.minute() == evening_m))) {
    slot_change = true;
    slot_minute = now.minute();
  } else if (now.minute() != lastMinute) {
    if (cup_full) {
      display.clearDisplay();
      display.display();
      display.setCursor(0, 0);
      dprintln(utf8rus("ВОЗЬМИТЕ"), 2, true);
      dprintln(utf8rus("СТАКАН"), 2, true);
      dprintln(utf8rus("!!!"), 2, true);
      lastMinute = now.minute();
      if (tg_id != "") {
        bot.sendMessage("Возьмите стакан!", tg_id);
        if (slot_change) { bot.sendMessage("Слот не может быть изменён, так как таблетки с прошлой выдачи всё ещё находятся в стакане!!!", tg_id); }
      }
    } else {
      display.clearDisplay();
      display.display();
      display.setCursor(0, 0);
      dprintln(utf8rus("LeB"), 1, true);
      dprintln(utf8rus("PILLBOX"), 1, true);
      dprintln(String(now.hour()) + ":" + String(now.minute()), 2, true);
      lastMinute = now.minute();
      //dprintln(utf8rus(""), 1, true);
      dprintln(utf8rus("Выбран слот: " + String(current_slot)), 1, true);

      if (tg_id == "") {
        dprintln(utf8rus("Нет Telegram"), 1, true);
      } else {
        dprintln("", 1, true);
        if (morning_h != -1) {
          dprintln(utf8rus("Утренний приём: " + String(morning_h) + ":" + String(morning_m)), 1, true);
        }
        if (evening_h != -1) {
          dprintln(utf8rus("Вечерний приём: " + String(evening_h) + ":" + String(evening_m)), 1, true);
        }
        if (slot_change) {
          if (current_slot < (SLOTS)) {
            //bot.sendMessage("Выбран следующий слот", tg_id);
            current_slot++;
            cfg_save("current_slot", String(current_slot));
            lastMinute = -1;
            slot_change = true;
          } else {
            bot.sendMessage("Выбран последний слот, заполните слоты", tg_id);
          }
          bot.sendMessage("Выбран слот: " + String(current_slot), tg_id);
          setStepperPos(current_slot * (360 / SLOTS));
          if (current_slot > 0) {
            cup_full = 1;
            cfg_save("cup_full", String(cup_full));
          }
          slot_change = false;
          /*while (myRTC_get.now().minute() == now.minute()) {
          delay(100);
        }*/
        }
      }
    }
  }
  bot.tick();
  check_reset_data();
}





void handleConnection() {
  Serial.println("Connection handled");
  if (server.hasArg("ssid") and server.hasArg("password") and server.hasArg("tg")) {  // параметр не найден
    if (server.arg("ssid") != "" and server.arg("tg") != "") {
      server.send(200, "text/html", "<!DOCTYPE html><html><head><title>LeB-PillBox</title><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"></head><body><h1>LeB-PillBox</h1><p>Попытка подключения, ожидайте перезапуска...</p></body></html>");
      cfg_save("wifi_ssid", server.arg("ssid"));
      cfg_save("wifi_password", server.arg("password"));
      cfg_save("tg_token", server.arg("tg"));
      delay(100);
      ESP.restart();
    } else {
      server.send(200, "text/html", "<!DOCTYPE html><html><head><title>LeB-PillBox</title><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"></head><body><h1>LeB-PillBox</h1><p>Ошибка: некорректные входные данные, повторите попытку!</p></body></html>");
    }
  } else {  // параметр найден
    File file = SPIFFS.open("/index.html", "r");
    String HTML_FORM_STRING = file.readString();
    file.close();
    server.send(200, "text/html", HTML_FORM_STRING);
  }
}

void handleTgMessage(FB_msg& msg) {
  /*display.clearDisplay();
  display.display();
  display.setCursor(0, 0);
  dprintln(utf8rus(msg.username));
  dprintln(utf8rus(msg.text));
  dprintln(utf8rus(msg.chatID));*/
  if (tg_id == "") {
    tg_id = msg.chatID;
    cfg_save("tg_id", tg_id);
  }

  if (tg_id == msg.chatID) {
    if (lastCMD == "/gmt") {
      gmt_offset = msg.text.toInt();
      cfg_save("gmt_offset", String(gmt_offset));
      update_time();
      lastCMD = "";
      lastMinute = -1;
      bot.sendMessage("Часовой пояс обновлён (" + String(gmt_offset) + ")", tg_id);
    } else if (lastCMD == "/mh") {
      morning_h = msg.text.toInt();
      if ((morning_h > -1) && (morning_h < 24)) {
        cfg_save("morning_h", String(morning_h));
        lastCMD = "/mm";
        bot.sendMessage("Введите минуту утреннего приёма:", tg_id);
      } else if (morning_h == -1) {
        morning_m = 0;
        cfg_save("morning_m", String(morning_m));
        lastCMD = "";
        bot.sendMessage("Утренний приём отключён!", tg_id);
        lastMinute = -1;
      } else {
        bot.sendMessage("Некорректный формат времени", tg_id);
      }
    } else if (lastCMD == "/mm") {
      morning_m = msg.text.toInt();
      if ((morning_m > -1) && (morning_m < 60)) {
        cfg_save("morning_m", String(morning_m));
        lastCMD = "";
        lastMinute = -1;
        bot.sendMessage("Время приёма установлено на " + String(morning_h) + ":" + String(morning_m), tg_id);
      } else {
        bot.sendMessage("Некорректный формат времени", tg_id);
      }
    } else if (lastCMD == "/eh") {
      evening_h = msg.text.toInt();
      if ((evening_h > -1) && (evening_h < 24)) {
        cfg_save("evening_h", String(evening_h));
        lastCMD = "/em";
        bot.sendMessage("Введите минуту вечернего приёма:", tg_id);
      } else if (evening_h == -1) {
        evening_m = 0;
        cfg_save("evening_m", String(evening_m));
        lastCMD = "";
        bot.sendMessage("Вечерний приём отключён!", tg_id);
        lastMinute = -1;
      } else {
        bot.sendMessage("Некорректный формат времени", tg_id);
      }
    } else if (lastCMD == "/em") {
      evening_m = msg.text.toInt();
      if ((evening_m > -1) && (evening_m < 60)) {
        cfg_save("evening_m", String(evening_m));
        lastCMD = "";
        lastMinute = -1;
        bot.sendMessage("Время приёма установлено на " + String(evening_h) + ":" + String(evening_m), tg_id);
      } else {
        bot.sendMessage("Некорректный формат времени", tg_id);
      }
    } else {
      if (msg.text == "/gmt") {
        bot.sendMessage("Введите часовой пояс\n(Нью-Йорк: -5, Гринвич: 0, Москва: 3):", tg_id);
        lastCMD = "/gmt";
      } else if (msg.text == "/restart") {
        bot.sendMessage("Перезапуск...", tg_id);
        lastCMD = "";
        delay(1000);
        bot.tickManual();
        delay(1000);
        ESP.restart();
      } else if (msg.text == "/reset") {
        bot.sendMessage("Сброс настроек...", tg_id);
        lastCMD = "";
        delay(1000);
        bot.tickManual();
        delay(1000);
        check_reset_data(true);
      } else if (msg.text == "/m" || msg.text == "/morning") {
        bot.sendMessage("Введите час утреннего приёма\n(-1, чтобы отключить приём):", tg_id);
        lastCMD = "/mh";
      } else if (msg.text == "/e" || msg.text == "/evening") {
        bot.sendMessage("Введите час вечернего приёма\n(-1, чтобы отключить приём):", tg_id);
        lastCMD = "/eh";
      } else if (msg.text == "/g" || msg.text == "/give") {
        slot_change = true;
        lastMinute = -1;
        lastCMD = "";
      } else if (msg.text == "/ignore") {
        cup_full = 0;
        cfg_save("cup_full", String(cup_full));
        lastCMD = "";
      } else if (msg.text == "/f" || msg.text == "/fill") {
        bot.sendMessage("Выбрана нулевая позиция. Снимите крышку устройства и загрузите таблетки в соответствующие слоты. После этого закройте устройство.", tg_id);
        current_slot = -1;
        cfg_save("current_slot", String(current_slot));
        lastMinute = -1;
        slot_change = true;
        lastCMD = "";
      } else {
        bot.sendMessage("Вас приветствует LeB-Pillbox!", tg_id);
        bot.sendMessage("Команды управления таблетницей:\n/restart - перезапустить таблетницу\n/reset - сбросить настройки таблетницы\n/gmt - установить часовой пояс\n/ignore - игнорировать выдачу\n/m или /morning - установить время утренней выдачи\n/e или /evening - установить время вечерней выдачи\n/g или /give - совершить выдачу\n/f или /fill  - заполнить слоты", tg_id);
        bot.sendMessage("Выбран слот: " + String(current_slot), tg_id);
        lastCMD = "";
      }
    }
  }
  delay(1000);
}



void dprint(String data) {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);
  display.print(data);
  display.display();
}

String utf8rus(String source) {
  int i, k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };
  k = source.length();
  i = 0;
  while (i < k) {
    n = source[i];
    i++;
    if (n >= 0xC0) {
      switch (n) {
        case 0xD0:
          {
            n = source[i];
            i++;
            if (n == 0x81) {
              n = 0xA8;
              break;
            }
            if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
            break;
          }
        case 0xD1:
          {
            n = source[i];
            i++;
            if (n == 0x91) {
              n = 0xB8;
              break;
            }
            if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
            break;
          }
      }
    }
    m[0] = n;
    target = target + String(m);
  }
  return target;
}

void cfg_save(String file_name, String data) {
  String file_path = "/" + file_name + ".cfg";
  File file = SPIFFS.open(file_path, "w");
  file.print(data);
}

String cfg_load(String file_name) {
  String file_path = "/" + file_name + ".cfg";
  if (SPIFFS.exists(file_path)) {
    File file = SPIFFS.open(file_path, "r");
    String data = file.readString();
    return data;
  } else {
    return "";
  }
}


void update_time() {
  delay(100);
  timeClient.begin();
  timeClient.update();
  //timeClient.setTimeOffset(60*60*gmt_offset);
  myRTC_set.setEpoch(timeClient.getEpochTime() + 60 * 60 * gmt_offset);
  Serial.println("OFFSET: ");
  Serial.println(gmt_offset);
  timeClient.end();
  delay(100);
}

void setStepperPos(int deg_pos) {
  stepper.enable();
  delay(50);
  stepper.setTargetDeg(deg_pos);

  if (abs(stepper.getTarget() - stepper.getCurrent()) > 1) {
    display.clearDisplay();
    display.display();
    display.setCursor(0, 0);
    dprintln(utf8rus(""), 2, true);
    dprintln(utf8rus("Выбор"), 2, true);
    dprintln(utf8rus("слота..."), 2, true);
    while (stepper.tick()) {
      Serial.print("Cur: ");
      Serial.print(stepper.getCurrent());
      Serial.print(" Tar: ");
      Serial.println(stepper.getTarget());
    }
  }

  stepper.tick();
  delay(50);
  stepper.disable();
}