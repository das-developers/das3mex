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

/* Shared declarations for the das3mex MEX gateway.
 *
 * das3mex is the ONE mex file behind the +das3 package.  The pure MATLAB
 * functions in +das3 call das3mex('command', ...) and never touch das2C
 * objects.  Everything that persists between calls (the catalog session,
 * credentials, settings) is static in das3mex.c and reached through the
 * accessors below.
 */

#ifndef _das3mex_h_
#define _das3mex_h_

#include <stdbool.h>
#include <stddef.h>
#include <mex.h>

#include <das3/core.h>
#include <das3/catalog.h>
#include <das3/source.h>
#include <das3/req.h>
#include <das3/http.h>
#include <das3/credentials.h>

#define DAS3MEX_VERSION "0.1.0"
#define DAS3MEX_AGENT   "das3mex/" DAS3MEX_VERSION

/* MATLAB error identifiers.  One per failure class so scripts can catch
 * them by prefix (das3:*) or by kind. */
#define ERRID_ARG    "das3:argcheck"
#define ERRID_NODE   "das3:node"       /* path did not resolve */
#define ERRID_QUERY  "das3:query"      /* bad requisition */
#define ERRID_SERVER "das3:server"     /* network or server failure */
#define ERRID_AUTH   "das3:auth"
#define ERRID_IO     "das3:io"
#define ERRID_INTERN "das3:internal"

/* What this binding can read: das3 streams, binary or text, and das2
 * streams, which the builder up-converts.  Asked for one at a time in
 * this order, the first type any source provides settles it, then
 * anything, which is still downloadable to disk.  Same policy as
 * das_fetch.  The das3 names are the family settled on 2026-10-31:
 * application/vnd.das.stream (binary packets), text/vnd.das.stream (all
 * UTF-8), and application/vnd.das.doc+xml for the single-document form,
 * which is left out here until the builder reads it. */
#define DEF_ACCEPT_DAS3_BIN  "application/vnd.das.stream"
#define DEF_ACCEPT_DAS3_TEXT "text/vnd.das.stream"
#define DEF_ACCEPT_DAS2_BIN  "application/vnd.das2.das2stream"
#define DEF_ACCEPT_DAS2_TEXT "text/vnd.das2.das2stream"

#define DAS3MEX_MAX_MOUNTS 16
#define DAS3MEX_MAX_KIDS   32
#define DAS3MEX_URL_SZ   4096

/* ************************************************************************* */
/* das3mex.c: session, errors, small mx helpers */

/* The catalog session, built on first use after any settings change.
 * NULL only if construction failed, which has already been reported. */
DasCat* das3mex_cat(void);

/* Settings that are not catalog policy */
float das3mex_timeout(void);
bool  das3mex_quiet(void);

/* Raise a MATLAB error carrying the last das2C error text.  Never returns.
 * Free anything you own before calling this, mexErrMsgIdAndTxt longjmps
 * out of the mex function. */
void das3mex_raise(const char* sId, const char* sContext);

/* The last error level message the das2C log handler saw, or the saved
 * das_error() text, or "" */
const char* das3mex_lastErr(void);

/* mxCreateString that tolerates NULL (-> "") */
mxArray* mx_str(const char* s);

/* A 1x1 struct with the given field names, all fields initially empty */
mxArray* mx_struct(int nFields, const char** aFields);

/* Copy a MATLAB char or string scalar into a NUL terminated buffer.
 * Returns false (and sets sBuf to "") if the array is not text. */
bool mx_toStr(const mxArray* p, char* sBuf, size_t uLen);

/* A cellstr element as text, "" if missing or not text */
bool mx_cellStr(const mxArray* pCell, size_t i, char* sBuf, size_t uLen);

/* A struct field as text/number/logical, with a default when absent */
bool   mx_fieldStr(const mxArray* pS, const char* sField, char* sBuf, size_t uLen);
double mx_fieldNum(const mxArray* pS, const char* sField, double rDef);
bool   mx_fieldBool(const mxArray* pS, const char* sField, bool bDef);

/* ************************************************************************* */
/* mx_src.c: catalog nodes, sources, requisitions, URLs, downloads */

/* das3mex('node', path) */
mxArray* mxsrc_node(const char* sPath);

/* das3mex('source', path, accept) */
mxArray* mxsrc_source(const char* sPath, const char* sAccept);

/* das3mex('explain', path, keys, opts): what a query resolves to, without
 * contacting the server */
mxArray* mxsrc_explain(const char* sPath, const mxArray* pKeys, const mxArray* pOpts);

/* das3mex('query', path, keys, opts, outfile): run it, body to a file */
mxArray* mxsrc_query(
	const char* sPath, const mxArray* pKeys, const mxArray* pOpts, const char* sOut
);

#endif /* _das3mex_h_ */
