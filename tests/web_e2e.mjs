// End to end: the web flasher in a real Chrome, Web MIDI with SysEx granted, against the plugged-in FM-1.
import puppeteer from "puppeteer-core";
const shot = process.argv[2];
const browser = await puppeteer.launch({ executablePath: "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  headless: "new", args: ["--window-size=1280,1000"] });
const ctx = browser.defaultBrowserContext();
await ctx.overridePermissions("http://localhost:8913", ["midi", "midi-sysex"]);
const page = await browser.newPage();
await page.setViewport({ width: 1280, height: 1000 });
page.on("console", m => console.log("console:", m.text()));
await page.goto("http://localhost:8913/", { waitUntil: "networkidle0" });
await page.waitForFunction(() => document.getElementById("pkg-check").textContent.includes("CRCs"), { timeout: 10000 });
await page.click("#connect");
await page.waitForFunction(() => !/looking|not connected/.test(document.getElementById("dev-status").textContent), { timeout: 20000 });
console.log("device:", await page.$eval("#dev-status", e => e.textContent), "|", await page.$eval("#dev-id", e => e.textContent));
if (process.argv[3] === "flash") {
  await page.click("#flash");
  const t0 = Date.now();
  await page.waitForFunction(() => /flashed|stopped/.test(document.getElementById("dev-status").textContent), { timeout: 300000, polling: 1000 });
  console.log("result:", await page.$eval("#dev-status", e => e.textContent), "in", Math.round((Date.now() - t0) / 1000), "s");
}
console.log("log:\n" + await page.$eval("#log", e => e.innerText));
await page.screenshot({ path: shot });
await browser.close();
