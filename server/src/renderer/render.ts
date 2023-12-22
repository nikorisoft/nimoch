import { Browser, Locator, Page, chromium } from "playwright";
import { NicmClient, NicmInfo } from "./decoder";
import { NimochProjectInput, NimochRationalNumber } from ".";

export interface NimochRendererContext {
    locator: Locator;
    context: string;
}

export interface NimochDecoder {
    id: number;
    name: string;
    client: NicmClient;
    info: NicmInfo;
}

export class NimochRenderer {
    protected browser: Browser;
    protected page: Page;
    protected inputs: Record<string, NimochDecoder>;
    protected fps: NimochRationalNumber;
    public uuid: string;

    public static async Init(baseUrl: string, width: number, height: number, fps: NimochRationalNumber, inputs: NimochProjectInput[]) {
        const browser = await chromium.launch();
        const context = await browser.newContext({
            baseURL: baseUrl,
            viewport: {
                width,
                height
            }
        });
        const page = await context.newPage();

        const decoders: Record<string, NimochDecoder> = {};

        for (const i in inputs) {
            const input = inputs[i];
            const client = new NicmClient(input.file);
            const info = await client.info();

            decoders[input.name] = {
                id: parseInt(i),
                name: input.name,
                client,
                info
            };
        }

        return new NimochRenderer(browser, page, fps, decoders);
    }

    protected constructor(browser: Browser, page: Page, fps: NimochRationalNumber, inputs: Record<string, NimochDecoder>) {
        this.browser = browser;
        this.page = page;
        this.inputs = inputs;
        this.uuid = crypto.randomUUID();
        this.fps = fps;
    }

    public async close() {
        await this.page.close();
        await this.browser.close();

        for (const name in this.inputs) {
            const decoder = this.inputs[name];

            await decoder.client.quit();
        }
    }

    public async open(url: string, selector: string, context: string) {
        await this.page.goto(url);

        this.page.on("console", (msg) => {
            console.error(`[Browser] (${msg.type()}) ${msg.text()}`);
        });

        await this.page.waitForSelector(selector);

        await this.page.evaluate(`init("${this.uuid}", { den: ${this.fps.den.toString()}, num: ${this.fps.num.toString()} })`);

        return {
            locator: this.page.locator(selector),
            context
        };
    }

    public async callShowFrame(time: string) {
        await this.page.evaluate(`showFrame("${time}")`);
    }

    public getImage(context: NimochRendererContext) {
        return context.locator.screenshot();
    }

    public getDecoder(name: string): NimochDecoder | null{
        if (this.inputs[name] == null) {
            return null;
        } else {
            return this.inputs[name];
        }
    }

    public getFPS(): NimochRationalNumber {
        return this.fps;
    }
}
