function show(s)
% DAS3.SHOW  Print a catalog node, source or query result the readable way
%
%   das3.show(das3.node(PATH))
%   das3.show(das3.source(PATH))
%   das3.show(das3.query(PATH, ..., 'dryrun', true))
%
% The same text das3.node and das3.source print when called with no
% output, for a struct you already hold.  Unknown structs fall back to
% disp.

	if ~isstruct(s)
		disp(s);
		return
	end
	if isfield(s, 'children')
		prnNode(s);
	elseif isfield(s, 'examples')
		prnSource(s, isfield(s, 'showall') && s.showall);
	elseif isfield(s, 'keys') && isfield(s, 'ok')
		prnExplain(s);
	else
		disp(s);
	end
end

%% Catalog node ----------------------------------------------------------

function prnNode(s)
	sVer = s.version;
	if ~isempty(sVer), sVer = [' ' sVer]; end
	fprintf('%s%s:  %s\n', s.type, sVer, s.label);
	if ~isempty(s.title), fprintf('Title:    %s\n', s.title); end
	fprintf('URI:      %s%s\n', s.uri, s.separator);

	n = numel(s.children);
	if n == 0
		fprintf('\nNo children listed.\n');
		return
	end
	wName = max(4, max(cellfun(@length, {s.children.name})));
	wType = max(4, max(cellfun(@length, {s.children.type})));
	fprintf('\nChildren (%d):\n\n', n);
	bUnsupported = false;
	for i = 1:n
		c = s.children(i);
		if c.loadable, sMark = ' '; else, sMark = '*'; bUnsupported = true; end
		fprintf('  %-*s  %-*s%s  %s\n', wName, c.name, wType, c.type, sMark, c.title);
	end
	if bUnsupported
		fprintf('\n  * this node type can be listed but not loaded or queried by das3\n');
	end
end

%% Data source -----------------------------------------------------------

function prnSource(s, bAll)
	sVer = s.version;
	if ~isempty(sVer), sVer = [' ' sVer]; end
	fprintf('HttpStreamSrc%s:  %s\n', sVer, s.name);
	if ~isempty(s.title),   fprintf('Title:    %s\n', s.title); end
	if ~isempty(s.summary), fprintf('Summary:  %s\n', s.summary); end
	fprintf('URI:      %s\n', s.uri);
	if s.authrequired
		if isempty(s.authrealm)
			fprintf('Auth:     REQUIRED\n');
		else
			fprintf('Auth:     REQUIRED, realm "%s"\n', s.authrealm);
		end
	else
		fprintf('Auth:     not required\n');
	end
	if ~isempty(s.formats)
		lGroups = unique({s.formats.group}, 'stable');
		fprintf('Formats:  %s\n', strjoin(lGroups, ' '));
	elseif ~isempty(s.mime)
		fprintf('Formats:  %s\n', s.mime);
	end

	% Examples: the number is what 'example', N selects
	n = numel(s.examples);
	if n > 0
		fprintf('\nexamples:\n  Key  Provides\n  ---  --------\n');
		for i = 1:n
			fprintf('  %-3d  %s\n', i - 1, s.examples(i).label);
			if bAll
				fprintf('       %s\n', strjoin(s.examples(i).keys, ' '));
			end
		end
	end

	lSec = {'coords', 'data', 'formats', 'options'};
	nCtrls = 0;
	for i = 1:numel(lSec)
		t = s.(lSec{i});
		if isempty(t), continue; end
		if ~bAll && strcmp(lSec{i}, 'formats'), continue; end
		if ~bAll, t = t(~[t.fixed]); end
		if isempty(t), continue; end
		nCtrls = nCtrls + sum(~[t.fixed]);
		prnSection(lSec{i}, t);
	end
	if nCtrls == 0
		fprintf('\nThis source takes no query parameters.\n');
	end
end

function prnSection(sName, t)
	n = numel(t);
	lKey = cell(1, n); lReq = cell(1, n); lVals = cell(1, n);
	for i = 1:n
		k = t(i).key;
		iDot = find(k == '.', 1);
		if isempty(iDot), lKey{i} = k; else, lKey{i} = k(iDot+1:end); end
		if t(i).fixed
			lReq{i} = 'fixed';
		elseif t(i).required
			lReq{i} = 'yes';
		else
			lReq{i} = '';
		end
		if ~isempty(t(i).values)
			lVals{i} = strjoin(t(i).values, ', ');
		elseif ~isempty(t(i).min) || ~isempty(t(i).max)
			lVals{i} = sprintf('%s to %s', t(i).min, t(i).max);
		elseif strcmp(t(i).type, 'boolean')
			lVals{i} = 'true, false';
		else
			lVals{i} = '';
		end
		if length(lVals{i}) > 40, lVals{i} = [lVals{i}(1:37) '...']; end
	end
	lType = {t.type}; lUnits = {t.units}; lDef = {t.default};
	w = [ max(3, max(cellfun(@length, lKey))), max(4, max(cellfun(@length, lType))), ...
	      max(5, max(cellfun(@length, lUnits))), max(7, max(cellfun(@length, lDef))), ...
	      max(3, max(cellfun(@length, lReq))), max(6, max(cellfun(@length, lVals))) ];
	fprintf('\n%s:\n', sName);
	fprintf('  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %s\n', w(1), 'Key', w(2), 'Type', ...
		w(3), 'Units', w(4), 'Default', w(5), 'Req', w(6), 'Values', 'Description');
	fprintf('  %s  %s  %s  %s  %s  %s  %s\n', repmat('-', 1, w(1)), repmat('-', 1, w(2)), ...
		repmat('-', 1, w(3)), repmat('-', 1, w(4)), repmat('-', 1, w(5)), repmat('-', 1, w(6)), ...
		repmat('-', 1, 24));
	for i = 1:n
		fprintf('  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %s\n', w(1), lKey{i}, w(2), lType{i}, ...
			w(3), lUnits{i}, w(4), lDef{i}, w(5), lReq{i}, w(6), lVals{i}, t(i).title);
	end
end

%% Query dry run ---------------------------------------------------------

function prnExplain(s)
	fprintf('Source:   %s\n', s.source);
	if ~isempty(s.label), fprintf('Example:  %s\n', s.label); end
	if ~isempty(s.mime),  fprintf('Produces: %s\n', s.mime); end
	if s.ok
		fprintf('Status:   ok\n');
	else
		fprintf('Status:   NOT OK, see the keys marked bad\n');
	end
	if ~isempty(s.keys)
		w = max(cellfun(@length, {s.keys.key}));
		fprintf('\nkeys:\n');
		for i = 1:numel(s.keys)
			k = s.keys(i);
			if k.derived, sFrom = '  (derived)'; else, sFrom = ''; end
			if strcmp(k.verdict, 'bad')
				fprintf('  %-*s = %s   <- bad: %s\n', w, k.key, k.value, k.why);
			else
				fprintf('  %-*s = %s%s\n', w, k.key, k.value, sFrom);
			end
		end
	end
	if ~isempty(s.raw)
		w = max(cellfun(@length, {s.raw.name}));
		fprintf('\nraw parameters (sent as given):\n');
		for i = 1:numel(s.raw)
			fprintf('  %-*s = %s\n', w, s.raw(i).name, s.raw(i).value);
		end
	end
end
