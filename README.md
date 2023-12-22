# nimoch

## What's this?

**ni**korisoft **m**ovie **o**rganizer with **c**ss and **h**tml

## What's in this repository?

- `server`: Main program that serves Websocket and API over HTTP
- `cli`: Client program that communicates with the `server` and composes a project into a movie file
- `decoder`: Auxiliary program that handles input movie files
- `projects`: Directory that your movie projects should reside
- `lib`: Standard library that can be used from the HTML files in movie projects

## How to use this?

### Prerequisites

- FFmpeg and libav* (developed and tested with v6.1)
- Node.js (developed and tested with v21)

### Install

For Node.js components (`cli` and `server`), run
```
$ npm install
$ npm run -ws build
```

For `decoder`, run

```
$ make
```

### Run

Run a server:

```
$ cd server; node dist/main.js
```

Then run a client to list projects:

```
$ cd cli; node dist/index.js list
(List of projects available)
```

Or to create a movie file:
```
$ cd cli; node dist/index.js encode (project name) -o (output file)
...

```

## License

Apache 2.0
