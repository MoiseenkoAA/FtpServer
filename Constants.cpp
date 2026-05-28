#include "stdafx.h"
#include "Constants.h"

#ifdef _DEBUG
//#define new RR_NEW
#endif

const CCon gCon;

#define STR_CONSTR(v, t) t

CCon::CCon() noexcept
:   m_hXmlCache(e_max)
{
#ifndef TOOLSLIB_USE_CMaaConstStr
    static const char* psz[e_max + 1] =
#else
    static const CMaaConstStr str[e_max + 1] =
#endif
    {
        STR_CONSTR(e_ConfigFileName, "RR.xml"),
        STR_CONSTR(e_True, "true"),
        STR_CONSTR(e_False, "false"),
        STR_CONSTR(e_Yes, "yes"),
        STR_CONSTR(e_No, "no"),
        STR_CONSTR(e_Allow, "Allow"), STR_CONSTR(e_Deny, "Deny"), STR_CONSTR(e_Reject, "Reject"),
        STR_CONSTR(e_Indeterminate, "indeterminate"),

        STR_CONSTR(e_FtpServersElement, "FtpServers"),
        STR_CONSTR(e_aLastFtpServerId, "LastFtpServerId"),
        STR_CONSTR(e_FtpServerElement, "FtpServer"),
        STR_CONSTR(e_FtpServer_aPort, "Port"),
        STR_CONSTR(e_FtpServer_aLog, "log"),
        STR_CONSTR(e_FtpServer_aFailLog, "faillog"),
        STR_CONSTR(e_FtpServer_aLoginLog, "login_log"),
        STR_CONSTR(e_FtpServer_aLogLock, "log_lock"),
        STR_CONSTR(e_FtpServerUsers, "Users"),
        STR_CONSTR(e_FtpServerUser, "User"),
        STR_CONSTR(e_FtpServerUser_aName, "Name"),
        STR_CONSTR(e_FtpServerUser_aFXP, "FXP"),
        STR_CONSTR(e_FtpServerUser_aPassType, "PassType"),
        STR_CONSTR(e_FtpServerUser_aPass, "Pass"),
        STR_CONSTR(e_FtpServerUser_aPassType_vEmail, "email"),
        STR_CONSTR(e_FtpServerUser_aPassType_vPlain, "Plain"),
        STR_CONSTR(e_FtpServerUser_aPassType_vHash, "hash"),
        STR_CONSTR(e_FtpServerUser_aPassType_vRusRoute, "RusRoute"),
        STR_CONSTR(e_FtpServerUser_aPassSalt, "PassSalt"),
        STR_CONSTR(e_FtpServerUser_aPassHash, "PassHash"),
        STR_CONSTR(e_FtpServerUser_aPassHashType, "PassHashType"),
        STR_CONSTR(e_FtpServerUser_aPassHashType_vGBM1, "GBM1"),
        STR_CONSTR(e_FtpServerUser_aEnabled, "Enabled"),
        STR_CONSTR(e_FtpServerDir, "dir"),
        STR_CONSTR(e_FtpServerDir_vfs_path, "vfs_path"),
        STR_CONSTR(e_FtpServerDir_vfs_path_root, "/"),
        STR_CONSTR(e_FtpServerDir_real_path, "real_path"),
        STR_CONSTR(e_FtpServerDirPerm, "dirperm"),
        STR_CONSTR(e_FtpServerDirPerm_path, "path"),
        STR_CONSTR(e_FtpServerDirPerm_perm, "perm"),

        STR_CONSTR(e_max, "End")
    };
    //CMaaUnivHash<CMaaString, CMaaString> h(e_max + 1);
    int i;
    for (i = 0; i < e_max; i++)
    {
#ifndef TOOLSLIB_USE_CMaaConstStr
        m[i] = CMaaString(psz[i], CMaaString::eROStrlenMemString);
#else
        m[i] = str[i];
#endif
        m_hXmlCache.Find(m[i], &m[i]) && m_hXmlCache.Add(m[i], m[i]);
    }
#ifndef TOOLSLIB_USE_CMaaConstStr
    m[i] = CMaaString(psz[i], CMaaString::eROStrlenMemString);
#else
    m[i] = str[i];
#endif
    if (m[e_max] != "End")
    {
        exit(100);
    }
    m[e_max].Empty();
}
/*
CCon::~CCon()
{
}
*/
const CMaaString & CCon::Get(e _e)  const noexcept
{
    return m[_e];
}
const CMaaString & CCon::operator[] (e _e)  const noexcept
{
    return m[_e];
}
void CCon::Fill(CMaaUnivHash<CMaaString, CMaaString>& h) const
{
    for (int i = 0; i < e_max; i++)
    {
        h.Add(gCon[(e)i], gCon[(e)i]);
    }
}

/*
const CMaaString & CMaaGlobalString(CMaaGlobalStrings::e _e) noexcept
{
    static CMaaGlobalStrings* p = RR_NEW_ CMaaGlobalStrings;
    return p->Get(_e);
}
*/

/* 
// Замена:
MAA_STATIC_FLAG sf_Flag1;
void GetServerStatistics(int& Users, int& ActiveUsers, double& BandWidth)
{
    CMaaSafeMakeStatic msms(&sf_Flag1);
    msms.Make();
    static CMaaString sName = gStaticStringsAllocator.Alloc("GetServerStatistics()");
    msms.Done();
    CMaaWin32Locker gLocker(gLock, sName);
    gLocker.LockM();
    // ...
    gLocker.UnLock();
}
// на:
void GetServerStatistics(int& Users, int& ActiveUsers, double& BandWidth)
{
    CMaaWin32Locker gLocker(gLock, CMaaGlobalString_(CMaaGlobalStrings::e_main));
    gLocker.LockM();
    // ...
    gLocker.UnLock();
}
*/
