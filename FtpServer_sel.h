#include "version_.h"

extern CMaaXmlDocument m_Cfg;

#define TR(x) x

class CMaaFtpServerConnection;

class CFtpServerData : public CMaaTcpSocket
{
public:
private:

     CMaaFtpServerConnection * m_pServer;

     _qword m_BytesTransferred;
     int m_Error;
     _dword m_Time0;

     int m_Mode; // 0 - Send m_DataTxt, 1 - Send file, 2 - Recv file
     CMaaString m_ConnTxt;

     CMaaString m_DataTxt;
     int m_DataTxtPos;
     CMaaFile m_File;
     char m_Buffer[64 * 1024];
     int m_BufferPos, m_BufferSize;
     
//     CMaaString 

     CMaaSockTimerT<CFtpServerData> m_Timer0, m_Timer1, m_TimerTimeOut10;
public:
     CMaaSockTimerT<CFtpServerData> m_TimerAbor13;

     CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer); // accept
     CFtpServerData(CMaaFdSockets * pFdSockets, CMaaFtpServerConnection * pServer, _IP Ip, _Port Port, CMaaString strServerIpPort); // connect
     ~CFtpServerData();

     void SetDataTxt(CMaaString txt)
     {
          m_Mode = 0;
          m_DataTxt = txt;
          m_DataTxtPos = m_BufferSize = 0;
          m_Timer1.Start(1);
          //printf("SetDataTxt():\n%s\n", (const char *)m_DataTxt);
     }
     void SetSendFile(CMaaFile f)
     {
          m_Mode = 1;
          m_File = f;
          m_BufferPos = m_BufferSize = 0;
          m_Timer1.Start(1);
     }
     void SetRecvFile(CMaaFile f)
     {
          m_Mode = 2;
          m_File = f;
          m_BufferPos = 0;
          m_Timer1.Start(1);
     }

     int Notify_Read();
     int Notify_Write();
     int Notify_Error();
     int Notify_Accepted(_IP IpFrom, _Port Port);
     int Notify_Connected(_IP Ip, _Port Port, const char * DnsName);
     const char * GetConnectionName () {return m_ConnTxt;}
     void OnTimer(int f);
     //int CloseByException(const char *msg);
};
//---------------------------------------------------------------------------
class CFtpDataServer : public CMaaUnivServer
{
     CMaaTcpSocket * CreateNewConnection(CMaaFdSockets * pFdSockets);
public:
     CMaaSockTimerT<CFtpDataServer> m_Timer0;
     CMaaFtpServerConnection * m_pFtpServerConnection;

     CFtpDataServer(CMaaFdSockets * pFdSockets, int Port, CMaaFtpServerConnection * pFtpServerConnection);
     ~CFtpDataServer();
     void OnTimer(int f);
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class CMaaFtpServerConnection : public CMaaTcpSocket
{
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
          ePasv = 0x02
     };
     int m_Step;
     CMaaString m_OutBuffer, m_InBuffer;
     CMaaString m_UserName, m_Password, m_Path, m_FileName;
     CMaaString m_ServerIpPort;
     int m_TryN;
     char m_Type;
     _qword m_Rest;
     _IP m_PortIp;
     _Port m_PortPort;
     bool Process();
     void SayError();
     _IP m_PeerIp;
     _Port m_PeerPort;
     bool m_PeerAccepted;
     CMaaString m_ListData;
     int m_TransferMode;
     CMaaFile m_File;
     CMaaString m_CmdHistory[2];
public:
     CMaaXmlNode m_UserNode;
     int m_DataError;
     _qword m_DataBytesTransferred;
     _dword m_TransferTime;
     CMaaSockTimerT<CMaaFtpServerConnection> m_Timer0, m_Timer1/*, m_Timer2*/, m_Timer3, m_TimerTimeOut10, m_TimerAcceptTimeOut11;
     CFtpServerData * m_pDataConn;
     CFtpDataServer * m_pPasvServer;
     void SetPeerAccepted(_IP Ip, _Port Port, bool bAccepted = true)
     {
          m_PeerIp = Ip;
          m_PeerPort = Port;
          m_PeerAccepted = bAccepted;
          m_Timer1.Start(1);
     }

     const char * GetConnectionName() {return m_ConnName;}
     CMaaFtpServerConnection(CMaaFdSockets * pFdSockets, CMaaString ServerIpPort, const char * ClassName);
     ~CMaaFtpServerConnection();
     int Notify_Accepted(_IP IpFrom, _Port Port);
     int Notify_Read();
     int Notify_Write();
     void OnTimer(int f);
     bool GetRealAndCanonicalFsName(CMaaString CurrentPath, CMaaString FsName, CMaaString *RealDir, CMaaString *CanonicalDir, bool bFile, CMaaString *pPermissions = NULL);
};
//---------------------------------------------------------------------------
class CMaaFtpServer : public CMaaUnivServer
{
     CMaaString m_ServName;
     CMaaString m_IpPort;
     CMaaTcpSocket * CreateNewConnection(CMaaFdSockets * pFdSockets);

     CMaaSockTimerT<CMaaFtpServer> m_Timer0;

public:
     CMaaFtpServer(CMaaFdSockets * pFdSockets, CMaaString Port, const char * ServerName = "FTP server");
     ~CMaaFtpServer();
     int Notify_Error();
     void OnTimer(int f);
};
//---------------------------------------------------------------------------

void CheckLicense();
