# OAG MOBILE TOUCH GAMES V2 — BT / KM FIX1

[تحميل UF2 للبيكو 2W](https://github.com/chanchella/GP2040-CE/raw/refs/heads/chatgpt/oag-touch-games-v2-bt-km-fix1-817948d6/downloads/OAG_MOBILE_TOUCH_GAMES_V2_BT_KM_FIX1_Pico2W.uf2)

[تحميل السورس الكامل وUF2 — ZIP](https://github.com/chanchella/GP2040-CE/raw/refs/heads/chatgpt/oag-touch-games-v2-bt-km-fix1-817948d6/downloads/OAG_MOBILE_TOUCH_GAMES_V2_BT_KM_FIX1_FULL_SOURCE_AND_UF2.zip)

إصلاح تعريف الدراعات على أندرويد عبر البلوتوث، مع الحفاظ على تعريف السلكي. F4+F5 لمدة ثانية للدراعات فقط، وF4+F5+1 لمدة ثانية للدراعات مع الماوس والكيبورد الطبيعيين.

نجحت 70 مجموعة اختبار و6216 مقارنة بين تقارير BLE والسلكي و403 فحوص لواجهة التاتش وAPI. الاختبارات برمجية مع محاكاة العتاد؛ لم تختبر النسخة على بيكو وموبايل فعليين.

بعد التحميل، انسى اقتران OAG BT Output القديم ثم أعد الاقتران لتحديث تعريف HID.

حزمة ZIP تشمل UF2 وBIN وELF وخريطة الربط والاختبارات وسجلات التحقق ونسخ الرجوع. داخلها كل كود السورس ومكتبات البناء والاختبارات في Source.tar.xz؛ فكّه في نفس المجلد ليظهر مجلد Source. تم فحص ملفات السورس الـ9982 كلها ومقارنتها بالمصدر المستخدم للبناء. استُبعدت فقط 14 ملف مساعدة ويندوز CHM لمتحكمات أخرى؛ القائمة في SOURCE_AUDIT داخل الحزمة.

[بصمات SHA-256](downloads/SHA256SUMS.txt)
