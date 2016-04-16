
/*
  Extending m590 at commands
  with micro-pro in a middle

  added commands:
    at+wget=www.google.com
    at+debug=1
    at+dns=www.google.com
 
*/

#include "Neoway_m590.cpp"

NeowayM590 m; // my modem


#include <stdarg.h>
#define PRINTF_BUF 80 // define the tmp buffer size (change if desired)
   void outf(const char *format, ...)
   {
   char buf[PRINTF_BUF];
   va_list ap;
        va_start(ap, format);
        vsnprintf(buf, sizeof(buf), format, ap);
        for(char *p = &buf[0]; *p; p++) // emulate cooked mode for newlines
        {
                if(*p == '\n')  Serial.write('\r');
                Serial.write(*p);
        }
        va_end(ap);
   }


int lineno = 0, needHeaders=0;  

void  onMyDataLine(char *data,int len ,void *sender) {
  int i;
  unsigned char *b=(unsigned char*)data;
  for(;len>0;len--,b++) if (b[0]>32) break; // ltrim
  for(;len>0;len--) if (b[len-1]>32) break; // rtrim
  b[len]=0;
  if (m.fTCP == 2)  outf("%s\n",b);
         else if (needHeaders) outf("%s\n",b);
}


int ensure_ppp() {
int ok;
if (!m.fPDP) { // auto attach PDP  - if not yet attached
ok = m.attachPDP("internet"); //"internet.mts.ru");
 if (ok>0) {
         m.printf("  --pdp attached\n");
         } else {
         m.printf(" --fail attach pdp\n");
         return 0;
         }
}
ok = m.connectPPP(); // check if connected, reconnect if not
m.printf("  --ppp connected my_ip:%d\n",ok);
return ok;  
}

int _at_wget(char *host,char *page,int debug=0) { // process wget command
int ok = 0; char ip[12];
      //Serial.println("apn\n");
      //Serial.println("IMEI:"+String( m.imei()) );
m.fDebug = debug; m.fEcho = 0;
ok = ensure_ppp();
if (ok<=0) {
   outf("-fail launch ppp");
   return -1;
}
if (ok) {
  ip[0]=0;
ok = m.dns(host,ip);
if (debug) outf(" --- dns=%d for %s ip:'%s'\n",ok,host,ip);
if (ok<=0) {
    outf("-fail get dns for %s",host);
    return 0;
    }

}

//m.fEcho = 1;
if (ok)   {
   ok = m.tcp_connect(ip,80); // try connect
   m.wait(100);
   if(debug) outf(" -- tcp_connected:%d fTCP:%d with ip:%s\n",ok,m.fTCP,ip);
   if (ok<=0) {
      outf("-%s - tcp_connect failed\n",ip);
      return 0;
     }
   }
if (ok) {
         m.wait(300);
         char buf[40];
         sprintf(buf,"GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n",page,host);
         ok = m.tcp_send(buf);
         if (debug) outf("tcp_send=%d\n",ok);
             while (m.fTCP>0) {
                m.wait(500); // wait till disconnected
                if (debug) outf(" -- m.fTCP=%d\n",m.fTCP);
             }
         if (debug) outf(" --disconnected\n");
         };
return ok;
}


int at_wget(char *url) {
char *p=strchr(url,'/'); if (p) { *p=0; p++;} else p="";
m.fEcho = 0;
 lineno=0; needHeaders=0; // no headers, show only content.
int ok = _at_wget(url,p,m.fDebug); // host and page & debug flag
m.fEcho = 1; 
if (ok>0) outf("OK\n");
   else outf("ERROR\n");
return ok;
}
 
void setup() {
// initialize both serial ports:
Serial.begin(115200);  // to USB
Serial1.begin(19200); // to modem // at+IPR=19200

 m.setup(&Serial1,&Serial); // Attached to real serial port (with output to CDC-Serial)
 m.onDataLine =onMyDataLine;

}

void run_in_cmd() {

  if (strncmp(m.inBuf,"at+wget=",8)==0) {
    int ok;
      at_wget(m.inBuf+8);
     } 
  else  if (strncmp(m.inBuf,"at+debug=",9)==0) {
    int ok;
      m.fDebug=atoi(m.inBuf+9);
      outf("OK\n");      
     } 
   else  if (strncmp(m.inBuf,"at+gpio=",8)==0) {
    int ok,pin,state;
    sscanf(m.inBuf+8,"%d,%d",&pin,&state);
      pinMode(pin,OUTPUT);
      outf(" --pin:%d state:%d now\n",pin,state);
      digitalWrite(pin,state?HIGH:LOW);
      outf("OK\n");      
     } 
  else if (strncmp(m.inBuf,"at+dns=",7)==0) {
    int ok; char ip[16];
      m.fEcho=0; 
      ok = ensure_ppp();
      if (ok>0)  ok = m.dns(m.inBuf+7,ip);
      //outf("\n  ---dns result %d\n",ok);
      delay(200);  m.wait(200); // clear all other responces
      if (ok>0) { outf("%s\nOK\n",ip); }
         else  outf("ERROR\n");
      m.fEcho=1;   
    }
    
     else { // just print collected buffer to modem
         Serial1.print(m.inBuf); Serial1.print("\r"); // push to modem...
    }
  
}

void loop() {
//while(Serial1.available()) Serial.write(Serial1.read());
while(m.step()); // do

while(Serial.available()) {
   char ch = Serial.read();
   if (ch=='\n') ;  // ignore
   else if  (ch=='\r') { // Push it
      m.inBuf[m.inLen]=0;
      run_in_cmd();
      m.inLen=0; m.inBuf[0]=0;
      }
   else if (m.inLen<79) {m.inBuf[m.inLen]=ch; m.inLen++;}
   //Serial1.write(Serial.read());
   }

}
