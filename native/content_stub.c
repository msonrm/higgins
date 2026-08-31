/* content.h の穴埋め（理由はそちらに書いた）。
   ★中身は読まれない —— `draw_logical()` を呼ばない限り触られないが、
     リンカは定義を要求する。 */
#include "content.h"

const unsigned short pool[1] = { 0 };
const Item citems[1] = { { 0, 0, 0, 0 } };
