function login(sServer, sRealm, sUser, sPass, varargin)
% DAS3.LOGIN  Supply a username and password for a data server
%
%   das3.login(SERVER, REALM, USER, PASS)
%   das3.login(SERVER, REALM, USER)           prompts for the password
%   das3.login(..., 'dataset', NAME)          only for that dataset parameter
%   das3.login(..., 'save', true)             also write ~/.das/servers
%
% SERVER is the service URL without query parameters, the way
% das3.source(PATH).servers lists it.  REALM is the HTTP realm the server
% announces, shown as authrealm by das3.source, or '' if the server has
% none.  Credentials live for the MATLAB session.  With 'save' they are
% written, base64 encoded but not encrypted, to the same file das_fetch
% reads, so command line and MATLAB use share logins.
%
% Without a login, a source that needs one triggers a prompt the first
% time it is used, unless das3.config('prompt', false).

	if nargin < 3
		error('das3:argcheck', 'das3.login needs at least SERVER, REALM and USER');
	end
	if nargin < 4 || isempty(sPass)
		sPass = input(sprintf('Password for %s at %s: ', char(sUser), char(sServer)), 's');
	end
	p = inputParser;
	p.addParameter('dataset', '', @(x) ischar(x) || isstring(x));
	p.addParameter('save', false, @islogical);
	p.parse(varargin{:});

	das3mex('cred', 'add', char(sServer), char(sRealm), char(p.Results.dataset), ...
		char(sUser), char(sPass));
	if p.Results.save
		n = das3mex('cred', 'save', '');
		fprintf('%d credential(s) saved to %s\n', n, fullfile(getenv('HOME'), '.das', 'servers'));
	end
end
