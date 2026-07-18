
#ifndef RR_SVC

#include "version_.h"
#include "Constants.h"

extern CMaaXmlDocument m_Cfg;

#define TR(x) x

#endif

class CMaaFtpServerConnection;
class CMaaFtpServer;

#ifdef RR_SVC
struct SFtpServerStat;
struct SFtpConnStat : public CMaaDLink
{
    SFtpServerStat* m_pServerStat;
    int m_Num;
    //CMaaFtpServerConnection* m_pConnection;
    _qword m_Send, m_Recv, m_Send0, m_Recv0;
    _qword m_Sent, m_Received;
    _dword m_dwTicks;
    _IP m_Ip = 0;
    _IP6 m_Ip6;
    CMaaString m_User, m_Cmd;

    SFtpConnStat(SFtpServerStat* pServerStat) noexcept;
    ~SFtpConnStat();
    bool OnSend(int x) noexcept;
    bool OnRecv(int x) noexcept;
    bool OnTimer() noexcept;
    void OnClose() noexcept;

    ADD_ALLOCATOR(SFtpConnStat)
};
struct SFtpServerStat
{
    int m_CfgNum, m_LastConnNum;
    int m_NServers;
    int m_NConnections;
    CMaaFtpServer* m_pServer;
    _qword m_Send, m_Recv, m_Send0, m_Recv0;
    _qword m_Sent, m_Received;
    _dword m_dwTicks;
    CMaaDList<SFtpConnStat> m_ActiveConn, m_ClosedConn;
    CMaaString m_IpPort;
    SFtpServerStat() noexcept;
    ~SFtpServerStat();
    void SetServer(int CfgNum, CMaaFtpServer* pServer);
    void UnSetServer(CMaaFtpServer* pServer) noexcept;
    void OnSend(int x) noexcept;
    void OnRecv(int x) noexcept;
    void OnTimer() noexcept;
    void Clean1() noexcept;
};
extern CMaaUnivHash<int, SFtpServerStat*> g_hFtpServerStat;
#endif

class CFtpServerData : public CMaaTcpSocket
{
public:
    CMaaFtpServerConnection* m_pServer;
private:
     _qword m_BytesTransferred;
     int m_Error;
     _dword m_Time0;

     int m_Mode; // 0 - Send m_DataTxt, 1 - Send file, 2 - Recv file
     CMaaString m_ConnTxt;

     CMaaString m_DataTxt;
     int m_DataTxtPos;
     CMaaFile m_File;
     char m_Buffer[256 * 1024];
     int m_BufferPos, m_BufferSize;
     
//     CMaaString 

     CMaaSockTimerT<CFtpServerData> m_Timer0, m_Timer1, m_TimerTimeOut10, m_TimerFlush14, m_TimerFlush15;
public:
     CMaaSockTimerT<CFtpServerData> m_TimerAbor13;

     CFtpServerData(CMaaFdSockets * pFdSockets, int domain, CMaaFtpServerConnection * pServer); // accept
     CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _IP Ip, _Port Port, CMaaString strServerIpPort); // connect
     CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _byte * Ip, _Port Port, CMaaString strServerIpPort); // connect
     ~CFtpServerData();

     void SetDataTxt(CMaaString txt) noexcept
     {
          m_Mode = 0;
          m_DataTxt = txt;
          m_DataTxtPos = m_BufferSize = 0;
          m_Timer1.StartExt(1);
          //printf("SetDataTxt():\n%s\n", (const char *)m_DataTxt);
     }
     void SetSendFile(CMaaFile f) noexcept
     {
          m_Mode = 1;
          m_File = f;
          m_BufferPos = m_BufferSize = 0;
          m_Timer1.StartExt(1);
     }
     void SetRecvFile(CMaaFile f) noexcept
     {
          m_Mode = 2;
          m_File = f;
          //m_BufferPos = 0;
          m_BufferPos = m_BufferSize = 0;
          m_Timer1.StartExt(1);
     }

     int Notify_Read() override;
     int Notify_Write() override;
     int Notify_Error() override;
     //void Notify_Act() noexcept override;
     int Notify_Accepted(_IP IpFrom, _Port Port) override;
     int Notify_Accepted6(_byte * IpFrom, _Port Port) override;
     int Notify_Connected(_IP Ip, _Port Port, const char * DnsName) override;
     int Notify_Connected6(_byte * Ip, _Port Port, const char * DnsName) override;
     CMaaString GetConnectionName() noexcept override {return m_ConnTxt;}
     void OnTimer(int f);
     //int CloseByException(const char *msg);
     void FlushRecv(bool bThrow);
};
//---------------------------------------------------------------------------
class CFtpDataServer : public CMaaUnivServer
{
     CMaaTcpSocket * CreateNewConnection(CMaaFdSockets * pFdSockets);
public:
     CMaaSockTimerT<CFtpDataServer> m_Timer0;
     CMaaFtpServerConnection * m_pFtpServerConnection;

     CFtpDataServer(CMaaFdSockets * pFdSockets, int Port, int domain, CMaaFtpServerConnection * pFtpServerConnection);
     ~CFtpDataServer();
     void OnTimer(int f);
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class CMaaFtpServerConnection : public CMaaTcpSocket, public CMaaDLink
{
#ifdef RR_SVC
public:
    CMaaFtpServer* m_pServer;
    SFtpConnStat* m_pStat;
private:
#endif
    static int
#ifdef _WIN32
     __cdecl
#endif
     DirCompare(const void * p1, const void * p2);

     CMaaString m_ConnName;
     int m_State;
     enum
     {
          ePort = 0x01,
          ePasv = 0x02,
          ePrt  = 0x04,
          ePsv  = 0x08
     };
     int m_Step;
     CMaaString m_OutBuffer, m_InBuffer;
     CMaaString m_UserName, m_Password, m_Path, m_FileName;
     CMaaString m_ServerIpPort, m_ConnectionIpPort;
     int m_TryN;
     char m_Type;
     _qword m_Rest;
     _IP m_PortIp;
     _byte m_PortIp6[16];
     _Port m_PortPort;
     bool m_EPSV_ALL;
     bool Process();
     void SayError();
     _IP m_PeerIp;
     _byte m_PeerIp6[16];
     _Port m_PeerPort;
     bool m_PeerAccepted;
     CMaaString m_ListData;
     int m_TransferMode;
     CMaaFile m_File;
     CMaaString m_CmdHistory[2];
protected:
     bool gbb;
public:
#ifdef RR_SVC
     int m_CfgNum;
#endif
     CMaaXmlNode m_UserNode;
#ifdef RR_SVC
     CMaaXmlNode m_RRUserNode;
#endif
     int m_DataError;
     _qword m_DataBytesTransferred;
     _dword m_TransferTime;
     CMaaSockTimerT<CMaaFtpServerConnection> m_Timer0, m_Timer1/*, m_Timer2*/, m_Timer3, m_TimerTimeOut10, m_TimerAcceptTimeOut11, m_Timer1s;
     CFtpServerData * m_pDataConn;
     CFtpDataServer * m_pPasvServer;
     void SetPeerAccepted(_IP Ip, _Port Port, bool bAccepted = true) noexcept
     {
          m_PeerIp = Ip;
          m_PeerPort = Port;
          m_PeerAccepted = bAccepted;
          m_Timer1.StartExt(1);
     }
     void SetPeerAccepted(_byte * Ip, _Port Port, bool bAccepted = true) noexcept
     {
          memcpy(m_PeerIp6, Ip, 16);
          m_PeerIp = 0;
          m_PeerPort = Port;
          m_PeerAccepted = bAccepted;
          m_Timer1.StartExt(1);
     }

     CMaaString GetConnectionName() noexcept override {return m_ConnName;}
#ifndef RR_SVC
     CMaaFtpServerConnection(CMaaFdSockets* pFdSockets, CMaaString ServerIpPort, const char* ClassName);
#else
     CMaaFtpServerConnection(CMaaFdSockets* pFdSockets, CMaaString ServerIpPort, int CfgNum, CMaaFtpServer* pServer, const char* ClassName);
     void OnSend(int x) noexcept;
     void OnRecv(int x) noexcept;
#endif
     ~CMaaFtpServerConnection();
     int Notify_Accepted(_IP IpFrom, _Port Port) override;
     int Notify_Accepted6(_byte * IpFrom, _Port Port) override;
     int Notify_Read() override;
     int Notify_Write() override;
     void Notify_Act() noexcept override;
     void OnTimer(int f);
     bool GetRealAndCanonicalFsName(CMaaString CurrentPath, CMaaString FsName, CMaaString *RealDir, CMaaString *CanonicalDir, bool bFile, CMaaString *pPermissions = nullptr);
};
//---------------------------------------------------------------------------
class CMaaFtpServer : public CMaaUnivServer
{
     CMaaString m_ServName;
     CMaaString m_IpPort;
     CMaaTcpSocket * CreateNewConnection(CMaaFdSockets * pFdSockets);

     CMaaSockTimerT<CMaaFtpServer> m_Timer0, m_Timer1s;

public:
#ifndef RR_SVC
     CMaaFtpServer(CMaaFdSockets* pFdSockets, CMaaString Port, const char* ServerName = "FTP server");
#else
     SFtpServerStat* m_pStat;
     int m_CfgNum;
     CMaaFtpServer(CMaaFdSockets* pFdSockets, CMaaString Port, int CfgNum, const char* ServerName = "FTP server");
     CMaaDList<CMaaFtpServerConnection> m_Connections;
     CMaaString GetIpPort() const noexcept { return m_IpPort; }
#endif
     ~CMaaFtpServer();
     int Notify_Error();
     void OnTimer(int f);
#ifdef RR_SVC
     static int DeleteUpdateFtpServers(bool bDeleteAll) noexcept;
     static int CreateFtpServers(CMaaSockThread* pThread) noexcept;
     static int KillConnections(int CfgNum, int ConnNum, CMaaString UserName) noexcept;
#endif
};
//---------------------------------------------------------------------------

#ifndef RR_SVC
void CheckLicense();
#else
extern CMaaUnivHash<int, CMaaFtpServer*> ghFtpServers;
#endif
