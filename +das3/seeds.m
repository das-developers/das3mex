function [lUrls] = seeds()
% DAS3.SEEDS  The compiled in URLs of the das catalog index and its mirrors
%
% The first one that answers is used.  das3.config('start', ...) replaces
% it for a session.

	l = das3mex('seeds');
	if nargout == 0
		fprintf('%s\n', l{:});
	else
		lUrls = l;
	end
end
