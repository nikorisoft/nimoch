import fs from "fs/promises";
import WebSocket from "ws";
import NimochConfig from "../config";

function sendJSON(ws: WebSocket, command: number, contents: any) {
    const str = JSON.stringify(contents);

    ws.send(Buffer.concat([Buffer.from([command]), Buffer.from(str)]));
}

function parseJSON(msg: Buffer) {
    return JSON.parse(msg.subarray(1).toString("utf-8"));
}

async function main() {
    const port = NimochConfig.server.port;

    const ws = new WebSocket(`ws://localhost:${port.toString()}/websocket/`);

    ws.on("open", () => {
        sendJSON(ws, 0x01, { id: "test" });
    });

    let currentFrame = 0;
    ws.on("message", async (msg: Buffer) => {
        if (msg[0] == 0x80) {
            const m = parseJSON(msg);

            console.log("ListProjectsResponse", m);

            ws.close();
        } else if (msg[0] == 0x81) {
            const m = parseJSON(msg);

            console.log("OpenProjectResponse", m);

            sendJSON(ws, 0x02, { frame: currentFrame });
        } else if (msg[0] == 0x82) {
            console.log("ReadFrame", "Status Code:", msg[1]);

            if (msg[1] == 0x00) {
                await fs.writeFile(`${currentFrame.toString()}.png`, msg.subarray(2));

                currentFrame++;
                sendJSON(ws, 0x02, { frame: currentFrame });
            } else {
                ws.close();
            }
        }
    });


    return 0;
}

main().then((ret) => {

}, (err) => {
    //
});

/*
ffmpeg -r 30 -f image2 -i "%d.png" -r 30 -pix_fmt yuv420p -f yuv4mpegpipe - | ../x264-r3172-c1c9931 --demux y4m -o output.mp4 -
*/