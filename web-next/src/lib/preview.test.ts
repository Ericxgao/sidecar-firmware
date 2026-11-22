// web-next/src/lib/preview.test.ts
import { describe, expect, it } from "vitest";
import { PreviewPlayer, clock, type AudioLike } from "./preview";

class FakeAudio implements AudioLike {
  preload = ""; autoplay = true; src = ""; currentTime = 0; duration = NaN; paused = true; ended = false;
  plays: string[] = [];
  private h: { [k: string]: (() => void)[] } = {};
  addEventListener(t: string, f: () => void) { (this.h[t] ??= []).push(f); }
  emit(t: string) { (this.h[t] ?? []).forEach((f) => f()); }
  play() { this.paused = false; this.plays.push(this.src); this.emit("play"); return Promise.resolve(); }
  pause() { if (!this.paused) { this.paused = true; this.emit("pause"); } }
  removeAttribute() { this.src = ""; }
  load() { /* nothing */ }
}
const mk = () => { const a = new FakeAudio(); const p = new PreviewPlayer(() => a); return { a, p }; };

describe("preview player", () => {
  it("makes no audio element, and fetches nothing, until a track is played; preload none, no autoplay", () => {
    let made = 0;
    const p = new PreviewPlayer(() => { made++; return new FakeAudio(); });
    expect(made).toBe(0);
    p.toggle("u1");
    expect(made).toBe(1);
    const a = new FakeAudio(); new PreviewPlayer(() => a).toggle("u"); expect(a.preload).toBe("none"); expect(a.autoplay).toBe(false);
  });
  it("one track plays at a time: starting another replaces the first", () => {
    const { a, p } = mk();
    p.toggle("u1");
    expect(p.state.url).toBe("u1"); expect(p.state.playing).toBe(true);
    p.toggle("u2");
    expect(p.state.url).toBe("u2"); expect(a.src).toBe("u2"); expect(a.plays).toEqual(["u1", "u2"]);
    expect(p.isPlaying("u1")).toBe(false); expect(p.isPlaying("u2")).toBe(true);
  });
  it("the same track pauses and resumes", () => {
    const { a, p } = mk();
    p.toggle("u1"); p.toggle("u1");
    expect(p.state.playing).toBe(false); expect(p.state.url).toBe("u1");
    p.toggle("u1");
    expect(p.state.playing).toBe(true); expect(a.plays).toEqual(["u1", "u1"]);
  });
  it("progress follows the audio, a seek moves it, and the end resets the track", () => {
    const { a, p } = mk();
    p.toggle("u1"); a.duration = 100; a.currentTime = 25; a.emit("timeupdate");
    expect(p.state.pos).toBe(250); expect(p.state.time).toBe("0:25 / 1:40");
    p.seek("u1", 500); expect(a.currentTime).toBe(50);
    p.seek("u2", 500); expect(a.currentTime).toBe(50); // not the loaded track: nothing moves
    a.ended = true; a.emit("ended");
    expect(p.state.url).toBe(""); expect(p.state.playing).toBe(false);
  });
  it("stop (list closed, filters changed, page left) silences and clears", () => {
    const { a, p } = mk();
    p.toggle("u1"); p.stop();
    expect(a.paused).toBe(true); expect(a.src).toBe(""); expect(p.state.url).toBe("");
  });
  it("a playback error is shown and the track is released", () => {
    const { a, p } = mk();
    p.toggle("u1"); a.emit("error");
    expect(p.state.error).toBe("could not play"); expect(p.state.playing).toBe(false); expect(p.state.url).toBe("");
  });
  it("clock", () => { expect(clock(0)).toBe("0:00"); expect(clock(65.9)).toBe("1:05"); expect(clock(NaN)).toBe("0:00"); });
});
