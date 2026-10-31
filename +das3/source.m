function [sSrc] = source(sPath, varargin)
% DAS3.SOURCE  Describe a data source: its examples and query keys
%
%   das3.source(PATH)                print the source's form
%   s = das3.source(PATH)            return it as a struct
%   das3.source(PATH, 'accept', T)   when PATH is a collection, pick the
%                                    child providing one of the media types
%                                    in T (char, comma separated, or cellstr)
%   das3.source(PATH, 'all', true)   also print fixed properties and the
%                                    formats section
%
% The struct fields:
%
%   name, title, summary, version, uri, url
%   servers       base URLs the source is served from, mirrors after the first
%   authrequired  true if the server wants a login (see das3.login)
%   authrealm     the HTTP realm it announces, if known
%   mime          the media type the default query produces
%   examples      struct array: label, and keys (the KEY=VALUE list that
%                 reproduces it, usable as das3.query(PATH, keys{:}))
%   coords, data, formats, options
%                 struct arrays, one row per property, with fields key,
%                 group, name, type, units, default, required, fixed,
%                 values (enum choices), min, max (a range), title
%
% Keys are what das3.query takes.  The section prefix may be
% dropped when unambiguous, so 'time.min' for 'coords.time.min', and a
% coordinate name alone takes a MIN,MAX[,STEP] range:
%
%   das3.query(PATH, 'time', {'2021-02-21', '2021-02-22'})
%
% See also das3.node, das3.query, das3.show

	p = inputParser;
	p.addParameter('accept', '', @(x) ischar(x) || isstring(x) || iscellstr(x));
	p.addParameter('all', false, @islogical);
	p.parse(varargin{:});
	sAccept = char(strjoin(string(p.Results.accept), ','));

	s = das3mex('source', char(sPath), sAccept);
	if nargout > 0
		sSrc = s;
		return
	end
	if p.Results.all
		s.showall = true;   % das3.show prints fixed properties and formats too
	end
	das3.show(s);
end
