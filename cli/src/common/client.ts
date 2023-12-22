import { WebSocket } from "ws";

export enum NimochWebsockCommand {
    ListProjects = 0x00,
    ListProjectsResponse = 0x80,
    OpenProject = 0x01,
    OpenProjectResponse = 0x81,
    GetFrame = 0x02,
    GetFrameResponse = 0x82,
    GetInputImage = 0x0a,
    GetInputImageResponse = 0x0b,
};

export interface NimochRationalNumber {
    den: number;
    num: number;
}

export interface NimochWebsockOpenProjectArgs {
    id: string;
}

export interface NimochWebsockOpenProjectResponseArgs {
    status: number;
    width?: number;
    height?: number;
    frames?: number;
    fps?: NimochRationalNumber;
    uuid?: string;
}

export interface NimochWebsockGetFrameArgs {
    frame: number;
}

export interface NimochWebsockGetInputImageArgs {
    name: string;
    pts: number;
    opt?: number;
}

export class NimochMessage {
    public command: NimochWebsockCommand;
    public payload: Buffer;

    public static From(command: NimochWebsockCommand, payload: Buffer) {
        return new NimochMessage(Buffer.concat([Buffer.from([command]), payload]));
    }

    public constructor(raw: Buffer) {
        this.command = raw[0];
        this.payload = raw.subarray(1);
    }

    public toBuffer() {
        return Buffer.concat([Buffer.from([this.command]), this.payload]);
    }
}

export class NimochMessageFrame extends NimochMessage {
    public status: number;
    public frame: Buffer | null;

    public static FromFrame(command: NimochWebsockCommand, status: number, frame: Buffer) {
        return new NimochMessage(Buffer.concat([Buffer.from([command, status]), frame]));
    }

    public constructor(raw: Buffer) {
        super(raw);

        this.status = this.payload[0];
        if (this.payload.byteLength > 1) {
            this.frame = this.payload.subarray(1);
        } else {
            this.frame = null;
        }
    }

    public toBuffer() {
        if (this.frame == null) {
            return Buffer.from([this.command, this.status]);
        } else {
            return Buffer.concat([Buffer.from([this.command, this.status]), this.frame]);
        }
    }
}

export class NimochMessageImage extends NimochMessageFrame {
}

export class NimochMessageJson<T> extends NimochMessage {
    public obj: T;

    public static FromObject<T>(command: NimochWebsockCommand, obj: T) {
        return new NimochMessageJson<T>(Buffer.concat([Buffer.from([command]), Buffer.from(JSON.stringify(obj))]));
    }

    public constructor(raw: Buffer) {
        super(raw);

        this.obj = JSON.parse(this.payload.toString("utf-8"));
    }

    public toBuffer() {
        this.payload = Buffer.from(JSON.stringify(this.obj));

        return super.toBuffer();
    }
}

export interface NimochProjectConfig {
    id: string; // Auto-filled
    name: string;
    author: string;
    fps: NimochRationalNumber;
    width: number;
    height: number;
    timeline: NimochProjectTimeline[];
}

export interface NimochProjectTimeline {
    end: string;
    html: string;

    selector: string;
    animationSelector?: string;
}

function bufferToMessage(buf: Buffer) {
    const cmd: NimochWebsockCommand = buf[0];

    switch (cmd) {
        case NimochWebsockCommand.ListProjectsResponse:
            return new NimochMessageJson<NimochProjectConfig[]>(buf);

        case NimochWebsockCommand.OpenProject:
            return new NimochMessageJson<NimochWebsockOpenProjectArgs>(buf);

        case NimochWebsockCommand.OpenProjectResponse:
            return new NimochMessageJson<NimochWebsockOpenProjectResponseArgs>(buf);

        case NimochWebsockCommand.GetFrame:
            return new NimochMessageJson<NimochWebsockGetFrameArgs>(buf);

        case NimochWebsockCommand.GetFrameResponse:
            return new NimochMessageFrame(buf);

        case NimochWebsockCommand.GetInputImage:
            return new NimochMessageJson<NimochWebsockGetInputImageArgs>(buf);

        case NimochWebsockCommand.GetInputImageResponse:
            return new NimochMessageImage(buf);

        default:
            return new NimochMessage(buf);
    }
}

export class NimochWebsockClient {
    protected socket: WebSocket;

    protected ready: boolean;
    protected openPromise: Promise<void>;

    protected recvPromise: Promise<NimochMessage> | null;
    protected recvQueue: NimochMessage[];
    protected recvNotification: ((msg: NimochMessage) => void) | null;

    public constructor(uri: string) {
        this.socket = new WebSocket(uri);
        this.recvPromise = null;
        this.recvQueue = [];
        this.recvNotification = null;
        this.ready = false;

        this.socket.on("message", (buf: Buffer) => {
            this.handleMessage(buf);
        });

        this.openPromise = new Promise<void>((resolve) => {
            this.socket.on("open", () => {
                resolve();
            });
        });
    }

    public async waitForReady() {
        if (this.ready) {
            return;
        } else {
            await this.openPromise;
            this.ready = true;
        }
    }

    public close() {
        this.socket.close();
    }

    public async recv() {
        if (this.recvPromise == null) {
            if (this.recvQueue.length > 0) {
                return this.recvQueue.pop();
            }
            this.setRecvPromise();
        }
        const promise = this.recvPromise;
        this.recvPromise = null;

        return promise;
    }

    protected setRecvPromise() {
        this.recvPromise = new Promise<NimochMessage>((resolve) => {
            this.recvNotification = resolve;
        });
    }

    protected handleMessage(msg: Buffer) {
        const nimochMsg = bufferToMessage(msg);

        if (this.recvNotification) {
            this.recvNotification(nimochMsg);
        } else {
            this.recvQueue.push(nimochMsg);
        }
    }

    public sendMessage(msg: NimochMessage) {
        this.socket.send(msg.toBuffer());
    }

    public async transact(msg: NimochMessage) {
        this.setRecvPromise();
        this.sendMessage(msg);
        return await this.recv();
    }
}
