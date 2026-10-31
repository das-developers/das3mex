function [sUser, sPass] = credprompt(sServer, sRealm, sDataset, sMsg)
% CREDPROMPT  Ask the user for a login, called from the das3mex mex file
%
% Returns empty strings when the user cancels.  With a desktop a dialog is
% used, without one the command window.  Neither hides the password while
% it is typed: MATLAB has no masked input and the java tricks that provide
% one are fragile across releases.  Use das3.login beforehand to avoid the
% prompt in scripts.

	sUser = '';
	sPass = '';
	if nargin < 4, sMsg = ''; end
	if isempty(sRealm), sRealm = sServer; end

	sTitle = sprintf('Login for %s', sRealm);
	sLine = sprintf('Server: %s', sServer);
	if ~isempty(sDataset), sLine = sprintf('%s, dataset %s', sLine, sDataset); end
	if ~isempty(sMsg), sLine = sprintf('%s\n%s', sMsg, sLine); end

	if usejava('desktop')
		answer = inputdlg({[sLine newline 'Username:'], 'Password:'}, sTitle, [1 60; 1 60]);
		if isempty(answer), return; end
		sUser = strtrim(answer{1});
		sPass = answer{2};
	else
		fprintf('%s\n%s\n', sTitle, sLine);
		sUser = strtrim(input('Username: ', 's'));
		if isempty(sUser), return; end
		sPass = input('Password: ', 's');
	end
end
