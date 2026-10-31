function [sCfg] = config(varargin)
% DAS3.CONFIG  Get or change the catalog session settings
%
%   das3.config                      print the current settings
%   s = das3.config                  return them as a struct
%   das3.config(NAME, VALUE, ...)    change one or more
%   das3.config('reset')             drop the session, back to defaults
%
% Settings (compare the options of the das_fetch program):
%
%   'start'    Where relative paths start.  A catalog URI prefix, the
%              default is 'tag:das2.org,2012:site:/'; or a URL or file
%              holding a node which becomes the seed, a root, so nothing
%              outside it can be reached; or URL=URI to also declare what
%              catalog name that node stands for.
%   'mount'    URL=NAME, or {URL, NAME}: the node at URL stands in for NAME
%              and everything under it, the rest of the catalog is
%              unchanged.  Appends to the mount list.
%   'mounts'   An N x 2 cell array replacing the whole mount list, {} to
%              clear it.
%   'cache'    Directory for cached catalog nodes, 'none' for no cache, ''
%              for the default ~/.das/cache/nodes.
%   'fresh'    true to ignore cached nodes and fetch them again.
%   'agent'    The HTTP user agent string to send.
%   'timeout'  Connection timeout in seconds, default 10.
%   'quiet'    true to suppress informational messages.
%   'prompt'   false to never ask for a password (a 401 then just fails).
%   'loglevel' One of 'crit', 'error', 'warn', 'info', 'debug', 'trace'.
%              Default 'warn'.  'debug' includes the library's own view of
%              each request.
%
% Changing start, mounts, cache, fresh or agent discards the nodes loaded
% so far; the next catalog call reloads what it needs.

	if (nargin == 1) && (ischar(varargin{1}) || isstring(varargin{1})) ...
			&& strcmpi(char(varargin{1}), 'reset')
		das3mex('reset');
		s = das3mex('config');
		if nargout > 0, sCfg = s; end
		return
	end

	s = das3mex('config');
	if nargin == 0
		if nargout == 0
			prn(s);
		else
			sCfg = s;
		end
		return
	end

	if mod(nargin, 2) ~= 0
		error('das3:argcheck', 'das3.config takes NAME, VALUE pairs');
	end
	for i = 1:2:nargin
		sName = lower(char(varargin{i}));
		v = varargin{i+1};
		switch sName
		case 'start'
			s.start = char(v);
		case 'mount'
			if iscell(v)
				if numel(v) ~= 2
					error('das3:argcheck', 'mount takes {URL, NAME}');
				end
				s.mounts(end+1, :) = {char(v{1}), char(v{2})};
			else
				v = char(v);
				iEq = find(v == '=', 1, 'last');
				if isempty(iEq)
					error('das3:argcheck', 'mount takes URL=NAME');
				end
				s.mounts(end+1, :) = {v(1:iEq-1), v(iEq+1:end)};
			end
		case 'mounts'
			if isempty(v)
				s.mounts = cell(0, 2);
			elseif iscell(v) && (size(v, 2) == 2)
				s.mounts = cellfun(@char, v, 'UniformOutput', false);
			else
				error('das3:argcheck', 'mounts takes an N x 2 cell array of {URL, NAME}');
			end
		case 'cache'
			s.cache = char(v);
		case 'fresh'
			s.fresh = logical(v);
		case 'agent'
			s.agent = char(v);
		case 'timeout'
			s.timeout = double(v);
		case 'quiet'
			s.quiet = logical(v);
		case 'prompt'
			s.prompt = logical(v);
		case 'loglevel'
			s.loglevel = char(v);
		otherwise
			error('das3:argcheck', 'Unknown setting ''%s''', sName);
		end
	end
	s = das3mex('config', s);
	if nargout > 0
		sCfg = s;
	end
end

function prn(s)
	fprintf('start:     %s\n', ifempty(s.start, '(default, tag:das2.org,2012:site:/)'));
	if isempty(s.mounts)
		fprintf('mounts:    (none)\n');
	else
		for i = 1:size(s.mounts, 1)
			fprintf('mount:     %s = %s\n', s.mounts{i,1}, s.mounts{i,2});
		end
	end
	fprintf('cache:     %s\n', ifempty(s.cache, '(default, ~/.das/cache/nodes)'));
	fprintf('fresh:     %s\n', tf(s.fresh));
	fprintf('agent:     %s\n', s.agent);
	fprintf('timeout:   %g s\n', s.timeout);
	fprintf('quiet:     %s\n', tf(s.quiet));
	fprintf('prompt:    %s\n', tf(s.prompt));
	fprintf('loglevel:  %s\n', s.loglevel);
end

function s = ifempty(s, sDef)
	if isempty(s), s = sDef; end
end

function s = tf(b)
	if b, s = 'true'; else, s = 'false'; end
end
