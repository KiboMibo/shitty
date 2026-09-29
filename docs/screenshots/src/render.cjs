// Renders mockup.html into the README's PNGs:
//   NODE_PATH="$(npm root -g)" node docs/screenshots/src/render.cjs
const { chromium } = require('playwright');
const path = require('path');

(async () => {
  const page = 'file://' + path.join(__dirname, 'mockup.html');
  const browser = await chromium.launch();
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
  const tab = await context.newPage();
  for (const view of ['main', 'edge']) {
    await tab.goto(page + '?view=' + view, { waitUntil: 'networkidle' });
    await tab.evaluate(() => document.fonts.ready);
    await tab.screenshot({ path: path.join(__dirname, '..', view + '.png') });
  }
  await browser.close();
})();
