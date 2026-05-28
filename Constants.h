#pragma once

#define RR_API_IP_LISTS_COUNT 20

class CCon
{
public:
    enum e
    {
        e_ConfigFileName = 0,
        e_True, e_False, e_Yes, e_No, e_Allow, e_Deny, e_Reject, e_Indeterminate,

        e_FtpServersElement,
        e_aLastFtpServerId,
        e_FtpServerElement,
        e_FtpServer_aPort,
        e_FtpServer_aLog,
        e_FtpServer_aFailLog,
        e_FtpServer_aLoginLog,
        e_FtpServer_aLogLock,
        e_FtpServerUsers,
        e_FtpServerUser,
        e_FtpServerUser_aName,
        e_FtpServerUser_aFXP,
        e_FtpServerUser_aPassType,
        e_FtpServerUser_aPass,
        e_FtpServerUser_aPassType_vEmail,
        e_FtpServerUser_aPassType_vPlain,
        e_FtpServerUser_aPassType_vHash,
        e_FtpServerUser_aPassType_vRusRoute,
        e_FtpServerUser_aPassSalt,
        e_FtpServerUser_aPassHash,
        e_FtpServerUser_aPassHashType,
        e_FtpServerUser_aPassHashType_vGBM1,
        e_FtpServerUser_aEnabled,
        e_FtpServerDir,
        e_FtpServerDir_vfs_path,
        e_FtpServerDir_vfs_path_root,
        e_FtpServerDir_real_path,
        e_FtpServerDirPerm,
        e_FtpServerDirPerm_path,
        e_FtpServerDirPerm_perm,

        e_max
    };
    CCon () noexcept;
    const CMaaString & Get(e _e) const noexcept;
    const CMaaString & operator[] (e _e) const noexcept;

    void Fill(CMaaUnivHash<CMaaString, CMaaString>& h) const;
protected:
    CMaaString m[e_max + 1];
public:
    CMaaUnivHash<CMaaString, CMaaString> m_hXmlCache;
};

//const CMaaString& CMaaGlobalString(CMaaGlobalStrings::e _e) noexcept;

#ifdef _DEBUG_MTX
//#define CMaaGlobalString_(x) CMaaGlobalString(x)
#else
//#define CMaaGlobalString_(x) CMaaStringZ
#endif

extern const CCon gCon;
//#define g_hXmlCache (CMaaUnivHash<CMaaString, CMaaString> &)gCon.m_hXmlCache
