classdef (TestTags = {'offline'}) OfflineTest < matlab.unittest.TestCase
	% Tests that need no network: the mex loads, settings round trip, and
	% catalog nodes saved under fixtures/ resolve as a local seed.

	properties
		sFixDir
	end

	methods (TestClassSetup)
		function setup(tc)
			tc.sFixDir = fullfile(fileparts(mfilename('fullpath')), 'fixtures');
			das3.config('reset');
		end
	end

	methods (TestMethodTeardown)
		function teardown(~)
			das3.config('reset');
		end
	end

	methods (Static)
		function v = key(r, sName)
			% One resolved key of a dry run result, '' if absent.  Old nodes
			% spell min as minimum, so a prefix match is accepted.
			i = find(strcmp({r.keys.key}, sName), 1);
			if isempty(i), i = find(startsWith({r.keys.key}, sName), 1); end
			if isempty(i), v = ''; else, v = r.keys(i).value; end
		end
	end

	methods (Test)
		function version(tc)
			v = das3.version;
			tc.verifyTrue(ischar(v.das3mex) && ~isempty(v.das3mex));
			tc.verifyTrue(ischar(v.das2C) && ~isempty(v.das2C));
		end

		function seeds(tc)
			l = das3.seeds;
			tc.verifyTrue(iscellstr(l) && ~isempty(l));
			tc.verifyTrue(startsWith(l{1}, 'https://'));
		end

		function configRoundTrip(tc)
			s = das3.config('timeout', 3, 'quiet', true, 'loglevel', 'info', ...
				'mount', 'https://example.org/a.json=site:/x/a', ...
				'mount', {'https://example.org/b.json', 'site:/x/b'});
			tc.verifyEqual(s.timeout, 3);
			tc.verifyTrue(s.quiet);
			tc.verifyEqual(s.loglevel, 'info');
			tc.verifyEqual(size(s.mounts), [2 2]);
			tc.verifyEqual(s.mounts{2,2}, 'site:/x/b');
			s = das3.config('mounts', {});
			tc.verifyEmpty(s.mounts);
			s = das3.config('reset');
			tc.verifyEqual(s.timeout, 10);
			tc.verifyFalse(s.quiet);
		end

		function badSetting(tc)
			tc.verifyError(@() das3.config('nonsense', 1), 'das3:argcheck');
		end

		function seedSourceSet(tc)
			% A SourceSet file declared to be its catalog name: the child
			% named in it is loaded from the network, so only the parent's
			% listing is checked here
			sUri = 'site:/uiowa/tracers/tracers-2/spice/geomagcoords';
			das3.config('start', [fullfile(tc.sFixDir, 'geomagcoords.json') '=' sUri]);
			n = das3.node('');
			tc.verifyEqual(n.type, 'SourceSet');
			tc.verifyTrue(n.iscatalog);
			tc.verifyTrue(any(strcmp({n.children.name}, 'flex')));
			iFlex = find(strcmp({n.children.name}, 'flex'), 1);
			tc.verifyTrue(any(strcmp(n.children(iFlex).provides, 'application/vnd.das.stream')));
		end

		function rootFunction(tc)
			% das3.root with a file: sticky, shown, and cleared by 'default'
			sFile = fullfile(tc.sFixDir, 'geomagcoords.json');
			r = das3.root(sFile);
			tc.verifyEqual(r, sFile);
			tc.verifyEqual(das3.root, sFile);
			n = das3.node('');
			tc.verifyEqual(n.type, 'SourceSet');
			r = das3.root(sFile, 'as', 'site:/uiowa/tracers/tracers-2/spice/geomagcoords');
			tc.verifyTrue(endsWith(r, '=site:/uiowa/tracers/tracers-2/spice/geomagcoords'));
			r = das3.root('default');
			tc.verifyTrue(startsWith(r, 'https://'));
			tc.verifyEmpty(das3.config().start);
		end

		function rootSourceFile(tc)
			% A source node file as a root: no URI, but the form is all there
			sFile = fullfile(tc.sFixDir, 'juno_wav_survey_das2.json');
			das3.config('start', sFile);
			s = das3.source('');
			tc.verifyEqual(s.name, 'Das2/2.2 Source');
			tc.verifyTrue(s.authrequired);
			tc.verifyEqual(numel(s.examples), 9);
			tc.verifyTrue(any(strcmp({s.coords.key}, 'coords.time.minimum')));
			tc.verifyTrue(any(strcmp({s.options.key}, 'options.b')));
			tc.verifyTrue(startsWith(s.servers{1}, 'https://jupiter.physics.uiowa.edu/das/server'));
		end

		function dryRunFromFile(tc)
			sFile = fullfile(tc.sFixDir, 'juno_wav_survey_das2.json');
			das3.config('start', sFile, 'quiet', true);

			% Example 0 by default, then the key spellings reqargs produces
			k = @das3test.OfflineTest.key;
			r = das3.query('', 'dryrun', true);
			tc.verifyTrue(r.ok);
			tc.verifyEqual(r.label, 'Perijove 16');
			tc.verifyEqual(k(r, 'coords.time.min'), '2018-10-02T11:00');
			tc.verifyEqual(r.mime, 'application/vnd.das2.das2stream');
			tc.verifyTrue(all(strcmp({r.keys.verdict}, 'ok')));

			% A range shortcut expands into derived entries for min and max
			r = das3.query('', 'time', {'2021-02-21', datetime(2021, 2, 22)}, 'b', 'dryrun', true);
			tc.verifyTrue(r.ok);
			tc.verifyEqual(k(r, 'time'), '2021-02-21,2021-02-22T00:00:00.000');
			tc.verifyEqual(k(r, 'coords.time.min'), '2021-02-21');
			tc.verifyEqual(k(r, 'coords.time.max'), '2021-02-22T00:00:00.000');
			tc.verifyEqual(k(r, 'b'), 'true');
			i = find(startsWith({r.keys.key}, 'coords.time.min'), 1);
			tc.verifyTrue(r.keys(i).derived);

			r = das3.query('', 'time.min', '2021-02-21', 'time.max', '2021-02-22', ...
				'time.res', 60, 'dryrun', true);
			tc.verifyTrue(r.ok);
			tc.verifyEqual(k(r, 'time.res'), '60');

			r = das3.query('', 'time', {'2021-02-21', '2021-02-22'}, 'raw', {'params', '--80khz'}, ...
				'dryrun', true);
			tc.verifyTrue(r.ok);
			tc.verifyEqual(numel(r.raw), 1);
			tc.verifyEqual(r.raw(1).value, '--80khz');

			r = das3.query('', 'example', 8, 'dryrun', true);
			tc.verifyEqual(r.label, 'Earth Flyby');
			tc.verifyFalse(startsWith(k(r, 'coords.time.min'), '2018-10'));
		end

		function badKey(tc)
			% A dry run reports bad keys with reasons instead of raising
			sFile = fullfile(tc.sFixDir, 'juno_wav_survey_das2.json');
			das3.config('start', sFile, 'quiet', true);
			r = das3.query('', 'tiem', '2021', 'dryrun', true);
			tc.verifyFalse(r.ok);
			i = find(strcmp({r.keys.key}, 'tiem'), 1);
			tc.verifyEqual(r.keys(i).verdict, 'bad');
			tc.verifyTrue(contains(r.keys(i).why, 'tiem'));

			r = das3.query('', 'nonsense', 'dryrun', true);
			tc.verifyFalse(r.ok);

			% A required key with no default left empty is reported too.  The
			% Juno node defaults its time bounds, the TRACERS one does not.
			das3.config('start', fullfile(tc.sFixDir, 'geomagcoords_flex.json'), 'quiet', true);
			r = das3.query('', 'time.inter', 60, 'dryrun', true);
			tc.verifyFalse(r.ok);
			i = find(strcmp({r.keys.key}, 'coords.time.min'), 1);
			tc.verifyNotEmpty(i);
			tc.verifyEqual(r.keys(i).verdict, 'bad');
		end

		function noMemoryResultsYet(tc)
			sFile = fullfile(tc.sFixDir, 'juno_wav_survey_das2.json');
			das3.config('start', sFile, 'quiet', true);
			tc.verifyError(@() das3.query(''), 'das3:notimp');
		end

		function badPath(tc)
			sFile = fullfile(tc.sFixDir, 'juno_wav_survey_das2.json');
			das3.config('start', sFile, 'quiet', true);
			tc.verifyError(@() das3.node('does/not/exist'), 'das3:node');
		end
	end
end
