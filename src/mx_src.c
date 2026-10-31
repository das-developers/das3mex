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

/* Catalog nodes, sources and requisitions as MATLAB structs.
 *
 * The library resolves paths and reads nodes (DasCat, DasNode), describes
 * sources (DasSrc) and turns requisitions into URLs (DasReq,
 * DasStreamSrc_url).  What lives here is the choice of which source to use
 * when a path names a collection, and the spelling of all of it as mx
 * structs.  Compare utilities/das_fetch.c, which makes the same choices for
 * a terminal.
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <inttypes.h>

#include <das3/node.h>
#include <das3/json.h>

#include "das3mex.h"

/* ************************************************************************* */
/* Small helpers */

static bool isDigits(const char* s)
{
	if((s == NULL)||(*s == '\0')) return false;
	for(; *s; ++s) if(!isdigit((unsigned char)*s)) return false;
	return true;
}

static const char* jstr(const DasJdo* p, const char* sPath)
{
	return (p == NULL) ? NULL : DasJdo_string(DasJdo_get(p, sPath));
}

/* ************************************************************************* */
/* Choosing a source under a collection
 *
 * Catalog paths usually stop at a Collection or SourceSet whose children
 * are the actual sources: a das stream, CSV, CDF, maybe raw packets.  The
 * library will not choose, it is not its business what a client can read
 * (see dassrc_loadable).  This binding wants das streams, so by default
 * the stream types are asked for one at a time, das3 before das2, binary
 * before text, then anything; the first type any source provides settles
 * it.  A caller's
 * accept list is one ask: everything that provides any of the listed types
 * qualifies, and more than one match is an error naming them, so the user
 * puts one on the end of the path. */

static bool stubProvides(const DasJdo* pStub, const char* sAccept)
{
	const DasJdo* pProv = (pStub == NULL) ? NULL : DasJdo_get(pStub, "provides");
	bool bList = (pProv != NULL) && (pProv->type == das_json_type_ary);

	const char* sWant = sAccept;
	while(true){
		sWant += strspn(sWant, ", \t");
		size_t uWant = strcspn(sWant, ", \t");
		if(uWant == 0) return false;
		if((uWant == 3) && (strncmp(sWant, "*/*", 3) == 0)) return true;
		if(bList){
			for(const das_json_ary_el* pEl = DasJdo_aryFirst(pProv); pEl; pEl = pEl->next){
				const char* sHas = DasJdo_string(pEl->value);
				if(sHas == NULL) continue;
				size_t uHas = strcspn(sHas, "; \t");
				if((uHas == uWant) && (strncasecmp(sWant, sHas, uWant) == 0)) return true;
			}
		}
		sWant += uWant;
	}
}

static const DasJdo* childStub(const DasNode* pNode, const char* sName)
{
	size_t uKids = DasNode_subCount(pNode);
	for(size_t u = 0; u < uKids; ++u)
		if(strcmp(DasNode_subName(pNode, u), sName) == 0) return DasNode_subStub(pNode, u);
	return NULL;
}

/* Resolve a path to a source, stepping into a collection if needed.
 * Returns NULL after leaving an explanation in the error text. */
static DasSrc* pickSrc(DasCat* pCat, const char* sPath, const char* sAccept)
{
	DasNode* pNode = DasCat_resolve(pCat, sPath);
	if(pNode == NULL){
		/* The library logged why; add what the nearest catalog offers, the
		   usual cause being a typo in the last component */
		const char* sRest = NULL;
		DasNode* pNear = DasCat_nearest(pCat, &sRest);
		if((pNear != NULL) && (DasNode_subCount(pNear) > 0)){
			char sMsg[1024];
			size_t n = snprintf(sMsg, sizeof(sMsg), "'%s' not found under %s, which has:",
			                    sRest ? sRest : "", DasCat_lastUri(pCat));
			for(size_t u = 0; (u < DasNode_subCount(pNear)) && (n < sizeof(sMsg)); ++u)
				n += snprintf(sMsg + n, sizeof(sMsg) - n, " %s", DasNode_subName(pNear, u));
			daslog_error_v("%s", sMsg);
		}
		return NULL;
	}
	if(DasNode_isStreamSrc(pNode)) return new_DasSrc_node(pNode);

	const char* aNames[DAS3MEX_MAX_KIDS];
	size_t uKids = dassrc_loadable(pNode, aNames, DAS3MEX_MAX_KIDS);
	if(uKids == 0){
		daslog_error_v("Nothing under %s is a data source this binding can fetch from",
		               DasNode_pathUri(pNode));
		return NULL;
	}
	if(uKids > DAS3MEX_MAX_KIDS) uKids = DAS3MEX_MAX_KIDS;

	/* One ask per type by default so das3 beats das2 instead of tying with
	   it, and a CSV only collection still works for 'file' queries; one ask
	   when the caller said what they can read */
	const char* aAsk[5] = {
		DEF_ACCEPT_DAS3_BIN, DEF_ACCEPT_DAS3_TEXT, DEF_ACCEPT_DAS2_BIN, DEF_ACCEPT_DAS2_TEXT, "*/*"
	};
	size_t uAsks = 5;
	if(sAccept != NULL){ aAsk[0] = sAccept; uAsks = 1; }

	const char* aPick[DAS3MEX_MAX_KIDS];
	size_t uPick = 0;
	const char* sAsk = NULL;
	for(size_t a = 0; (a < uAsks) && (uPick == 0); ++a){
		sAsk = aAsk[a];
		for(size_t k = 0; k < uKids; ++k)
			if(stubProvides(childStub(pNode, aNames[k]), sAsk)) aPick[uPick++] = aNames[k];
	}
	if(uPick != 1){
		char sMsg[1024];
		size_t n;
		if(uPick == 0)
			n = snprintf(sMsg, sizeof(sMsg), "Nothing under %s provides %s, the sources it has are:",
			             DasNode_pathUri(pNode), sAsk);
		else
			n = snprintf(sMsg, sizeof(sMsg), "%s offers %zu sources for %s, put one on the end of the path:",
			             DasNode_pathUri(pNode), uPick, sAsk);
		const char** aShow = (uPick == 0) ? aNames : aPick;
		size_t uShow = (uPick == 0) ? uKids : uPick;
		for(size_t u = 0; (u < uShow) && (n < sizeof(sMsg)); ++u)
			n += snprintf(sMsg + n, sizeof(sMsg) - n, " %s", aShow[u]);
		daslog_error_v("%s", sMsg);
		return NULL;
	}
	DasNode* pSub = DasCat_subNode(pCat, pNode, aPick[0]);
	if(pSub == NULL){
		daslog_error_v("Couldn't load source '%s' under %s", aPick[0], DasNode_pathUri(pNode));
		return NULL;
	}
	return new_DasSrc_node(pSub);
}

/* ************************************************************************* */
/* das3mex('node', path) */

static const char* nodeTypeName(const DasNode* pNode)
{
	const char* s = jstr(DasNode_getJdo(pNode, NULL), D2FRAG_TYPE);
	return s ? s : "Node";
}

mxArray* mxsrc_node(const char* sPath)
{
	DasCat* pCat = das3mex_cat();
	if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");

	DasNode* pNode = DasCat_resolve(pCat, sPath);
	if(pNode == NULL){
		/* Same explanation as pickSrc gives, by way of a throwaway call */
		const char* sRest = NULL;
		DasNode* pNear = DasCat_nearest(pCat, &sRest);
		if((pNear != NULL) && (DasNode_subCount(pNear) > 0)){
			char sMsg[1024];
			size_t n = snprintf(sMsg, sizeof(sMsg), "'%s' not found under %s, which has:",
			                    sRest ? sRest : "", DasCat_lastUri(pCat));
			for(size_t u = 0; (u < DasNode_subCount(pNear)) && (n < sizeof(sMsg)); ++u)
				n += snprintf(sMsg + n, sizeof(sMsg) - n, " %s", DasNode_subName(pNear, u));
			daslog_error_v("%s", sMsg);
		}
		das3mex_raise(ERRID_NODE, NULL);
	}

	const char* aF[] = {
		"type", "name", "label", "title", "summary", "version", "uri", "url",
		"separator", "iscatalog", "issource", "children"
	};
	mxArray* pS = mx_struct(sizeof(aF)/sizeof(aF[0]), aF);
	mxSetField(pS, 0, "type", mx_str(nodeTypeName(pNode)));
	mxSetField(pS, 0, "name", mx_str(DasNode_name(pNode)));
	mxSetField(pS, 0, "label", mx_str(DasNode_label(pNode)));
	mxSetField(pS, 0, "title", mx_str(DasNode_title(pNode)));
	mxSetField(pS, 0, "summary", mx_str(DasNode_summary(pNode)));
	mxSetField(pS, 0, "version", mx_str(DasNode_version(pNode)));
	/* A root has no URI; the location stands in, as das_fetch shows it */
	const char* sUri = DasNode_pathUri(pNode);
	mxSetField(pS, 0, "uri", mx_str((sUri && sUri[0]) ? sUri : DasCat_lastUri(pCat)));
	mxSetField(pS, 0, "url", mx_str(DasNode_srcUrl(pNode)));
	mxSetField(pS, 0, "separator", mx_str(DasNode_isCatalog(pNode) ? DasNode_pathSep(pNode) : ""));
	mxSetField(pS, 0, "iscatalog", mxCreateLogicalScalar(DasNode_isCatalog(pNode)));
	mxSetField(pS, 0, "issource", mxCreateLogicalScalar(DasNode_isStreamSrc(pNode)));

	size_t uKids = DasNode_subCount(pNode);
	const char* aK[] = {"name", "type", "title", "loadable", "provides"};
	mxArray* pKids = mxCreateStructMatrix(uKids, 1, sizeof(aK)/sizeof(aK[0]), aK);
	for(size_t u = 0; u < uKids; ++u){
		const DasJdo* pStub = DasNode_subStub(pNode, u);
		const char* sT = jstr(pStub, D2FRAG_TYPE);
		const char* sDesc = jstr(pStub, D2FRAG_TITLE);
		if(sDesc == NULL) sDesc = jstr(pStub, D2FRAG_LABEL);
		if(sDesc == NULL) sDesc = jstr(pStub, D2FRAG_DESC);
		mxSetField(pKids, u, "name", mx_str(DasNode_subName(pNode, u)));
		mxSetField(pKids, u, "type", mx_str(sT));
		mxSetField(pKids, u, "title", mx_str(sDesc));
		mxSetField(pKids, u, "loadable", mxCreateLogicalScalar(DasNode_subType(pNode, u) != d2node_inv));

		/* The media types a source child provides, what pickSrc chooses on */
		const DasJdo* pProv = (pStub == NULL) ? NULL : DasJdo_get(pStub, "provides");
		size_t uProv = 0;
		if((pProv != NULL) && (pProv->type == das_json_type_ary))
			for(const das_json_ary_el* pEl = DasJdo_aryFirst(pProv); pEl; pEl = pEl->next) ++uProv;
		mxArray* pC = mxCreateCellMatrix(1, uProv);
		size_t i = 0;
		if(uProv > 0)
			for(const das_json_ary_el* pEl = DasJdo_aryFirst(pProv); pEl; pEl = pEl->next)
				mxSetCell(pC, i++, mx_str(DasJdo_string(pEl->value)));
		mxSetField(pKids, u, "provides", pC);
	}
	mxSetField(pS, 0, "children", pKids);
	return pS;
}

/* ************************************************************************* */
/* das3mex('source', path, accept) */

static const char* propTypeName(const DasSrcProp* p)
{
	switch(DasSrcProp_type(p)){
	case das_srcprop_bool: return "boolean";
	case das_srcprop_int:  return "integer";
	case das_srcprop_real: return "real";
	case das_srcprop_time: return "isotime";
	case das_srcprop_enum: return "enum";
	default:               return "string";
	}
}

/* One section as a struct array of properties.  Fixed properties are
 * included with fixed=true; they say what the stream will look like, and
 * the MATLAB printer decides whether to show them. */
static mxArray* sectionToMx(const DasSrc* pSrc, das_src_sec_e sec)
{
	size_t uRows = 0;
	for(size_t g = 0; g < DasSrc_groupCount(pSrc, sec); ++g)
		uRows += DasSrcGroup_propCount(DasSrc_group(pSrc, sec, g));

	const char* aF[] = {
		"key", "group", "name", "type", "units", "default", "required", "fixed",
		"values", "min", "max", "title"
	};
	mxArray* pA = mxCreateStructMatrix(uRows, 1, sizeof(aF)/sizeof(aF[0]), aF);
	char sTitle[64];
	size_t r = 0;
	for(size_t g = 0; g < DasSrc_groupCount(pSrc, sec); ++g){
		const DasSrcGroup* pG = DasSrc_group(pSrc, sec, g);
		for(size_t i = 0; i < DasSrcGroup_propCount(pG); ++i, ++r){
			const DasSrcProp* p = DasSrcGroup_prop(pG, i);
			const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
			mxSetField(pA, r, "key", mx_str(DasSrcProp_key(p)));
			mxSetField(pA, r, "group", mx_str(DasSrcGroup_name(pG)));
			mxSetField(pA, r, "name", mx_str(DasSrcProp_name(p)));
			const char* sT = DasSrcProp_typeName(p);
			mxSetField(pA, r, "type", mx_str((sT && sT[0]) ? sT : propTypeName(p)));
			mxSetField(pA, r, "units", mx_str(DasSrcProp_units(p)));
			mxSetField(pA, r, "default", mx_str(DasSrcProp_value(p)));
			mxSetField(pA, r, "required", mxCreateLogicalScalar(pC ? DasSrcCtrl_required(pC) : false));
			mxSetField(pA, r, "fixed", mxCreateLogicalScalar(pC == NULL));
			mxSetField(pA, r, "title", mx_str(DasSrcProp_title(p, sTitle, sizeof(sTitle))));

			size_t uEnum = pC ? DasSrcCtrl_enumCount(pC) : 0;
			mxArray* pVals = mxCreateCellMatrix(1, uEnum);
			for(size_t e = 0; e < uEnum; ++e) mxSetCell(pVals, e, mx_str(DasSrcCtrl_enum(pC, e)));
			mxSetField(pA, r, "values", pVals);
			const char* sLo = "";
			const char* sHi = "";
			if(pC) DasSrcCtrl_range(pC, &sLo, &sHi);
			mxSetField(pA, r, "min", mx_str(sLo));
			mxSetField(pA, r, "max", mx_str(sHi));
		}
	}
	return pA;
}

/* Examples as label plus the KEY=VALUE list that reproduces each, so a
 * MATLAB caller can start from one and change a key */
static mxArray* examplesToMx(const DasSrc* pSrc)
{
	size_t uEx = DasSrc_exampleCount(pSrc);
	const char* aF[] = {"label", "keys"};
	mxArray* pA = mxCreateStructMatrix(uEx, 1, 2, aF);
	char sArg[1024];
	for(size_t u = 0; u < uEx; ++u){
		mxSetField(pA, u, "label", mx_str(DasSrc_exampleLabel(pSrc, u)));
		DasReq* pReq = DasSrc_newReq(pSrc, (int)u);
		size_t n = pReq ? DasReq_count(pReq) : 0;
		size_t nRaw = pReq ? DasReq_rawCount(pReq) : 0;
		mxArray* pK = mxCreateCellMatrix(1, n + nRaw);
		for(size_t i = 0; i < n; ++i){
			snprintf(sArg, sizeof(sArg), "%s=%s", DasReq_key(pReq, i), DasReq_val(pReq, i));
			mxSetCell(pK, i, mx_str(sArg));
		}
		/* Raw parameters keep the das_fetch "-p" spelling so they round trip
		   through das3.url(..., keys{:}) unchanged */
		for(size_t i = 0; i < nRaw; ++i){
			const char* sVal = NULL;
			const char* sName = DasReq_raw(pReq, i, &sVal);
			snprintf(sArg, sizeof(sArg), "-p %s=%s", sName, sVal ? sVal : "");
			mxSetCell(pK, n + i, mx_str(sArg));
		}
		if(pReq) del_DasReq(pReq);
		mxSetField(pA, u, "keys", pK);
	}
	return pA;
}

static mxArray* srcToMx(const DasSrc* pSrc)
{
	const char* aF[] = {
		"name", "title", "summary", "version", "uri", "url", "servers",
		"authrequired", "authrealm", "formats", "mime", "examples",
		"coords", "data", "options"
	};
	mxArray* pS = mx_struct(sizeof(aF)/sizeof(aF[0]), aF);
	mxSetField(pS, 0, "name", mx_str(DasSrc_name(pSrc)));
	mxSetField(pS, 0, "title", mx_str(DasSrc_title(pSrc)));
	mxSetField(pS, 0, "summary", mx_str(DasJdo_string(DasSrc_jdo(pSrc, D2FRAG_DESC))));
	mxSetField(pS, 0, "version", mx_str(DasSrc_version(pSrc)));
	mxSetField(pS, 0, "uri", mx_str(DasSrc_path(pSrc)));
	mxSetField(pS, 0, "url", mx_str(DasSrc_url(pSrc)));

	size_t uBases = DasStreamSrc_urlCount(pSrc);
	mxArray* pB = mxCreateCellMatrix(1, uBases);
	for(size_t u = 0; u < uBases; ++u) mxSetCell(pB, u, mx_str(DasStreamSrc_baseUrl(pSrc, u)));
	mxSetField(pS, 0, "servers", pB);

	mxSetField(pS, 0, "authrequired", mxCreateLogicalScalar(DasSrc_needsAuth(pSrc)));
	mxSetField(pS, 0, "authrealm", mx_str(DasSrc_authRealm(pSrc)));
	mxSetField(pS, 0, "mime", mx_str(DasSrc_mime(pSrc, NULL)));
	mxSetField(pS, 0, "examples", examplesToMx(pSrc));
	mxSetField(pS, 0, "coords", sectionToMx(pSrc, das_src_sec_coord));
	mxSetField(pS, 0, "data", sectionToMx(pSrc, das_src_sec_data));
	mxSetField(pS, 0, "formats", sectionToMx(pSrc, das_src_sec_format));
	mxSetField(pS, 0, "options", sectionToMx(pSrc, das_src_sec_option));
	return pS;
}

mxArray* mxsrc_source(const char* sPath, const char* sAccept)
{
	DasCat* pCat = das3mex_cat();
	if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");
	DasSrc* pSrc = pickSrc(pCat, sPath, sAccept);
	if(pSrc == NULL) das3mex_raise(ERRID_NODE, NULL);
	mxArray* pS = srcToMx(pSrc);
	del_DasSrc(pSrc);
	return pS;
}

/* ************************************************************************* */
/* Requisitions from MATLAB arguments
 *
 * keys: a cellstr of das_fetch style arguments, already normalized by
 *       +das3/private/reqargs.m: "KEY=VALUE", bare "KEY" (true), bare "N"
 *       or "examples.N" (start from example N), "-p NAME=VALUE" (raw).
 * opts: a struct with optional fields accept (char), timeout (double),
 *       quiet (logical). */

static DasReq* mkReq(const DasSrc* pSrc, const mxArray* pKeys, bool bQuiet)
{
	size_t uKeys = (pKeys && mxIsCell(pKeys)) ? mxGetNumberOfElements(pKeys) : 0;
	if((pKeys != NULL) && !mxIsCell(pKeys) && !mxIsEmpty(pKeys))
		mexErrMsgIdAndTxt(ERRID_ARG, "keys must be a cell array of KEY=VALUE strings");

	/* Example selectors come out first, the example is the base the other
	   keys modify.  Same rule as das_fetch. */
	int nExample = DASSRC_BLANK_REQ;
	size_t uSettings = 0, uRaw = 0;
	char sArg[1024];
	for(size_t u = 0; u < uKeys; ++u){
		mx_cellStr(pKeys, u, sArg, sizeof(sArg));
		if(strncmp(sArg, "-p ", 3) == 0){ ++uRaw; continue; }
		const char* sNum = sArg;
		if(strchr(sArg, '=') == NULL){
			const char* pDot = strchr(sArg, '.');
			if(pDot != NULL){
				size_t uLen = (size_t)(pDot - sArg);
				bool bSec = ((uLen == 7) && (strncmp(sArg, "example", 7) == 0)) ||
				            ((uLen == 8) && (strncmp(sArg, "examples", 8) == 0));
				if(bSec){
					if(!isDigits(pDot + 1)){
						daslog_error_v("Examples are numbered, there is no '%s'", sArg);
						return NULL;
					}
					sNum = pDot + 1;
				}
			}
			if(isDigits(sNum)){ nExample = atoi(sNum); continue; }
		}
		++uSettings;
	}

	/* Nothing asked for at all runs the first example, so a bare
	   das3.fetch(PATH) always shows something */
	if((nExample < 0) && (uSettings == 0) && (uRaw == 0) && (DasSrc_exampleCount(pSrc) > 0)){
		nExample = 0;
		if(!bQuiet)
			mexPrintf("das3: no query given, using example 0, \"%s\"\n", DasSrc_exampleLabel(pSrc, 0));
	}

	DasReq* pReq = DasSrc_newReq(pSrc, nExample);
	if(pReq == NULL) return NULL;

	for(size_t u = 0; u < uKeys; ++u){
		mx_cellStr(pKeys, u, sArg, sizeof(sArg));
		if(strncmp(sArg, "-p ", 3) == 0){
			char* pEq = strchr(sArg + 3, '=');
			if(pEq == NULL){
				daslog_error_v("Raw parameter '%s' needs NAME=VALUE", sArg + 3);
				del_DasReq(pReq);
				return NULL;
			}
			*pEq = '\0';
			DasReq_setRaw(pReq, sArg + 3, pEq + 1);
			continue;
		}
		char* pEq = strchr(sArg, '=');
		if(pEq == NULL){
			if(isDigits(sArg)) continue;
			const char* pDot = strchr(sArg, '.');
			if(pDot && isDigits(pDot + 1) && (strncmp(sArg, "example", 7) == 0)) continue;
			DasReq_set(pReq, sArg, "true");   /* bare KEY toggles a boolean on */
			continue;
		}
		*pEq = '\0';
		DasReq_set(pReq, sArg, pEq + 1);
	}
	return pReq;
}

/* The URL for base uBase, 0 on failure.  Too small a buffer is the one
 * failure the library does not log. */
static size_t reqUrl(const DasSrc* pSrc, DasReq* pReq, size_t uBase, char* sUrl, size_t uLen)
{
	size_t uNeed = DasStreamSrc_url(pSrc, pReq, uBase, sUrl, uLen);
	if(uNeed >= uLen){
		daslog_error("Query URL too long");
		return 0;
	}
	return uNeed;
}

/* das3mex('explain', path, keys, opts)
 *
 * What a query resolves to, in interface terms: the source, the keys as
 * the library read them with a verdict on each, and the media type the
 * answer would have.  Protocol parameters and the URL are not reported.
 * Anything easy to see ends up in end user code, and the catalog exists
 * so that data can move between servers without that code noticing.
 * The URL goes to the debug log for whoever goes looking. */

static const char* verdictName(das_req_verdict_e v)
{
	switch(v){
	case das_req_ok:  return "ok";
	case das_req_bad: return "bad";
	default:          return "unchecked";
	}
}

static mxArray* reqToMx(const DasReq* pReq)
{
	const char* aK[] = {"key", "value", "verdict", "why", "derived"};
	size_t uAll = DasReq_count(pReq);
	/* A blank requisition lists every required key with an empty value as
	   a form aid.  After a check those that stayed empty carry a bad verdict
	   and a reason, which is worth showing; the rest are placeholders. */
	size_t uKeys = 0;
	for(size_t u = 0; u < uAll; ++u){
		const char* sVal = DasReq_val(pReq, u);
		if((sVal && sVal[0]) || (DasReq_verdict(pReq, u) == das_req_bad)) ++uKeys;
	}
	mxArray* pK = mxCreateStructMatrix(uKeys, 1, 5, aK);
	for(size_t u = 0, k = 0; u < uAll; ++u){
		const char* sVal = DasReq_val(pReq, u);
		das_req_verdict_e v = DasReq_verdict(pReq, u);
		if(!((sVal && sVal[0]) || (v == das_req_bad))) continue;
		mxSetField(pK, k, "key", mx_str(DasReq_key(pReq, u)));
		mxSetField(pK, k, "value", mx_str(sVal));
		mxSetField(pK, k, "verdict", mx_str(verdictName(v)));
		mxSetField(pK, k, "why", mx_str((v == das_req_bad) ? DasReq_why(pReq, u) : ""));
		mxSetField(pK, k, "derived", mxCreateLogicalScalar(DasReq_derived(pReq, u)));
		++k;
	}
	return pK;
}

mxArray* mxsrc_explain(const char* sPath, const mxArray* pKeys, const mxArray* pOpts)
{
	DasCat* pCat = das3mex_cat();
	if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");
	char sAccept[256];
	mx_fieldStr(pOpts, "accept", sAccept, sizeof(sAccept));
	bool bQuiet = mx_fieldBool(pOpts, "quiet", das3mex_quiet());

	DasSrc* pSrc = pickSrc(pCat, sPath, sAccept[0] ? sAccept : NULL);
	if(pSrc == NULL) das3mex_raise(ERRID_NODE, NULL);
	DasReq* pReq = mkReq(pSrc, pKeys, bQuiet);
	if(pReq == NULL){ del_DasSrc(pSrc); das3mex_raise(ERRID_QUERY, NULL); }

	/* A dry run reports problems instead of raising them, that is what it
	   is for.  The check marks every entry; the URL build below is only so
	   that the debug log has it. */
	size_t uBad = 0;
	DasSrc_check(pSrc, pReq, &uBad);
	if(uBad == 0){
		char sUrl[DAS3MEX_URL_SZ];
		if(DasStreamSrc_url(pSrc, pReq, 0, sUrl, sizeof(sUrl)) < sizeof(sUrl))
			daslog_debug_v("Request URL: %s", sUrl);
	}

	/* Raw parameters are the user's own protocol-level words, echoed as
	   given since they typed them */
	const char* aR[] = {"name", "value"};
	size_t uRaw = DasReq_rawCount(pReq);
	mxArray* pRaw = mxCreateStructMatrix(uRaw, 1, 2, aR);
	for(size_t u = 0; u < uRaw; ++u){
		const char* sVal = NULL;
		const char* sName = DasReq_raw(pReq, u, &sVal);
		mxSetField(pRaw, u, "name", mx_str(sName));
		mxSetField(pRaw, u, "value", mx_str(sVal));
	}

	/* A root node has no catalog URI, its location stands in */
	const char* sSrcUri = DasSrc_path(pSrc);
	if((sSrcUri == NULL) || (sSrcUri[0] == '\0')) sSrcUri = DasSrc_url(pSrc);

	const char* aF[] = {"source", "label", "mime", "ok", "keys", "raw"};
	mxArray* pS = mx_struct(sizeof(aF)/sizeof(aF[0]), aF);
	mxSetField(pS, 0, "source", mx_str(sSrcUri));
	mxSetField(pS, 0, "label", mx_str(DasReq_label(pReq)));
	mxSetField(pS, 0, "mime", mx_str(DasSrc_mime(pSrc, pReq)));
	mxSetField(pS, 0, "ok", mxCreateLogicalScalar(uBad == 0));
	mxSetField(pS, 0, "keys", reqToMx(pReq));
	mxSetField(pS, 0, "raw", pRaw);
	/* Drop the error text the check may have logged; verdicts carry it */
	del_DasReq(pReq);
	del_DasSrc(pSrc);
	return pS;
}

/* ************************************************************************* */
/* das3mex('query', path, keys, opts, outfile)
 *
 * The body goes to disk untouched, whatever it is.  This is das_fetch's
 * 'data' command and exists so that sources, requisitions and logins can
 * be exercised before the dataset conversion exists, and afterwards for
 * CDF and CSV formats that the in-memory path does not read.  The URL is
 * not reported back, see mxsrc_explain(). */

mxArray* mxsrc_query(
	const char* sPath, const mxArray* pKeys, const mxArray* pOpts, const char* sOut
){
	DasCat* pCat = das3mex_cat();
	if(pCat == NULL) das3mex_raise(ERRID_INTERN, "catalog session");
	char sAccept[256];
	mx_fieldStr(pOpts, "accept", sAccept, sizeof(sAccept));
	bool bQuiet = mx_fieldBool(pOpts, "quiet", das3mex_quiet());
	float rTimeout = (float)mx_fieldNum(pOpts, "timeout", das3mex_timeout());

	DasSrc* pSrc = pickSrc(pCat, sPath, sAccept[0] ? sAccept : NULL);
	if(pSrc == NULL) das3mex_raise(ERRID_NODE, NULL);
	DasReq* pReq = mkReq(pSrc, pSrc ? pKeys : NULL, bQuiet);
	if(pReq == NULL){ del_DasSrc(pSrc); das3mex_raise(ERRID_QUERY, NULL); }

	FILE* pFile = fopen(sOut, "wb");
	if(pFile == NULL){
		del_DasReq(pReq); del_DasSrc(pSrc);
		mexErrMsgIdAndTxt(ERRID_IO, "Couldn't open %s for writing", sOut);
	}

	/* Each base URL in turn until one answers.  A server that answers with
	   an error is an answer; only connection failures move on. */
	char sUrl[DAS3MEX_URL_SZ];
	char sSrcUri[1024];
	const char* sPathOrUrl = DasSrc_path(pSrc);
	if((sPathOrUrl == NULL) || (sPathOrUrl[0] == '\0')) sPathOrUrl = DasSrc_url(pSrc);
	strncpy(sSrcUri, sPathOrUrl, sizeof(sSrcUri) - 1);
	sSrcUri[sizeof(sSrcUri) - 1] = '\0';
	char sMime[256] = {'\0'};
	int64_t nTotal = 0;
	int nCode = 0;
	const char* sErrId = NULL;
	size_t uBases = DasStreamSrc_urlCount(pSrc);
	for(size_t u = 0; ; ++u){
		if(reqUrl(pSrc, pReq, u, sUrl, sizeof(sUrl)) == 0){ sErrId = ERRID_QUERY; break; }
		if(!bQuiet) mexPrintf("das3: querying %s\n", DasSrc_path(pSrc));
		daslog_debug_v("Request URL: %s", sUrl);

		DasHttpResp res;
		memset(&res, 0, sizeof(DasHttpResp));
		if(!das_http_getBody(sUrl, DasCat_agent(pCat), DasCat_credMngr(pCat), &res, rTimeout)){
			nCode = res.nCode;
			sErrId = ERRID_SERVER;
			if((nCode == 401)||(nCode == 403)) sErrId = ERRID_AUTH;
			else if(nCode == 400) sErrId = ERRID_QUERY;
			daslog_error_v("%s", res.sError ? res.sError : "Request failed");
			DasHttpResp_freeFields(&res);
			/* Only a connection failure (no status at all) tries a mirror */
			if((nCode == 0) && (u + 1 < uBases)){
				if(!bQuiet) mexPrintf("das3: %s\n", das3mex_lastErr());
				daslog_warn_v("Trying alternate server %s", DasStreamSrc_baseUrl(pSrc, u + 1));
				sErrId = NULL;
				continue;
			}
			break;
		}
		nCode = res.nCode;
		if(res.pMime) strncpy(sMime, res.pMime, sizeof(sMime) - 1);

		DasErrCode nRet = das_http_copyBody(&res, pFile, &nTotal, NULL, NULL);
		DasHttpResp_freeFields(&res);
		if(nRet == DASERR_IO) sErrId = ERRID_IO;
		else if(nRet != DAS_OKAY) sErrId = ERRID_SERVER;
		break;
	}
	fclose(pFile);
	del_DasReq(pReq);
	del_DasSrc(pSrc);
	if(sErrId != NULL) das3mex_raise(sErrId, NULL);

	const char* aF[] = {"source", "file", "mime", "bytes", "status"};
	mxArray* pS = mx_struct(sizeof(aF)/sizeof(aF[0]), aF);
	mxSetField(pS, 0, "source", mx_str(sSrcUri));
	mxSetField(pS, 0, "file", mx_str(sOut));
	mxSetField(pS, 0, "mime", mx_str(sMime));
	mxSetField(pS, 0, "bytes", mxCreateDoubleScalar((double)nTotal));
	mxSetField(pS, 0, "status", mxCreateDoubleScalar((double)nCode));
	return pS;
}
