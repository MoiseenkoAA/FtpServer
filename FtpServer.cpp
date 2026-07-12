// FtpServer1.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Temp.h"
#ifndef RR_SVC
#include "FtpServer.h"
#endif

//#define FTP_DBG
#ifdef FTP_DBG
static int rrlog(const char* msg) { CMaaAtomicFastMutexLocker agLocker(gLock); CMaaFile log("c:\\temp\\ftp_log.txt", CMaaFile::eAC_SrSw, eNoExcept); log.Write(msg, (int)strlen(msg)); log.Close(); return 1; }
#else
static int rrlog(const char* msg) { return 0; }
#endif

#if defined(__SHAPERS) || !defined(TL_EPOLL)
#define NO_FORK
#endif

#ifdef __SHAPERS
#undef __SHAPERS
#endif

#ifdef RR_SVC
CMaaUnivHash<int, CMaaFtpServer*> ghFtpServers;
#endif

#ifndef RR_SVC

CMaaFile gLog, gLoginLog;
CMaaString gLogLock;

#ifdef _UNICODE
#ifdef printf
#undef printf
#endif
//#define printf printf_time(); __utf8_printf
#define printf printf_log
/*
#ifdef _tprintf
#undef _tprintf
#endif
#define _tprintf __unicode_printf
*/
#else
#ifdef __unix__
//#define printf printf_time(); __utf8_printf
#define printf printf_log
#endif
#endif

//#define printf2 printf_time(); __utf8_printf2
#define printf2 printf2_log

#endif

//#define BB_SUPPORT

/*
#ifdef _WIN32
#define FTP_ROOT_DIR "C:\\Ftp"
#else
//#define FTP_ROOT_DIR "/home/maa"
#define FTP_ROOT_DIR "/data/home/maasoftw"
#endif
*/


CMaaString FailLog, LoginLog;

#ifndef RR_SVC
CMaaMutex g_Mutex;
CMaaString g_ConfigFileName;
//static _qword s_FileTime = -2, s_FileSize = -2;
//static time_t s_LastUpdatedTime = -1;
static bool s_UpdateCfgForced = false;
CMaaXmlDocument m_Cfg("FtpServer");
#endif
std::atomic<bool> gbChild{false};

#ifdef __unix__
#include <signal.h>

static void OnMySIGHUP(int x)
{
     signal(SIGHUP, OnMySIGHUP);

     //s_FileTime = -2, s_FileSize = -2;
     //s_LastUpdatedTime = -1;
     s_UpdateCfgForced = true;

     //printf ( "OnMySIGPIPE\n" );
     //fflush ( stdout );
     return;
}
#endif

void printf_time()
{
#ifndef RR_SVC
    __utf8_printf("%t: ", time(nullptr));
#endif
}

void out_print_log(CMaaString txt)
{
#ifndef RR_SVC
    CFileTimedLock lk(gLogLock, 20, false);
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if (!gLog.IsOpen())
    {
        gLog = CMaaFile(CMaaFileStdout, CMaaFile::eA_SrSw, eNoExcept);
        if (gLog.IsOpen())
        {
            gLog.SetCloseOnExec(false);
        }
    }
    gLog.Seek(0, SEEK_END);
    gLog.Write(txt);
#endif
}

void out_print_login_log(CMaaString txt)
{
#ifndef RR_SVC
    CFileTimedLock lk(gLogLock, 20, false);
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if (!gLoginLog.IsOpen())
    {
        /*
        gLoginLog = CMaaFile(CMaaFileStdout, CMaaFile::eA_SrSw, eNoExcept);
        if (gLoginLog.IsOpen())
        {
            gLoginLog.SetCloseOnExec(false);
        }
        */
    }
    gLoginLog.Seek(0, SEEK_END);
    gLoginLog.Write(txt);
#endif
}

int printf_log(const char * format, ...)
{
#ifndef RR_SVC
    //CFileTimedLock lk(gLogLock, 20, false);
    CMaaString txt1 = GetTextDateTime(time(nullptr)) + ": ", txt2;
    va_list list;
    va_start(list, format);
    txt2.FormatV(format, list);
    va_end(list);
    txt1 += txt2;
    out_print_log(txt1);
#endif
    return 1;
}

int printf2_log(const char * format, const char * text, ...)
{
#ifndef RR_SVC
    //CFileTimedLock lk(gLogLock, 20, false);
    CMaaString txt1 = GetTextDateTime(time(nullptr)) + ": ", txt2;
    va_list list;
    va_start(list, text);
    txt2.FormatV2(format, text, list);
    va_end(list);
    txt1 += txt2;
    out_print_log(txt1);
#endif
    return 1;
}


#define MAX_CHILDREN 500
std::atomic<int> gChildren{0};
#ifdef __unix__
//CMaaPtrAE<pid_t> ChildrenPids(10);
CMaaUnivHash<pid_t> ghChildrenPids(MAX_CHILDREN + 10);
#endif

const int TIME_OUT_1 = 240000000;

#define DEF_m_TimerTimeOut10_Start_TIME_OUT_1 gLock.LockM(); m_TimerTimeOut10.Start(TIME_OUT_1); gLock.UnLockM()
#define DEF_SRV_m_TimerTimeOut10_Start_TIME_OUT_1 /*gLock.LockM();*/ m_pServer->m_TimerTimeOut10.Start(TIME_OUT_1, false); /*gLock.UnLockM()*/

static CMaaString ToFtpSafe(CMaaString txt)
{
    const int l = txt.Length();
    for  (int i = 0; i < l; i++)
    {
        if   ((unsigned char)txt[i] < ' ' || (unsigned char)txt[i] > 0x7f)
        {
            txt[i] = '_';
        }
    }
    return txt;
}

static CMaaString ToFtpSafeUtf8(CMaaString txt)
{
    return txt.Utf8ToPrintable(false);
}

static constexpr int GetHexNibble(char c) noexcept
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

//static constexpr CMaa256Bits b256Rus("ÀÁÂÃÄÅ¨ÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäå¸æçèéêëìíîïðñòóôõö÷øùúûüýþÿ", 2 * 33, false); // + ¨¸ - 2025

static int RussianAscii1251CarsCount(const CMaaString &str, int CountOne = 0) noexcept
{
    //CMaaString RussianAlphabet = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
    const CMaa256Bits& b256Rus = gfnCp1251RusFlags();
    int Count = 0;
    const char * pp = (const char *)(const char *)str;
    const char * ee = pp + str.Length();
    while(pp < ee)
    {
        char c = *pp++;
        //if   (RussianAlphabet.Find(c) >= 0)
        if (b256Rus.Test(c))
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

static int RussianOemCarsCount(const CMaaString &src, int CountOne = 0)
{
    //CMaaString RussianAlphabet = "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
    const CMaa256Bits& b256Rus = gfnCp1251RusFlags();
    int Count = 0;
    CMaaString str = src.OemToChar();
    const char * pp = (const char *)(const char *)str;
    const char * ee = pp + str.Length();
    while(pp < ee)
    {
        const char c = *pp++;
        //if   (RussianAlphabet.Find(c) >= 0)
        if (b256Rus.Test(c))
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

static CMaaString FromUtf8Ex(const CMaaString &Text)
{
    const int n1251 = RussianAscii1251CarsCount(Text);
    const int nOem = RussianOemCarsCount(Text);
    const int nUtf8 = Text.Utf8CharCount() * 2;
    if   (nUtf8 >= n1251 && nUtf8 >= nOem)
    {
    }
    else if (n1251 > 0 && n1251 > nOem)
    {
        return UnicodeToUtf8(AnsiToUnicode(Text, false, 1251));
    }
    else if (nOem > 0)
    {
        return UnicodeToUtf8(AnsiToUnicode(Text.OemToChar(), false, 1251));
    }
    return Text;
}

static CMaaString ToUtf8(CMaaString Text)
{
    return Text;
}


static CMaaString ToFileName(CMaaString txt)
{
    CMaaPtr_<char, 1> Buf(txt.Length());
    int i, j;
    const int n = txt.Length();
    for  (i = j = 0; i < n; i++)
    {
        if   (txt[i] == '%' && GetHexNibble(txt[i + 1]) >= 0 && GetHexNibble(txt[i + 2]) >= 0)
        {
            Buf[j++] = (GetHexNibble(txt[i + 1]) << 4) + GetHexNibble(txt[i + 2]);
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
#ifndef RR_SVC
CMaaFtpServer::CMaaFtpServer(CMaaFdSockets* pFdSockets, CMaaString Port, const char* ServerName)
#else
CMaaFtpServer::CMaaFtpServer(CMaaFdSockets* pFdSockets, CMaaString Port, int CfgNum, const char* ServerName)
#endif
:   CMaaUnivServer(pFdSockets, Port, ServerName),
    m_IpPort(Port),
    m_Timer0(this, 0),
    m_Timer1s(this, 1000),
    m_ServName(ServerName)
{
#ifdef RR_SVC
    m_CfgNum = CfgNum;
#endif
    m_Timer0.Attach(pFdSockets);
#ifdef RR_SVC
    m_Timer1s.Attach(pFdSockets);
#endif
#ifndef RR_SVC
    printf("Ready...\n");
#endif
#ifdef RR_SVC
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    ghFtpServers.AddOver(m_CfgNum, this);
    if (!(m_pStat = g_hFtpServerStat[CfgNum, nullptr]))
    {
        try
        {
            m_pStat = RR_NEW SFtpServerStat;
        }
        catch (...)
        {
            //m_pStat = nullptr;
        }
    }
    if (m_pStat)
    {
        m_pStat->SetServer(CfgNum, this);
    }
    gLocker.UnLock();
    m_Timer1s.Start(1000000);
#endif
#ifdef FTP_DBG
    rrlog("||| CMaaFtpServer::CMaaFtpServer()\n");
#endif
}
//---------------------------------------------------------------------------
CMaaFtpServer::~CMaaFtpServer()
{
    //printf("Finished\n");
#ifdef FTP_DBG
    rrlog("||| CMaaFtpServer::~CMaaFtpServer()\n");
#endif
    //m_pFdSockets->m_pThread->RemoveWakeUpPair();
#ifdef RR_SVC
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    if (ghFtpServers[m_CfgNum, nullptr] == this)
    {
        ghFtpServers.Remove(m_CfgNum);
    }
    if (m_pStat)
    {
        m_pStat->UnSetServer(this);
    }
    gLocker.UnLock();
    printf("FtpServer#%d on port %S is exited\n", m_CfgNum, &m_IpPort);
#endif
}
//---------------------------------------------------------------------------
int CMaaFtpServer::Notify_Error()
{
    XTOOSockErr err("CMaaFtpServer::Notify_Error()", nullptr);
    printf("CMaaFtpServer::Notify_Error(): %s\n", err.GetMsg());
    CloseByException("Error");
    return 0;
}
//---------------------------------------------------------------------------
void CMaaFtpServer::OnTimer(int f)
{
#ifdef RR_SVC
    if (f == 1000)
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pStat)
        {
            m_pStat->OnTimer();
        }
        return;
    }
    CMaaFtpServerConnection* p;
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    while ((p = m_Connections.GetFromFront()))
    {
        p->m_pServer = nullptr;
        p->m_Timer0.Start(1);
    }
    gLocker.UnLock();
#endif

    //    printf("CMaaFtpServer::OnTimer(): f = %d\n", f);
    CloseByException("");
}
//---------------------------------------------------------------------------
CMaaTcpSocket * CMaaFtpServer::CreateNewConnection(CMaaFdSockets * pFdSockets)
{
#ifdef NO_FORK
#ifndef RR_SVC
    return new CMaaFtpServerConnection(pFdSockets, m_IpPort, "FTP server connection");
#else
    return new CMaaFtpServerConnection(pFdSockets, m_IpPort, m_CfgNum, this, "FTP server connection");
#endif
#else
    printf("CMaaFtpServer::CreateNewConnection(): fork()\n");
#ifdef __unix__
    
    pid_t pid = -1;
    if (gChildren.load() < MAX_CHILDREN)
    {
        pid = fork();
        printf("CMaaFtpServer::CreateNewConnection(): fork() returns %d\n", (int)pid);
    }
    if   (pid < 0)
    {
        return nullptr;
    }
    if   (pid == 0)
    {
        m_pFdSockets->close_epoll_fd();//FXX_epollfd(); // m_pFdSockets->close_epoll_fd();
        //m_Timer0.Start(1000000);
        m_Timer0.Start(1);
        gbChild = true;
        return new CMaaFtpServerConnection(pFdSockets, m_IpPort, "FTP server connection");
    }
    if   (pid > 0)
    {
        pFdSockets->close_epoll_fd();//FXX_epollfd(); // pFdSockets->close_epoll_fd();
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        //ChildrenPids[gChildren++] = pid;
        if (!ghChildrenPids.Add(pid, TOOLSLIB_HT_UNUSED_DATA0))
        {
            ++gChildren;
        }
    }
    return nullptr;
#else
    return new CMaaFtpServerConnection(pFdSockets, m_IpPort, "FTP server connection");
#endif
#endif
}
//---------------------------------------------------------------------------
#ifndef RR_SVC
CMaaFtpServerConnection::CMaaFtpServerConnection(CMaaFdSockets* pFdSockets, CMaaString ServerIpPort, const char* ClassName)
#else
CMaaFtpServerConnection::CMaaFtpServerConnection(CMaaFdSockets* pFdSockets, CMaaString ServerIpPort, int CfgNum, CMaaFtpServer* pServer, const char* ClassName)
#endif
    :   CMaaTcpSocket(pFdSockets),
#ifdef RR_SVC
    m_pServer(pServer),
#endif
    m_ConnName(ClassName),
    m_ServerIpPort(ServerIpPort),
    //m_ConnectionIpPort(ServerIpPort),
    //m_File(nullptr),
    m_Timer0(this, 0),
    m_Timer1(this, 1),
    m_Timer3(this, 3),
    m_TimerTimeOut10(this, 10),
    m_TimerAcceptTimeOut11(this, 11),
    m_Timer1s(this, 1000),
    m_UserNode(nullptr)
#ifdef RR_SVC
    , m_RRUserNode(nullptr)
#endif
{
    // printf("pFdSockets->LookEp();\n");
    // pFdSockets->LookEp();
#ifdef RR_SVC
    m_CfgNum = CfgNum;
#endif

    gbb = false;
    m_pDataConn = nullptr;
    m_pPasvServer = nullptr;
    m_Step = 0;
    m_State = 0;
    m_Type = 'I';
    m_Rest = 0;
    m_PortIp = 0;
    memset(m_PortIp6, 0, sizeof(m_PortIp6));
    m_PortPort = 0;
    m_EPSV_ALL = false;
    m_Path = gCon[CCon::e_FtpServerDir_vfs_path_root];
    m_TryN = 0;
    //m_TransferMode = -1;
    m_Timer0.Attach(pFdSockets);
    m_Timer1.Attach(pFdSockets);
    m_Timer3.Attach(pFdSockets);
    m_TimerTimeOut10.Attach(pFdSockets);
    m_TimerAcceptTimeOut11.Attach(pFdSockets);
#ifdef __SHAPERS
    if (0) // test by telnet
    {
        //gLock_lib.LockM();
        gpSockStartup&& gpSockStartup->m_SysShaperMutex.LockM();
        if (gpSockStartup)
        {
            // 1024000 B/s (1000 KB/s) speed limit
            gpSockStartup->m_ShaperThread.m_Snd1000->Add(m_SndLLShaper);
            //gpSockStartup->m_ShaperThread.m_Rcv1000->Add(m_RcvLLShaper);
        }
        gpSockStartup&& gpSockStartup->m_SysShaperMutex.UnLockM();
        //gLock_lib.UnLockM();
        m_SndLLShaper.SetConnectionLimits(5, -1); // 5 chars per second
        //m_RcvLLShaper.SetConnectionLimits(10000, -1);
    }
#endif
#ifdef RR_SVC
    m_Timer1s.Attach(pFdSockets);
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        m_pServer->m_Connections.AddAtBack(this);
        try
        {
            m_pStat = pServer->m_pStat ? RR_NEW SFtpConnStat(pServer->m_pStat) : nullptr;
        }
        catch (...)
        {
            m_pStat = nullptr;
        }
    }
    //m_Timer1s.Start(1000000);
#endif
#ifdef FTP_DBG
    rrlog("||| CMaaFtpServerConnection::CMaaFtpServerConnection()\n");
#endif
}
CMaaFtpServerConnection::~CMaaFtpServerConnection()
{
#ifdef FTP_DBG
    rrlog("||| CMaaFtpServerConnection::~CMaaFtpServerConnection()\n");
#endif
    {
        //_IP Ip = 0;
        //_Port Port = 0;
        //GetConnInfo(nullptr, nullptr, &Ip, &Port);
        //printf("%s\n", (const char *)CMaaString::sFormat("Closed connection for %I:%d", Ip, Port));
        printf("%s\n", (const char *)CMaaString::sFormat("Closed connection for %S", &m_ConnName));
    }
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
#ifdef RR_SVC
    if (m_pStat)
    {
        m_pStat->OnClose();
    }
    if (m_pServer)
    {
        m_pServer->m_Connections.Release(this);
    }
#endif
    if (m_pDataConn)
    {
        m_pDataConn->m_pServer = nullptr;
        m_pDataConn->m_TimerAbor13.Start(1);
    }
    if (m_pPasvServer)
    {
        m_pPasvServer->m_pFtpServerConnection = nullptr;
        m_pPasvServer->m_Timer0.Start(1);
    }
    gLocker.UnLock();
#ifdef __unix__
#ifndef NO_FORK
	exit(EXIT_SUCCESS);
#endif
#endif
}
#ifdef RR_SVC
void CMaaFtpServerConnection::OnSend(int x) noexcept
{
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if (m_pStat && !m_pStat->OnSend(x))
    {
        m_Timer1s.Start(1000000);
    }
}
void CMaaFtpServerConnection::OnRecv(int x) noexcept
{
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if (m_pStat && !m_pStat->OnRecv(x))
    {
        m_Timer1s.Start(1000000);
    }
}
#endif
int CMaaFtpServerConnection::Notify_Accepted(_IP IpFrom, _Port Port)
{
    printf("%s\n", (const char*)CMaaString::sFormat("Accepted connection from %I:%d", IpFrom, Port));
    DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
    m_Step = 1;
    //m_OutBuffer  = "200-MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
    m_OutBuffer = "220 MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
#ifdef RR_SVC
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    if (m_pStat)
    {
        m_pStat->m_Ip = IpFrom;
    }
    gLocker.UnLock();
#endif
    _IP IpL;
    _Port PortL;
    GetConnInfo(&IpL, &PortL, nullptr, nullptr);
    m_ConnectionIpPort.Format("%I:%d", IpL, PortL);
    return eAll;
}
int CMaaFtpServerConnection::Notify_Accepted6(_byte * IpFrom, _Port Port)
{
    printf("%s\n", (const char *)CMaaString::sFormat("Accepted connection from [%J]:%d", IpFrom, Port));
    DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
    m_Step = 1;
    //m_OutBuffer  = "200-MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
    m_OutBuffer = "220 MaaSoftware FTP server v " FTP_SERVER_VERSION " ready.\r\n";
#ifdef RR_SVC
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    if (m_pStat)
    {
        m_pStat->m_Ip6 = IpFrom;
    }
    gLocker.UnLock();
#endif
    _byte IpL[16];
    _Port PortL;
    GetConnInfo6(IpL, &PortL, nullptr, nullptr);
    m_ConnectionIpPort.Format("[%J]:%d", IpL, PortL);
    return eAll;
}
int CMaaFtpServerConnection::Notify_Read()
{
    char Buffer[10 * 1024];
    const int r = RecvData(Buffer, sizeof(Buffer));
#ifdef FTP_DBG
    SetThreadName(-1, "CMaaFtpServerConnection_Thread");
#endif
    if   (IsClosed(r))
    {
        CloseByException("Close");
    }
#ifdef RR_SVC
    OnRecv(r);
#endif
    //int r = RealRcvSndLen(r_);
    //     printf("CMaaFtpServerConnection::Notify_Read(): r = %d\n", r);

    m_InBuffer += CMaaString(Buffer, r);
    while(Process()) ;

    /*
    try
    {
        if (r > 0)
        {
            m_InBuffer += CMaaString(Buffer, RealRcvSndLen(r));
            while(Process()) ;
        }
    }
    catch(XTOOSockErr e)
    {
        throw;
    }
    catch(...)
    {
        if   (IsClosed(r_))
        {
            CloseByException("Close");
        }
        throw;
    }
    if   (IsClosed(r_))
    {
        CloseByException("Close");
    }
    */
    //     printf("CMaaFtpServerConnection::Notify_Read(): while(Process()) ; - done\n");
    const int m = m_OutBuffer.Length() ? eWrite : 0;
    if   (m_InBuffer.Length() > 10 * 1024)
    {
        return m | eDisableRead;
    }
    return m | eRead;
}
void CMaaFtpServerConnection::Notify_Act() noexcept
{
    //ChangeFdModeEx(0);
    const int m = (m_OutBuffer.Length() ? eWrite : eDisableWrite) | (m_InBuffer.Length() > 10 * 1024 ? eDisableRead : eRead);
    ChangeFdModeEx(m);
}
int CMaaFtpServerConnection::Notify_Write()
{
    const int w = SendData(m_OutBuffer, m_OutBuffer.Length());
    //     printf("CMaaFtpServerConnection::Notify_Write(): w = %d\n", w);
    m_OutBuffer = m_OutBuffer.RefMid(w);
#ifdef FTP_DBG
    SetThreadName(-1, "CMaaFtpServerConnection_Thread");
#endif
#ifdef RR_SVC
    OnSend(w);
#endif
    if   (m_OutBuffer.IsEmpty() && m_Step == -1)
    {
        CloseByException("Quit");
    }
    while(Process()) ;
    return m_OutBuffer.Length() ? eWrite : eDisableWrite;
}
static constexpr CMaaConstStr s_cstrUnits[] = { "B/s", "KB/s", "MB/s", "GB/s", "" };
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
            m_TimerTimeOut10.GetWaitForTime(&us, m_pFdSockets ? m_pFdSockets->GetTime() : 0);
            printf("us=%d\n", us);
        }
        CloseByException("OnTimer(0)");
        break;
    case 2:
    case 1:
        {
            CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
            if (m_pDataConn)
            {
                switch (m_TransferMode)
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
            if (f == 1)
            {
                m_Timer1.Stop();
            }
        }
        break;
    case 3:
        m_Timer3.Stop();
        if   (m_DataError)
        {
            if   (m_DataError == 13)
            {
                //printf("Data transfer aborted\n");
                m_OutBuffer.Append("426 Data transfer aborted\r\n");
            }
            else
            {
                printf("Data transfer error %d\n", m_DataError);
                m_OutBuffer.Append("426 Transfer data error %d\r\n", m_DataError);
            }
        }
        else
        {
            const double t = m_TransferTime > 0 ? (double)m_TransferTime / 1000.0 : 0.001;
            double v = (double)m_DataBytesTransferred / t;
            //static const CMaaConstStr s_cstrUnits[] = { "B/s", "KB/s", "MB/s", "GB/s", "" };
            int u;
            for  (u = 0; s_cstrUnits[u + 1].len && v >= 1000.0; u++)
            {
                v /= 1024.0;
            }
            printf("Data transfer complete successfully, speed = %.3lf %s\n", v, s_cstrUnits[u].p);
            m_OutBuffer.Append("226 Data transfer complete successfully, %,D bytes / %,D.%03d sec = %.3lf %s.\r\n", m_DataBytesTransferred, (_qword)(m_TransferTime / 1000), (int)(m_TransferTime % 1000), v, s_cstrUnits[u].p);
        }
        DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        ChangeFdMode(eAll);
        break;
#ifdef RR_SVC
    case 1000:
        {
            CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
            if (m_pStat)
            {
                if (!m_pStat->OnTimer())
                {
                    m_Timer1s.Stop();
                }
            }
        }
        break;
#endif
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
    //return CMaaString::Compare(a->m_FileName, b->m_FileName, 2);
    return CMaaString::Compare(a->m_Fn, b->m_Fn, 2);
}

bool CMaaFtpServerConnection::Process()
{
    if   (m_Step == -1)
    {
        return false;
    }
    const int x = m_InBuffer.Find('\n');
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
    CMaaString txt = m_InBuffer.Left(m_InBuffer[x - 1] == '\r' ? x - 1 : x);
#ifdef BB_SUPPORT
    if (txt.IsLeft("bb ", 3))
    {
	txt = txt.Mid(3).Base64Decode();
	gbb = true;
    }
#endif
    m_InBuffer = m_InBuffer.RefMid(x + 1);

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

    _byte RemoteSrcIp6[16], LocalDstIp6[16];
    _Port RemoteSrcPort6 = 0, LocalDstPort6 = 0;
    {
        memset(RemoteSrcIp6, 0, sizeof(RemoteSrcIp6));
        memset(LocalDstIp6, 0, sizeof(LocalDstIp6));

        if (m_domain == AF_INET) // AF_INET6
        {
            /*
            _IP Ip = 0;
            _Port Port = 0;
            GetConnInfo(&LocalDstIp, &LocalDstPort, &Ip, &Port);
            RemoteSrcIp = Ip;
            RemoteSrcPort = Port;
            */
            GetConnInfo(&LocalDstIp, &LocalDstPort, &RemoteSrcIp, &RemoteSrcPort);
        }
        else
        {
            GetConnInfo6(LocalDstIp6, &LocalDstPort6, RemoteSrcIp6, &RemoteSrcPort6);
        }
        CMaaString src = txt.Left(512);
        CMaaString safe1 = ToFtpSafe(src);
        CMaaString safe2 = ToFtpSafeUtf8(ToFileName(src));
        if   (safe2 != safe1)
        {
            //printf("%I:%d%c%S\n", Ip, Port, '\x0F', &safe2); // 0x0f - ñèìâîë øåñòåð¸íêè
            if (m_domain == AF_INET) // AF_INET6
            {
                printf("%I:%d~%S\n", RemoteSrcIp, RemoteSrcPort, &safe2);
            }
            else
            {
                printf("[%J]:%d~%S\n", RemoteSrcIp6, RemoteSrcPort6, &safe2);
            }
        }
        else
        {
            if (m_domain == AF_INET) // AF_INET6
            {
                printf("%I:%d %S\n", RemoteSrcIp, RemoteSrcPort, &safe1);
            }
            else
            {
                printf("[%J]:%d %S\n", RemoteSrcIp6, RemoteSrcPort6, &safe1);
            }
        }
    }
    CMaaString txtu = txt.ToUpper(0);
    CMaaString txt5u = txtu.RefLeft(5);
#ifdef RR_SVC
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pStat)
        {
            m_pStat->m_Cmd = txt5u != "PASS " ? txt : (txt.RefLeft(5) + "********").s();
        }
    }
#endif
    if   (txtu == "QUIT")
    {
        m_OutBuffer += "221 Goodbye!\r\n";
        m_Step = -1;
        return true;
        //CloseByException("Quit");
    }
    if   (txtu == "HELP")
    {
        DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        m_OutBuffer += "214-Supported commands:\r\n";
        m_OutBuffer += "    USER    PASS    SYST    NOOP    HELP    CWD     CDUP    PWD     XPWD\r\n";
        m_OutBuffer += "    TYPE    REST    PORT    PASV    LIST    NLST    RETR    STOR    SIZE\r\n";
        m_OutBuffer += "    DELE    MKD     XMKD    RMD     XRMD    RNFR    RNTO    MDTM    ABOR\r\n";
        m_OutBuffer += "    APPE    OPTS    FEAT    UTF8    EPRT    EPSV\r\n";
        m_OutBuffer += "214 Developer's e-mail: support@maasoftware.ru\r\n";
        return true;
    }
    if   (txtu == "SITE")
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
        if   (txt5u == "USER ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;

            m_UserName = txt.RefMid(5);
            m_Password.Empty();
            {
#ifndef RR_SVC
                g_Mutex.LockM();
#if 1
                const int ms = s_UpdateCfgForced ? -1 : 0;
                s_UpdateCfgForced = false;
                m_Cfg.UpdateFromFile(g_ConfigFileName, gCon.m_hXmlCache, ms, false/*, nullptr, nullptr, nullptr, CMaaXmlDocument::eDefaultRO*/);
#endif
                CMaaXmlElement e = m_Cfg.DocumentElement();
                g_Mutex.UnLockM();
#else
                CMaaManualMutexLocker1 gLocker(gLock);
                gLocker.Lock();
                CMaaXmlNode e = gConfig.m_Cfg.DocumentElement().FindNode(gCon[CCon::e_FtpServersElement]).FindNodeWithAttr(gCon[CCon::e_FtpServerElement], gCon[CCon::e_FtpServer_aId], CMaaString(m_CfgNum));
                //m_RRUserNode = gConfig.m_Cfg.DocumentElement().FindNode(gCon[CCon::e_UsersListsElement]).FindNodeWithAttr(gCon[CCon::e_UsersListsElm2], gCon[CCon::e_UsersListsElm2_aLogin], m_UserName);
                CMaaXmlNode u = gConfig.m_Cfg.DocumentElement().FindNode(gCon[CCon::e_UsersListsElement]);
                gLocker.UnLock();
#endif
                CMaaXmlNode n0 = e.FindNode(gCon[CCon::e_FtpServerUsers]);
                m_UserNode = n0.FindNodeWithAttr(gCon[CCon::e_FtpServerUser], gCon[CCon::e_FtpServerUser_aName], m_UserName);
                if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aEnabled]) == gCon[CCon::e_False])
                {
                    m_UserNode.Empty();
#ifdef RR_SVC
                    m_RRUserNode.Empty();
                }
                else
                {
                    m_RRUserNode = u.FindNodeWithAttr(gCon[CCon::e_UsersListsElm2], gCon[CCon::e_UsersListsElm2_aLogin], m_UserName);
                    if (m_RRUserNode.FindAttribute(gCon[CCon::e_UsersListsElm2_aDisabled]) == gCon[CCon::e_True])
                    {
                        m_RRUserNode.Empty();
                    }
#endif
                }
            }
            bool bEmptyPass = false;
            CMaaString setuser, setgroup;
            if   (!m_UserNode.IsNull())
            {
                setuser = m_UserNode.FindAttribute("setuser");
                setgroup = m_UserNode.FindAttribute("setgroup");
                if   (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vPlain])
                {
                    bEmptyPass = !m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPass]).Length();
                }
                else if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vHash])
                {
                    CMaaString Pass;
                    CMaaString Salt = m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassSalt]);
                    LongInt2 liSalt(8);
                    if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassHashType]) == gCon[CCon::e_FtpServerUser_aPassHashType_vGBM1] && Import(liSalt, Salt) == 8)
                    {
                        CMaaString Hash(nullptr, 32);
                        ::gGostBsMaa.Hash(nullptr, Pass, Pass.Length(), liSalt(), (char*)(const char*)Hash, 32);
                        bEmptyPass = (Export(Hash) == m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassHash]));
                    }
                }
#ifdef RR_SVC
                else if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vRusRoute])
                {
                    bEmptyPass = CConfig::CheckPassword(m_RRUserNode, CMaaStringZ);
                }
#endif
            }
            if   (!bEmptyPass)
            {
                m_OutBuffer += "331 User name ok, need password\r\n";
                m_Step++;
            }
            else
            {
#ifdef RR_SVC
                {
                    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                    if (m_pStat)
                    {
                        m_pStat->m_User = m_UserName;
                    }
                }
#endif
                m_OutBuffer += "230 User is logged in\r\n";
                m_Step += 2;
                if (LoginLog.IsNotEmpty())
                {
                    try
                    {
                        CMaaString Line;
                        const time_t t = time(nullptr);
                        //CMaaString dt = GetTextDateTime(t, 2);
                        char buff[128];
                        memset(buff, 0, sizeof(buff));
                        tm ttm;
#ifdef __unix__
			localtime_r(&t, &ttm);
#else
			memcpy(&ttm, localtime(&t), sizeof(ttm));
#endif
                        const size_t xsz = strftime(buff, sizeof(buff) - 1, "%b %d %H:%M:%S", &ttm);
                        CMaaString _login = m_UserNode.IsNull() ? CMaaString("unknown") : m_UserName;
                        if (m_domain == AF_INET) // AF_INET6
                        {
                            Line.Format2("%s%I%d%I%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to ::ffff:%4:%5, empty pass\n", buff, RemoteSrcIp, RemoteSrcPort, LocalDstIp, LocalDstPort, &_login);
                        }
                        else
                        {
                            Line.Format2("%s%J%d%J%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to [%4]:%5, empty pass\n", buff, RemoteSrcIp6, RemoteSrcPort6, LocalDstIp6, LocalDstPort6, &_login);
                        }
                        out_print_login_log(Line);
                    }
                    catch(...)
                    {
                    }
                }
            }
        }
        else if (txtu == "OPTS UTF8 ON")
        {
            //DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 OPTS command ok\r\n";
        }
        else
        {
            SayError();
        }
        break;
    case 2:
        if   (txt5u == "PASS ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;

            m_Password = txt.RefMid(5);

            bool bPassOk = false;

            CMaaString setuser, setgroup;
            if   (!m_UserNode.IsNull())
            {
                setuser = m_UserNode.FindAttribute("setuser");
                setgroup = m_UserNode.FindAttribute("setgroup");
                CMaaString Pass = m_Password;
                if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vPlain])
                {
                    bPassOk = m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPass]) == Pass;
                }
                else if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vEmail])
                {
                    bPassOk = Pass.is_a_email(); //Find('@') > 0;
                }
                else if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vHash])
                {
                    CMaaString Salt = m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassSalt]);
                    LongInt2 liSalt(8);
                    if   (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassHashType]) == gCon[CCon::e_FtpServerUser_aPassHashType_vGBM1] && Import(liSalt, Salt) == 8)
                    {
                        CMaaString Hash(nullptr, 32);
                        ::gGostBsMaa.Hash(nullptr, Pass, Pass.Length(), liSalt(), (char *)(const char *)Hash, 32);
                        bPassOk = (Export(Hash) == m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassHash]));
                    }
                }
#ifdef RR_SVC
                else if (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aPassType]) == gCon[CCon::e_FtpServerUser_aPassType_vRusRoute])
                {
                    bPassOk = CConfig::CheckPassword(m_RRUserNode, Pass);
                }
#endif
            }

            //if   (m_Password.IsNotEmpty() && m_Password != "nopassword")
            //if   (m_UserName == "maa" && m_Password == "qwerty")
            if   (bPassOk)
            {
                if (LoginLog.IsNotEmpty())
                {
                    try
                    {
                        CMaaString Line;
                        const time_t t = time(nullptr);
                        //CMaaString dt = GetTextDateTime(t, 2);
                        char buff[128];
                        memset(buff, 0, sizeof(buff));
                        tm ttm;
#ifdef __unix__
			            localtime_r(&t, &ttm);
#else
			            memcpy(&ttm, localtime(&t), sizeof(ttm));
#endif
                        const size_t xsz = strftime(buff, sizeof(buff) - 1, "%b %d %H:%M:%S", &ttm);
                        CMaaString _login = m_UserNode.IsNull() ? CMaaString("unknown") : m_UserName;
                        if (m_domain == AF_INET) // AF_INET6
                        {
                            Line.Format2("%s%I%d%I%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to ::ffff:%4:%5, pass ok\n", buff, RemoteSrcIp, RemoteSrcPort, LocalDstIp, LocalDstPort, &_login);
                        }
                        else
                        {
                            Line.Format2("%s%J%d%J%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to [%4]:%5, pass ok\n", buff, RemoteSrcIp6, RemoteSrcPort6, LocalDstIp6, LocalDstPort6, &_login);
                        }
                        out_print_login_log(Line);
                    }
                    catch(...)
                    {
                    }
                }
            }
            if   (bPassOk && (setuser.IsNotEmpty() || setgroup.IsNotEmpty()))
            {
                int uid = -1;
                int gid = -1;
                if   (setuser.IsNotEmpty())
                {
                    CMaaString All = CMaaFile("/etc/passwd", CMaaFile::eR_SrSw, eNoExcept).ReadAll();
                    while(All.IsNotEmpty())
                    {
                        CMaaString Line = All.GetLine0();
                        CMaaString user = Line.GetWord(':');
                        CMaaString pass = Line.GetWord(':');
                        CMaaString _uid = Line.GetWord(':');
                        CMaaString _gid = Line.GetWord(':');
                        if   (user == setuser)
                        {
                            uid = _uid.ToInt(-1);
                            gid = _gid.ToInt(-1);
                            break;
                        }
                    }
                }
                if   (setgroup.IsNotEmpty())
                {
                    CMaaString All = CMaaFile("/etc/group", CMaaFile::eR_SrSw, eNoExcept).ReadAll();
                    while (All.IsNotEmpty())
                    {
                        CMaaString Line = All.GetLine0();
                        CMaaString group = Line.GetWord(':');
                        CMaaString pass = Line.GetWord(':');
                        CMaaString _gid = Line.GetWord(':');
                        if   (group == setgroup)
                        {
                            gid = _gid.ToInt(gid);
                            break;
                        }
                    }
                }
#ifdef __unix__
                if (gid < 0)
                {
                    bPassOk = false;
                    m_OutBuffer += "530 Bad get gid by name in the system\r\n";
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
                if   (uid < 0)
                {
                    bPassOk = false;
                    m_OutBuffer += "530 Bad get user id in the system\r\n";
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
#ifdef RR_SVC
                {
                    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                    if (m_pStat)
                    {
                        m_pStat->m_User = m_UserName;
                    }
                }
#endif

                if (LoginLog.IsNotEmpty())
                {
                    try
                    {
                        CMaaString Line;
                        const time_t t = time(nullptr);
                        //CMaaString dt = GetTextDateTime(t, 2);
                        char buff[128];
                        memset(buff, 0, sizeof(buff));
                        tm ttm;
#ifdef __unix__
			localtime_r(&t, &ttm);
#else
			memcpy(&ttm, localtime(&t), sizeof(ttm));
#endif
                        const size_t xsz = strftime(buff, sizeof(buff) - 1, "%b %d %H:%M:%S", &ttm);
                        CMaaString _login = m_UserNode.IsNull() ? CMaaString("unknown") : m_UserName;
                        if (m_domain == AF_INET) // AF_INET6
                        {
                            Line.Format2("%s%I%d%I%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to ::ffff:%4:%5, authorized\n", buff, RemoteSrcIp, RemoteSrcPort, LocalDstIp, LocalDstPort, &_login);
                        }
                        else
                        {
                            Line.Format2("%s%J%d%J%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: logged in from %2 [%2] to [%4]:%5, authorized\n", buff, RemoteSrcIp6, RemoteSrcPort6, LocalDstIp6, LocalDstPort6, &_login);
                        }
                        out_print_login_log(Line);
                    }
                    catch(...)
                    {
                    }
                }
            }
            else
            {
                //Jan 06 17:27:22 vm03 proftpd[3018] vm03 (46.159.103.21[46.159.103.21]): USER anonymous: no such user found from 46.159.103.21 [46.159.103.21] to ::ffff:141.101.245.192:3321
                if (FailLog.IsNotEmpty())
                {
                    try
                    {
                        CMaaString Line;
                        const time_t t = time(nullptr);
                        //CMaaString dt = GetTextDateTime(t, 2);
                        char buff[128];
                        memset(buff, 0, sizeof(buff));
                        tm ttm;
#ifdef __unix__
						localtime_r(&t, &ttm);
#else
						memcpy(&ttm, localtime(&t), sizeof(ttm));
#endif
                        const size_t xsz = strftime(buff, sizeof(buff) - 1, "%b %d %H:%M:%S", &ttm);
                        CMaaString _login = m_UserNode.IsNull() ? CMaaString("unknown") : m_UserName;
                        if (m_domain == AF_INET) // AF_INET6
                        {
                            Line.Format2("%s%I%d%I%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: no such user found from %2 [%2] to ::ffff:%4:%5\n", buff, RemoteSrcIp, RemoteSrcPort, LocalDstIp, LocalDstPort, &_login);
                        }
                        else
                        {
                            //GetConnInfo6(LocalDstIp6, &LocalDstPort6, RemoteSrcIp6, &RemoteSrcPort6);
                            //char strL6[64], strR6[64];
                            //CMaaIpToText(strL6, LocalDstIp6);
                            //CMaaIpToText(strR6, RemoteSrcIp6);
                            Line.Format2("%s%J%d%J%d%S", "%1 vm03 proftpd[3018] vm03 (%2[%2]): USER %6: no such user found from %2 [%2] to [%4]:%5\n", buff, RemoteSrcIp6, RemoteSrcPort6, LocalDstIp6, LocalDstPort6, &_login);
                        }
                        CMaaFile f(FailLog, CMaaFile::eACD_SrSw);
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
        if   (txtu == "SYST")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "215 UNIX Type: L8\r\n";
        }
        else if (txtu == "OPTS UTF8 ON")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 OPTS command ok\r\n";
        }
        else if (txtu == "UTF8" || txt5u == "UTF8 ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 OPTS command ok\r\n";
        }
        else if (txtu == "FEAT")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "211-Extensions supported:\r\n";
            //m_OutBuffer += " MLST\r\n";
            m_OutBuffer += " SIZE\r\n";
            m_OutBuffer += " MDTM\r\n";
            m_OutBuffer += " REST\r\n";
            m_OutBuffer += " APPE\r\n";
            m_OutBuffer += " UTF8\r\n";
            m_OutBuffer += "211 END\r\n";
        }
        else if (txtu == "NOOP")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer += "200 NOOP command ok\r\n";
        }
        else if (txt5u.IsLeft("CWD ", 4))
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Dir = ToFileName(txt.RefMid(4));
            /*
               if   (Dir != gCon[CCon::e_FtpServerDir_vfs_path_root] && Dir.LastChar() == '/')
               {
                    Dir -= 1;
                    printf("%s\n", (const char *)Dir);
               }
               */
            CMaaString RealDir, CanonicalDir;
            if   (GetRealAndCanonicalFsName(m_Path, Dir, &RealDir, &CanonicalDir, false))
            {
                m_Path = CanonicalDir;
                m_OutBuffer.Append("250 %S is the current directory\r\n", &m_Path);
            }
            else
            {
                m_OutBuffer += "550 No such directory\r\n";
            }
        }
        else if (txtu == "CDUP")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Dir = "..";
            CMaaString RealDir, CanonicalDir;
            if   (GetRealAndCanonicalFsName(m_Path, Dir, &RealDir, &CanonicalDir, false))
            {
                m_Path = CanonicalDir;
                m_OutBuffer.Append("250 %S is the current directory\r\n", &m_Path);
            }
            else
            {
                m_OutBuffer += "550 No such directory\r\n";
            }
        }
        else if (txtu == "PWD" || txtu == "XPWD")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_OutBuffer.Append("257 \"%S\" is current directory\r\n", &m_Path);
        }
        else if (txt5u == "TYPE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString Type = txt.RefMid(5);
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
        else if (txt5u == "REST ")
        {
            CMaaString Rest = txt.RefMid(5);

            _qword x = -1;
            if   (mysscanf64(Rest, &x) && x >= 0)
            {
                DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
                m_Rest = x;
                m_OutBuffer.Append("350 Restarting at position %D\r\n", m_Rest);
            }
            else
            {
                SayError();
            }
        }
        else if (txt5u == "PORT " && !m_EPSV_ALL)
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv));
            m_TransferMode = -1;

            CMaaString Port = txt.RefMid(5);
            int iPort[6] = {-1, -1, -1, -1, -1, -1};
            Port.dsscanf("%d,%d,%d,%d,%d,%d", &iPort[0], &iPort[1], &iPort[2], &iPort[3], &iPort[4], &iPort[5]);
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
                if   (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aFXP]) != gCon[CCon::e_True])
                {
                    _IP Ip = 0;
                    GetConnInfo(nullptr, nullptr, &Ip, nullptr);
                    if   (Ip != m_PortIp)
                    {
                        //m_OutBuffer += "426 Illegal IP or port range rejected\r\n";
                        m_OutBuffer.Append("426 Illegal IP or port range rejected cip=%I, portip=%I\r\n", Ip, m_PortIp);
                        return true;
                    }
                }
                m_State = (m_State & ~(ePasv)) | ePort;
                m_OutBuffer += "200 PORT command ok\r\n";
            }
        }
        else if (txt5u == "EPRT " && !m_EPSV_ALL)
        {
            // RFC2428
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv));
            m_TransferMode = -1;

            CMaaString Port = txt.RefMid(5);
            const char d = Port[0];
            Port = Port.RefMid(1);
            int pos = Port.Find(d), e = 0;
            if (pos < 0)
            {
                e = 1;
            }
            else
            {
                CMaaString proto = Port.Left(pos);
                Port = Port.RefMid(pos + 1);
                pos = Port.Find(d);
                if (pos < 0)
                {
                    e = 2;
                }
                else
                {
                    CMaaString ip = Port.Left(pos);
                    Port = Port.RefMid(pos + 1);
                    pos = Port.Find(d);
                    if (pos < 0)
                    {
                        e = 3;
                    }
                    else
                    {
                        Port = Port.Left(pos);
                        pos = 0;
                        if (Port.dsscanf("%d", &pos) != 1 || pos <= 0 || pos > 65535)
                        {
                            e = 4;
                        }
                        else if (proto == "1")
                        {
                            m_PortPort = pos;
                            if (CMaaIpToLong(ip, &m_PortIp))
                            {
                                e = 5;
                            }
                            else
                            {
                                if   (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aFXP]) != gCon[CCon::e_True])
                                {
                                    _IP Ip = 0;
                                    if (!GetConnInfo(nullptr, nullptr, &Ip, nullptr, false))
                                    {
                                        e = 6;
                                    }
                                    if   (e || Ip != m_PortIp)
                                    {
                                        //m_OutBuffer += "426 Illegal IP or port range rejected\r\n";
                                        m_OutBuffer.Append("426 Illegal IP or port range rejected cip=%I, portip=%I\r\n", Ip, m_PortIp);
                                        return true;
                                    }
                                }
                                m_State = (m_State & ~(ePasv)) | ePort;
                                m_OutBuffer += "200 EPRT command ok\r\n";
                            }
                        }
                        else if (proto == "2")
                        {
                            m_PortPort = pos;
                            if (CMaaIpToLong6(ip, m_PortIp6))
                            {
                                e = 5;
                            }
                            else
                            {
                                if   (m_UserNode.FindAttribute(gCon[CCon::e_FtpServerUser_aFXP]) != gCon[CCon::e_True])
                                {
                                    _byte Ip[16];
                                    memset(Ip, 0, sizeof(Ip));
                                    if (!GetConnInfo6(nullptr, nullptr, Ip, nullptr, false))
                                    {
                                        e = 6;
                                    }
                                    if   (e || memcmp(Ip, m_PortIp6, 16))
                                    {
                                        //m_OutBuffer += "426 Illegal IP or port range rejected\r\n";
                                        m_OutBuffer.Append("426 Illegal IP or port range rejected cip=%J, eprtip=%J\r\n", Ip, m_PortIp);
                                        return true;
                                    }
                                }
                                m_State = (m_State & ~(ePasv)) | ePrt;
                                m_OutBuffer += "200 EPRT command ok\r\n";
                            }
                        }
                        else
                        {
                            //e = 7;
                            m_OutBuffer.Append("522 Network protocol is unsupported (1,2)\r\n");
                            return true;
                        }
                    }
                }
            }
            if (e)
            {
                m_OutBuffer += "500 EPRT syntax error\r\n";
            }
        }
        else if (txtu == "PASV" && !m_EPSV_ALL)
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv));
            m_TransferMode = -1;
            try
            {
                if   (m_pPasvServer)
                {
                    m_pPasvServer->m_Timer0.Start(1);
                    m_pPasvServer = nullptr;
                }
                m_pPasvServer = new CFtpDataServer(m_pFdSockets, 0, AF_INET, this);
                if   (!m_pPasvServer)
                {
                    throw 1;
                }
                m_PortPort = m_pPasvServer->GetBindedPort();
                GetConnInfo(&m_PortIp, nullptr, nullptr, nullptr);
                m_State = (m_State & ~(ePort)) | ePasv;
            }
            catch(XTOOSockErr err)
            {
                printf("Error: catch(XTOOSockErr): %s\n", err.GetMsg());
                m_OutBuffer.Append("400 Error: %s\r\n", err.GetMsg());
            }
            catch(...)
            {
                printf("Error: catch(...)\n");
                m_OutBuffer.Append("400 Unknown error\r\n");
            }
            if   (m_State & ePasv)
            {
                if (gbb)
        		{
		            m_OutBuffer += CMaaString("bb ") + CMaaString::sFormat("227 Entering passive mode(%d,%d,%d,%d,%d,%d)",
                        (m_PortIp >> 24) & 0xff,  (m_PortIp >> 16) & 0xff, (m_PortIp >> 8) & 0xff, m_PortIp & 0xff,
                        (m_PortPort >> 8) & 0xff, m_PortPort & 0xff).Base64Encode() + "\r\n";
		        }
		        else
		        {
                    m_OutBuffer.Append("227 Entering passive mode(%d,%d,%d,%d,%d,%d)\r\n",
                            (m_PortIp >> 24) & 0xff,  (m_PortIp >> 16) & 0xff, (m_PortIp >> 8) & 0xff, m_PortIp & 0xff,
                            (m_PortPort >> 8) & 0xff, m_PortPort & 0xff);
		        }
            }
        }
        else if (txtu == "EPSV ALL")
        {
            // RFC2428
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv));
            m_TransferMode = -1;
            m_EPSV_ALL = true;
            m_OutBuffer += "200 EPSV command ok\r\n";
        }
        else if (txt5u == "EPSV " || txt5u == "EPSV")
        {
            // RFC2428
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv));
            m_TransferMode = -1;
            CMaaString a;
            if (txt5u == "EPSV ")
            {
                a = txt.RefMid(5);
            }
            try
            {
                if   (m_pPasvServer)
                {
                    m_pPasvServer->m_Timer0.Start(1);
                    m_pPasvServer = nullptr;
                }
                int domain = -1;
                if (a.IsEmpty())
                {
                    domain = GetDomainSock();
                }
                else if (a == "1")
                {
                    domain = AF_INET;
                }
                else if (a == "2")
                {
                    domain = AF_INET6;
                }
                else
                {
                    throw 2;
                }
                //__utf8_printf("new CFtpDataServer(m_pFdSockets, 0, %d, this);\n", domain);
                m_pPasvServer = new CFtpDataServer(m_pFdSockets, 0, domain, this);
                //__utf8_printf("= %p\n", m_pPasvServer);
                if   (!m_pPasvServer)
                {
                    throw 1;
                }
                if (domain == AF_INET)
                {
                    m_PortPort = m_pPasvServer->GetBindedPort();
                    GetConnInfo(&m_PortIp, nullptr, nullptr, nullptr);
                    m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv)) | ePasv;
                }
                else
                {
                    m_PortPort = m_pPasvServer->GetBindedPort();
                    GetConnInfo6(m_PortIp6, nullptr, nullptr, nullptr);
                    m_State = (m_State & ~(ePasv | ePort | ePrt | ePsv)) | ePsv;
                }
                //__utf8_printf("port = %d\n", m_PortPort);
            }
            catch(XTOOSockErr err)
            {
                printf("Error: catch(XTOOSockErr): %s\n", err.GetMsg());
                m_OutBuffer.Append("400 Error: %s\r\n", err.GetMsg());
            }
            catch(int x)
            {
                printf("Error: catch(%d)\n", x);
                m_OutBuffer.Append(x == 2 ? "522 Network protocol not supported error (1,2)\r\n" : "400 Unknown or syntax error\r\n");
            }
            catch(...)
            {
                printf("Error: catch(...)\n");
                m_OutBuffer.Append("400 Unknown or syntax error\r\n");
            }
            if   (m_State & (ePasv | ePsv))
            {
                //__utf8_printf("229 Entering extended passive mode (|||%d|)\n", m_PortPort);
                m_OutBuffer.Append("229 Entering extended passive mode (|||%d|)\r\n", m_PortPort);
            }
        }
        else if (txt5u == "LIST " || txtu == "LIST" || txt5u == "NLST " || txtu == "NLST")
        {
            //printf("%s\n", (const char *)txt);

            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            const bool bNLST = txt5u.IsLeft("NLST", 4);

            CMaaString Mask = txt.RefMid(5);
            if   (Mask.IsLeft("-la ", 4)  || Mask == "-la" || Mask.IsLeft("-al ", 4) || Mask == "-al")
            {
                Mask = Mask.RefMid(4);
            }
            else if (Mask.IsLeft("-a ", 3)  || Mask == "-a")
            {
                Mask = Mask.RefMid(3);
            }
            Mask = ToFileName(Mask);
            if   (Mask.IsEmpty() || Mask == ".")
            {
                Mask = "*.*";
            }
            if   (Mask == gCon[CCon::e_FtpServerDir_vfs_path_root])
            {
                Mask += "*.*";
            }
            //printf("Mask: %S\n", &Mask);
            CMaaString RealFs, CanonicalFs, Perm;
            CMaaString Data;
            CMaaString tmp = Mask.RefRight(3);
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
                    if (RealFs == gCon[CCon::e_FtpServerDir_vfs_path_root])
                    {
                    }
                    */
                    if   (Mask.LastChar() == '/')
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
                const __time64_t CurrentTime = _time64(nullptr);
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
                        CMaaPtrAE_<CMaaFindFile2::sFind, 1> m(100);
                        int N = 0;
                        //printf("...%S\n", &Mask);
                        CMaaFindFile2 ff(Mask, 1);
                        CMaaFindFile2::sFind f;
                        //const char** Month = g_pszEngMonth; // { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };// or international)
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
                                    const int n = CanonicalFs.ReverseFind('/');
                                    CMaaString d = CanonicalFs.Left(n > 0 ? n : 1);
                                    if   (!GetRealAndCanonicalFsName(d, "..", nullptr, nullptr, false, nullptr))
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
                            const int n = txt.ReverseFind('/');
                            if   (n >= 0)
                            {
                                txt = txt.RefMid(n + 1);
                            }
                            m[0].m_Dir = CMaaFile::GetFolderName(Mask);
                            m[0].m_FileName = Mask;//  m[0].m_Dir + szFILESYSTEM_SLASH + txt;
                            m[0].m_Fn = txt;
                        }
                        if   (CanonicalFs.IsRight("/*.*", 4))
                        {
                            CMaaString d0 = CanonicalFs.RemoveFromRight(3);
                            CMaaString d = d0;
#ifdef _WIN32
                            d = d.ToUpper(e_utf8_rus);
#endif
                            for  (CMaaXmlNode n = m_UserNode.FindNode(gCon[CCon::e_FtpServerDir]); !n.IsNull(); n = n.FindNext())
                            {
                                CMaaString Path = n.FindAttribute(gCon[CCon::e_FtpServerDir_vfs_path]);
                                CMaaString x = Path;
#ifdef _WIN32
                                x = x.ToUpper(e_utf8_rus);
#endif
                                if   (x.Length() > d.Length() && x.IsLeft(d))
                                {
                                    int n = Path.Find(d.Length() + 1, '/');
                                    if   (n < 0)
                                    {
                                        n = Path.Length();
                                    }
                                    x = Path.RefMid(d.Length(), n - d.Length());

                                    CMaaString tmp = d0;
                                    if   (tmp.Length() > 1)
                                    {
                                        tmp -= 1;
                                    }
                                    CMaaString d2;
                                    if   (GetRealAndCanonicalFsName(tmp, x, &d2, nullptr, false, nullptr) || GetRealAndCanonicalFsName(tmp, x, &d2, nullptr, true, nullptr))
                                    {
                                        CMaaFindFile2 ff(d2, 1);
                                        CMaaFindFile2::sFind f;
                                        if   (ff.Get(f))
                                        {
                                            int nnn = 0;
                                            for  (; nnn < N; nnn++)
                                            {
                                                if   (m[nnn].m_Fn == x)
                                                {
                                                    break;
                                                }
                                            }
                                            if   (nnn >= N)
                                            {
                                                f.m_FileName = f.m_Dir + szFILESYSTEM_SLASH + x;
                                                f.m_Fn = x;
                                                m[N++] = f;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        //printf("test1\n");
                        CMaaPtr_<CMaaFindFile2::sFind *, 1> mm(N + 1);
                        int i;
                        for  (i = 0; i < N; i++)
                        {
                            mm[i] = &m[i];
                        }
                        qsort(&m[0], N, sizeof(m[0]), DirCompare);

                        //const char** Month = g_pszEngMonth; // { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };// or international)
                        for  (i = 0; i < N; i++)
                        {
                            CMaaFindFile2::sFind &f = *mm[i];

                            CMaaString tmp;
                            tm tt;
                            memset(&tt, 0, sizeof(tt));
                            const tm * t = localtime(&f.m_mTime);
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
                                const int x = stat(f.m_FileName, &buf);
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
                                     g_pszEngMonth[tt.tm_mon], tt.tm_mday,
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
                                             g_pszEngMonth[tt.tm_mon], tt.tm_mday,
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
                                     g_pszEngMonth[tt.tm_mon], tt.tm_mday,
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
            //m_OutBuffer.Append("226 Waiting for data connection\r\n");//,
            //(m_PortIp >> 24) & 0xff,  (m_PortIp >> 16) & 0xff, (m_PortIp >> 8) & 0xff, m_PortIp & 0xff,
            //(m_PortPort >> 8) & 0xff, m_PortPort & 0xff);

            //m_State = (m_State & ~(ePort)) | ePasv;

            m_ListData = Data;
            m_TransferMode = 0;

            if   (m_State & ePort)
            {
                try
                {
                    m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp, m_PortPort, m_ConnectionIpPort);
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
            else if (m_State & ePrt)
            {
                try
                {
                    m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp6, m_PortPort, m_ConnectionIpPort);
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
            else if (m_State & (ePasv | ePsv))
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
                         gLock.UnLockM();
                         return true;
                    }
                    if   (m_pPasvServer)
                    {
                         gLock.UnLockM();
                         return true;
                    }
                    gLock.UnLockM();
                */
            }
            // Error
            m_OutBuffer += "426 Error transferring data.\r\n";
        }
        else if (txt5u == "RETR " || txt5u == "STOR " || txt5u == "APPE ")
        {
            //printf("%s\n", (const char *)txt);

            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            const bool bRecv = txt5u == "RETR ";
            const bool bAppe = txt5u == "APPE ";
            CMaaString FileName = ToFileName(txt.RefMid(5));
            CMaaString Perm, tmp = FileName;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, nullptr, true, &Perm))
            {
                m_OutBuffer += "426 dir not found.\r\n";
                return true;
            }
            //bool bSkipExisted = false;
            //const char* pMode = nullptr;
            CMaaFile::eMode fMode = CMaaFile::eNoMode;
            if   (bRecv)
            {
                if   (Perm.Find(" R+") < 0)
                {
                    CMaaString tmp2 = ToFtpSafe(tmp);
                    m_OutBuffer.Append("426 you have no permissions to read the file %S.\r\n", &tmp2);
                    return true;
                }
                //pMode = "R|Sr";
                fMode = CMaaFile::eR_Sr;
            }
            else if (bAppe)
            {
                if (Perm.Find(" A+") < 0)
                {
                    CMaaString tmp2 = ToFtpSafe(tmp);
                    m_OutBuffer.Append("426 you have no permissions to append to the file %S.\r\n", &tmp2);
                    return true;
                }
                if (m_Rest == 0)
                {
                    fMode = Perm.Find(" C+") >= 0 ? CMaaFile::eAC_SrSw : CMaaFile::eA_SrSw;
                    //bSkipExisted = false;// (m_Rest == 0 && Perm.Find(" D+") < 0);
                }
                else
                {
                    fMode = Perm.Find(" C+") >= 0 ? CMaaFile::eAC_SrSw : CMaaFile::eA_SrSw;
                }
            }
            else
            {
                if   (Perm.Find(" W+") < 0)
                {
                    CMaaString tmp2 = ToFtpSafe(tmp);
                    m_OutBuffer.Append("426 you have no permissions to write the file %S.\r\n", &tmp2);
                    return true;
                }
                if   (m_Rest == 0)
                {
                    //pMode = Perm.Find(" C+") >= 0 ? "WC|SwSr" : "W|SwSr";
                    fMode = Perm.Find(" C+") >= 0 ? CMaaFile::eWC_SrSw : CMaaFile::eW_SrSw;
                    if   (Perm.Find(" D+") < 0)
                    {
                        //pMode = Perm.Find(" C+") >= 0 ? "WCN|SwSr" : "WN|SwSr"; // prev file was created always on D- && C-
                        //pMode = Perm.Find(" C+") >= 0 ? "WCN|SwSr" : nullptr;
                        fMode = Perm.Find(" C+") >= 0 ? CMaaFile::eWCN_SrSw : CMaaFile::eNoMode;
                    }
                    //bSkipExisted = (m_Rest == 0 && Perm.Find(" D+") < 0);
                }
                else
                {
                    //pMode = Perm.Find(" C+") >= 0 ? "AC|SwSr" : "A|SwSr";
                    fMode = Perm.Find(" C+") >= 0 ? CMaaFile::eAC_SrSw : CMaaFile::eA_SrSw;
                }
            }
            if (fMode == CMaaFile::eNoMode) // if (!pMode)
            {
                CMaaString tmp2 = ToFtpSafe(tmp);
                m_OutBuffer.Append("426 you have no access to a file %S.\r\n", &tmp2);
                return true;
            }
            //FileName = CMaaString("C:\\Ftp\\") + m_Path + "/" + FileName;
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   ((m_State & (ePort | ePasv | ePrt | ePsv)) == 0)
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
                    CMaaString tmpMode = CMaaFile::GetMode(fMode);
                    printf("opening %S, %S\n", &FileName, &tmpMode);
                    m_File = CMaaFile(FileName, fMode);
                    if   ((!bAppe || m_Rest) && !m_File.Seek(m_Rest))
                    {
                        printf("seek failed\n");
                        throw 1;
                    }
                    m_TransferMode = bRecv ? 1 : 2;
                }
                catch(CMaaString txt)
                {
                    m_OutBuffer.Append("426 Error: %S\r\n", &txt);
                    return true;
                }
                catch(XTOOFile2Error err)
                {
                    m_OutBuffer.Append("426 Error: %s\r\n", err.GetMsg());
                    return true;
                }
                catch(...)
                {
                    m_OutBuffer.Append("426 unknown error\r\n");
                    return true;
                }
                m_OutBuffer += "150 Opening BINARY mode data connection for transferring file.\r\n";

                m_Rest = 0;

                if   (m_State & ePort)
                {
                    try
                    {
                        m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp, m_PortPort, m_ConnectionIpPort);
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
                else if (m_State & ePrt)
                {
                    try
                    {
                        m_pDataConn = new CFtpServerData(m_pFdSockets, this, m_PortIp6, m_PortPort, m_ConnectionIpPort);
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
                else if (m_State & (ePasv | ePsv))
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
                              gLock.UnLockM();
                              return true;
                         }
                         if   (m_pPasvServer)
                         {
                              gLock.UnLockM();
                              return true;
                         }
                         gLock.UnLockM();
                    */
                }
                // Error
                m_OutBuffer += "426 Error transferring data.\r\n";
            }
        }
        else if (txtu == "ABOR" || (txtu.RefLeft(7).Find("ABOR") > 0 && txt[0] != ' ' && txt[1] != ' ' && txt[2] != ' ')) // == "\xFF\xF4""ABOR", etc))
        {
            bool bByTimer = false;
            {
                CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                if (m_pDataConn)
                {
                    bByTimer = true;
                    m_pDataConn->m_TimerAbor13.Start(1);
                }
                if (m_pPasvServer)
                {
                    m_pPasvServer->m_Timer0.Start(1);
                }
            }
            if   (!bByTimer)
            {
                m_OutBuffer.Append("226 ABOR command complete.\r\n");
            }
            m_State &= ~(ePort | ePasv);
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
        }
        else if (txt5u == "SIZE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: can't change dir\r\n", &tmp2);
                return true;
            }
            //FileName = CMaaString("C:\\Ftp\\") + m_Path + FileName;
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DL+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to list directory for file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                const _qword x = CMaaFile::Length(FileName);
                m_OutBuffer.Append("213 %D\r\n", x);
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: unknown error\r\n", &tmp2);
            }
            return true;
        }
        else if (txt5u == "MDTM ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(5));
            CMaaString cName = FileName;
            const int n = FileName.Find(' ');
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
                    FileName = FileName.RefMid(n + 1);
                    dt.RefLeft(4).dsscanf("%d", &x);
                    tt.tm_year = x - 1900;
                    x = 1;
                    dt.RefMid(4, 2).dsscanf("%d", &x);
                    tt.tm_mon = x - 1;
                    x = 1;
                    dt.RefMid(6, 2).dsscanf("%d", &x);
                    tt.tm_mday = x;
                    x = 0;
                    dt.RefMid(8, 2).dsscanf("%d", &x);
                    tt.tm_hour = x;
                    x = 0;
                    dt.RefMid(10, 2).dsscanf("%d", &x);
                    tt.tm_min = x;
                    x = 0;
                    dt.RefMid(12, 2).dsscanf("%d", &x);
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
                        m_OutBuffer.Append("550 error converting datetime \"%S\"\r\n", &tmp2);
                        return true;
                    }
                    CMaaString Perm;
                    if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 no file named \"%S\"\r\n", &tmp2);
                        return true;
                    }
                    FileName = CMaaFile::MkCompatible(FileName);
                    if   (Perm.Find(" W+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 you have no permissions to write file %S.\r\n", &tmp2);
                        return true;
                    }
                    try
                    {
                        CMaaFile::SetDateTimeEx(FileName, t, 0, true);
                        t = CMaaFile::GetDateTime(FileName, nullptr, true);

                        tm tt;
                        memset(&tt, 0, sizeof(tt));
                        const tm * p = gmtime(&t);
                        if   (!p)
                        {
                            throw 1;
                        }
                        memcpy(&tt, p, sizeof(tt));
                        m_OutBuffer.Append("213 %04d%02d%02d%02d%02d%02d.%03d\r\n",
                             tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, 0);
                    }
                    catch(XTOOError err)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, err.GetMsg());
                    }
                    catch(...)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 unknown error for setting datetime \"%S\"\r\n", &tmp2);
                    }
                    return true;
                }
            }
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 no file named \"%S\"\r\n", &tmp2);
                return true;
            }
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DL+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to list directory of the file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                const time_t t = CMaaFile::GetDateTime(FileName, nullptr, true);
                tm tt;
                memset(&tt, 0, sizeof(tt));
                tm * p = gmtime(&t);
                if   (!p)
                {
                    throw 1;
                }
                memcpy(&tt, p, sizeof(tt));
                m_OutBuffer.Append("213 %04d%02d%02d%02d%02d%02d.%03d\r\n",
                     tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, 0);
            }
            catch(XTOOError err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 unknown error for gettting datetime \"%S\"\r\n", &tmp2);
            }
            return true;
        }
        else if (txtu.IsLeft("SITE CHMOD ", 11)) // SITE CHMOD 664 email.txt--
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = txt.RefMid(11).RemoveSpaces();
            //ToFileName(txt.RefMid(11));
            CMaaString Mode = FileName.GetWord();
            FileName = ToFileName(FileName.RemoveSpaces());
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 no file named \"%S\"\r\n", &tmp2);
                return true;
            }
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" CHMOD+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to write file %S.\r\n", &tmp2);
                return true;
            }
            int mode = 0;
            Mode.dsscanf("%o", &mode);
            printf("chmod %S oct: %o\n", &FileName, mode);
            try
            {
#ifdef __unix__
                const int x = chmod(FileName, (mode_t)mode);
#else
                const int x = -1;
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
                m_OutBuffer.Append("200 CHMOD command successful.\r\n");
                printf("ok\n");
            }
            catch(XTOOError err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 unknown error for setting datetime \"%S\"\r\n", &tmp2);
            }
            return true;
        }
        else if (txt5u == "DELE ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "426 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" D+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to delete file %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                CMaaFile::RemoveEx(FileName);
                m_OutBuffer.Append("250 DELE command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, err.GetMsg());
            }
            catch(...)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: unknown error\r\n", &tmp2);
            }
            return true;
        }
        else if (txt5u.IsLeft("MKD ", 4) || txt5u == "XMKD ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(txt5u[0] == 'X' ? 5 : 4));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DC+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to create directory %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                //printf("%s\n", (const char *)UnicodeToAnsi(Utf8ToUnicode(FileName)));
                printf("%S\n", &FileName);
                CMaaFile::MkDir(FileName, false, true);
                printf("ok\n");
                m_OutBuffer.Append("257 MKD/XMKD command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                printf("error: %s\n", err.GetMsg());
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, (const char *)err.GetMsg());
            }
            return true;
        }
        else if (txt5u.IsLeft("RMD ", 4) || txt5u == "XRMD ")
        {
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(txt5u[0] == 'X' ? 5 : 4));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, false, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   (Perm.Find(" DD+") < 0)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 you have no permissions to delete directory %S.\r\n", &tmp2);
                return true;
            }
            try
            {
                CMaaFile::RmDir(FileName, true);
                m_OutBuffer.Append("250 RMD/XRMD command successful\r\n");
            }
            catch(XTOOFile2Error err)
            {
                CMaaString tmp2 = ToFtpSafe(cName);
                m_OutBuffer.Append("550 %S: %s\r\n", &tmp2, (const char *)err.GetMsg());
            }
            return true;
        }
        else if (txt5u == "RNFR " || txt5u == "RNTO ")
        {
            if   (txt5u == "RNTO " && !m_CmdHistory[1].IsLeftCi("RNFR ", 5, 0))
            {
                m_OutBuffer += "503 Bad sequence of commands.\r\n";
                return true;
            }
            //printf("%s\n", (const char *)txt);
            DEF_m_TimerTimeOut10_Start_TIME_OUT_1;
            CMaaString FileName = ToFileName(txt.RefMid(5));
            CMaaString cName = FileName;
            CMaaString Perm;
            if   (!GetRealAndCanonicalFsName(m_Path, FileName, &FileName, &cName, true, &Perm))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //FileName.ReplaceNN('/', '\\');
            FileName = CMaaFile::MkCompatible(FileName);
            if   (txt5u == "RNFR ")
            {
                if   (CMaaFile::IsADir(FileName) || CMaaFile::IsAFile(FileName))
                {
                    if   (CMaaFile::IsAFile(FileName) && Perm.Find(" Ren+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 you have no permissions to rename the file %S.\r\n", &tmp2);
                        return true;
                    }
                    if   (CMaaFile::IsADir(FileName) && Perm.Find(" DRen+") < 0)
                    {
                        CMaaString tmp2 = ToFtpSafe(cName);
                        m_OutBuffer.Append("550 you have no permissions to rename the directory %S.\r\n", &tmp2);
                        return true;
                    }
                    m_OutBuffer += "350 File or directory exists, ready for destination name\r\n";
                }
                else
                {
                    CMaaString tmp2 = ToFtpSafe(cName);
                    m_OutBuffer.Append("550 %S: No such file or directory.\r\n", &tmp2);
                }
                return true;
            }

            CMaaString From = ToFileName(m_CmdHistory[1].RefMid(5));
            if   (!GetRealAndCanonicalFsName(m_Path, From, &From, nullptr, true))
            {
                m_OutBuffer += "550 dir not found.\r\n";
                return true;
            }
            //From.ReplaceNN('/', '\\');
            From = CMaaFile::MkCompatible(From);
            try
            {
                CMaaFile::Rename(From, FileName, true);
                m_OutBuffer += "250 RNTO command successful.\r\n";
            }
            catch(XTOOError err)//XTOOFile2Error
            {
                m_OutBuffer.Append("550 RNTO error: %s.\r\n", (const char *)err.GetMsg());
            }
            return true;
        }
        else
        {
            if   (m_OutBuffer.Length() < 5 * 1024)
            {
                m_OutBuffer.Append("500 Command not understood: ");
                for  (int i = 0; i < txt.Length() && i < 100; i++)
                {
                    const char c = txt[i];
                    if   (c > ' ' && c < 0x7f)
                    {
                        m_OutBuffer += c;
                    }
                    else
                    {
                        char ttt[20];
                        sprintf(ttt, "%%%02X", (int)(unsigned char)c);
                        m_OutBuffer += ttt;
                    }
                }
                m_OutBuffer += "\r\n";
                //m_OutBuffer.Append("500 Command not understood: %S%s\r\n", &ToFtpSafe(txt.Left(32)), txt.Length() > 32 ? "..." : "");
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

    FsName.ReplaceNN('\\', '/');
    //FsName = CMaaFile::MkCompatible(FsName);

    //printf("GetRealAndCanonicalFsName(%s, %s,,, %s)\n", (const char *)CurrentPath, (const char *)FsName, bFile ? "true" : "false");

    CMaaString OneFileName;
    if   (bFile)
    {
        const int n = FsName.ReverseFind('/');
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
            OneFileName = FsName.RefMid(n + 1);
            FsName = FsName.Left(n);
            if   (FsName.IsEmpty())
            {
                FsName = "/";
            }
        }
        if   (OneFileName.IsEmpty() || OneFileName == "." || OneFileName == "..")
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
        CMaaXmlNode n = m_UserNode.FindNodeWithAttr(gCon[CCon::e_FtpServerDir], gCon[CCon::e_FtpServerDir_vfs_path], gCon[CCon::e_FtpServerDir_vfs_path_root]);
        if   (n.IsNull())
        {
            return false;
        }
        RealRootDir = n.FindAttribute(gCon[CCon::e_FtpServerDir_real_path]);
        if   (!CMaaFile::IsADir(RealRootDir))
        {
            return false;
        }
    }
    int a, b;
    CMaaString RealDir = RealRootDir, cDir = gCon[CCon::e_FtpServerDir_vfs_path_root], Path;
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
        CMaaXmlNode n = m_UserNode.FindNodeWithAttr(gCon[CCon::e_FtpServerDir], gCon[CCon::e_FtpServerDir_vfs_path], cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute(gCon[CCon::e_FtpServerDir_real_path]);
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
        //Path.ReplaceNN('/', '\\');
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

        CMaaXmlNode n = m_UserNode.FindNodeWithAttr(gCon[CCon::e_FtpServerDir], gCon[CCon::e_FtpServerDir_vfs_path], cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute(gCon[CCon::e_FtpServerDir_real_path]);
        }

        Path = RealDir;
        //Path.ReplaceNN('/', '\\');
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
        Path = CMaaFile::MkCompatible(RealDir == "/" ? RealDir : (RealDir + "/").s());
#ifdef _WIN32
        Path = Path.ToUpper(e_utf8_rus);
#endif
        int x = 0;
        for  (CMaaXmlNode n = m_UserNode.FindNode(gCon[CCon::e_FtpServerDirPerm]); !n.IsNull(); n = n.FindNext())
        {
            tmp = n.FindAttribute(gCon[CCon::e_FtpServerDirPerm_path]);
            if   (tmp != "/")
            {
                tmp += "/";
            }
            tmp = CMaaFile::MkCompatible(tmp);
#ifdef _WIN32
            tmp = tmp.ToUpper(e_utf8_rus);
#endif
            if   (tmp.Length() <= Path.Length() && Path.IsLeft(tmp))
            {
                //printf("tmp = %S, Path = %S\n", &tmp, &Path);
                if   (x < (int)tmp.Length())
                {
                    Perm = n.FindAttribute(gCon[CCon::e_FtpServerDirPerm_perm]);
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

        CMaaXmlNode n = m_UserNode.FindNodeWithAttr(gCon[CCon::e_FtpServerDir], gCon[CCon::e_FtpServerDir_vfs_path], cDir);
        if   (!n.IsNull())
        {
            RealDir = n.FindAttribute(gCon[CCon::e_FtpServerDir_real_path]);
        }
        /*
          Path = RealDir;
          //Path.ReplaceNN('/', '\\');
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
CFtpServerData::CFtpServerData(CMaaFdSockets * pFdSockets, int domain, CMaaFtpServerConnection * pServer) // accept
:   CMaaTcpSocket(pFdSockets, domain),
    m_ConnTxt("Ftp-Data"),
    //m_File(nullptr),
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
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        m_pServer = pServer;
        m_pServer->m_pDataConn = this;
    }
    //AddFdSocket(); //???

#ifdef __SHAPERS
    //gLock_lib.LockM();
    gpSockStartup&& gpSockStartup->m_SysShaperMutex.LockM();
    if   (gpSockStartup)
    {
#if 0
    // IN TOOLSLIB:
    ///1024000 B/s (1000 KB/s) speed limit
    ///m_Snd1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 1024000);
    ///m_Rcv1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 1024000);
    /// 10 B/s (0.01 KB/s) speed limit by default
    //m_Snd1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 10);
    //m_Rcv1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 10);
#endif
        gpSockStartup->m_ShaperThread.m_Snd1000->Add(m_SndLLShaper);
        gpSockStartup->m_ShaperThread.m_Rcv1000->Add(m_RcvLLShaper);
    }
    gpSockStartup&& gpSockStartup->m_SysShaperMutex.UnLockM();
    //gLock_lib.UnLockM();
    //m_SndLLShaper.SetConnectionLimits(10000, -1);
    //m_RcvLLShaper.SetConnectionLimits(10000, -1);
#endif
#ifdef FTP_DBG
    rrlog("||| CFtpServerData::CFtpServerData(a)\n");
#endif
}

CFtpServerData::CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _IP Ip, _Port Port, CMaaString strServerIpPort)
:   CMaaTcpSocket(pFdSockets),
    m_ConnTxt("Ftp-Data"),
    //m_File(nullptr),
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
    ChangeFdModeEx(eAll); //rem
    if   (strServerIpPort.IsNotEmpty() && strServerIpPort.Find(':') > 0)
    {
        _IP BindIp = 0;
        int Port = 0;
        CMaaIpToLongEx(strServerIpPort, &BindIp, ":");
        //strIpPort = strIpPort.Mid(trailer_pos > 0 ? trailer_pos + 1: 1);
        //strIpPort.dsscanf("%d", &Port);
        if   (BindIp && 
#ifdef _WIN32
            0
#else
            1*0
#endif
            )
        {
            int x = -1;
            try
            {
                x = Bind(0, BindIp, true);
            }
            catch(...)
            {
            }
            printf("Bind(0, %I) returns %d\n", BindIp, x);
            for  (int nn = 0; nn < 100 && x < 0; nn++)
            {
                int portn = 0;
                GetRnd(&portn, (int)sizeof(portn));
                portn = ((unsigned)portn % 50000U) + 14000;
                printf("binding (%d, %I)...\n", portn, BindIp);
                try
                {
                    x = Bind(portn, BindIp, true);
                }
                catch(...)
                {
                }
                if   (x >= 0)
                {
                    printf("Bind(%d, %I) returns %d\n", portn, BindIp, x);
                }
            }
        }
    }

#ifdef __SHAPERS
    //gLock_lib.LockM();
    gpSockStartup&& gpSockStartup->m_SysShaperMutex.LockM();
    if   (gpSockStartup)
    {
#if 0
    // IN TOOLSLIB:
    ///1024000 B/s (1000 KB/s) speed limit
    ///m_Snd1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 1024000);
    ///m_Rcv1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 1024000);
    /// 10 B/s (0.01 KB/s) speed limit by default
    //m_Snd1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 10);
    //m_Rcv1000 = TL_NEW CMaaTcpShaper("102400", 10.0, 10);
#endif
        gpSockStartup->m_ShaperThread.m_Snd1000->Add(m_SndLLShaper);
        gpSockStartup->m_ShaperThread.m_Rcv1000->Add(m_RcvLLShaper);
    }
    gpSockStartup&& gpSockStartup->m_SysShaperMutex.UnLockM();
    //gLock_lib.UnLockM();
    //m_SndLLShaper.SetConnectionLimits(10000, -1);
    //m_RcvLLShaper.SetConnectionLimits(10000, -1);
#endif

    AsyncConnect(Ip, Port);
#ifdef FTP_DBG
    rrlog("||| CFtpServerData::CFtpServerData(c4)\n");
#endif
}

CFtpServerData::CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _byte * Ip, _Port Port, CMaaString strServerIpPort)
:   CMaaTcpSocket(pFdSockets, AF_INET6),
    m_ConnTxt("Ftp-Data"),
    //m_File(nullptr),
    m_Timer0(this, 0),
    m_Timer1(this, 1),
    m_TimerTimeOut10(this, 10),
    m_TimerAbor13(this, 13)
{
    printf("CFtpServerData::CFtpServerData(%J, %d, %S)...\n", Ip, Port, &strServerIpPort);
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
    ChangeFdModeEx(eAll); //rem
    if   (strServerIpPort.IsNotEmpty() && strServerIpPort.Find(':') > 0)
    {
        _byte BindIp[16];
        memset(BindIp, 0, sizeof(BindIp));
        int Port = 0;
        CMaaIpToLongEx(strServerIpPort, BindIp, ":");
        //strIpPort = strIpPort.Mid(trailer_pos > 0 ? trailer_pos + 1: 1);
        //strIpPort.dsscanf("%d", &Port);
        if   (BindIp && 
#ifdef _WIN32
            0
#else
            1*0
#endif
            )
        {
            int x = -1;
            try
            {
                x = Bind6(0, BindIp, true);
            }
            catch(...)
            {
            }
            printf("Bind(0, %J) returns %d\n", BindIp, x);
            for  (int nn = 0; nn < 100 && x < 0; nn++)
            {
                int portn = 0;
                GetRnd(&portn, (int)sizeof(portn));
                portn = ((unsigned)portn % 50000U) + 14000;
                printf("binding (%d, %J)...\n", portn, BindIp);
                try
                {
                    x = Bind6(portn, BindIp, true);
                }
                catch(...)
                {
                }
                if   (x >= 0)
                {
                    printf("Bind(%d, %J) returns %d\n", portn, BindIp, x);
                }
            }
        }
    }
    AsyncConnect6(Ip, Port);
#ifdef FTP_DBG
    rrlog("||| CFtpServerData::CFtpServerData(c6)\n");
#endif
}
CFtpServerData::~CFtpServerData()
{
    if (m_Mode == 2)
    {
        FlushRecv(false);
    }
#ifdef FTP_DBG
    rrlog("||| CFtpServerData::~CFtpServerData()\n"); //fflush(stdout);
#endif
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if   (m_pServer && m_pServer->m_pDataConn == this)
    {
        m_pServer->m_pDataConn = nullptr;
        m_pServer->m_DataError = m_Error;
        m_pServer->m_DataBytesTransferred = m_BytesTransferred;
        m_pServer->m_TransferTime = GetTickCount() - m_Time0;
        //printf("m_pServer->m_Timer3.Start(1);\n");
        m_pServer->m_Timer3.Start(1);
    }
}

#ifdef TL_EPOLLET
//#define FTP_MULTI_READ
#endif

int CFtpServerData::Notify_Read()
{
    //printf("R\n");fflush(stdout);
    if   (m_Mode != 2)
    {
        return eDisableRead;
    }
#ifdef FTP_MULTI_READ
    while(1)
    {
#endif
    const int r = RecvData(m_Buffer + m_BufferSize, sizeof(m_Buffer) - m_BufferSize);
#ifdef FTP_DBG
    SetThreadName(-1, "CFtpServerData_Thread");
#endif
    if   (IsClosed(r))
    {
        FlushRecv(true);
        m_Error = 0;
        CloseByException("Close");
    }
#ifdef FTP_MULTI_READ
    if (!r)
    {
        break;
    }
#endif
#ifdef RR_SVC
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer)
        {
            m_pServer->OnRecv(r);
        }
    }
#endif

    m_BufferSize += r;

    //int r = RealRcvSndLen(r_);
    //     printf("CFtpServerData::Notify_Read(): r = %d\n", r);
    //printf("(%d)\n", r);fflush(stdout);
    //printf("point1\n");fflush(stdout);
    m_TimerTimeOut10.Start(TIME_OUT_1, false);

    //printf("point2\n");fflush(stdout);
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        //printf("point3\n");fflush(stdout);
        //int us1=-1, us2=-1;
        if (m_pServer)
        {
            //us1=1000000000, us2=1000000000;
            ////printf("point4\n");fflush(stdout);
            //m_pServer->m_TimerTimeOut10.GetWaitForTime(&us1, m_pFdSockets->GetTime());
            //m_pServer->m_TimerTimeOut10.Start();
            DEF_SRV_m_TimerTimeOut10_Start_TIME_OUT_1;
            //m_pServer->m_TimerTimeOut10.GetWaitForTime(&us2, m_pFdSockets->GetTime());
        }
        //printf("point5\n");fflush(stdout);
    }
    //printf("point6\n");fflush(stdout);
    //printf("us1=%d, us2=%d\n", us1, us2);

    if (m_BufferSize >= (int)sizeof(m_Buffer) / 2)
    {
        try
        {
            //printf("point7\n");fflush(stdout);
            //int x = m_BufferSize;
            const int x = m_File.Write(m_Buffer, m_BufferSize);
            //printf("file write %d\n", x);fflush(stdout);
            if   (x > 0)
            {
                m_BytesTransferred += x;
            }
            if   (x != m_BufferSize)
            {
                m_Error = 2; // file write error
                CloseByException("file write error");
            }
            m_BufferSize = 0;
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
    }
    /*
    if   (IsClosed(r_))
    {
        //          printf("CFtpServerData::Notify_Read(): IsClosed(%d) == true\n", r);
        //printf("closing\n");fflush(stdout);
        m_Error = 0;
        CloseByException("File recv complete");
    }
    */
#ifdef FTP_MULTI_READ
    }
#endif
    return 0;//eRead;
}
void CFtpServerData::FlushRecv(bool bThrow)
{
    if (m_File.IsOpen() && m_BufferSize > 0)
    {
        try
        {
            //int x = m_BufferSize;
            const int x = m_File.Write(m_Buffer, m_BufferSize);
            if   (x > 0)
            {
                m_BytesTransferred += x;
            }
            if   (x != m_BufferSize)
            {
                m_Error = 2; // file write error
                if (bThrow)
                {
                    CloseByException("file write error");
                }
                return;
            }
            m_BufferSize = 0;
        }
        catch(XTOOFile2Error err)
        {
            m_Error = 2; // file write error
            if   (bThrow)
            {
                CloseByException("file write error");
            }
        }
    }
}
int CFtpServerData::Notify_Write()
{
    if   (m_Mode != 0 && m_Mode != 1)
    {
        return eDisableWrite;
    }
    m_TimerTimeOut10.Start(TIME_OUT_1, false);

#ifdef FTP_DBG
    SetThreadName(-1, "CFtpServerData_Thread");
#endif

    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer)
        {
            //m_pServer->m_TimerTimeOut10.Start();
            DEF_SRV_m_TimerTimeOut10_Start_TIME_OUT_1;
        }
    }

    if   (m_Mode == 0)
    {
        int w;
        do
        {
            w = SendData(m_DataTxtPos + (const char *)m_DataTxt, m_DataTxt.Length() - m_DataTxtPos);
            //               printf("CFtpServerData::Notify_Write(): w = %d\n", w);
#ifdef RR_SVC
            {
                CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                if (m_pServer)
                {
                    m_pServer->OnSend(w);
                }
            }
#endif
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
            //bool bFlush = false;
            if (sizeof(m_Buffer) - m_BufferSize >= sizeof(m_Buffer) / 4)
            {
                try
                {
                    const int x = m_File.Read(m_Buffer + m_BufferSize, sizeof(m_Buffer) - m_BufferSize);
                    if   (x > 0)
                    {
                        m_BufferSize += x;
                    }
                    //bFlush = (x <= 0);
                }
                catch(XTOOFile2Error err)
                {
                    if   (m_BufferSize == 0)
                    {
                        m_Error = 1; // file read error
                        CloseByException("file read error");
                    }
                }
            }
            if   (m_BufferSize == m_BufferPos)
            {
                m_Error = 0;
                CloseByException("Send file complete");
            }
            w = SendData(&m_Buffer[m_BufferPos], m_BufferSize - m_BufferPos);
            //               printf("CFtpServerData::Notify_Write(): w = %d\n", w);
#ifdef RR_SVC
            {
                CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                if (m_pServer)
                {
                    m_pServer->OnSend(w);
                }
            }
#endif
            m_BufferPos += w;
            m_BytesTransferred += w;
            if   (m_BufferPos >= (int)sizeof(m_Buffer) / 2)
            {
                memmove(m_Buffer, m_Buffer + m_BufferPos, m_BufferSize -= m_BufferPos);
                m_BufferPos = 0;
            }

        }    while(w > 0);
        m_TimerTimeOut10.Start(TIME_OUT_1, false);
        return 0;//eWrite;
    }
    return 0;
}
int CFtpServerData::Notify_Error()
{
    {
        XTOOSockErr e("CFtpServerData::Notify_Error():", nullptr);
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
    CloseByException("");
    return 0;
}
int CFtpServerData::Notify_Accepted(_IP IpFrom, _Port Port)
{
    printf("accepted from %I:%d\n", IpFrom, Port); fflush(stdout);
    m_Time0 = GetTickCount();
    bool b = false;
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer && m_pServer->m_pDataConn == this)
        {
            m_pServer->SetPeerAccepted(IpFrom, Port, true);
            b = true;
        }
    }
    if   (!b)
    {
        printf("  No server to data connection pointer. throw 1"); fflush(stdout);
        throw 1;
        //CloseByException("No server to data connection pointer");
    }
    //printf("CFtpServerData::Notify_Accepted(_IP IpFrom, _Port Port)\n");
    return eDisableRead|eDisableWrite|eExept;
}
int CFtpServerData::Notify_Accepted6(_byte * IpFrom, _Port Port)
{
    printf("accepted from %J:%d\n", IpFrom, Port); fflush(stdout);
    m_Time0 = GetTickCount();
    bool b = false;
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer && m_pServer->m_pDataConn == this)
        {
            m_pServer->SetPeerAccepted(IpFrom, Port, true);
            b = true;
        }
    }
    if   (!b)
    {
        printf("  No server to data connection pointer. throw 1"); fflush(stdout);
        throw 1;
        //CloseByException("No server to data connection pointer");
    }
    //printf("CFtpServerData::Notify_Accepted(_IP IpFrom, _Port Port)\n");
    return eDisableRead|eDisableWrite|eExept;
}
int CFtpServerData::Notify_Connected(_IP Ip, _Port Port, const char * DnsName)
{
    printf("connected to %I:%d\n", Ip, Port); fflush(stdout);

    m_Time0 = GetTickCount();
    bool b = false;
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer && m_pServer->m_pDataConn == this)
        {
            m_pServer->SetPeerAccepted(Ip, Port, false);
            b = true;
        }
    }
    if   (!b)
    {
        CloseByException("No server to data connection pointer");
    }
    return eAll;
}
int CFtpServerData::Notify_Connected6(_byte * Ip, _Port Port, const char * DnsName)
{
    printf("connected to %J:%d\n", Ip, Port); fflush(stdout);

    m_Time0 = GetTickCount();
    bool b = false;
    {
        CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
        if (m_pServer && m_pServer->m_pDataConn == this)
        {
            m_pServer->SetPeerAccepted(Ip, Port, false);
            b = true;
        }
    }
    if   (!b)
    {
        CloseByException("No server to data connection pointer");
    }
    return eAll;
}
void CFtpServerData::OnTimer(int f)
{
    //printf("CFtpServerData::OnTimer(%d):\n", f); fflush(stdout);

    switch(f)
    {
    case 10:
        //printf("CFtpServerData::OnTimer(%d)\n", f);
        printf("Data transfer %d s time out\n", TIME_OUT_1 / 1000000);
        m_Error = 3; // time out
        CMaa_fallthrough;
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
CFtpDataServer::CFtpDataServer(CMaaFdSockets * pFdSockets, int Port, int domain, CMaaFtpServerConnection * pFtpServerConnection)
:   CMaaUnivServer(pFdSockets, Port, domain, "FTP Data Server"),
    m_Timer0(this, 0),
    m_pFtpServerConnection(pFtpServerConnection)
{
#ifdef FTP_DBG
    rrlog("||| CFtpDataServer::CFtpDataServer()\n");
#endif
    m_Timer0.Attach(pFdSockets);
}
//---------------------------------------------------------------------------
CFtpDataServer::~CFtpDataServer()
{
#ifdef FTP_DBG
    rrlog("||| CFtpDataServer::~CFtpDataServer()\n");
#endif
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if   (m_pFtpServerConnection && m_pFtpServerConnection->m_pPasvServer == this)
    {
        m_pFtpServerConnection->m_pPasvServer = nullptr;
    }
}
//---------------------------------------------------------------------------
CMaaTcpSocket * CFtpDataServer::CreateNewConnection(CMaaFdSockets * pFdSockets)
{
#ifdef FTP_DBG
    SetThreadName(-1, "CFtpDataServer_Thread");
#endif
    m_Timer0.Start(1);
    return new CFtpServerData(pFdSockets, GetDomainSock(), m_pFtpServerConnection);
}
//---------------------------------------------------------------------------
void CFtpDataServer::OnTimer(int f)
{
    //     printf("CFtpDataServer::OnTimer(): f = %d\n", f);
    CloseByException("OnTimer(0)");
}
//---------------------------------------------------------------------------

//int BlockThrSigs();

#ifdef __linux__
#include <signal.h>

static void OnMySIGCHLD(int sig)
{
#if 1
    signal(SIGCHLD, OnMySIGCHLD);
    //return;
    pid_t pid;
    int status;

    /* EEEEXTEERMINAAATE! */
printf("OnMySIGCHLD() - 1\n");
    while((pid = waitpid(-1, &status, 0/*WNOHANG*/)) >= 0)
    {
        printf ("waitpid() = %d\n", (int)pid);
        if   (pid > 0 && WIFEXITED(status))
        {
            printf("(%d is exited)\n", (int)pid);
        }
    }
    int e = errno;
    printf ("waitpid() = %d, e=%d, %s\n", (int)pid, e, strerror(e));
printf("OnMySIGCHLD() - 2\n");

//    signal(SIGCHLD, OnMySIGCHLD);
printf("OnMySIGCHLD() - 3\n");

#else

printf("OnMySIGCHLD() - 1\n");

        int wstat;
        // union wait wstat;
        pid_t   pid;

        while (1)
        {
            pid = wait3(&wstat, WNOHANG, (struct rusage *)NULL);
            if (pid == 0)
            {
                printf ("wait3() = 0\n");
                return;
            }
            else if (pid == -1)
            {
                printf ("wait3() = -1\n");
                return;
            }
            else
            {
                printf ("status = %d\n", wstat);
                if (WIFEXITED(wstat))
                {
                    printf("(%d is exited)\n", (int)pid);
                }
                //printf ("Return code: %d\n", wstat.w_retcode);
            }
        }
#endif
}

#endif

#ifndef RR_SVC
int main(int argn, char * args[])
{
    CMaaSetConsoleUtf8AndLocale ConsoleInitializer;
    //signal(SIGHUP, OnMySIGHUP);

#ifdef __linux__
    //signal(SIGCHLD, OnMySIGCHLD);
#endif

    //BlockThrSigs();

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
            __utf8_printf("Platform: ");
#ifdef _WIN64
            __utf8_printf("WIN64 ");
#else
#ifdef _WIN32
            __utf8_printf("WIN32 ");
#endif
#endif
#ifdef __unix__
            if   (sizeof(void *) == 8)
            {
                __utf8_printf("Unix 64 bits ");
            }
            else
            {
                __utf8_printf("Unix 32 bits ");
            }
#ifdef TL_EPOLL
#ifdef TL_EPOLLET
            __utf8_printf("EPOLLET ");
#else
            __utf8_printf("EPOLL ");
#endif
#endif
#endif
#ifdef __SHAPERS
            __utf8_printf("SHAPERS ");
#endif
#ifdef NO_FORK
            __utf8_printf("NO_FORK ");
#endif
            __utf8_printf("version\n\n");
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
        //s_FileTime = CMaaFile::GetFileTime64(g_ConfigFileName, false);
        //s_FileSize = CMaaFile::Length(g_ConfigFileName, false);
        //s_LastUpdatedTime = time(nullptr);

        //const char * pszConfigFileName = g_ConfigFileName; //"FtpServerCfg.xml";
        //CMaaFile f(pszConfigFileName, CMaaFile::eR);
        //CMaaString txt = f.Read();

        //CMaaXmlDocument doc("FtpServer");
        CMaaString errorMsg;
        int errorLine, errorColumn;
        //if   (!doc.SetContent(txt, &errorMsg, &errorLine, &errorColumn, CMaaXmlDocument::eMainRefString2))
        //if (!doc.SetContent(txt, gCon.m_hXmlCache, &errorMsg, &errorLine, &errorColumn, CMaaXmlDocument::eDefaultRO))
        if (!m_Cfg.UpdateFromFile(g_ConfigFileName, -1, true, &errorMsg, &errorLine, &errorColumn))
        {
            CMaaString txt;
            txt.Format2("%S%d%d%s", TR("Error in config file \"%1\"\nLine: %2, Column: %3, Error: %4"), &g_ConfigFileName, errorLine, errorColumn, (const char *)errorMsg);
            throw txt;
        }
        //m_Cfg = doc;
        CMaaXmlElement e = m_Cfg.DocumentElement();
        FailLog = e.FindAttribute(gCon[CCon::e_FtpServer_aFailLog]);
        LoginLog = e.FindAttribute(gCon[CCon::e_FtpServer_aLoginLog]);
        gLogLock = e.FindAttribute(gCon[CCon::e_FtpServer_aLogLock]);
        if (gLogLock.IsEmpty())
        {
            gLogLock = CMaaFile::GetCurrentDirectory() + szFILESYSTEM_SLASH + "log.lk";
        }
        CMaaString tmp = e.FindAttribute(gCon[CCon::e_FtpServer_aPort]);
        if   (tmp.IsNotEmpty())
        {
            port = tmp;
        }
        //e.FindAttribute(gCon[CCon::e_FtpServer_aPort]).dsscanf("%d", &port);
        tmp = e.FindAttribute(gCon[CCon::e_FtpServer_aLog]);
        if (tmp.IsNotEmpty())
        {
            gLog = CMaaFile(tmp, CMaaFile::eAC_SrSw, eNoExcept);
            if (gLog.IsOpen())
            {
                gLog.SetCloseOnExec(false);
            }
        }
        if (LoginLog.IsNotEmpty())
        {
            gLoginLog = CMaaFile(LoginLog, CMaaFile::eAC_SrSw, eNoExcept);
            if (gLoginLog.IsOpen())
            {
                gLoginLog.SetCloseOnExec(false);
            }
        }
        bOk = true;
    }
    catch(XTOOFile2Error e)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        CMaaString txt;
        //txt.Format2("%s", "Error reading config file: %1", (const char *)e.GetOemMsg());
        txt.Format2("%s", "Error reading config file: %1", e.GetMsg());
        //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
        printf("%s\n", (const char *)txt);
    }
    catch(CMaaString txt)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
        printf("%S\n", &txt);
    }
    catch(...)
    {
        //CProtocolListColor c(CProtocolListColor::eRed);
        CMaaString txt;
        txt.Format2("", "Error loading config file");
        //MessageBox(nullptr, txt, "FtpServerConfig", MB_OK);
        printf("%s\n", (const char *)txt);
    }
    if   (!bOk)
    {
        return 2;
    }


    if   (argn > 1)
    {
        CMaaString tmp = args[1];//e.FindAttribute(gCon[CCon::e_FtpServer_aPort]);
        if   (tmp.IsNotEmpty())
        {
            port = tmp;
        }
        //dsscanf(args[1], "%d", &port);
        //printf("Using port %d\n", port);
        printf("Using port %S\n", &port);
    }
    try
    {
        CMaaSockStartup st;
        gpSockStartup = &st;
        CMaaSockThread thr(nullptr);
        CMaaFtpServer * s = new CMaaFtpServer(thr.m_pFdSockets, port);
        thr.Create();
#ifdef _WIN32
        thr.Wait(INFINITE);
#else
        while(!gbChild)
        {
            //gLock.LockM();
            int c = gChildren.load();
            //gLock.UnLockM();
            if   (c)
            {
                int status = 0;
                pid_t pid = //wait(&status);
                    waitpid(-1, &status, WNOHANG);
                int e = (int)errno;
                if   (pid == -1)
                {
                    printf("wait() returns %d\n", (int)pid);
                    printf("errno = %d (%s)\n", e, strerror(e));
                    //perror("error: ");
                    usleep(1000000);
                }
                else if (pid == 0)
                {
                    // no chlds changed status
                    //printf("errno = %d (%s)\n", e, strerror(e));
                    //perror("error: ");
                    usleep(1000000);
                }
                else
                {
                    printf("wait() returns pid=%d\n", (int)pid);
                    if   (WIFEXITED(status))
                    {
                        printf("(%d is exited)\n", (int)pid);
                        {
                            CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
                            if (!ghChildrenPids.Remove(pid))
                            {
                                c = --gChildren;
                            }
                            /*
                            int c = 0;
                            for  (int i = 0; i < gChildren; i++)
                            {
                                if   (ChildrenPids[i] != pid)
                                {
                                    ChildrenPids[c++] = ChildrenPids[i];
                                }
                            }
                            gChildren = c;
                            */
                        }
                        printf("children left: %d\n", c);
                    }
                }
            }
            else
            {
                usleep(1000000);
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
    gpSockStartup = nullptr;
    return 0;
}
#endif

/*
chdir(const char *);
getcwd(char *, int);
chroot(const char *);
*/

#ifdef RR_SVC
int CMaaFtpServer::DeleteUpdateFtpServers(bool bDeleteAll) noexcept
{
    int N = 0;
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    CMaaUnivHash<int, CMaaFtpServer*>::iterator it(ghFtpServers);
    for (; it; ++it)
    {
        CMaaFtpServer* p = it.data();
        if (bDeleteAll)
        {
            p->m_Timer0.Start(1);
            N++;
        }
        else
        {
            CMaaXmlNode e = gConfig.m_Cfg.DocumentElement().FindNode(gCon[CCon::e_FtpServersElement]).FindNodeWithAttr(gCon[CCon::e_FtpServerElement], gCon[CCon::e_FtpServer_aId], CMaaString(p->m_CfgNum));
            if (!e || e.FindAttribute(gCon[CCon::e_FtpServer_aPort]) != p->m_IpPort || e.FindAttribute(gCon[CCon::e_FtpServer_aEnabled]) != gCon[CCon::e_True])
            {
                p->m_Timer0.Start(1);
                N++;
            }
        }
    }
    return N;
}
int CMaaFtpServer::CreateFtpServers(CMaaSockThread * pThread) noexcept
{
    int N = 0;
    CMaaManualMutexLocker1 gLocker(gLock);
    gLocker.Lock();
    CMaaXmlNode e = gConfig.m_Cfg.DocumentElement().FindNode(gCon[CCon::e_FtpServersElement]);
    gLocker.UnLock();
    
    CMaaXmlNode n = e.FindNode(gCon[CCon::e_FtpServerElement]);
    for (; n; ++n)
    {
        if (n.FindAttribute(gCon[CCon::e_FtpServer_aEnabled]) != gCon[CCon::e_True])
        {
            continue;
        }
        const int CfgNum = n.FindAttribute(gCon[CCon::e_FtpServer_aId]).ToInt(0);
        CMaaString IpPort = n.FindAttribute(gCon[CCon::e_FtpServer_aPort]);
        gLocker.Lock();
        const int x = ghFtpServers.Find(CfgNum);
        gLocker.UnLock();
        if (x)
        {
            CMaaSockThread* t = nullptr;
            CMaaFtpServer* p = nullptr;
            try
            {
                t = RR_NEW CMaaSockThread(pThread);
                if (!t)
                {
                    throw 1;
                }
                p = RR_NEW CMaaFtpServer(t->m_pFdSockets, IpPort, CfgNum);
                if (!p || !t->Create())
                {
                    throw 2;
                }
                N++;
//#ifdef RR_SVC
                printf("FtpServer#%d on port %S is ready...\n", CfgNum, &IpPort);
//#endif
            }
            catch (...)
            {
                delete p;
                delete t;
                printf("Error creating FtpServer#%d on port %S\n", CfgNum, &IpPort);
            }
        }
    }
    return N;
}
int CMaaFtpServer::KillConnections(int CfgNum, int ConnNum, CMaaString UserName) noexcept
{
    int N = 0;
    CMaaAtomicFastMutexLocker agLocker(gLock); // automatic scope locker
    if (UserName.IsNotEmpty() || CfgNum == -1)
    {
        CMaaUnivHash<int, CMaaFtpServer*>::iterator it(ghFtpServers);
        for (; it; ++it)
        {
            CMaaFtpServer* p = it.data();
            CMaaFtpServerConnection* c;
            for (c = p->m_Connections.LookAtFront(); c; c = p->m_Connections.Next(c))
            {
                if (c->m_pStat && (CfgNum == -1 || c->m_pStat->m_User == UserName))
                {
                    c->m_Timer0.Start(1);
                    N++;
                }
            }
        }
    }
    CMaaFtpServer* p = ghFtpServers[CfgNum, nullptr];
    if (p)
    {
        CMaaFtpServerConnection* c;
        for (c = p->m_Connections.LookAtFront(); c; c = p->m_Connections.Next(c))
        {
            if (c->m_pStat && (ConnNum == -1 || c->m_pStat->m_Num == ConnNum))
            {
                c->m_Timer0.Start(1);
                N++;
            }
        }
    }
    return N;
}

CMaaUnivHash<int, SFtpServerStat*> g_hFtpServerStat;

SFtpConnStat::SFtpConnStat(SFtpServerStat* pServerStat) noexcept
{
    m_pServerStat = pServerStat;
    m_Num = pServerStat->m_LastConnNum++;
    m_Send = m_Recv = m_Send0 = m_Recv0 = m_Sent = m_Received = 0;
    m_dwTicks = GetTickCount();
    pServerStat->m_ActiveConn.AddAtBack(this);
    m_pServerStat->m_NConnections++;
#ifdef FTP_DBG
    rrlog("||| SFtpConnStat::SFtpConnStat()\n");
#endif
}
SFtpConnStat::~SFtpConnStat()
{
    CMaaDList<SFtpConnStat>::Release(this);
#ifdef FTP_DBG
    rrlog("||| SFtpConnStat::~SFtpConnStat()\n");
#endif
}
DEF_ALLOCATOR(SFtpConnStat)

bool SFtpConnStat::OnSend(int x) noexcept
{
    const bool bRet = m_Send0 || m_Recv0;
    m_Send0 += x;
    m_Sent += x;
    m_dwTicks = GetTickCount();
    m_pServerStat->OnSend(x);
    return bRet;
}
bool SFtpConnStat::OnRecv(int x) noexcept
{
    const bool bRet = m_Send0 || m_Recv0;
    m_Recv0 += x;
    m_Received += x;
    m_dwTicks = GetTickCount();
    m_pServerStat->OnRecv(x);
    return bRet;
}
bool SFtpConnStat::OnTimer() noexcept
{
    m_Send = m_Send0;
    m_Recv = m_Recv0;
    m_Send0 = m_Recv0 = 0;
    return m_Send || m_Recv;
}
void SFtpConnStat::OnClose() noexcept
{
    m_dwTicks = GetTickCount();
    CMaaDList<SFtpConnStat>::Release(this);
    m_pServerStat->m_NConnections--;
    m_pServerStat->m_ClosedConn.AddAtBack(this);
}

SFtpServerStat::SFtpServerStat() noexcept
{
    m_CfgNum = -3;
    m_LastConnNum = m_NServers = m_NConnections = 0;
    m_pServer = nullptr;
    m_Send = m_Recv = m_Send0 = m_Recv0 = m_Sent = m_Received = 0;
    m_dwTicks = GetTickCount();
#ifdef FTP_DBG
    rrlog("||| SFtpServerStat::SFtpServerStat()\n");
#endif
}
SFtpServerStat::~SFtpServerStat()
{
    SFtpConnStat* p;
    while ((p = m_ClosedConn.LookAtFront()))
    {
        delete p;
    }
    while ((p = m_ActiveConn.LookAtFront()))
    {
        delete p;
    }
#ifdef FTP_DBG
    rrlog("||| SFtpServerStat::~SFtpServerStat()\n");
#endif
}
void SFtpServerStat::SetServer(int CfgNum, CMaaFtpServer* pServer)
{
    m_CfgNum = CfgNum;
    m_NServers++;
    m_pServer = pServer;
    m_Send0 = m_Recv0 = 0;
    m_IpPort = pServer->GetIpPort();
    m_dwTicks = GetTickCount();
    g_hFtpServerStat.AddOver(CfgNum, this);
}
void SFtpServerStat::UnSetServer(CMaaFtpServer* pServer) noexcept
{
    m_NServers--;
    if (m_pServer == pServer)
    {
        m_dwTicks = GetTickCount();
        m_pServer = nullptr;
    }
    m_Send = m_Recv = 0;
}
void SFtpServerStat::OnSend(int x) noexcept
{
    m_Send0 += x;
    m_Sent += x;
    m_dwTicks = GetTickCount();
}
void SFtpServerStat::OnRecv(int x) noexcept
{
    m_Recv0 += x;
    m_Received += x;
    m_dwTicks = GetTickCount();
}
void SFtpServerStat::OnTimer() noexcept
{
    m_Send = m_Send0;
    m_Recv = m_Recv0;
    m_Send0 = m_Recv0 = 0;
    Clean1();
}
void SFtpServerStat::Clean1() noexcept
{
    _dword dwTicks = GetTickCount();
    SFtpConnStat *p;
    while ((p = m_ClosedConn.LookAtFront()))
    {
        if ((_sdword)(dwTicks - p->m_dwTicks) < 15000)
        {
            break;
        }
        m_ClosedConn.Release(p);
        delete p;
    }
}
#endif
