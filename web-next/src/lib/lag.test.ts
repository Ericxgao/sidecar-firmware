import { describe, expect, it } from "vitest";
import { LAG_CMD, LagMarker } from "./lag";

describe("LagMarker (the Lag now button)", () => {
  it("sends the one fixed command, no text of the page's own", async () => {
    const sent: string[] = [];
    const m = new LagMarker(async (c) => void sent.push(c), () => 10_000);
    expect(await m.press()).toBe("sent");
    expect(sent).toEqual(["lagnow"]);
    expect(LAG_CMD).toBe("lagnow");
  });

  it("lets one press through a second, like the Sidecar does", async () => {
    let t = 5000;
    const sent: string[] = [];
    const m = new LagMarker(async (c) => void sent.push(c), () => t);
    expect(await m.press()).toBe("sent");
    t += 400;
    expect(await m.press()).toBe("too-soon");
    t += 599;
    expect(await m.press()).toBe("too-soon");
    t += 1;
    expect(await m.press()).toBe("sent");
    expect(sent.length).toBe(2);
    expect(m.count).toBe(2);
  });

  it("does not send a second one while the first is on its way", async () => {
    let release = () => {};
    const sent: string[] = [];
    const m = new LagMarker((c) => new Promise<void>((r) => { sent.push(c); release = r; }), () => 1000);
    const first = m.press();
    expect(await m.press()).toBe("too-soon");
    release();
    expect(await first).toBe("sent");
    expect(sent.length).toBe(1);
  });

  it("a refused or lost command does not use up the second, and says so", async () => {
    let fail = true;
    const m = new LagMarker(async () => { if (fail) throw new Error("not paired"); }, () => 1000);
    await expect(m.press()).rejects.toThrow("not paired");
    expect(m.count).toBe(0);
    fail = false;
    expect(await m.press()).toBe("sent");
  });
});
