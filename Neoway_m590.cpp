
/*
 
   GPRS modem Neoway m590
   internal tcp/ip support class

   (c),  vslipenchuk@gmail.com


Bugs of m590:
   Some Http servers that quickly close connections (with 302 Moved error) detected as "fail tcp_connect"
   example: google.com yandex.ru.

*/


#include <Arduino.h>
#include <Stream.h>

#define BUF_SIZE 80

class NeowayM590 {  
  public:
  char inBuf[BUF_SIZE]; int inLen=0; // input  
  char outBuf[BUF_SIZE]; int outLen=0; // output
  char data[BUF_SIZE]; int dataLen=0; // data by tcp_recv here
  char ip[16];
  Stream *d,*_out; // modem and output (screen) 
  int ok;
  int ip_status; // connected=1, disconnected = -1
  char *bin=0; 
  int  waitTcpData=0;   // expected TCP data on read stream ...
  char fEcho=1; // echo output to _out stream
  char fPDP=0,fPPP=0,fLine; // PPP, TCP connection flags
  int  fTCP = 0,fDebug=0;
  char fCopyToData; // request copies string to data buffer (IMEI, MSISDN & other one-line answers
  char *copyIP; // ex ip for copy
  void (*onDataLine)(char *data,int len, void *handle); // caller
  
  bool available() {
    return fTCP && fLine;
    };
  char *read() { // wait till line ready
    while (fTCP && fLine==0) wait(100); // collect
    if (!fLine) return "";
    fLine=0; // clear
    return data; 
    
  };
  void setup(Stream *Serial,Stream *Output=0,int _echo=1) {
    d=Serial;   _out=Output; fEcho = _echo; // by Default
  };
  
   void printf(const char *format, ...)
   {
   char buf[80];
   if (!_out || !fDebug) return;
   va_list ap;
        va_start(ap, format);
        vsnprintf(buf, sizeof(buf), format, ap);
        for(char *p = &buf[0]; *p; p++) // emulate cooked mode for newlines
        {
                //if(*p == '\n')  Serial.write('\r');
                _out->print(*p);
        }
        va_end(ap);
   }
  void flashDataLine() {
    data[dataLen]=0;
     if (onDataLine) onDataLine(data,dataLen,this);
      //print("LINE:");
      //print(data);
    if (fTCP == 1 && dataLen == 2 && data[0]=='\r' && data[1]=='\n') {
        printf("  ---Headers done\n");
        fTCP=2;
        }
    dataLen=0; data[0]=0; // do it again
    //fLine=1; // Line ready flag. stop reading from 
  }
  void pushTcpRecv(char *ch,int l) {
  while(l>0) {
             //Serial.println("PUSH:"+String(ch[0]));
         if ( dataLen<sizeof(data)-2) {  // add to buf if we have a place
              data[dataLen]=*ch;  dataLen++;  data[dataLen]=0;
              } 
        if (waitTcpData>0) waitTcpData--; // expected TCP len
        if (*ch=='\n'   || dataLen==sizeof(data)-2 || waitTcpData==0  ) flashDataLine();
        ch++;  l--; 
        }
  };  
  bool step() { // reads from in to buffer and print on a screen
    if (!d->available()) return false;
    char ch = d->read();
    if (waitTcpData>0) { // Bulk data after +TCPRECV line
      pushTcpRecv(&ch,1);
      return true;
      }
    if (ch == '>' && outLen == 0  && bin) { // first symb
         //print("sending data\n");
      d->println(bin);
      bin=0;
      return true;
      }
    else if ((ch =='\r') || (ch =='\n')) { // flash a line
       if (strncmp(outBuf,"+TCPRECV:",9)==0) {
        char *p;  int l;
         outBuf[outLen]=ch; outLen++;
         p = outBuf + 9;
         p = strchr(p,',');
         if (p) {
             p++;
             l=sscanf(p,"%d",&waitTcpData); // full len here...
             p=strchr(p,','); 
             if (l==1 && p) {
                p++;
                l = (outLen - (p - outBuf)); //  text len here !!!
                pushTcpRecv(p,l);  outLen=0; outBuf[0]=0; // clear in buffer
                //if (_out) { _out->print("["); _out->print(len); _out->println("]");}
                } else waitTcpData=0; // syntax parse error !!???
         }
         return true;
         }
      outBuf[outLen]=0;
      if (fCopyToData==1 && outLen>0) { strcpy(data,outBuf); fCopyToData=2; } // copy non-zero answers
      //Serial.println("<IN:"+String(outBuf)+">\r"); // debug
      if ( strcmp(outBuf,"OK")==0) ok++;
      else if (strncmp(outBuf,"+DNS:",5)==0 ) {
        char *p=outBuf+5; p[15]=0;
        //printf("--- Found +DNS: '%s' copy it",p);
        //if (fCopyToData==3) strcpy(data,p);
        if (copyIP) {strcpy(copyIP,p); copyIP=0;}
        fCopyToData = 4; // done
        ok = strstr(p,"Error")?-1:1; // ok or not?
        outLen=0; outBuf[0]=0; // clear out
        return true;
        }
      if (strncmp(outBuf,"+IPSTATUS:",10)==0) {
        char *p;
        p=outBuf+10;
        if (strstr(p,"DISCONNECT"))  { ip_status = -2; fTCP=-1;}
         else if (strstr(p,"CONNECT")) { ip_status = 1; fTCP=1; fLine=0;}
        ok = 1; // treet it as OK     
        }
      if (strncmp(outBuf,"+CGATT:",7)==0) {
            fPDP=atoi(outBuf+7);
           }
      if (strncmp(outBuf,"MODEM:STARTUP",13)==0) { // modem reset - POWER problem
         printf(" -- reset all flags, modem reset");
         fTCP=fPDP=fPPP=0;
         waitTcpData=0; 
         }
      if (strncmp(outBuf,"+TCPCLOSE:",10)==0) { 
          printf("---LinkClosed OK----\n"); fTCP=-1; fLine=0;} 
      if (strncmp(outBuf,"+TCPSEND:",9)==0) {
            char *p = outBuf+9;
            if (strncmp(p,"Error",5)==0) ok=-2; // fail send
           }
      
      if (strncmp(outBuf,"+XIIC:",6)==0) { // here is IP addr ready!
          char *p=outBuf+6;
          while(*p && *p!=',') p++;
          if (*p==',') {
            p++; // ipaddr here till eol
            while(*p && *p==' ') p++;
            if (strcmp(p,"0.0.0.0")!=0) {
             strncpy(ip,p,15); ip[15]=0; fPPP=1;
               printf(" --- ppp ip: %s\n ",ip);
             }
          }
           
          
          }
      outLen=0; outBuf[0]=0; // clear out
      } else  if (outLen<sizeof(outBuf)-1) {
          outBuf[outLen]=ch; outLen++;
          }
      if (_out && fEcho) _out->print(ch); // echo output to Out stream
    return true;
    };
  int wait(int msec) {
     ok = 0;
      while(msec>=0) {
       while (step());
       if (ok) return ok; // ok
       delay(10); msec-=10;
       }
     return 0;
     }
  bool at(char *cmd) {  // run at command - wait for answer
    d->print("at"); d->print(cmd); d->print("\r");
    return wait(300)>0; // now - wait OK and push an answer
    };
  bool atf(char *cmd, ...) { // formatted AT
     char buf[40];
     va_list ap;
        va_start(ap, cmd);
        vsnprintf(buf, sizeof(buf), cmd, ap);
     va_end(ap);
     return at(buf);
   }
  char *str_at(char *cmd) { // One line answer AT-commands
      fCopyToData=1;
      at(cmd);
      if (fCopyToData==2) return data; // have fired
      return ""; // not fired
    }
    
  char *imei() { return str_at("+cgsn");}
  char *imsi() { return str_at("+cimi");}
    
  bool at(String cmd) {   return at(cmd.c_str());  }
  bool attachPDP(char *APN,int timeOut=20) {
     int i;     
     atf("+CGDCONT=1,\"IP\",\"%s\"",APN);
       wait(100); // just wait
     at("+CGATT=1"); // attach PDP
      fPDP = 0;
     for(i=0;i<timeOut*10;i++) {
        at("+CGATT?"); // must OK
        if (fPDP>0) return true;
        wait(100);
        }
    return false;
    }
  bool dns(char *host,char *ip=0) { // if ok - than data has ip addr
    copyIP=ip;
    int ok = atf("+DNS=\"%s\"",host);
    if (ok<0) return false; // +DNS:Error - is abad result 
    fCopyToData = 3; // wait this flag
       wait(5000); // max 5 second
    copyIP=0; 
    //if (ok && ip) strcpy(ip,data);
    if (ok) wait(200); // read all other DNS records
    return ok; // copied OK
    }
  bool getip(int sec=20) { // open pppd
      ip[0]=0;
    while( sec>0) {
      at("+xiic?");
      if (ip[0]) return true;  //OK
      wait(1000); sec--;
      }
    return false;
    };
  bool connectPPP() {
      fPPP=0; // clear PPP flag
      at("+XISP=0"); // internal IP stack
      at("+XIIC=1"); // establish IP link
      getip(20); //wait for it
      return fPPP>0;
    };
  int  tcp_status() {
    at("+IPSTATUS=0");
    return ip_status;
    }
  void tcp_close() {
    fTCP=0; fLine=0;
    at("+tcpclose");
    };

  bool tcp_connect(char *ip,int port,int timeOut=10) { // try tcp_connect (with 10 sec timeout)
    int i;
    tcp_close();
    
    atf("+tcpsetup=0,%s,%d",ip,port);
    delay(100);
    for(i=0;i<timeOut*10;i++) {
      int ok = tcp_status();
      if (fTCP>0) return true; // connected
      if (fTCP<0) return false; // failed
         printf(" -- begin wait tcp_connect fTCP=%d\n",fTCP);
      wait(100);
        //print("  --end wait\n");
      }
    return false; // failed
    };
  bool tcp_send(char *text) {
    bin=text; // will be sent on prompt '>' 
    return atf("+tcpsend=0,%d",strlen(text));
    }
  
  };
