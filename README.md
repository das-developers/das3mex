# das3mex

MATLAB bindings for the [das2C](https://github.com/das-developers/das2C)
library: stream values straight into RAM from servers using flexible
catalog nodes. No URLs embedded in user code, no hard-locked server
protocol.

Status: the catalog side works (browse, describe, query to file, logins).
Reading streams into MATLAB structures is next. 

## Build

Requires GNU make, a C compiler mex knows about, and the das2C
dependencies (expat, fftw3, openssl, zlib development packages).

```bash
git clone --recursive <this repo>
make                 # builds deps/das2C, then +das3/private/das3mex.mexa64
make test            # 14 unit tests, 5 of them need the network
make MATLAB_ROOT=/opt/MATLAB/R2024a   # if the guess is wrong
```

## Use

```matlab
addpath('/path/to/das3mex')

das3.node                                  % the federated sites
das3.node('uiowa/juno/wav')                % a catalog's children
das3.node('uiowa/juno/wav/survey')         % a source: examples and query keys

s = das3.source('uiowa/juno/wav/survey');  % the same as a struct
das3.show(s)                               % printed again

uri = 'uiowa/tracers/tracers-2/spice/geomagcoords';
info = das3.query( uri ...
  'time', {datetime(2026,1,1), datetime(2026,1,2), 60}, 'lshell', ...
  'file', 'out.d3b' ...
)       % response body to a file

das3.query('uiowa/juno/wav/survey', 'time', {'2021-02-21', '2021-02-22'}, 'b', ...
           'dryrun', true)                 % what would be asked, no network

das3.config('timeout', 30)
das3.config('start', './my_node.json=site:/uiowa/tracers')   % offline or test catalogs
```

A source behind a password asks for one the first time it is queried;
`das3.login` supplies it ahead of time for scripts.  Every function has
help: `help das3.query` lists the key spellings and options.
