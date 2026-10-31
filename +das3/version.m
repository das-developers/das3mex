function [sVer] = version()
% DAS3.VERSION  Version of the das3mex binding and the das2C library behind it
%
%   das3.version          prints both
%   v = das3.version      returns a struct with fields das3mex and das2C

	s = das3mex('version');
	if nargout == 0
		fprintf('das3mex %s (das2C %s)\n', s.das3mex, s.das2C);
	else
		sVer = s;
	end
end
