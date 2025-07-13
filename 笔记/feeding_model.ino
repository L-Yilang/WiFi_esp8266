#include <ESP8266WiFi.h>
#include <DNSServer.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>

const char* SSID_NAME = "喂食器设置"; //默认SSID名称,就是网络名称
#define SUBTITLE "设置"
#define TITLE "参数设置"
#define POST_TITLE "设置已提交"
#define POST_BODY "参数已发送到设备，2秒后可再次提交。"
//参数默认值
float feedInterval = 12.0;
float feedAmount = 1.0;
int blinkTimes = 3;


//下面的东西不需要更改
const byte HTTP_CODE = 200; //HTTP状态码
const byte DNS_PORT = 53; //DNS端口
const unsigned long TICK_TIMER = 1000 * 10;//定时器间隔 1s * 60
IPAddress APIP(172, 0, 0, 1); //网关

//接入互联网的相关变量
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;

//初始化模块特殊变量
unsigned long lastTick = 0;
DNSServer dnsServer;
ESP8266WebServer webServer(80);

// EEPROM参数存储地址，这是在将信息存入网络模块
#define EEPROM_SIZE 16
#define ADDR_MAGIC 0
#define ADDR_INTERVAL 4
#define ADDR_AMOUNT 8
#define ADDR_BLINK 12
#define MAGIC_VALUE 0xA5A5A5A5

//---------------------------以下是服务器调用的工具函数
void tryConnectWifi() {
  if (wifiSSID.length() == 0) {
    wifiConnected = false;
    return;
  }

  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str()); //连接互联网
  Serial.print(wifiSSID.c_str());
  Serial.print(wifiPass.c_str());
  
  unsigned long startAttempt = millis();
  wifiConnected = false;
  while (millis() - startAttempt < 5000) { //5秒等待
    if (WiFi.status() == WL_CONNECTED) {
      
      configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org"); //！！！连接阿里云获取时间
      // 时区偏移（秒），夏令时偏移，NTP服务器域名
      // 事实上，我们还可以使用手机时间，用js即可获取。但我们不希望需要经常打开手机校准
      wifiConnected = true;
      break;
    }
    delay(200);
  }
}
// 提交后处理，读取参数并通过GPIO输出
String posted() {
  String intervalStr = input("interval");
  String amountStr = input("amount");
  String blinkStr = input("blink");

  feedInterval = intervalStr.toFloat();
  feedAmount = amountStr.toFloat();
  blinkTimes = blinkStr.toInt();

  // 结构化格式输出参数（JSON）
  Serial.print("{\"interval\":");
  Serial.print(feedInterval, 1);
  Serial.print(",\"amount\":");
  Serial.print(feedAmount, 1);
  Serial.print(",\"blink\":");
  Serial.print(blinkTimes);
  Serial.println("}");

  saveParams(); // 保存参数到EEPROM

  return header(POST_TITLE) + POST_BODY + "<br><a href='/'>返回</a>" + footer(); //返回无实际作用
}
void saveParams() {
  EEPROM.put(ADDR_MAGIC, (uint32_t)MAGIC_VALUE);
  EEPROM.put(ADDR_INTERVAL, feedInterval);
  EEPROM.put(ADDR_AMOUNT, feedAmount);
  EEPROM.put(ADDR_BLINK, blinkTimes);
  EEPROM.commit();
}
void loadParams() {
  uint32_t magic = 0;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic == MAGIC_VALUE) {
    EEPROM.get(ADDR_INTERVAL, feedInterval);
    EEPROM.get(ADDR_AMOUNT, feedAmount);
    EEPROM.get(ADDR_BLINK, blinkTimes);
  }
}
//展示网络连接状态
String getConnStatusText() {
  return wifiConnected ? "<span style='color:green'>已连接互联网</span>" : "<span style='color:red'>未连接互联网</span>";
}
String currentTimeStr = "";
//获取当前时间字符串，格式为 "YYYY-MM-DD HH:MM:SS"
String getCurrentTimeString() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[32];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}
//打印当前时间到串口
void printCurrentTime() {
  currentTimeStr = getCurrentTimeString();
  char jsonBuf[48];
  sprintf(jsonBuf, "{\"time\":\"%s\"}", currentTimeStr.c_str());
  Serial.println(jsonBuf);
}
//----------------------------


//以下是html相关。HTML就是服务器返回的字符串

//伪装成函数的字符串
String input(String argName) {
  String a = webServer.arg(argName);
  a.replace("<","&lt;");
  a.replace(">","&gt;");
  a.substring(0,200);
  return a;
}
String footer() { 
  return "</div><div class=q><a>&#169; All rights reserved.</a></div>";
}
String header(String t) {
  String a = String(SSID_NAME);
  String CSS = "article { background: #f2f2f2; padding: 1.3em; }" 
    "body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; margin: 0; padding: 0; }"
    "div { padding: 0.5em; }"
    "h1 { margin: 0.5em 0 0 0; padding: 0.5em; }"
    "input { width: 100%; padding: 9px 10px; margin: 8px 0; box-sizing: border-box; border-radius: 0; border: 1px solid #555555; border-radius: 10px; }"
    "label { color: #333; display: block; font-style: italic; font-weight: bold; }"
    "nav { background: #0066ff; color: #fff; display: block; font-size: 1.3em; padding: 1em; }"
    "nav b { display: block; font-size: 1.5em; margin-bottom: 0.5em; } "
    "textarea { width: 100%; }"
    "button[disabled] { background: #ccc; color: #888; }";
  String h = "<!DOCTYPE html><html>"
    "<head><title>" + a + " :: " + t + "</title>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>" + CSS + "</style>"
    "<meta charset=\"UTF-8\"></head>"
    "<body><nav><b>" + a + "</b> " + SUBTITLE + "</nav><div><h1>" + t + "</h1></div><div>";
  return h;
}
//


// 主页面，包含三个输入框和提交按钮，按钮禁用2秒，并显示网络连接状态和WiFi设置按钮
String index() {
  String js = "<script>"
    "function disableBtn(){"
    "var btn=document.getElementById('submitBtn');"
    "btn.disabled=true;"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/post',true);"
    "xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState==4){"
    "setTimeout(function(){btn.disabled=false;},200);"
    "}"
    "};"
    "var interval=document.getElementsByName('interval')[0].value;"
    "var amount=document.getElementsByName('amount')[0].value;"
    "var blink=document.getElementsByName('blink')[0].value;"
    "xhr.send('interval='+encodeURIComponent(interval)+'&amount='+encodeURIComponent(amount)+'&blink='+encodeURIComponent(blink));"
    "return false;"
    "}"
    "</script>";
  //显示网络连接状态和WiFi设置按钮
  String wifiBtn = "<div style='margin:1em 0'><a href='/wifi'><button type='button'>WiFi设置</button></a></div>";
  String connStatus = wifiBtn + "<div>互联网：" + getConnStatusText() + "</div>";
  //显示当前时间
  String timeStatus = "<div>当前校准时间：" + currentTimeStr + "</div>";
  return header(TITLE)+ connStatus + timeStatus +
    "<div><form onsubmit='return disableBtn()'>"
    "<label>喂食时间间隔(小时):</label>"
    "<input type='number' step='0.1' min='0.1' name='interval' value='" + String(feedInterval,1) + "' required>"
    "<label>喂食相对数量:</label>"
    "<input type='number' step='0.1' min='0.1' name='amount' value='" + String(feedAmount,1) + "' required>"
    "<label>喂食前闪灯次数:</label>"
    "<input type='number' min='0' max='50' name='blink' value='" + String(blinkTimes) + "' required>"
    "<button id='submitBtn' type='submit'>保存并提交</button>"
    "</form></div>" + js + footer();
}
//WiFi设置页面
String wifiPage() {
  String js =
   "<script>"
    "function connectWifi(){"
    "var btn=document.getElementById('wifiBtn');"//绑定按钮
    "btn.disabled=true;"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/wifi_connect',true);"//函数设置为POST方式，重要
    "xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"

    "xhr.onreadystatechange=function(){" //请求完成的回调函数
    "if(xhr.readyState==4){"
    "setTimeout(function(){btn.disabled=false;location.reload();},1000);"
    "}"
    "};"

    "var ssid=document.getElementsByName('ssid')[0].value;"
    "var pass=document.getElementsByName('pass')[0].value;"
    "xhr.send('ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));"//正式发送请求
    "return false;"

    "}"
    "</script>";
  String connStatus = "<div>互联网：" + getConnStatusText() + "</div>";
  //显示当前时间
  String timeStatus = "<div>当前校准时间：" + currentTimeStr + "</div>";
  return header("WiFi设置") + connStatus + timeStatus +
    "<div><form onsubmit='return connectWifi()'>"
    "<label>WiFi名称(SSID):</label>"
    "<input type='text' name='ssid' value='" + wifiSSID + "' required>"
    "<label>WiFi密码:</label>"
    "<input type='text' name='pass' value='" + wifiPass + "'>"
    "<button id='wifiBtn' type='submit'>连接</button>"
    "</form></div>"
    "<div style='margin-top:1em'><a href='/'><button type='button'>返回主界面</button></a></div>"
    + js + footer();
}


//-----------------------------固定结构：主体部分
void setup() {

  //一，初始化串口
  Serial.begin(115200);

  //二，初始化EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadParams(); // 启动时加载参数

  //三，设置为AP+STA混合模式
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));//IP 网关 子网掩码

  //四，启动WiFi热点
  WiFi.softAP(SSID_NAME);  

  //五，启动DNS服务器，实现DNS欺骗（所有域名都指向本机IP）
  dnsServer.start(DNS_PORT, "*", APIP);

  //url就是路由
  //六------，配置Web服务器的路由，路由其实就是网页UI与服务器函数的连接处
  webServer.on("/post",[]() {  //当收到这个url
    webServer.send(HTTP_CODE, "text/html", posted()); //状态码 内容标志 激活的函数（函数返回值为html文本）
  });
  //WiFi设置页面
  webServer.on("/wifi",[]() {
    webServer.send(HTTP_CODE, "text/html", wifiPage());
  });
  //WiFi连接处理
  webServer.on("/wifi_connect", []() {
    wifiSSID = input("ssid");
    wifiPass = input("pass");
    tryConnectWifi();
    webServer.send(HTTP_CODE, "text/html", wifiPage());
  });
  webServer.onNotFound([]() { 
    webServer.send(HTTP_CODE, "text/html", index());
  });
  webServer.begin(); // 启动Web服务器
}


///--------------------------------------固定结构，主循环
void loop() { 
  // 每隔TICK_TIMER毫秒更新时间
  if ((millis() - lastTick) > TICK_TIMER) {
    lastTick = millis();
    if (wifiConnected){
      printCurrentTime(); //十秒打印一次
    }
    // 这里可以添加定时任务
  }
  // 处理DNS请求，实现劫持
  dnsServer.processNextRequest();
  // 处理HTTP请求
  webServer.handleClient();
}