// Renders report/report.html to report.pdf (A4) with headless Chromium.
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + path.resolve(__dirname, 'report.html'));
  await page.pdf({ path: path.resolve(__dirname, '..', 'report.pdf'), format: 'A4', printBackground: true,
                   preferCSSPageSize: true });
  await browser.close();
})();
