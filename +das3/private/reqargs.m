function [lKeys, sOpts, sLocal] = reqargs(varargin)
% REQARGS  Turn MATLAB style query arguments into das_fetch style strings
%
%   [lKeys, sOpts] = reqargs(varargin{:})
%
% The das2C library resolves query keys itself (time=A,B, lshell, csv,
% coords.time.min and so on, see 'das3.help keys'), so the job here is only
% to flatten MATLAB values into the KEY=VALUE text das_fetch users type.
% The accepted forms are:
%
%   'KEY=VALUE'              passed through untouched
%   'KEY', VALUE             a name/value pair.  VALUE may be:
%                              char or string        KEY=VALUE
%                              numeric scalar        KEY=number (%.17g)
%                              logical               KEY=true or KEY=false
%                              datetime              KEY=yyyy-mm-ddTHH:MM:SS.FFF (UTC)
%                              cell or array of the above, joined with commas,
%                              which is how coordinate ranges are written:
%                              'time', {'2021-02-21', '2021-02-22', 60}
%   'KEY'  (as the last argument, or followed by another KEY=VALUE)
%                            a bare toggle, the same as 'KEY=true'
%   N  (a number on its own) start from example N of the source
%
% These names are taken out as options instead of keys.  sOpts goes to the
% mex file: 'accept', 'timeout', 'quiet'.  sLocal stays in MATLAB: 'file',
% 'dryrun'.  'example', N becomes the key "N".  To send a protocol
% parameter straight to the server bypassing the interface use
% 'raw', {'name', 'value'} (the das_fetch -p option).
%
% A bare key in the middle of the list swallows the next argument as its
% value, unless that argument is KEY=VALUE text or a reserved option name.
% Write 'flag=true' or 'flag', true for a toggle followed by another bare
% key.

	lKeys = {};
	sOpts = struct('accept', '', 'timeout', [], 'quiet', []);
	sLocal = struct('file', '', 'dryrun', false);
	lOptNames = {'accept', 'timeout', 'quiet', 'example', 'raw', 'file', 'dryrun'};

	i = 1;
	n = numel(varargin);
	while i <= n
		a = varargin{i};

		% A lone number is an example selector
		if isnumeric(a) || islogical(a)
			if ~isscalar(a)
				error('das3:argcheck', 'Argument %d: a number on its own selects an example, use a scalar', i);
			end
			lKeys{end+1} = sprintf('%d', round(double(a))); %#ok<AGROW>
			i = i + 1;
			continue
		end

		if ~(ischar(a) || (isstring(a) && isscalar(a)))
			error('das3:argcheck', 'Argument %d: expected a key name or KEY=VALUE text', i);
		end
		a = char(a);

		% Already in das_fetch form
		if contains(a, '=')
			lKeys{end+1} = a; %#ok<AGROW>
			i = i + 1;
			continue
		end

		% A bare key: a toggle if last, or if the next item is itself
		% KEY=VALUE, or if the next item is one of the reserved option names
		bLast = (i == n);
		bNextIsKv = false;
		bNextIsOpt = false;
		if ~bLast && (ischar(varargin{i+1}) || isstring(varargin{i+1}))
			sNext = char(varargin{i+1});
			bNextIsKv = contains(sNext, '=');
			bNextIsOpt = any(strcmpi(sNext, lOptNames));
		end
		if bLast || bNextIsKv || bNextIsOpt
			if any(strcmpi(a, lOptNames))
				error('das3:argcheck', 'Option ''%s'' needs a value', a);
			end
			lKeys{end+1} = a; %#ok<AGROW>
			i = i + 1;
			continue
		end

		v = varargin{i+1};
		i = i + 2;

		switch lower(a)
		case 'accept'
			sOpts.accept = char(strjoin(string(v), ','));
		case 'timeout'
			sOpts.timeout = double(v);
		case 'quiet'
			sOpts.quiet = logical(v);
		case 'example'
			lKeys{end+1} = sprintf('%d', round(double(v))); %#ok<AGROW>
		case 'file'
			sLocal.file = char(v);
		case 'dryrun'
			sLocal.dryrun = logical(v);
		case 'raw'
			% {'name', 'value'} or {'n1','v1'; 'n2','v2'} or 'name=value'
			if ischar(v) || isstring(v)
				lKeys{end+1} = ['-p ' char(v)]; %#ok<AGROW>
			elseif iscell(v)
				if isvector(v) && numel(v) == 2 && ~iscell(v{1})
					v = v(:)';
				end
				for r = 1:size(v, 1)
					lKeys{end+1} = sprintf('-p %s=%s', char(v{r,1}), valstr(v{r,2})); %#ok<AGROW>
				end
			else
				error('das3:argcheck', 'raw takes ''name=value'' or a cell of {name, value}');
			end
		otherwise
			lKeys{end+1} = [a '=' valstr(v)]; %#ok<AGROW>
		end
	end
end

function s = valstr(v)
% One value, or a list of values comma joined
	if iscell(v)
		parts = cellfun(@valstr, v, 'UniformOutput', false);
		s = strjoin(parts, ',');
		return
	end
	if ischar(v)
		s = v;
	elseif isstring(v)
		s = char(strjoin(v, ','));
	elseif isa(v, 'datetime')
		% The library speaks UTC.  A zoned datetime is converted, an unzoned
		% one is taken as UTC already, which is the das convention.
		if ~isempty(v.TimeZone)
			v.TimeZone = 'UTC';
		end
		s = char(strjoin(string(v, 'yyyy-MM-dd''T''HH:mm:ss.SSS'), ','));
	elseif islogical(v)
		parts = repmat({'false'}, 1, numel(v));
		parts(v(:)') = {'true'};
		s = strjoin(parts, ',');
	elseif isnumeric(v)
		s = strjoin(arrayfun(@(x) sprintf('%.17g', x), double(v(:)'), 'UniformOutput', false), ',');
	else
		error('das3:argcheck', 'Cannot turn a %s into a query value', class(v));
	end
end
