#pragma once
namespace tour {
inline constexpr char kCoursePage[] = R"HTML(<!doctype html>
<html lang="ja">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>コース設定 · M5StopWatch</title>
<style>
:root {color-scheme:dark; font:100%/1.5 "Noto Sans CJK JP", system-ui, sans-serif;
  background:#101114; color:#f5f5f7; accent-color:#77ceff}
* {box-sizing:border-box}
body {max-width:46rem; margin:auto; padding:clamp(1rem,4vw,2.5rem)}
header {border-bottom:1px solid #41434a; margin-bottom:1.5rem}
h1 {font-size:1.8rem; margin:0 0 .7rem}
h2 {font-size:1.1rem; font-weight:700}
fieldset {min-width:0; margin:0; padding:0; border:0}
label {display:block; color:#d4d6db}
input, button {font:inherit; min-height:44px; border-radius:.6rem}
input {display:block; width:100%; margin-top:.3rem; border:1px solid #71757f;
  padding:.5rem .7rem; background:#151619; color:#f5f5f7}
button {cursor:pointer; background:#292b30; color:inherit; border:1px solid #656972; padding:.4rem .85rem}
button:disabled {opacity:.4; cursor:default}
:focus-visible {outline:3px solid #77ceff; outline-offset:2px}
.heading, .controls {display:flex; align-items:center; justify-content:space-between; gap:.6rem}
.heading {margin-top:1.25rem}
output {color:#77ceff; font-variant-numeric:tabular-nums}
.segment {padding:.75rem 1rem 1rem; margin:0 0 .8rem; background:#24262b; border-radius:.8rem}
.segment label {font-size:.9rem}
.fields {display:grid; grid-template-columns:minmax(0,1fr) minmax(12rem,45%); gap:.65rem}
.timings {grid-template-columns:repeat(2,minmax(0,1fr))}
.duration legend {padding:0; color:#d4d6db}
.segment legend {font-size:.9rem}
.units {display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:.65rem}
.units label {display:flex; align-items:baseline; gap:.35rem}
.units input {min-width:0}
.controls {margin-bottom:.5rem}
.controls button {min-width:44px; padding:.25rem .6rem}
.actions {display:flex; gap:.35rem}
#save {margin-top:1.5rem; width:100%; font-weight:700; color:#061923; background:#77ceff; border:0}
#status {min-height:1.5em}
@media(max-width:32rem) {.fields {grid-template-columns:1fr}}
</style>
<main>
<header><h1>コース設定</h1></header>
<form id="form">
<fieldset id="editor" disabled>
<label>コース名<input id="course" required maxlength="96"></label>
<div class="heading"><h2>区間の配分</h2><output id="total" aria-label="標準時間"></output></div>
<div id="segments"></div>
<button type="button" id="add">＋ 区間を追加</button>
<h2>振動予告</h2>
<div class="fields timings">
<fieldset class="duration" id="overall"><legend>全体の終了前</legend></fieldset>
<fieldset class="duration" id="warning"><legend>区間の目安終了前</legend></fieldset>
</div>
<button type="submit" id="save">本体に保存</button>
</fieldset>
<p role="status" id="status">設定を読み込んでいます…</p>
<button type="button" id="retry" hidden>読み込みを再試行</button>
</form>
<template id="row">
<section class="segment">
<div class="controls"><strong class="number"></strong><div class="actions">
<button type="button" data-step="-1">↑</button><button type="button" data-step="1">↓</button>
<button type="button" data-remove>削除</button>
</div></div>
<div class="fields"><label>区間名<input class="name" required maxlength="96"></label>
<fieldset class="duration"><legend>目安時間</legend></fieldset></div>
</section>
</template>
<template id="durationFields">
<div class="units">
<label><input class="minutes" type="number" inputmode="numeric" min="0" step="1" placeholder="0">分</label>
<label><input class="seconds" type="number" inputmode="numeric" min="0" step="1" placeholder="0">秒</label>
</div>
</template>
</main>
<script>
'use strict';
const byId = id => document.getElementById(id);
const list = byId('segments'), course = byId('course'), editor = byId('editor');
const statusLine = byId('status'), bytes = new TextEncoder();
const rows = () => Array.from(list.children);
function showDuration(group, seconds) {
  const fields = byId('durationFields').content.cloneNode(true);
  if (seconds !== '') {
    fields.querySelector('.minutes').value = Math.floor(seconds / 60);
    fields.querySelector('.seconds').value = seconds % 60;
  }
  group.replaceChildren(group.querySelector('legend'), fields);
}
function readDuration(group) {
  const minutes = group.querySelector('.minutes'), seconds = group.querySelector('.seconds');
  minutes.setCustomValidity('');
  if (!minutes.validity.valid || !seconds.validity.valid) return NaN;
  const value = Number(minutes.value) * 60 + Number(seconds.value);
  if (value >= 1 && value <= 2147483647) return value;
  minutes.setCustomValidity(value === 0 ? '分か秒を入力して、1秒以上にしてください。'
                                       : '合計が2,147,483,647秒以内になるように入力してください。');
  return NaN;
}
function total() {
  const seconds = rows().reduce((sum, row) => sum + readDuration(row.querySelector('.duration')), 0);
  byId('total').value = Number.isSafeInteger(seconds) && seconds >= 0
    ? '標準 ' + Math.floor(seconds / 60) + '分' + seconds % 60 + '秒' : '標準 —';
}
function refresh() {
  const all = rows();
  all.forEach((row, i) => {
    const label = '区間 ' + (i + 1);
    row.querySelector('.number').textContent = label;
    row.setAttribute('aria-label', label);
    const [up, down, remove] = row.querySelectorAll('button');
    up.disabled = i === 0;
    down.disabled = i === all.length - 1;
    remove.disabled = all.length === 1;
    [up, down, remove].forEach((b, j) => b.setAttribute('aria-label', label + ['を上へ','を下へ','を削除'][j]));
  });
  byId('add').disabled = all.length >= 32;
  total();
}
function append(name, seconds) {
  const row = byId('row').content.firstElementChild.cloneNode(true);
  row.querySelector('.name').value = name;
  showDuration(row.querySelector('.duration'), seconds);
  list.append(row);
  return row;
}
function checkName(input) {
  const bad = !input.value.trim() || bytes.encode(input.value).length > 96 || /[\x00-\x1f\x7f]/.test(input.value);
  input.setCustomValidity(bad ? '名前は空白以外の文字を含むUTF-8の96バイト以内で入力してください。' : '');
}
function edited() { statusLine.textContent = '変更はまだ本体に保存されていません。'; }
byId('form').addEventListener('input', event => {
  if (event.target === course || event.target.matches('.name')) checkName(event.target);
  if (event.target.closest('.timings')) readDuration(event.target.closest('.duration'));
  total();
  edited();
});
byId('add').onclick = () => {
  const row = append('', '');
  refresh();
  row.querySelector('.name').focus();
  edited();
};
list.onclick = event => {
  const button = event.target.closest('button');
  if (!button) return;
  const row = button.closest('.segment');
  let focus = row;
  if (button.hasAttribute('data-remove')) {
    focus = row.nextElementSibling || row.previousElementSibling;
    row.remove();
  } else if (button.dataset.step === '-1') {
    list.insertBefore(row, row.previousElementSibling);
  } else {
    list.insertBefore(row.nextElementSibling, row);
  }
  refresh();
  focus.querySelector('.name').focus();
  edited();
};
async function load() {
  byId('retry').hidden = true;
  statusLine.textContent = '設定を読み込んでいます…';
  try {
    const response = await fetch('/course', {cache:'no-store'});
    if (!response.ok) throw Error();
    const [name, timings, ...lines] = (await response.text()).split('\n');
    course.value = name;
    const [overall, warning] = timings.split('\t');
    showDuration(byId('overall'), overall);
    showDuration(byId('warning'), warning);
    list.replaceChildren();
    for (const line of lines) {
      const [seconds, segmentName] = line.split('\t');
      append(segmentName, seconds);
    }
    if (!lines.length) append('', '');
    refresh();
    editor.disabled = false;
    statusLine.textContent = '';
  } catch {
    statusLine.textContent = '設定を読めませんでした。本体のWi-Fi接続を確認してください。';
    byId('retry').hidden = false;
  }
}
byId('retry').onclick = load;
byId('form').onsubmit = async event => {
  event.preventDefault();
  [course, ...list.querySelectorAll('.name')].forEach(checkName);
  const timings = [readDuration(byId('overall')), readDuration(byId('warning'))].join('\t');
  const segments = rows().map(row =>
    readDuration(row.querySelector('.duration')) + '\t' + row.querySelector('.name').value);
  if (!event.target.reportValidity()) return;
  const body = [course.value, timings, ...segments].join('\n');
  editor.disabled = true;
  statusLine.textContent = '本体に保存しています…';
  try {
    const response = await fetch('/course', {method:'PUT', body});
    const message = await response.text();
    statusLine.textContent = message || (response.ok ? '本体に保存しました。' : '本体に保存できませんでした。');
  } catch {
    statusLine.textContent = '保存結果を確認できません。本体のWi-Fiに接続して再度保存してください。';
  } finally {
    editor.disabled = false;
  }
};
load();
</script>
</html>)HTML";
}  // namespace tour
