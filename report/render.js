// Renders a report HTML file to PDF (A4) with headless Chromium.
// Usage: node report/render.js [input.html] [output.pdf]
// Defaults: report/report.html -> report.pdf
const { chromium } = require('playwright');
const path = require('path');
const input = path.resolve(process.argv[2] || path.join(__dirname, 'report.html'));
const output = path.resolve(process.argv[3] || path.join(__dirname, '..', 'report.pdf'));
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + input);
  await page.pdf({ path: output, format: 'A4', printBackground: true, preferCSSPageSize: true });
  await browser.close();
})();
