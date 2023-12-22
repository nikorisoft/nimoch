import { NimochProjectTimeline, NimochRationalNumber } from ".";

interface NimochTimelineScene {
    id: string;
    startFrame: number;
    endFrame: number;

    html: string;
    selector: string;
}

export class NimochTimeline {
    protected fps: NimochRationalNumber;
    protected scenes: NimochTimelineScene[];

    public constructor(fps: NimochRationalNumber, timeline: NimochProjectTimeline[]) {
        this.fps = fps;
        this.scenes = [];

        let lastFrame = 0;
        let id = 0;
        for (const t of timeline) {
            const endFrame = this.timeToFrame(t.end);
            if (lastFrame >= endFrame) {
                continue;
            }

            this.scenes.push({
                id: id.toString(),
                startFrame: lastFrame,
                endFrame: endFrame,

                html: t.html,
                selector: t.selector
            });
            lastFrame = endFrame;
            id++;
        }

        console.log(this.scenes);
    }

    public timeToFrame(time: string) {
        const m = /([0-9.]+)\s*(ms|s|f|)/.exec(time);
    
        if (m == null) {
            throw new Error(`Failed to parse time string: ${time}`);
        }
    
        const num = parseFloat(m[1]);
        if (m[2] === "f") {
            return Math.floor(num);
        } else if (m[2] === "s") {
            return Math.round(num * this.fps.num / this.fps.den);
        } else if (m[2] === "ms") {
            return Math.round(num * this.fps.num / this.fps.den / 1000.0);
        } else {
            return Math.round(num * this.fps.num / this.fps.den / 60000);
        }
    }

    public frameToMs(frame: number) {
        return 1000.0 * frame * this.fps.den / this.fps.num;
    }

    public frameToMsWithInScene(frame: number, scene: NimochTimelineScene) {
        return 1000.0 * (frame - scene.startFrame) * this.fps.den / this.fps.num;
    }

    public frameToTimeBaseInScene(frame: number, scene: NimochTimelineScene) {
        return 60000.0 * (frame - scene.startFrame) * this.fps.den / this.fps.num;
    }

    public getLength() {
        const lastScene = this.scenes[this.scenes.length - 1];

        return lastScene.endFrame;
    }

    public getScene(frame: number) {
        for (const s of this.scenes) {
            if (frame >= s.startFrame && frame < s.endFrame) {
                return s;
            }
        }

        return null;
    }
}
