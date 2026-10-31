/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das3mex, a MATLAB binding for the das2C library.
 *
 * das3mex is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das3mex is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das3mex; if not, see <http://www.gnu.org/licenses/>.
 */

/* das3mex: the gateway.
 *
 *   das3mex('version')                       -> struct
 *   das3mex('config')                        -> settings struct
 *   das3mex('config', settings)              -> apply settings
 *   das3mex('reset')                         -> drop the session
 *   das3mex('seeds')                         -> cellstr
 *   das3mex('node', path)                    -> node struct
 *   das3mex('source', path, accept)          -> source struct
 *   das3mex('explain', path, keys, opts)     -> struct, no network
 *   das3mex('query', path, keys, opts, outfile) -> struct
 *   das3mex('cred', 'add', server, realm, dataset, user, pass)
 *   das3mex('cred', 'load', file)            -> count
 *   das3mex('cred', 'save', file)            -> count
 *
 * Why one session: a DasCat remembers every node it loaded and the
 * credentials every request should carry.  Those have to outlive a single
 * call, so they are statics here and the +das3 functions stay stateless.
 * The session is rebuilt lazily after a settings change because DasCat
 * policy (prefix, seed, mounts) freezes on the first resolve.
 *
 * Why no mexLock: 'clear mex' is the developer's reset button.  Losing the
 * node cache costs one HTTP round trip per catalog level, nothing more,
 * and credentials loaded from ~/.das/servers come back on their own.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "das3mex.h"

/* ************************************************************************* */
/* Settings, the state the session is built from */

typedef struct {
	char  sStart[DAS3MEX_URL_SZ];   /* -r: prefix, URL, file or URL=URI, "" default */
	char* aMountUrl[DAS3MEX_MAX_MOUNTS];
	char* aMountUri[DAS3MEX_MAX_MOUNTS];
	size_t uMounts;
	char  sCache[1024];             /* "" default dir, "none" disables */
	bool  bFresh;                   /* ignore cached nodes */
	char  sAgent[128];
	float rTimeout;
	bool  bQuiet;
	bool  bPrompt;                  /* may ask MATLAB for a password */
	int   nLogLevel;
} das3mex_settings;

static das3mex_settings g_set;
static bool g_bSetInit = false;

/* The session proper.  A DasCat owns its credential manager and deletes it,
 * so credentials added by das3.login are kept here too and re-added when the
 * session is rebuilt. */
static DasCat* g_pCat = NULL;
static bool    g_bDirty = true;   /* settings changed since g_pCat was built */

#define MAX_LOGIN 32
static das_credential g_aLogin[MAX_LOGIN];
static size_t g_uLogin = 0;

static char g_sLastErr[2048] = {'\0'};
static bool g_bInit = false;

/* ************************************************************************* */
/* Logging and errors */

/* das2C log lines go to the command window at the level das2C gives them.
 * Warnings use MATLAB's warning channel so they stand out; error text is
 * kept so the mex error that follows can quote it.  Timestamps are off,
 * this is an interactive session not a server log. */
static void logHandler(int nLevel, const char* sMsg, bool bPrnTime)
{
	if(nLevel >= DASLOG_ERROR){
		/* Errors accumulate within one call: a requisition with three bad
		   keys reports all three in the MATLAB error, not just the last */
		size_t uHave = strlen(g_sLastErr);
		if((uHave > 0) && (uHave + 1 < sizeof(g_sLastErr))) g_sLastErr[uHave++] = '\n';
		strncpy(g_sLastErr + uHave, sMsg, sizeof(g_sLastErr) - uHave - 1);
		g_sLastErr[sizeof(g_sLastErr) - 1] = '\0';
		/* Not printed here: the MATLAB error that follows quotes it.  A
		   caller that recovers instead (alternate server) prints it itself. */
		return;
	}
	if(nLevel >= DASLOG_WARN){
		if(!g_set.bQuiet) mexWarnMsgIdAndTxt("das3:lib", "%s", sMsg);
		return;
	}
	mexPrintf("das3: %s\n", sMsg);
}

const char* das3mex_lastErr(void)
{
	if(g_sLastErr[0] != '\0') return g_sLastErr;
	das_error_msg* pMsg = das_get_error();
	if((pMsg != NULL) && (pMsg->message != NULL) && (pMsg->message[0] != '\0'))
		return pMsg->message;
	return "";
}

void das3mex_raise(const char* sId, const char* sContext)
{
	/* Copy first: mexErrMsgIdAndTxt does not return and the static buffer
	   should be clean for the next call */
	char sMsg[2300];
	const char* sErr = das3mex_lastErr();
	if((sErr[0] != '\0') && (sContext != NULL) && (sContext[0] != '\0'))
		snprintf(sMsg, sizeof(sMsg), "%s: %s", sContext, sErr);
	else if(sErr[0] != '\0')
		snprintf(sMsg, sizeof(sMsg), "%s", sErr);
	else
		snprintf(sMsg, sizeof(sMsg), "%s", sContext ? sContext : "das2C call failed");
	g_sLastErr[0] = '\0';
	mexErrMsgIdAndTxt(sId, "%s", sMsg);
}

/* ************************************************************************* */
/* mx helpers */

mxArray* mx_str(const char* s)
{
	return mxCreateString(s ? s : "");
}

mxArray* mx_struct(int nFields, const char** aFields)
{
	return mxCreateStructMatrix(1, 1, nFields, aFields);
}

bool mx_toStr(const mxArray* p, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	if(p == NULL) return false;

	/* A MATLAB string scalar arrives as a class object, not a char array.
	   mxArrayToUTF8String copes with char only, so ask MATLAB to convert. */
	mxArray* pChar = NULL;
	const mxArray* pUse = p;
	if(mxIsClass(p, "string")){
		mxArray* pIn[1] = { (mxArray*)p };
		if(mexCallMATLAB(1, &pChar, 1, pIn, "char") != 0) return false;
		pUse = pChar;
	}
	if(!mxIsChar(pUse)){
		if(pChar) mxDestroyArray(pChar);
		return false;
	}
	char* s = mxArrayToUTF8String(pUse);
	if(s != NULL){
		strncpy(sBuf, s, uLen - 1);
		sBuf[uLen - 1] = '\0';
		mxFree(s);
	}
	if(pChar) mxDestroyArray(pChar);
	return (s != NULL);
}

bool mx_cellStr(const mxArray* pCell, size_t i, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	if((pCell == NULL) || !mxIsCell(pCell) || (i >= mxGetNumberOfElements(pCell)))
		return false;
	return mx_toStr(mxGetCell(pCell, i), sBuf, uLen);
}

bool mx_fieldStr(const mxArray* pS, const char* sField, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	if((pS == NULL) || !mxIsStruct(pS)) return false;
	const mxArray* pF = mxGetField(pS, 0, sField);
	if((pF == NULL) || mxIsEmpty(pF)) return false;
	return mx_toStr(pF, sBuf, uLen);
}

double mx_fieldNum(const mxArray* pS, const char* sField, double rDef)
{
	if((pS == NULL) || !mxIsStruct(pS)) return rDef;
	const mxArray* pF = mxGetField(pS, 0, sField);
	if((pF == NULL) || mxIsEmpty(pF) || !mxIsNumeric(pF)) return rDef;
	return mxGetScalar(pF);
}

bool mx_fieldBool(const mxArray* pS, const char* sField, bool bDef)
{
	if((pS == NULL) || !mxIsStruct(pS)) return bDef;
	const mxArray* pF = mxGetField(pS, 0, sField);
	if((pF == NULL) || mxIsEmpty(pF)) return bDef;
	if(mxIsLogical(pF)) return mxIsLogicalScalarTrue(pF);
	if(mxIsNumeric(pF)) return mxGetScalar(pF) != 0.0;
	return bDef;
}

/* ************************************************************************* */
/* Credentials prompt
 *
 * das2C's built in prompt reads the terminal, which MATLAB does not have in
 * desktop mode and shares awkwardly in -nodisplay mode.  So the prompt is
 * delegated to the MATLAB function credprompt (in +das3/private) which can
 * use inputdlg or input as appropriate.  It returns {user, pass} or {} to
 * cancel. */
static bool mexPrompt(
	const char* sServer, const char* sRealm, const char* sDataset,
	const char* sMessage, char* sUser, char* sPassword
){
	if(!g_set.bPrompt) return false;

	mxArray* pIn[4];
	pIn[0] = mx_str(sServer);
	pIn[1] = mx_str(sRealm);
	pIn[2] = mx_str(sDataset);
	pIn[3] = mx_str(sMessage);
	mxArray* pOut[2] = {NULL, NULL};

	/* mexCallMATLABWithTrap so a Ctrl-C or an error in the prompt becomes
	   "no credentials" instead of unwinding through das2C's HTTP code with
	   a half open socket */
	mxArray* pErr = mexCallMATLABWithTrap(2, pOut, 4, pIn, "credprompt");
	for(int i = 0; i < 4; ++i) mxDestroyArray(pIn[i]);
	if(pErr != NULL){
		mxDestroyArray(pErr);
		return false;
	}
	bool bOk = mx_toStr(pOut[0], sUser, 128) && mx_toStr(pOut[1], sPassword, 128);
	if(pOut[0]) mxDestroyArray(pOut[0]);
	if(pOut[1]) mxDestroyArray(pOut[1]);
	return bOk && (sUser[0] != '\0');
}

/* ************************************************************************* */
/* Session */

static void defaults(void)
{
	memset(&g_set, 0, sizeof(g_set));
	strncpy(g_set.sAgent, DAS3MEX_AGENT, sizeof(g_set.sAgent) - 1);
	g_set.rTimeout = 10.0f;
	g_set.bPrompt = true;
	g_set.nLogLevel = DASLOG_WARN;
	/* Where paths start is NOT read from the environment or preferences,
	   unlike das_fetch's DASFETCH_START.  A script that sets the root says
	   so in a line anyone can read; hidden per-user state would make the
	   same path resolve differently on two machines. */
	g_bSetInit = true;
}

static void dropSession(void)
{
	if(g_pCat != NULL){ del_DasCat(g_pCat); g_pCat = NULL; }
	g_bDirty = true;
}

static void atExit(void)
{
	dropSession();
	das_finish();
}

/* The one time library setup.  DASERR_DIS_RET is not optional: the default
   disposition calls exit(), which would take MATLAB down with it. */
static void libInit(void)
{
	if(g_bInit) return;
	if(!g_bSetInit) defaults();
	das_init("das3mex", DASERR_DIS_RET, 0, g_set.nLogLevel, logHandler);
	daslog_set_showline(DASLOG_NOTHING);
	mexAtExit(atExit);
	g_bInit = true;
}

/* -r START or -m URL=URI in das_fetch's terms.  The split rule is copied
 * from das_fetch.c applyStartArg(): the last '=' divides location from
 * name when what follows is a URI or, with no query string before it, a
 * path.  Returns false after logging. */
static bool applyStart(DasCat* pCat, const char* sVal, bool bMount, const char* sMountUri)
{
	if(bMount){
		if((sMountUri == NULL) || (sMountUri[0] == '\0')){
			snprintf(g_sLastErr, sizeof(g_sLastErr), "mount of %s needs a catalog name", sVal);
			return false;
		}
		return DasCat_mount(pCat, sVal, sMountUri) == DAS_OKAY;
	}

	const char* pEq = strrchr(sVal, '=');
	const char* sName = (pEq != NULL) ? pEq + 1 : NULL;
	bool bNamed = false;
	if((sName != NULL) && (sName[0] != '\0') && !das_isUrlOrFile(sName)){
		if(das_isUri(sName)) bNamed = true;
		else bNamed = (memchr(sVal, '?', (size_t)(pEq - sVal)) == NULL);
	}
	if(bNamed){
		char sUrl[DAS3MEX_URL_SZ];
		size_t uLen = (size_t)(pEq - sVal);
		if(uLen >= sizeof(sUrl)) uLen = sizeof(sUrl) - 1;
		memcpy(sUrl, sVal, uLen);
		sUrl[uLen] = '\0';
		if(!das_isUrlOrFile(sUrl)){
			snprintf(g_sLastErr, sizeof(g_sLastErr),
				"start takes URL=URI, '%s' before the '=' is not a URL or a file", sUrl);
			return false;
		}
		return DasCat_setSeed(pCat, sUrl, sName) == DAS_OKAY;
	}
	if(das_isUrlOrFile(sVal)) return DasCat_setSeed(pCat, sVal, NULL) == DAS_OKAY;
	if(pEq != NULL){
		snprintf(g_sLastErr, sizeof(g_sLastErr),
			"start takes URL=URI, and '%s' after the '=' is not a catalog name", sName);
		return false;
	}
	return DasCat_setPrefix(pCat, sVal) == DAS_OKAY;
}

DasCat* das3mex_cat(void)
{
	libInit();
	if(!g_bDirty && (g_pCat != NULL)) return g_pCat;
	dropSession();

	/* Saved credentials first, as das_fetch does, then the ones given this
	   session which may override a stale saved password */
	char sCredFile[1024];
	snprintf(sCredFile, sizeof(sCredFile), "%s" DAS_DSEPS "servers", das_userdir());
	DasCredMngr* pMgr = new_CredMngr(sCredFile);
	FILE* pTest = fopen(sCredFile, "r");
	if(pTest != NULL){
		fclose(pTest);
		CredMngr_load(pMgr, NULL, NULL);
	}
	for(size_t u = 0; u < g_uLogin; ++u) CredMngr_addCred(pMgr, g_aLogin + u);
	CredMngr_setPrompt(pMgr, mexPrompt);

	DasCat* pCat = new_DasCat(pMgr, g_set.sAgent);
	if(pCat == NULL){
		del_CredMngr(pMgr);
		return NULL;
	}
	bool bOk = true;
	if(g_set.sStart[0] != '\0') bOk = applyStart(pCat, g_set.sStart, false, NULL);
	for(size_t u = 0; bOk && (u < g_set.uMounts); ++u)
		bOk = applyStart(pCat, g_set.aMountUrl[u], true, g_set.aMountUri[u]);
	if(!bOk){
		del_DasCat(pCat);
		return NULL;
	}

	if(strcmp(g_set.sCache, "none") == 0)
		DasCat_setCache(pCat, NULL, 0);
	else if(g_set.sCache[0] != '\0')
		DasCat_setCache(pCat, g_set.sCache, g_set.bFresh ? 0 : DASCAT_DEF_CACHE_SEC);
	else if(g_set.bFresh)
		DasCat_setCache(pCat, DasCat_cacheDir(pCat), 0);

	g_pCat = pCat;
	g_bDirty = false;
	return g_pCat;
}

float das3mex_timeout(void){ return g_set.rTimeout; }
bool  das3mex_quiet(void){ return g_set.bQuiet; }

/* ************************************************************************* */
/* Settings as a MATLAB struct, in both directions */

/* das_loglvl_toStr() knows the stream log levels, not the library's, so
   the names daslog_strlevel() accepts are spelled here */
static const char* logLevelName(int nLevel)
{
	if(nLevel >= DASLOG_NOTHING) return "nothing";
	if(nLevel >= DASLOG_CRIT) return "crit";
	if(nLevel >= DASLOG_ERROR) return "error";
	if(nLevel >= DASLOG_WARN) return "warn";
	if(nLevel >= DASLOG_INFO) return "info";
	if(nLevel >= DASLOG_DEBUG) return "debug";
	return "trace";
}

static const char* g_aSetFields[] = {
	"start", "mounts", "cache", "fresh", "agent", "timeout", "quiet", "prompt", "loglevel"
};
#define N_SET_FIELDS (sizeof(g_aSetFields)/sizeof(g_aSetFields[0]))

static mxArray* settingsToMx(void)
{
	mxArray* pS = mx_struct(N_SET_FIELDS, g_aSetFields);
	mxSetField(pS, 0, "start", mx_str(g_set.sStart));

	mxArray* pMounts = mxCreateCellMatrix(g_set.uMounts, 2);
	for(size_t u = 0; u < g_set.uMounts; ++u){
		mxSetCell(pMounts, u, mx_str(g_set.aMountUrl[u]));
		mxSetCell(pMounts, u + g_set.uMounts, mx_str(g_set.aMountUri[u]));
	}
	mxSetField(pS, 0, "mounts", pMounts);
	mxSetField(pS, 0, "cache", mx_str(g_set.sCache));
	mxSetField(pS, 0, "fresh", mxCreateLogicalScalar(g_set.bFresh));
	mxSetField(pS, 0, "agent", mx_str(g_set.sAgent));
	mxSetField(pS, 0, "timeout", mxCreateDoubleScalar(g_set.rTimeout));
	mxSetField(pS, 0, "quiet", mxCreateLogicalScalar(g_set.bQuiet));
	mxSetField(pS, 0, "prompt", mxCreateLogicalScalar(g_set.bPrompt));
	mxSetField(pS, 0, "loglevel", mx_str(logLevelName(g_set.nLogLevel)));
	return pS;
}

static void clearMounts(void)
{
	for(size_t u = 0; u < g_set.uMounts; ++u){
		free(g_set.aMountUrl[u]);
		free(g_set.aMountUri[u]);
	}
	g_set.uMounts = 0;
}

static void settingsFromMx(const mxArray* pS)
{
	if(!mxIsStruct(pS)) mexErrMsgIdAndTxt(ERRID_ARG, "config takes a settings struct");
	das3mex_settings old = g_set;   /* compare after, only policy changes dirty the session */

	mx_fieldStr(pS, "start", g_set.sStart, sizeof(g_set.sStart));
	mx_fieldStr(pS, "cache", g_set.sCache, sizeof(g_set.sCache));
	g_set.bFresh = mx_fieldBool(pS, "fresh", g_set.bFresh);
	char sAgent[128];
	if(mx_fieldStr(pS, "agent", sAgent, sizeof(sAgent)) && (sAgent[0] != '\0'))
		strncpy(g_set.sAgent, sAgent, sizeof(g_set.sAgent) - 1);
	g_set.rTimeout = (float)mx_fieldNum(pS, "timeout", g_set.rTimeout);
	g_set.bQuiet = mx_fieldBool(pS, "quiet", g_set.bQuiet);
	g_set.bPrompt = mx_fieldBool(pS, "prompt", g_set.bPrompt);
	char sLvl[32];
	if(mx_fieldStr(pS, "loglevel", sLvl, sizeof(sLvl)) && (sLvl[0] != '\0')){
		g_set.nLogLevel = daslog_strlevel(sLvl);
		daslog_setlevel(g_set.nLogLevel);
	}

	/* Mounts: an N x 2 cellstr, URL then catalog name.  Replaced wholesale,
	   das3.config does the appending. */
	const mxArray* pM = mxGetField(pS, 0, "mounts");
	bool bMountsChanged = false;
	if((pM != NULL) && mxIsCell(pM)){
		size_t uRows = mxGetM(pM);
		if((mxGetN(pM) != 2) && (uRows != 0))
			mexErrMsgIdAndTxt(ERRID_ARG, "mounts must be an N x 2 cell array of {URL, NAME}");
		if(uRows > DAS3MEX_MAX_MOUNTS)
			mexErrMsgIdAndTxt(ERRID_ARG, "at most %d mounts", DAS3MEX_MAX_MOUNTS);
		char sUrl[DAS3MEX_URL_SZ], sUri[1024];
		/* Compare before replacing so an unchanged config does not drop the
		   node cache */
		bMountsChanged = (uRows != old.uMounts);
		for(size_t u = 0; !bMountsChanged && (u < uRows); ++u){
			mx_cellStr(pM, u, sUrl, sizeof(sUrl));
			mx_cellStr(pM, u + uRows, sUri, sizeof(sUri));
			if(strcmp(sUrl, old.aMountUrl[u]) || strcmp(sUri, old.aMountUri[u])) bMountsChanged = true;
		}
		if(bMountsChanged){
			clearMounts();
			for(size_t u = 0; u < uRows; ++u){
				mx_cellStr(pM, u, sUrl, sizeof(sUrl));
				mx_cellStr(pM, u + uRows, sUri, sizeof(sUri));
				g_set.aMountUrl[u] = strdup(sUrl);
				g_set.aMountUri[u] = strdup(sUri);
			}
			g_set.uMounts = uRows;
		}
	}

	if(bMountsChanged || strcmp(old.sStart, g_set.sStart) || strcmp(old.sCache, g_set.sCache)
	   || (old.bFresh != g_set.bFresh) || strcmp(old.sAgent, g_set.sAgent))
		g_bDirty = true;
}

/* ************************************************************************* */
/* Credentials: das3mex('cred', 'add'|'load'|'save', ...) */

static mxArray* credCmd(int nIn, const mxArray* pIn[])
{
	char sSub[16];
	if((nIn < 2) || !mx_toStr(pIn[1], sSub, sizeof(sSub)))
		mexErrMsgIdAndTxt(ERRID_ARG, "cred needs a sub-command: add, load or save");

	if(strcmp(sSub, "add") == 0){
		char sServer[DASCRED_SRV_SZ], sRealm[DASCRED_REALM_SZ], sDataset[DASCRED_DSET_SZ];
		char sUser[128], sPass[128];
		if((nIn != 7) || !mx_toStr(pIn[2], sServer, sizeof(sServer)) ||
		   !mx_toStr(pIn[3], sRealm, sizeof(sRealm)) || !mx_toStr(pIn[4], sDataset, sizeof(sDataset)) ||
		   !mx_toStr(pIn[5], sUser, sizeof(sUser)) || !mx_toStr(pIn[6], sPass, sizeof(sPass)))
			mexErrMsgIdAndTxt(ERRID_ARG, "cred add takes server, realm, dataset, user, password");
		if(g_uLogin >= MAX_LOGIN)
			mexErrMsgIdAndTxt(ERRID_ARG, "at most %d logins per session", MAX_LOGIN);

		/* das_cred_init wants the base64 hash, CredMngr_addUserPass makes it.
		   So the live manager builds the credential and a copy is kept here
		   for the next session rebuild. */
		DasCat* pCat = das3mex_cat();
		if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");
		DasCredMngr* pMgr = DasCat_credMngr(pCat);
		const char* sDs = sDataset[0] ? sDataset : NULL;
		if(CredMngr_addUserPass(pMgr, sServer, sRealm, sDs, sUser, sPass) < 0){
			memset(sPass, 0, sizeof(sPass));
			das3mex_raise(ERRID_ARG, "credential rejected");
		}
		memset(sPass, 0, sizeof(sPass));
		das_credential* pCred = CredMngr_getCred(pMgr, sServer, sRealm, sDs, false);
		if(pCred == NULL) das3mex_raise(ERRID_INTERN, "credential not stored");

		size_t uAt = g_uLogin;
		for(size_t u = 0; u < g_uLogin; ++u){
			if((strcmp(g_aLogin[u].sServer, pCred->sServer) == 0) &&
			   (strcmp(g_aLogin[u].sRealm, pCred->sRealm) == 0) &&
			   (strcmp(g_aLogin[u].sDataset, pCred->sDataset) == 0)){ uAt = u; break; }
		}
		if(uAt == g_uLogin) ++g_uLogin;
		g_aLogin[uAt] = *pCred;
		return mxCreateDoubleScalar((double)g_uLogin);
	}

	if((strcmp(sSub, "load") == 0) || (strcmp(sSub, "save") == 0)){
		char sFile[1024] = {'\0'};
		if(nIn > 2) mx_toStr(pIn[2], sFile, sizeof(sFile));
		DasCat* pCat = das3mex_cat();
		if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");
		DasCredMngr* pMgr = DasCat_credMngr(pCat);
		int n = (sSub[0] == 'l') ? CredMngr_load(pMgr, NULL, sFile[0] ? sFile : NULL)
		                         : CredMngr_save(pMgr, NULL, sFile[0] ? sFile : NULL);
		if(n < 0) das3mex_raise(ERRID_IO, sSub[0] == 'l' ? "loading credentials" : "saving credentials");
		return mxCreateDoubleScalar((double)n);
	}

	mexErrMsgIdAndTxt(ERRID_ARG, "unknown cred sub-command '%s'", sSub);
	return NULL;
}

/* ************************************************************************* */
/* The gateway */

void mexFunction(int nOut, mxArray* pOut[], int nIn, const mxArray* pIn[])
{
	if(!g_bSetInit) defaults();
	g_sLastErr[0] = '\0';

	char sCmd[32];
	if((nIn < 1) || !mx_toStr(pIn[0], sCmd, sizeof(sCmd)))
		mexErrMsgIdAndTxt(ERRID_ARG, "das3mex needs a command string, see +das3 functions");
	if(nOut > 1)
		mexErrMsgIdAndTxt(ERRID_ARG, "das3mex returns at most one value");

	char sPath[DAS3MEX_URL_SZ];
	char sAccept[256];

	if(strcmp(sCmd, "version") == 0){
		libInit();
		const char* aF[] = {"das3mex", "das2C"};
		mxArray* pS = mx_struct(2, aF);
		mxSetField(pS, 0, "das3mex", mx_str(DAS3MEX_VERSION));
		mxSetField(pS, 0, "das2C", mx_str(das_version()));
		pOut[0] = pS;
		return;
	}

	if(strcmp(sCmd, "config") == 0){
		libInit();
		if(nIn > 1) settingsFromMx(pIn[1]);
		pOut[0] = settingsToMx();
		return;
	}

	if(strcmp(sCmd, "reset") == 0){
		dropSession();
		clearMounts();
		defaults();
		if(g_bInit) daslog_setlevel(g_set.nLogLevel);
		g_uLogin = 0;
		memset(g_aLogin, 0, sizeof(g_aLogin));
		pOut[0] = mxCreateLogicalScalar(true);
		return;
	}

	if(strcmp(sCmd, "seeds") == 0){
		size_t uSeeds = 0;
		const char** psSeeds = das_seed_urls(&uSeeds);
		mxArray* pC = mxCreateCellMatrix(uSeeds, 1);
		for(size_t u = 0; u < uSeeds; ++u) mxSetCell(pC, u, mx_str(psSeeds[u]));
		pOut[0] = pC;
		return;
	}

	if(strcmp(sCmd, "cred") == 0){
		libInit();
		pOut[0] = credCmd(nIn, pIn);
		return;
	}

	/* Everything below takes a catalog path second */
	if((nIn < 2) || !mx_toStr(pIn[1], sPath, sizeof(sPath)))
		mexErrMsgIdAndTxt(ERRID_ARG, "%s needs a catalog path", sCmd);
	/* "a/b/" means "a/b", tab completion adds the slash */
	if(!das_isUrlOrFile(sPath)){
		size_t u = strlen(sPath);
		while((u > 1) && (sPath[u-1] == '/') && (sPath[u-2] != ':')) sPath[--u] = '\0';
	}

	if(strcmp(sCmd, "node") == 0){
		/* An empty path is where paths start, as for das_fetch node */
		pOut[0] = mxsrc_node(sPath[0] ? sPath : NULL);
		return;
	}
	if(strcmp(sCmd, "source") == 0){
		sAccept[0] = '\0';
		if(nIn > 2) mx_toStr(pIn[2], sAccept, sizeof(sAccept));
		pOut[0] = mxsrc_source(sPath, sAccept[0] ? sAccept : NULL);
		return;
	}
	if(strcmp(sCmd, "explain") == 0){
		pOut[0] = mxsrc_explain(sPath, (nIn > 2) ? pIn[2] : NULL, (nIn > 3) ? pIn[3] : NULL);
		g_sLastErr[0] = '\0';   /* verdicts carry what the check logged */
		return;
	}
	if(strcmp(sCmd, "query") == 0){
		char sOut[1024];
		if((nIn < 5) || !mx_toStr(pIn[4], sOut, sizeof(sOut)) || (sOut[0] == '\0'))
			mexErrMsgIdAndTxt(ERRID_ARG, "query takes path, keys, opts, outfile");
		pOut[0] = mxsrc_query(sPath, pIn[2], pIn[3], sOut);
		return;
	}

	mexErrMsgIdAndTxt(ERRID_ARG, "unknown das3mex command '%s'", sCmd);
}
