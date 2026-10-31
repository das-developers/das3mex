function [sNode] = node(sPath)
% DAS3.NODE  Look at a catalog node: a catalog's children or a source's form
%
%   das3.node                 where paths start, the federated sites
%   das3.node(PATH)           print what is at PATH
%   s = das3.node(PATH)       return it as a struct instead
%
% PATH is a catalog path, not a web path.  Any of these forms work:
%
%   uiowa/juno/wav/survey             relative to the start prefix
%   site:/uiowa/juno/wav/survey       shorthand for the URI form below
%   tag:das2.org,2012:site:/uiowa     a full catalog path URI
%   https://host/some/node.json       a node by direct URL
%   ./local_node.json                 a node file on disk
%
% The struct has the node's type, name, title, uri, url and a children
% struct array with each child's name, type, title, whether this binding
% can load it, and the media types it provides.
%
% When PATH names a data source, or a collection of them, the printed form
% is that of das3.source: the examples and the query keys.  A collection
% shows the source das3.query would use; add a child's name to the path to
% see another.  das3.show prints a struct returned earlier.
%
% See also das3.source, das3.query, das3.show, das3.config

	if nargin < 1, sPath = ''; end
	s = das3mex('node', char(sPath));
	if nargout > 0
		sNode = s;
		return
	end

	bSrcSet = s.iscatalog && any(strcmp(s.type, {'SourceSet', 'Collection'}));
	if s.issource || bSrcSet
		try
			das3.source(sPath);
			return
		catch e
			% A collection with nothing readable: fall through to the listing,
			% the message says which children exist
			fprintf('%s\n\n', e.message);
		end
	end
	das3.show(s);
end
