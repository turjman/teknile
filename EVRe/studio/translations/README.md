# Translations

The Studio is written in English; the files here translate it. `evre_studio_ar.ts` is Arabic: every text of the
window, its messages and the Help pages. The build turns each `.ts` into a `.qm` (`lrelease`) and puts it into the
program; the language is chosen at the bottom of the sidebar and applied at the next start (STUDIO.md 14.4).

## After a change of the texts

```sh
cmake --build build --target update_translations   # lupdate over src/: new texts come in unfinished
```

Then translate them, in Qt Linguist (`linguist translations/evre_studio_ar.ts`) or in the file itself, and mark each
one finished. The GUI test fails while a message is unfinished or empty, or when a translation loses one of these:

- the placeholders `%1`, `%2` … (each as often as in the English) and `%n` (a count);
- the Help's markers `%CODE%` (a code block) and `%NAME%` (a number the Studio puts in);
- the HTML tags (`<b>`, `<code>`, `<li>` …), each as often as in the English.

## Arabic

- Every message a count is in (`%n`) has six forms: 0, 1, 2, 3–10, 11–99, and 100–102 (100, 101, 102 and the like).
  A text with two counts says them as labels instead: «الخطوط: %2 · الصفوف: %3».
- Numbers keep Western digits and a decimal point; units, symbols, register names, addresses, file names, code and the
  JSON keys stay as they are.
- A number with a Latin prefix or unit beside an Arabic word is laid out piece by piece, right to left: "10.0 k" comes
  out as "k 10.0", and "(+32 ppm)" loses its brackets' order. Such a piece goes between the characters U+2066 and
  U+2069 (a left-to-right isolate; invisible in the file) in the translation: `%1 k` in «%1 k عينة/ث», `(%2)` in
  «%1 (%2)». A line that starts with such a piece then still counts as Arabic, and sits at the right.
- The glossary used: register مسجّل, map خريطة, device جهاز, link وصلة, poll استطلاع, chart مخطط, line خط, sample
  عينة, cursor مؤشر, Log (the tab) السجل, Log (the column) تسجيل, Plot رسم, trigger القدح, arm تجهيز, hold-off (the trigger's) مهلة التجاهل, free running (Auto) جريان حر, edge (rising, falling) حافة, tag (a mark's label) وسم, handle (the level's) مقبض, range (a lane's) مدى, lane مسار, fold (a lane) طيّ, strip (a folded lane) شريحة, scroll bar شريط التمرير, separator (between lanes) خط فاصل, options (a lane's ⋯) خيارات, note ملاحظة,
  recording تسجيل, broadcast بث, slave التابع, token رمز الدخول, In flight قيد الإرسال, Auto send الإرسال التلقائي, Monitor (the tab) المراقبة, Decoded فك الترميز, fast stream تدفق سريع (Fast streams, the card: التدفقات السريعة), block (of a stream) كتلة, window (of a stream) نافذة, channel قناة, record (one instant of a stream's channels) سجلّ, lost (samples) مفقودة, fast line خط سريع, gap (in a line) فجوة, mapped (a file, into memory) يُربط بالذاكرة, cut off (a file) مقطوع, Streams (the Map settings page) التدفقات, summary (of a fast line's older samples) ملخص, Older samples (the Chart tab's choice) العينات الأقدم, ppm (kept as it is). Commands are verbal nouns (رسم المعروض, إعادة التشغيل الآن), not imperatives; a singular reads "واحد", not "%n" after the noun.
