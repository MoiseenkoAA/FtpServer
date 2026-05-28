// CryptRSAGenKeys.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "version_.h"
#include "keys.h"

#ifdef _UNICODE
#ifdef printf
#undef printf
#endif
#define printf __utf8_printf
/*
#ifdef _tprintf
#undef _tprintf
#endif
#define _tprintf __unicode_printf
*/
#else
#ifdef __unix__
#ifdef printf
#undef printf
#endif
#define printf __utf8_printf
#endif
#endif

#define printf2 __utf8_printf2


#define PRODUCT_NAME    "FtpServer"
#define PRODUCT_VERSION FTP_SERVER_VERSION //"1.0"

#define TR(x) x

CMaaXmlDocument gDecrReg("RegFile");

//#define XML_REG_FILENAME "reg.xml"
#define BIN_REG_FILENAME "reg.bin"

bool CheckRegistrationFile(const char * pBinFileName /*= BIN_REG_FILENAME*/,
     LongInt2 &n, LongInt2 &e, bool bWriteOut = false);

bool CheckRegistrationFile(const char * pBinFileName /*= BIN_REG_FILENAME*/,
     LongInt2 &n, LongInt2 &e, bool bWriteOut)
{
     printf("\n");
     try
     {
          CMaaFile f(pBinFileName, CMaaFile::eR);
          int l = (int)f.Length();
          if   (l < 0 || l > 20 * 1024)
          {
               printf("Bad length of bin file: %d\n", l);
               throw 1;
          }
          l = l >= 0 ? l : 0;
          CMaaPtr_<char, 1> Buffer(l);
          l = f.Read(Buffer, l);
          f.Close();

          LongInt2 Dec;
          time_t t;
          int y = RSADecrypt(n, e, Buffer, l, Dec, &t);
          if   (y < 0)
          {
               printf("RSADecrypt() returns error %d\n", y);
               throw y;
          }

          CMaaString s(Dec(), y);

          if   (bWriteOut)
          {
               f = CMaaFile("Decr_reg.xml"/*pDecrFileName*/, CMaaFile::eWC);
               f.Write(s, s.Length());
               f.Close();
          }

          CMaaXmlDocument doc("RegFile");
          CMaaString errorMsg;
          int errorLine, errorColumn;

          if   (!doc.SetContent(s, &errorMsg, &errorLine, &errorColumn, 0))
          {
               CMaaString txt;
               txt.Format2("%s%d%d%s", TR("Error in config file \"%1\"\nLine: %2, Column: %3, Error: %4\nCreating default config..."), pBinFileName, errorLine, errorColumn, (const char *)errorMsg);
               //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
               printf("%s\n", (const char *)txt);
               throw 1;
          }
          else
          {
               doc.ConvertToUtf8();
               CMaaXmlElement elm = doc.DocumentElement();
#ifdef _WIN32
#define translate(x) (x)
#else
#define translate(x) UnicodeToAnsi(Utf8ToUnicode(x)).Translit()
#endif
               CMaaString ProductName = elm.FindAttribute("Product");
               CMaaString ProductVersion = translate(elm.FindAttribute("Version"));
               CMaaString SerialNumber = translate(elm.FindAttribute("SerialNumber"));
               CMaaString RegisterQuantity = translate(elm.FindAttribute("Quantity"));
               CMaaString RegisterName = translate(elm.FindAttribute("UserName"));
               CMaaString RegisterEmail = translate(elm.FindAttribute("UserEmail"));
               CMaaString RegisterTown = translate(elm.FindAttribute("UserTown"));
               CMaaString RegistrationDate = translate(elm.FindAttribute("RegistrationDate"));
               CMaaString RegisterOptions = translate(elm.FindAttribute("Options"));
               if   (ProductName != PRODUCT_NAME)
               {
                    printf("Registration file is not for the current product.\n");
                    throw 1;
               }
               
               ProductName = translate(ProductName);

               gDecrReg = doc;

               CMaaString DispTxt1, DispTxt;
               DispTxt1.Format(TR("Registered to:\nUser name: %S\nUser e-mail: %S\nTown: %S\nProduct: %S, version: %S, quantity: %S\nRegistration date: %S\n"/*"Serial number: %S\n"*/),
                    &RegisterName, &RegisterEmail, &RegisterTown, &ProductName, &ProductVersion, &RegisterQuantity, &RegistrationDate);//, &SerialNumber);

#ifdef _WIN32
//               DispTxt = DispTxt1.CharToOem();
               DispTxt = DispTxt1;
#else
               DispTxt = DispTxt1.Translit();
#endif
               printf("%s\n", (const char *)DispTxt);
               return true;
          }
     }
     catch(XTOOFile2Error e)
     {
          //CProtocolListColor c(CProtocolListColor::eRed);
          printf2("%s", "File error: %1\n", e.GetMsg());
          //CMaaString txt;
          //txt.Format2("%s", "File error: %1", e.GetMsg());
          //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
          //printf("%s\n", (const char *)txt.CharToOem());
     }
     catch(int)
     {
     }
     catch(...)
     {
          //CProtocolListColor c(CProtocolListColor::eRed);
          CMaaString txt;
          txt.Format2("", "Unknown error");
          //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
          printf("%s\n", (const char *)txt);
     }
     printf("%s\n", TR("Full functional but UNREGISTERED version."));
     return false;
}

void CheckLicense()
{
     int N = ::N;
     int R = N & ~1;
     LongInt2 n(N);
     LongInt2 e(N);//, d(N);

     n.LoadFromMem(::n, ::N);
     //d.LoadFromMem(::d, ::N);
     e.LoadFromMem(::e, ::N);

     if   (!CheckRegistrationFile(BIN_REG_FILENAME, n, e, false))
     {
          throw 1;
     }
}
