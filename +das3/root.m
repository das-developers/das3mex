function [sRoot] = root(sUrl, varargin)
% DAS3.ROOT  Set or show the catalog root, the node relative paths start at
%
%   das3.root                       show the root in use
%   r = das3.root                   return it
%   das3.root(URL)                  a node by URL (or a local file) becomes
%                                   the root for this MATLAB session
%   das3.root(URL, 'as', URI)       the same, and declare which catalog
%                                   name that node stands for
%   das3.root('default')            back to the federated das catalog
%
% The root is a seed: paths given to das3.node, das3.source and das3.query
% are relative to it, and nothing outside it can be reached.  A project
% can therefore publish its own root.json and have users point at it
% instead of walking the federated catalog from das2.org:
%
%   das3.root('https://space.physics.uiowa.edu/das/stream/source/tracers.json')
%   das3.node('tracers-2/spice')
%
% A URL without a scheme is taken to be https.  'as' gives the node its
% catalog URI so that paths saved with the full name
% (tag:das2.org,2012:site:/...) still resolve under it.
%
% The root is sticky for the MATLAB session and nowhere else: not saved
% in preferences, not read from the environment.  Two people trading a
% script see the same paths resolve the same way because the root they
% use is the one the script set, in a line they can both read.
%
% See also das3.config, das3.seeds, das3.node

	s = das3mex('config');

	if nargin == 0
		if nargout > 0
			sRoot = currentRoot(s);
		else
			fprintf('%s\n', currentRoot(s));
			if isempty(s.start)
				fprintf('(the federated das catalog, prefix tag:das2.org,2012:site:/)\n');
			end
		end
		return
	end

	p = inputParser;
	p.addParameter('as', '', @(x) ischar(x) || isstring(x));
	p.parse(varargin{:});
	sAs = char(p.Results.as);
	sUrl = char(sUrl);

	if any(strcmpi(sUrl, {'default', ''}))
		sStart = '';
	else
		sStart = withScheme(sUrl);
		if ~isempty(sAs)
			sStart = [sStart '=' sAs];
		end
	end

	s = das3.config('start', sStart);
	if nargout > 0
		sRoot = currentRoot(s);
	elseif isempty(sStart)
		fprintf('Catalog root: the federated das catalog\n');
	else
		fprintf('Catalog root: %s\n', sStart);
	end
end

function sRoot = currentRoot(s)
	if ~isempty(s.start)
		sRoot = s.start;
	else
		l = das3mex('seeds');
		sRoot = l{1};
	end
end

function sUrl = withScheme(sUrl)
% A bare host/path is meant as https; an existing file is left alone
	if contains(sUrl, '://') || startsWith(sUrl, {'/', './', '../'}) || isfile(sUrl)
		return
	end
	if startsWith(sUrl, 'tag:') || startsWith(sUrl, 'site:') || startsWith(sUrl, 'doi:')
		return    % a catalog URI prefix, das3.config handles it as such
	end
	sUrl = ['https://' sUrl];
end
