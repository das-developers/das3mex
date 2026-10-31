function [sResult] = query(sPath, varargin)
% DAS3.QUERY  Ask a data source for data
%
%   das3.query(PATH, KEY, VALUE, ...)              (in memory: phase 2, not yet)
%   info = das3.query(PATH, ..., 'file', FILE)     response body to a file
%   info = das3.query(PATH, ..., 'dryrun', true)   resolve only, no network
%
% PATH is a catalog path, see das3.node.  With no keys at all the source's
% example 0 is run, so a bare das3.query(PATH) always yields something.
%
% Keys are the source's interface keys, listed by das3.source(PATH), and
% the library accepts these shortcut spellings:
%
%   'time', {A, B}        a coordinate range: minimum and maximum, and a
%                         third item sets the resolution or interval,
%                         whichever the source has.  A and B may be text or
%                         datetime; an empty item skips that bound
%   'time', A             the span A denotes: a day, an hour, a year
%   'time.min', A         one property, section prefix optional
%   'lshell'              a bare key toggles a boolean on (as the last
%                         argument; write 'lshell=true' before other keys)
%   'csv'                 select an output format group
%   'KEY=VALUE'           das_fetch spelling, passed through
%   'example', N  or  N   start from the maintainer's example N
%
% Options, taken out of the key list:
%
%   'file', FILE          write the response body to FILE untouched: a das
%                         stream, CSV, CDF or raw packets, whatever the
%                         source produces.  Returns a struct with source,
%                         file, mime, bytes, status.
%   'dryrun', true        do not contact the server.  Returns what the
%                         query resolved to: source URI, example label,
%                         the media type it would produce, and the keys as
%                         the source read them, each with a verdict and,
%                         when bad, the reason.  A dry run reports problems
%                         instead of raising them.
%   'accept', TYPES       when PATH is a collection, the media types you
%                         can read, default das streams in any form
%   'raw', {NAME, VALUE}  a protocol parameter sent as is, bypassing the
%                         interface.  Ties the script to one server's API;
%                         prefer keys
%   'timeout', SEC        connection timeout for this request
%   'quiet', true         no progress messages
%
% A password prompt appears if the source needs one and none was given
% with das3.login, unless das3.config('prompt', false).
%
% See also das3.node, das3.source, das3.login, das3.config

	[lKeys, sOpts, sLocal] = reqargs(varargin{:});

	if sLocal.dryrun
		sResult = das3mex('explain', char(sPath), lKeys, sOpts);
		return
	end
	if ~isempty(sLocal.file)
		sResult = das3mex('query', char(sPath), lKeys, sOpts, sLocal.file);
		return
	end
	error('das3:notimp', ['Results in memory are not implemented yet; ' ...
		'use ''file'', NAME to save the response, or ''dryrun'', true']);
end
