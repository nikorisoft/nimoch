export interface NimochProjectConfig {
    id: string; // Auto-filled
    name: string;
    author: string;
    fps: NimochRationalNumber;
    width: number;
    height: number;
    timeline: NimochProjectTimeline[];
    inputs: NimochProjectInput[];
}

export interface NimochProjectTimeline {
    end: string;
    html: string;

    selector: string;
}

export interface NimochProjectInput {
    file: string;
    name: string;
    options: string[];
    stream: number;
}

export interface NimochRationalNumber {
    den: number;
    num: number;
}
