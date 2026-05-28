// FtpServer1.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "FtpServer.h"

#ifdef _UNICODE
#ifdef printf
#undef printf
#endif
#define printf printf_time(); __utf8_printf
/*
#ifdef _tprintf
#undef _tprintf
#endif
#define _tprintf __unicode_printf
*/
#else
#ifdef __unix__
#define printf printf_time(); __utf8_printf
#endif
#endif

#define printf2 printf_time(); __utf8_printf2


/*
#ifdef _WIN32
#define FTP_ROOT_DIR "C:\\Ftp"
#else
//#define FTP_ROOT_DIR "/home/maa"
#define FTP_ROOT_DIR "/data/home/maasoftw"
#endif
*/

#ifdef __unix__
_dword GetTickCount();
/*
_dword GetTickCount()
{
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
*/
#endif

CMaaString FailLog;

CMaaMutex g_Mutex;
CMaaString g_ConfigFileName;
static _qword s_FileTime = -2, s_FileSize = -2;
static time_t s_LastUpdatedTime = -1;
CMaaXmlDocument m_Cfg("FtpServer");

#ifdef __unix__
#include <signal.h>

static void OnMySIGHUP(int x)
{
     signal(SIGHUP, OnMySIGHUP);

     s_FileTime = -2, s_FileSize = -2;
     s_LastUpdatedTime = -1;

     //printf ( "OnMySIGPIPE\n" );
     //fflush ( stdout );
     return;
}
#endif

void printf_time()
{
    CMaaString txt = GetTextDateTime(time(NULL)) + ": ";
    __utf8_printf("%S", &txt);
}

int gChildren = 0;
#ifdef __unix__
CMaaPtrAE<pid_t> ChildrenPids(10);
#endif

const int TIME_OUT_1 = 240000000;

#define DEF_m_TimerTimeOut10_Start_TIME_OUT_1 gLock.LockM(); m_TimerTimeOut10.Start(TIME_OUT_1); gLock.UnLockM()

CMaaString ToFtpSafe(CMaaString txt)
{
    int l = txt.Length();
    for  (int i = 0; i < l; i++)
    {
        if   ((unsigned char)txt[i] < ' ' || (unsigned char)txt[i] > 0x7f)
        {
            txt[i] = '_';
        }
    }
    return txt;
}

CMaaString ToFtpSafeUtf8(CMaaString txt)
{
    return txt.Utf8ToPrintable(false);
}

int GetHexNibble(char c)
{
    if   (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if   (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if   (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

#ifdef _WIN32
#define _WC_ WCHAR
#else
#define _WC_ unsigned short
#endif

int RussianAscii1251CarsCount(CMaaString str, int CountOne = 0)
{
    CMaaString RussianAlphabet = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
    int Count = 0;
    const char * pp = (const char *)(const char *)str;
    const char * ee = pp + str.Length();
    while(pp < ee)
    {
        char c = *pp++;
        if   (RussianAlphabet.Find(c) >= 0)
        {
            Count++;
            if   (CountOne)
            {
                break;
            }
        }
    }
    return Count;
}

int RussianOemCarsCount(CMaaString str, int CountOne = 0)
{
    CMaaString RussianAlphabet = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
    int Count = 0;
    str = str.OemToChar();
    const char * pp = (const char *)(const char *)str;
    const char * ee = pp + str.Length();
    while(pp < ee)
    {
        char c = *pp++;
        if   (RussianAlphabet.Find(c) >= 0)
        {
            Count++;
            if   (CountOne)
            {
                break;
            }
        }
    }
    return Count;
}

CMaaString FromUtf8Ex(CMaaString Text)
{
    int n1251 = RussianAscii1251CarsCount(Text);
    int nOem = RussianOemCarsCount(Text);
    int nUtf8 = Text.Utf8CharCount() * 2;
    if   (nUtf8 >= n1251 && nUtf8 >= nOem)
    {
    }
    else if (n1251 > 0 && n1251 > nOem)
    {
        Text = UnicodeToUtf8(AnsiToUnicode(Text, false, 1251));
    }
    else if (nOem > 0)
    {
        Text = UnicodeToUtf8(AnsiToUnicode(Text.OemToChar(), false, 1251));
    }

#if 0
    int l = Text.GetLength();
    int un = 0, rn = 0, en = 0;
    try
    {
        //CMaaPtr<char> Buffer (2 * l + 40);
        //CMaaPtr<WCHAR> BufferU (l + 4);
        CMaaPtr<char> Buffer(2 * l + 40, 1);
        CMaaPtr<_WC_> BufferU(l + 4, 1);
        if   (!Buffer.IsValid() || !BufferU.IsValid())
        {
            throw 1;
        }
        _WC_ * u = BufferU;
        memcpy(Buffer, Text, Text.Length());
        memset(Buffer + Text.Length(), 0, 4);

        const char * p = Buffer;
        int j = 0;
        for  (int i = 0; i < l; i++)
        {
            u[j++] = (unsigned short)(unsigned char)p[i];
            long UnicodeChar = -1;
            //WCHAR & w = *(WCHAR *)&UnicodeChar;
            //int
            unsigned char * pp = (unsigned char *)p;
            if   ((pp[i] & 0xE0) == 0xC0 &&  (pp[i+1] & 0xC0) == 0x80)
            {
                UnicodeChar = (pp[i+1] & 0x3f) | ((pp[i] & 0x1f) << 6);
                i++;
            }
            else if ((pp[i] & 0xf0) == 0xe0 && (pp[i+1] & 0xc0) == 0x80 && (pp[i+2] & 0xc0) == 0x80)
            {
                UnicodeChar = (pp[i+2] & 0x3f) | ((pp[i+1] & 0x3f) << 6) | ((pp[i] & 0x0f) << 12);
                i += 2;
            }
            else if ((pp[i] & 0xf8) == 0xf0 && (pp[i+1] & 0xc0) == 0x80 && (pp[i+2] & 0xc0) == 0x80 && (pp[i+3] & 0xc0) == 0x80)
            {
                UnicodeChar = (pp[i+3] & 0x3f) | ((pp[i+2] & 0x3f) << 6) | ((pp[i+1] & 0x3f) << 12) | ((pp[i] & 0x07) << 18);
                UnicodeChar = '_';
                i += 3;
            }
            if   (UnicodeChar >= 0)
            {
                u[j - 1] = (_WC_)UnicodeChar;
                un++;
                //u[j - 1] = w;
                /*
                    if (UnicodeChar == 0)
                    {
                         p[j - 1] = ' ';
                    }
                    else if (UnicodeChar < 0x80)
                    {
                         p[j - 1] = (char)UnicodeChar;
                    }
                    else if (UnicodeChar >= 0x410 && UnicodeChar < 0x450)
                    {
                         const char * Rus = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
                         p[j - 1] = (UnicodeChar - 0x410) < (int)strlen(Rus) ? Rus[UnicodeChar - 0x410] : '!';
                    }
                    else if (UnicodeChar == 0x451)
                    {
                         p[j - 1] = '¸';
                    }
                    else if (UnicodeChar == 0x401)
                    {
                         p[j - 1] = '¨';
                    }
                    else
                    {
                         p[j - 1] = '?';
                    }
                    */
            }
            else if (u[j-1] >= 0x80)
            {
                rn++;
            }
        }
        u[j] = 0;
#ifdef _WIN32
        j = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)(WCHAR*)(_WC_*)BufferU, j, (char *)Buffer, (int)(Buffer.Size() - 1), NULL, NULL);
#else
        char * pb = Buffer;
        for  (int i = 0; i < j; i++)
        {
            long UnicodeChar = BufferU[i];
            if   (UnicodeChar == 0)
            {
                pb[i] = ' ';
            }
            else if (UnicodeChar < 0x80)
            {
                pb[i] = (char)UnicodeChar;
            }
            else if (UnicodeChar >= 0x410 && UnicodeChar < 0x450)
            {
                static const char * Rus = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
                pb[i] = (UnicodeChar - 0x410) < (int)strlen(Rus) ? Rus[UnicodeChar - 0x410] : '!';
            }
            else if (UnicodeChar == 0x451)
            {
                pb[i] = '¸';
            }
            else if (UnicodeChar == 0x401)
            {
                pb[i] = '¨';
            }
            else
            {
                pb[i] = '?';
                en++;
            }
        }
#endif
        if   (j < 0)
        {
            j = 0;
        }
        if   (un >= rn)
        {
            return CMaaString(Buffer, j);
        }
    }
    catch(...)
    {
    }
#endif
    return Text;
}

CMaaString ToUtf8(CMaaString Text)
{
#if 0
    int l = Text.GetLength();
    try
    {
        //CMaaPtr<char> Buffer (4 * l + 40);
        //CMaaPtr<WCHAR> BufferU (l + 4);
        CMaaPtr<char> Buffer (4 * l + 40, 1);
        CMaaPtr<_WC_> BufferU (l + 4, 1);
        if   (!Buffer.IsValid() || !BufferU.IsValid() || l > 10 * 1024 * 1024)
        {
            throw 1;
        }
        _WC_ * w = BufferU;
        memcpy(Buffer, Text, Text.Length());
        memset(Buffer + Text.Length(), 0, 4);

        char * p = Buffer;
        char * pp = Buffer;
#ifdef __WIN32
        l = MultiByteToWideChar(CP_ACP, 0, (char *)Buffer, l, (LPWSTR)(WCHAR*)(_WC_*)BufferU, l + 4);
#else
        static bool b1st = true;
        static _WC_ Map[256];
        if   (b1st)
        {
            static const unsigned char * Rus = (const unsigned char *)"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
            for  (int i = 0; Rus[i]; i++)
            {
                _WC_ UnicodeChar = 0x410 + i;
                Map[Rus[i]] = UnicodeChar;
            }
            Map[(unsigned char)'¸'] = 0x451;
            Map[(unsigned char)'¨'] = 0x401;
            b1st = false;
        }
        for  (int i = 0; i < l; i++)
        {
            unsigned char c = (unsigned char)p[i];
            _WC_ UnicodeChar;
            if   (c <= 0x7f)
            {
                UnicodeChar = (_WC_)c;
            }
            else
            {
                UnicodeChar = Map[c] != 0 ? Map[c] : (_WC_)'_';
            }
            w[i] = UnicodeChar;
        }
#endif
        int j = 0;
        for  (int i = 0; i < l; i++)
        {
            //p[j++] = (unsigned char)(unsigned short)u[i];
            long u = (long)(unsigned short)w[i];
            if   (u <= 0x7f)
            {
                p[j++] = (unsigned char)u;
            }
            else if (u <= 0x7ff)
            {
                p[j++] = (char)(0xC0 | ((u >> 6) & 0x1f));
                p[j++] = (char)(0x80 | (u & 0x3f));
            }
            else if (u <= 0xffff)
            {
                p[j++] = (char)(0xE0 | ((u >> 12) & 0x0f));
                p[j++] = (char)(0x80 | ((u >> 6 ) & 0x3f));
                p[j++] = (char)(0x80 | (u & 0x3f));
            }
            else if (u <= 0x1fffff)
            {
                p[j++] = (char)(0xF0 | ((u >> 18) & 0x07));
                p[j++] = (char)(0x80 | ((u >> 12) & 0x0f));
                p[j++] = (char)(0x80 | ((u >> 6 ) & 0x3f));
                p[j++] = (char)(0x80 | (u & 0x3f));
            }
        }
        return CMaaString(pp, j);
    }
    catch(...)
    {
    }
#endif
    return Text;
}


CMaaString ToFileName(CMaaString txt)
{
    CMaaPtr<char> Buf(txt.Length());
    int i, j, n = txt.Length();
    for  (i = j = 0; i < n; i++)
    {
        if   (txt[i] == '%' && GetHexNibble(txt[i + 1]) >= 0 && GetHexNibble(txt[i + 2]) >= 0)
        {
            Buf[j++] = GetHexNibble(txt[i + 1]) * 0x10 + GetHexNibble(txt[i + 2]);
            i += 2;
        }
        else
        {
            Buf[j++] = txt[i];
        }
    }
    return FromUtf8Ex(CMaaString(Buf, j));
}

//---------------------------------------------------------------------------
CMaaFtpServer::CMaaFtpServer(CMaaFdSockets * pFdSockets, CMaaString Port, const char * ServerName)
:    CMaaUnivServer(pFdSockets, Port, ServerName),
m_IpPort(Port),
m_Timer0(this, 0),
m_ServName(ServerName)
{
    m_Timer0.Attach(pFdSockets);
    printf("Ready...\n");
}
//---------------------------------------------------------------------------
CMaaFtpServer::~CMaaFtpServer()
{
    //printf("Finished\n");
    //printf("CMaaFtpServer::~CMaaFtpServer()\n");
    //m_pFdSockets->m_pThread->RemoveWakeUpPair();
}
//---------------------------------------------------------------------------
int CMaaFtpServer::Notify_Error()
{
    XTOOSockErr err("CMaaFtpServer::Notify_Error()", NULL);
    printf("CMaaFtpServer::Notify_Error(): %s\n", err.GetMsg());
    CloseByException("Error");
    return 0;
}
//---------------------------------------------------------------------------
void CMaaFtpServer::OnTimer(int f)
{
    //    printf("CMaaFtpServer::OnTimer(): f = %d\n", f);
    CloseByException("");
}
//---------------------------------------------------------------------------
CMaaTcpSocket * CMaaFtpServer::CreateNewConnection(CMaaFdSockets * pFdSockets)
{
    printf("CMaaFtpServer::CreateNewConnection(): fork()\n");
#ifdef __unix__
    pid_t pid = fork();
    printf("CMaaFtpServer::CreateNewConnection(): fork() returns %d\n", (int)pid);
    if   (pid < 0)
    {
        return NULL;
    }
    if   (pid == 0)
    {
        m_pFdSockets->FXX_epollfd();
        //m_Timer0.Start(1000000);
        m_Timer0.Start(1);
        return new CMaaFtpServerConnection(pFdSockets, m_IpPort, "FTP server connection");
    }
    if   (pid > 0)
    {
        pFdSockets->FXX_epollfd();
        gLock.LockM();
        ChildrenPids[gChildren++] = pid;
        gLock.UnLockM();
    }
    return NULL;
#else
    return new CMaaFtpServerConnection(pFdSockets, m_IpPort, "FTP server connection");
#endif
}
//---------------------------------------------------------------------------
CMaaFtpServerConnection::CMaaFtpServerConnection(CMaaFdSockets * pFdSockets, CMaaString ServerIpPort, const char * ClassName)
:    CMaaTcpSocket(pFdSockets),
m_ConnName(ClassName),
m_ServerIpPort(ServerIpPort),
m_File(NULL),
m_Timer0(this, 0),
m_Timer1(this, 1),
m_Timer3(this, 3),
m_TimerTimeOut10(this, 10),
m_TimerAcceptTimeOut11(this, 11),
m_UserNode(NULL)
{
// printf("pFdSockets->LookEp();\n");
// pFdSockets->LookEp();
 
    m_pDataConn = NULL;
    m_pPasvServer = NULL;
    m_Step = 0;
    m_State = 0;
    m_Type = 'I';
    m_Rest = 0;
    m_PortIp = 0;
    m_PortPort = 0;
    m_Path = "/";
    m_TryN = 0;
    //m_TransferMode = -1;
    m_Timer0.Attach(pFdSockets);
    m_Timer1.Attach(pFdSockets);
    m_Timer3.Attach(pFdSockets);
    m_TimerTimeOut10.Attach(pFdSockets);
    m_TimerAcceptTimeOut11.Attach(pFdSockets);
}
CMaaFtpServerConnection::~CMaaFtpServerConnection()
{
    printf("CMaaFtpServerConnection::~CMaaFtpServerConnection()\n");
    {
        _IP Ip = 0;
        _Port Port = 0;
        GetConnInfo(NULL, NULL, &Ip, &Port);
        printf("%s\n", (const char *)CMaaString::sFormat("Closed connection for %I:%d", Ip, Port));
    }
#ifdef __unix__
	exit(EXIT_SUCCESS);
#endif
}
int CMaaFtpServerConnection::Notify_Accepted(_IP IpFrom, _Port Port)
{
    printf("%s\n", (const char *)CMaaString::sFormat("Accepted connection from %I:%d", IpFrom, Port));
    DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
    m_Step = 1;
    //m_OutBuffer  = "200-MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
    m_OutBuffer = "220 MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
    return eAll;
}
int CMaaFtpServerConnection::Notify_Read()
{
    char Buffer[10 * 1024];
    int r = RecvData(Buffer, sizeof(Buffer));
    //     printf("CMaaFtpServerConnection::Notify_Read(): r = %d\n", r);
    if   (IsClosed(r))
    {
        //          printf("CMaaFtpServerConnection::Notify_Read(): IsClosed(%d) == true\n", r);
        CloseByException("Close");
    }
    m_InBuffer += CMaaString(Buffer, r);
    while(Process()) ;
    //     printf("CMaaFtpServerConnection::Notify_Read(): while(Process()) ; - done\n");
    int m = m_OutBuffer.Length() ? eWrite : 0;
    if   (m_InBuffer.Length() > 10 * 1024)
    {
        return m | eDisableRead;
    }
    return m | eRead;
}
int CMaaFtpServerConnection::Notify_Write()
{
    int w = SendData(m_OutBuffer, m_OutBuffer.Length());
    //     printf("CMaaFtpServerConnection::Notify_Write(): w = %d\n", w);
    m_OutBuffer = m_OutBuffer.Mid(w);
    if   (m_OutBuffer == "" && m_Step == -1)
    {
        CloseByException("Quit");
    }
    while(Process()) ;
    return m_OutBuffer.Length() ? eWrite : eDisableWrite;
}
void CMaaFtpServerConnection::OnTimer(int f)
{
    //     printf("CMaaFtpServerConnection::OnTimer(): f = %d\n", f);
    switch(f)
    {
    case 10:
    case 0:
        //printf("CMaaFtpServerConnection::OnTimer(%d)\n", f);
        if   (f == 10)
        {
            int us=1000000000;
            m_TimerTimeOut10.GetWaitForTime(&us, gpSockStartup ? gpSockStartup->GetTime() : 0);
            printf("us=%d\n", us);
        }
        CloseByException("OnTimer(0)");
        break;
    case 2:
    case 1:
        gLock.LockM();
        if   (m_pDataConn)
        {
            switch(m_TransferMode)
            {
            case 0:
                m_pDataConn->SetDataTxt(m_ListData);
                m_ListData.Empty();
                break;
            case 1:
                m_pDataConn->SetSendFile(m_File);
                m_File.Close();
                break;
            case 2:
                m_pDataConn->SetRecvFile(m_File);
                m_File.Close();
                break;
            }
            m_TransferMode = -1;
        }
        if   (f == 1)
        {
            m_Timer1.Stop();
        }
        gLock.UnLockM();
        break;
    case 3:
        m_Timer3.Stop();
        if   (m_DataError)
        {
            if   (m_DataError == 13)
            {
                //printf("Data transfer aborted\n");
                m_OutBuffer += CMaaString::sFormat("426 Data transfer aborted\r\n");
            }
            else
            {
                printf("Data transfer error %d\n", m_DataError);
                m_OutBuffer += CMaaString::sFormat("426 Transfer data error %d\r\n", m_DataError);
            }
        }
        else
        {
            double t = m_TransferTime > 0 ? (double)m_TransferTime / 1000.0 : 0.001;
            double v = (double)m_DataBytesTransferred / t;
            const char * units[] = {"B/s", "KB/s", "MB/s", "GB/s", NULL};
            int u;
            for  (u = 0; units[u + 1] && v >= 1000.0; u++)
            {
                v /= 1024.0;
            }
            printf("Data transfer complete successfully, speed = %.3lf %s\n", v, units[u]);
            m_OutBuffer += CMaaString::sFormat("226 Data transfer complete successfully, %,D bytes / %,D.%03d sec = %.3lf %s.\r\n", m_DataBytesTransferred, (_qword)(m_TransferTime / 1000), (int)(m_TransferTime % 1000), v, units[u]);
        }
        DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        ChangeFdMode(eAll);
        break;
    }
}

int
#ifdef _WIN32
__cdecl
#endif
CMaaFtpServerConnection::DirCompare(const void * p1, const void * p2)
{
    CMaaFindFile2::sFind * a = (CMaaFindFile2::sFind *)p1;
    CMaaFindFile2::sFind * b = (CMaaFindFile2::sFind *)p2;
    if   (a->m_Type != b->m_Type)
    {
        return a->m_Type - b->m_Type;
    }
    if   (a->m_FileName != b->m_FileName)
    {
        if   (a->m_FileName > b->m_FileName)
        {
            return 1;
        }
        return -1;
    }
    return 0;
}

bool CMaaFtpServerConnection::Process()
{
    if   (m_Step == -1)
    {
        return false;
    }
    int x = m_InBuffer.Find('\n');
    if   (x < 0)
    {
        if   (m_InBuffer.Length() > 5 * 1024)
        {
            CloseByException("Error");
        }
        return false;
    }
    if   (m_OutBuffer.Length() >= 5 * 1024)
    {
        return false;
    }
    CMaaString txt = m_InBuffer.Left(x);
    if   (txt.Right(1) == "\r")
    {
        txt = txt.Left(txt.Length() - 1);
    }
    m_InBuffer = m_InBuffer.Mid(x + 1);

    //     printf("CMaaFtpServerConnection::Process(): txt = %S\n", &txt);

    /*
     if   (m_InBuffer[0] == '\r')
     {
          m_InBuffer = m_InBuffer.Mid(1);
     }
     */
    //CMaaString m_UserName, m_Password, m_Path, m_FileName;
    m_CmdHistory[1] = m_CmdHistory[0];
    m_CmdHistory[0] = txt;
    _IP RemoteSrcIp = 0, LocalDstIp = 0;
    _Port RemoteSrcPort = 0, LocalDstPort = 0;
    {
        _IP Ip = 0;
        _Port Port = 0;
        GetConnInfo(&LocalDstIp, &LocalDstPort, &Ip, &Port);
        RemoteSrcIp = Ip;
        RemoteSrcPort = Port;
        CMaaString src = txt.Left(512);
        CMaaString safe1 = ToFtpSafe(src);
        CMaaString safe2 = ToFtpSafeUtf8(ToFileName(src));
        if   (safe2 != safe1)
        {
            //printf("%I:%d%c%S\n", Ip, Port, '\x0F', &safe2); // 0x0f - ñèìâîë øåñòåð¸íêè
            printf("%I:%d~%S\n", Ip, Port, &safe2);
        }
        else
        {
            printf("%I:%d %S\n", Ip, Port, &safe1);
        }
    }
    if   (txt.ToUpper() == "QUIT")
    {
        m_OutBuffer += "221 Goodbye!\r\n";
        m_Step = -1;
        return true;
        //CloseByException("Quit");
    }
    if   (txt.ToUpper() == "HELP")
    {
        DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        m_OutBuffer += "214-Supported commands:\r\n";
        m_OutBuffer += "    USER    PASS    SYST    NOOP    HELP    CWD     CDUP    PWD     XPWD\r\n";
        m_OutBuffer += "    TYPE    REST    PORT    PASV    LIST    NLST    RETR    STOR    SIZE\r\n";
        m_OutBuffer += "    DELE    MKD     XMKD    RMD     XRMD    RNFR    RNTO    MDTM    ABOR\r\n";
        m_OutBuffer += "    OPTS    FEAT    UTF8\r\n";
        m_OutBuffer += "214 Developer's e-mail: support@maasoftware.ru\r\n";
        return true;
    }
    if   (txt.ToUpper() == "SITE")
    {
        DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        m_OutBuffer += "214-The following SITE commands are recognized (* ==>'s unimplemented)\r\n";
        //m_OutBuffer += "    USER    PASS    SYST    NOOP    HELP    CWD     CDUP    PWD     XPWD\r\n";
        //m_OutBuffer += "    TYPE    REST    PORT    PASV    LIST    NLST    RETR    STOR    SIZE\r\n";
        //m_OutBuffer += "    DELE    MKD     XMKD    RMD     XRMD    RNFR    RNTO    MDTM    ABOR\r\n";
        m_OutBuffer += "214 no specific commands are supported\r\n";
        return true;
    }
    switch(m_Step)
    {
    case 1:
        if   (txt.Left(5).ToUpper() == "USER ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;

            m_UserName = txt.Mid(5);
            m_Password.Empty();
            {
                g_Mutex.LockM();
                _qword qtime = -2, qsize = -2;
                const char * pszFileName = g_ConfigFileName;
//                if   (s_LastUpdatedTime == -1)

                if   (s_LastUpdatedTime != time(NULL) &&
                    (
                     s_FileTime != (qtime = CMaaFile::GetFileTime64(pszFileName, false)) ||
                     s_FileSize != (qsize = CMaaFile::Length(pszFileName, false))
                    )
                   )
                {
//__utf8_printf("Reloading config...\n");
                    CMaaFile f(pszFileName, "R");
                    CMaaString txt = f.Read();

                    CMaaXmlDocument doc("FtpServer");
                    CMaaString errorMsg;
                    int errorLine, errorColumn;
                    if   (!doc.SetContent(txt, &errorMsg, &errorLine, &errorColumn, 0))
                    {
                        //CMaaString txt;
                        //txt.Format2("%s%d%d%s", TR("Error in config file \"%1\"\nLine: %2, Column: %3, Error: %4"), pszConfigFileName, errorLine, errorColumn, (const char *)errorMsg);
                        //throw txt;
__utf8_printf("Error reloading config - using old config\n");
                    }
                    else
                    {
                        m_Cfg = doc;
//__utf8_printf("Reloading config - ok\n");
                    }
                    s_FileTime = qtime;
                    s_FileSize = qsize;
                    s_LastUpdatedTime = time(NULL);
                }
                else
                {
//__utf8_printf("Does not need to reload config\n");
                }

                CMaaXmlElement e = m_Cfg.DocumentElement();
                g_Mutex.UnLockM();
                CMaaXmlNode n0 = e.FindNode("Users");
                m_UserNode = n0.FindNodeWithAttr("User", "Name", m_UserName);
            }
            bool bEmptyPass = false;
            CMaaString setuser, setgroup;
            if   (!m_UserNode.IsNull())
            {
                setuser = m_UserNode["setuser"];
                setgroup = m_UserNode["setgroup"];
                if   (m_UserNode.FindAttribute("PassType") == "Plain" && m_UserNode.FindAttribute("Pass") == "")
                {
                    bEmptyPass = true;
                }
                if   (m_UserNode.FindAttribute("PassType") == "hash")
                {
                    CMaaString Pass = "";
                    CMaaString Salt = m_UserNode.FindAttribute("PassSalt");
                    LongInt2 liSalt(8);
                    if   (m_UserNode.FindAttribute("PassHashType") == "GBM1" && Import(liSalt, Salt) == 8)
                    {
                        CMaaString Hash(NULL, 32);
                        ::gGostBsMaa.Hash(NULL, Pass, Pass.Length(), liSalt(), (char *)(const char *)Hash, 32);
                        bEmptyPass = (Export(Hash) == m_UserNode.FindAttribute("PassHash"));
                    }
                }
            }
            if   (!bEmptyPass)
            {
                m_OutBuffer += "331 User name ok, need password\r\n";
                m_Step++;
            }
            else
            {
                m_OutBuffer += "230 User is logged in\r\n";
                m_Step += 2;
            }
        }
        else
        {
            SayError();
        }
        break;
    case 2:
        if   (txt.Left(5).ToUpper() == "PASS ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;

            m_Password = txt.Mid(5);

            bool bPassOk = false;

            CMaaString setuser, setgroup;
            if   (!m_UserNode.IsNull())
            {
                setuser = m_UserNode["setuser"];
                setgroup = m_UserNode["setgroup"];
                CMaaString Pass = m_Password;
                if   (m_UserNode.FindAttribute("PassType") == "Plain")
                {
                    bPassOk = m_UserNode.FindAttribute("Pass") == Pass;
                }
                else if (m_UserNode.FindAttribute("PassType") == "email")
                {
                    bPassOk = Pass.Find('@') > 0;
                }
                else
                {
                    //if   (m_UserNode.FindAttribute("PassType") == "hash")
                    CMaaString Salt = m_UserNode.FindAttribute("PassSalt");
                    LongInt2 liSalt(8);
                    if   (m_UserNode.FindAttribute("PassHashType") == "GBM1" && Import(liSalt, Salt) == 8)
                    {
                        CMaaString Hash(NULL, 32);
                        ::gGostBsMaa.Hash(NULL, Pass, Pass.Length(), liSalt(), (char *)(const char *)Hash, 32);
                        bPassOk = (Export(Hash) == m_UserNode.FindAttribute("PassHash"));
                    }
                }
            }

            //if   (m_Password != "" && m_Password != "nopassword")
            //if   (m_UserName == "maa" && m_Password == "qwerty")
            if   (bPassOk && (setuser.Length() || setgroup.Length()))
            {
                int uid = -1;
                int gid = -1;
                if   (setuser.Length())
                {
                    CMaaFile f("/etc/passwd", "R|SrSw", false);
                    while(f.IsOpen() && f.GetCurPos() < f.Length())
                    {
                        CMaaString Line = f.fgets();
                        CMaaString user = Line.GetWord(true, true, ":");
                        CMaaString pass = Line.GetWord(true, true, ":");
                        CMaaString _uid = Line.GetWord(true, true, ":");
                        CMaaString _gid = Line.GetWord(true, true, ":");
                        if   (user == setuser)
                        {
                            sscanf(_uid, "%d", &uid);
                            sscanf(_gid, "%d", &gid);
                            break;
                        }
                    }
                }
                if   (setgroup.Length())
                {
                    CMaaFile f("/etc/group", "R|SrSw", false);
                    while(f.IsOpen() && f.GetCurPos() < f.Length())
                    {
                        CMaaString Line = f.fgets();
                        CMaaString group = Line.GetWord(true, true, ":");
                        CMaaString pass = Line.GetWord(true, true, ":");
                        CMaaString _gid = Line.GetWord(true, true, ":");
                        if   (group == setgroup)
                        {
                            sscanf(_gid, "%d", &gid);
                            break;
                        }
                    }
                }
#ifdef __unix__
                if   (gid >= 0)
                {
					printf("setgid(%d)\n", gid);
                    if   (setgid(gid) == 0)
                    {
                        // ok
                        printf("setgid(%d) - ok\n", gid);
                    }
                    else
					{
                        bPassOk = false;
                        m_OutBuffer += "530 Bad access to setgid()\r\n";
                        if   (++m_TryN <= 3 && uid < 0)
                        {
                            //m_OutBuffer += "220 Enter user name.\r\n";
                            m_Step = 1;
                        }
                        else
                        {
                            m_Step = -1;
                        }
                        break;
                    }
                }
                if   (uid >= 0)
                {
                    printf("setuid(%d)\n", uid);
                    if   (setuid(uid) == 0)
                    {
                        // ok
                        printf("setuid(%d) - ok\n", uid);
                    }
                    else
					{
                        bPassOk = false;
                        m_OutBuffer += "530 Bad access to setuid()\r\n";
                        if   (++m_TryN <= 3)
                        {
                            //m_OutBuffer += "220 Enter user name.\r\n";
                            m_Step = 1;
                        }
                        else
                        {
                            m_Step = -1;
                        }
                        break;
                    }
                }
#endif
            }
            printf("send 230 reply...\n");
            if   (bPassOk)
            {
                m_OutBuffer += "230 User is logged in\r\n";
                m_Step++;
            }
            else
            {
                //Jan 06 17:27:22 vm03 proftpd[3018] vm03 (46.159.103.21[46.159.103.21]): USER anonymous: no such user found from 46.159.103.21 [46.159.103.21] to ::ffff:141.101.245.192:3321
                if (FailLog.Length())
                {
                    try
                    {
                        CMaaString Line;
                        time_t t = time(NULL);
                        CMaaString dt = GetTextDateTime(t, 2);
                        char buff[128];
                        memset(buff, 0, sizeof(buff));
                        tm ttm;
#ifdef __unix__
						localtime_r(&t, &ttm);
#else
						memcpy(&ttm, localtime(&t), sizeof(ttm));
#endif
                        size_t xsz = strftime(buff, sizeof(buff) - 1, "%b %d %H:%M:%S", &ttm);
                        CMaaString _login = m_UserNode.IsNull() ? "unknown" : m_UserName;
                        Line.Format2("%s%I%d%I%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: no such user found from %2 [%2] to ::ffff:%4:%5\n", buff, RemoteSrcIp, RemoteSrcPort, LocalDstIp, LocalDstPort, &_login);
                        CMaaFile f(FailLog, "ACD|SrSw");
                        f.Write(Line);
                        //f.fprintf("
                    }
                    catch(...)
                    {
                    }
                }
                m_OutBuffer += "530 Bad password\r\n";
                if   (++m_TryN <= 3)
                {
                    //m_OutBuffer += "220 Enter user name.\r\n";
                    m_Step = 1;
                }
                else
                {
                    m_Step = -1;
                }
            }
        }
        else
        {
            SayError();
        }
        break;
    case 3:
        if   (txt.ToUpper() == "SYST")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "215 UNIX Type: L8\r\n";
        }
        else if (txt.ToUpper() == "OPTS UTF8 ON")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 OPTS command ok\r\n";
        }
        else if (txt.ToUpper() == "UTF8" || txt.Left(5).ToUpper() == "UTF8 ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 OPTS command ok\r\n";
        }
        else if (txt.ToUpper() == "FEAT")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "211-Extensions supported:\r\n";
            //m_OutBuffer += " MLST\r\n";
            m_OutBuffer += " SIZE\r\n";
            m_OutBuffer += " MDTM\r\n";
            m_OutBuffer += " UTF8\r\n";
            m_OutBuffer += "211 END\r\n";
        }
        else if (txt.ToUpper() == "NOOP")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 NOOP command ok\r\n";
        }
        else if (txt.Left(4).ToUpper() == "CWD ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Dir = ToFileName(txt.Mid(4));
            /*
               if   (Dir != "/" && Dir.Right(1) == "/")
               {
                    Dir = Dir.Left(Dir.Length() - 1);
                    printf("%s\n", (const char *)Dir);
               }
               */
            CMaaString RealDir, CanonicalDir;
            if   (GetRealAndCanonicalFsName(m_Path, Dir, &RealDir, &CanonicalDir, false))
            {
                m_Path = CanonicalDir;
                m_OutBuffer += CMaaString::sFormat("250 %s is the current directory\r\n", (const char *)m_Path);
            }
            else
            {
                m_OutBuffer += "550 No such directory\r\n";
            }
        }
        else if (txt.ToUpper() == "CDUP")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Dir = "..";
            CMaaString RealDir, CanonicalDir;
            if   (GetRealAndCanonicalFsName(m_Path, Dir, &RealDir, &CanonicalDir, false))
            {
                m_Path = CanonicalDir;
                m_OutBuffer += CMaaString::sFormat("250 %s is the current directory\r\n", (const char *)m_Path);
            }
            else
            {
                m_OutBuffer += "550 No such directory\r\n";
            }
        }
        else if (txt.ToUpper() == "PWD" || txt.ToUpper() == "XPWD")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += CMaaString::sFormat("257 \"%s\" is current directory\r\n", (const char *)m_Path);
        }
        else if (txt.Left(5).ToUpper() == "TYPE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Type = txt.Mid(5);
            if   (Type == "A" || Type == "I")
            {
                m_Type = Type[0];
                m_OutBuffer += "200 Type set ok\r\n";
            }
            else
            {
                SayError();
            }
        }
        else if (txt.Left(5).ToUpper() == "REST ")
        {
            CMaaString Rest = txt.Mid(5);

            _qword x = -1;
            if   (mysscanf64(Rest, &x) && x >= 0)
            {
                DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
                m_Rest = x;
                m_OutBuffer += CMaaString::sFormat("350 Restarting at position %D\r\n", m_Rest);
            }
            else
            {
                SayError();
            }
        }
        else if (txt.Left(5).ToUpper() == "PORT ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort));
            m_TransferMode = -1;

            CMaaString Port = txt.Mid(5);
            int iPort[6] = {-1, -1, -1, -1, -1, -1};
            sscanf(Port, "%d,%d,%d,%d,%d,%d", &iPort[0], &iPort[1], &iPort[2], &iPort[3], &iPort[4], &iPort[5]);
            int i;
            for  (i = 0; i < 6; i++)
            {
                if   (iPort[i] < 0 || iPort[i] > 255)
                {
                    break;
                }
            }
            if   (i != 6)
            {
                m_OutBuffer += "426 Illegal IP or port range rejected\r\n";
            }
            else
            {
                m_PortIp = CMaaIpAddress(iPort[0], iPort[1], iPort[2], iPort[3]);
                m_PortPort = iPort[4] * 256 + iPort[5];
                if   (m_UserNode.FindAttribute("FXP") != "true")
                {
                    _IP Ip = 0;
                    GetConnInfo(NULL, NULL, &Ip, NULL);
                    if   (Ip != m_PortIp)
                    {
                        //m_OutBuffer += "426 Illegal IP or port range rejected\r\n";
                        m_OutBuffer += CMaaString::sFormat("426 Illegal IP or port range rejected cip=%I, portip=%I\r\n", Ip, m_PortIp);
                        return true;
                    }
                }
                m_State = (m_State & ~(ePasv)) | ePort;
                m_OutBuffer += "200 PORT command ok\r\n";
            }
        }
        else if (txt.ToUpper() == "PASV")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort));
            m_TransferMode = -1;
            try
            {
                if   (m_pPasvServer)
                {
                    m_pPasvServer->m_Timer0.Start(0);
                    m_pPasvServer = NULL;
                }
                m_pPasvServer = new CFtpDataServer(m_pFdSockets, 0, this);
                if   (!m_pPasvServer)
                {
                    throw 1;
                }
                m_PortPort = m_pPasvServer->GetBindedPort();
                GetConnInfo(&m_PortIp, NULL, NULL, NULL);
                m_State = (m_State & ~(ePort)) | ePasv;
            }
            catch(XTOOSockErr err)
            {
                printf("Error: catch(XTOOSockErr): %s\n", err.GetMsg());
                m_OutBuffer += CMaaString::sFormat("400 Error: %s\r\n", err.GetMsg());
            }
            catch(...)
            {
                printf("Error: catch(...)\n");
                m_OutBuffer += CMaaString::sFormat("400 Unknown error\r\n");
            }
            if   (m_State & ePasv)
            {
                m_OutBuffer += CMaaString::sFormat("227 Entering passive mode(%d,%d,%d,%d,%d,%d)\r\n",
                     (m_PortIp >> 24) & 0xff,  (m_PortIp >> 16) & 0xff, (m_PortIp >> 8) & 0xff, m_PortIp & 0xff,
                     (m_PortPort >> 8) & 0xff, m_PortPort & 0xff);
            }
        }
        else if (txt.Left(5).ToUpper() == "LIST " || txt.ToUpper() == "LIST" || txt.Left(5).ToUpper() == "NLST " || txt.ToUpper() == "NLST")
        {
            //printf("%s\n", (const char *)txt);

            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            bool bNLST = txt.Left(4).ToUpper() == "NLST";

            CMaaString Mask = txt.Mid(5);
            if   (Mask.Left(4) == "-la " || Mask == "-la" || Mask.Left(4) == "-al " || Mask == "-al")
            {
                Mask = Mask.Mid(4);
            }
            else if (Mask.Left(3) == "-a " || Mask == "-a")
            {
                Mask = Mask.Mid(3);
            }
            Mask = ToFileName(Mask);
            if   (Mask == "" || Mask == ".")
            {
                Mask = "*.*";
            }
            if   (Mask == "/")
            {
                Mask += "*.*";
            }
            //printf("Mask: %S\n", &Mask);
            CMaaString RealFs, CanonicalFs, Perm;
            CMaaString Data;
            CMaaString tmp = Mask.Right(3);
            if   (tmp == ".." || tmp == "/.." || tmp == "\\..")
            {
                Mask += "/*.*";
            }
            //printf("...%S\n", &Mask);
            //printf("LIST: Mask = %s\n", (const char *)Mask);
            //printf("path=%S\n", &m_Path);

            if   (GetRealAndCanonicalFsName(m_Path, Mask, &RealFs, &CanonicalFs, false, &Perm) ||
                 GetRealAndCanonicalFsName(m_Path, Mask, &RealFs, &CanonicalFs, true, &Perm))
            {
                //printf("...%S\n", &Mask);
                //printf("...RealFs=%S\n", &RealFs);
                if   (CMaaFile::IsADir(RealFs))
                {
                    //printf("...%S - is a dir\n", &RealFs);
                    /*
if (RealFs == "/")
{
    
}
*/
                    if   (Mask.Right(1) == "/")
                    {
                        Mask += "*.*";
                    }
                    else
                    {
                        Mask += "/*.*";
                    }
                    Perm.Empty();
                    GetRealAndCanonicalFsName(m_Path, Mask, &RealFs, &CanonicalFs, true, &Perm);
                }
                Mask = RealFs;
                //printf("Mask=%S, Perm=%S\n", &Mask, &Perm);
                //printf("LIST(2): Mask = %s\n", (const char *)Mask);
                Mask = CMaaFile::MkCompatible(Mask);
                printf("Mask=%S, Perm=%S\n", &Mask, &Perm);
                __time64_t CurrentTime = _time64(NULL);
                tm tt_cur;
                memset(&tt_cur, 0, sizeof(tt_cur));
                {
                    tm * t = localtime(&CurrentTime);
                    if   (t)
                    {
                        memcpy(&tt_cur, t, sizeof(tt_cur));
                    }
                }
                //printf("LIST(3): Mask = %s\n", (const char *)Mask);
                if   (Perm.Find(" DL+") >= 0)
                {
                    try
                    {
                        CMaaPtrAE<CMaaFindFile2::sFind> m(100, 1);
                        int N = 0;
                        //printf("...%S\n", &Mask);
                        CMaaFindFile2 ff(Mask, 1);
                        CMaaFindFile2::sFind f;
                        while(ff.Get(f))
                        {
                            //printf("fn: %s\n", (const char *)f.m_FileName);
                            /*
                                   static const char * Month[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};// or international)
                                   CMaaString tmp;
                                   tm tt;
                                   memset(&tt, 0, sizeof(tt));
                                   tm * t = localtime(&f.m_mTime);
                                   if   (t)
                                   {
                                        memcpy(&tt, t, sizeof(tt));
                                   }
                                   else
                                   {
                                        memcpy(&tt, &tt_cur, sizeof(tt));
                                   }
                                   */
                            if   (bNLST)
                            {
                                if   (f.m_Type == CMaaFindFile2::sFind::eFile || f.m_Type == CMaaFindFile2::sFind::eDir)
                                {
                                    //tmp = f.GetFileName() + "\r\n";
                                    //Data += tmp;
                                    m[N++] = f;
                                }
                            }
                            else
                            {
                                if   (//f.m_Type == CMaaFindFile2::sFind::eSl ||
                                     f.m_Type == CMaaFindFile2::sFind::eUnknown)
                                {
                                    continue;
                                }
                                if   (f.m_Type == CMaaFindFile2::sFind::eDot || f.m_Type == CMaaFindFile2::sFind::eDotDot)
                                {
                                    int n = CanonicalFs.ReverseFind('/');
                                    CMaaString d = CanonicalFs.Left(n > 0 ? n : 1);
                                    if   (!GetRealAndCanonicalFsName(d, "..", NULL, NULL, false, NULL))
                                    {
                                        continue;
                                    }
                                }
                                m[N++] = f;
                                /*
                                        tmp.Format("%crw-rw-rw-    1 user group %6D %s %2d %s %S\r\n",
                                             (f.m_Type == CMaaFindFile2::sFind::eDir || 
                                              f.m_Type == CMaaFindFile2::sFind::eDot ||
                                              f.m_Type == CMaaFindFile2::sFind::eDotDot) ? 'd' : '-',
                                             (_qword)f.m_Size, 
                                             Month[tt.tm_mon], tt.tm_mday, 
                                             tt.tm_year == tt_cur.tm_year ? 
                                             (const char *)CMaaString::sFormat("%02d:%02d", tt.tm_hour, tt.tm_min) :
                                             (const char *)CMaaString::sFormat("%d", tt.tm_year + 1900),
                                             &f.GetFileName());
                                        Data += tmp;
                                        */
                            }
                        }
                        if   (N == 1 && CMaaFile::IsAFile(Mask))
                        {
                            CMaaString txt = CanonicalFs;
                            int n = txt.ReverseFind('/');
                            if   (n >= 0)
                            {
                                txt = txt.Mid(n + 1);
                            }
                            m[0].m_FileName = txt;
                        }
                        if   (CanonicalFs.Right(4) == "/*.*")
                        {
                            CMaaString d0 = CanonicalFs.Left(CanonicalFs.Length() - 3);
                            CMaaString d = d0;
#ifdef _WIN32
                            d = d.ToUpper();
#endif
                            for  (CMaaXmlNode n = m_UserNode.FindNode("dir"); !n.IsNull(); n = n.FindNext())
                            {
                                CMaaString Path = n.FindAttribute("vfs_path");
                                CMaaString x = Path;
#ifdef _WIN32
                                x = x.ToUpper();
#endif
                                if   (x.Length() > d.Length() && x.Left(d.Length()) == d)
                                {
                                    int n = Path.Find(d.Length() + 1, '/');
                                    if   (n < 0)
                                    {
                                        n = Path.Length();
                                    }
                                    x = Path.Mid(d.Length(), n - d.Length());

                                    CMaaString tmp = d0;
                                    if   (tmp.Length() > 1)
                                    {
                                        tmp = tmp.Left(tmp.Length() - 1);
                                    }
                                    CMaaString d2;
                                    if   (GetRealAndCanonicalFsName(tmp, x, &d2, NULL, false, NULL) || GetRealAndCanonicalFsName(tmp, x, &d2, NULL, true, NULL))
                                    {
                                        CMaaFindFile2 ff(d2, 1);
                                        CMaaFindFile2::sFind f;
                                        if   (ff.Get(f))
                                        {
                                            int nnn = 0;
                                            for  (; nnn < N; nnn++)
                                            {
                                                if   (m[nnn].m_FileName == x)
                                                {
                                                    break;
                                                }
                                            }
                                            if   (nnn >= N)
                                            {
                                                f.m_FileName = x;
                                                m[N++] = f;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        //printf("test1\n");
                        CMaaPtr<CMaaFindFile2::sFind *> mm(N + 1, 1);
                        int i;
                        for  (i = 0; i < N; i++)
                        {
                            mm[i] = &m[i];
                        }
                        qsort(&m[0], N, sizeof(m[0]), DirCompare);

                        for  (i = 0; i < N; i++)
                        {
                            CMaaFindFile2::sFind &f = *mm[i];

                            static const char * Month[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};// or international)
                            CMaaString tmp;
                            tm tt;
                            memset(&tt, 0, sizeof(tt));
                            tm * t = localtime(&f.m_mTime);
                            if   (t)
                            {
                                memcpy(&tt, t, sizeof(tt));
                            }
                            else
                            {
                                memcpy(&tt, &tt_cur, sizeof(tt));
                            }

                            CMaaString AnsiFileName = UnicodeToAnsi(Utf8ToUnicode(f.GetFileName()));
                            CMaaString Utf8FileName = f.GetFileName();
                            if   (bNLST)
                            {
                                //                                        tmp = AnsiFileName + "\r\n";
                                tmp = Utf8FileName + "\r\n";
                                Data += tmp;
                            }
                            else
                            {
                                struct stat buf;
                                memset(&buf, 0, sizeof(buf));
                                int x = stat(f.m_FileName, &buf);
                                CMaaString Mode;
                                if   (x)
                                {
                                    Mode = "rw-rw-rw-";
                                    //mode_t    st_mode;    /* protection */
                                    //nlink_t   st_nlink;   /* number of hard links */
                                    //uid_t     st_uid;     /* user ID of owner */
                                    //gid_t     st_gid;
                                }
                                else
                                {
                                    for  (int j = 2; j >= 0; j--)
                                    {
                                        if   (buf.st_mode & (4 << (3 * j)))
                                        {
                                            Mode += "r";
                                        }
                                        else
                                        {
                                            Mode += "-";
                                        }
                                        if   (buf.st_mode & (2 << (3 * j)))
                                        {
                                            Mode += "w";
                                        }
                                        else
                                        {
                                            Mode += "-";
                                        }
                                        if   (buf.st_mode & (1 << (3 * j)))
                                        {
                                            Mode += "x";
                                        }
                                        else
                                        {
                                            Mode += "-";
                                        }
                                    }
                                }

                                tmp.Format("%c%S 1 user group %6D %s %2d %s %S\r\n",
                                     (f.m_Type == CMaaFindFile2::sFind::eDir ||
                                     f.m_Type == CMaaFindFile2::sFind::eDot ||
                                     f.m_Type == CMaaFindFile2::sFind::eDotDot) ? 'd' :
                                     f.m_Type == CMaaFindFile2::sFind::eSl ? 'l' : '-',
                                     &Mode,
                                     (_qword)f.m_Size,
                                     Month[tt.tm_mon], tt.tm_mday,
                                     tt.tm_year == tt_cur.tm_year ?
                                     (const char *)CMaaString::sFormat("%02d:%02d", tt.tm_hour, tt.tm_min) :
                                     (const char *)CMaaString::sFormat("%d", tt.tm_year + 1900),
                                     &AnsiFileName);

                                //printf("%d %S", x, &tmp);
                                /*
                                        tmp.Format("%crw-rw-rw- 1 user group %6D %s %2d %s %S\r\n",
                                             (f.m_Type == CMaaFindFile2::sFind::eDir ||
                                             f.m_Type == CMaaFindFile2::sFind::eDot ||
                                             f.m_Type == CMaaFindFile2::sFind::eDotDot) ? 'd' : 
                                             f.m_Type == CMaaFindFile2::sFind::eSl ? 'l' : '-',
                                             (_qword)f.m_Size,
                                             Month[tt.tm_mon], tt.tm_mday,
                                             tt.tm_year == tt_cur.tm_year ?
                                             (const char *)CMaaString::sFormat("%02d:%02d", tt.tm_hour, tt.tm_min) :
                                             (const char *)CMaaString::sFormat("%d", tt.tm_year + 1900),
                                             &AnsiFileName);
*/
                                tmp.Format("%crw-rw-rw- 1 user group %6D %s %2d %s %S\r\n",
                                     (f.m_Type == CMaaFindFile2::sFind::eDir ||
                                     f.m_Type == CMaaFindFile2::sFind::eDot ||
                                     f.m_Type == CMaaFindFile2::sFind::eDotDot) ? 'd' :
                                     f.m_Type == CMaaFindFile2::sFind::eSl ? 'l' : '-',
                                     (_qword)f.m_Size,
                                     Month[tt.tm_mon], tt.tm_mday,
                                     tt.tm_year == tt_cur.tm_year ?
                                     (const char *)CMaaString::sFormat("%02d:%02d", tt.tm_hour, tt.tm_min) :
                                     (const char *)CMaaString::sFormat("%d", tt.tm_year + 1900),
                                     &Utf8FileName);
                                Data += tmp;
                            }
                        }

                    }
                    catch(...)
                    {
                    }
                }
            }

            //printf("list data:\n%s\n", (const char *)Data);

            m_OutBuffer += "150 Opening ASCII mode data connection for /bin/ls.\r\n";
            //m_OutBuffer += CMaaString::sFormat("226 Waiting for data connection\r\n");//,
            //(m_PortIp >> 24) & 0xff,  (m_PortIp >> 16) & 0xff, (m_PortIp >> 8) & 0xff, m_PortIp & 0xff,
            //(m_PortPort >> 8) & 0xff, m_PortPort & 0xff);

            //m_State = (m_State & ~(ePort)) | ePasv;

            m_ListData = Data;
            m_TransferMode = 0;

            if   (m_State & ePort)
            {
                try
                {
                    m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp, m_PortPort, m_ServerIpPort);
                    return true;
                }
                catch(XTOOSockErr err)
                {
                    printf("catch(XTOOSockErr): %s\n", err.GetMsg());
                }
                catch(...)
                {
                    printf("catch(...)\n");
                }
            }
            else if (m_State & ePasv)
            {
                if   (m_pDataConn || m_pPasvServer)
                {
                    OnTimer(2);
                    return true;
                }

                /*
                    gLock.LockM();
                    if   (m_pDataConn)
                    {
                         m_pDataConn->SetDataTxt(m_ListData);
                         gLock.UnLock();
                         return true;
                    }
                    if   (m_pPasvServer)
                    {
                         gLock.UnLock();
                         return true;
                    }
                    gLock.UnLock();
                    */
            }
            // Error
            m_OutBuffer += "426 Error transferring data.\r\n";
        }
        else if (txt.Left(5).ToUpper() == "RETR " || txt.Left(5).ToUpper() == "STOR ")
        {
            //printf("%s\n", (const char *)txt);

            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            bool bRecv = txt.Left(5).ToUpper() == "RETR ";
            CMaaString FileName = ToFileName(txt.Mid(5));
            CMaaString Perm, tmp = FileName;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, NULL, true, &Perm))
            {
                m_OutBuffer += "426 dir not found.\r\n";
                return true;
            }
            bool bSkipExisted = false;
            const char * pMode = NULL;
            if   (bRecv)
            {
                if   (Perm.Find(" R+") < 0)
                {
                    CMaaString tmp2 = ToFtpSafe(tmp);
                    m_OutBuffer += CMaaString::sFormat("426 you have no permissions to read the file %S.\r\n", &tmp2);
                    return true;
                }
                pMode = "R|Sr";
            }
            else
            {
                if   (Perm.Find(" W+") < 0)
                {
                    CMaaString tmp2 = ToFtpSafe(tmp);
                    m_OutBuffer += CMaaString::sFormat("426 you have no permissions to write the file %S.\r\n", &tmp2);
                    return true;
                }
                if   (m_Rest == 0)
                {
                    pMode = Perm.Find(" C+") >= 0 ? "WC|SwSr" : "W|SwSr";
                    if   (Perm.Find(" D+") < 0)
                    {
                        pMode = Perm.Find(" C+") >= 0 ? "WCN|SwSr" : "WN|SwSr";
                    }
                    bSkipExisted = (m_Rest == 0 && Perm.Find(" D+") < 0);
                }
                else
                {
                    pMode = Perm.Find(" C+") >= 0 ? "AC|SwSr" : "A|SwSr";
                }
            }
            //FileName = CMaaString("C:\\Ftp\\") + m_Path + "/" + FileName;
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   ((m_State & (ePort | ePasv)) == 0)
            {
                m_OutBuffer += "426 use PORT or PASV before transfer request.\r\n";
            }
            else
            {
                try
                {
                    /*
                	 if (bSkipExisted)
                	 {
                	    if (CMaaFile::IsAFile(FileName))
                	    {
                		throw CMaaString::sFormat("%S is exists", &FileName);
                	    }
                	 }
                         */
                    printf("opening %S, %s\n", &FileName, pMode);
                    m_File = CMaaFile(FileName, pMode);
                    if   (!m_File.Seek(m_Rest))
                    {
                        printf("seek failed\n");
                        throw 1;
                    }
                    m_TransferMode = bRecv ? 1 : 2;
                }
                catch(CMaaString txt)
                {
                    m_OutBuffer += CMaaString::sFormat("426 Error: %S\r\n", &txt);
                    return true;
                }
                catch(XTOOFile2Error err)
                {
                    m_OutBuffer += CMaaString::sFormat("426 Error: %s\r\n", err.GetMsg());
                    return true;
                }
                catch(...)
                {
                    m_OutBuffer += CMaaString::sFormat("426 unknown error\r\n");
                    return true;
                }
                m_OutBuffer += "150 Opening BINARY mode data connection for transferring file.\r\n";

                m_Rest = 0;

                if   (m_State & ePort)
                {
                    try
                    {
                        m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp, m_PortPort, m_ServerIpPort);
                        return true;
                    }
                    catch(XTOOSockErr err)
                    {
                        printf("catch(XTOOSockErr): %s\n", err.GetMsg());
                    }
                    catch(...)
                    {
                        printf("catch(...)\n");
                    }
                }
                else if (m_State & ePasv)
                {
                    if   (m_pDataConn || m_pPasvServer)
                    {
                        OnTimer(2);
                        return true;
                    }

                    /*
                         gLock.LockM();
                         if   (m_pDataConn)
                         {
                              m_pDataConn->SetDataTxt(m_ListData);
                              gLock.UnLock();
                              return true;
                         }
                         if   (m_pPasvServer)
                         {
                              gLock.UnLock();
                              return true;
                         }
                         gLock.UnLock();
                         */
                }
                // Error
                m_OutBuffer += "426 Error transferring data.\r\n";
            }
        }
        else if (txt.ToUpper() == "ABOR" || (txt.ToUpper(0) == "\xFF\xF4""ABOR"))
        {
            bool bByTimer = false;
            gLock.LockM();
            if   (m_pDataConn)
            {
                bByTimer = true;
                m_pDataConn->m_TimerAbor13.Start(1);
            }
            if   (m_pPasvServer)
            {
                m_pPasvServer->m_Timer0.Start(1);
            }
            gLock.UnLock();
            if   (!bByTimer)
            {
                m_OutBuffer += CMaaString::sFormat("226 ABOR command complete.\r\n");
            }
            m_State &= ~(ePort | ePasv);
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        }
        else if (txt.Left(5).ToUpper() == "SIZE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: can't change dir\r\n", &tmp2);
                return true;
            }
            //FileName = CMaaString("C:\\Ftp\\") + m_Path + FileName;
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DL+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to list directory for file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                _qword x = CMaaFile::Length(FileName);
                m_OutBuffer += CMaaString::sFormat("213 %D\r\n", x);
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: unknown error\r\n", &tmp2);
            }
            return true;
        }
        else if (txt.Left(5).ToUpper() == "MDTM ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(5));
            CMaaString cName = FileName;
            int n = FileName.Find(' ');
            if   (n >= 14)
            {
                int x = 0;
                for  (int i = 0; i < n; i++)
                {
                    if   ((FileName[i] >= '0' && FileName[i] <= '9') || (FileName[i] == '.' && i == 14))
                    {
                        x++;
                    }
                }
                if   (x == n)
                {
                    // Set file date time
                    tm tt;
                    memset(&tt, 0, sizeof(tt));
                    int x = 0;
                    CMaaString dt = FileName.Left(n);
                    FileName = FileName.Mid(n + 1);
                    sscanf(dt.Left(4), "%d", &x);
                    tt.tm_year = x - 1900;
                    x = 1;
                    sscanf(dt.Mid(4, 2), "%d", &x);
                    tt.tm_mon = x - 1;
                    x = 1;
                    sscanf(dt.Mid(6, 2), "%d", &x);
                    tt.tm_mday = x;
                    x = 0;
                    sscanf(dt.Mid(8, 2), "%d", &x);
                    tt.tm_hour = x;
                    x = 0;
                    sscanf(dt.Mid(10, 2), "%d", &x);
                    tt.tm_min = x;
                    x = 0;
                    sscanf(dt.Mid(12, 2), "%d", &x);
                    tt.tm_sec = x;
#ifdef _WIN32
                    time_t t = _mkgmtime(&tt);
#endif
#ifdef __unix__
                    time_t t = timegm(&tt);
#endif
                    if   (t == (time_t)-1)
                    {
                        CMaaString tmp2 = ToFtpSafe(dt);
                        m_OutBuffer += CMaaString::sFormat("550 error converting datetime \"%S\"\r\n", &tmp2);
                        return true;
                    }
                    CMaaString Perm;
                    if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 no file named \"%S\"\r\n", &tmp2);
                        return true;
                    }
                    FileName = CMaaFile::MkCompatible(FileName);
                    if   (Perm.Find(" W+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 you have no permissions to write file %S.\r\n", &tmp2);
                        return true;
                    }
                    try
                    {
                        CMaaFile::SetDateTimeEx(FileName, t, 0, true);
                        t = CMaaFile::GetDateTime(FileName, NULL, true);

                        tm tt;
                        memset(&tt, 0, sizeof(tt));
                        tm * p = gmtime(&t);
                        if   (!p)
                        {
                            throw 1;
                        }
                        memcpy(&tt, p, sizeof(tt));
                        m_OutBuffer += CMaaString::sFormat("213 %04d%02d%02d%02d%02d%02d.%03d\r\n",
                             tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, 0);
                    }
                    catch(XTOOError err)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, err.GetMsg());
                    }
                    catch(...)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 unknown error for setting datetime \"%S\"\r\n", &tmp2);
                    }
                    return true;
                }
            }
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 no file named \"%S\"\r\n", &tmp2);
                return true;
            }
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DL+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to list directory of the file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                time_t t = CMaaFile::GetDateTime(FileName, NULL, true);
                tm tt;
                memset(&tt, 0, sizeof(tt));
                tm * p = gmtime(&t);
                if   (!p)
                {
                    throw 1;
                }
                memcpy(&tt, p, sizeof(tt));
                m_OutBuffer += CMaaString::sFormat("213 %04d%02d%02d%02d%02d%02d.%03d\r\n",
                     tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, 0);
            }
            catch(XTOOError err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 unknown error for gettting datetime \"%S\"\r\n", &tmp2);
            }
            return true;
        }
        else if (txt.Left(11).ToUpper() == "SITE CHMOD ") // SITE CHMOD 664 email.txt--
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = txt.Mid(11).RemoveSpaces();
            ToFileName(txt.Mid(11));
            CMaaString Mode = FileName.GetWord();
            FileName = ToFileName(FileName.RemoveSpaces());
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 no file named \"%S\"\r\n", &tmp2);
                return true;
            }
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" CHMOD+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to write file %S.\r\n", &tmp2);
                return true;
            }
            int mode = 0;
            sscanf(Mode, "%o", &mode);
            printf("chmod %S oct: %o\n", &FileName, mode);
            try
            {
#ifdef __unix__
				int x = chmod(FileName, (mode_t)mode);
#else
				int x = -1;
#endif
				if   (x)
                {
#ifdef __unix__
                    XTOOLastError err("chmod() error", errno);
#else
                    XTOOLastError err("chmod() error", 0);
#endif
                    printf("error: %s\n", err.GetMsg());
                    throw err;
                }
                m_OutBuffer += CMaaString::sFormat("200 CHMOD command successful.\r\n");
                printf("ok\n");
            }
            catch(XTOOError err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 unknown error for setting datetime \"%S\"\r\n", &tmp2);
            }
            return true;
        }
        else if (txt.Left(5).ToUpper() == "DELE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "426 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" D+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to delete file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                CMaaFile::RemoveEx(FileName);
                m_OutBuffer += CMaaString::sFormat("250 DELE command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: unknown error\r\n", &tmp2);
            }
            return true;
        }
        else if (txt.Left(4).ToUpper() == "MKD " || txt.Left(5).ToUpper() == "XMKD ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(txt.Left(1).ToUpper() == "X" ? 5 : 4));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DC+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to create directory %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                //printf("%s\n", (const char *)UnicodeToAnsi(Utf8ToUnicode(FileName)));
                printf("%S\n", &FileName);
                CMaaFile::MkDir(FileName, false, true);
                printf("ok\n");
                m_OutBuffer += CMaaString::sFormat("257 MKD/XMKD command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                printf("error: %s\n", err.GetMsg());
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, (const char *)err.GetMsg());
            }
            return true;
        }
        else if (txt.Left(4).ToUpper() == "RMD " || txt.Left(5).ToUpper() == "XRMD ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(txt.Left(1).ToUpper() == "X" ? 5 : 4));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, false, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DD+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 you have no permissions to delete directory %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                CMaaFile::RmDir(FileName, true);
                m_OutBuffer += CMaaString::sFormat("250 RMD/XRMD command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer += CMaaString::sFormat("550 %S: %s\r\n", &tmp2, (const char *)err.GetMsg());
            }
            return true;
        }
        else if (txt.Left(5).ToUpper() == "RNFR " || txt.Left(5).ToUpper() == "RNTO ")
        {
            if   (txt.Left(5).ToUpper() == "RNTO " && m_CmdHistory[1].Left(5).ToUpper() != "RNFR ")
            {
                m_OutBuffer += "503 Bad sequence of commands.\r\n";
                return true;
            }
            //printf("%s\n", (const char *)txt);
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.Mid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN("/", "\\");
            FileName = CMaaFile::MkCompatible(FileName);
            if   (txt.Left(5).ToUpper() == "RNFR ")
            {
                if   (CMaaFile::IsADir(FileName) || CMaaFile::IsAFile(FileName))
                {
                    if   (CMaaFile::IsAFile(FileName) && Perm.Find(" Ren+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 you have no permissions to rename the file %S.\r\n", &tmp2);
                        return true;
                    }
                    if   (CMaaFile::IsADir(FileName) && Perm.Find(" DRen+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer += CMaaString::sFormat("550 you have no permissions to rename the directory %S.\r\n", &tmp2);
                        return true;
                    }
                    m_OutBuffer += "350 File or directory exists, ready for destination name\r\n";
                }
                else
                {
                    CMaaString tmp2 = ToFtpSafe(cName);
                    m_OutBuffer += CMaaString::sFormat("550 %S: No such file or directory.\r\n", &tmp2);
                }
                return true;
            }

            CMaaString From = ToFileName(m_CmdHistory[1].Mid(5));
            if   (!GetRealAndCanonicalFsName(m_Path, From, &From, NULL, true))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //From.ReplaceNN("/", "\\");
            From = CMaaFile::MkCompatible(From);
            try
            {
                CMaaFile::Rename(From, FileName, true);
                m_OutBuffer += "250 RNTO command successful.\r\n";
            }
            catch(XTOOError err)//XTOOFile2Error
            {
                m_OutBuffer += CMaaString::sFormat("550 RNTO error: %s.\r\n", (const char *)err.GetMsg());
            }
            return true;
        }
        else
        {
            if   (m_OutBuffer.Length() < 5 * 1024)
            {
                m_OutBuffer += CMaaString::sFormat("500 Command not understood: ");
                for  (int i = 0; i < txt.Length() && i < 100; i++)
                {
                    if   (txt[i] > ' ' && txt[i] < 0x7f)
                    {
                        m_OutBuffer += txt.Mid(i, 1);
                    }
                    else
                    {
                        char ttt[20];
                        sprintf(ttt, "%%%02X", (int)(unsigned char)(char)txt[i]);
                        m_OutBuffer += ttt;
                    }
                }
                m_OutBuffer += "\r\n";
                //m_OutBuffer += CMaaString::sFormat("500 Command not understood: %S%s\r\n", &ToFtpSafe(txt.Left(32)), txt.Length() > 32 ? "..." : "");
            }
            else
            {
                SayError();
            }
        }
        break;
    }
    //printf("return\n");
    return true;
}
//---------------------------------------------------------------------------
bool CMaaFtpServerConnection::GetRealAndCanonicalFsName(CMaaString CurrentPath, CMaaString FsName, CMaaString *pRealDir, CMaaString *pCanonicalDir, bool bFile, CMaaString *pPermissions)
{
    //    printf("\n\nGetRealAndCanonicalFsName(CurrentPath=%S\nFsName=%S)\n", &CurrentPath, &FsName);

    FsName.ReplaceNN("\\", "/");
    //FsName = CMaaFile::MkCompatible(FsName);

    //printf("GetRealAndCanonicalFsName(%s, %s,,, %s)\n", (const char *)CurrentPath, (const char *)FsName, bFile ? "true" : "false");

    CMaaString OneFileName;
    if   (bFile)
    {
        int n = FsName.ReverseFind('/');
        if   (n < 0)
        {
            if   (CurrentPath[0] != '/')
            {
                return false;
            }
            OneFileName = FsName;
            FsName.Empty();
        }
        else
        {
            OneFileName = FsName.Mid(n + 1);
            FsName = FsName.Left(n);
            if   (FsName == "")
            {
                FsName = "/";
            }
        }
        if   (OneFileName == "" || OneFileName == "." || OneFileName == "..")
        {
            return false;
        }
    }
    if   (FsName[0] == '/')
    {
        CurrentPath = "/";
        CurrentPath.Empty();
    }

    //printf("FsName=%S\n", &FsName);

    CMaaString RealRootDir;
    {
        CMaaXmlNode n = m_UserNode.FindNodeWithAttr("dir", "vfs_path", "/");
        if   (n.IsNull())
        {
            return false;
        }
        RealRootDir = n.FindAttribute("real_path");
        if   (!CMaaFile::IsADir(RealRootDir))
        {
            return false;
        }
    }
    int a, b;
    CMaaString RealDir = RealRootDir, cDir = "/", Path;
    for  (a = 0; a < CurrentPath.Length(); )
    {
        b = CurrentPath.Find(a + 1, '/');
        if   (b < 0)
        {
            b = CurrentPath.Length();
        }
        cDir = CurrentPath.Left(b);
        //
        // map canonical cDir to real fs dir
        //
        //          printf("RealRootDir = %S\na = %d\nCurrentPath[0]=%c\n", &RealRootDir, a, (char)CurrentPath[0]);
        if   (RealRootDir == "/" && a == 0 && CurrentPath[0] == '/')
        {
            RealDir.Empty();
        }
        RealDir = RealDir + CurrentPath.Mid(a, b - a);
        //          printf("RealDir = %S\n", &RealDir);
        CMaaXmlNode n = m_UserNode.FindNodeWithAttr("dir", "vfs_path", cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute("real_path");
        }
        /*
          if	(cDir == "/")
          {
               RealDir = RealRootDir;
          }
          */
        a = b;

        Path = RealDir;
        //          printf("Path = %S\n", &Path);
        //Path.ReplaceNN("/", "\\");
        Path = CMaaFile::MkCompatible(Path);
        //          printf("Path = %S\n", &Path);
        if   (!CMaaFile::IsADir(Path))
        {
            //          printf("return false\n");
            //printf("false\n");
            return false;
        }
    }

    //printf("point2\n");


    //printf("cDir = %S, RealDir = %S, FsName = \"%S\"\n", &cDir, &RealDir, &FsName);

    a = 0;
    if   (FsName[0] == '/')
    {
        a++;
    }
    for  (; a < FsName.Length(); )
    {
        b = FsName.Find(a, '/');
        if   (b < 0)
        {
            b = FsName.Length();
        }
        CMaaString tmp = FsName.Mid(a, b - a);
        a = b + 1;
        if   (tmp == ".")
        {
            continue;
        }
        if   (tmp == "..")
        {
            if   (cDir == "/")
            {
                return false;
            }
            int n = cDir.ReverseFind('/');
            if   (n == 0)
            {
                n++;
            }
            cDir = cDir.Left(n);
            if   (!GetRealAndCanonicalFsName(cDir, "", &RealDir, &cDir, false))
            {
                return false;
            }
            continue;
        }

        //printf("-RealDir=%S\n", &RealDir);
        //printf("-cDir=%S\n", &cDir);
        //printf("-FsName=%S\n", &FsName);

        if   (cDir == "/")
        {
            cDir = cDir + tmp;
        }
        else
        {
            cDir = cDir + "/" + tmp;
        }
        //
        // map canonical cDir to real fs dir
        //
        if   (RealDir == "/")
        {
            RealDir = RealDir + tmp;
        }
        else
        {
            RealDir = RealDir + "/" + tmp;
        }

        CMaaXmlNode n = m_UserNode.FindNodeWithAttr("dir", "vfs_path", cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute("real_path");
        }

        Path = RealDir;
        //Path.ReplaceNN("/", "\\");
        Path = CMaaFile::MkCompatible(Path);
        if   (!CMaaFile::IsADir(Path))
        {
            return false;
        }
    }
    //printf("-----cDir = %S, RealDir = %S\n", &cDir, &RealDir);

    if   (pPermissions)
    {
        CMaaString Path, Perm, tmp;
        Path = CMaaFile::MkCompatible(RealDir == "/" ? RealDir : RealDir + "/");
#ifdef _WIN32
        Path = Path.ToUpper();
#endif
        int x = 0;
        for  (CMaaXmlNode n = m_UserNode.FindNode("dirperm"); !n.IsNull(); n = n.FindNext())
        {
            tmp = n.FindAttribute("path");
            if   (tmp != "/")
            {
                tmp += "/";
            }
            tmp = CMaaFile::MkCompatible(tmp);
#ifdef _WIN32
            tmp = tmp.ToUpper();
#endif
            if   (tmp.Length() <= Path.Length() && tmp == Path.Left(tmp.Length()))
            {
                //printf("tmp = %S, Path = %S\n", &tmp, &Path);
                if   (x < (int)tmp.Length())
                {
                    Perm = n.FindAttribute("perm");
                    //printf("Perm = %S\n", &Perm);
                    x = (int)tmp.Length();
                }
            }
        }
        *pPermissions = Perm;
    }
    if   (bFile)
    {
        if   (cDir == "/")
        {
            cDir = cDir + OneFileName;
        }
        else
        {
            cDir = cDir + "/" + OneFileName;
        }
        if   (RealDir == "/")
        {
            RealDir = RealDir + OneFileName;
        }
        else
        {
            RealDir = RealDir + "/" + OneFileName;
        }

        CMaaXmlNode n = m_UserNode.FindNodeWithAttr("dir", "vfs_path", cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute("real_path");
        }
        /*
          Path = RealDir;
          //Path.ReplaceNN("/", "\\");
          Path = CMaaFile::MkCompatible(Path);
          if   (!CMaaFile::IsADir(Path))
          if   (!CMaaFile::IsAFile(Path))
          {
               return false;
          }
          */
    }
    //printf("cDir = %s, RealDir = %s\n", (const char *)cDir, (const char *)RealDir);
    //printf("RealDir=%S\n", &RealDir);
    //printf("cDir=%S\n", &cDir);

    if   (pRealDir)
    {
        *pRealDir = RealDir;
    }
    if   (pCanonicalDir)
    {
        *pCanonicalDir = cDir;
    }
    return true;
}
//---------------------------------------------------------------------------
void CMaaFtpServerConnection::SayError()
{
    if   (m_OutBuffer.Length() < 5 * 1024)
    {
        m_OutBuffer += "500 Command not understood\r\n";
    }
}
//---------------------------------------------------------------------------
CFtpServerData::CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer) // accept
:    CMaaTcpSocket(pFdSockets),
m_ConnTxt("Ftp-Data"),
m_File(NULL),
m_Timer0(this, 0),
m_Timer1(this, 1),
m_TimerTimeOut10(this, 10),
m_TimerAbor13(this, 13)
{
    m_Timer0.Attach(pFdSockets);
    m_Timer1.Attach(pFdSockets);
    m_TimerTimeOut10.Attach(pFdSockets);
    m_TimerAbor13.Attach(pFdSockets);
    m_BytesTransferred = 0;
    m_Error = 0;
    m_Time0 = 0;
    m_Mode = -1;
    gLock.LockM();
    m_pServer = pServer;
    m_pServer->m_pDataConn = this;
    gLock.UnLock();
    //AddFdSocket(); //???
}

CFtpServerData::CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _IP Ip, _Port Port, CMaaString strServerIpPort)
:    CMaaTcpSocket(pFdSockets),
m_ConnTxt("Ftp-Data"),
m_File(NULL),
m_Timer0(this, 0),
m_Timer1(this, 1),
m_TimerTimeOut10(this, 10),
m_TimerAbor13(this, 13)
{
    printf("CFtpServerData::CFtpServerData(%I, %d, %S)...\n", Ip, Port, &strServerIpPort);
    m_Timer0.Attach(pFdSockets);
    m_Timer1.Attach(pFdSockets);
    m_TimerTimeOut10.Attach(pFdSockets);
    m_TimerAbor13.Attach(pFdSockets);
    m_BytesTransferred = 0;
    m_Error = 10;
    m_Time0 = 0;
    m_Mode = -1;
    m_pServer = pServer;
    AddFdSocket();
    ChangeFdMode(eAll);
    if   (strServerIpPort.Length() && strServerIpPort.Find(':') > 0)
    {
        _IP BindIp = 0;
        int Port = 0;
        CMaaIpToLongEx(strServerIpPort, &BindIp, ":");
        //strIpPort = strIpPort.Mid(trailer_pos > 0 ? trailer_pos + 1: 1);
        //sscanf(strIpPort, "%d", &Port);
        if   (BindIp && 0)
        {
            int x = Bind(0, BindIp, true);
            printf("Bind(0, %I) returns %d\n", BindIp, x);
            for  (int nn = 0; nn < 1000 && x <= 0; nn++)
            {
                int portn = 0;
                GetRnd(&portn, (int)sizeof(portn));
                portn = ((unsigned)portn % 50000U) + 14000;
                printf("binding (%d, %I)...\n", portn, BindIp);
                x = Bind(portn, BindIp, true);
                if   (x > 0)
                {
                    printf("Bind(%d, %I) returns %d\n", portn, BindIp, x);
                }
            }
        }
    }
    AsyncConnect(Ip, Port);
}
CFtpServerData::~CFtpServerData()
{
    //printf("CFtpServerData::~CFtpServerData()\n");
    gLock.LockM();
    if   (m_pServer && m_pServer->m_pDataConn == this)
    {
        m_pServer->m_pDataConn = NULL;
        m_pServer->m_DataError = m_Error;
        m_pServer->m_DataBytesTransferred = m_BytesTransferred;
        m_pServer->m_TransferTime = GetTickCount() - m_Time0;
        //printf("m_pServer->m_Timer3.Start(1);\n");
        m_pServer->m_Timer3.Start(1);
    }
    gLock.UnLock();
}

int CFtpServerData::Notify_Read()
{
    //printf("R\n");fflush(stdout);
    if   (m_Mode != 2)
    {
        return eDisableRead;
    }
    int r = RecvData(m_Buffer, sizeof(m_Buffer));
    //     printf("CFtpServerData::Notify_Read(): r = %d\n", r);
    //printf("(%d)\n", r);fflush(stdout);
    if   (IsClosed(r))
    {
        //          printf("CFtpServerData::Notify_Read(): IsClosed(%d) == true\n", r);
        //printf("closing\n");fflush(stdout);
        m_Error = 0;
        CloseByException("File recv complete");
    }
    //printf("point1\n");fflush(stdout);
    m_TimerTimeOut10.Start(TIME_OUT_1);

    //printf("point2\n");fflush(stdout);
    gLock.LockM();
    //printf("point3\n");fflush(stdout);
    int us1=-1, us2=-1;
    if   (m_pServer)
    {
        us1=1000000000, us2=1000000000;
        //printf("point4\n");fflush(stdout);
        m_pServer->m_TimerTimeOut10.GetWaitForTime(&us1, gpSockStartup->GetTime());
        m_pServer->m_TimerTimeOut10.Start();
        m_pServer->m_TimerTimeOut10.GetWaitForTime(&us2, gpSockStartup->GetTime());
    }
    //printf("point5\n");fflush(stdout);
    gLock.UnLockM();
    //printf("point6\n");fflush(stdout);
    //printf("us1=%d, us2=%d\n", us1, us2);

    try
    {
        //printf("point7\n");fflush(stdout);
        int x = m_File.Write(m_Buffer, r);
        //printf("file write %d\n", x);fflush(stdout);
        if   (x > 0)
        {
            m_BytesTransferred += x;
        }
        if   (x != r)
        {
            m_Error = 2; // file write error
            CloseByException("file write error");
        }
    }
    catch(XTOOFile2Error err)
    {
        //printf("file write error\n");fflush(stdout);
        //if   (m_BufferSize == 0)
        {
            m_Error = 2; // file write error
            CloseByException("file write error");
        }
    }
    return eRead;
}
int CFtpServerData::Notify_Write()
{
    if   (m_Mode != 0 && m_Mode != 1)
    {
        return eDisableWrite;
    }
    m_TimerTimeOut10.Start(TIME_OUT_1, false);

    gLock.LockM();
    if   (m_pServer)
    {
        m_pServer->m_TimerTimeOut10.Start();
    }
    gLock.UnLock();

    if   (m_Mode == 0)
    {
        int w;
        do
        {
            w = SendData(m_DataTxtPos + (const char *)m_DataTxt, m_DataTxt.Length() - m_DataTxtPos);
            //               printf("CFtpServerData::Notify_Write(): w = %d\n", w);
            m_DataTxtPos += w;
            m_BytesTransferred += w;

        }    while(w > 0);
        if   (m_DataTxtPos >= (int)m_DataTxt.Length())
        {
            m_Error = 0;
            //printf("Send LIST %d bytes complete\n", m_DataTxtPos);
            CloseByException("Send LIST complete");
        }
        m_TimerTimeOut10.Start(TIME_OUT_1, false);
        return eWrite;
    }
    if   (m_Mode == 1)
    {
        int w;
        do
        {
            try
            {
                int x = m_File.Read(m_Buffer + m_BufferSize, sizeof(m_Buffer) - m_BufferSize);
                if   (x > 0)
                {
                    m_BufferSize += x;
                }
            }
            catch(XTOOFile2Error err)
            {
                if   (m_BufferSize == 0)
                {
                    m_Error = 1; // file read error
                    CloseByException("file read error");
                }
            }
            if   (m_BufferSize == 0)
            {
                m_Error = 0;
                CloseByException("Send file complete");
            }
            w = SendData(&m_Buffer[m_BufferPos], m_BufferSize - m_BufferPos);
            //               printf("CFtpServerData::Notify_Write(): w = %d\n", w);
            m_BufferPos += w;
            m_BytesTransferred += w;
            if   (m_BufferPos > 0)
            {
                memmove(m_Buffer, m_Buffer + m_BufferPos, m_BufferSize -= m_BufferPos);
                m_BufferPos = 0;
            }

        }    while(w > 0);
        m_TimerTimeOut10.Start(TIME_OUT_1, false);
        return eWrite;
    }
    return 0;
}
int CFtpServerData::Notify_Error()
{
    {
        XTOOSockErr e("CFtpServerData::Notify_Error():", NULL);
        //CProtocolListColor c(CProtocolListColor::eRed);
        printf("%s\n", e.GetMsg());

        fflush(stdout);
        if   (e.GetErrorCode() == CMaa_CONN_CLOSED_ERROR)
        {
            if   (m_Mode != 2)
            {
                //return eDisableRead;
            }
            else
            {
                //printf("closing\n");fflush(stdout);
                m_Error = 0;
                CloseByException("File recv complete");
            }
        }
    }
    CloseByExeption("");
    return 0;
}
int CFtpServerData::Notify_Accepted(_IP IpFrom, _Port Port)
{
    m_Time0 = GetTickCount();
    bool b = false;
    gLock.LockM();
    if   (m_pServer && m_pServer->m_pDataConn == this)
    {
        m_pServer->SetPeerAccepted(IpFrom, Port, true);
        b = true;
    }
    gLock.UnLock();
    if   (!b)
    {
        throw 1;
        //CloseByException("No server to data connection pointer");
    }
    //printf("CFtpServerData::Notify_Accepted(_IP IpFrom, _Port Port)\n");
    return eDisableRead|eDisableWrite|eExept;
}
int CFtpServerData::Notify_Connected(_IP Ip, _Port Port, const char * DnsName)
{
    printf("connected\n");//fflush(stdout);

    m_Time0 = GetTickCount();
    bool b = false;
    gLock.LockM();
    if   (m_pServer && m_pServer->m_pDataConn == this)
    {
        m_pServer->SetPeerAccepted(Ip, Port, false);
        b = true;
    }
    gLock.UnLockM();
    if   (!b)
    {
        CloseByException("No server to data connection pointer");
    }
    return eAll;
}
void CFtpServerData::OnTimer(int f)
{
    //     printf("CFtpServerData::OnTimer(): f = %d\n", f);
    //printf("OnTimer(%d)\n", f);fflush(stdout);

    switch(f)
    {
    case 10:
        //printf("CFtpServerData::OnTimer(%d)\n", f);
        printf("Data transfer %d s time out\n", TIME_OUT_1 / 1000000);
        m_Error = 3; // time out
    case 0:
        if   (f == 0)
        {
            //printf("CFtpServerData::OnTimer(%d)\n", f);
        }
        CloseByException("OnTimer(0)");
        break;
    case 1:
        m_Timer1.Stop();
        ChangeFdMode(eAll);
        break;
    case 13:
        //printf("CFtpServerData::OnTimer(%d)\n", f);
        printf("Data transfer aborted\n");
        m_Error = 13; // abort
        CloseByException("OnTimer(13)");
        break;
    }
}
//---------------------------------------------------------------------------
CFtpDataServer::CFtpDataServer(CMaaFdSockets * pFdSockets, int Port, CMaaFtpServerConnection * pFtpServerConnection)
:    CMaaUnivServer(pFdSockets, Port, "FTP Data Server"),
m_Timer0(this, 0),
m_pFtpServerConnection(pFtpServerConnection)
{
    m_Timer0.Attach(pFdSockets);
}
//---------------------------------------------------------------------------
CFtpDataServer::~CFtpDataServer()
{
    //printf("CFtpDataServer::~CFtpDataServer()\n");
    gLock.LockM();
    if   (m_pFtpServerConnection && m_pFtpServerConnection->m_pPasvServer == this)
    {
        m_pFtpServerConnection->m_pPasvServer = NULL;
    }
    gLock.UnLock();
}
//---------------------------------------------------------------------------
CMaaTcpSocket * CFtpDataServer::CreateNewConnection(CMaaFdSockets * pFdSockets)
{
    m_Timer0.Start(1);
    return new CFtpServerData(pFdSockets, m_pFtpServerConnection);
}
//---------------------------------------------------------------------------
void CFtpDataServer::OnTimer(int f)
{
    //     printf("CFtpDataServer::OnTimer(): f = %d\n", f);
    CloseByException("OnTimer(0)");
}
//---------------------------------------------------------------------------

/*
class AA
{
     int m;
public:
     AA(int x)
     {
          m = x;
     }
     void t()
     {
          printf("m = %d\n", m);
     }
};
*/

int main(int argn, char * args[])
{
    //signal(SIGHUP, OnMySIGHUP);

    if   (0)
    {
        CMaaString Path = CMaaFile::getcwd();
        printf("%s\n", (const char *)Path);
        Path = CMaaFile::AppendPath(Path, "unix");
        printf("%s\n\n", (const char *)Path);

        CMaaFile::chdir(Path);
        Path = CMaaFile::getcwd();
        printf("%s\n\n", (const char *)Path);


        Path = CMaaFile::AppendPath(Path, "..");
        printf("%s\n", (const char *)Path);

        CMaaString tmp;

        do
        {
            tmp = Path;
            Path = CMaaFile::AppendPath(Path, "..");
            printf("%s\n", (const char *)Path);

        } while(Path != tmp);
    }

    /*
#ifdef _WIN32
     CMaaFile::chdir("windows");
#endif

#ifdef __unix__
     CMaaFile::chdir("unix");
#endif
*/
    // (size_t)( (ptrdiff_t)
    //printf("sizeof(int) == %d, sizeof(long) == %d, sizeof(_dword) == %d, sizeof(_qword) == %d\n",
    //	sizeof(int), sizeof(long), sizeof(_dword), sizeof(_qword));

#if 0
    {
        {
            struct __finddata64_t m_ff;
            intptr_t m_h;
            m_h = _findfirst64("C:\\windows\\System32\\*.*", &m_ff);
            if   (m_h != -1)
            {
                do
                {
                    int x = (int)strlen(m_ff.name);
                    if   (x > 4 && !stricmp(m_ff.name + x - 4, ".inf"))
                    {
                        printf("%s\n", m_ff.name);
                    }

                } while(_findnext64(m_h, &m_ff) != -1);
                _findclose(m_h);
            }
        }
        printf("--------------\n");
        int nn = 0;
        //CMaaFindFile2 ff("Debug");
        CMaaFindFile2 ff("c:\\windows\\system32\\*.inf", 1);
        CMaaFindFile2::sFind f;
        while(ff.Get(f))
        {
            const char * nm[5] = {"unknown", "file", "dir", "dot", "dotdot"};

            printf("%3d %s %s\n", ++nn, f.GetTypeName(), (const char *)f.m_FileName);
        }
        printf("--------------\n");
    }
#endif

    try
    {
        printf("MaaSoftware FTP server v " FTP_SERVER_VERSION "\n" FTP_SERVER_COPYRIGHT "\nweb: http://www.maasoftware.ru , http://www.maasoftware.com\ne-mail: support@maasoftware.ru , support@maasoftware.com\n\nCurrent directory: %s\n", (const char *)CMaaFile::getcwd());

        if   (1)
        {
            printf("Platform: ");
#ifdef _WIN64
            printf("WIN64 ");
#else
#ifdef _WIN32
            printf("WIN32 ");
#endif
#endif
#ifdef __unix__
            if   (sizeof(void *) == 8)
            {
                printf("Unix 64 bits ");
            }
            else
            {
                printf("Unix 32 bits ");
            }
#ifdef TL_EPOLL
#ifdef TL_EPOLLET
            printf("EPOLLET version ");
#else
            printf("EPOLL version ");
#endif
#endif
#endif
            printf("\n\n");
        }

        CheckLicense();
    }
    catch(...)
    {
        for  (int i = 10; i > 0; i--)
        {
            printf(" \rWait %d...", i);
            fflush(stdout);
#ifdef _WIN32
            Sleep(1000);
#else
            usleep(1000000);
#endif
        }
        printf("\r                         \n");
    }
    /*
     {
          AA a(1);
          for  (int i = 0; i < 10; i++)
          {
               {
                    a.t();
               }
               AA a(2);
               a.t();
          }
     }
     */
    if   (0)
    {
        printf("--------------\n");
        int nn = 0;
        //CMaaFindFile2 ff("Debug");
        CMaaFindFile2 ff("/home/*", 2);
        CMaaFindFile2::sFind f;
        while(ff.Get(f))
        {
            const char * nm[5] = {"unknown", "file", "dir", "dot", "dotdot"};

            printf("%3d %s %s\n", ++nn, f.m_Type >= CMaaFindFile2::sFind::eFile && f.m_Type <= CMaaFindFile2::sFind::eDotDot ? nm[f.m_Type] : nm[0],
                 (const char *)f.m_FileName);
        }
        printf("--------------\n");
    }
    /*
     {
          printf("--------------\n");
          int nn = 0;
          //CMaaFindFile2 ff("Debug");
          CMaaFindFile2 ff("c:\\ftp\\*", -1);
          CMaaFindFile2::sFind f;
          while(ff.Get(f))
          {
               const char * nm[5] = {"unknown", "file", "dir", "dot", "dotdot"};

               printf("%3d %s %s\n", ++nn, f.m_Type >= CMaaFindFile2::sFind::eFile && f.m_Type <= CMaaFindFile2::sFind::eDotDot ? nm[f.m_Type] : nm[0],
                    (const char *)f.m_FileName);
          }
          printf("--------------\n");
     }
     {
          int nn = 0;
          CMaaFindFile2 ff("Debug\\Ftp*");
          //CMaaFindFile2 ff(".\\Ftp*");
          //CMaaFindFile2 ff(".", "*\\Ftp*", -1);
          //CMaaFindFile2 ff("Debug", "*\\Ftp*", -1);
          CMaaFindFile2::sFind f;
          while(ff.Get(f))
          {
               const char * nm[5] = {"unknown", "file", "dir", "dot", "dotdot"};

               printf("%3d %s %s\n", ++nn, f.m_Type >= CMaaFindFile2::sFind::eFile && f.m_Type <= CMaaFindFile2::sFind::eDotDot ? nm[f.m_Type] : nm[0],
                    (const char *)f.m_FileName);
          }
     }
     return 1;
     */

    CMaaString port = "0.0.0.0:21";

    bool bOk = false;
    try
    {
        g_ConfigFileName = CMaaFile::GetCurrentDirectory() + szFILESYSTEM_SLASH + "FtpServerCfg.xml";
        s_FileTime = CMaaFile::GetFileTime64(g_ConfigFileName, false);
        s_FileSize = CMaaFile::Length(g_ConfigFileName, false);
        s_LastUpdatedTime = time(NULL);

        const char * pszConfigFileName = g_ConfigFileName; //"FtpServerCfg.xml";
        CMaaFile f(pszConfigFileName, "R");
        CMaaString txt = f.Read();

        CMaaXmlDocument doc("FtpServer");
        CMaaString errorMsg;
        int errorLine, errorColumn;
        if   (!doc.SetContent(txt, &errorMsg, &errorLine, &errorColumn, 0))
        {
            CMaaString txt;
            txt.Format2("%s%d%d%s", TR("Error in config file \"%1\"\nLine: %2, Column: %3, Error: %4"), pszConfigFileName, errorLine, errorColumn, (const char *)errorMsg);
            throw txt;
        }
        m_Cfg = doc;
        CMaaXmlElement e = doc.DocumentElement();
        FailLog = e.FindAttribute("faillog");
        CMaaString tmp = e.FindAttribute("Port");
        if   (tmp != "")
        {
            port = tmp;
        }
        //sscanf(e.FindAttribute("Port"), "%d", &port);
        bOk = true;
    }
    catch(XTOOFile2Error e)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        CMaaString txt;
        //txt.Format2("%s", "Error reading config file: %1", (const char *)e.GetOemMsg());
        txt.Format2("%s", "Error reading config file: %1", e.GetMsg());
        //MessageBox(NULL, txt, "FtpServerConfig", MB_OK);
        printf("%s\n", (const char *)txt);
    }
    catch(CMaaString txt)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        //MessageBox(NULL, txt, "FtpServerConfig", MB_OK);
        printf("%S\n", &txt);
    }
    catch(...)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        CMaaString txt;
        txt.Format2("", "Error loading config file");
        //MessageBox(NULL, txt, "FtpServerConfig", MB_OK);
        printf("%s\n", (const char *)txt);
    }
    if   (!bOk)
    {
        return 2;
    }


    if   (argn > 1)
    {
        CMaaString tmp = args[1];//e.FindAttribute("Port");
        if   (tmp != "")
        {
            port = tmp;
        }
        //sscanf(args[1], "%d", &port);
        //printf("Using port %d\n", port);
        printf("Using port %S\n", &port);
    }
    try
    {
        CMaaSockStartup st;
        gpSockStartup = &st;
        CMaaSockThread thr(NULL);
        CMaaFtpServer * s = new CMaaFtpServer(thr.m_pFdSockets, port);
        thr.Create();
#ifdef _WIN32
        thr.Wait(INFINITE);
#else
        while(1)
        {
            gLock.LockM();
            int c = gChildren;
            gLock.UnLockM();
            if   (c)
            {
                int status;
                pid_t pid = wait(&status);
                printf("wait() returns %d\n", (int)pid);
                if   (pid == -1)
                {
                    perror("error: ");
                }
                else
                {
                    if   (WIFEXITED(status))
                    {
                        printf("(%d is exited)\n", (int)pid);
                        gLock.LockM();
                        int c = 0;
                        for  (int i = 0; i < gChildren; i++)
                        {
                            if   (ChildrenPids[i] != pid)
                            {
                                ChildrenPids[c++] = ChildrenPids[i];
                            }
                        }
                        gChildren = c;
                        gLock.UnLockM();
                    }
                }
            }
            else
            {
                usleep(100000);
            }
        }
        thr.Wait((DWORD)-1);
#endif
    }
    catch(XTOOSockErr err)
    {
        printf("catch(XTOOSockErr): %s\n", err.GetMsg());
    }
    catch(...)
    {
        printf("catch(...)\n");
    }
    gpSockStartup = NULL;
    return 0;
}

/*
chdir(const char *);
getcwd(char *, int);
chroot(const char *);
*/
