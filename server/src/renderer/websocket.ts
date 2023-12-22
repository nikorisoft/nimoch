import FastifyWebsocket from "@fastify/websocket";
import { FastifyPluginAsync } from "fastify";
import fp from "fastify-plugin";
import fs from "fs/promises";
import path from "path";

import NimochConfig from "../config";
import { parse } from "yaml";
import { WebSocket } from "ws";
import { NimochRenderer, NimochRendererContext } from "./render";
import { NimochProjectConfig, NimochRationalNumber } from ".";
import { NimochTimeline } from "./timeline";

const ConfigCandidates = ["project.yaml", "project.yml", "project.json"];

async function readProjectConfig(filename: string, id: string): Promise<NimochProjectConfig | null> {
    try {
        const str = await fs.readFile(filename);

        const contents = parse(str.toString());

        return {
            id, 
            ...contents
        };
    } catch (e) {
        return null;
    }
}

enum NimochWebsockCommand {
    ListProjects = 0x00,
    ListProjectsResponse = 0x80,
    OpenProject = 0x01,
    OpenProjectResponse = 0x81,
    GetFrame = 0x02,
    GetFrameResponse = 0x82,
    GetInputImage = 0x0a,
    GetInputImageResponse = 0x0b,
};

const renderers: Record<string, NimochRenderer> = {};

class NimochWebsockClient {
    protected socket: WebSocket;
    protected renderer: NimochRenderer | null;
    protected timeline: NimochTimeline | null;
    protected context: NimochRendererContext | null;
    protected static regexTime = /([0-9.]+)\s*(s|ms|us|f|)/;

    public constructor(socket: WebSocket) {
        this.socket = socket;
        this.renderer = null;
        this.timeline = null;
        this.context = null;

        this.socket.on("message", (msg: Buffer) => {
            this.messageHandler(msg);
        });

        this.socket.on("close", () => {
            this.close();
        })
    }

    public static ConvertTime(fps: NimochRationalNumber, input: string, outputUnit = "") {
        // Input -> inernal time stamp (1/60000)
        const m = NimochWebsockClient.regexTime.exec(input);
        if (m == null) {
            throw new Error("Failed to parse time string: " + input);
        }
        const num = parseFloat(m[1]);
        const unit = m[2];

        let time = num;

        switch (unit) {
            case "s":
                time *= 60000;
                break;
            case "ms":
                time *= 60;
                break;
            case "us":
                time *= 60.0 / 1000.0;
                break;
            case "f":
                time = time * fps.num * 60000 / fps.den;
                break;
        }

        switch (outputUnit) {
            case "s":
                return time / 60000;
            case "ms":
                return time / 60;
            case "us":
                return time * 1000.0 / 60.0;
            case "f":
                return time * fps.den / 60000 / fps.num;
            default:
                return Math.round(time);
        }
    }

    public messageHandler(data: Buffer) {
        switch (data[0]) {
            case NimochWebsockCommand.ListProjects:
                this.handleListProjects();
                break;
            case NimochWebsockCommand.OpenProject:
                {
                    const arg = this.handleJSONArgument(data);
                    this.handleOpenProject(arg);
                    break;
                }
            case NimochWebsockCommand.GetFrame:
                {
                    const arg = this.handleJSONArgument(data);
                    this.handleShowFrame(arg);
                    break;
                }
            case NimochWebsockCommand.GetInputImage:
                {
                    const arg = this.handleJSONArgument(data);
                    this.handleGetInputImage(arg);
                    break;
                }
        }
    }

    protected async close() {
        if (this.renderer != null) {
            delete renderers[this.renderer.uuid];
            await this.renderer.close();
        }
    }

    protected handleJSONArgument(data: Buffer) {
        const str = data.subarray(1).toString("utf-8");

        return JSON.parse(str);
    }

    protected sendJSON(command: NimochWebsockCommand, contents: any) {
        const string = JSON.stringify(contents);

        const bufCommand = Buffer.from([command]);
        const bufContents = Buffer.from(string);

        this.socket.send(Buffer.concat([bufCommand, bufContents]));
    }

    protected async listProjects() {
        const projectDir = NimochConfig.projects.directory;
        const files = await fs.readdir(projectDir);

        const projects = [];

        for (const f of files) {
            try {
                const stat = await fs.stat(path.join(projectDir, f));
                if (stat.isDirectory()) {
                    for (const fn of ConfigCandidates) {
                        const configFile = path.join(projectDir, f, fn);

                        const projectConfig = await readProjectConfig(configFile, f);
                        if (projectConfig != null) {
                            projects.push(projectConfig);
                        }
                    }
                }
            } catch (err) {
                // Ignore the error
            }
        }

        return projects;
    }

    protected async handleListProjects() {
        const projects = await this.listProjects();

        this.sendJSON(NimochWebsockCommand.ListProjectsResponse, projects);
    }

    protected async handleOpenProject(arg: { id: string }) {
        const projects = await this.listProjects();

        const project = projects.find((p) => p.id === arg.id);
        if (project == null) {
            this.sendJSON(NimochWebsockCommand.OpenProjectResponse, { status: 404 });
        } else {
            this.renderer = await NimochRenderer.Init(`http://localhost:${NimochConfig.server.port}/projects/${arg.id}/`, project.width, project.height, project.fps, project.inputs);
            renderers[this.renderer.uuid] = this.renderer;

            this.timeline = new NimochTimeline(project.fps, project.timeline);

            this.sendJSON(NimochWebsockCommand.OpenProjectResponse,
                {
                    status: 200,
                    width: project.width,
                    height: project.height,
                    fps: project.fps,
                    frames: this.timeline.getLength(),
                    uuid: this.renderer.uuid
                });
        }
    }

    protected async handleShowFrame(arg: { frame: number }) {
        if (this.timeline == null || this.renderer == null) {
            this.sendBinary(NimochWebsockCommand.GetFrameResponse, Buffer.from([0x0a]));
            return;
        }
        const scene = this.timeline.getScene(arg.frame);
        if (scene == null) {
            this.sendBinary(NimochWebsockCommand.GetFrameResponse, Buffer.from([0x0b]))
            return;
        }

        if (this.context == null || this.context.context !== scene.id) {
            this.context = await this.renderer.open(scene.html, scene.selector, scene.id);
        }

        const time = this.timeline.frameToTimeBaseInScene(arg.frame, scene);

        await this.renderer.callShowFrame(time.toString());

        const image = await this.renderer.getImage(this.context);

        this.sendBinary(NimochWebsockCommand.GetFrameResponse, Buffer.from([0x00]), image);
    }

    protected sendBinary(cmd: NimochWebsockCommand, ...buf: Buffer[]) {
        const data = Buffer.concat([Buffer.from([cmd]), ...buf]);

        this.socket.send(data);
    }

    protected async handleGetInputImage(arg: { name: string, pts: number, opt?: number }) {
        if (this.timeline == null || this.renderer == null) {
            this.sendBinary(NimochWebsockCommand.GetInputImageResponse, Buffer.from([0x0a]));
            return;
        }
        const input = this.renderer.getDecoder(arg.name);
        if (input == null) {
            this.sendBinary(NimochWebsockCommand.GetInputImageResponse, Buffer.from([0x0c]));
            return;
        }

        try {
            const image = await input.client.image(arg.pts, arg.opt == null ? 0 : arg.opt);

            this.sendBinary(NimochWebsockCommand.GetInputImageResponse, Buffer.from([0x00]), image);
        } catch (e){
            this.sendBinary(NimochWebsockCommand.GetInputImageResponse, Buffer.from([0x0d]));
        }
    }
}

const rendererPluginAsync: FastifyPluginAsync<{}> = async (fastify, options) => {
    await fastify.register(FastifyWebsocket);

    fastify.get("/websocket/", {
        websocket: true
    }, (connection, req) => {
        new NimochWebsockClient(connection.socket);
    });

    fastify.get<{
        Params: {
            rendererId: string,
            name: string,
            frame: string
        }
    }>("/stream/:rendererId/:name/:frame", async (req, res) => {
        const r = renderers[req.params.rendererId];
        if (r == null) {
            res.status(404);
            return {
                error: "Failed to find renderer"
            };
        }
        const n = r.getDecoder(req.params.name);
        if (n == null) {
            res.status(404);
            return {
                error: "Failed to find decoder"
            };
        }

        let pts = 0;
        try {
            const t = NimochWebsockClient.ConvertTime(r.getFPS(), req.params.frame);
            pts = t * n.info.timebase.den / 60000 / n.info.timebase.num;
        } catch (e) {
            return {
                error: "Failed to parse time"
            };
        }

        try {
            const f = await n.client.image(pts, 0);

            return f;
        } catch (e) {
            const err = e as Error;
            res.status(400);

            return {
                error: "Decoder returned error: " + err.toString()
            };
        }
    });
};

export default fp(rendererPluginAsync, "4.x");
