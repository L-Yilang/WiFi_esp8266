// ESP8266 WiFi Captive Portal
// By 125K (github.com/125K)

// Libraries
#include <ESP8266WiFi.h>
#include <DNSServer.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>
// #include <ArduinoJson.h> // 新增：用于JSON解析

// Default SSID name
const char* SSID_NAME = "喂食器设置"; // 默认SSID名称

// Default main strings
#define SUBTITLE "设置"
#define TITLE "总览"
#define BODY ""
#define POST_TITLE "设置已提交"
#define POST_BODY "参数已发送到设备，2秒后可再次提交。"
#define CLEAR_TITLE "Cleared"

// Init system settings
const byte HTTP_CODE = 200; // HTTP状态码
const byte DNS_PORT = 53; // DNS端口
const unsigned long TICK_TIMER = 1000 * 10;// 定时器间隔 1s * 60
IPAddress APIP(172, 0, 0, 1); // Gateway // 网关

String currentSSID = "";

//互联网相关
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;

unsigned long lastTick = 0;
DNSServer dnsServer;
ESP8266WebServer webServer(80);

// 参数默认值
float feedInterval = 12.0;
float feedAmount = 1.0;
int blinkTimes = 3;

// EEPROM参数存储地址
#define FEED_PLAN_BYTES_PER_HOUR 3
#define EEPROM_SIZE (16 + 24 * FEED_PLAN_BYTES_PER_HOUR) // 16为参数区，后面为喂食计划区
#define ADDR_MAGIC 0
#define ADDR_INTERVAL 4
#define ADDR_AMOUNT 8
#define ADDR_BLINK 12
#define ADDR_FEED_PLAN 16 // 新增：喂食计划起始地址
#define MAGIC_VALUE 0xA5A5A5A5

//工具函数
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
    delay(500);
  }
}


// 提交后处理，读取参数并通过GPIO输出
String posted() {

  Serial.println("AAAE");

  return String(POST_BODY) + "<br><a href='/'>返回</a>" + footer();
}

void BLINK() { // The built-in LED will blink 5 times after a password is posted. // 密码提交后内置LED闪烁5次
  for (int counter = 0; counter < 2; counter++)
  {
    // For blinking the LED. // 控制LED闪烁
    digitalWrite(BUILTIN_LED, counter % 2);
    delay(500);
  }
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

void PRIN() {
  Serial.print(loadFeedPlanJson() + "E");
}
//保存喂食计划到EEPROM，支持 {"1":{"amount":1.0,"light":xxx,"water":yyy}, ...}
void saveFeedPlan(const String& json) {
  // 只输出喂食计划JSON，不包含其他信息

  for (int i = 1; i <= 24; ++i) {
    String key = "\"" + String(i) + "\":";
    int idx = json.indexOf(key);
    float amount = 0.0;
    int light_mode = 0;
    int water_duration = 0;
    int minute_value = 0; // 新增：分钟值
    if (idx >= 0) {
      int objStart = json.indexOf("{", idx);
      int objEnd = json.indexOf("}", objStart);
      String objStr = json.substring(objStart, objEnd + 1);

      // amount
      int amountIdx = objStr.indexOf("\"amount\":");
      if (amountIdx >= 0) {
        int valStart = amountIdx + 9;
        int valEnd = objStr.indexOf(",", valStart);
        if (valEnd < 0) valEnd = objStr.indexOf("}", valStart);
        if (valEnd > valStart) {
          amount = String(objStr.substring(valStart, valEnd)).toFloat();
        }
      }
      // light - 修复：正确解析数字字符串值
      int lightIdx = objStr.indexOf("\"light\":");
      if (lightIdx >= 0) {
        int valStart = lightIdx + 8;
        // 跳过可能的空格
        while (valStart < objStr.length() && objStr.charAt(valStart) == ' ') valStart++;
        // 检查是否有引号
        bool hasQuotes = (valStart < objStr.length() && objStr.charAt(valStart) == '"');
        if (hasQuotes) valStart++; // 跳过开始引号
        
        int valEnd;
        if (hasQuotes) {
          valEnd = objStr.indexOf("\"", valStart); // 找结束引号
        } else {
          valEnd = objStr.indexOf(",", valStart);
          if (valEnd < 0) valEnd = objStr.indexOf("}", valStart);
        }
        
        if (valEnd > valStart) {
          String lightStr = objStr.substring(valStart, valEnd);
          lightStr.trim();
          // 直接解析数字字符串
          light_mode = lightStr.toInt();
          // 确保值在有效范围内
          if (light_mode < 0 || light_mode > 2) light_mode = 0;
        }
      }
      // water
      int waterIdx = objStr.indexOf("\"water\":");
      if (waterIdx >= 0) {
        int valStart = waterIdx + 8;
        int valEnd = objStr.indexOf(",", valStart);
        if (valEnd < 0) valEnd = objStr.indexOf("}", valStart);
        if (valEnd > valStart) {
          water_duration = String(objStr.substring(valStart, valEnd)).toInt();
        }
      }
      // minute - 新增：解析分钟值
      int minuteIdx = objStr.indexOf("\"minute\":");
      if (minuteIdx >= 0) {
        int valStart = minuteIdx + 9;
        int valEnd = objStr.indexOf(",", valStart);
        if (valEnd < 0) valEnd = objStr.indexOf("}", valStart);
        if (valEnd > valStart) {
          minute_value = String(objStr.substring(valStart, valEnd)).toInt();
        }
      }
    }
    // 存储 - 扩展存储结构以包含分钟值
    uint8_t v = 0;
    if (amount >= 0.1) {
      v = (uint8_t)(amount * 10.0 + 0.5);
      if (v > 50) v = 50;
    }
    int base = ADDR_FEED_PLAN + (i - 1) * FEED_PLAN_BYTES_PER_HOUR;
    EEPROM.write(base, v);
    EEPROM.write(base + 1, (uint8_t)light_mode);
    EEPROM.write(base + 2, (uint8_t)water_duration);
    // 注意：当前结构只有3字节，如需存储分钟值需要扩展结构
  }
  EEPROM.commit();
}

// 读取喂食计划为JSON字符串，格式为 {"1":{"amount":1.0,"light":"0","water":2}, ...}，只包含有喂食计划的小时
String loadFeedPlanJson() {
  String json = "{";
  bool first = true;
  for (int i = 1; i <= 24; ++i) {
    int base = ADDR_FEED_PLAN + (i - 1) * FEED_PLAN_BYTES_PER_HOUR;
    uint8_t v = EEPROM.read(base);
    uint8_t light_mode = EEPROM.read(base + 1);
    uint8_t water_duration = EEPROM.read(base + 2);
    if (v > 0) { // 只包含有喂食计划的小时
      float amount = v / 10.0;
      // 修复：输出数字字符串格式
      String lightStr = String(light_mode); // 直接使用数字字符串
      // 只包含有喂食计划的小时
      if (!first) json += ",";
      json += "\"" + String(i) + "\":{\"amount\":" + String(amount, 1) + ",\"light\":\"" + lightStr + "\",\"water\":" + String(water_duration) + "}";
      first = false;
    }
  }
  json += "}";
  return json;
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

  char buffer[8];
  sprintf(buffer, "[%02d:%02d]", timeinfo.tm_hour, timeinfo.tm_min);
  return String(buffer);
}

//打印当前时间到串口
void printCurrentTime() {
  currentTimeStr = getCurrentTimeString();

  //如果时间错误
  if (currentTimeStr.startsWith("[00:")) {
    currentTimeStr = "";
    return;
  }

  Serial.print(currentTimeStr + "E"); // 在时间后加上结束符 E
}

String input(String argName) {
  String a = webServer.arg(argName);
  a.replace("<","&lt;");
  a.replace(">","&gt;");
  a = a.substring(0,200); // 修正：截断后赋值回a
  return a;
}

String footer() { 
  return "</div><div class=q><a>&#169; All rights reserved.</a></div>";
}

String header(String t) {
  String a = String(currentSSID);
  String CSS =
    "article { background: #f2f2f2; padding: 1.3em; }"
    "body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; padding: 0; }"
    "div { padding: 0.5em; }"
    "h1 { margin: 0.5em 0 0 0; padding: 0.5em; }"
    "input { width: 100%; padding: 9px 10px; margin: 8px 0; box-sizing: border-box; border-radius: 0; border: 1px solid #555555; border-radius: 10px; }"
    "label { color: #333; display: block; font-style: italic; font-weight: bold; }"
    "nav {"
    "  background: #0066ff;"
    "  color: #fff;"
    "  display: block;"
    "  font-size: 1.3em;"
    "  padding: 1em;"
    "  text-align: center;"
    "}"
    "nav b {"
    "  display: block;"
    "  font-size: 1.5em;"
    "  margin-bottom: 0.5em;"
    "}"
    "textarea { width: 100%; }"
    "button {"
    "  font-size: 15px;"
    "  padding: 8px 18px;"
    "  border-radius: 6px;"
    "  border: 1px solid #ccc;"
    "  background: #f5f5f5;"
    "  transition: transform 0.04s, background 0.01s;"
    "}"
    "button:active {"
    "  transform: scale(0.96);"
    "  background: #e0e0e0;"
    "}"
    "button[disabled] { background: #ccc; color: #888; }";
  String h = "<!DOCTYPE html><html>"
    "<head><title>" + t + "</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>" + CSS + "</style>"
    "<meta charset='UTF-8'></head>"
    "<body><nav>" + a + "" + t + "</nav>";
  return h;
}

// 主页面，包含三个输入框和提交按钮，按钮禁用2秒，并显示网络连接状态和WiFi设置按钮
String index() {
  // 插入 info-popup 样式
  String infoPopupCSS =
    ".info-popup {"
    "  background-color: #eff6ff;"
    "  border: solid 1px #1d4ed8;"
    "  border-radius: 8px;"
    "  padding: 12px 16px 12px 14px;"
    "  margin: 0 auto 12px auto;"
    "  max-width: 340px;"
    "  display: block;"
    "}"
    ".info-message {"
    "  color: #1d4ed8;"
    "  font-size: 15px;"
    "  line-height: 1.7;"
    "  word-break: break-all;"
    "  text-align: left;"
    "}";

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
    "xhr.send('');" // 添加这行，发送空数据
    "return false;"
    "}"
    // 新增：拉取喂食计划并渲染 info-popup
    "function renderFeedInfoBox(plan) {"
    "  var feedAmounts = {};"
    "  var litBoxes = [];"
    "  for(var i=1;i<=24;i++){"
    "    var entry=plan[i]||{};"
    "    var v=Number(entry.amount||0);"
    "    if(v>0){feedAmounts[i]=v;litBoxes.push(i);}else{feedAmounts[i]=1.0;}"
    "  }"
    "  litBoxes.sort(function(a,b){return a-b;});"
    "  var minGap='-';"
    "  if(litBoxes.length>=2){"
    "    var min=Infinity;"
    "    for(var i=1;i<litBoxes.length;i++){"
    "      var gap=litBoxes[i]-litBoxes[i-1];if(gap<min)min=gap;"
    "    }"
    "    var wrapGap=(24-litBoxes[litBoxes.length-1])+litBoxes[0];"
    "    if(wrapGap<min)min=wrapGap;"
    "    minGap=min;"
    "  }"
    "  var infoLines=["
    "    litBoxes.length"
    "      ?'喂食计划: '+litBoxes.map(function(hour){return hour+' 时('+((feedAmounts[hour]||1.0)*100).toFixed(0)+'%)';}).join(', ')"
    "      :'喂食时间: 无',"
    "    '喂食次数: '+litBoxes.length,"
    "    '最小喂食间距: '+minGap+' 时',"
    "    '总计喂食相对数量: '+litBoxes.reduce(function(sum,h){return sum+(feedAmounts[h]||1.0);},0).toFixed(1)"
    "  ];"
    "  document.getElementById('feedInfoBox').innerHTML="
    "    '<div class=\"info-popup\">'+infoLines.map(function(line){return '<div class=\"info-message\">'+line+'</div>';}).join('')+'</div>';"
    "}"
    "function loadFeedPlanForIndex(){"
    "  fetch('/feed_plan').then(function(res){return res.json();}).then(function(json){renderFeedInfoBox(json);});"
    "}"
    "window.addEventListener('DOMContentLoaded',loadFeedPlanForIndex);"
    "</script>";
  //显示网络连接状态和WiFi设置按钮
  String wifiBtn = "<div style='margin:1em 0;display:inline-block'><a href='/wifi'><button type='button'>WiFi设置</button></a></div>";
  // 新增：喂食设置按钮
  String feedBtn = "<div style='margin:1em 0;display:inline-block'><a href='/feed'><button type='button'>喂食设置</button></a></div>";
  String connStatus = wifiBtn + feedBtn + "<div>互联网：" + getConnStatusText() + "</div>";
  //显示当前时间
  String timeStatus = "<div>当前校准时间：" + currentTimeStr + "</div>";
  // info-popup 容器
  String feedInfoDiv = "<div id='feedInfoBox'></div>";
  // 修复：补全 HTML 结构
  return header(TITLE) +
    "<style>" + infoPopupCSS + "</style>" +
    "<div>" + BODY + "</div>" +
    connStatus + timeStatus +
    feedInfoDiv +
    "<div><form onsubmit='return disableBtn()'>"
    "<button id='submitBtn' type='submit'>保存并提交</button>"
    "</form></div>" + js + "</body></html>";
}
//WiFi设置页面
String wifiPage() {
  String js =
   "<script>"
    "function connectWifi(){"
    "var btn=document.getElementById('wifiBtn');"//绑定按钮
    "btn.disabled=true;"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/wifi_connect',true);"//函数设置为POST方式
    "xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"

    "xhr.onreadystatechange=function(){" //请求完成的回调函数
    "if(xhr.readyState==4){"
    "setTimeout(function(){btn.disabled=false;location.reload();},1000);"//刷新网页
    "}"
    "};"

    "var ssid=document.getElementsByName('ssid')[0].value;"
    "var pass=document.getElementsByName('pass')[0].value;"
    "xhr.send('ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));"//正式发送请求
    "return false;"//

    "}"
    "</script>";
  String connStatus = "<div>互联网：" + getConnStatusText() + "</div>";
  //显示当前时间
  String timeStatus = "<div>当前校准时间：" + currentTimeStr + "</div>";
  // 修复：补全 HTML 结构，使用 header/footer
  return header("WiFi设置") +
    connStatus + timeStatus +
    "<div><form onsubmit='return connectWifi()'>"
    "<label>WiFi名称(SSID):</label>"
    "<input type='text' name='ssid' value='" + wifiSSID + "' required>"
    "<label>WiFi密码:</label>"
    "<input type='text' name='pass' value='" + wifiPass + "'>"
    "<button id='wifiBtn' type='submit'>连接</button>"
    "</form></div>"
    "<div style='margin-top:1em'><a href='/'><button type='button'>返回主界面</button></a></div>"
    + js + footer() + "</body></html>";
}

// 将页面内容放入 PROGMEM，避免堆溢出
static const char FEED_SETTING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-cn">
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no, maximum-scale=1.0">
    <meta charset="UTF-8">
    <title>喂食设置</title>
    <style>
        .container-outer {
            display: flex;
            justify-content: center;
            padding-left: 24px;   
            padding-right: 24px;  
        }
        .container {
            display: flex;
            flex-wrap: wrap;
            gap: 0px 6px; /* 行，列 */
            justify-content: center;
            margin-top: 20px;
            width: 100%;
            max-width: 400px;
        }
        .item {
            display: flex;
            flex-direction: column;
            align-items: center;
            margin-bottom: 12px;
        }
        .box {
            width: 44px;
            height: 44px;
            background: #ddd;
            border: 2px solid #aaa;
            border-radius: 8px;
            cursor: pointer;
            transition: background 0.1s, border 0.2s;
        }
        .box.lit {
            background: #ffd700;
            border-color: #ff9800;
        }
        .label {
            margin-bottom: 2px;
            font-size: 12px;
            color: #333;
        }
        .output {
            text-align: center;
            margin-top: 18px;
            font-size: 15px;
            color: #0078d4;
        }
        .button-container {
            display: flex;
            justify-content: center;
            margin-top: 16px;
            gap: 24px; /* 按钮间距增大 */
        }
        button {
            font-size: 15px;
            padding: 8px 18px;
            border-radius: 6px;
            border: 1px solid #ccc;
            background: #f5f5f5;
            transition: transform 0.04s, background 0.01s;
        }
        button:active {
            transform: scale(0.96);
            background: #e0e0e0;
        }
        .info-popup {
          background-color: #eff6ff;
          border: solid 1px #1d4ed8;
          border-radius: 8px;
          padding: 12px 16px 12px 14px;
          margin: 0 auto 12px auto;
          max-width: 340px;
          display: block;
        }
        .info-message {
          color: #1d4ed8;
          font-size: 15px;
          line-height: 1.7;
          word-break: break-all;
          text-align: left;
        }
        nav {
            background: #0066ff;
            color: #fff;
            display: block;
            font-size: 1.3em;
            padding: 1em;
            text-align: center;
        }
        nav b {
            display: block;
            font-size: 1.5em;
            margin-bottom: 0.5em;
        }
        @media (max-width: 480px) {
            .container {
                max-width: 98vw;
                gap: 0px 4px;
            }
            .box {
                width: 38px;
                height: 38px;
            }
            .label {
                font-size: 11px;
            }
            .output {
                font-size: 13px;
            }
            button {
                font-size: 13px;
                padding: 7px 12px;
            }
        }
    </style>
</head>
<body>
    <nav>喂食设置</nav>
    <div class="container-outer">
        <div class="container">
            <div class="item"><div class="label">1:00</div><div class="box" data-index="1"></div></div>
            <div class="item"><div class="label">2:00</div><div class="box" data-index="2"></div></div>
            <div class="item"><div class="label">3:00</div><div class="box" data-index="3"></div></div>
            <div class="item"><div class="label">4:00</div><div class="box" data-index="4"></div></div>
            <div class="item"><div class="label">5:00</div><div class="box" data-index="5"></div></div>
            <div class="item"><div class="label">6:00</div><div class="box" data-index="6"></div></div>
            <div class="item"><div class="label">7:00</div><div class="box" data-index="7"></div></div>
            <div class="item"><div class="label">8:00</div><div class="box" data-index="8"></div></div>
            <div class="item"><div class="label">9:00</div><div class="box" data-index="9"></div></div>
            <div class="item"><div class="label">10:00</div><div class="box" data-index="10"></div></div>
            <div class="item"><div class="label">11:00</div><div class="box" data-index="11"></div></div>
            <div class="item"><div class="label">12:00</div><div class="box" data-index="12"></div></div>
            <div class="item"><div class="label">13:00</div><div class="box" data-index="13"></div></div>
            <div class="item"><div class="label">14:00</div><div class="box" data-index="14"></div></div>
            <div class="item"><div class="label">15:00</div><div class="box" data-index="15"></div></div>
            <div class="item"><div class="label">16:00</div><div class="box" data-index="16"></div></div>
            <div class="item"><div class="label">17:00</div><div class="box" data-index="17"></div></div>
            <div class="item"><div class="label">18:00</div><div class="box" data-index="18"></div></div>
            <div class="item"><div class="label">19:00</div><div class="box" data-index="19"></div></div>
            <div class="item"><div class="label">20:00</div><div class="box" data-index="20"></div></div>
            <div class="item"><div class="label">21:00</div><div class="box" data-index="21"></div></div>
            <div class="item"><div class="label">22:00</div><div class="box" data-index="22"></div></div>
            <div class="item"><div class="label">23:00</div><div class="box" data-index="23"></div></div>
            <div class="item"><div class="label">24:00</div><div class="box" data-index="24"></div></div>
        </div>
    </div>
    <!-- 信息展示区域 -->
    <div class="output" id="output"></div>
    <div class="button-container">
        <button id="resetBtn">归零</button>
        <button id="outputBtn">保存/提交</button>
        <button id="backBtn" onclick="location.href='/'">返回主页</button>
    </div>
    <script>
        // 禁用双击缩放
        let lastTouchEnd = 0;
        document.querySelectorAll('.box').forEach(box => {
            box.addEventListener('touchend', function (event) {
                let now = Date.now();
                if (now - lastTouchEnd <= 300) {
                    event.preventDefault();
                }
                lastTouchEnd = now;
            }, false);
        });

        // 工具函数：根据数量返回颜色（0.1~5.0，越大越深，效果更夸张）
        function getFeedColor(amount) {
            let t = Math.max(0, Math.min(1, (amount - 0.1) / (5.0 - 0.1)));
            let h = 50 - t * 20;
            let l = 58 - t * 16;
            let s = 98 - t * 6;
            if (amount <= 1.0) {
                let t2 = Math.max(0, Math.min(1, (amount - 0.1) / (1.0 - 0.1)));
                h = 60 - t2 * 10;
                l = 80 - t2 * 22;
                s = 90 - t2 * 8;
            }
            return `hsl(${h}, ${s}%, ${l}%)`;
        }
        function getFeedBorderColor(amount) {
            let t = Math.max(0, Math.min(1, (amount - 0.1) / (5.0 - 0.1)));
            let h = 40 - t * 30;
            let l = 45 - t * 17;
            let s = 100;
            return `hsl(${h}, ${s}%, ${l}%)`;
        }

        function updateLitBoxColors() {
            document.querySelectorAll('.box').forEach(box => {
                const hour = box.getAttribute('data-index');
                let amount = 1.0;
                if (feedAmounts[hour]) {
                    if (typeof feedAmounts[hour] === 'object') {
                        amount = feedAmounts[hour].amount ?? 1.0;
                    } else {
                        amount = feedAmounts[hour];
                    }
                }
                if (box.classList.contains('lit')) {
                    box.style.background = getFeedColor(amount);
                    box.style.borderColor = getFeedBorderColor(amount);
                } else {
                    box.style.background = '';
                    box.style.borderColor = '';
                }
            });
        }

        // 渲染信息展示块，显示分钟
        function renderFeedInfoBox() {
            const litBoxes = [];
            for (let i = 1; i <= 24; i++) {
                let box = document.querySelector('.box[data-index="' + i + '"]');
                if (box && box.classList.contains('lit')) litBoxes.push(i);
            }
            litBoxes.sort((a, b) => a - b);

            let minGap = '-';
            if (litBoxes.length >= 2) {
                let min = Infinity;
                for (let i = 1; i < litBoxes.length; i++) {
                    let gap = litBoxes[i] - litBoxes[i - 1];
                    if (gap < min) min = gap;
                }
                let wrapGap = (24 - litBoxes[litBoxes.length - 1]) + litBoxes[0];
                if (wrapGap < min) min = wrapGap;
                minGap = min;
            }

            const infoLines = [
                litBoxes.length
                    ? '喂食计划: ' + litBoxes.map(hour => {
                        let m = (feedAmounts[hour] && typeof feedAmounts[hour].minute === 'number')
                            ? feedAmounts[hour].minute.toString().padStart(2, '0')
                            : '00';
                        return hour + ':' + m + ' 时(' + ((feedAmounts[hour]?.amount ?? 1.0) * 100).toFixed(0) + '%)';
                    }).join(', ')
                    : '喂食时间: 无',
                '喂食次数: ' + litBoxes.length,
                '最小喂食间距: ' + minGap + ' 时',
                '总计喂食相对数量: ' + litBoxes.reduce((sum, hour) => sum + (feedAmounts[hour]?.amount ?? 1.0), 0).toFixed(1)
            ];

            document.getElementById('output').innerHTML = 
                '<div class="info-popup">' + 
                infoLines.map(line => '<div class="info-message">' + line + '</div>').join('') + 
                '</div>';
        }

        // 长按弹窗相关
        let longPressTimer = null;
        let popupEl = null;
        // feedAmounts 结构扩展：{hour: {amount, light, water, minute}}
        let feedAmounts = {}; 

        // 拉取喂食计划（新结构）
        function loadFeedPlan() {
            fetch('/feed_plan')
                .then(res => res.json())
                .then(json => {
                    feedAmounts = {};
                    for (let i = 1; i <= 24; i++) {
                        let entry = json[i] || {};
                        let v = Number(entry.amount || 0);
                        // 修复：将后端的数字字符串转换为前端使用的选项值
                        let light = entry.light || '0';
                        let lightOption = 'none';
                        if (light === '1') lightOption = 'option1';
                        else if (light === '2') lightOption = 'option2';
                        let water = typeof entry.water === 'number' ? entry.water : 0;
                        let minute = typeof entry.minute === 'number' ? entry.minute : 0;
                        if (v > 0) {
                            feedAmounts[i] = { amount: v, light: lightOption, water, minute };
                            let box = document.querySelector('.box[data-index="' + i + '"]');
                            if (box) box.classList.add('lit');
                            // 同步label分钟
                            let label = box?.previousElementSibling;
                            if (label && label.classList.contains('label')) {
                                label.textContent = i + ':' + (minute.toString().padStart(2, '0'));
                            }
                        } else {
                            feedAmounts[i] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                            let box = document.querySelector('.box[data-index="' + i + '"]');
                            if (box) box.classList.remove('lit');
                            let label = box?.previousElementSibling;
                            if (label && label.classList.contains('label')) {
                                label.textContent = i + ':00';
                            }
                        }
                    }
                    updateLitBoxColors();
                    renderFeedInfoBox();
                })
                .catch(err => {
                    console.error('Failed to load feed plan:', err);
                    for (let i = 1; i <= 24; i++) {
                        feedAmounts[i] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                    }
                    renderFeedInfoBox();
                });
        }

        // 保存喂食计划（新结构，含minute）
        function saveFeedPlan(cb) {
            let obj = {};
            for (let i = 1; i <= 24; i++) {
                let box = document.querySelector('.box[data-index="' + i + '"]');
                if (box && box.classList.contains('lit')) {
                    let fa = feedAmounts[i] || { amount: 1.0, light: 'none', water: 0, minute: 0 };
                    // 修复：将前端的选项值转换为后端需要的数字字符串
                    let lightValue = 'none';
                    if (fa.light === 'option1') lightValue = '1';
                    else if (fa.light === 'option2') lightValue = '2';
                    else lightValue = '0';
                    obj[i] = { 
                        amount: Number(fa.amount || 1.0), 
                        light: lightValue, 
                        water: fa.water || 0,
                        minute: typeof fa.minute === 'number' ? fa.minute : 0
                    };
                }
            }
            fetch('/feed_plan', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(obj)
            }).then(() => {
                renderFeedInfoBox();
                if (cb) cb();
            });
        }

        function showFeedPopup(hour, boxEl) {
            if (popupEl) {
                popupEl.remove();
                popupEl = null;
            }
            document.body.style.overflow = 'hidden';

            let fa = feedAmounts[hour] || { amount: 1.0, light: 'none', water: 0, minute: 0 };
            let amount = fa.amount ?? 1.0;
            let light = fa.light ?? 'none';
            let water = fa.water ?? 0;
            let minute = (typeof fa.minute === 'number') ? fa.minute : 0;

            popupEl = document.createElement('div');
            popupEl.style.position = 'fixed';
            popupEl.style.left = '0';
            popupEl.style.top = '0';
            popupEl.style.width = '100vw';
            popupEl.style.height = '100vh';
            popupEl.style.background = 'rgba(0,0,0,0.18)';
            popupEl.style.display = 'flex';
            popupEl.style.alignItems = 'center';
            popupEl.style.justifyContent = 'center';
            popupEl.style.zIndex = '9999';

            popupEl.innerHTML = `
        <div style="background:#fff;border-radius:12px;box-shadow:0 2px 16px #0002;padding:22px 22px 18px 22px;min-width:240px;max-width:96vw;">
            <div style="font-size:17px;font-weight:500;margin-bottom:12px;text-align:center;">
                <b>${hour}：</b>
                <input type="number" min="0" max="59" value="${minute.toString().padStart(2, '0')}" id="minuteInput" style="width:48px;font-size:17px;font-weight:bold;text-align:center;border:1px solid #ccc;border-radius:4px;">
            </div>
            <div style="margin-bottom:12px;">
                <label style="font-size:15px;font-weight:bold;">喂食相对数量：</label>
                <input type="range" min="0.1" max="5.0" step="0.1" value="${Number(amount).toFixed(1)}" id="feedAmountRange" style="width:120px;vertical-align:middle;">
                <input type="number" min="0.1" max="5.0" step="0.1" value="${Number(amount).toFixed(1)}" id="feedAmountInput" style="width:48px;vertical-align:middle;">
            </div>
            <div style="margin-bottom:12px;">
                <label style="font-size:15px;font-weight:bold;">灯光效果：</label>
                <select id="lightEffectSelect" style="font-size:15px;">
                    <option value="none" ${light === 'none' ? 'selected' : ''}>无</option>
                    <option value="option1" ${light === 'option1' ? 'selected' : ''}>选项1</option>
                    <option value="option2" ${light === 'option2' ? 'selected' : ''}>选项2</option>
                </select>
            </div>
            <div style="margin-bottom:18px;">
                <label style="font-size:15px;font-weight:bold;">水流时间：</label>
                <input type="range" min="0" max="5" step="1" value="${Number(water)}" id="waterTimeRange" style="width:90px;vertical-align:middle;">
                <input type="number" min="0" max="5" step="1" value="${Number(water)}" id="waterTimeInput" style="width:38px;vertical-align:middle;">
                <span style="font-size:13px;">秒</span>
            </div>
            <div style="text-align:center;">
                <button id="closePopupBtn" style="padding:6px 18px;font-size:15px;border-radius:6px;border:1px solid #ccc;background:#f5f5f5;">关闭</button>
            </div>
        </div>
    `;
    document.body.appendChild(popupEl);

            // 分钟输入框变化处理
            const minuteInput = popupEl.querySelector('#minuteInput');
            minuteInput.oninput = function() {
                let newMinute = Math.max(0, Math.min(59, Number(this.value) || 0));
                this.value = newMinute.toString().padStart(2, '0');
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].minute = newMinute;
                // 更新原始 box 的显示
                let label = boxEl.previousElementSibling;
                if (label && label.classList.contains('label')) {
                    label.textContent = hour + ':' + this.value;
                }
                renderFeedInfoBox();
            };

            // 同步滑块和数字输入
            const range = popupEl.querySelector('#feedAmountRange');
            const input = popupEl.querySelector('#feedAmountInput');
            range.oninput = function() { 
                input.value = range.value; 
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].amount = Number(range.value);
                updateLitBoxColors();
                renderFeedInfoBox();
            };
            input.oninput = function() {
                let v = Math.max(0.1, Math.min(5.0, Number(input.value) || 0.1));
                input.value = v.toFixed(1);
                range.value = v.toFixed(1);
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].amount = Number(range.value);
                updateLitBoxColors();
                renderFeedInfoBox();
            };
            range.onchange = input.onchange = function() {
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].amount = Number(range.value);
                updateLitBoxColors();
                renderFeedInfoBox();
            };

            // 灯光效果下拉框
            const lightSelect = popupEl.querySelector('#lightEffectSelect');
            lightSelect.onchange = function() {
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].light = this.value;
            };

            // 水流时间
            const waterRange = popupEl.querySelector('#waterTimeRange');
            const waterInput = popupEl.querySelector('#waterTimeInput');
            waterRange.oninput = function() {
                waterInput.value = waterRange.value;
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].water = Number(waterRange.value);
            };
            waterInput.oninput = function() {
                let v = Math.max(0, Math.min(5, Number(waterInput.value) || 0));
                waterInput.value = v;
                waterRange.value = v;
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].water = Number(v);
            };
            waterRange.onchange = waterInput.onchange = function() {
                if (!feedAmounts[hour]) feedAmounts[hour] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                feedAmounts[hour].water = Number(waterRange.value);
            };

            // 关闭弹窗
            popupEl.querySelector('#closePopupBtn').onclick = function() {
                popupEl.remove();
                popupEl = null;
                document.body.style.overflow = '';
            };
            popupEl.addEventListener('click', function(e){
                if(e.target === popupEl){
                    popupEl.remove();
                    popupEl = null;
                    document.body.style.overflow = '';
                }
            });
        }

        document.querySelectorAll('.box').forEach(box => {
            box.addEventListener('click', function() {
                this.classList.toggle('lit');
                updateLitBoxColors();
                renderFeedInfoBox();
            });
            let pressStart = null;
            box.addEventListener('touchstart', function(e){
                if (popupEl) return;
                pressStart = Date.now();
                longPressTimer = setTimeout(() => {
                    showFeedPopup(box.getAttribute('data-index'), box);
                }, 500);
            });
            box.addEventListener('touchend', function(e){
                clearTimeout(longPressTimer);
            });
            box.addEventListener('touchmove', function(e){
                clearTimeout(longPressTimer);
            });
            box.addEventListener('mousedown', function(e){
                if (popupEl) return;
                if (e.button !== 0) return;
                longPressTimer = setTimeout(() => {
                    showFeedPopup(box.getAttribute('data-index'), box);
                }, 500);
            });
            box.addEventListener('mouseup', function(e){
                clearTimeout(longPressTimer);
            });
            box.addEventListener('mouseleave', function(e){
                clearTimeout(longPressTimer);
            });
        });

        document.addEventListener('DOMContentLoaded', function() {
            for (let i = 1; i <= 24; i++) {
                feedAmounts[i] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
            }
            updateLitBoxColors();
            renderFeedInfoBox();
            loadFeedPlan();
        });

        document.getElementById('outputBtn').onclick = function() {
            saveFeedPlan();
            PRIN();
        };

        document.getElementById('resetBtn').onclick = function() {
            document.querySelectorAll('.box.lit').forEach(box => box.classList.remove('lit'));
            for (let i = 1; i <= 24; i++) {
                feedAmounts[i] = { amount: 1.0, light: 'none', water: 0, minute: 0 };
                let box = document.querySelector('.box[data-index="' + i + '"]');
                let label = box?.previousElementSibling;
                if (label && label.classList.contains('label')) {
                    label.textContent = i + ':00';
                }
            }
            updateLitBoxColors();
            renderFeedInfoBox();
            saveFeedPlan();
        };
    </script>
</body>
</html>
)rawliteral";

// feedSettingPage 返回 PROGMEM 指针
const char* feedSettingPage() {
  return FEED_SETTING_HTML;
}

void setup() {
  // 初始化串口，方便调试输出
  Serial.begin(115200);

  // 初始化EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadParams(); // 启动时加载参数

  // 设置为AP+STA混合模式
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));//IP 网关 子网掩码

  // 启动WiFi热点
  WiFi.softAP(SSID_NAME);  

  // 启动DNS服务器，实现DNS欺骗（所有域名都指向本机IP）
  dnsServer.start(DNS_PORT, "*", APIP);

  // 配置Web服务器的路由
  webServer.on("/post",[]() { 
    webServer.send(HTTP_CODE, "text/html", posted());
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
  // 新增：喂食设置页面
  webServer.on("/feed", []() {
    webServer.send_P(HTTP_CODE, "text/html", FEED_SETTING_HTML);
  });
  // 新增：喂食计划接口
  webServer.on("/feed_plan", HTTP_GET, []() {
    webServer.send(200, "application/json", loadFeedPlanJson());
  });
  webServer.on("/feed_plan", HTTP_POST, []() {
    String body = webServer.arg("plain");
    saveFeedPlan(body);
    PRIN();
    webServer.send(200, "text/plain", "OK");
  });
  webServer.onNotFound([]() { 
    webServer.send(HTTP_CODE, "text/html", index());
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