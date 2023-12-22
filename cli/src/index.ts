import { program } from "commander";
import config from "config";
import { NimochMessage, NimochMessageFrame, NimochMessageJson, NimochProjectConfig, NimochRationalNumber, NimochWebsockClient, NimochWebsockCommand, NimochWebsockGetFrameArgs, NimochWebsockOpenProjectArgs, NimochWebsockOpenProjectResponseArgs } from "./common/client";
import { spawn } from "child_process";
import { Writable, once } from "stream";

program.name("nimoch-cli")
    .option("-c, --connect <uri>", "WebSocket URI", "ws://localhost:4440/websocket/")
    .version("0.1.0");

program.command("list")
    .description("show list of projects")
    .action(handleList);

program.command("open")
    .argument("<project>")
    .description("open a project and wait")
    .action(handleOpen);

program.command("encode")
    .argument("<project>")
    .description("encode a project to movie file")
    .requiredOption("-o, --output <output>", "Output movie file")
    .action(handleEncode);

async function initWebsocket() {
    const ws = program.opts().connect as string;
    const client = new NimochWebsockClient(ws);

    await client.waitForReady();

    return client;
}

async function handleList() {
    const client = await initWebsocket();

    const ret = (await client.transact(NimochMessage.From(NimochWebsockCommand.ListProjects, Buffer.from([])))) as NimochMessageJson<NimochProjectConfig[]>;

    for (const proj of ret.obj) {
        console.log("%s: %s (%d x %d, %s fps)", proj.id, proj.name, proj.width, proj.height, ffmpegFPSString(proj.fps));
    }

    client.close();
}

function ffmpegFPSString(fps: NimochRationalNumber) {
    if (fps.den === 1) {
        return fps.num.toString();
    } else {
        return `${fps.num.toString()}/${fps.den.toString()}`;
    }
}

async function handleEncode(project: string, options: Record<string, string>) {
    const client = await initWebsocket();

    console.debug("Project = %s", project);
    const msg = NimochMessageJson.FromObject<NimochWebsockOpenProjectArgs>(NimochWebsockCommand.OpenProject, { id: project });
    const ret = (await client.transact(msg)) as NimochMessageJson<NimochWebsockOpenProjectResponseArgs>;

    if (ret.obj.status !== 200) {
        console.error(`Failed to open project "${project}". Server returned response ${ret.obj.status.toString()}`);
        client.close();
        return;
    }

    if (ret.obj.fps == null) {
        console.error(`No information for fps. Cannot encode.`);
        client.close();
        return;
    }

    const length = ret.obj.frames! * ret.obj.fps.den / ret.obj.fps.num;

    console.debug("Width = %d, Height = %d, FPS = %d, Total frames = %d (%s s)", ret.obj.width, ret.obj.height, ret.obj.fps, ret.obj.frames, length.toFixed(1));

    const ffmpegBinary: string = config.get<string>("bin.ffmpeg");
    const ffmpegOptions: string[] = [];
    ffmpegOptions.push(...config.get<string[]>("encoder.options"))

    const ffmpegProc = spawn(ffmpegBinary, ["-y", "-r", ffmpegFPSString(ret.obj.fps), "-f", "image2pipe", "-i", "-", ...ffmpegOptions, options.output], {
        stdio: ["pipe", "inherit", "inherit"],
        env: {
            ...process.env,
            ...config.get<Record<string, string>>("encoder.env")
        }
    });

    let i = 0;
    do {
        const request = NimochMessageJson.FromObject<NimochWebsockGetFrameArgs>(NimochWebsockCommand.GetFrame, { frame: i });
        const frame = (await client.transact(request)) as NimochMessageFrame;

        if (frame.frame == null) {
            break;
        }
        await writeToPipe(ffmpegProc.stdin, frame.frame);

        i++;
    } while (true);

    ffmpegProc.stdin.destroy();

    client.close();
}

async function handleOpen(project: string) {
    const client = await initWebsocket();

    console.debug("Project = %s", project);
    const msg = NimochMessageJson.FromObject<NimochWebsockOpenProjectArgs>(NimochWebsockCommand.OpenProject, { id: project });
    const ret = (await client.transact(msg)) as NimochMessageJson<NimochWebsockOpenProjectResponseArgs>;

    if (ret.obj.status !== 200) {
        console.error(`Failed to open project "${project}". Server returned response ${ret.obj.status.toString()}`);
        client.close();
        return;
    }
    console.debug("Successfully opened the project %s", project);
    console.debug("UUID = %s", ret.obj.uuid);
    // Wait
}


async function writeToPipe(pipe: Writable, buf: Buffer) {
    if (pipe.write(buf) !== true) {
        await once(pipe, "drain");
    }
}

program.parseAsync();
