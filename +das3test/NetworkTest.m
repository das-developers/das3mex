classdef (TestTags = {'network'}) NetworkTest < matlab.unittest.TestCase
	% Tests against the live das catalog and the Iowa servers.  Skipped
	% when the catalog index does not answer.

	methods (TestClassSetup)
		function setup(tc)
			das3.config('reset');
			das3.config('quiet', true, 'prompt', false);
			try
				n = das3.node(''); %#ok<NASGU>
			catch e
				tc.assumeFail(['No catalog access: ' e.message]);
			end
		end
	end

	methods (TestClassTeardown)
		function teardown(~)
			das3.config('reset');
		end
	end

	methods (Test)
		function rootListing(tc)
			n = das3.node('');
			tc.verifyTrue(n.iscatalog);
			tc.verifyTrue(any(strcmp({n.children.name}, 'uiowa')));
		end

		function collectionPicksStream(tc)
			s = das3.source('uiowa/tracers/tracers-2/spice/geomagcoords');
			tc.verifyTrue(endsWith(s.uri, '/flex'));
			s = das3.source('uiowa/juno/wav/survey');
			tc.verifyTrue(endsWith(s.uri, '/das2'));
		end

		function acceptCsvOnly(tc)
			% Nothing under the Juno survey provides CSV, which is an error
			% that names the children
			tc.verifyError(@() das3.source('uiowa/juno/wav/survey', 'accept', 'text/csv'), 'das3:node');
		end

		function dryRun(tc)
			r = das3.query('uiowa/tracers/tracers-2/spice/geomagcoords', ...
				'time', {'2026-01-01', '2026-01-01T01:00', 60}, 'csv', 'dryrun', true);
			tc.verifyTrue(endsWith(r.source, '/flex'));
			tc.verifyTrue(r.ok);
			tc.verifyEqual(r.mime, 'text/csv');
			i = find(strcmp({r.keys.key}, 'coords.time.inter'), 1);
			tc.verifyEqual(r.keys(i).value, '60');
			tc.verifyTrue(any(strcmp({r.keys.key}, 'csv')));
		end

		function queryToFile(tc)
			sOut = [tempname '.d3b'];
			c = onCleanup(@() delete(sOut));
			info = das3.query('uiowa/tracers/tracers-2/spice/geomagcoords', ...
				'time', {'2026-01-01', '2026-01-01T00:05', 60}, 'file', sOut);
			tc.verifyEqual(info.status, 200);
			tc.verifyGreaterThan(info.bytes, 100);
			d = dir(sOut);
			tc.verifyEqual(d.bytes, info.bytes);
		end

		function projectRoot(tc)
			% A project's own node as the catalog root, given without a
			% scheme the way people type hostnames
			c = onCleanup(@() evalc('das3.root(''default'')'));
			r = das3.root('space.physics.uiowa.edu/das/stream/source/tracers.json');
			tc.verifyEqual(r, 'https://space.physics.uiowa.edu/das/stream/source/tracers.json');
			n = das3.node('');
			tc.verifyTrue(any(strcmp({n.children.name}, 'tracers-2')));
			s = das3.source('tracers-2/spice/geomagcoords');
			tc.verifyEqual(s.name, 'GeomagCoords');
			% Outside the root is unreachable, by design
			tc.verifyError(@() das3.node('uiowa/juno'), 'das3:node');
		end

		function authChallenge(tc)
			% A source behind HTTP basic auth, with the prompt switched off,
			% fails with the auth identifier rather than hanging or crashing.
			% The interactive prompt itself is exercised by hand.
			sOut = [tempname '.d3b'];
			c = onCleanup(@() delete(sOut));
			das3.config('prompt', false);
			tc.verifyError(@() das3.query('uiowa/tracers/tracers-2/ql/mag/hsk', ...
				'time', {'2026-01-01', '2026-01-01T00:05'}, 'file', sOut), 'das3:auth');
		end
	end
end
