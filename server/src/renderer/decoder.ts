import config from "config";
import { ChildProcessByStdio, spawn } from "child_process";
import path from "path";
import { once, Readable, Writable } from "stream";
import { Mutex } from "async-mutex";

const NICM_PATH = path.resolve(__dirname, config.get<string>("bin.decoder"));

enum NicmServeCommand {
    QUIT = 0,
    INFO = 1,
    IMAGE = 2,
    SCENE_DETECT = 256
};

interface NicmServeResponseHeader {
    code: number;
    size: number;
}

export interface NicmServeSceneDetectResult {
    scores: number[];
}

const NICM_SERVE_RESPONSE_HEADER_SIZE = 16;

export interface NicmInfo {
    aspect_ratio: { num: number, den: number };
    timebase: { num: number, den: number };
    start_time: number;
    first_pts: number;
    duration: number;
    width: number;
    height: number;
    stream: number;
}

export interface NicmDetectStream {
    index: number;
    pts?: number;
    timebase: {
        num: number;
        den: number;
    }
    fps?: {
        num: number;
        den: number;
    }
}

export interface NicmDetectResult {
    programs: {
        [service_id: string]: {
            video: NicmDetectStream[];
            audio: NicmDetectStream[];
            subtitle: NicmDetectStream[];
        }
    };
}

export type NicmAudioDecodeSegment = {
    start: number;
    end: number;
    layout: string;
    channels: number;
    format: string;
    sampleRate: number;
    frames: number;
};

export type NicmAudioDecodeSegmentInfo = NicmAudioDecodeSegment[];

function nicmServeRequest(command: NicmServeCommand, ...args: number[]) {
    const buf = Buffer.alloc(64, 0);
    let i;

    buf.writeIntLE(command, 0, 6);
    for (i = 0; i < 7; i++) {
        if (i < args.length) {
            buf.writeBigInt64LE(BigInt(args[i]), 8 + i * 8);
        }
    }

    return buf;
}
function nicmServeResponse(response: Buffer): NicmServeResponseHeader {
    const code = response.readBigInt64LE(0);
    const size = response.readBigInt64LE(8);

    // Should fit in 2^53 - 1
    return {
        code: Number(code),
        size: Number(size)
    };
}

export function execPipeStdout(command: string, args: string[]): Promise<string> {
    return new Promise<string>((resolve, reject) => {
        let ret = "";

        const proc = spawn(command, args, {
            stdio: ["ignore", "pipe", "inherit"],
        });

        proc.stdout.on("data", (data: Buffer) => {
            ret += data.toString("utf-8");
        });

        proc.on("close", (code) => {
            if (code !== 0) {
                reject(new Error("Process exited with " + code));
            } else {
                resolve(ret);
            }
        });

        proc.on("error", (err) => {
            reject(err);
        });
    });
}

export class NicmClient {
    public static async Detect(filename: string, opts?: string[]): Promise<NicmDetectResult> {
        if (opts == null) {
            opts = [];
        }
        const result = await execPipeStdout(NICM_PATH, ["detect", ...opts, filename]);

        return JSON.parse(result);
    }

    protected proc: ChildProcessByStdio<Writable, Readable, null>;
    protected mutex: Mutex;

    public constructor(filename: string, stream?: number, additionalOpts?: string[]) {
        const opts = [];
        if (stream != null) {
            opts.push("-s", stream.toString());
        }
        if (additionalOpts != null) {
            opts.push(...additionalOpts);
        }

        this.proc = spawn(NICM_PATH, ["serve", ...opts, filename], {
            stdio: ["pipe", "pipe", "inherit"]
        });
        this.mutex = new Mutex();
    }

    protected async transact(request: Buffer): Promise<Buffer> {
        const release = await this.mutex.acquire();
        let error;
        try {
            if (this.proc.stdin.write(request) !== true) {
                await once(this.proc.stdin, "drain");
            }
            const responseBuffer = await readFromStream(this.proc.stdout, NICM_SERVE_RESPONSE_HEADER_SIZE);
            const header = nicmServeResponse(responseBuffer);

            if (header.size > 0) {
                return await readFromStream(this.proc.stdout, header.size);
            } else if (header.code === 0) {
                return Buffer.from([]);
            }
            error = new Error("Server returned " + header.code);
        } finally {
            release();
        }

        throw error;
    }

    public async info(): Promise<NicmInfo> {
        const data = await this.transact(nicmServeRequest(NicmServeCommand.INFO));

        return JSON.parse(data.toString("utf-8"));
    }

    public async image(pts: number, opt: number): Promise<Buffer> {
        const data = await this.transact(nicmServeRequest(NicmServeCommand.IMAGE, pts, 0, opt));

        return data;
    }

    public async sceneDetect(pts: number, opt: number, maxFrames: number = 0, cutOffScore: number = 0): Promise<NicmServeSceneDetectResult> {
        const data = await this.transact(nicmServeRequest(NicmServeCommand.SCENE_DETECT, pts, opt, maxFrames, cutOffScore));

        return JSON.parse(data.toString("utf-8"));
    }

    public async quit() {
        await this.transact(nicmServeRequest(NicmServeCommand.QUIT));

        this.proc.stdin.end();
    }
}

async function readFromStream(stream: Readable, size: number): Promise<Buffer> {
    const buf = Buffer.alloc(size);
    let offset = 0;

    while (size > 0) {
        await once(stream, "readable");

        let chunk = stream.read(size);

        if (chunk == null) {
            chunk = stream.read();
            if (chunk == null) {
                throw new Error("Cannot read");
            }
        }
        (chunk as Buffer).copy(buf, offset);
        offset += chunk.length;
        size -= chunk.length;
    }

    return buf;
}
