// Optional browser regression test: node tests/course-page.cjs (Playwright + Chromium).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

async function main() {
  const header = fs.readFileSync(path.join(__dirname, '../StopWatchTourGuide/src/CoursePage.h'), 'utf8');
  const html = header.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
  const browser = await chromium.launch({executablePath: process.env.CHROMIUM_PATH});
  try {
    const page = await browser.newPage({viewport: {width: 375, height: 812}});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    let saved = '施設案内\n120\t30\n420\t展示\n90\t体験';
    let writes = 0;
    await page.route('http://127.0.0.1/**', async route => {
      if (route.request().url().endsWith('/course')) {
        if (route.request().method() === 'PUT') {
          saved = route.request().postData();
          writes++;
          return route.fulfill({body: '本体に保存しました。', contentType: 'text/plain; charset=utf-8'});
        }
        return route.fulfill({body: saved, contentType: 'text/plain; charset=utf-8'});
      }
      return route.fulfill({body: html, contentType: 'text/html; charset=utf-8'});
    });
    await page.goto('http://127.0.0.1/');
    const groups = page.locator('#segments .duration');
    const first = groups.first(), second = groups.nth(1);
    const minutes = group => group.getByRole('spinbutton', {name: '分', exact: true});
    const seconds = group => group.getByRole('spinbutton', {name: '秒', exact: true});
    const values = group => Promise.all([minutes(group).inputValue(), seconds(group).inputValue()]);
    await page.waitForSelector('#editor:enabled');
    assert.deepEqual(await values(first), ['7', '0']);
    assert.deepEqual(await values(second), ['1', '30']);
    assert.deepEqual(await values(page.locator('#warning')), ['0', '30']);

    for (const width of [375, 960]) {
      await page.setViewportSize({width, height: 812});
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      for (const group of [first, page.locator('#overall')]) {
        const m = await minutes(group).boundingBox(), s = await seconds(group).boundingBox();
        assert(m.width >= 44 && s.width >= 44 && m.x + m.width <= s.x && s.x + s.width <= width);
      }
    }
    await minutes(first).fill('2');
    await minutes(first).press('Tab');
    assert(await seconds(first).evaluate(el => el === document.activeElement));
    await seconds(first).fill('15');
    await minutes(second).fill('');
    await seconds(second).fill('90');
    await seconds(page.locator('#overall')).fill('');
    await minutes(page.locator('#warning')).fill('1');
    assert.equal(await page.locator('#total').textContent(), '標準 3分45秒');
    const save = page.getByRole('button', {name: '本体に保存'});
    for (const bad of ['0', '-1', '1.5', '2147483648', '']) {
      await seconds(second).fill(bad);
      assert.equal(await page.locator('#form').evaluate(form => form.checkValidity()), false);
      await save.click();
      assert.equal(writes, 0);
    }
    await minutes(second).fill('35791394');
    await seconds(second).fill('7');
    assert(await page.locator('#form').evaluate(form => form.checkValidity()));
    await seconds(second).fill('8');
    assert.equal(await page.locator('#form').evaluate(form => form.checkValidity()), false);
    await minutes(second).fill('');
    await seconds(second).fill('90');
    await save.click();
    await page.getByText('本体に保存しました。', {exact: true}).waitFor();
    assert.equal(writes, 1);
    assert.equal(saved, '施設案内\n120\t90\n135\t展示\n90\t体験');
    await page.reload();
    await page.waitForSelector('#editor:enabled');
    assert.deepEqual(await values(first), ['2', '15']);
    assert.deepEqual(await values(second), ['1', '30']);
    assert.deepEqual(await values(page.locator('#warning')), ['1', '30']);
    assert.deepEqual(errors, []);
    console.log('PASS 分・秒の入力、合計、保存と再表示、入力上限、スマートフォン幅とPC幅');
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
